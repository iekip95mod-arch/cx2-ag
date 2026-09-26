#include "nps/steps/separable.h"

#include <utility>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/integrate.h"

namespace nps {
namespace {

const char kFamily[] = "calculus.ode.separable.first-order";
const char kConstant[] = "C";

enum class Inverse { None, Identity, Exponential, Reciprocal };

struct Separation {
    Arena &arena;
    Derivation &derivation;
    const Command &command;
    Meter meter;
    SeparableResult result;
    size_t mark;
    std::vector<std::string> assumptions;
    bool complete = false;

    Separation(Arena &a, Derivation &d, const Command &c, const Budget &budget)
        : arena(a), derivation(d), command(c), meter(budget), mark(d.mark()) {}

    bool work() { return !arena.failed() && meter.rewrite(); }

    std::string dependent_name() const { return arena.text(command.dependent); }

    void refuse(SeparableOutcome outcome, DerivationStatus status, const std::string &detail) {
        result.outcome = outcome;
        result.status = status;
        result.detail = detail;
    }

    void unsupported(const std::string &detail) {
        refuse(SeparableOutcome::UnsupportedForm, DerivationStatus::Unsupported, detail);
    }

    bool mentions(NodeId expression, const std::string &name) {
        std::vector<std::string> symbols;
        collect_symbols(arena, expression, &symbols);
        for (const std::string &symbol : symbols)
            if (symbol == name) return true;
        return false;
    }

    NodeId product(const std::vector<NodeId> &factors) {
        if (factors.empty()) return arena.integer("1");
        return factors.size() == 1 ? factors[0] : arena.nary(Kind::Mul, factors);
    }

    // Flattens products and negations into factors. False when one factor mixes both variables.
    bool partition(NodeId expression, std::vector<NodeId> *x_side, std::vector<NodeId> *y_side) {
        if (!work()) return false;
        const Kind kind = arena.at(expression).kind;
        if (kind == Kind::Mul) {
            for (NodeId factor : arena.children(expression))
                if (!partition(factor, x_side, y_side)) return false;
            return true;
        }
        if (kind == Kind::Neg) {
            x_side->push_back(arena.integer("-1"));
            return partition(arena.children(expression)[0], x_side, y_side);
        }
        const bool in_x = mentions(expression, command.variable_name);
        const bool in_y = mentions(expression, dependent_name());
        if (in_x && in_y) return false;
        (in_y ? y_side : x_side)->push_back(expression);
        return true;
    }

    NodeId substitute(NodeId expression, NodeId symbol, NodeId replacement) {
        if (!work()) return kNoNode;
        if (expression == symbol) return replacement;
        const ChildView children = arena.children(expression);
        if (children.empty()) return expression;
        std::vector<NodeId> replaced;
        for (NodeId child : children) {
            const NodeId next = substitute(child, symbol, replacement);
            if (next == kNoNode) return kNoNode;
            replaced.push_back(next);
        }
        const Kind kind = arena.at(expression).kind;
        return kind == Kind::Call ? arena.call(arena.text(expression), replaced)
                                  : arena.nary(kind, replaced);
    }

    // Reads logarithms and negative powers exactly rather than trusting the evaluator's refusal.
    bool defined(NodeId expression) {
        if (!work()) return false;
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        for (NodeId child : children)
            if (!defined(child)) return false;
        Rational value;
        if (kind == Kind::Call && arena.text(expression) == "ln")
            return evaluate_rational(arena, children[0], {}, &value) && value.num > 0;
        if (kind == Kind::Pow) {
            Rational exponent;
            if (!evaluate_rational(arena, children[1], {}, &exponent) || exponent.num >= 0)
                return true;
            return evaluate_rational(arena, children[0], {}, &value) && value.num != 0;
        }
        return true;
    }

    bool same(NodeId left, NodeId right) {
        return left != kNoNode && right != kNoNode && !arena.failed() &&
               canonicalize(arena, left) == canonicalize(arena, right);
    }

    VerificationRecord evidence(const char *method, VerificationOutcome outcome,
                                EvidenceStrength passing, const std::string &detail) {
        VerificationRecord record;
        record.method = method;
        record.outcome = outcome;
        record.strength = strength_for(outcome, passing);
        record.detail = detail;
        return record;
    }

    StepId transform(const char *rule, const std::string &title, ClaimType claim,
                     const char *obligation, const char *obligation_text, NodeId before,
                     NodeId after, const std::string &action, const std::string &reason,
                     const char *when, VerificationRecord verification) {
        if (before == kNoNode || after == kNoNode || arena.failed() || !meter.step()) return kNoStep;
        Step entry;
        entry.phase = "separate";
        entry.rule_id = rule;
        entry.rule_name = title;
        entry.goal = title;
        entry.claim = claim;
        entry.explanation_short = reason;
        entry.explanation_detailed = when;
        entry.proof_obligations.push_back({obligation, obligation_text});
        entry.verifications.push_back(std::move(verification));
        TransformationPayload change;
        change.before = before;
        change.after = after;
        change.concrete_action = action;
        return derivation.add_transformation(kNoStep, std::move(entry), std::move(change));
    }

    bool integrated(NodeId integrand, NodeId variable, const char *side, NodeId *out) {
        const IntegrateResult primitive =
            integrate_particular(arena, derivation, integrand, variable, meter);
        for (const std::string &assumption : derivation.context.active_assumptions)
            assumptions.push_back(assumption);
        if (primitive.outcome == IntegrateOutcome::Cancelled) {
            refuse(SeparableOutcome::Cancelled, DerivationStatus::Cancelled, primitive.detail);
            return false;
        }
        if (primitive.outcome == IntegrateOutcome::ResourceExceeded) {
            refuse(SeparableOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                   primitive.detail);
            return false;
        }
        if (primitive.outcome != IntegrateOutcome::Integrated ||
            primitive.status != DerivationStatus::SolvedAndVerified ||
            primitive.particular == kNoNode) {
            unsupported(std::string("the ") + side + " side has no verified antiderivative here: " +
                        primitive.detail);
            return false;
        }
        *out = primitive.particular;
        return true;
    }

    Inverse inverse_for(NodeId g) {
        const NodeId y = command.dependent;
        if (same(g, arena.integer("1"))) return Inverse::Identity;
        if (same(g, y)) return Inverse::Exponential;
        if (same(g, arena.binary(Kind::Pow, y, arena.integer("2")))) return Inverse::Reciprocal;
        return Inverse::None;
    }

    NodeId explicit_form(Inverse inverse, NodeId family) {
        switch (inverse) {
            case Inverse::Identity: return family;
            case Inverse::Exponential: return arena.call("exp", {family});
            case Inverse::Reciprocal:
                return arena.unary(Kind::Neg, arena.binary(Kind::Pow, family, arena.integer("-1")));
            case Inverse::None: break;
        }
        return kNoNode;
    }

    void solve() {
        const NodeId x = command.variable;
        const NodeId y = command.dependent;
        const NodeId slope = command.expression;
        const std::string &x_name = command.variable_name;
        const std::string y_name = dependent_name();
        if (x_name == kConstant || y_name == kConstant) {
            unsupported("C names the constant of integration, so it cannot be either variable");
            return;
        }
        std::vector<std::string> symbols;
        collect_symbols(arena, slope, &symbols);
        for (const std::string &symbol : symbols) {
            if (symbol != x_name && symbol != y_name && symbol != "pi" && symbol != "e") {
                unsupported("the right side may use only " + x_name + ", " + y_name +
                            " and numbers, and " + symbol + " is neither");
                return;
            }
        }
        std::vector<NodeId> x_side;
        std::vector<NodeId> y_side;
        if (!partition(slope, &x_side, &y_side)) {
            if (!arena.failed() && !meter.stopped())
                unsupported("the right side is not a product of a factor in " + x_name +
                            " alone and a factor in " + y_name + " alone");
            return;
        }
        const NodeId f = canonicalize(arena, product(x_side));
        const NodeId g = canonicalize(arena, product(y_side));
        const NodeId h = canonicalize(arena, arena.binary(Kind::Pow, g, arena.integer("-1")));
        const NodeId derivative = arena.call("diff", {y, x});
        const NodeId equation = arena.binary(Kind::Equals, derivative, slope);
        if (arena.failed()) return;

        PlanPayload plan;
        plan.strategy_id = "ode.separable.plan";
        plan.selected_strategy = "Separate the variables";
        plan.matched_problem_facts.push_back("the right side is " + print(arena, f) + " times " +
                                             print(arena, g));
        plan.selection_rationale =
            "a right side that splits into a factor of the independent variable and a factor of "
            "the dependent one can be separated and each side integrated on its own";
        Step plan_step;
        plan_step.phase = "plan";
        plan_step.goal = "Solve " + print(arena, equation) + " for " + y_name;
        plan_step.rule_id = plan.strategy_id;
        plan_step.rule_name = "Separation of variables";
        plan_step.claim = ClaimType::NoClaim;
        plan_step.explanation_short =
            "Move every " + y_name + " to one side and every " + x_name +
            " to the other, integrate both sides, then solve for " + y_name + " if the relation allows";
        register_strategy_precondition(
            plan, plan_step, "pre.ode.first-order-form",
            "the equation gives the first derivative of the dependent variable as an expression in "
            "the two variables",
            "structural reading of the equation", EvidenceStrength::StructurallyValid,
            VerificationOutcome::NotAttempted, "read from the command");
        register_strategy_precondition(
            plan, plan_step, "pre.ode.separable-factors",
            "the right side is a product of a factor in the independent variable alone and a factor "
            "in the dependent variable alone",
            "structural factor partition", EvidenceStrength::StructurallyValid,
            VerificationOutcome::NotAttempted, "read from the factors of the right side");
        if (!meter.step()) return;
        const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));
        derivation.complete_plan_precondition(plan_id, "pre.ode.first-order-form",
                                              VerificationOutcome::Passed,
                                              "one side is the first derivative of " + y_name);
        derivation.complete_plan_precondition(plan_id, "pre.ode.separable-factors",
                                              VerificationOutcome::Passed,
                                              "every factor mentions at most one of the variables");

        const bool divides = !same(g, arena.integer("1"));
        const std::string nonzero = print(arena, g) + " != 0";
        if (divides) assumptions.push_back(nonzero);
        const NodeId separated = arena.binary(Kind::Equals, arena.call("int", {h, y}),
                                              arena.call("int", {f, x}));
        if (transform("ode.separable.separate", "Separate the variables", ClaimType::Implication,
                      "obl.ode.separation-divides-nonzero",
                      "dividing by the dependent factor keeps every solution on which it is not zero",
                      equation, separated,
                      divides ? "Divide both sides by " + print(arena, g) + " and integrate each"
                              : "Integrate each side",
                      divides ? "Where " + nonzero + " the equation is the same as the separated one. "
                                "A constant solution with " + print(arena, g) + " = 0 is not reached"
                              : "The dependent factor is one, so nothing is divided",
                      "Reach for separation when the derivative is a product of a factor in the "
                      "independent variable alone and one in the dependent variable alone. Dividing "
                      "by the dependent factor is only valid where it is not zero, which is why a "
                      "constant solution at one of its zeros is lost here.",
                      evidence("structural factor partition", VerificationOutcome::Passed,
                               EvidenceStrength::StructurallyValid,
                               "the factors split into " + print(arena, f) + " and " +
                                   print(arena, g))) == kNoStep)
            return;

        NodeId upper = kNoNode;
        NodeId lower = kNoNode;
        if (!integrated(h, y, "dependent", &upper) || !integrated(f, x, "independent", &lower))
            return;
        const NodeId constant = arena.symbol(kConstant);
        const NodeId family = arena.binary(Kind::Add, lower, constant);
        const NodeId relation = arena.binary(Kind::Equals, upper, family);
        if (transform("ode.separable.integrate-both-sides", "Integrate both sides",
                      ClaimType::Implication, "obl.ode.integrals-differ-by-constant",
                      "two antiderivatives of equal differentials differ by one constant, and C "
                      "names it",
                      separated, relation, "Write both antiderivatives and one constant C",
                      "Two antiderivatives of equal differentials differ by a constant, so one C "
                      "covers both sides",
                      "Once both sides are separated each integral is an ordinary antiderivative. "
                      "Write one constant rather than one per side, because two free constants on "
                      "opposite sides are the same freedom counted twice.",
                      evidence("rule-local invariant", VerificationOutcome::Passed,
                               EvidenceStrength::StructurallyValid,
                               "C appears nowhere in either antiderivative")) == kNoStep)
            return;

        const Inverse inverse = inverse_for(g);
        NodeId general = relation;
        NodeId solved_for = kNoNode;
        if (inverse != Inverse::None) {
            solved_for = explicit_form(inverse, family);
            const NodeId stated = arena.binary(Kind::Equals, y, solved_for);
            if (inverse != Inverse::Identity) {
                const std::string how = inverse == Inverse::Exponential
                                            ? "Exponentiate both sides"
                                            : "Take the reciprocal of both sides and negate";
                if (transform("ode.separable.solve-explicit", "Solve for " + y_name,
                              ClaimType::SolutionSetPreserved, "obl.ode.explicit-inverts-relation",
                              "the explicit form has exactly the solutions of the relation on the "
                              "branch the integration recorded",
                              relation, stated, how,
                              "The left side is invertible on the recorded branch, so the relation "
                              "and the explicit form have the same solutions",
                              "Solve for the dependent variable when its side of the relation is a "
                              "logarithm or a reciprocal, whose inverses keep every solution on the "
                              "branch already recorded. Other left sides are left as the relation.",
                              evidence("inverse function on the recorded branch",
                                       VerificationOutcome::Passed,
                                       EvidenceStrength::StructurallyValid,
                                       how + " of " + print(arena, relation))) == kNoStep)
                    return;
            }
            general = stated;
        }

        NodeId answer = general;
        NodeId explicit_answer = solved_for;
        if (command.initial_point != kNoNode) {
            Rational point;
            Rational value;
            if (!evaluate_rational(arena, command.initial_point, {}, &point) ||
                !evaluate_rational(arena, command.initial_value, {}, &value)) {
                unsupported("the initial condition needs exact rational values");
                return;
            }
            Rational factor;
            if (evaluate_rational(arena, g, {{y_name, value}}, &factor) && factor.num == 0) {
                unsupported("the dependent factor is zero at the initial value, so the solution "
                            "through it is the constant one separation does not reach");
                return;
            }
            const NodeId x0 = command.initial_point;
            const NodeId y0 = command.initial_value;
            const NodeId upper_at = substitute(upper, y, y0);
            const NodeId lower_at = substitute(lower, x, x0);
            if (upper_at == kNoNode || lower_at == kNoNode) return;
            if (!defined(upper_at) || !defined(lower_at)) {
                unsupported("the initial point lies outside the branch the integration recorded");
                return;
            }
            const NodeId fixed = canonicalize(
                arena, arena.binary(Kind::Add, upper_at, arena.unary(Kind::Neg, lower_at)));
            answer = substitute(general, constant, fixed);
            if (solved_for != kNoNode) explicit_answer = substitute(solved_for, constant, fixed);
            const bool holds = same(upper_at, arena.binary(Kind::Add, lower_at, fixed));
            if (transform("ode.separable.initial-condition", "Use the initial condition",
                          ClaimType::Definition, "obl.ode.initial-condition-holds",
                          "the particular solution passes through the initial point", general,
                          answer,
                          "Substitute " + x_name + " = " + print(arena, x0) + " and " + y_name +
                              " = " + print(arena, y0) + ", which gives C = " + print(arena, fixed),
                          "The initial point picks the one member of the family that passes "
                          "through it",
                          "Use an initial condition after the general solution is written. Put "
                          "the point into the relation, read off the constant and check the "
                          "relation holds there before using it.",
                          evidence("exact substitution of the initial point",
                                   holds ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                                   EvidenceStrength::CandidateChecked,
                                   holds ? "the relation holds at the initial point"
                                         : "the relation does not hold at the initial point")) ==
                kNoStep)
                return;
            if (!holds) {
                refuse(SeparableOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                       "the constant the initial point gave does not satisfy the relation");
                return;
            }
            result.constant = fixed;
        }

        if (!check(explicit_answer, upper, lower, f, h)) return;
        result.outcome = SeparableOutcome::Solved;
        result.solution = answer;
        result.explicit_solution = explicit_answer != kNoNode;
    }

    // An explicit answer is differentiated into the equation and an implicit one side by side.
    bool check(NodeId explicit_answer, NodeId upper, NodeId lower, NodeId f, NodeId h) {
        const NodeId x = command.variable;
        const NodeId y = command.dependent;
        bool computed = false;
        bool agrees = false;
        std::string observed;
        const char *method = explicit_answer != kNoNode
                                 ? "differentiate the solution and substitute it into the equation"
                                 : "differentiate both sides of the relation";
        Derivation scratch;
        if (explicit_answer != kNoNode) {
            const DiffResult rate = differentiate(arena, scratch, explicit_answer, x, meter);
            const NodeId expected = substitute(command.expression, y, explicit_answer);
            computed = rate.derivative != kNoNode && expected != kNoNode;
            agrees = computed && same(rate.derivative, expected);
            if (computed) observed = print(arena, canonicalize(arena, rate.derivative));
        } else {
            const DiffResult left = differentiate(arena, scratch, upper, y, meter);
            const DiffResult right = differentiate(arena, scratch, lower, x, meter);
            computed = left.derivative != kNoNode && right.derivative != kNoNode;
            agrees = computed && same(left.derivative, h) && same(right.derivative, f);
            if (computed)
                observed = print(arena, canonicalize(arena, left.derivative)) + " and " +
                           print(arena, canonicalize(arena, right.derivative));
        }
        if (arena.failed() || meter.stopped() || !meter.step()) return false;
        Step entry;
        entry.phase = "check";
        entry.goal = "Check the solution against the equation";
        entry.rule_id = "ode.separable.check-solution";
        entry.rule_name = "Check by differentiation";
        entry.claim = ClaimType::EquivalentExpression;
        entry.explanation_short = explicit_answer != kNoNode
            ? "Differentiate the solution and compare it with the right side evaluated on it"
            : "Differentiate each side of the relation and compare with the separated factors";
        entry.proof_obligations.push_back(
            {"obl.ode.solution-satisfies-equation", "the solution satisfies the differential equation"});
        // Canonical forms can miss an equality but never invent one, so a mismatch is inconclusive.
        entry.verifications.push_back(evidence(
            method, agrees ? VerificationOutcome::Passed : VerificationOutcome::Inconclusive,
            EvidenceStrength::CandidateChecked,
            agrees ? "the derivative matches the equation"
                   : computed ? "the canonical forms did not match"
                              : "the solution could not be differentiated by rule"));
        CheckPayload payload;
        payload.target_claim = "the solution satisfies the differential equation";
        payload.check_method = method;
        payload.expected_relation = explicit_answer != kNoNode
                                        ? print(arena, command.expression) + " with the solution in place of " +
                                              arena.text(y)
                                        : print(arena, h) + " and " + print(arena, f);
        payload.observed_result = computed ? observed : "not differentiable by rule";
        derivation.add_check(kNoStep, std::move(entry), std::move(payload));
        if (agrees) return true;
        refuse(SeparableOutcome::Refused, DerivationStatus::Unsupported,
               "the solution could not be confirmed against the equation, so it is withheld");
        return false;
    }

    SeparableResult finish() {
        if (arena.failed() || meter.stopped()) {
            result.solution = kNoNode;
            result.constant = kNoNode;
            result.explicit_solution = false;
            const bool cancelled = meter.halt() == Halt::Cancelled;
            result.outcome = cancelled ? SeparableOutcome::Cancelled : SeparableOutcome::ResourceExceeded;
            result.status = cancelled ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached;
            result.detail = arena.failed() ? status_name(arena.status()) : halt_name(meter.halt());
            keep_verified_prefix(derivation, mark, arena);
        } else if (result.outcome == SeparableOutcome::Solved) {
            result.status = derivation.outcome_from(mark);
        } else {
            keep_verified_prefix(derivation, mark, arena);
        }
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = kFamily;
        context.requested_method =
            "separate the variables, integrate both sides, then solve for the dependent variable "
            "where the relation allows";
        context.original_expression = derivation.request.original_expression;
        if (complete && !arena.failed())
            context.normalized_problem_model = arena.binary(
                Kind::Equals, arena.call("diff", {command.dependent, command.variable}),
                command.expression);
        context.active_assumptions = assumptions;
        context.angle_convention = "radians";
        context.branch_convention = "real domain, principal values";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.numeric_mode = derivation.request.numeric_mode;
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        result.cost = meter.cost();
        return result;
    }
};

}  // namespace

const char *separable_outcome_name(SeparableOutcome outcome) {
    switch (outcome) {
        case SeparableOutcome::Solved: return "solved";
        case SeparableOutcome::UnsupportedForm: return "unsupported form";
        case SeparableOutcome::InvalidInput: return "invalid input";
        case SeparableOutcome::Refused: return "refused";
        case SeparableOutcome::VerificationFailed: return "verification failed";
        case SeparableOutcome::Cancelled: return "cancelled";
        case SeparableOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

SeparableResult solve_separable(Arena &arena, Derivation &derivation, const Command &command,
                                const Budget &budget) {
    Separation separation(arena, derivation, command, budget);
    const auto present = [&arena](NodeId node) { return node < arena.node_count(); };
    separation.complete =
        command.kind == CommandKind::Desolve && command.status == CommandStatus::Ready &&
        present(command.expression) && present(command.variable) && present(command.dependent) &&
        arena.at(command.variable).kind == Kind::Symbol &&
        arena.at(command.dependent).kind == Kind::Symbol &&
        command.variable_name == arena.text(command.variable) &&
        (command.initial_point == kNoNode) == (command.initial_value == kNoNode) &&
        (command.initial_point == kNoNode ||
         (present(command.initial_point) && present(command.initial_value)));
    if (!separation.complete) {
        separation.refuse(SeparableOutcome::InvalidInput, DerivationStatus::InvalidInput,
                          "a complete separable differential equation command is required");
    } else if (derivation.request.numeric_mode != NumericMode::Exact) {
        separation.unsupported("separable differential equations are solved in Exact mode only");
    } else if (separation.work()) {
        separation.solve();
    }
    return separation.finish();
}

}  // namespace nps
