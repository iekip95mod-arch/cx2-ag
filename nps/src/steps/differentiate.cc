#include "nps/steps/differentiate.h"

#include <optional>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/print.h"
#include "nps/steps/numeric_mode.h"

namespace nps {
namespace {

struct Context {
    Context(Arena &a, Derivation &d, NodeId v, Meter &m)
        : arena(a), derivation(d), variable(v), meter(m) {}

    Arena &arena;
    Derivation &derivation;
    NodeId variable;
    Meter &meter;
    bool failed = false;
    // Set when the refusal was capacity rather than a form with no rule. The arena cannot report it.
    bool exhausted = false;
    std::string detail;
    RestrictionSet restrictions;
};

// A halt is a refusal like any other as far as the recursion is concerned: it unwinds through the
// same failed flag, and the caller tells the two apart by asking the meter rather than the detail.
void halted(Context &ctx) {
    ctx.failed = true;
    ctx.detail = halt_name(ctx.meter.halt());
}

NodeId derive(Context &ctx, NodeId id, StepId parent);

Step envelope(const std::string &goal, const char *rule_id, const char *rule_name,
              const char *why) {
    Step s;
    s.phase = "differentiate";
    s.goal = goal;
    s.rule_id = rule_id;
    s.rule_name = rule_name;
    s.explanation_short = why;
    s.claim = ClaimType::EquivalentExpression;
    // Here rather than at each rule, because every rule in this file declares this one and no other.
    s.proof_obligations.push_back({"obl.calculus.rule-preserves-value",
                                   "the rewritten subexpression has the value the original had"});
    return s;
}

VerificationRecord rule_invariant(const char *detail) {
    VerificationRecord v;
    v.method = "rule-local invariant";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::StructurallyValid;
    v.detail = detail;
    return v;
}

// A rule holds only where the expression it rewrites has a value. Both sides are asked: the input
// carries what the form needs, and the answer carries what the rule introduced, which for the
// logarithm is the reciprocal it hands back where the input only needed a positive argument.
void carry_restrictions(Context &ctx, StepId here, NodeId before, NodeId after) {
    if (here == kNoStep)
        return;
    const NodeId sides[] = {before, after};
    for (NodeId side : sides) {
        if (side == kNoNode)
            continue;
        for (const Restriction &r : restrictions_of(ctx.arena, side))
            ctx.restrictions.add(r, here);
    }
}

void complete(Context &ctx, StepId here, NodeId after) {
    if (ctx.derivation.complete_transformation(here, after))
        carry_restrictions(ctx, here, kNoNode, after);
}

StepId record(Context &ctx, StepId parent, Step s, NodeId before, NodeId after,
              const std::string &action) {
    if (!ctx.meter.step()) {
        halted(ctx);
        return kNoStep;
    }
    TransformationPayload p;
    p.before = before;
    p.after = after;
    p.concrete_action = action;
    p.reversible = false;
    const StepId here = ctx.derivation.add_transformation(parent, std::move(s), std::move(p));
    carry_restrictions(ctx, here, before, after);
    return here;
}

// A leaf rule's answer for the caller. The rule's result only counts as the result if the rule was
// recorded: a halt inside record leaves the step out, so returning the value anyway puts a term in
// the derivative that no step accounts for. Every leaf goes through this rather than each one
// testing the step id, because the next leaf added would have to remember to.
NodeId leaf_result(Context &ctx, StepId parent, Step s, NodeId before, NodeId after,
                   const std::string &action) {
    return record(ctx, parent, std::move(s), before, after, action) == kNoStep ? kNoNode : after;
}

NodeId d_of(Arena &arena, NodeId id, NodeId variable) {
    std::vector<NodeId> args;
    args.push_back(id);
    args.push_back(variable);
    return arena.call("d", args);
}

// Division is Mul with a negative power, so this is the only shape a quotient can arrive in. The
// power comes back positive, which is the exponent the denominator carries once it is written the
// way a reader expects to see it.
bool reciprocal_factor(const Arena &arena, NodeId id, NodeId *base, int64_t *power) {
    const Node &n = arena.at(id);
    if (n.kind != Kind::Pow)
        return false;
    const ChildView c = arena.children(n);
    int64_t e;
    if (!small_integer(arena, c[1], &e) || e >= 0)
        return false;
    *base = c[0];
    *power = -e;
    return true;
}

NodeId call1(Arena &arena, const char *name, NodeId arg) {
    std::vector<NodeId> args;
    args.push_back(arg);
    return arena.call(name, args);
}

// The derivative of the named function with respect to its own argument, before the chain rule
// multiplies by the inner derivative. Returning kNoNode means the table does not have it, which is
// a refusal rather than a zero.
NodeId outer_derivative(Arena &arena, const std::string &name, NodeId u) {
    NodeId one = arena.integer("1");
    if (name == "sin")
        return call1(arena, "cos", u);
    if (name == "cos")
        return arena.unary(Kind::Neg, call1(arena, "sin", u));
    if (name == "tan")
        return arena.binary(Kind::Pow, call1(arena, "cos", u), arena.integer("-2"));
    if (name == "exp")
        return call1(arena, "exp", u);
    if (name == "ln")
        return arena.binary(Kind::Pow, u, arena.integer("-1"));
    if (name == "sqrt") {
        NodeId half = arena.binary(Kind::Mul, one, arena.binary(Kind::Pow, arena.integer("2"),
                                                                arena.integer("-1")));
        return arena.binary(Kind::Mul, half,
                            arena.binary(Kind::Pow, call1(arena, "sqrt", u), arena.integer("-1")));
    }
    return kNoNode;
}

const char *outer_rule_name(const std::string &name) {
    if (name == "sin" || name == "cos" || name == "tan")
        return "Trigonometric derivative";
    if (name == "exp")
        return "Exponential derivative";
    if (name == "ln")
        return "Logarithmic derivative";
    if (name == "sqrt")
        return "Square root derivative";
    return "Function derivative";
}

NodeId derive_constant(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    NodeId zero = a.integer("0");
    Step s = envelope("Differentiate " + print(a, id), "d.constant", "Constant rule",
                      "A quantity that does not change has derivative zero");
    s.explanation_detailed =
        "Reach for this whenever the expression has no occurrence of the variable in it, however "
        "complicated it looks: a plain number, pi, or a second letter being held fixed all qualify. "
        "A derivative measures how fast something changes as the variable moves, and none of these "
        "move at all, so the answer is zero.";
    s.verifications.push_back(rule_invariant("the expression contains no occurrence of the variable"));
    return leaf_result(ctx, parent, std::move(s), d_of(a, id, ctx.variable), zero,
                       "Replace the derivative of a constant with zero");
}

NodeId derive_variable(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    NodeId one = a.integer("1");
    Step s = envelope("Differentiate " + print(a, id), "d.variable", "Derivative of the variable",
                      "The variable changes at the same rate as itself");
    s.explanation_detailed =
        "Reach for this when the expression is the bare variable and nothing else. If x grows by "
        "some amount then x grows by exactly that amount, so the rate of change is one. It is the "
        "power rule with an exponent of one, written out separately because it comes up constantly "
        "as the last step of a longer derivative.";
    s.verifications.push_back(rule_invariant("the expression is the variable itself"));
    return leaf_result(ctx, parent, std::move(s), d_of(a, id, ctx.variable), one,
                       "Replace the derivative of the variable with one");
}

// A composite rule that failed partway may complete itself with the parts it has and the rest left
// as derivatives-of, which keeps the checked work below it: a record is only ever truncated, so a
// completed child cannot outlive an unfilled parent. One thing forbids it, that nothing landed
// below, where completing would record a restatement of the rule dressed as progress.
//
// A halt takes the same route as a form with no rule, which is a change from #32 and is what
// STEP-025 asks for read closely: it commands the verified prefix be preserved and forbids only a
// terminal answer for the original goal. A completed child under a halted parent is verified prefix,
// and the requirement has no clause excusing work that sits under one. The two differ in what the
// remainder means rather than in what the record keeps: unsupported names a form that has no rule,
// and a halt names one the meter did not reach, which the status and the detail line say and the
// mathematics does not need to. Either way the line written is true, because d/dx of a sum is the
// sum of the derivatives whether or not the second one was taken.
bool may_complete_partially(Context &ctx, StepId here) {
    return ctx.derivation.completed_transformation_after(here);
}

NodeId derive_sum(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    const ChildView terms = a.children(id);

    Step s = envelope("Differentiate " + print(a, id), "d.sum", "Sum rule",
                      "The derivative of a sum is the sum of the derivatives");
    s.explanation_detailed = "Each term is differentiated on its own and the results are added.";
    s.verifications.push_back(rule_invariant("differentiation is linear, so this holds term by term"));
    StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                         "Differentiate each term separately");

    std::vector<NodeId> parts;
    for (size_t i = 0; i < terms.size(); ++i) {
        NodeId dt = derive(ctx, terms[i], here);
        if (ctx.failed) {
            // PRD 808. The terms already differentiated are checked work, and dropping them costs a
            // student the part we could do. They cannot be kept under an unfilled parent, because a
            // record is only ever truncated and a completed child cannot outlive it, so the parent
            // is completed with what it honestly holds: the derivatives taken, then the remaining
            // terms left as derivatives-of. That is what a teacher writes before saying which one
            // has no rule, and it names the unsupported subproblem in the mathematics rather than
            // only in the detail line.
            if (!may_complete_partially(ctx, here))
                return kNoNode;
            for (size_t j = i; j < terms.size(); ++j)
                parts.push_back(d_of(a, terms[j], ctx.variable));
            complete(ctx, here, a.nary(Kind::Add, parts));
            return kNoNode;
        }
        parts.push_back(dt);
    }
    NodeId out = a.nary(Kind::Add, parts);
    complete(ctx, here, out);
    return out;
}

NodeId derive_quotient(Context &ctx, NodeId id, StepId parent, const std::vector<NodeId> &top,
                       const std::vector<NodeId> &bottom) {
    Arena &a = ctx.arena;
    NodeId u = top.size() == 1 ? top[0] : a.nary(Kind::Mul, top);
    NodeId v = bottom.size() == 1 ? bottom[0] : a.nary(Kind::Mul, bottom);

    Step s = envelope("Differentiate " + print(a, id), "d.quotient", "Quotient rule",
                      "Each part contributes to the derivative, with the two products subtracted "
                      "over the denominator squared");
    s.explanation_detailed =
        "For u over v the derivative is u' v minus u v', all over v squared. The subtraction "
        "comes from differentiating the reciprocal 1/v. The denominator must be nonzero.";
    s.verifications.push_back(rule_invariant("both the numerator and the denominator depend on the "
                                             "variable, so the quotient rule is the applicable one"));
    StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                         "Multiply the derivative of " + print(a, u) + " by " + print(a, v) +
                             ", subtract " + print(a, u) + " times the derivative of " + print(a, v) +
                             ", then divide by " + print(a, v) + " squared");

    NodeId du = derive(ctx, u, here);
    NodeId dv = ctx.failed ? kNoNode : derive(ctx, v, here);
    if (ctx.failed) {
        if (!may_complete_partially(ctx, here))
            return kNoNode;
        if (du == kNoNode)
            du = d_of(a, u, ctx.variable);
        if (dv == kNoNode)
            dv = d_of(a, v, ctx.variable);
    }

    NodeId numerator = a.binary(Kind::Add, a.binary(Kind::Mul, du, v),
                                a.unary(Kind::Neg, a.binary(Kind::Mul, u, dv)));
    NodeId out = a.binary(Kind::Mul, numerator, a.binary(Kind::Pow, v, a.integer("-2")));
    complete(ctx, here, out);
    return ctx.failed ? kNoNode : out;
}

NodeId derive_product(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    const ChildView factors = a.children(id);

    // Constant factors come out rather than going through the product rule, because "the two is
    // just along for the ride" is the explanation a reader wants, not u'v + uv' with v' = 0.
    std::vector<NodeId> constants;
    std::vector<NodeId> varying;
    for (NodeId f : factors) {
        if (depends_on(a, f, ctx.variable))
            varying.push_back(f);
        else
            constants.push_back(f);
    }

    if (varying.empty())
        return derive_constant(ctx, id, parent);

    if (!constants.empty()) {
        NodeId rest = varying.size() == 1 ? varying[0] : a.nary(Kind::Mul, varying);
        const std::vector<NodeId> &scale = constants;
        NodeId factor = scale.size() == 1 ? scale[0] : a.nary(Kind::Mul, scale);

        Step s = envelope("Differentiate " + print(a, id), "d.constant-multiple",
                          "Constant multiple rule",
                          "A constant factor stays where it is and the rest is differentiated");
        s.explanation_detailed =
            "Reach for this when one factor has no variable in it, as in 3 sin(x) or x squared over "
            "2. Scaling something by a fixed number scales its rate of change by the same number, "
            "so the constant can be lifted out of the way and put back afterwards. This is not the "
            "product rule: that one is for when both factors change.";
        s.verifications.push_back(rule_invariant("the factor taken out contains no occurrence of "
                                                 "the variable"));
        StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                             "Take " + print(a, factor) + " outside the derivative");

        NodeId inner = derive(ctx, rest, here);
        if (ctx.failed) {
            if (!may_complete_partially(ctx, here))
                return kNoNode;
            inner = d_of(a, rest, ctx.variable);
        }
        NodeId out = a.binary(Kind::Mul, factor, inner);
        complete(ctx, here, out);
        return ctx.failed ? kNoNode : out;
    }

    // A quotient needs a varying top and a varying bottom. A constant denominator has already left
    // through the constant multiple above, and a bare reciprocal is the power rule, which explains
    // it better than a quotient rule with a numerator of one.
    std::vector<NodeId> top;
    std::vector<NodeId> bottom;
    for (NodeId f : varying) {
        NodeId base;
        int64_t power;
        if (reciprocal_factor(a, f, &base, &power))
            bottom.push_back(power == 1 ? base
                                        : a.binary(Kind::Pow, base, a.integer(integer_text(power))));
        else
            top.push_back(f);
    }
    if (!top.empty() && !bottom.empty())
        return derive_quotient(ctx, id, parent, top, bottom);

    // Two or more varying factors: apply the product rule to the first against the rest, which
    // keeps the record binary and readable however many factors there are.
    NodeId u = varying[0];
    NodeId v = varying.size() == 2 ? varying[1]
                                   : a.nary(Kind::Mul, std::vector<NodeId>(varying.begin() + 1,
                                                                           varying.end()));

    Step s = envelope("Differentiate " + print(a, id), "d.product", "Product rule",
                      "Each changing factor contributes a term with the other factor unchanged");
    s.explanation_detailed =
        "For a product u times v the derivative is u' v plus u v', because each factor contributes "
        "its own rate of change while the other is held still.";
    s.verifications.push_back(rule_invariant("both factors depend on the variable, so the product "
                                             "rule is the applicable one"));
    StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                         "Differentiate each factor while keeping the other unchanged, then add the two products");

    NodeId du = derive(ctx, u, here);
    NodeId dv = ctx.failed ? kNoNode : derive(ctx, v, here);
    if (ctx.failed) {
        if (!may_complete_partially(ctx, here))
            return kNoNode;
        if (du == kNoNode)
            du = d_of(a, u, ctx.variable);
        if (dv == kNoNode)
            dv = d_of(a, v, ctx.variable);
    }

    NodeId out = a.binary(Kind::Add, a.binary(Kind::Mul, du, v), a.binary(Kind::Mul, u, dv));
    complete(ctx, here, out);
    return ctx.failed ? kNoNode : out;
}

NodeId derive_power(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    NodeId base = a.children(id)[0];
    NodeId exponent = a.children(id)[1];

    if (depends_on(a, exponent, ctx.variable)) {
        ctx.failed = true;
        ctx.detail = "an exponent that contains the variable needs logarithmic differentiation, "
                     "which is not implemented";
        return kNoNode;
    }

    int64_t n;
    if (!folded_integer(a, exponent, &n)) {
        ctx.failed = true;
        ctx.detail = "only an integer exponent is supported here";
        return kNoNode;
    }

    NodeId new_exponent = a.integer(integer_text(n - 1));
    NodeId outer = a.binary(Kind::Mul, a.integer(integer_text(n)),
                            a.binary(Kind::Pow, base, new_exponent));

    if (base == ctx.variable) {
        Step s = envelope("Differentiate " + print(a, id), "d.power", "Power rule",
                          "Bring the exponent down and reduce it by one");
        s.explanation_detailed =
            "Reach for this when the variable itself is raised to a fixed whole number, as in x "
            "squared or x cubed. The old exponent comes down to the front as a multiplier and the "
            "new one is smaller by one, so x to the n becomes n times x to the n minus one. A "
            "negative exponent works the same way, which is why one over x squared differentiates "
            "without a separate rule.";
        s.verifications.push_back(rule_invariant("the base is the variable and the exponent is a "
                                                 "constant integer"));
        return leaf_result(ctx, parent, std::move(s), d_of(a, id, ctx.variable), outer,
                           "Apply the power rule with exponent " + integer_text(n));
    }

    Step s = envelope("Differentiate " + print(a, id), "d.chain-power",
                      "Power rule with the chain rule",
                      "Differentiate the power, then multiply by the derivative of the base");
    s.explanation_detailed =
        "The base is itself a function of the variable, so the power rule alone is not enough: the "
        "result is multiplied by the rate at which the base changes.";
    s.verifications.push_back(rule_invariant("the base depends on the variable, so the chain rule "
                                             "applies"));
    StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                         "Apply the power rule to the base and then the chain rule");

    NodeId inner = derive(ctx, base, here);
    if (ctx.failed) {
        if (!may_complete_partially(ctx, here))
            return kNoNode;
        inner = d_of(a, base, ctx.variable);
    }
    NodeId out = a.binary(Kind::Mul, outer, inner);
    complete(ctx, here, out);
    return ctx.failed ? kNoNode : out;
}

NodeId derive_call(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    const std::string name = a.text(id);
    const ChildView args = a.children(id);
    if (args.size() != 1) {
        ctx.failed = true;
        ctx.detail = "only functions of one argument are supported here";
        return kNoNode;
    }

    NodeId u = args[0];
    NodeId outer = outer_derivative(a, name, u);
    if (outer == kNoNode) {
        ctx.failed = true;
        ctx.detail = "there is no derivative rule for " + name;
        return kNoNode;
    }

    if (u == ctx.variable) {
        Step s = envelope("Differentiate " + print(a, id), "d.function", outer_rule_name(name),
                          "Use the known derivative of this function");
        s.explanation_detailed =
            "Reach for this when a named function is applied to the bare variable, as in sin(x) "
            "rather than sin(2x). Each of these functions has a derivative worth learning by "
            "heart, and while the argument is the variable itself there is nothing further to "
            "account for. The moment the argument becomes anything else, the chain rule is the one "
            "that applies instead.";
        s.verifications.push_back(rule_invariant("the argument is the variable itself, so no chain "
                                                 "rule is needed"));
        return leaf_result(ctx, parent, std::move(s), d_of(a, id, ctx.variable), outer,
                           "Apply the derivative of " + name);
    }

    Step s = envelope("Differentiate " + print(a, id), "d.chain", "Chain rule",
                      "Differentiate the outer function, then multiply by the inner derivative");
    s.explanation_detailed =
        "The argument is itself a function of the variable, so the rate of change of the whole is "
        "the rate of the outer function times the rate of the inner one.";
    s.verifications.push_back(rule_invariant("the argument depends on the variable, so the chain "
                                             "rule applies"));
    StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                         "Apply the chain rule through " + name);

    NodeId inner = derive(ctx, u, here);
    if (ctx.failed) {
        if (!may_complete_partially(ctx, here))
            return kNoNode;
        inner = d_of(a, u, ctx.variable);
    }
    NodeId out = a.binary(Kind::Mul, outer, inner);
    complete(ctx, here, out);
    return ctx.failed ? kNoNode : out;
}

NodeId derive(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    if (ctx.failed || id == kNoNode)
        return kNoNode;
    if (!ctx.meter.rewrite()) {
        halted(ctx);
        return kNoNode;
    }

    if (!depends_on(a, id, ctx.variable))
        return derive_constant(ctx, id, parent);
    if (id == ctx.variable)
        return derive_variable(ctx, id, parent);

    switch (a.at(id).kind) {
        case Kind::Add:
            return derive_sum(ctx, id, parent);
        case Kind::Mul:
            return derive_product(ctx, id, parent);
        case Kind::Pow:
            return derive_power(ctx, id, parent);
        case Kind::Call:
            return derive_call(ctx, id, parent);
        case Kind::Neg: {
            // The same rule as a factor of -1, recorded so the sign change is traceable too.
            Step s = envelope("Differentiate " + print(a, id), "d.constant-multiple",
                              "Constant multiple rule",
                              "A minus sign stays where it is and the rest is differentiated");
            s.explanation_detailed =
                "Reach for this when the whole expression is negated. A minus sign is a constant "
                "factor of minus one, so it comes out of the way like any other constant and goes "
                "back on afterwards, which keeps the sign change visible rather than folded in.";
            s.verifications.push_back(rule_invariant("negation is multiplication by a constant"));
            StepId here = record(ctx, parent, std::move(s), d_of(a, id, ctx.variable), kNoNode,
                                 "Take the minus sign outside the derivative");
            NodeId inner = derive(ctx, a.children(id)[0], here);
            if (ctx.failed) {
                if (!may_complete_partially(ctx, here))
                    return kNoNode;
                inner = d_of(a, a.children(id)[0], ctx.variable);
            }
            NodeId out = a.unary(Kind::Neg, inner);
            complete(ctx, here, out);
            return ctx.failed ? kNoNode : out;
        }
        default:
            ctx.failed = true;
            ctx.detail = "there is no derivative rule for this form";
            return kNoNode;
    }
}

}  // namespace

const char *diff_outcome_name(DiffOutcome o) {
    switch (o) {
        case DiffOutcome::Differentiated: return "differentiated";
        case DiffOutcome::UnsupportedForm: return "unsupported form";
        case DiffOutcome::NotAVariable: return "not a variable";
        case DiffOutcome::Refused: return "refused";
        case DiffOutcome::Cancelled: return "cancelled";
        case DiffOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

namespace {

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, NumericMode mode) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "calculus.derivative.single-variable";
    inputs.requested_method = "differentiate by rule";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain, principal values";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.numeric_mode = mode;
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

}  // namespace

namespace {

// VER-004's second sentence, made into a question the code can answer: Giac checking a result Giac
// helped produce is corroboration and not a proof, so the rule asks whether anything already went to
// the backend. The whole derivation is in scope rather than this call's steps, because a derivation
// is one problem's record and a Giac-assisted rewrite feeding the expression in is exactly the
// suboperation the requirement means.
bool answer_owes_nothing_to_the_backend(const Derivation &derivation) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).backend_requests > 0)
            return false;
    }
    return true;
}

// Compares Giac's derivative with ours after canonicalizing both, which is what "after
// normalization" asks for: the two are the same answer written differently far more often than they
// disagree, and comparing printed forms would report that difference as a fault.
VerificationRecord giac_derivative_agrees(Arena &arena, Backend &giac, Meter &meter, NodeId subject,
                                          NodeId variable, NodeId ours, bool independent,
                                          bool *disagreed) {
    VerificationRecord v;
    v.method = "Giac Adapter Op::Differentiate compared after canonicalization";
    *disagreed = false;
    if (!meter.backend_call()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = halt_name(meter.halt());
        v.strength = strength_for(v.outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
        return v;
    }

    Adapter adapter(arena, giac);
    Request request;
    request.op = Op::Differentiate;
    request.target = subject;
    request.variable = variable;
    const Response response = adapter.run(request);
    if (!response.usable() || response.value == kNoNode || response.value >= arena.node_count()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = std::string("Giac returned ") + tag_name(response.tag) +
                   (response.detail.empty() ? "" : ": " + response.detail);
        v.strength = strength_for(v.outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
        return v;
    }

    const NodeId theirs = canonicalize(arena, response.value);
    const NodeId mine = canonicalize(arena, ours);
    if (theirs == kNoNode || mine == kNoNode) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = "one of the two derivatives could not be canonicalized, so nothing was compared";
        v.strength = strength_for(v.outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
        return v;
    }
    if (theirs != mine) {
        v.outcome = VerificationOutcome::Failed;
        v.strength = EvidenceStrength::Failed;
        v.detail = "Giac differentiated the same input to " + print(arena, theirs) +
                   " where the rules gave " + print(arena, mine);
        *disagreed = true;
        return v;
    }

    // Agreement is recorded as inconclusive rather than passed when Giac had already been consulted
    // below, which is VER-004 saying this is a cross-check and not an independent proof. The check
    // ran and it agreed; what it cannot do is settle a question against its own earlier answer.
    if (!independent) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = "Giac agrees, but Giac was also asked for part of the answer it is checking, so "
                   "this corroborates the result rather than proving it";
        // Set rather than derived, because the three Inconclusive records above are checks that
        // could not evaluate and this one is a check that ran and agreed. strength_for cannot tell
        // those apart, so it gives all four the Unsupported the other three want.
        v.strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
        return v;
    }
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
    v.detail = "Giac differentiated the same input and the two agree once both are canonicalized";
    return v;
}

}  // namespace

namespace {

DiffResult differentiate_body(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                              const Budget &budget, Backend *giac, Meter *shared_meter) {
    DiffResult result;
    // Read once, on entry, so a solve cannot change mode under itself. PLAT-013 needs the recorded
    // context to name the mode the derivation was actually produced in.
    const NumericMode mode = derivation.request.numeric_mode;
    if (expression == kNoNode || variable == kNoNode || arena.failed()) {
        result.detail = "nothing to differentiate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }
    if (arena.at(variable).kind != Kind::Symbol) {
        result.outcome = DiffOutcome::NotAVariable;
        result.detail = "the variable has to be a symbol";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }
    if (contains_list(arena, expression)) {
        result.outcome = DiffOutcome::UnsupportedForm;
        result.detail = "list and matrix differentiation is not supported";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }
    if (divides_by_zero(arena, expression)) {
        result.outcome = DiffOutcome::UnsupportedForm;
        result.detail = "the expression divides by zero, which has no value to differentiate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }
    // The same refusal for the conditions that are not division. A restriction says the answer holds
    // where the reader keeps a condition, and one nobody can keep is not a restriction, it is an
    // expression with no value. Recording ln(0) as needing 0 > 0 would be worse than either.
    if (has_unmeetable_condition(arena, expression)) {
        result.outcome = DiffOutcome::UnsupportedForm;
        result.detail = "the expression is undefined here, so there is nothing to differentiate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }

    std::optional<Meter> owned_meter;
    if (shared_meter == nullptr)
        shared_meter = &owned_meter.emplace(budget);
    Meter &meter = *shared_meter;
    const size_t mark = derivation.mark();

    const std::string name = arena.text(variable);
    PlanPayload plan;
    plan.strategy_id = "calculus.differentiate.rules";
    plan.selected_strategy = "Differentiate by rule";
    plan.matched_problem_facts.push_back("differentiate with respect to " + name);
    plan.selection_rationale =
        "each rule is applied to the form it matches and the parts are differentiated in turn, so "
        "every line of the result can be traced to the rule that produced it";

    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Differentiate " + print(arena, expression) + " with respect to " + name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Differentiate by rule";
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short = "Work outwards in, applying the rule that matches each form";
    register_strategy_precondition(
        plan, plan_step, "pre.differentiate.registered-rules",
        "every form in the expression has a derivative rule", "registered derivative rule dispatch",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked while traversing the expression");
    StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    Context ctx(arena, derivation, variable, meter);

    NodeId subject = expression;
    if (has_decimal(arena, expression)) {
        if (!meter.step()) {
            ctx.failed = true;
            ctx.detail = halt_name(meter.halt());
        } else {
            const ModeStep promoted = read_decimals_exactly(arena, derivation, plan_id,
                                                            "differentiate", expression, &subject);
            if (const char *why = promotion_refusal(promoted)) {
                ctx.failed = true;
                ctx.exhausted = promotion_exhausted(promoted);
                ctx.detail = why;
            }
        }
    }
    NodeId d = ctx.failed ? kNoNode : derive(ctx, subject, plan_id);

    // STEP-025: a halted solve keeps the rules that were checked and drops the rest. The
    // precondition is settled first, or the plan reads as unchecked and takes the prefix with it.
    // Scoped to what the traversal reached, because a form it never visited proves nothing: a form
    // with no rule is the refusal below, not this.
    if (meter.stopped()) {
        derivation.complete_plan_precondition(
            plan_id, "pre.differentiate.registered-rules", VerificationOutcome::Passed,
            "every form visited before the stop matched a registered derivative rule");
        const bool cancelled = meter.halt() == Halt::Cancelled;
        // A halt keeps steps, so a condition one of them needs has to reach it before the trim, the
        // same order the unsupported path settles in. This was a hole rather than a decision: the
        // halt path was written when a halt rewound to entry, and it kept its silence through #10,
        // which made a flat prefix survivable, and through this change, which makes a composite one
        // survivable too. A kept step with an unsettled restriction reads as unqualified.
        ctx.restrictions.settle(arena, derivation, nullptr);
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        result.outcome = cancelled ? DiffOutcome::Cancelled : DiffOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }

    if (ctx.failed || d == kNoNode) {
        // PRD 808: a derivation checked up to a named unsupported subproblem is worth keeping, and
        // a sum whose first term differentiated has one. Whether anything survives is decided in
        // the composite rules rather than here: a record is truncated, never spliced, so a rule
        // that gave up partway completes its own step with the remainder left as derivatives-of,
        // and one that got nowhere leaves a hole that cuts the prefix back to nothing. The
        // precondition is settled first for the same reason the halt path settles it, and scoped
        // the same way: a form that produced a recorded step did match a rule, and the one that did
        // not is this refusal rather than a counterexample to it.
        derivation.complete_plan_precondition(
            plan_id, "pre.differentiate.registered-rules", VerificationOutcome::Passed,
            "every form that produced a recorded step matched a registered derivative rule");
        // A kept step keeps the condition it needs, or it reads as unqualified when it is not.
        // Settled before the trim, so a condition on a dropped step goes with the step.
        ctx.restrictions.settle(arena, derivation, nullptr);
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        // The same distinction integrate draws: an arena that ran out while the derivative was
        // being built is a resource limit rather than an expression with no rule for it.
        const bool exhausted = arena.failed() || ctx.exhausted;
        result.outcome = exhausted ? DiffOutcome::ResourceExceeded : DiffOutcome::UnsupportedForm;
        result.detail = arena.failed()
                            ? std::string("the working space ran out before the derivative "
                                          "was built")
                        : ctx.detail.empty()
                            ? std::string("the expression contains a form with no rule")
                            : ctx.detail;
        result.status = exhausted ? DerivationStatus::ResourceLimitReached
                        : kept    ? DerivationStatus::PartiallySolved
                                  : DerivationStatus::Unsupported;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, result.status, mode);
        return result;
    }

    derivation.complete_plan_precondition(
        plan_id, "pre.differentiate.registered-rules", VerificationOutcome::Passed,
        "every visited form matched a registered derivative rule");

    // The third of three, because each of the paths above returns and each keeps steps of its own.
    ctx.restrictions.settle(arena, derivation, nullptr);

    // VER-004. Last, after the rules have finished and before the status is read, so a disagreement
    // reaches outcome_from below and the derivation is labelled by it rather than reporting a
    // verified answer with a failed check hanging off it.
    if (giac != nullptr) {
        const bool independent = answer_owes_nothing_to_the_backend(derivation);
        bool disagreed = false;
        VerificationRecord agreement = giac_derivative_agrees(arena, *giac, meter, subject, variable,
                                                              d, independent, &disagreed);

        Step check;
        check.kind = StepKind::Check;
        check.phase = "differentiate";
        check.goal = "Check the derivative against Giac";
        check.rule_id = "calculus.differentiate.giac-cross-check";
        check.rule_name = "Giac cross-check";
        check.claim = ClaimType::NoClaim;
        check.backend_requests = 1;
        check.explanation_short = "Giac differentiates the same input and the two answers are "
                                  "compared in canonical form";
        check.explanation_detailed =
            independent
                ? "Reach for this to confirm a derivative the rules produced on their own. Two "
                  "independent methods reaching the same answer is worth more than either alone."
                : "Giac was already asked for part of the working above, so this compares the "
                  "answer against the same source that helped produce it. It is worth having and "
                  "it is not a proof, which is why it is recorded as inconclusive rather than "
                  "passed.";
        check.proof_obligations.push_back(
            {"obl.differentiate.matches-backend",
             "the derivative the rules produced is the derivative Giac produces"});
        check.verifications.push_back(agreement);

        CheckPayload payload;
        payload.target_claim = print(arena, d) + " is the derivative of " + print(arena, subject);
        payload.check_method = "ask Giac to differentiate the same input and compare both answers "
                               "after canonicalization";
        payload.expected_relation = "the two derivatives canonicalize to the same expression";
        payload.observed_result = agreement.detail;
        derivation.add_check(plan_id, std::move(check), std::move(payload));
    }

    result.outcome = DiffOutcome::Differentiated;
    result.derivative = d;
    result.status = derivation.outcome_from(mark);

    // Decimal mode's answer line. Nothing above it left exact arithmetic, and this rewrites rather
    // than rounds, so the status it was given stands.
    if (mode == NumericMode::Decimal && result.status == DerivationStatus::SolvedAndVerified &&
        meter.step()) {
        NodeId reported = kNoNode;
        if (report_in_decimals(arena, derivation, plan_id, "differentiate", d, &reported) ==
            ModeStep::Recorded)
            result.derivative = reported;
    }
    if (meter.stopped()) {
        const bool cancelled = meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        result.outcome = cancelled ? DiffOutcome::Cancelled : DiffOutcome::ResourceExceeded;
        result.derivative = kNoNode;
        result.detail = halt_name(meter.halt());
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept ? DerivationStatus::Cancelled : DerivationStatus::NotRecorded;
    }
    result.cost = meter.cost();
    record_context(derivation, budget, expression, result.status, mode);
    return result;
}

}  // namespace

DiffResult differentiate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                         const Budget &budget, Backend *giac) {
    return differentiate_body(arena, derivation, expression, variable, budget, giac, nullptr);
}

DiffResult differentiate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                         Meter &meter, Backend *giac) {
    DiffResult result;
    if (meter.checkpoint()) {
        result = differentiate_body(arena, derivation, expression, variable, meter.budget(), giac, &meter);
    } else {
        const bool cancelled = meter.halt() == Halt::Cancelled;
        result.outcome = cancelled ? DiffOutcome::Cancelled : DiffOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = cancelled ? DerivationStatus::NotRecorded : DerivationStatus::ResourceLimitReached;
        record_context(derivation, meter.budget(), expression, result.status, derivation.request.numeric_mode);
    }
    result.cost = meter.cost();
    return result;
}

}  // namespace nps
