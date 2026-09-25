#include "nps/steps/integrate.h"

#include "nps/cas/giac_adapter.h"
#include "nps/core/canonical.h"
#include "nps/core/checked.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/numeric_mode.h"
#include "nps/steps/rewrite.h"
#include "nps/core/print.h"

namespace nps {
namespace {

struct Context {
    Context(Arena &a, Derivation &d, NodeId v, Meter &m, NumericMode n,
            std::optional<Rational> point)
        : arena(a), derivation(d), variable(v), meter(m), mode(n), branch_point(point) {}

    Arena &arena;
    Derivation &derivation;
    NodeId variable;
    Meter &meter;
    NumericMode mode;
    std::optional<Rational> branch_point;
    bool failed = false;
    // Set when the refusal was capacity rather than a form with no rule. The arena cannot report it.
    bool exhausted = false;
    std::string detail;
    // Every restriction a rule recorded, so the context can list what the answer assumes.
    std::vector<std::string> assumptions;
    RestrictionSet recorded;
};

// A halt is a refusal like any other as far as the recursion is concerned: it unwinds through the
// same failed flag, and the caller tells the two apart by asking the meter rather than the detail.
void halted(Context &ctx) {
    ctx.failed = true;
    ctx.detail = halt_name(ctx.meter.halt());
}

// The first reason wins, the way Arena::fail keeps the first status. Unwinding only ever adds outer
// and less specific reasons, and a rule that already said exactly what stopped it should not have
// that replaced by its caller's guess at what shape it was.
void refuse(Context &ctx, const std::string &why) {
    if (ctx.failed)
        return;
    ctx.failed = true;
    ctx.detail = why;
}

NodeId antiderive(Context &ctx, NodeId id, StepId parent);

Step envelope(const std::string &goal, const char *rule_id, const std::string &rule_name,
              const char *why) {
    Step s;
    s.phase = "integrate";
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

VerificationRecord rule_invariant(const std::string &detail) {
    VerificationRecord v;
    v.method = "rule-local invariant";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::StructurallyValid;
    v.detail = detail;
    return v;
}

// The same question the differentiator asks at the same point: what does this rule's own input and
// its own answer need before either has a value. The logarithm's positive argument and the
// substitution's non-zero coefficient were both written out here by hand before the predicate
// existed, and both now come from the form rather than from remembering to say so.
void carry_restrictions(Context &ctx, StepId here, NodeId before, NodeId after) {
    if (here == kNoStep)
        return;
    const NodeId sides[] = {before, after};
    for (NodeId side : sides) {
        if (side == kNoNode)
            continue;
        for (const Restriction &r : restrictions_of(ctx.arena, side))
            ctx.recorded.add(r, here);
    }
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

// Differentiate's helper of the same name, and for the same reason: a leaf rule whose record halted
// has no result to hand back, and returning one puts a term in the answer that no step accounts for.
NodeId leaf_result(Context &ctx, StepId parent, Step s, NodeId before, NodeId after,
                   const std::string &action) {
    return record(ctx, parent, std::move(s), before, after, action) == kNoStep ? kNoNode : after;
}

NodeId int_of(Arena &arena, NodeId id, NodeId variable) {
    std::vector<NodeId> args;
    args.push_back(id);
    args.push_back(variable);
    return arena.call("int", args);
}

NodeId call1(Arena &arena, const char *name, NodeId arg) {
    std::vector<NodeId> args;
    args.push_back(arg);
    return arena.call(name, args);
}

bool literal(const Arena &arena, NodeId id, int64_t value) {
    const Node &n = arena.at(id);
    return n.kind == Kind::Integer && n.small_valid && n.small == value;
}

// value divided by divisor, spelled the way the parser spells a division. A divisor of one is left
// out rather than written, since a reader would not write it either.
NodeId over(Arena &arena, NodeId value, NodeId divisor) {
    if (literal(arena, divisor, 1))
        return value;
    // The reciprocal is folded on its own so a rational divisor comes back as the number a reader
    // would write: dividing by one half is times 2, not times (2^-1)^-1.
    NodeId reciprocal = canonicalize(arena, arena.binary(Kind::Pow, divisor, arena.integer("-1")));
    if (reciprocal == kNoNode)
        return kNoNode;
    if (literal(arena, reciprocal, 1))
        return value;
    return arena.binary(Kind::Mul, value, reciprocal);
}

// Whether u is a x + b for constants a and b, and what a is. The coefficient comes back in canonical
// form, because it is displayed as the differential and a folded 2 reads better than 1 * 2, and
// because a coefficient that folds to zero means the argument was never varying.
bool linear_in(Context &ctx, NodeId u, NodeId *coefficient) {
    Arena &a = ctx.arena;
    NodeId raw = kNoNode;
    if (u == ctx.variable) {
        raw = a.integer("1");
    } else {
        const Node &n = a.at(u);
        switch (n.kind) {
            case Kind::Add: {
                std::vector<NodeId> parts;
                for (NodeId term : a.children(n)) {
                    if (!depends_on(a, term, ctx.variable))
                        continue;
                    NodeId c;
                    if (!linear_in(ctx, term, &c))
                        return false;
                    parts.push_back(c);
                }
                if (parts.empty())
                    return false;
                raw = parts.size() == 1 ? parts[0] : a.nary(Kind::Add, parts);
                break;
            }
            case Kind::Mul: {
                std::vector<NodeId> factors;
                size_t varying = 0;
                for (NodeId f : a.children(n)) {
                    if (!depends_on(a, f, ctx.variable)) {
                        factors.push_back(f);
                        continue;
                    }
                    NodeId c;
                    if (++varying > 1 || !linear_in(ctx, f, &c))
                        return false;
                    factors.push_back(c);
                }
                if (varying == 0)
                    return false;
                raw = factors.size() == 1 ? factors[0] : a.nary(Kind::Mul, factors);
                break;
            }
            case Kind::Neg: {
                NodeId c;
                if (!linear_in(ctx, a.children(n)[0], &c))
                    return false;
                raw = a.unary(Kind::Neg, c);
                break;
            }
            default:
                return false;
        }
    }
    NodeId folded = canonicalize(a, raw);
    if (folded == kNoNode || literal(a, folded, 0))
        return false;
    // Every rule that reaches here divides by this coefficient, and a measured decimal has no exact
    // reciprocal to divide by. Refusing at the one place the coefficient is settled says that once,
    // rather than leaving three rules to produce an answer their own derivative check then rejects.
    if (ctx.mode == NumericMode::Exact && has_decimal(a, folded)) {
        refuse(ctx, "the coefficient " + print(a, folded) +
                        " is a measured decimal, and this rule has to divide by it. Switch to "
                        "decimal mode, or write the coefficient as an exact fraction.");
        return false;
    }
    *coefficient = folded;
    return true;
}


std::string substitution_detail(const Arena &arena, NodeId u, NodeId coefficient, NodeId variable) {
    const std::string derivative = print(arena, coefficient);
    return "The derivative of " + print(arena, u) + " with respect to " + print(arena, variable) +
           " is " + derivative + (literal(arena, coefficient, 1)
               ? ", so no scale correction is needed."
               : ". Divide the antiderivative by " + derivative + " to undo this factor.");
}

std::string substitution_action(const Arena &arena, NodeId coefficient, std::string action) {
    if (!literal(arena, coefficient, 1))
        action += ", then divide by " + print(arena, coefficient);
    return action;
}

NodeId integrate_constant(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    NodeId after = literal(a, id, 0) ? id : a.binary(Kind::Mul, id, ctx.variable);
    Step s = envelope("Integrate " + print(a, id), "i.constant", "Constant rule",
                      "A constant integrates to the constant times the variable");
    s.explanation_detailed =
        "Reach for this when the expression has no variable in it at all. Ask what differentiates "
        "to a constant: a constant times the variable does, because differentiating it leaves the "
        "constant behind. So 5 integrates to 5 x, and checking that by differentiating gets 5 back.";
    s.verifications.push_back(rule_invariant("the expression contains no occurrence of the variable"));
    return leaf_result(ctx, parent, std::move(s), int_of(a, id, ctx.variable), after,
                       "Multiply the constant by the variable");
}

// Differentiate's rule of the same name, for this engine's Context. What it guards is shared and
// lives on the derivation: a composite that failed partway may complete itself with the rest left
// as integrals-of, but only where work actually landed below it. Differentiate's copy carries the
// argument for why a halt is not excluded from that.
bool may_complete_partially(Context &ctx, StepId here) {
    return ctx.derivation.completed_transformation_after(here);
}

NodeId integrate_sum(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    const ChildView terms = a.children(id);

    Step s = envelope("Integrate " + print(a, id), "i.sum", "Sum rule",
                      "The integral of a sum is the sum of the integrals");
    s.explanation_detailed = "Each term is integrated on its own and the results are added.";
    s.verifications.push_back(rule_invariant("integration is linear, so this holds term by term"));
    StepId here = record(ctx, parent, std::move(s), int_of(a, id, ctx.variable), kNoNode,
                         "Integrate each term separately");

    std::vector<NodeId> parts;
    for (size_t i = 0; i < terms.size(); ++i) {
        NodeId it = antiderive(ctx, terms[i], here);
        if (ctx.failed) {
            if (!may_complete_partially(ctx, here))
                return kNoNode;
            for (size_t j = i; j < terms.size(); ++j)
                parts.push_back(int_of(a, terms[j], ctx.variable));
            ctx.derivation.complete_transformation(here, a.nary(Kind::Add, parts));
            return kNoNode;
        }
        parts.push_back(it);
    }
    NodeId out = a.nary(Kind::Add, parts);
    ctx.derivation.complete_transformation(here, out);
    return out;
}

NodeId integrate_scaled(Context &ctx, NodeId id, StepId parent, NodeId factor, NodeId rest,
                        const char *why, const std::string &action, const char *invariant) {
    Arena &a = ctx.arena;
    Step s = envelope("Integrate " + print(a, id), "i.constant-multiple", "Constant multiple rule",
                      why);
    s.explanation_detailed =
        "Reach for this when one factor has no variable in it, as in 3 sin(x) or x squared over 2. "
        "A fixed multiplier can be lifted out, the rest integrated on its own, and the multiplier "
        "put back, because scaling a function scales its area by the same amount. Only a factor "
        "free of the variable may move: anything that changes has to stay inside.";
    s.verifications.push_back(rule_invariant(invariant));
    StepId here = record(ctx, parent, std::move(s), int_of(a, id, ctx.variable), kNoNode, action);

    NodeId inner = antiderive(ctx, rest, here);
    if (ctx.failed) {
        if (!may_complete_partially(ctx, here))
            return kNoNode;
        inner = int_of(a, rest, ctx.variable);
    }
    NodeId out = factor == kNoNode ? a.unary(Kind::Neg, inner) : a.binary(Kind::Mul, factor, inner);
    ctx.derivation.complete_transformation(here, out);
    return ctx.failed ? kNoNode : out;
}

NodeId integrate_product(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    std::vector<NodeId> constants;
    std::vector<NodeId> varying;
    for (NodeId f : a.children(id)) {
        if (depends_on(a, f, ctx.variable))
            varying.push_back(f);
        else
            constants.push_back(f);
    }
    if (varying.empty())
        return integrate_constant(ctx, id, parent);

    if (!constants.empty()) {
        NodeId rest = varying.size() == 1 ? varying[0] : a.nary(Kind::Mul, varying);
        NodeId factor = constants.size() == 1 ? constants[0] : a.nary(Kind::Mul, constants);
        return integrate_scaled(ctx, id, parent, factor, rest,
                                "A constant factor stays where it is and the rest is integrated",
                                "Take " + print(a, factor) + " outside the integral",
                                "the factor taken out contains no occurrence of the variable");
    }

    refuse(ctx, "a product of two expressions that both contain the variable needs integration "
                "by parts or a substitution, which is not implemented");
    return kNoNode;
}

NodeId integrate_power(Context &ctx, NodeId id, StepId parent, NodeId base, NodeId exponent) {
    Arena &a = ctx.arena;
    if (depends_on(a, exponent, ctx.variable)) {
        refuse(ctx, "an exponent that contains the variable has no rule here; the exponential is "
                    "written exp(x)");
        return kNoNode;
    }
    int64_t n;
    if (!folded_integer(a, exponent, &n)) {
        refuse(ctx, "only an integer exponent is supported here");
        return kNoNode;
    }
    NodeId coefficient;
    if (!linear_in(ctx, base, &coefficient)) {
        refuse(ctx, "the base " + print(a, base) +
                        " is not linear in the variable, which would need a substitution that is "
                        "not implemented");
        return kNoNode;
    }
    const bool direct = base == ctx.variable;
    const std::string goal = "Integrate " + print(a, id);

    if (n == -1) {
        NodeId argument = base;
        Rational at_point;
        if (ctx.branch_point &&
            evaluate_rational(a, base, {{a.text(ctx.variable), *ctx.branch_point}}, &at_point) &&
            at_point.num < 0)
            argument = a.unary(Kind::Neg, base);
        NodeId after = over(a, call1(a, "ln", argument), coefficient);
        Step s = direct ? envelope(goal, "i.reciprocal", "Logarithmic integral",
                                   "The integral of one over the variable is its natural logarithm")
                        : envelope(goal, "i.linear-substitution",
                                   "Logarithmic integral with a linear substitution",
                                   "Substitute for the linear argument, then use the logarithm");
        s.explanation_detailed =
            (direct ? std::string() : substitution_detail(a, base, coefficient, ctx.variable) + " ") +
            "The logarithm is only defined for a positive argument; for a negative one the "
            "antiderivative is the logarithm of the negated argument, so the restriction is "
            "recorded rather than hidden inside an absolute value.";
        s.verifications.push_back(rule_invariant(
            direct ? "the exponent is minus one and the base is the variable"
                   : "the exponent is minus one and the base is linear in the variable"));
        if (argument != base) {
            s.explanation_short = "Use the logarithm of the negated argument on its negative branch";
            s.explanation_detailed =
                (direct ? std::string() : substitution_detail(a, base, coefficient, ctx.variable) + " ") +
                "The argument is negative at the supplied point. Use ln(" + print(a, argument) +
                "), whose derivative returns the same reciprocal. The logarithm's positive-argument "
                "condition is recorded and must hold throughout the requested interval.";
        }
        return leaf_result(ctx, parent, std::move(s), int_of(a, id, ctx.variable), after,
                           substitution_action(a, coefficient, "Use the natural logarithm of " + print(a, argument)));
    }

    if (add_overflows(n, 1)) {
        refuse(ctx, "the exponent is at the edge of what an integer here can hold");
        return kNoNode;
    }
    NodeId raised = a.integer(integer_text(n + 1));
    NodeId after = over(a, over(a, a.binary(Kind::Pow, base, raised), raised), coefficient);
    Step s = direct ? envelope(goal, "i.power", "Power rule",
                               "Raise the exponent by one and divide by the new exponent")
                    : envelope(goal, "i.linear-substitution", "Power rule with a linear substitution",
                               "Substitute for the linear base, then use the power rule");
    if (!direct)
        s.explanation_detailed = substitution_detail(a, base, coefficient, ctx.variable);
    else
        s.explanation_detailed =
            "Reach for this when the variable is raised to a fixed whole number and that number is "
            "not minus one. It runs the power rule backwards: the exponent goes up by one and the "
            "whole thing is divided by that new exponent. Minus one is the one exponent this "
            "cannot do, because raising it by one gives zero and nothing may be divided by zero. "
            "That case is the natural logarithm instead.";
    s.verifications.push_back(rule_invariant(
        direct ? "the base is the variable and the exponent is a constant integer other than "
                 "minus one"
               : "the base is linear in the variable with a non-zero coefficient, so the "
                 "substitution is invertible"));
    return leaf_result(ctx, parent, std::move(s), int_of(a, id, ctx.variable), after,
                       substitution_action(a, coefficient,
                           "Raise " + print(a, base) + " to power " + integer_text(n + 1) +
                           (n == 0 ? std::string() : " and divide by " + integer_text(n + 1))));
}

// The antiderivative of the named function in its own argument. kNoNode means the table does not
// have it, which is a refusal rather than a zero.
NodeId outer_antiderivative(Arena &arena, const std::string &name, NodeId u) {
    if (name == "sin")
        return arena.unary(Kind::Neg, call1(arena, "cos", u));
    if (name == "cos")
        return call1(arena, "sin", u);
    if (name == "exp")
        return call1(arena, "exp", u);
    if (name == "sqrt")
        return over(arena, arena.nary(Kind::Mul, {arena.integer("2"), u, call1(arena, "sqrt", u)}),
                    arena.integer("3"));
    return kNoNode;
}

const char *outer_rule_name(const std::string &name) {
    if (name == "exp")
        return "Exponential integral";
    if (name == "sqrt")
        return "Square-root integral";
    return "Trigonometric integral";
}

NodeId integrate_logarithm(Context &ctx, NodeId id, NodeId argument, NodeId coefficient, StepId parent) {
    Arena &a = ctx.arena;
    const NodeId primitive_one = over(a, argument, coefficient);
    const NodeId product = a.binary(Kind::Mul, primitive_one, id);
    Step s = envelope("Integrate " + print(a, id), "i.logarithm-parts", "Integration by parts",
                      "Differentiate the logarithm and integrate its constant multiplier");
    s.explanation_detailed =
        "Choose " + print(a, id) + " as the factor to differentiate and 1 as the factor to integrate. "
        "A primitive of 1 is " + print(a, primitive_one) + ". Multiplying this by the derivative of the "
        "logarithm gives 1. Integration by parts therefore gives " + print(a, product) +
        " minus the integral of 1. The logarithm's argument must stay positive.";
    s.verifications.push_back(rule_invariant(
        "the affine argument has constant nonzero derivative, so the product rule reduces the remaining integrand to 1"));
    const StepId here = record(ctx, parent, std::move(s), int_of(a, id, ctx.variable), kNoNode,
                              "Use integration by parts, leaving only the integral of 1");
    const NodeId remaining = antiderive(ctx, a.integer("1"), here);
    if (ctx.failed) return kNoNode;
    const NodeId after = a.binary(Kind::Add, product, a.unary(Kind::Neg, remaining));
    ctx.derivation.complete_transformation(here, after);
    carry_restrictions(ctx, here, kNoNode, after);
    return after;
}

NodeId integrate_call(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    const std::string name = a.text(id);
    const ChildView args = a.children(id);
    if (args.size() != 1) {
        refuse(ctx, "only functions of one argument are supported here");
        return kNoNode;
    }
    NodeId u = args[0];
    NodeId outer = outer_antiderivative(a, name, u);
    if (outer == kNoNode && name != "ln") {
        refuse(ctx, "there is no antiderivative rule for " + name);
        return kNoNode;
    }
    NodeId coefficient;
    if (!linear_in(ctx, u, &coefficient)) {
        refuse(ctx, "the argument " + print(a, u) +
                        " is not linear in the variable, which would need a substitution that is "
                        "not implemented");
        return kNoNode;
    }
    if (name == "ln") return integrate_logarithm(ctx, id, u, coefficient, parent);
    const bool direct = u == ctx.variable;
    NodeId after = over(a, outer, coefficient);
    Step s = direct ? envelope("Integrate " + print(a, id), "i.function", outer_rule_name(name),
                               "Use the known antiderivative of this function")
                    : envelope("Integrate " + print(a, id), "i.linear-substitution",
                               std::string(outer_rule_name(name)) + " with a linear substitution",
                               "Substitute for the linear argument, then use the known "
                               "antiderivative");
    if (name == "sqrt")
        s.explanation_detailed =
            (direct ? std::string() : substitution_detail(a, u, coefficient, ctx.variable) + " ") +
            "Write the square root as power 1/2. Raising the exponent by one gives 3/2, "
            "and dividing by 3/2 multiplies by 2/3. Write the result as the argument times "
            "its square root to keep the real branch visible. The argument must be nonnegative. "
            "At a zero endpoint, use the continuous value of the primitive.";
    else if (!direct)
        s.explanation_detailed = substitution_detail(a, u, coefficient, ctx.variable);
    else
        s.explanation_detailed =
            "Reach for this when a named function is applied to the bare variable, as in sin(x) "
            "rather than sin(2x). Each of these has an antiderivative worth learning, and it is "
            "read off by asking what differentiates to the function in front of you. Once the "
            "argument is anything but the variable, a substitution is needed first.";
    s.verifications.push_back(rule_invariant(
        direct ? "the argument is the variable itself, so no substitution is needed"
               : "the argument is linear in the variable with a non-zero coefficient"));
    return leaf_result(ctx, parent, std::move(s), int_of(a, id, ctx.variable), after,
                       substitution_action(a, coefficient, "Use " + print(a, outer)));
}

NodeId antiderive(Context &ctx, NodeId id, StepId parent) {
    Arena &a = ctx.arena;
    if (ctx.failed || id == kNoNode)
        return kNoNode;
    if (!ctx.meter.rewrite()) {
        halted(ctx);
        return kNoNode;
    }

    if (!depends_on(a, id, ctx.variable))
        return integrate_constant(ctx, id, parent);

    switch (a.at(id).kind) {
        case Kind::Symbol:
            // Only the variable reaches here, and it is its own first power.
            return integrate_power(ctx, id, parent, id, a.integer("1"));
        case Kind::Pow: {
            // A power of a power is one power, so 1/x^2 reaches the same rule as x^-2 rather than
            // being refused for a base that is not linear. Only the exponents are folded, so the
            // integrand is still shown to the reader as it was written.
            NodeId base = a.children(id)[0];
            NodeId exponent = a.children(id)[1];
            const Node &inner = a.at(base);
            if (inner.kind == Kind::Pow) {
                const NodeId flattened = canonicalize(a, id);
                if (flattened != kNoNode && a.at(flattened).kind == Kind::Pow) {
                    base = a.children(flattened)[0];
                    exponent = a.children(flattened)[1];
                }
            }
            return integrate_power(ctx, id, parent, base, exponent);
        }
        case Kind::Add:
            return integrate_sum(ctx, id, parent);
        case Kind::Mul:
            return integrate_product(ctx, id, parent);
        case Kind::Call:
            return integrate_call(ctx, id, parent);
        case Kind::Neg:
            return integrate_scaled(ctx, id, parent, kNoNode, a.children(id)[0],
                                    "A minus sign stays where it is and the rest is integrated",
                                    "Take the minus sign outside the integral",
                                    "negation is multiplication by a constant");
        default:
            refuse(ctx, "there is no antiderivative rule for this form");
            return kNoNode;
    }
}

bool mentions_symbol(const Arena &arena, NodeId id, const std::string &name) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Symbol && arena.text(id) == name)
        return true;
    for (NodeId a : arena.children(n)) {
        if (mentions_symbol(arena, a, name))
            return true;
    }
    return false;
}

// C unless the integrand already uses it, then the first of C1 to C9 it does not.
NodeId constant_symbol(Arena &arena, NodeId expression) {
    if (!mentions_symbol(arena, expression, "C"))
        return arena.symbol("C");
    for (char digit = '1'; digit <= '9'; ++digit) {
        std::string name = "C";
        name.push_back(digit);
        if (!mentions_symbol(arena, expression, name))
            return arena.symbol(name);
    }
    return kNoNode;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    const std::vector<std::string> &assumptions, DerivationStatus status,
                    NumericMode mode) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "calculus.integral.indefinite.single-variable";
    inputs.requested_method = "integrate by rule";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions = assumptions;
    inputs.angle_convention = angle_mode_name(derivation.request.angle_mode);
    inputs.branch_convention = "real domain, principal values";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.numeric_mode = mode;
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

}  // namespace

const char *integrate_outcome_name(IntegrateOutcome o) {
    switch (o) {
        case IntegrateOutcome::Integrated: return "integrated";
        case IntegrateOutcome::UnsupportedForm: return "unsupported form";
        case IntegrateOutcome::NotAVariable: return "not a variable";
        case IntegrateOutcome::VerificationFailed: return "verification failed";
        case IntegrateOutcome::Refused: return "refused";
        case IntegrateOutcome::Cancelled: return "cancelled";
        case IntegrateOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

namespace {

IntegrateResult integrate_impl(Arena &arena, Derivation &derivation, NodeId expression,
                              NodeId variable, Meter &meter, bool include_constant,
                              std::optional<Rational> branch_point, Backend *backend) {
    const Budget &budget = meter.budget();
    IntegrateResult result;
    std::vector<std::string> no_assumptions;
    // Read once, on entry, and carried from here. A mode that could change under a running solve
    // would give one derivation two readings, which is the guarantee PLAT-013 makes.
    const NumericMode mode = derivation.request.numeric_mode;
    if (expression == kNoNode || variable == kNoNode || arena.failed()) {
        result.detail = "nothing to integrate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }
    if (arena.at(variable).kind != Kind::Symbol) {
        result.outcome = IntegrateOutcome::NotAVariable;
        result.detail = "the variable has to be a symbol";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }
    if (contains_list(arena, expression)) {
        result.outcome = IntegrateOutcome::UnsupportedForm;
        result.detail = "list and matrix integration is not supported";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }
    if (derivation.request.angle_mode == AngleMode::Degrees && angle_dependent(arena, expression, variable)) {
        result.outcome = IntegrateOutcome::UnsupportedForm;
        result.detail = "the trigonometric integration rules assume radians, and degree mode is active";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }
    if (divides_by_zero(arena, expression)) {
        result.outcome = IntegrateOutcome::UnsupportedForm;
        result.detail = "the integrand divides by zero, which has no value to integrate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }
    if (has_unmeetable_condition(arena, expression)) {
        result.outcome = IntegrateOutcome::UnsupportedForm;
        result.detail = "the integrand is undefined here, so there is nothing to integrate";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }

    const size_t mark = derivation.mark();

    const std::string name = arena.text(variable);
    PlanPayload plan;
    plan.strategy_id = "calculus.integrate.rules";
    plan.selected_strategy = "Integrate by rule";
    plan.matched_problem_facts.push_back("integrate with respect to " + name);
    plan.selection_rationale =
        "each rule is applied to the form it matches and the parts are integrated in turn, and the "
        "answer is then differentiated to confirm it gives the integrand back";

    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Integrate " + print(arena, expression) + " with respect to " + name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Integrate by rule";
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short =
        "Work outwards in, applying the rule that matches each form, then check by differentiating";
    register_strategy_precondition(
        plan, plan_step, "pre.integrate.registered-rules",
        "every form in the integrand has an antiderivative rule",
        "registered antiderivative rule dispatch", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked while traversing the integrand");
    register_strategy_precondition(
        plan, plan_step, "pre.integrate.linear-inner-forms",
        "every function argument and every power base is the variable or linear in it",
        "registered inner-form analysis", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked while matching power and function rules");
    StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    Context ctx(arena, derivation, variable, meter, mode, branch_point);

    // Both modes read a typed decimal as the rational it names, and PRD 13.1 is satisfied by that
    // being a step the reader can see rather than by the mode. What the modes differ on is the exit.
    NodeId integrand = expression;
    if (has_decimal(arena, expression)) {
        if (!ctx.meter.step()) {
            halted(ctx);
        } else {
            const ModeStep promoted = read_decimals_exactly(arena, derivation, plan_id, "integrate",
                                                            expression, &integrand);
            if (const char *why = promotion_refusal(promoted)) {
                ctx.exhausted = promotion_exhausted(promoted);
                refuse(ctx, why);
            }
        }
    }

    if (!ctx.failed &&
        !gather_repeated_factors(arena, derivation, plan_id, "integrate", meter, integrand,
                                 &integrand)) {
        halted(ctx);
    }

    NodeId particular = ctx.failed ? kNoNode : antiderive(ctx, integrand, plan_id);

    // The constant of integration, as a step of its own so the reader sees where it comes from.
    NodeId general = particular;
    if (!ctx.failed && particular != kNoNode && include_constant) {
        NodeId c = constant_symbol(arena, integrand);
        if (c == kNoNode) {
            refuse(ctx, "the integrand uses every name this build has for the constant of "
                        "integration");
        } else {
            general = arena.binary(Kind::Add, particular, c);
            Step s = envelope("State the general antiderivative", "i.constant-of-integration",
                              "Constant of integration",
                              "Any constant differentiates to zero, so adding one gives another "
                              "antiderivative");
            s.explanation_detailed =
                "Differentiation loses constants, so every function that differentiates to the "
                "integrand has the form found plus some constant. The constant stands for all of "
                "them at once.";
            // The one rule in this file whose after state is not the value it was handed. It names
            // a family, so it carries neither the equivalence claim nor the obligation the envelope
            // gives every other rule here.
            s.claim = ClaimType::FamilyUpToConstant;
            s.proof_obligations.clear();
            s.proof_obligations.push_back(
                {"obl.calculus.family-adds-a-constant",
                 "the after state is the antiderivative found plus one constant free nowhere "
                 "before it"});
            s.verifications.push_back(rule_invariant("the derivative of a constant is zero"));
            record(ctx, plan_id, std::move(s), particular, general,
                   "Add the constant " + arena.text(c));
        }
    }

    // VER-005: the answer is differentiated by rule and has to give the integrand back. The scratch
    // derivation keeps the derivative's own steps out of this record, which only needs the outcome.
    DiffResult back;
    NodeId expected = kNoNode;
    NodeId observed = kNoNode;
    VerificationOutcome comparison = VerificationOutcome::Inconclusive;
    ResultTag check_tag = ResultTag::Exact;
    std::string comparison_detail;
    if (!ctx.failed && general != kNoNode) {
        Derivation scratch;
        back = differentiate(arena, scratch, general, variable, meter);
        if (back.outcome == DiffOutcome::Cancelled || back.outcome == DiffOutcome::ResourceExceeded) {
            // The check spent the budget the solve had left, which halts the solve the same way.
            ctx.failed = true;
            ctx.detail = back.detail;
        } else if (!ctx.meter.step()) {
            halted(ctx);
        } else {
            expected = canonicalize(arena, integrand);
            observed = back.derivative == kNoNode ? kNoNode : canonicalize(arena, back.derivative);

            Step check;
            check.phase = "check";
            check.goal = "Check the answer";
            check.rule_id = "calculus.integrate.check-by-differentiation";
            check.rule_name = "Check by differentiation";
            check.claim = ClaimType::EquivalentExpression;
            check.explanation_short = "Differentiate the result and compare it with the integrand";
            check.explanation_detailed =
                "An antiderivative is right exactly when its derivative is the integrand, so the "
                "check is to differentiate it by the same rules and compare the two forms.";
            check.proof_obligations.push_back({"obl.integrate.derivative-returns-integrand",
                                               "the derivative of the antiderivative is the "
                                               "integrand"});

            CheckPayload payload;
            payload.target_claim = "the derivative of " + print(arena, general) + " is " +
                                   print(arena, integrand);
            payload.check_method = "differentiate the result by rule and compare canonical forms";
            payload.expected_relation =
                expected == kNoNode ? std::string("the integrand") : print(arena, expected);

            VerificationRecord v;
            v.method = "differentiate the antiderivative";
            EvidenceStrength strength = EvidenceStrength::CandidateChecked;
            if (back.outcome != DiffOutcome::Differentiated) {
                payload.observed_result = "the result could not be differentiated: " + back.detail;
                v.outcome = VerificationOutcome::Inconclusive;
                v.detail = "the derivative rules refused the antiderivative";
            } else if (expected == kNoNode || observed == kNoNode) {
                payload.observed_result = "the comparison outgrew the expression limits";
                v.outcome = VerificationOutcome::Inconclusive;
                v.detail = "the canonical forms could not be built";
            } else if (expected == observed) {
                payload.observed_result = print(arena, observed);
                v.outcome = VerificationOutcome::Passed;
                v.detail = "the derivative of the result is the integrand";
            } else {
                payload.observed_result = print(arena, observed);
                v.outcome = VerificationOutcome::Inconclusive;
                v.detail = "the canonical forms differ, which does not establish inequivalence";
                if (backend && !arena.failed() && meter.checkpoint() && meter.backend_call()) {
                    Request identity;
                    identity.op = Op::IsZero;
                    identity.target = arena.binary(Kind::Add, observed, arena.unary(Kind::Neg, expected));
                    if (!arena.failed()) {
                        Adapter adapter(arena, *backend);
                        const Response checked = adapter.run(identity);
                        check_tag = checked.tag;
                        meter.checkpoint();
                        payload.check_method = "differentiate by rule, then simplify the exact difference with Giac";
                        payload.expected_relation = "zero";
                        payload.observed_result = tag_name(checked.tag);
                        if (!arena.failed() && checked.single_value() != kNoNode) {
                            const std::string residual_text = print(arena, checked.single_value());
                            payload.observed_result = checked.tag == ResultTag::Exact ? residual_text
                                : payload.observed_result + ": " + residual_text;
                        } else if (!checked.detail.empty()) {
                            payload.observed_result += ": " + checked.detail;
                        }
                        v.method = "native differentiation and Giac exact difference";
                        strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
                        v.detail = checked.detail.empty() ? tag_name(checked.tag) : checked.detail;
                        Rational residual;
                        if (!arena.failed() && !meter.stopped() && checked.tag == ResultTag::Exact &&
                            checked.single_value() != kNoNode &&
                            evaluate_rational(arena, checked.single_value(), {}, &residual)) {
                            v.outcome = residual.num == 0 ? VerificationOutcome::Passed : VerificationOutcome::Failed;
                            v.detail = residual.num == 0 ? "the exact difference is zero under the recorded domain restrictions"
                                                       : "the exact difference is a nonzero constant";
                        }
                    }
                }
            }
            comparison = v.outcome;
            comparison_detail = v.detail;
            v.strength = strength_for(v.outcome, strength);
            check.verifications.push_back(v);
            derivation.add_check(plan_id, std::move(check), std::move(payload));
        }
    }

    result.cost = meter.cost();

    // STEP-025: a halted solve keeps the rules that were checked and drops the rest. Both
    // preconditions are settled first, or the plan reads as unchecked and takes the prefix with it.
    // Scoped to what the traversal reached, because a form it never visited proves nothing: a form
    // with no rule is the refusal below, not this.
    const bool check_halted = back.outcome == DiffOutcome::Cancelled ||
                              back.outcome == DiffOutcome::ResourceExceeded || arena.failed() ||
                              check_tag == ResultTag::Cancelled;
    if (meter.stopped() || check_halted) {
        derivation.complete_plan_precondition(
            plan_id, "pre.integrate.registered-rules", VerificationOutcome::Passed,
            "every form visited before the stop matched a registered antiderivative rule");
        derivation.complete_plan_precondition(
            plan_id, "pre.integrate.linear-inner-forms", VerificationOutcome::Passed,
            "every inner form matched before the stop met its registered linearity requirement");
        const bool cancelled = back.outcome == DiffOutcome::Cancelled ||
                               check_tag == ResultTag::Cancelled || meter.halt() == Halt::Cancelled;
        // Settled before the trim, the same order and for the same reason as the refusal below: a
        // halt keeps steps, and the logarithm rule is one that can be kept while needing a non-zero
        // argument to hold. The context still claims no assumptions, because the answer this path
        // does not offer is what an assumption would be qualifying.
        ctx.recorded.settle(arena, derivation, &ctx.assumptions);
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        result.outcome = cancelled ? IntegrateOutcome::Cancelled : IntegrateOutcome::ResourceExceeded;
        result.detail = arena.failed() ? status_name(arena.status()) : meter.stopped() ? halt_name(meter.halt())
            : check_tag != ResultTag::Exact ? comparison_detail : back.detail;
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }

    if (ctx.failed || general == kNoNode) {
        // PRD 808, the same shape as differentiate's refusal below its own halt path: an integrand
        // whose first term integrated has a checked prefix, and verified_prefix_end cuts the
        // composite above the failing term without taking that prefix with it. Both preconditions
        // settle first or the plan reads as unchecked, scoped to what actually produced a step.
        derivation.complete_plan_precondition(
            plan_id, "pre.integrate.registered-rules", VerificationOutcome::Passed,
            "every form that produced a recorded step matched a registered antiderivative rule");
        derivation.complete_plan_precondition(
            plan_id, "pre.integrate.linear-inner-forms", VerificationOutcome::Passed,
            "every inner form in a recorded step met its registered linearity requirement");
        // The conditions belong to the steps, so a step that survives has to take its conditions
        // with it. Settling before the trim rather than after, because a condition written onto a
        // step the trim then drops goes with it, where a step kept without its condition is a
        // transformation shown as unqualified when it is not.
        ctx.recorded.settle(arena, derivation, &ctx.assumptions);
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        // An answer that went missing because the arena ran out is a resource limit, not a form
        // with no rule. Without this the two are indistinguishable here, and (x+1+...+1)^2 was
        // called unsupported two characters after the same shape integrated.
        const bool exhausted = arena.failed() || ctx.exhausted;
        result.outcome =
            exhausted ? IntegrateOutcome::ResourceExceeded : IntegrateOutcome::UnsupportedForm;
        result.detail = arena.failed()
                            ? std::string("the working space ran out before the "
                                          "antiderivative was built")
                        : ctx.detail.empty()
                            ? std::string("the integrand contains a form with no rule")
                            : ctx.detail;
        result.status = exhausted ? DerivationStatus::ResourceLimitReached
                        : kept    ? DerivationStatus::PartiallySolved
                                  : DerivationStatus::Unsupported;
        // The traversal metered every form it reached before the one with no rule, and this path
        // reported none of it. The halt path beside it has always reported its cost.
        result.cost = meter.cost();
        record_context(derivation, budget, expression, no_assumptions, result.status, mode);
        return result;
    }

    derivation.complete_plan_precondition(
        plan_id, "pre.integrate.registered-rules", VerificationOutcome::Passed,
        "every visited form matched a registered antiderivative rule");
    derivation.complete_plan_precondition(
        plan_id, "pre.integrate.linear-inner-forms", VerificationOutcome::Passed,
        "every matched inner form met its registered linearity requirement");

    // Only now, because the refusals above that rewind would be writing a condition onto a step
    // about to be dropped, qualifying an answer nobody was given. The two paths that keep steps,
    // the unsupported one above and the failed check below, settle their own conditions instead.
    ctx.recorded.settle(arena, derivation, &ctx.assumptions);

    if (comparison == VerificationOutcome::Failed) {
        // The record stays, failed check and all, so the reader can see what was tried. The answer
        // does not: section 17 says a result that failed its check is not offered as one.
        result.outcome = IntegrateOutcome::VerificationFailed;
        result.detail = "the result failed its own derivative check, so it is not offered";
        result.status = DerivationStatus::VerificationFailed;
        record_context(derivation, budget, expression, ctx.assumptions, result.status, mode);
        return result;
    }

    if (comparison != VerificationOutcome::Passed) {
        result.outcome = IntegrateOutcome::Refused;
        result.detail = "the derivative check is inconclusive, so the answer is withheld: " + comparison_detail;
        result.status = DerivationStatus::PartiallySolved;
        record_context(derivation, budget, expression, ctx.assumptions, result.status, mode);
        return result;
    }

    result.outcome = IntegrateOutcome::Integrated;
    result.antiderivative = general;
    result.particular = particular;
    result.status = derivation.outcome_from(mark);

    // Decimal mode's closing report, after the check rather than before it, so what was verified is
    // the exact answer and what is reported says out loud that it is a rewriting of it. The status
    // stays solved rather than becoming numerically approximated, because a fraction with no
    // terminating decimal is left alone rather than rounded: nothing here loses a digit.
    if (mode == NumericMode::Decimal && result.status == DerivationStatus::SolvedAndVerified &&
        meter.step()) {
        NodeId reported = kNoNode;
        if (report_in_decimals(arena, derivation, plan_id, "integrate", general, &reported) ==
            ModeStep::Recorded) {
            result.antiderivative = reported;
            // The particular antiderivative goes with it, unrecorded, because it is the same answer
            // without the constant rather than a step of its own. Two fields of one result holding
            // one answer in two forms is a caller's bug waiting to happen.
            const NodeId bare = decimalize(arena, particular);
            if (bare != kNoNode)
                result.particular = bare;
        }
    }

    if (meter.stopped()) {
        const bool cancelled = meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        result.outcome = cancelled ? IntegrateOutcome::Cancelled : IntegrateOutcome::ResourceExceeded;
        result.antiderivative = kNoNode;
        result.particular = kNoNode;
        result.detail = halt_name(meter.halt());
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept ? DerivationStatus::Cancelled : DerivationStatus::NotRecorded;
    }
    result.cost = meter.cost();
    record_context(derivation, budget, expression, ctx.assumptions, result.status, mode);
    return result;
}

}

IntegrateResult integrate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                          const Budget &budget, Backend *backend) {
    Meter meter(budget);
    return integrate_impl(arena, derivation, expression, variable, meter, true, std::nullopt, backend);
}

IntegrateResult integrate_particular(Arena &arena, Derivation &derivation, NodeId expression,
                                    NodeId variable, Meter &meter, std::optional<Rational> branch_point, Backend *backend) {
    return integrate_impl(arena, derivation, expression, variable, meter, false, branch_point, backend);
}

}  // namespace nps
