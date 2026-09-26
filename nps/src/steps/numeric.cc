#include "nps/steps/numeric.h"

#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"

namespace nps {
namespace {

constexpr size_t kMaxDegree = 12;
constexpr size_t kMaxHalvings = 50;
constexpr size_t kMaxNewton = 30;
constexpr int64_t kMaxIntervals = 1000;

enum class Kind2 { Bisection, Newton, Trapezoid, Simpson };

struct MethodSpec {
    const char *name;
    size_t arity;
    Kind2 kind;
    const char *plan_rule;
    const char *plan_name;
};

const MethodSpec kSpecs[] = {
    {"bisect", 5, Kind2::Bisection, "plan.numeric-bisection", "Bisection on a sign change"},
    {"newtonroot", 4, Kind2::Newton, "plan.numeric-newton", "Newton's method"},
    {"trapsum", 5, Kind2::Trapezoid, "plan.numeric-trapezoid", "The trapezoid rule"},
    {"simpsum", 5, Kind2::Simpson, "plan.numeric-simpson", "Simpson's rule"},
};

const MethodSpec *spec_named(std::string_view name) {
    for (const MethodSpec &spec : kSpecs) {
        if (name == spec.name)
            return &spec;
    }
    return nullptr;
}

using Poly = std::vector<Rational>;

bool poly_add(const Poly &a, const Poly &b, Poly *out) {
    Poly sum(a.size() > b.size() ? a.size() : b.size(), Rational{0, 1});
    for (size_t i = 0; i < sum.size(); ++i) {
        const Rational left = i < a.size() ? a[i] : Rational{0, 1};
        const Rational right = i < b.size() ? b[i] : Rational{0, 1};
        if (!rational_add(left, right, &sum[i]))
            return false;
    }
    *out = std::move(sum);
    return true;
}

bool poly_mul(const Poly &a, const Poly &b, Poly *out) {
    if (a.size() + b.size() - 1 > kMaxDegree + 1)
        return false;
    Poly product(a.size() + b.size() - 1, Rational{0, 1});
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) {
            Rational term;
            if (!rational_mul(a[i], b[j], &term) || !rational_add(product[i + j], term, &product[i + j]))
                return false;
        }
    }
    *out = std::move(product);
    return true;
}

// The coefficients of a polynomial in the one variable, lowest power first.
bool read_poly(const Arena &arena, NodeId id, const std::string &variable, Poly *out, int depth = 0) {
    if (depth > 64 || arena.is_approximate(id))
        return false;
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(id);
    switch (n.kind) {
        case Kind::Integer: {
            Rational value;
            if (!evaluate_rational(arena, id, {}, &value))
                return false;
            *out = {value};
            return true;
        }
        case Kind::Symbol:
            if (arena.text(id) != variable)
                return false;
            *out = {Rational{0, 1}, Rational{1, 1}};
            return true;
        case Kind::Neg: {
            Poly inner;
            if (!read_poly(arena, kids[0], variable, &inner, depth + 1))
                return false;
            for (Rational &c : inner)
                c.num = -c.num;
            *out = std::move(inner);
            return true;
        }
        case Kind::Add:
        case Kind::Mul: {
            Poly acc = {Rational{n.kind == Kind::Add ? 0 : 1, 1}};
            for (NodeId child : kids) {
                Poly part;
                if (!read_poly(arena, child, variable, &part, depth + 1))
                    return false;
                if (!(n.kind == Kind::Add ? poly_add(acc, part, &acc) : poly_mul(acc, part, &acc)))
                    return false;
            }
            *out = std::move(acc);
            return true;
        }
        case Kind::Pow: {
            int64_t exponent = 0;
            Poly base;
            if (!small_integer(arena, kids[1], &exponent) || !read_poly(arena, kids[0], variable, &base, depth + 1))
                return false;
            if (exponent < 0) {
                // Only a constant may be divided by, so the function stays a polynomial.
                if (base.size() != 1 || base[0].num == 0 || exponent < -64)
                    return false;
                Rational value;
                if (!rational_power(base[0], exponent, &value))
                    return false;
                *out = {value};
                return true;
            }
            if (static_cast<size_t>(exponent) > kMaxDegree)
                return false;
            Poly acc = {Rational{1, 1}};
            for (int64_t i = 0; i < exponent; ++i) {
                if (!poly_mul(acc, base, &acc))
                    return false;
            }
            *out = std::move(acc);
            return true;
        }
        default:
            return false;
    }
}

void evaluate_at(const Poly &c, mpq_srcptr x, mpq_ptr out) {
    detail::Mpq coefficient;
    mpq_set_ui(out, 0, 1);
    for (size_t i = c.size(); i-- > 0;) {
        mpq_mul(out, out, x);
        detail::mpq_set_rational(coefficient.get(), c[i]);
        mpq_add(out, out, coefficient.get());
    }
}

int sign_at(const Poly &c, mpq_srcptr x) {
    detail::Mpq value;
    evaluate_at(c, x, value.get());
    return mpq_sgn(value.get());
}

Poly derivative(const Poly &c) {
    Poly d;
    for (size_t i = 1; i < c.size(); ++i) {
        Rational term;
        rational_mul(c[i], Rational{static_cast<int64_t>(i), 1}, &term);
        d.push_back(term);
    }
    if (d.empty())
        d.push_back(Rational{0, 1});
    return d;
}

// An upper bound on the k-th derivative over |x| <= reach, from the absolute coefficients.
void derivative_bound(const Poly &c, size_t k, mpq_srcptr reach, mpq_ptr out) {
    mpq_set_ui(out, 0, 1);
    detail::Mpq term, power, coefficient;
    for (size_t i = k; i < c.size(); ++i) {
        detail::mpq_set_rational(coefficient.get(), c[i]);
        mpq_abs(term.get(), coefficient.get());
        for (size_t f = i - k + 1; f <= i; ++f) {
            mpq_set_ui(power.get(), static_cast<unsigned long>(f), 1);
            mpq_mul(term.get(), term.get(), power.get());
        }
        mpq_set_ui(power.get(), 1, 1);
        for (size_t p = 0; p < i - k; ++p)
            mpq_mul(power.get(), power.get(), reach);
        mpq_mul(term.get(), term.get(), power.get());
        mpq_add(out, out, term.get());
    }
}

NodeId mpq_node(Arena &arena, mpq_srcptr value) {
    Rational r;
    if (!detail::mpq_get_rational(value, &r))
        return kNoNode;
    return canonical_rational(arena, r);
}

NodeId poly_node(Arena &arena, const Poly &c, NodeId variable) {
    std::vector<NodeId> terms;
    for (size_t i = c.size(); i-- > 0;) {
        if (c[i].num == 0)
            continue;
        NodeId power = i == 0 ? kNoNode : i == 1 ? variable
                                                 : arena.binary(Kind::Pow, variable, arena.integer(std::to_string(i)));
        NodeId coefficient = canonical_rational(arena, c[i]);
        terms.push_back(power == kNoNode ? coefficient
                        : (c[i].num == 1 && c[i].den == 1) ? power
                                                            : arena.binary(Kind::Mul, coefficient, power));
    }
    if (terms.empty())
        return arena.integer("0");
    NodeId sum = terms[0];
    for (size_t i = 1; i < terms.size(); ++i)
        sum = arena.binary(Kind::Add, sum, terms[i]);
    return sum;
}

bool constant_argument(Arena &arena, NodeId id, Rational *out) {
    const NodeId exact = exactify(arena, id);
    if (exact == kNoNode)
        return false;
    std::vector<std::string> symbols;
    collect_symbols(arena, exact, &symbols);
    return symbols.empty() && evaluate_rational(arena, exact, {}, out) && out->den > 0;
}

std::string sign_word(int sign) {
    return sign > 0 ? "positive" : sign < 0 ? "negative" : "zero";
}

struct Run {
    Arena &arena;
    Derivation &derivation;
    NodeId input;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    const MethodSpec *spec = nullptr;
    NodeId variable = kNoNode;
    std::string variable_name;
    Poly f;
    bool failed = false;
    NumericOutcome failure = NumericOutcome::OutsideEnvelope;
    std::string detail;
    size_t iterations = 0;

    Run(Arena &a, Derivation &d, NodeId e, const Budget &budget)
        : arena(a), derivation(d), input(e), meter(budget), mark(d.mark()) {}

    bool refuse(NumericOutcome outcome, std::string why) {
        if (failed)
            return false;
        failed = true;
        failure = outcome;
        detail = std::move(why);
        return false;
    }

    bool running() {
        if (failed)
            return false;
        if (arena.failed())
            return refuse(NumericOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped() || !meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? NumericOutcome::Cancelled : NumericOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        return true;
    }

    bool step() {
        if (!meter.step()) {
            running();
            return refuse(NumericOutcome::ResourceExceeded, halt_name(meter.halt()));
        }
        return running();
    }

    VerificationRecord evidence(const char *method, bool passed, std::string why, const char *obligation) {
        const VerificationOutcome outcome = passed ? VerificationOutcome::Passed : VerificationOutcome::Inconclusive;
        return {method, outcome, strength_for(outcome, EvidenceStrength::StructurallyValid), std::move(why), obligation};
    }

    Step envelope(const char *rule, const char *name, ClaimType claim, std::string explanation,
                  const char *cue = "") {
        Step s;
        s.explanation_detailed = cue;
        s.phase = "Iterate";
        s.goal = "Carry the method one step further";
        s.rule_id = rule;
        s.rule_name = name;
        s.claim = claim;
        s.explanation_short = std::move(explanation);
        return s;
    }

    bool transformation(Step s, NodeId before, NodeId after, std::string action) {
        if (!step())
            return false;
        if (before == kNoNode || after == kNoNode)
            return refuse(NumericOutcome::ResourceExceeded, "a value outgrew exact int64 storage");
        TransformationPayload change;
        change.before = before;
        change.after = after;
        change.reversible = false;
        change.concrete_action = std::move(action);
        derivation.add_transformation(plan, std::move(s), std::move(change));
        return running();
    }

    bool check(Step s, std::string target, std::string method, std::string observed) {
        if (!step())
            return false;
        s.phase = "Check";
        s.goal = "Check the stated bound";
        CheckPayload payload;
        payload.target_claim = std::move(target);
        payload.check_method = std::move(method);
        payload.expected_relation = "within the bound";
        payload.observed_result = std::move(observed);
        derivation.add_check(plan, std::move(s), std::move(payload));
        return running();
    }

    NumericResult finish(NumericOutcome outcome, NodeId value, NodeId bound, bool certified, std::string why) {
        NumericResult result;
        result.outcome = outcome;
        result.detail = std::move(why);
        result.iterations = iterations;
        if (arena.failed()) {
            result.outcome = NumericOutcome::ResourceExceeded;
            result.detail = status_name(arena.status());
        }
        switch (result.outcome) {
            case NumericOutcome::Approximated:
                result.value = value;
                result.bound = bound;
                result.bound_certified = certified;
                result.status = certified ? DerivationStatus::NumericallyApproximated : DerivationStatus::SolvedButUnchecked;
                break;
            case NumericOutcome::OutsideEnvelope: result.status = DerivationStatus::Unsupported; break;
            case NumericOutcome::InvalidInput:
            case NumericOutcome::NoSignChange: result.status = DerivationStatus::InvalidInput; break;
            case NumericOutcome::DidNotConverge:
            case NumericOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case NumericOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case NumericOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        std::vector<std::string> assumptions;
        if (result.outcome == NumericOutcome::Approximated) {
            assumptions.push_back("the function is a polynomial, so it is continuous and differentiable everywhere");
            if (!certified)
                assumptions.push_back("no sign change within the tolerance was found, so the stated bound is not proved");
        } else {
            keep_verified_prefix(derivation, mark, arena);
        }
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = spec ? (spec->kind == Kind2::Bisection || spec->kind == Kind2::Newton
                                                 ? "calculus.numerical-root.polynomial"
                                                 : "calculus.numerical-integral.polynomial")
                                         : "calculus.numerical-root.polynomial";
        context.requested_method = spec ? spec->name : "numeric";
        context.normalized_problem_model = input;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = input < arena.node_count() ? print(arena, input) : std::string();
        context.numeric_mode = derivation.request.numeric_mode;
        context.active_assumptions = assumptions;
        context.angle_convention = "not applicable";
        context.branch_convention = "real domain";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    NumericResult stopped() { return finish(failure, kNoNode, kNoNode, false, detail); }

    bool open_plan(const char *precondition, const char *statement, const char *method_text) {
        if (!step())
            return false;
        Step s;
        s.phase = "Plan";
        s.goal = std::string("Approximate with ") + spec->plan_name;
        s.rule_id = spec->plan_rule;
        s.rule_name = spec->plan_name;
        s.explanation_short = "The learner asked for this method by name, so the answer is an approximation with a stated bound.";
        s.explanation_detailed = "Every value is computed exactly as a fraction, so the only error is the method's own.";
        PlanPayload payload;
        payload.strategy_id = s.rule_id;
        payload.selected_strategy = s.rule_name;
        payload.matched_problem_facts.push_back(print(arena, input));
        payload.selection_rationale = "The method was requested explicitly, as CALC-019 requires.";
        register_strategy_precondition(payload, s, "pre.numeric.polynomial",
                                       "the function is a polynomial in one variable, so it is continuous",
                                       "polynomial shape reading", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, s.goal);
        if (precondition)
            register_strategy_precondition(payload, s, precondition, statement, method_text,
                                           EvidenceStrength::StructurallyValid, VerificationOutcome::Passed, s.goal);
        plan = derivation.add_plan(kNoStep, std::move(s), std::move(payload));
        return running();
    }
};

NodeId bracket_node(Arena &arena, mpq_srcptr a, mpq_srcptr b) {
    const NodeId left = mpq_node(arena, a);
    const NodeId right = mpq_node(arena, b);
    if (left == kNoNode || right == kNoNode)
        return kNoNode;
    return arena.call("bracket", {left, right});
}

NumericResult bisection(Run &run, const Rational &a0, const Rational &b0, const Rational &tolerance) {
    detail::Mpq a, b, m, width, tol, half;
    detail::mpq_set_rational(a.get(), a0);
    detail::mpq_set_rational(b.get(), b0);
    detail::mpq_set_rational(tol.get(), tolerance);
    mpq_set_ui(half.get(), 1, 2);
    const int sa = sign_at(run.f, a.get());
    const int sb = sign_at(run.f, b.get());
    if (sa == 0 || sb == 0) {
        if (!run.open_plan("pre.numeric.sign-change", "the function has opposite signs at the two ends, or a zero at one",
                           "exact sign evaluation"))
            return run.stopped();
        return run.finish(NumericOutcome::Approximated, mpq_node(run.arena, sa == 0 ? a.get() : b.get()),
                          run.arena.integer("0"), true, "an end of the bracket is an exact root");
    }
    if (sa == sb)
        return run.finish(NumericOutcome::NoSignChange, kNoNode, kNoNode, false,
                          "the function is " + sign_word(sa) + " at both ends, so bisection has no bracket to halve");
    if (!run.open_plan("pre.numeric.sign-change", "the function has opposite signs at the two ends, or a zero at one",
                       "exact sign evaluation"))
        return run.stopped();
    for (;;) {
        mpq_sub(width.get(), b.get(), a.get());
        mpq_mul(width.get(), width.get(), half.get());
        if (mpq_cmp(width.get(), tol.get()) <= 0)
            break;
        if (run.iterations >= kMaxHalvings)
            return run.finish(NumericOutcome::DidNotConverge, kNoNode, kNoNode, false,
                              "the tolerance needs more than " + std::to_string(kMaxHalvings) + " halvings");
        mpq_add(m.get(), a.get(), b.get());
        mpq_mul(m.get(), m.get(), half.get());
        const int sm = sign_at(run.f, m.get());
        const NodeId before = bracket_node(run.arena, a.get(), b.get());
        const NodeId middle = mpq_node(run.arena, m.get());
        std::string why = "f at the midpoint " + (middle == kNoNode ? std::string("?") : print(run.arena, middle)) +
                          " is " + sign_word(sm);
        if (sm == 0) {
            mpq_set(a.get(), m.get());
            mpq_set(b.get(), m.get());
        } else if (sm == sa) {
            mpq_set(a.get(), m.get());
            why += ", the same sign as at the left end, so the root is in the right half";
        } else {
            mpq_set(b.get(), m.get());
            why += ", the opposite sign to the left end, so the root is in the left half";
        }
        ++run.iterations;
        Step s = run.envelope("num.bisect-halve", "Halve the bracket", ClaimType::Implication, why,
                                "Reach for bisection when the function has opposite signs at two points, since a root lies between them.");
        s.proof_obligations.push_back({"obl.numeric.root-in-bracket", "a root lies in the new bracket"});
        s.verifications.push_back(run.evidence("exact sign evaluation", true, why, "obl.numeric.root-in-bracket"));
        if (!run.transformation(std::move(s), before, bracket_node(run.arena, a.get(), b.get()), why))
            return run.stopped();
        if (sm == 0)
            return run.finish(NumericOutcome::Approximated, middle, run.arena.integer("0"), true,
                              "the midpoint is an exact root");
    }
    mpq_add(m.get(), a.get(), b.get());
    mpq_mul(m.get(), m.get(), half.get());
    const int left = sign_at(run.f, a.get());
    const int right = sign_at(run.f, b.get());
    const bool proved = left * right <= 0 && mpq_cmp(width.get(), tol.get()) <= 0;
    const NodeId value = mpq_node(run.arena, m.get());
    const NodeId bound = mpq_node(run.arena, width.get());
    const std::string observed = "f is " + sign_word(left) + " and " + sign_word(right) +
                                 " at the ends of the last bracket, which is at most twice the tolerance wide";
    Step s = run.envelope("num.bisect-check", "Check the last bracket", ClaimType::Implication,
                          "A continuous function that changes sign has a root between, so the midpoint is within half the width.");
    s.proof_obligations.push_back({"obl.numeric.bound-holds", "a root lies within the stated bound of the answer"});
    s.verifications.push_back(run.evidence("sign change by the intermediate value theorem", proved, observed,
                                           "obl.numeric.bound-holds"));
    if (!run.check(std::move(s), "a root lies within the bound of the midpoint", "sign change by the intermediate value theorem",
                   observed))
        return run.stopped();
    if (!proved)
        return run.finish(NumericOutcome::VerificationFailed, kNoNode, kNoNode, false, observed);
    return run.finish(NumericOutcome::Approximated, value, bound, true, "the last bracket proves the bound");
}

// The working grid for Newton iterates, a power of ten at least a thousand times finer than the tolerance.
bool newton_grid(const Rational &tolerance, mpz_ptr grid) {
    mpz_set_ui(grid, 1);
    detail::Mpq step, target;
    detail::mpq_set_rational(target.get(), tolerance);
    mpq_set_ui(step.get(), 1, 1000);
    mpq_mul(target.get(), target.get(), step.get());
    for (int digits = 0; digits <= 15; ++digits) {
        mpq_set_ui(step.get(), 1, 1);
        mpz_set(mpq_denref(step.get()), grid);
        mpq_canonicalize(step.get());
        if (mpq_cmp(step.get(), target.get()) <= 0)
            return true;
        mpz_mul_ui(grid, grid, 10);
    }
    return false;
}

NumericResult newton(Run &run, const Rational &x0, const Rational &tolerance) {
    detail::Mpz grid;
    if (!newton_grid(tolerance, grid.get()))
        return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false,
                          "a tolerance below one part in a trillion is outside the envelope");
    if (!run.open_plan(nullptr, nullptr, nullptr))
        return run.stopped();
    const Poly df = derivative(run.f);
    {
        Step s = run.envelope("num.newton-derivative", "Differentiate the polynomial", ClaimType::Definition,
                              "Each term c x^k becomes k c x^(k-1).",
                              "Newton's step divides by the derivative, so the derivative comes first.");
        s.proof_obligations.push_back({"obl.numeric.derivative", "the derivative is read from the coefficients by the power rule"});
        s.verifications.push_back(run.evidence("power rule on each coefficient", true, "the power rule applied term by term",
                                               "obl.numeric.derivative"));
        if (!run.transformation(std::move(s), poly_node(run.arena, run.f, run.variable),
                                poly_node(run.arena, df, run.variable), "Differentiate each term"))
            return run.stopped();
    }
    detail::Mpq x, fx, dfx, next, delta, tol, scaled;
    detail::mpq_set_rational(x.get(), x0);
    detail::mpq_set_rational(tol.get(), tolerance);
    for (;;) {
        if (run.iterations >= kMaxNewton)
            return run.finish(NumericOutcome::DidNotConverge, kNoNode, kNoNode, false,
                              "the iterates did not settle within " + std::to_string(kMaxNewton) + " steps");
        evaluate_at(run.f, x.get(), fx.get());
        evaluate_at(df, x.get(), dfx.get());
        if (mpq_sgn(dfx.get()) == 0)
            return run.finish(NumericOutcome::DidNotConverge, kNoNode, kNoNode, false,
                              "the derivative is zero at the current iterate, so Newton's step is undefined");
        mpq_div(next.get(), fx.get(), dfx.get());
        mpq_sub(next.get(), x.get(), next.get());
        // Round to the grid so each iterate stays a short fraction, and say so on the step.
        mpz_mul(mpq_numref(scaled.get()), mpq_numref(next.get()), grid.get());
        mpz_set(mpq_denref(scaled.get()), mpq_denref(next.get()));
        detail::Mpz rounded;
        mpz_mul_ui(mpq_numref(scaled.get()), mpq_numref(scaled.get()), 2);
        mpz_add(mpq_numref(scaled.get()), mpq_numref(scaled.get()), mpq_denref(scaled.get()));
        mpz_mul_ui(mpq_denref(scaled.get()), mpq_denref(scaled.get()), 2);
        mpz_fdiv_q(rounded.get(), mpq_numref(scaled.get()), mpq_denref(scaled.get()));
        mpz_set(mpq_numref(next.get()), rounded.get());
        mpz_set(mpq_denref(next.get()), grid.get());
        mpq_canonicalize(next.get());
        ++run.iterations;
        const NodeId before = mpq_node(run.arena, x.get());
        const NodeId after = mpq_node(run.arena, next.get());
        std::string grid_text(mpz_sizeinbase(grid.get(), 10) + 2, '\0');
        mpz_get_str(grid_text.data(), 10, grid.get());
        grid_text.resize(grid_text.find('\0'));
        const std::string action = "x - f(x)/f'(x), rounded to the nearest multiple of 1/" + grid_text;
        Step s = run.envelope("num.newton-iterate", "Take one Newton step", ClaimType::Definition, action,
                              "Follow the tangent line to where it crosses zero, which closes in fast near a simple root.");
        s.proof_obligations.push_back({"obl.numeric.newton-update",
                                       "the next iterate is x - f(x)/f'(x) rounded to the working grid"});
        s.verifications.push_back(run.evidence("exact Newton update rounded to the working grid", true, action,
                                               "obl.numeric.newton-update"));
        if (!run.transformation(std::move(s), before, after, action))
            return run.stopped();
        mpq_sub(delta.get(), next.get(), x.get());
        mpq_abs(delta.get(), delta.get());
        mpq_set(x.get(), next.get());
        if (mpq_cmp(delta.get(), tol.get()) <= 0)
            break;
    }
    detail::Mpq low, high;
    mpq_sub(low.get(), x.get(), tol.get());
    mpq_add(high.get(), x.get(), tol.get());
    const int sl = sign_at(run.f, low.get());
    const int sh = sign_at(run.f, high.get());
    const bool proved = sl * sh <= 0;
    const std::string observed = "f is " + sign_word(sl) + " a tolerance below the answer and " + sign_word(sh) +
                                 " a tolerance above it" + (proved ? ", so a root lies between" : ", so no root is proved nearby");
    Step s = run.envelope("num.newton-check", "Check for a sign change around the answer", ClaimType::Implication,
                          "A continuous function that changes sign across the tolerance has a root within it.");
    s.proof_obligations.push_back({"obl.numeric.bound-holds", "a root lies within the stated bound of the answer"});
    s.verifications.push_back(run.evidence("sign change by the intermediate value theorem", proved, observed,
                                           "obl.numeric.bound-holds"));
    if (!run.check(std::move(s), "a root lies within the tolerance of the last iterate",
                   "sign change by the intermediate value theorem", observed))
        return run.stopped();
    return run.finish(NumericOutcome::Approximated, mpq_node(run.arena, x.get()), mpq_node(run.arena, tol.get()), proved,
                      observed);
}

NumericResult quadrature(Run &run, const Rational &a0, const Rational &b0, int64_t n) {
    const bool simpson = run.spec->kind == Kind2::Simpson;
    if (!run.open_plan(nullptr, nullptr, nullptr))
        return run.stopped();
    detail::Mpq a, b, h, x, fx, sum, weight, value, bound, reach, maxd, term, exact, error, index;
    detail::mpq_set_rational(a.get(), a0);
    detail::mpq_set_rational(b.get(), b0);
    mpq_sub(h.get(), b.get(), a.get());
    mpq_set_si(index.get(), n, 1);
    mpq_div(h.get(), h.get(), index.get());
    mpq_set_ui(sum.get(), 0, 1);
    for (int64_t i = 0; i <= n; ++i) {
        if (!run.running())
            return run.stopped();
        mpq_set_si(index.get(), i, 1);
        mpq_mul(x.get(), h.get(), index.get());
        mpq_add(x.get(), x.get(), a.get());
        evaluate_at(run.f, x.get(), fx.get());
        const long w = simpson ? (i == 0 || i == n ? 1 : i % 2 == 1 ? 4 : 2) : (i == 0 || i == n ? 1 : 2);
        mpq_set_si(weight.get(), w, 1);
        mpq_mul(fx.get(), fx.get(), weight.get());
        mpq_add(sum.get(), sum.get(), fx.get());
    }
    mpq_set_si(weight.get(), 1, simpson ? 3 : 2);
    mpq_mul(value.get(), sum.get(), h.get());
    mpq_mul(value.get(), value.get(), weight.get());
    run.iterations = static_cast<size_t>(n);
    const NodeId value_node = mpq_node(run.arena, value.get());
    {
        const std::string action = simpson ? "h/3 times the ends plus four times each odd node plus twice each even interior node"
                                           : "h/2 times the ends plus twice each interior node";
        Step s = run.envelope("num.quad-sum", simpson ? "Sum Simpson's weights" : "Sum the trapezoid weights",
                              ClaimType::Definition, action,
                              simpson ? "Replace the curve on each pair of intervals by a parabola, whose area is exact."
                                      : "Replace the curve on each interval by a straight line, whose area is exact.");
        s.proof_obligations.push_back({"obl.numeric.rule-sum", "the approximation is the weighted sum the rule prescribes"});
        s.verifications.push_back(run.evidence("exact rational weighted sum", true, action, "obl.numeric.rule-sum"));
        if (!run.transformation(std::move(s), run.input, value_node, action))
            return run.stopped();
    }
    // Trapezoid error is at most (b-a) h^2 max|f''| / 12 and Simpson's (b-a) h^4 max|f''''| / 180.
    mpq_abs(reach.get(), a.get());
    mpq_abs(term.get(), b.get());
    if (mpq_cmp(term.get(), reach.get()) > 0)
        mpq_set(reach.get(), term.get());
    derivative_bound(run.f, simpson ? 4 : 2, reach.get(), maxd.get());
    mpq_sub(bound.get(), b.get(), a.get());
    for (int p = 0; p < (simpson ? 4 : 2); ++p)
        mpq_mul(bound.get(), bound.get(), h.get());
    mpq_mul(bound.get(), bound.get(), maxd.get());
    mpq_set_si(weight.get(), 1, simpson ? 180 : 12);
    mpq_mul(bound.get(), bound.get(), weight.get());
    const NodeId bound_node = mpq_node(run.arena, bound.get());
    {
        const std::string action = simpson ? "(b-a) h^4 max|f''''| / 180, with max|f''''| bounded from the coefficients"
                                           : "(b-a) h^2 max|f''| / 12, with max|f''| bounded from the coefficients";
        Step s = run.envelope("num.quad-bound", "State the error bound", ClaimType::Implication, action,
                              "The rule's error grows with how sharply the function bends, which a higher derivative measures.");
        s.proof_obligations.push_back({"obl.numeric.error-bound", "the error is at most the rule's bound"});
        s.verifications.push_back(run.evidence("derivative bound from the coefficients", true, action,
                                               "obl.numeric.error-bound"));
        if (!run.transformation(std::move(s), value_node, bound_node, action))
            return run.stopped();
    }
    // The exact integral from the antiderivative's coefficients, to see the bound hold.
    mpq_set_ui(exact.get(), 0, 1);
    for (size_t k = 0; k < run.f.size(); ++k) {
        detail::Mpq coefficient, pa, pb;
        detail::mpq_set_rational(coefficient.get(), run.f[k]);
        mpq_set_ui(pa.get(), 1, 1);
        mpq_set_ui(pb.get(), 1, 1);
        for (size_t p = 0; p <= k; ++p) {
            mpq_mul(pa.get(), pa.get(), a.get());
            mpq_mul(pb.get(), pb.get(), b.get());
        }
        mpq_sub(pb.get(), pb.get(), pa.get());
        mpq_set_ui(index.get(), static_cast<unsigned long>(k + 1), 1);
        mpq_div(pb.get(), pb.get(), index.get());
        mpq_mul(pb.get(), pb.get(), coefficient.get());
        mpq_add(exact.get(), exact.get(), pb.get());
    }
    mpq_sub(error.get(), exact.get(), value.get());
    mpq_abs(error.get(), error.get());
    const bool proved = mpq_cmp(error.get(), bound.get()) <= 0;
    const NodeId exact_node = mpq_node(run.arena, exact.get());
    const std::string observed = std::string("the exact integral is ") +
                                 (exact_node == kNoNode ? "too long to print" : print(run.arena, exact_node)) +
                                 (proved ? ", and the approximation is within the bound of it" : ", outside the bound");
    Step s = run.envelope("num.quad-check", "Compare with the exact integral", ClaimType::Implication,
                          "A polynomial has an exact antiderivative, so the bound can be checked rather than trusted.");
    s.proof_obligations.push_back({"obl.numeric.bound-holds", "the exact value lies within the stated bound of the answer"});
    s.verifications.push_back(run.evidence("exact integral comparison", proved, observed, "obl.numeric.bound-holds"));
    if (!run.check(std::move(s), "the exact integral lies within the bound of the approximation",
                   "exact integral comparison", observed))
        return run.stopped();
    if (!proved)
        return run.finish(NumericOutcome::VerificationFailed, kNoNode, kNoNode, false, observed);
    if (value_node == kNoNode || bound_node == kNoNode)
        return run.finish(NumericOutcome::ResourceExceeded, kNoNode, kNoNode, false, "the sum outgrew exact int64 storage");
    return run.finish(NumericOutcome::Approximated, value_node, bound_node, true, observed);
}

}  // namespace

std::optional<size_t> numeric_command_arity(std::string_view name) {
    const MethodSpec *spec = spec_named(name);
    return spec ? std::optional<size_t>(spec->arity) : std::nullopt;
}

const char *numeric_outcome_name(NumericOutcome outcome) {
    switch (outcome) {
        case NumericOutcome::Approximated: return "approximated";
        case NumericOutcome::OutsideEnvelope: return "outside envelope";
        case NumericOutcome::InvalidInput: return "invalid input";
        case NumericOutcome::NoSignChange: return "no sign change";
        case NumericOutcome::DidNotConverge: return "did not converge";
        case NumericOutcome::VerificationFailed: return "verification failed";
        case NumericOutcome::Cancelled: return "cancelled";
        case NumericOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

NumericResult numeric_method(Arena &arena, Derivation &derivation, NodeId call, const Budget &budget) {
    Run run(arena, derivation, call, budget);
    if (!run.running())
        return run.stopped();
    if (call >= arena.node_count() || arena.at(call).kind != Kind::Call)
        return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false, "this is not a numerical method call");
    run.spec = spec_named(arena.text(call));
    if (!run.spec)
        return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false, "this numerical method is not recorded");
    const ChildView args = arena.children(call);
    if (args.size() != run.spec->arity)
        return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false,
                          std::string(run.spec->name) + " takes " + std::to_string(run.spec->arity) + " arguments");
    if (derivation.request.numeric_mode != NumericMode::Exact)
        return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false,
                          "numerical methods compute exactly and run in Exact mode");
    if (arena.at(args[1]).kind != Kind::Symbol)
        return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false, "the second argument names the variable");
    run.variable = args[1];
    run.variable_name = arena.text(args[1]);
    const NodeId exact = exactify(arena, args[0]);
    if (exact == kNoNode || !read_poly(arena, exact, run.variable_name, &run.f))
        return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false,
                          "the function has to be a polynomial of degree at most 12 in " + run.variable_name +
                              " with rational coefficients");
    std::vector<Rational> numbers;
    for (size_t i = 2; i < args.size(); ++i) {
        Rational value;
        if (!constant_argument(arena, args[i], &value))
            return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false,
                              "every argument after the variable has to be an exact number");
        numbers.push_back(value);
    }
    const Rational zero{0, 1};
    const auto positive = [&zero](const Rational &r) { return r.num > 0 && !rational_equal(r, zero); };
    switch (run.spec->kind) {
        case Kind2::Bisection: {
            Rational width;
            if (!rational_sub(numbers[1], numbers[0], &width) || !positive(width))
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false, "the bracket needs a < b");
            if (!positive(numbers[2]))
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false, "the tolerance has to be positive");
            return bisection(run, numbers[0], numbers[1], numbers[2]);
        }
        case Kind2::Newton:
            if (!positive(numbers[1]))
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false, "the tolerance has to be positive");
            return newton(run, numbers[0], numbers[1]);
        case Kind2::Trapezoid:
        case Kind2::Simpson: {
            Rational width;
            if (!rational_sub(numbers[1], numbers[0], &width) || !positive(width))
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false, "the interval needs a < b");
            if (numbers[2].den != 1 || numbers[2].num < 1)
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false,
                                  "the number of intervals has to be a positive whole number");
            if (numbers[2].num > kMaxIntervals)
                return run.finish(NumericOutcome::OutsideEnvelope, kNoNode, kNoNode, false,
                                  "at most " + std::to_string(kMaxIntervals) + " intervals are summed");
            if (run.spec->kind == Kind2::Simpson && numbers[2].num % 2 != 0)
                return run.finish(NumericOutcome::InvalidInput, kNoNode, kNoNode, false,
                                  "Simpson's rule needs an even number of intervals");
            return quadrature(run, numbers[0], numbers[1], numbers[2].num);
        }
    }
    return run.stopped();
}

}  // namespace nps
