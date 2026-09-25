#include "nps/steps/rearrange.h"

#include <algorithm>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"

namespace nps {
namespace {

const size_t kSamples = 6;

struct Context {
    Context(Arena &a, Derivation &d, NodeId v, Meter &m)
        : arena(a), derivation(d), variable(v), meter(m) {}

    Arena &arena;
    Derivation &derivation;
    NodeId variable;
    Meter &meter;
    bool failed = false;
    std::string detail;
    RearrangeOutcome outcome = RearrangeOutcome::UnsupportedForm;
    std::vector<std::string> restrictions;
    std::vector<Restriction> conditions;
};

void refuse(Context &ctx, RearrangeOutcome outcome, const std::string &why) {
    ctx.failed = true;
    ctx.outcome = outcome;
    ctx.detail = why;
}

Coroutine<size_t> occurrences(TaskContext &task, const Arena &arena, NodeId id, NodeId variable) {
    co_await task.checkpoint();
    if (id == variable)
        co_return 1;
    size_t found = 0;
    for (NodeId child : arena.children(arena.at(id)))
        found += co_await occurrences(task, arena, child, variable);
    co_return found;
}

bool literal(const Arena &arena, NodeId id, int64_t value) {
    const Node &n = arena.at(id);
    return n.kind == Kind::Integer && n.small_valid && n.small == value;
}

NodeId gather(Arena &arena, Kind kind, const std::vector<NodeId> &parts) {
    if (parts.empty())
        return kNoNode;
    return parts.size() == 1 ? parts[0] : arena.nary(kind, parts);
}

NodeId negated(Arena &arena, NodeId id) { return arena.unary(Kind::Neg, id); }

NodeId reciprocal(Arena &arena, NodeId id) {
    return arena.binary(Kind::Pow, id, arena.unary(Kind::Neg, arena.integer("1")));
}

NodeId substitute(Arena &arena, NodeId id, NodeId variable, NodeId replacement) {
    if (id == variable)
        return replacement;
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(n);
    if (kids.empty())
        return id;
    std::vector<NodeId> rebuilt;
    bool changed = false;
    for (size_t i = 0; i < kids.size(); ++i) {
        const NodeId next = substitute(arena, kids[i], variable, replacement);
        changed = changed || next != kids[i];
        rebuilt.push_back(next);
    }
    if (!changed)
        return id;
    if (n.kind == Kind::Call)
        return arena.call(arena.text(id), rebuilt);
    return arena.nary(n.kind, rebuilt);
}

Step envelope(const std::string &goal, const char *rule_id, const char *rule_name,
              const std::string &why) {
    Step s;
    s.phase = "rearrange";
    s.goal = goal;
    s.rule_id = rule_id;
    s.rule_name = rule_name;
    s.explanation_short = why;
    s.claim = ClaimType::SolutionSetPreserved;
    return s;
}

VerificationRecord passed(const char *method, EvidenceStrength strength,
                          const std::string &detail) {
    VerificationRecord v;
    v.method = method;
    v.outcome = VerificationOutcome::Passed;
    v.strength = strength;
    v.detail = detail;
    return v;
}

// One inverse operation, applied to both sides and recorded. A rule that introduces a form hands it
// over as introduced and the form decides what it needs, so the rule chooses whether a condition
// arises and never chooses which node it is about. Walking the whole result instead would restate
// conditions the original already carried, which every later step would then repeat.
bool record_move(Context &ctx, StepId parent, Step s, NodeId before, NodeId after,
                 const std::string &action, NodeId introduced = kNoNode) {
    if (!ctx.meter.step()) {
        ctx.failed = true;
        ctx.outcome = RearrangeOutcome::Refused;
        ctx.detail = halt_name(ctx.meter.halt());
        return false;
    }
    TransformationPayload p;
    p.before = before;
    p.after = after;
    p.concrete_action = action;
    p.reversible = true;
    const StepId here = ctx.derivation.add_transformation(parent, std::move(s), std::move(p));
    RestrictionSet conditions;
    for (const Restriction &r : restrictions_of(ctx.arena, introduced)) {
        if (std::none_of(ctx.conditions.begin(), ctx.conditions.end(),
                         [&](const Restriction &held) { return subsumes(held, r); })) {
            conditions.add(r, here);
            ctx.conditions.push_back(r);
        }
    }
    std::vector<std::string> texts;
    conditions.settle(ctx.arena, ctx.derivation, &texts);
    for (const std::string &text : texts) {
        if (std::find(ctx.restrictions.begin(), ctx.restrictions.end(), text) == ctx.restrictions.end())
            ctx.restrictions.push_back(text);
    }
    return true;
}

std::string equation_text(Arena &arena, NodeId left, NodeId right) {
    return print(arena, left) + " = " + print(arena, right);
}

// The peel. Each turn strips one wrapper from the side holding the variable and applies its inverse
// to the other side, until the variable stands alone.
Coroutine<bool> isolate(TaskContext &task, Context &ctx, StepId parent, NodeId *left, NodeId *right) {
    Arena &a = ctx.arena;
    const std::string name = a.text(ctx.variable);

    while (!ctx.failed && *left != ctx.variable) {
        co_await task.checkpoint();
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RearrangeOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            co_return false;
        }

        const NodeId before = a.binary(Kind::Equals, *left, *right);
        // PERF-008's repeated canonical state, asked on the equation the turn starts from. A peel
        // that arrives back at a form it already stripped is going round rather than making
        // progress. Skipped when the arena is out, because there the node is missing rather than
        // repeated and the refusal below says so correctly.
        if (before != kNoNode && !ctx.meter.reached(before)) {
            ctx.failed = true;
            ctx.outcome = RearrangeOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            co_return false;
        }
        const Node &n = a.at(*left);
        switch (n.kind) {
            case Kind::Add: {
                std::vector<NodeId> rest;
                NodeId carrier = kNoNode;
                for (NodeId term : a.children(n)) {
                    if ((co_await occurrences(task, a, term, ctx.variable)) > 0)
                        carrier = term;
                    else
                        rest.push_back(term);
                }
                const NodeId moved = gather(a, Kind::Add, rest);
                if (carrier == kNoNode || moved == kNoNode) {
                    refuse(ctx, RearrangeOutcome::UnsupportedForm,
                           "the sum has no term this rule can move");
                    co_return false;
                }
                const NodeId next_right = a.binary(Kind::Add, *right, negated(a, moved));
                *left = carrier;
                *right = next_right;
                Step s = envelope("Isolate " + name, "alg.rearrange.subtract-both-sides",
                                  "Subtraction property of equality",
                                  "Taking the same terms from both sides keeps them equal");
                s.explanation_detailed =
                    "Every term on the left that does not contain " + name +
                    " is subtracted from both sides, which leaves the term holding " + name +
                    " on its own. The move is reversible, so nothing about the formula changes.";
                s.verifications.push_back(
                    passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                           "the same expression was subtracted from both sides"));
                s.proof_obligations.push_back(
                    {"obl.rearrange.same-solutions",
                     "the rearranged equation has the solutions the original had"});
                if (!record_move(ctx, parent, std::move(s), before,
                                 a.binary(Kind::Equals, *left, *right),
                                 "Subtract " + print(a, moved) + " from both sides"))
                    co_return false;
                break;
            }
            case Kind::Mul: {
                std::vector<NodeId> rest;
                NodeId carrier = kNoNode;
                for (NodeId factor : a.children(n)) {
                    if ((co_await occurrences(task, a, factor, ctx.variable)) > 0)
                        carrier = factor;
                    else
                        rest.push_back(factor);
                }
                const NodeId divisor = gather(a, Kind::Mul, rest);
                if (carrier == kNoNode || divisor == kNoNode) {
                    refuse(ctx, RearrangeOutcome::UnsupportedForm,
                           "the product has no factor this rule can divide out");
                    co_return false;
                }
                if (decide(a, divisor, Condition::NonZero) == Decision::Fails) {
                    refuse(ctx, RearrangeOutcome::Refused,
                           "the factor beside " + name +
                               " is zero, and dividing by it would not preserve the formula");
                    co_return false;
                }
                // A factor of one is what 1/x parses into, and dividing by it would read as a move
                // when nothing moves. Dropping it is its own rule so the shape change is still a
                // step somebody can follow.
                if (canonicalize(a, divisor) == a.integer("1")) {
                    *left = carrier;
                    Step s = envelope("Isolate " + name, "alg.rearrange.drop-unit-factor",
                                      "Multiplying by one changes nothing",
                                      "A factor of one can be left out");
                    s.explanation_detailed =
                        "The other factor is one, so it is dropped rather than divided out: "
                        "dividing by one would look like a move when nothing moves.";
                    s.verifications.push_back(
                        passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                               "the dropped factor folds to one"));
                    s.proof_obligations.push_back(
                        {"obl.rearrange.same-solutions",
                         "the rearranged equation has the solutions the original had"});
                    if (!record_move(ctx, parent, std::move(s), before,
                                     a.binary(Kind::Equals, *left, *right),
                                     "Drop the factor of one"))
                        co_return false;
                    break;
                }
                const NodeId next_right = a.binary(Kind::Mul, *right, reciprocal(a, divisor));
                *left = carrier;
                *right = next_right;
                const bool settled = decide(a, divisor, Condition::NonZero) == Decision::Holds;
                Step s = envelope("Isolate " + name, "alg.rearrange.divide-both-sides",
                                  "Division property of equality",
                                  "Dividing both sides by the same non-zero expression keeps them "
                                  "equal");
                s.explanation_detailed =
                    "The factors beside " + name +
                    " are divided out of both sides. Division needs a non-zero divisor, so a "
                    "divisor that is not a number is carried as a stated restriction rather than "
                    "assumed.";
                s.verifications.push_back(
                    passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                           settled ? "the divisor is a non-zero number"
                                   : "the divisor is recorded as non-zero and contains no " + name));
                s.proof_obligations.push_back(
                    {"obl.rearrange.same-solutions",
                     "the rearranged equation has the solutions the original had"});
                if (!record_move(ctx, parent, std::move(s), before,
                                 a.binary(Kind::Equals, *left, *right),
                                 "Divide both sides by " + print(a, divisor),
                                 reciprocal(a, divisor)))
                    co_return false;
                break;
            }
            case Kind::Neg: {
                const NodeId inner = a.children(n)[0];
                const NodeId next_right = negated(a, *right);
                *left = inner;
                *right = next_right;
                Step s = envelope("Isolate " + name, "alg.rearrange.negate-both-sides",
                                  "Multiplication property of equality",
                                  "Multiplying both sides by minus one keeps them equal");
                s.explanation_detailed =
                    "The minus sign in front of the left side is removed by multiplying both sides "
                    "by minus one, which is its own inverse.";
                s.verifications.push_back(passed(
                    "rule-local equality invariant", EvidenceStrength::StructurallyValid,
                    "both sides were multiplied by minus one"));
                s.proof_obligations.push_back(
                    {"obl.rearrange.same-solutions",
                     "the rearranged equation has the solutions the original had"});
                if (!record_move(ctx, parent, std::move(s), before,
                                 a.binary(Kind::Equals, *left, *right),
                                 "Multiply both sides by minus one"))
                    co_return false;
                break;
            }
            case Kind::Pow: {
                const NodeId base = a.children(n)[0];
                const NodeId exponent = a.children(n)[1];
                if ((co_await occurrences(task, a, exponent, ctx.variable)) > 0) {
                    refuse(ctx, RearrangeOutcome::UnsupportedForm,
                           name + " is in an exponent, which needs a logarithm rather than an "
                                  "inverse operation");
                    co_return false;
                }
                int64_t power = 0;
                if (!folded_integer(a, exponent, &power)) {
                    refuse(ctx, RearrangeOutcome::UnsupportedForm,
                           "only an integer power of " + name + " has an inverse here");
                    co_return false;
                }
                if (power == 1) {
                    *left = base;
                    Step s = envelope("Isolate " + name, "alg.rearrange.first-power",
                                      "A first power is the base",
                                      "Raising to the first power changes nothing");
                    s.explanation_detailed =
                        "A first power is the base itself, so the exponent is dropped before the "
                        "next inverse operation.";
                    s.verifications.push_back(
                        passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                               "the exponent is one"));
                    s.proof_obligations.push_back(
                        {"obl.rearrange.same-solutions",
                         "the rearranged equation has the solutions the original had"});
                    if (!record_move(ctx, parent, std::move(s), before,
                                     a.binary(Kind::Equals, *left, *right),
                                     "Drop the exponent of one"))
                        co_return false;
                    break;
                }
                if (power == -1) {
                    const Decision other_side = decide(a, *right, Condition::NonZero);
                    if (other_side == Decision::Fails) {
                        refuse(ctx, RearrangeOutcome::Refused,
                               "one over " + name +
                                   " is zero for no value, so the formula cannot be rearranged");
                        co_return false;
                    }
                    const bool settled = other_side == Decision::Holds;
                    const NodeId next_right = reciprocal(a, *right);
                    *left = base;
                    *right = next_right;
                    Step s = envelope("Isolate " + name, "alg.rearrange.reciprocal-both-sides",
                                      "Reciprocal of both sides",
                                      "Two equal non-zero expressions have equal reciprocals");
                    s.explanation_detailed =
                        "The left side is one over the part holding " + name +
                        ", so taking the reciprocal of both sides turns it the right way up. That "
                        "needs the other side to be non-zero, which is stated rather than assumed.";
                    s.verifications.push_back(
                        passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                               settled ? "the other side is a non-zero number"
                                       : "the other side is recorded as non-zero"));
                    s.proof_obligations.push_back(
                        {"obl.rearrange.same-solutions",
                         "the rearranged equation has the solutions the original had"});
                    if (!record_move(ctx, parent, std::move(s), before,
                                     a.binary(Kind::Equals, *left, *right),
                                     "Take the reciprocal of both sides", next_right))
                        co_return false;
                    break;
                }
                refuse(ctx, RearrangeOutcome::UnsupportedForm,
                       power % 2 == 0
                           ? "undoing an even power of " + name +
                                 " gives a positive and a negative root, and this rule records no "
                                 "branches"
                           : "undoing a power of " + name +
                                 " other than one needs a root, which this rule does not write");
                co_return false;
            }
            case Kind::Call:
                refuse(ctx, RearrangeOutcome::UnsupportedForm,
                       name + " is inside " + a.text(*left) +
                           ", and this rule has no inverse for that function");
                co_return false;
            default:
                refuse(ctx, RearrangeOutcome::UnsupportedForm,
                       "there is no inverse operation for this form");
                co_return false;
        }

        co_await task.checkpoint();
        if (a.failed()) {
            refuse(ctx, RearrangeOutcome::Refused, status_name(a.status()));
            co_return false;
        }
    }
    co_return !ctx.failed;
}

// Giac's answer to whether the substituted formula is an identity. It can fail an answer and never
// supplies one, which is what keeps the walkthrough generated by the rules above.
VerificationRecord backend_opinion(Arena &arena, Backend &giac, Meter &meter, NodeId difference,
                                   bool *disagreed) {
    VerificationRecord v;
    v.method = "Giac Adapter Op::IsZero on the substituted difference";
    *disagreed = false;
    if (!meter.backend_call()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = halt_name(meter.halt());
        return v;
    }
    Adapter adapter(arena, giac);
    Request request;
    request.op = Op::IsZero;
    request.target = difference;
    const Response response = adapter.run(request);
    if (!response.usable() || response.value == kNoNode ||
        response.value >= arena.node_count()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = std::string("Giac returned ") + tag_name(response.tag) +
                   (response.detail.empty() ? "" : ": " + response.detail);
        return v;
    }
    if (literal(arena, response.value, 0)) {
        v.outcome = VerificationOutcome::Passed;
        v.strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
        v.detail = "Giac reduced the substituted difference to zero";
        return v;
    }
    v.outcome = VerificationOutcome::Failed;
    v.strength = EvidenceStrength::Failed;
    v.detail = "Giac reduced the substituted difference to " + print(arena, response.value) +
               " rather than zero";
    *disagreed = true;
    return v;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    const std::vector<std::string> &restrictions, DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "algebra.formula-rearrangement.single-occurrence";
    inputs.requested_method = "inverse operations";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions = restrictions;
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

}  // namespace

NodeId substitute_symbol(Arena &arena, NodeId id, NodeId symbol, NodeId replacement) {
    return substitute(arena, id, symbol, replacement);
}

const char *rearrange_outcome_name(RearrangeOutcome o) {
    switch (o) {
        case RearrangeOutcome::Isolated: return "isolated";
        case RearrangeOutcome::NotAnEquation: return "not an equation";
        case RearrangeOutcome::NotAVariable: return "not a variable";
        case RearrangeOutcome::VariableAbsent: return "variable absent";
        case RearrangeOutcome::VariableRepeated: return "variable appears more than once";
        case RearrangeOutcome::UnsupportedForm: return "unsupported form";
        case RearrangeOutcome::VerificationFailed: return "verification failed";
        case RearrangeOutcome::Refused: return "refused";
        case RearrangeOutcome::Cancelled: return "cancelled";
        case RearrangeOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

Coroutine<RearrangeResult> rearrange_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                         NodeId equation, NodeId variable, Meter &meter, Budget budget,
                                         Backend *giac) {
    co_await task.checkpoint();
    RearrangeResult result;
    const size_t mark = derivation.mark();
    auto finish = [&]() {
        if (arena.failed()) {
            result.outcome = RearrangeOutcome::ResourceExceeded;
            result.formula = kNoNode;
            result.expression = kNoNode;
            result.status = DerivationStatus::ResourceLimitReached;
            result.detail = status_name(arena.status());
            derivation.context.derivation_status = result.status;
        }
        for (size_t i = mark; i < derivation.size(); ++i) {
            const Step &step = derivation.at(static_cast<StepId>(i));
            if (step.kind != StepKind::Plan && !step.verified())
                continue;
            for (const std::string &condition : step.domain_restrictions) {
                if (std::find(result.restrictions.begin(), result.restrictions.end(), condition) ==
                    result.restrictions.end())
                    result.restrictions.push_back(condition);
            }
        }
        derivation.context.active_assumptions = result.restrictions;
        result.cost = meter.cost();
        return std::move(result);
    };
    std::vector<std::string> none;
    if (equation == kNoNode || variable == kNoNode || arena.failed()) {
        result.detail = "nothing to rearrange";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (arena.at(equation).kind != Kind::Equals) {
        result.outcome = RearrangeOutcome::NotAnEquation;
        result.detail = "this rule rearranges a formula, and that is not one";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (arena.at(variable).kind != Kind::Symbol) {
        result.outcome = RearrangeOutcome::NotAVariable;
        result.detail = "the variable to isolate has to be a symbol";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (contains_list(arena, equation)) {
        result.outcome = RearrangeOutcome::UnsupportedForm;
        result.detail = "rearrangement requires a scalar equation";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (divides_by_zero(arena, equation)) {
        result.outcome = RearrangeOutcome::Refused;
        result.detail = "the formula divides by zero, which has no value to rearrange";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (has_unmeetable_condition(arena, equation)) {
        result.outcome = RearrangeOutcome::Refused;
        result.detail = "the formula is undefined here, so there is nothing to rearrange";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }

    const std::string name = arena.text(variable);
    const size_t found = co_await occurrences(task, arena, equation, variable);
    co_await task.checkpoint();
    if (found == 0) {
        result.outcome = RearrangeOutcome::VariableAbsent;
        result.detail = "the formula does not contain " + name;
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    if (found > 1) {
        result.outcome = RearrangeOutcome::VariableRepeated;
        result.detail = name + " appears " + std::to_string(found) +
                        " times, and collecting it into one term first is a rule this engine does "
                        "not have";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }

    PlanPayload plan;
    plan.strategy_id = "alg.rearrange.inverse-operations";
    plan.selected_strategy = "Undo the operations around the variable";
    plan.matched_problem_facts.push_back(name + " appears once in the formula");
    plan.selection_rationale =
        "each operation wrapped around " + name +
        " is undone on both sides in turn, innermost last, and every move is reversible, so the "
        "rearranged formula says the same thing as the one it came from";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Rearrange for " + name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Undo the operations around the variable";
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short = "Peel the operations off " + name + " one at a time";
    register_strategy_precondition(plan, plan_step, "pre.rearrange.single-occurrence",
                                   name + " appears exactly once", "occurrence count",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed,
                                   "one occurrence was found in the formula");
    register_strategy_precondition(plan, plan_step, "pre.rearrange.invertible-path",
                                   "every operation around " + name + " has an inverse rule",
                                   "registered inverse-operation dispatch",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked while peeling the formula");
    co_await task.checkpoint();
    if (!meter.step()) {
        derivation.rewind_to(mark);
        result.outcome = meter.halt() == Halt::Cancelled ? RearrangeOutcome::Cancelled
                                                         : RearrangeOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = meter.halt() == Halt::Cancelled ? DerivationStatus::NotRecorded
                                                        : DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    Context ctx(arena, derivation, variable, meter);
    ctx.conditions = restrictions_of(arena, equation);
    RestrictionSet original_conditions;
    for (const Restriction &condition : ctx.conditions)
        original_conditions.add(condition, plan_id);
    original_conditions.settle(arena, derivation, &ctx.restrictions);
    NodeId left = arena.children(equation)[0];
    NodeId right = arena.children(equation)[1];

    if ((co_await occurrences(task, arena, right, variable)) > 0) {
        std::swap(left, right);
        Step s = envelope("Put " + name + " on the left", "alg.rearrange.swap-sides",
                          "Symmetry of equality",
                          "An equation reads the same either way round");
        s.explanation_detailed =
            "Equality is symmetric, so the side holding " + name +
            " can be written first without changing what the formula says.";
        s.verifications.push_back(
            passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                   "the two sides were exchanged unchanged"));
        s.proof_obligations.push_back({"obl.rearrange.same-solutions",
                                       "the rearranged equation has the solutions the original had"});
        if (!record_move(ctx, plan_id, std::move(s), equation,
                         arena.binary(Kind::Equals, left, right),
                         "Write the side holding " + name + " first")) {
            derivation.rewind_to(mark);
            result.outcome = meter.halt() == Halt::Cancelled ? RearrangeOutcome::Cancelled
                                                             : RearrangeOutcome::ResourceExceeded;
            result.detail = halt_name(meter.halt());
            result.status = meter.halt() == Halt::Cancelled ? DerivationStatus::NotRecorded
                                                            : DerivationStatus::ResourceLimitReached;
            result.cost = meter.cost();
            record_context(derivation, budget, equation, none, result.status);
            co_return finish();
        }
    }

    co_await isolate(task, ctx, plan_id, &left, &right);
    co_await task.checkpoint();

    if (meter.stopped()) {
        // Checked moves survive without asserting the unvisited inverse path.
        const bool cancelled = meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena, true);
        result.outcome = cancelled ? RearrangeOutcome::Cancelled
                                   : RearrangeOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        result.cost = meter.cost();
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }

    if (ctx.failed || arena.failed()) {
        if (ctx.failed && !arena.failed())
            derivation.complete_plan_precondition(plan_id, "pre.rearrange.invertible-path",
                                                  VerificationOutcome::Failed, ctx.detail);
        keep_verified_prefix(derivation, mark, arena, true);
        result.outcome = ctx.outcome;
        result.detail = ctx.detail.empty() ? "the formula holds a form with no inverse rule"
                                           : ctx.detail;
        result.status = result.outcome == RearrangeOutcome::Refused
                            ? DerivationStatus::InvalidInput
                            : DerivationStatus::Unsupported;
        result.cost = meter.cost();
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }

    derivation.complete_plan_precondition(plan_id, "pre.rearrange.invertible-path",
                                          VerificationOutcome::Passed,
                                          "every operation met a registered inverse rule");

    const NodeId formula = arena.binary(Kind::Equals, left, right);
    co_await task.checkpoint();
    const NodeId substituted_left = substitute(arena, arena.children(equation)[0], variable, right);
    co_await task.checkpoint();
    const NodeId substituted_right = substitute(arena, arena.children(equation)[1], variable, right);
    co_await task.checkpoint();
    const SampleAgreement agreement =
        agrees_on_samples(arena, substituted_left, substituted_right, kSamples);

    bool backend_disagreed = false;
    VerificationRecord backend;
    const bool asked_backend = giac != nullptr;
    if (asked_backend) {
        co_await task.checkpoint();
        const NodeId difference =
            arena.binary(Kind::Add, substituted_left, negated(arena, substituted_right));
        backend = backend_opinion(arena, *giac, meter, difference, &backend_disagreed);
    }

    co_await task.checkpoint();
    if (!meter.step()) {
        keep_verified_prefix(derivation, mark, arena, true);
        const bool cancelled = meter.halt() == Halt::Cancelled;
        result.outcome = cancelled ? RearrangeOutcome::Cancelled
                                   : RearrangeOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = cancelled ? DerivationStatus::NotRecorded
                                  : DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, equation, none, result.status);
        co_return finish();
    }


    Step check;
    check.phase = "check";
    check.goal = "Check the rearrangement";
    check.rule_id = "alg.rearrange.check-by-substitution";
    check.rule_name = "Check by substitution";
    check.claim = ClaimType::SolutionSetPreserved;
    check.explanation_short = "Put the rearranged right side back where " + name + " was";
    check.explanation_detailed =
        "The rearranged formula is right when putting its right side back where " + name +
        " stood leaves the two sides of the original equal. Both sides are worked out at several "
        "exact values, which can show the two differ but cannot on its own prove they agree "
        "everywhere, so a backend is asked as well when one is supplied.";
    check.proof_obligations.push_back({"obl.rearrange.substitution-identity",
                                       "the original formula holds once " + name +
                                           " is replaced by the rearranged right side"});

    VerificationRecord sampled;
    sampled.method = "exact evaluation at " + std::to_string(kSamples) + " rational assignments";
    if (agreement.evaluated == 0) {
        sampled.outcome = VerificationOutcome::Inconclusive;
        sampled.detail = "no assignment gave both sides a value, so nothing was compared";
    } else if (agreement.agreed == agreement.evaluated) {
        sampled.outcome = VerificationOutcome::Passed;
        sampled.detail = "the two sides took the same value at all " +
                         std::to_string(agreement.evaluated) +
                         " assignments that could be worked out, which is evidence of the identity "
                         "rather than a proof of it";
    } else {
        sampled.outcome = VerificationOutcome::Failed;
        sampled.detail = "the two sides differed: " + agreement.disagreement;
    }
    sampled.strength = strength_for(sampled.outcome, EvidenceStrength::NumericallyCorroborated);
    check.verifications.push_back(sampled);
    if (asked_backend)
        check.verifications.push_back(backend);

    CheckPayload payload;
    payload.target_claim = equation_text(arena, arena.children(equation)[0],
                                         arena.children(equation)[1]) +
                           " holds with " + name + " replaced by " + print(arena, right);
    payload.check_method = asked_backend
                               ? "substitute the rearranged side back, evaluate both sides exactly, "
                                 "and ask Giac whether the difference is zero"
                               : "substitute the rearranged side back and evaluate both sides "
                                 "exactly at several assignments";
    payload.expected_relation = "the two sides of the original formula stay equal";
    payload.observed_result =
        sampled.outcome == VerificationOutcome::Failed
            ? "the sides differed: " + agreement.disagreement
            : sampled.outcome == VerificationOutcome::Inconclusive
                  ? "no assignment could be worked out"
                  : "the sides agreed at all " + std::to_string(agreement.evaluated) +
                        " assignments";
    if (asked_backend)
        payload.observed_result += ", and " + backend.detail;
    derivation.add_check(plan_id, std::move(check), std::move(payload));

    result.cost = meter.cost();
    result.restrictions = ctx.restrictions;

    if (sampled.outcome == VerificationOutcome::Failed || backend_disagreed) {
        // The record stays so the failing check is readable, and the answer is withheld: section 17
        // does not offer a result that failed its own check.
        result.outcome = RearrangeOutcome::VerificationFailed;
        result.detail = sampled.outcome == VerificationOutcome::Failed
                            ? "the rearranged formula failed its own substitution check"
                            : "Giac and the rearranged formula disagree, so no answer is offered";
        result.status = DerivationStatus::VerificationFailed;
        record_context(derivation, budget, equation, ctx.restrictions, result.status);
        co_return finish();
    }

    result.outcome = RearrangeOutcome::Isolated;
    result.formula = formula;
    result.expression = right;
    // A restriction only qualifies an answer the checks agreed with, so an unchecked or disagreed
    // outcome outranks it rather than being overwritten by it.
    result.status = derivation.outcome_from(mark);
    if (result.status == DerivationStatus::SolvedAndVerified && !ctx.restrictions.empty())
        result.status = DerivationStatus::ConditionallySolved;
    record_context(derivation, budget, equation, ctx.restrictions, result.status);
    co_return finish();
}

RearrangeResult rearrange(Arena &arena, Derivation &derivation, NodeId equation, NodeId variable,
                          const Budget &budget, Backend *giac) {
    return rearrange(arena, derivation, equation, variable, budget, giac, 262144);
}

RearrangeResult rearrange(Arena &arena, Derivation &derivation, NodeId equation, NodeId variable,
                          const Budget &budget, Backend *giac, size_t frame_bytes) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    std::vector<std::byte> storage(frame_bytes);
    TaskContext context(storage);
    auto task = make_task(context, rearrange_steps, arena, derivation, equation, variable, meter, budget, giac);
    while (task.state() == TaskState::Pending)
        task.advance(1024);
    if (task.result())
        return *task.result();
    RearrangeResult refused;
    refused.outcome = RearrangeOutcome::ResourceExceeded;
    refused.status = DerivationStatus::ResourceLimitReached;
    refused.detail = "coroutine frame storage exhausted";
    refused.cost = meter.cost();
    keep_verified_prefix(derivation, mark, arena, true);
    for (size_t i = mark; i < derivation.size(); ++i) {
        for (const std::string &condition : derivation.at(static_cast<StepId>(i)).domain_restrictions) {
            if (std::find(refused.restrictions.begin(), refused.restrictions.end(), condition) ==
                refused.restrictions.end())
                refused.restrictions.push_back(condition);
        }
    }
    record_context(derivation, budget, equation, refused.restrictions, refused.status);
    return refused;
}

}  // namespace nps
