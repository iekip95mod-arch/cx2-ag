#include "nps/steps/calculus.h"

#include <algorithm>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/integrate.h"

namespace nps {
namespace {

int compare(const Rational &left, const Rational &right) {
    detail::Mpq a, b;
    detail::mpq_set_rational(a.get(), left);
    detail::mpq_set_rational(b.get(), right);
    return mpq_cmp(a.get(), b.get());
}

NodeId number(Arena &arena, const Rational &value) {
    const NodeId numerator = arena.integer(std::to_string(value.num));
    if (value.den == 1)
        return numerator;
    const NodeId denominator = arena.integer(std::to_string(value.den));
    const NodeId reciprocal = arena.binary(Kind::Pow, denominator, arena.integer("-1"));
    return arena.binary(Kind::Mul, numerator, reciprocal);
}

struct Interval {
    Rational lower;
    Rational upper;
};

// Integrated reaches here only unverified, which is the withheld precondition Refused names.
CalculusOutcome outcome_for(IntegrateOutcome outcome) {
    switch (outcome) {
        case IntegrateOutcome::Cancelled: return CalculusOutcome::Cancelled;
        case IntegrateOutcome::ResourceExceeded: return CalculusOutcome::ResourceExceeded;
        case IntegrateOutcome::VerificationFailed: return CalculusOutcome::VerificationFailed;
        case IntegrateOutcome::Integrated:
        case IntegrateOutcome::Refused: return CalculusOutcome::Refused;
        case IntegrateOutcome::UnsupportedForm:
        case IntegrateOutcome::NotAVariable: return CalculusOutcome::UnsupportedForm;
    }
    return CalculusOutcome::UnsupportedForm;
}

// What a form check found. BeyondCapacity is this build running out of room, a degree ceiling or a
// halt. Unsupported is a statement about the expression itself, which is the only one a caller may
// repeat back to the learner as a fact about their input.
enum class Form { Ok, BeyondCapacity, Unsupported };

// Said instead of a sentence about the learner's expression whenever the refusal is this build's
// ceiling rather than the mathematics. Both ceilings are reachable with a rational function.
const char *const degree_ceiling =
    "the native calculus engine handles polynomial degrees up to 32 and this request is higher";
const char *const coefficient_ceiling =
    "the leading coefficients of this expression overflow the exact arithmetic the native calculus engine uses";

struct Calculation {
    Arena &arena;
    Derivation &derivation;
    const Command &command;
    Meter meter;
    CalculusResult result;
    size_t mark;
    Backend *identity_backend;

    Calculation(Arena &a, Derivation &d, const Command &c, const Budget &budget, Backend *b)
        : arena(a), derivation(d), command(c), meter(budget), mark(d.mark()), identity_backend(b) {}

    bool work() { return !arena.failed() && meter.rewrite(); }

    // The status as well as the sentence, which is what linear.cc:714 does for the same class. The
    // shell prints the status verbatim on the note line at nps_v4.lua:2758-2759, and keeping the
    // four refusal kinds distinct is asked for whether or not a given renderer branches on it.
    void refuse(Form form, const char *ceiling, const char *unsupported) {
        const bool capacity = form == Form::BeyondCapacity;
        result.outcome = capacity ? CalculusOutcome::ResourceExceeded
                                  : CalculusOutcome::UnsupportedForm;
        result.status = capacity ? DerivationStatus::ResourceLimitReached
                                 : DerivationStatus::Unsupported;
        result.detail = capacity ? ceiling : unsupported;
    }

    void refuse(const char *ceiling) {
        result.outcome = CalculusOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = ceiling;
    }

    NodeId folded(NodeId expression) {
        if (arena.failed() || expression == kNoNode) return kNoNode;
        Rational value;
        return evaluate_rational(arena, expression, {}, &value) ? number(arena, value)
                                                               : canonicalize(arena, expression);
    }

    NodeId quotient(NodeId numerator, NodeId denominator) {
        return folded(arena.binary(Kind::Mul, numerator,
                      arena.binary(Kind::Pow, denominator, arena.integer("-1"))));
    }

    NodeId substitute(NodeId expression, NodeId replacement) {
        if (!work()) return kNoNode;
        if (expression == command.variable) return replacement;
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        if (children.empty()) return expression;
        std::vector<NodeId> replaced;
        for (NodeId child : children) {
            const NodeId next = substitute(child, replacement);
            if (next == kNoNode) return kNoNode;
            replaced.push_back(next);
        }
        return kind == Kind::Call ? arena.call(arena.text(expression), replaced)
                                  : arena.nary(kind, replaced);
    }

    bool range(NodeId expression, const Interval &variable, Interval *out) {
        if (!work()) return false;
        if (expression == command.variable) {
            *out = variable;
            return true;
        }
        Rational constant;
        if (evaluate_rational(arena, expression, {}, &constant) ||
            (compare(variable.lower, variable.upper) == 0 &&
             evaluate_rational(arena, expression, {{command.variable_name, variable.lower}}, &constant))) {
            *out = {constant, constant};
            return true;
        }
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        if (kind == Kind::Neg) {
            Interval inner;
            return range(children[0], variable, &inner) &&
                   rational_sub({}, inner.upper, &out->lower) &&
                   rational_sub({}, inner.lower, &out->upper);
        }
        if (kind == Kind::Add || kind == Kind::Mul) {
            Interval accumulated = kind == Kind::Add ? Interval{} : Interval{{1, 1}, {1, 1}};
            for (NodeId child : children) {
                Interval next;
                if (!range(child, variable, &next)) return false;
                if (kind == Kind::Add) {
                    if (!rational_add(accumulated.lower, next.lower, &accumulated.lower) ||
                        !rational_add(accumulated.upper, next.upper, &accumulated.upper)) return false;
                } else {
                    Rational products[4];
                    if (!rational_mul(accumulated.lower, next.lower, &products[0]) ||
                        !rational_mul(accumulated.lower, next.upper, &products[1]) ||
                        !rational_mul(accumulated.upper, next.lower, &products[2]) ||
                        !rational_mul(accumulated.upper, next.upper, &products[3])) return false;
                    const auto ends = std::minmax_element(std::begin(products), std::end(products),
                        [](const Rational &a, const Rational &b) { return compare(a, b) < 0; });
                    accumulated = {*ends.first, *ends.second};
                }
            }
            *out = accumulated;
            return true;
        }
        if (kind == Kind::Pow) {
            int64_t exponent = 0;
            Interval base;
            if (!folded_integer(arena, children[1], &exponent) || exponent < -32 || exponent > 32 ||
                !range(children[0], variable, &base)) return false;
            const bool crosses_zero = base.lower.num <= 0 && base.upper.num >= 0;
            if (exponent <= 0 && crosses_zero) return false;
            Rational a, b;
            if (!rational_power(base.lower, exponent, &a) || !rational_power(base.upper, exponent, &b))
                return false;
            out->lower = compare(a, b) < 0 ? a : b;
            out->upper = compare(a, b) < 0 ? b : a;
            if (exponent > 0 && exponent % 2 == 0 && crosses_zero) out->lower = {};
            return true;
        }
        return false;
    }

    struct DomainProof {
        std::string condition;
        std::string reason;
    };

    bool root_neighborhood(NodeId radicand, const Rational &point, std::vector<DomainProof> &proofs) {
        unsigned degree = 0, order = 0;
        Rational derivative;
        if (polynomial(radicand, &degree) != Form::Ok ||
            first_nonzero(radicand, point, degree, &order, &derivative) != Form::Ok)
            return false;
        const int right = derivative.num > 0 ? 1 : derivative.num < 0 ? -1 : 0;
        const int left = order % 2 == 0 ? right : -right;
        if ((command.direction >= 0 && right < 0) || (command.direction <= 0 && left < 0)) return false;
        const std::string side = command.direction > 0 ? "from the right" : command.direction < 0 ? "from the left" : "from both sides";
        const std::string condition = print(arena, radicand) + " >= 0 for " + command.variable_name +
            " sufficiently close to " + print(arena, command.point) + " " + side;
        const std::string reason = order > degree ? "The radicand is the zero polynomial, so it is nonnegative on both sides"
            : "The radicand's first nonzero derivative has order " + std::to_string(order) +
              " and value " + print(arena, number(arena, derivative)) +
              ". Its sign and the order's parity prove the radicand is positive " + side + " sufficiently near the approach point";
        proofs.push_back({condition, reason});
        return !arena.failed();
    }

    bool continuous(NodeId expression, const Interval &variable, std::vector<DomainProof> *neighborhood = nullptr,
                    bool smooth = false) {
        if (!work()) return false;
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        if (kind == Kind::Integer || kind == Kind::Decimal || expression == command.variable) return true;
        if (kind == Kind::Symbol)
            return arena.text(expression) == "pi" || arena.text(expression) == "e";
        if (kind == Kind::Neg || kind == Kind::Add || kind == Kind::Mul) {
            for (NodeId child : children)
                if (!continuous(child, variable, neighborhood, smooth)) return false;
            return true;
        }
        if (kind == Kind::Pow) {
            int64_t exponent = 0;
            if (!folded_integer(arena, children[1], &exponent) ||
                !continuous(children[0], variable, neighborhood, smooth)) return false;
            if (exponent > 0) return true;
            Interval base;
            return range(children[0], variable, &base) && (base.lower.num > 0 || base.upper.num < 0);
        }
        if (kind == Kind::Call && children.size() == 1) {
            const std::string name = arena.text(expression);
            if (!continuous(children[0], variable, neighborhood, smooth)) return false;
            if (name == "sin" || name == "cos" || name == "exp") return true;
            Interval argument;
            if (!range(children[0], variable, &argument)) return false;
            if (name == "ln") return argument.lower.num > 0;
            if (name != "sqrt" || argument.lower.num < 0) return false;
            if (smooth) return argument.lower.num > 0;
            return !neighborhood || argument.lower.num > 0 || root_neighborhood(children[0], variable.lower, *neighborhood);
        }
        return false;
    }

    bool linear_family() const {
        return command.kind == CommandKind::Tangent || command.kind == CommandKind::Linearize;
    }

    NodeId call(NodeId expression) {
        if (linear_family())
            return arena.call(command_kind_name(command.kind),
                              {expression, command.variable, command.point});
        if (command.kind == CommandKind::DefiniteIntegral)
            return arena.call("int", {expression, command.variable, command.lower, command.upper});
        std::vector<NodeId> arguments{expression, command.variable, command.point};
        if (command.direction != 0) arguments.push_back(arena.integer(std::to_string(command.direction)));
        return arena.call("limit", arguments);
    }

    StepId step(const char *rule, const std::string &title, NodeId before, NodeId after,
                const std::string &action, const std::string &reason, bool pending = false,
                ClaimType claim = ClaimType::EquivalentExpression) {
        if (before == kNoNode || (!pending && after == kNoNode) || arena.failed() || !meter.step()) return kNoStep;
        Step entry;
        entry.phase = "calculus";
        entry.rule_id = rule;
        entry.rule_name = title;
        entry.goal = title;
        entry.claim = claim;
        entry.explanation_short = reason;
        const std::string id = rule;
        if (id == "defint.interval")
            entry.explanation_detailed = "Before using endpoint subtraction, check every point between the bounds. A pole inside the interval makes ordinary antiderivative subtraction invalid even when both endpoints are finite.";
        else if (id == "defint.zero-width")
            entry.explanation_detailed = "Use this rule when the bounds coincide and the integrand is defined there. It does not assign a value to an undefined integrand.";
        else if (id == "defint.fundamental-theorem")
            entry.explanation_detailed = "Use the fundamental theorem after finding an antiderivative and proving continuity on the whole interval. Upper minus lower also handles reversed bounds without changing the requested sign.";
        else if (id == "defint.subtract")
            entry.explanation_detailed = "Once both endpoints have been substituted, combine their exact values. Keep the subtraction order and retain any symbolic constants without rounding.";
        else if (id == "tangent.point-value")
            entry.explanation_detailed = "A tangent line touches the curve at the point, so the function has to be defined there. Evaluate it exactly before any slope is taken, because an undefined value has no tangent to report.";
        else if (id == "tangent.slope")
            entry.explanation_detailed = "The derivative is a function of the variable. Substituting the point turns it into the one number the tangent line uses as its slope.";
        else if (id == "tangent.line")
            entry.explanation_detailed = "The point-slope form passes through the point with the derivative as its slope. That is an exact description of the line itself and says nothing yet about how far it stays near the curve.";
        else if (id == "tangent.linearization")
            entry.explanation_detailed = "The linearization is the tangent line read as an approximation of the function near the point. It is not an equality. The two agree at the point and drift apart as the variable moves away from it.";
        else if (id == "limit.continuity")
            entry.explanation_detailed = "Direct substitution determines a limit only when the expression is continuous at the approach point. Check denominators and real function domains before substituting.";
        else if (id == "limit.real-domain")
            entry.explanation_detailed = "At a polynomial zero, the first nonzero Taylor term determines the sign nearby. Its coefficient has the sign of the corresponding derivative because its factorial divisor is positive. An odd order changes sign across the point. An even order preserves it. The square root is continuous wherever its radicand is nonnegative.";
        else if (id == "limit.rational-form")
            entry.explanation_detailed = "Combine rational terms before inspecting zero over zero. Keep every denominator exclusion from the original expression, even if it cancels from the combined quotient.";
        else if (id == "limit.lhopital")
            entry.explanation_detailed = "Use L'Hopital's rule for a zero over zero limit with differentiable numerator and denominator. Nonzero polynomials have isolated zeros, which supplies the punctured-neighborhood conditions here. This is a limit rule, not equality of the two quotients at ordinary points.";
        else
            entry.explanation_detailed = "After the vanishing factors are resolved, check that the denominator is nonzero at the approach point. The resulting quotient is continuous and can be evaluated by substitution.";
        entry.proof_obligations.push_back({"obl.calculus.rule-preserves-value", "the calculus rule preserves the requested value"});
        VerificationRecord verification;
        verification.method = "rule-local invariant";
        verification.outcome = VerificationOutcome::Passed;
        verification.strength = EvidenceStrength::StructurallyValid;
        verification.detail = reason;
        entry.verifications.push_back(std::move(verification));
        TransformationPayload change;
        change.before = before;
        change.after = pending ? kNoNode : after;
        change.concrete_action = action;
        return derivation.add_transformation(kNoStep, std::move(entry), std::move(change));
    }

    void integral() {
        Rational lower, upper;
        if (!evaluate_rational(arena, command.lower, {}, &lower) ||
            !evaluate_rational(arena, command.upper, {}, &upper)) {
            result.detail = "native definite integrals currently require finite rational bounds";
            return;
        }
        const Interval interval = compare(lower, upper) <= 0 ? Interval{lower, upper} : Interval{upper, lower};
        if (!continuous(command.expression, interval)) {
            result.detail = "continuity on the complete interval could not be established, including its endpoints";
            return;
        }
        const NodeId question = call(command.expression);
        const std::string bounds = "[" + print(arena, number(arena, interval.lower)) + ", " +
                                   print(arena, number(arena, interval.upper)) + "]";
        if (step("defint.interval", "Check the interval", question, question,
                 "Check the integrand throughout " + bounds,
                 "The supported operations are continuous on this interval and no denominator vanishes") == kNoStep) return;
        if (compare(lower, upper) == 0) {
            const NodeId zero = arena.integer("0");
            if (step("defint.zero-width", "Equal bounds", question, zero,
                     "Use zero for an interval of zero width", "A continuous function has zero integral over a zero-width interval") != kNoStep) {
                result.outcome = CalculusOutcome::Evaluated;
                result.value = zero;
            }
            return;
        }
        const IntegrateResult primitive = integrate_particular(arena, derivation, command.expression,
                                                               command.variable, meter, interval.lower, identity_backend);
        if (primitive.outcome != IntegrateOutcome::Integrated ||
            primitive.status != DerivationStatus::SolvedAndVerified) {
            result.outcome = outcome_for(primitive.outcome);
            result.detail = primitive.detail;
            result.status = primitive.status;
            return;
        }
        if (!continuous(primitive.particular, interval)) {
            result.detail = "continuity of the antiderivative's real branch over the requested interval could not be established";
            return;
        }
        const NodeId upper_value = substitute(primitive.particular, command.upper);
        const NodeId lower_value = substitute(primitive.particular, command.lower);
        if (upper_value == kNoNode || lower_value == kNoNode) return;
        const NodeId difference = arena.binary(Kind::Add, upper_value, arena.unary(Kind::Neg, lower_value));
        if (step("defint.fundamental-theorem", "Evaluate the antiderivative at both bounds", question, difference,
                 "Substitute " + print(arena, command.upper) + " and " + print(arena, command.lower) +
                 " into the antiderivative, then subtract the lower value from the upper value",
                 "The integrand is continuous on the interval and the antiderivative passed its derivative check") == kNoStep) return;
        const NodeId answer = folded(difference);
        if (step("defint.subtract", "Subtract the endpoint values", difference, answer,
                 "Simplify the upper value minus the lower value",
                 "The order of subtraction preserves the orientation of the requested bounds") != kNoStep) {
            result.outcome = CalculusOutcome::Evaluated;
            result.value = answer;
        }
    }

    Form polynomial(NodeId expression, unsigned *degree) {
        if (!work()) return Form::BeyondCapacity;
        Rational value;
        if (evaluate_rational(arena, expression, {}, &value)) { *degree = 0; return Form::Ok; }
        if (expression == command.variable) { *degree = 1; return Form::Ok; }
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        if (kind == Kind::Neg) return polynomial(children[0], degree);
        if (kind == Kind::Add || kind == Kind::Mul) {
            unsigned accumulated = 0;
            Form ceiling = Form::Ok;
            for (NodeId child : children) {
                unsigned next = 0;
                const Form child_form = polynomial(child, &next);
                if (child_form == Form::Unsupported) return Form::Unsupported;
                if (child_form == Form::BeyondCapacity) { ceiling = Form::BeyondCapacity; continue; }
                accumulated = kind == Kind::Add ? std::max(accumulated, next) : accumulated + next;
                if (accumulated > 32) ceiling = Form::BeyondCapacity;
            }
            if (ceiling != Form::Ok) return ceiling;
            *degree = accumulated;
            return Form::Ok;
        }
        if (kind == Kind::Pow) {
            int64_t exponent = 0;
            unsigned base = 0;
            if (!folded_integer(arena, children[1], &exponent) || exponent < 1) return Form::Unsupported;
            if (exponent > 32) return Form::BeyondCapacity;
            const Form base_form = polynomial(children[0], &base);
            if (base_form != Form::Ok) return base_form;
            if (base * exponent > 32) return Form::BeyondCapacity;
            *degree = base * static_cast<unsigned>(exponent);
            return Form::Ok;
        }
        return Form::Unsupported;
    }

    struct Fraction { NodeId numerator = kNoNode; NodeId denominator = kNoNode; };

    Form fraction(NodeId expression, Fraction *out, std::vector<NodeId> *exclusions,
                  const Rational *point = nullptr) {
        if (!work()) return Form::BeyondCapacity;
        unsigned degree = 0;
        const Form whole = polynomial(expression, &degree);
        if (whole == Form::Ok) {
            *out = {expression, arena.integer("1")};
            return Form::Ok;
        }
        const Kind kind = arena.at(expression).kind;
        const ChildView children = arena.children(expression);
        if (kind == Kind::Call && point && continuous(expression, {*point, *point}, nullptr, true)) {
            *out = {expression, arena.integer("1")};
            return Form::Ok;
        }
        if (kind == Kind::Neg) {
            const Form inner = fraction(children[0], out, exclusions, point);
            if (inner != Form::Ok) return inner;
            out->numerator = arena.unary(Kind::Neg, out->numerator);
            return arena.failed() ? Form::BeyondCapacity : Form::Ok;
        }
        if (kind == Kind::Add || kind == Kind::Mul) {
            Fraction accumulated{arena.integer(kind == Kind::Add ? "0" : "1"), arena.integer("1")};
            Form ceiling = Form::Ok;
            for (NodeId child : children) {
                Fraction next;
                const Form child_form = fraction(child, &next, exclusions, point);
                if (child_form == Form::Unsupported) return Form::Unsupported;
                if (child_form == Form::BeyondCapacity) { ceiling = Form::BeyondCapacity; continue; }
                const NodeId numerator = kind == Kind::Mul
                    ? arena.binary(Kind::Mul, accumulated.numerator, next.numerator)
                    : arena.binary(Kind::Add, arena.binary(Kind::Mul, accumulated.numerator, next.denominator),
                                             arena.binary(Kind::Mul, next.numerator, accumulated.denominator));
                accumulated = {folded(numerator), folded(arena.binary(Kind::Mul, accumulated.denominator, next.denominator))};
            }
            if (ceiling != Form::Ok) return ceiling;
            *out = accumulated;
            return arena.failed() ? Form::BeyondCapacity : Form::Ok;
        }
        if (kind == Kind::Pow) {
            int64_t exponent = 0;
            Fraction base;
            if (!folded_integer(arena, children[1], &exponent)) return Form::Unsupported;
            if (exponent < -32 || exponent > 32) return Form::BeyondCapacity;
            const Form base_form = fraction(children[0], &base, exclusions, point);
            if (base_form != Form::Ok) return base_form;
            if (exponent <= 0) {
                exclusions->push_back(base.numerator);
                std::swap(base.numerator, base.denominator);
                exponent = -exponent;
            }
            const NodeId power = arena.integer(std::to_string(exponent));
            *out = {folded(arena.binary(Kind::Pow, base.numerator, power)),
                    folded(arena.binary(Kind::Pow, base.denominator, power))};
            return arena.failed() ? Form::BeyondCapacity : Form::Ok;
        }
        return whole == Form::BeyondCapacity ? Form::BeyondCapacity : Form::Unsupported;
    }

    Form first_nonzero(NodeId expression, const Rational &point, unsigned degree,
                       unsigned *order, Rational *coefficient, bool smooth = false) {
        for (unsigned current = 0; current <= degree; ++current) {
            if (smooth && !continuous(expression, {point, point}, nullptr, true)) return Form::Unsupported;
            if (!work()) return Form::BeyondCapacity;
            if (!evaluate_rational(arena, expression, {{command.variable_name, point}}, coefficient))
                return smooth ? Form::Unsupported : Form::BeyondCapacity;
            if (coefficient->num != 0) { *order = current; return Form::Ok; }
            if (current == degree) break;
            Derivation scratch;
            const DiffResult differentiated = differentiate(arena, scratch, expression, command.variable, meter);
            if (differentiated.outcome != DiffOutcome::Differentiated)
                return smooth ? Form::Unsupported : Form::BeyondCapacity;
            expression = folded(differentiated.derivative);
        }
        *order = degree + 1;
        return Form::Ok;
    }

    // Only ever called on something polynomial() has already accepted, so the one way it fails is
    // the exact arithmetic running out: x^d needs d factorial, and 21 factorial passes int64.
    Form leading(NodeId expression, unsigned bound, unsigned *degree, Rational *derivative) {
        *degree = 0;
        *derivative = {};
        for (unsigned order = 0; order <= bound; ++order) {
            Rational at_zero;
            if (!work() || !evaluate_rational(arena, expression, {{command.variable_name, {}}}, &at_zero))
                return Form::BeyondCapacity;
            if (at_zero.num != 0) { *degree = order; *derivative = at_zero; }
            if (order == bound) break;
            Derivation scratch;
            const DiffResult differentiated = differentiate(arena, scratch, expression, command.variable, meter);
            if (differentiated.outcome != DiffOutcome::Differentiated) return Form::BeyondCapacity;
            expression = folded(differentiated.derivative);
        }
        return Form::Ok;
    }

    void classify(int right, int left, const std::string &reason, bool at_infinity = false) {
        if (arena.failed() || !meter.step()) return;
        Step check;
        check.phase = "check";
        check.goal = "Determine whether the limit exists";
        check.rule_id = "limit.classify";
        check.rule_name = "Compare the limiting signs";
        check.claim = ClaimType::NoClaim;
        check.explanation_short = reason;
        check.explanation_detailed = at_infinity ? "The highest polynomial powers determine the unbounded growth and its sign along the requested approach to infinity." : "An infinite limit describes unbounded growth. A two-sided limit exists in the extended real sense only when both sides approach the same signed infinity. Opposite signs prove that the two-sided limit does not exist.";
        check.proof_obligations.push_back({"obl.limit.classification", "the polynomial orders and signs determine the limiting behavior"});
        VerificationRecord evidence;
        evidence.method = "polynomial order and sign comparison";
        evidence.outcome = VerificationOutcome::Passed;
        evidence.strength = EvidenceStrength::StructurallyValid;
        evidence.detail = reason;
        check.verifications.push_back(std::move(evidence));
        CheckPayload comparison;
        comparison.target_claim = "the requested limit has the reported behavior";
        comparison.check_method = "compare the orders of the vanishing or leading polynomial terms";
        comparison.expected_relation = command.direction == 0 ? "matching limiting signs on the requested approach" : "the sign on the requested side";
        const auto name = [](int sign) { return sign > 0 ? "+infinity" : "-infinity"; };
        comparison.observed_result = at_infinity ? std::string("requested infinite approach: ") + name(right) : command.direction == 0
            ? std::string("left: ") + name(left) + ", right: " + name(right)
            : std::string(command.direction < 0 ? "left: " : "right: ") + name(command.direction < 0 ? left : right);
        if (derivation.add_check(kNoStep, std::move(check), std::move(comparison)) == kNoStep) return;
        result.does_not_exist = command.direction == 0 && right != left;
        result.infinity = result.does_not_exist ? 0 : command.direction < 0 ? left : right;
        result.outcome = result.does_not_exist ? CalculusOutcome::DoesNotExist
                                               : CalculusOutcome::InfiniteLimit;
    }

    void infinite_limit(int approach) {
        if (command.direction != 0) {
            result.outcome = CalculusOutcome::InvalidInput;
            result.status = DerivationStatus::InvalidInput;
            result.detail = "a limit at infinity does not take a finite-point side argument";
            return;
        }
        Fraction rational;
        std::vector<NodeId> exclusions;
        unsigned numerator_bound = 0, denominator_bound = 0;
        Form form = fraction(command.expression, &rational, &exclusions);
        if (form == Form::Ok) form = polynomial(rational.numerator, &numerator_bound);
        if (form == Form::Ok) form = polynomial(rational.denominator, &denominator_bound);
        if (form != Form::Ok) {
            refuse(form, degree_ceiling, "native limits at infinity currently require a rational function");
            return;
        }
        for (NodeId excluded : exclusions) {
            unsigned bound = 0, degree = 0;
            Rational coefficient;
            Form excluded_form = polynomial(excluded, &bound);
            if (excluded_form == Form::Ok) excluded_form = leading(excluded, bound, &degree, &coefficient);
            if (excluded_form != Form::Ok) {
                refuse(excluded_form, coefficient_ceiling,
                       "native limits at infinity currently require a rational function");
                return;
            }
            if (coefficient.num == 0) {
                result.outcome = CalculusOutcome::InvalidInput;
                result.status = DerivationStatus::InvalidInput;
                result.detail = "the original expression has an identically zero denominator";
                return;
            }
        }
        unsigned numerator_degree = 0, denominator_degree = 0;
        Rational numerator, denominator;
        if (leading(rational.numerator, numerator_bound, &numerator_degree, &numerator) != Form::Ok ||
            leading(rational.denominator, denominator_bound, &denominator_degree, &denominator) != Form::Ok) {
            refuse(coefficient_ceiling);
            return;
        }
        if (denominator.num == 0) {
            result.outcome = CalculusOutcome::InvalidInput;
            result.status = DerivationStatus::InvalidInput;
            result.detail = "the denominator is identically zero";
            return;
        }
        const std::string reason = "The numerator has degree " + std::to_string(numerator_degree) +
            " and the denominator has degree " + std::to_string(denominator_degree) +
            ". Lower powers do not affect the limit at infinity";
        if (numerator.num == 0 || numerator_degree <= denominator_degree) {
            Rational value;
            if (numerator.num != 0 && numerator_degree == denominator_degree && !rational_div(numerator, denominator, &value)) return;
            const NodeId answer = number(arena, value);
            if (step("limit.infinity", "Compare the highest powers", call(command.expression), answer,
                     numerator.num == 0 || numerator_degree < denominator_degree
                         ? "Use zero because the numerator grows more slowly"
                         : "Divide the leading coefficients of the equal-degree polynomials",
                     reason) != kNoStep) {
                result.outcome = CalculusOutcome::Evaluated;
                result.value = answer;
            }
        } else {
            int sign = (numerator.num < 0) == (denominator.num < 0) ? 1 : -1;
            if (approach < 0 && (numerator_degree - denominator_degree) % 2 != 0) sign = -sign;
            classify(sign, sign, reason + ". The sign follows from the leading coefficients and the approach direction", true);
        }
    }

    void limit() {
        NodeId approach = command.point;
        int sign = 1;
        if (arena.at(approach).kind == Kind::Neg) {
            approach = arena.children(approach)[0];
            sign = -1;
        }
        if (arena.at(approach).kind == Kind::Symbol &&
            (arena.text(approach) == "infinity" || arena.text(approach) == "inf")) {
            infinite_limit(sign);
            return;
        }
        Rational point;
        if (!evaluate_rational(arena, command.point, {}, &point)) {
            result.detail = "native finite limits currently require an exact rational approach point";
            return;
        }
        std::vector<DomainProof> domain;
        if (continuous(command.expression, {point, point}, &domain)) {
            const NodeId question = call(command.expression);
            for (const DomainProof &proof : domain) {
                const StepId check = step("limit.real-domain", "Establish the real approach domain", question, question,
                    "Check the radicand on the requested approach sides", proof.reason);
                if (check == kNoStep) return;
                derivation.restrictions_at(check).push_back(proof.condition);
            }
            const NodeId substituted = substitute(command.expression, command.point);
            const NodeId answer = folded(substituted);
            const StepId evaluation = step("limit.continuity", "Substitute at a continuous point", question, answer,
                     "Replace " + command.variable_name + " with " + print(arena, command.point),
                     domain.empty() ? "Every supported operation is continuous at the approach point"
                                    : "Every supported operation is continuous on the proved real approach domain");
            if (evaluation != kNoStep) {
                for (const DomainProof &proof : domain)
                    derivation.restrictions_at(evaluation).push_back(proof.condition);
                result.outcome = CalculusOutcome::Evaluated;
                result.value = answer;
            }
            return;
        }
        Fraction rational;
        std::vector<NodeId> exclusions;
        unsigned numerator_degree = 0, denominator_degree = 0;
        Form form = fraction(command.expression, &rational, &exclusions, &point);
        if (form == Form::Ok) form = polynomial(rational.denominator, &denominator_degree);
        if (form != Form::Ok) {
            refuse(form, degree_ceiling,
                   "this limit needs a rule beyond the supported continuous and polynomial-denominator families");
            return;
        }
        const bool polynomial_numerator = polynomial(rational.numerator, &numerator_degree) == Form::Ok;
        for (NodeId excluded : exclusions) {
            unsigned degree = 0, order = 0;
            Rational coefficient;
            Form excluded_form = polynomial(excluded, &degree);
            if (excluded_form == Form::Ok)
                excluded_form = first_nonzero(excluded, point, degree, &order, &coefficient);
            if (excluded_form != Form::Ok) {
                refuse(excluded_form, coefficient_ceiling,
                       "this limit needs a rule beyond the supported continuous and polynomial-denominator families");
                return;
            }
            if (order > degree) {
                result.outcome = CalculusOutcome::InvalidInput;
                result.status = DerivationStatus::InvalidInput;
                result.detail = "the original expression has an identically zero denominator";
                return;
            }
        }
        unsigned numerator_order = 0, denominator_order = 0;
        Rational numerator_coefficient, denominator_coefficient;
        const Form denominator_form = first_nonzero(rational.denominator, point, denominator_degree,
                                                    &denominator_order, &denominator_coefficient);
        if (denominator_form != Form::Ok) {
            refuse(denominator_form, coefficient_ceiling,
                   "this limit needs a rule beyond the supported continuous and polynomial-denominator families");
            return;
        }
        if (denominator_order > denominator_degree) {
            result.outcome = CalculusOutcome::InvalidInput;
            result.status = DerivationStatus::InvalidInput;
            result.detail = "the denominator is identically zero";
            return;
        }
        const Form numerator_form = first_nonzero(rational.numerator, point,
            polynomial_numerator ? numerator_degree : denominator_order,
            &numerator_order, &numerator_coefficient, !polynomial_numerator);
        if (numerator_form != Form::Ok) {
            refuse(numerator_form, coefficient_ceiling,
                   "this limit needs a rule beyond the supported continuous and polynomial-denominator families");
            return;
        }
        NodeId expression = quotient(rational.numerator, rational.denominator);
        const StepId normalization = step("limit.rational-form", polynomial_numerator ? "Write a polynomial quotient" : "Write a quotient with a polynomial denominator",
            call(command.expression), call(expression), "Combine the rational expression over a common denominator",
            "Each excluded denominator is a nonzero polynomial, so its roots are isolated");
        if (normalization == kNoStep) return;
        for (NodeId excluded : exclusions)
            derivation.restrictions_at(normalization).push_back("For nearby " + command.variable_name + ": " + print(arena, excluded) + " is not zero");
        if (numerator_coefficient.num != 0 && numerator_order < denominator_order) {
            const int right = (numerator_coefficient.num < 0) == (denominator_coefficient.num < 0) ? 1 : -1;
            const int left = (denominator_order - numerator_order) % 2 == 0 ? right : -right;
            classify(right, left, "The denominator vanishes to order " + std::to_string(denominator_order) +
                     " and the numerator to order " + std::to_string(numerator_order) +
                     ". Their first nonzero derivatives establish the signs on each side");
            return;
        }
        NodeId numerator = rational.numerator, denominator = rational.denominator;
        for (unsigned order = 0; order < denominator_order; ++order) {
            const NodeId top_derivative = arena.call("diff", {numerator, command.variable});
            const NodeId bottom_derivative = arena.call("diff", {denominator, command.variable});
            const StepId change = step("limit.lhopital", "Resolve the zero over zero form", call(expression),
                call(quotient(top_derivative, bottom_derivative)), "Differentiate the numerator and denominator once",
                polynomial_numerator
                    ? "Both polynomials vanish here and the denominator has an isolated zero of finite order, so L'Hopital's rule applies"
                    : "Both functions vanish here and are smooth near the point. The nonconstant polynomial denominator has a derivative with isolated zeros, and the reduced limit exists, so L'Hopital's rule applies", true);
            if (change == kNoStep) return;
            const size_t child_mark = derivation.mark();
            const DiffResult top = differentiate(arena, derivation, numerator, command.variable, meter);
            const DiffResult bottom = top.outcome == DiffOutcome::Differentiated
                ? differentiate(arena, derivation, denominator, command.variable, meter) : DiffResult{};
            derivation.adopt_roots_since(child_mark, change);
            if (top.outcome != DiffOutcome::Differentiated || bottom.outcome != DiffOutcome::Differentiated) return;
            numerator = folded(top.derivative);
            denominator = folded(bottom.derivative);
            const NodeId next = quotient(numerator, denominator);
            if (!derivation.complete_transformation(change, call(next))) return;
            expression = next;
        }
        const NodeId answer = folded(substitute(expression, command.point));
        if (step("limit.evaluate", "Evaluate the reduced limit", call(expression), answer,
                 "Substitute the approach point into the reduced quotient",
                 "The reduced denominator is nonzero at the approach point") != kNoStep) {
            result.outcome = CalculusOutcome::Evaluated;
            result.value = answer;
        }
    }

    // CALC-010. The supported envelope is an expression whose value and whose derivative both fold
    // to exact rationals at the requested point, which covers the polynomials and rational functions
    // the native differentiation engine already handles. Anything that leaves a symbol standing at
    // the point is refused rather than approximated, and a point outside the domain is refused as a
    // statement about the expression rather than about this build.
    void tangent() {
        result.approximate = command.kind == CommandKind::Linearize;
        Rational point;
        if (!evaluate_rational(arena, command.point, {}, &point)) {
            refuse(Form::Unsupported, degree_ceiling,
                   "the tangent point must be an exact number");
            return;
        }
        const NodeId at_point = folded(substitute(command.expression, command.point));
        Rational height;
        if (at_point == kNoNode || !evaluate_rational(arena, at_point, {}, &height)) {
            if (!work()) return;
            refuse(Form::Unsupported, degree_ceiling,
                   "the expression has no exact value at that point, so it has no tangent line there");
            return;
        }
        if (step("tangent.point-value", "Evaluate the function at the point", call(command.expression),
                 at_point, "Substitute the point into the expression",
                 "The expression is defined at the point, so the tangent line touches the curve there",
                 false, ClaimType::Definition)
            == kNoStep)
            return;
        const DiffResult differentiated =
            differentiate(arena, derivation, command.expression, command.variable, meter, identity_backend);
        if (differentiated.outcome != DiffOutcome::Differentiated || differentiated.derivative == kNoNode) {
            switch (differentiated.outcome) {
                case DiffOutcome::Cancelled:
                    result.outcome = CalculusOutcome::Cancelled;
                    result.status = DerivationStatus::Cancelled;
                    break;
                case DiffOutcome::ResourceExceeded:
                    result.outcome = CalculusOutcome::ResourceExceeded;
                    result.status = DerivationStatus::ResourceLimitReached;
                    break;
                case DiffOutcome::Refused:
                    result.outcome = CalculusOutcome::Refused;
                    result.status = DerivationStatus::Unsupported;
                    break;
                default:
                    result.outcome = CalculusOutcome::UnsupportedForm;
                    result.status = DerivationStatus::Unsupported;
                    break;
            }
            result.detail = differentiated.detail.empty()
                ? "the native differentiation engine has no rule for this expression"
                : differentiated.detail;
            return;
        }
        const NodeId slope = folded(substitute(differentiated.derivative, command.point));
        Rational gradient;
        if (slope == kNoNode || !evaluate_rational(arena, slope, {}, &gradient)) {
            if (!work()) return;
            refuse(Form::Unsupported, degree_ceiling,
                   "the derivative has no exact value at that point, so the slope is undefined there");
            return;
        }
        // A point substitution instantiates the function at one point rather than rewriting it, so
        // neither of these two states an equivalent expression: VER-002 reads the derivative and its
        // value at the point as disagreeing everywhere else.
        if (step("tangent.slope", "Evaluate the derivative at the point", differentiated.derivative,
                 slope, "Substitute the point into the derivative",
                 "The derivative is defined at the point, so it is the slope of the tangent line",
                 false, ClaimType::Definition)
            == kNoStep)
            return;
        result.slope = slope;
        result.point_value = at_point;
        const NodeId offset = arena.binary(Kind::Add, command.variable,
                                           arena.unary(Kind::Neg, command.point));
        const NodeId line = folded(arena.binary(Kind::Add, at_point,
                                                arena.binary(Kind::Mul, slope, offset)));
        if (line == kNoNode || !work()) return;
        const bool approximate = result.approximate;
        if (step(approximate ? "tangent.linearization" : "tangent.line",
                 approximate ? "Assemble the linearization" : "Assemble the tangent line",
                 call(command.expression), line,
                 approximate ? "Write the point-slope line as the local approximation"
                             : "Write the point-slope line through the point",
                 approximate
                     ? "Near the point the function is approximated by this line. The relation is an "
                       "approximation rather than an equality away from the point"
                     : "The line passes through the point with the derivative as its slope",
                 false, approximate ? ClaimType::NoClaim : ClaimType::Definition)
            == kNoStep)
            return;
        if (!verify_line(line, height, gradient, point)) return;
        result.outcome = CalculusOutcome::Evaluated;
        result.value = line;
    }

    // The final check the family is required to have. The assembled line is evaluated exactly at the
    // point and one unit away, which pins both the value it must match and the slope it must have,
    // and it fails if either does. An affine function is determined by those two readings.
    bool verify_line(NodeId line, const Rational &height, const Rational &gradient,
                     const Rational &point) {
        Rational shifted;
        Rational at_point;
        Rational away;
        const bool stepped = rational_add(point, {1, 1}, &shifted);
        const bool read = stepped &&
            evaluate_rational(arena, line, {{command.variable_name, point}}, &at_point) &&
            evaluate_rational(arena, line, {{command.variable_name, shifted}}, &away);
        Rational rise;
        const bool matched = read && rational_sub(away, at_point, &rise) &&
                             compare(at_point, height) == 0 && compare(rise, gradient) == 0;
        if (!meter.step() || arena.failed()) return false;
        Step check;
        check.phase = "check";
        check.goal = "Check the line against the point and the slope";
        check.rule_id = "tangent.check-line";
        check.rule_name = "Tangent agreement at the point";
        check.claim = ClaimType::EquivalentExpression;
        check.explanation_short =
            "Evaluate the assembled line at the point and one unit away, which reads back its value "
            "and its slope";
        check.proof_obligations.push_back(
            {"obl.calculus.tangent-agreement",
             "the line meets the curve at the point and has the derivative as its slope"});
        VerificationRecord evidence;
        evidence.method = "exact evaluation at the point and one unit away";
        evidence.outcome = matched ? VerificationOutcome::Passed
                         : read ? VerificationOutcome::Failed : VerificationOutcome::Inconclusive;
        evidence.strength = strength_for(evidence.outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
        evidence.detail = matched
            ? "the line matches the function value at the point and rises by the derivative"
            : read ? "the line disagrees with the function value or the derivative at the point"
                   : "the assembled line could not be evaluated exactly";
        check.verifications.push_back(std::move(evidence));
        CheckPayload payload;
        payload.target_claim = "the assembled line is tangent to the curve at the point";
        payload.check_method = "exact evaluation at the point and one unit away";
        payload.expected_relation = "the function value at the point and the derivative as the rise";
        payload.observed_result = read ? print(arena, line) : "not exactly evaluable";
        derivation.add_check(kNoStep, std::move(check), std::move(payload));
        if (matched) return true;
        result.outcome = CalculusOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the assembled line failed its tangency check, so the answer is withheld";
        return false;
    }

    void record_comparison(VerificationOutcome outcome, const std::string &detail,
                           CheckPayload comparison_record) {
        Step check;
        check.phase = "check";
        check.goal = "Compare with Giac";
        check.rule_id = "calculus.check-giac";
        check.rule_name = "Independent CAS comparison";
        check.claim = ClaimType::EquivalentExpression;
        check.explanation_short = "Ask Giac the original question and simplify the difference from the native answer";
        check.proof_obligations.push_back({"obl.calculus.giac-agreement", "the native result agrees with Giac's independent calculation"});
        VerificationRecord evidence;
        evidence.method = "Giac exact difference";
        evidence.outcome = outcome;
        evidence.strength = strength_for(outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
        evidence.detail = detail;
        check.verifications.push_back(std::move(evidence));
        derivation.add_check(kNoStep, std::move(check), std::move(comparison_record));
    }

    // Cancellation is the learner asking to stop, so the answer goes with it. A backend that ran out
    // of room says nothing about the native answer, so that answer stays and the record carries the
    // refusal, which is what outcome_from then reads as solved but unchecked.
    bool gave_up(const Response &response, const std::string &what, const char *method,
                 const char *relation) {
        if (response.tag != ResultTag::Cancelled && response.tag != ResultTag::ResourceFailure &&
            response.tag != ResultTag::Timeout)
            return false;
        if (response.tag == ResultTag::Cancelled || result.value == kNoNode) {
            result.value = kNoNode;
            result.outcome = response.tag == ResultTag::Cancelled ? CalculusOutcome::Cancelled
                                                                  : CalculusOutcome::ResourceExceeded;
            result.status = response.tag == ResultTag::Cancelled ? DerivationStatus::Cancelled
                                                                 : DerivationStatus::ResourceLimitReached;
            result.detail = response.detail;
            return true;
        }
        if (!meter.step()) return true;
        // The payload says what was actually done, which is integrate.cc:818-827's reason for
        // rewriting its own. Neither refusal simplified a difference, so neither may say it did.
        const std::string said = response.detail.empty() ? std::string(tag_name(response.tag))
                                                         : response.detail;
        CheckPayload unfinished;
        unfinished.target_claim = "the native answer agrees with an independent Giac calculation";
        unfinished.check_method = method;
        // Empty where no difference was ever formed, which lua_module.cc:1271 then skips rather than
        // showing the learner an expected value for a comparison that did not happen.
        unfinished.expected_relation = relation;
        unfinished.observed_result = tag_name(response.tag) + (": " + said);
        record_comparison(VerificationOutcome::Inconclusive, what + said, std::move(unfinished));
        return true;
    }

    void cross_check(Backend &backend) {
        if (linear_family()) return;
        if (arena.failed() || meter.stopped() || result.infinity != 0 || result.does_not_exist ||
            (result.value == kNoNode && result.status != DerivationStatus::Unsupported &&
             result.status != DerivationStatus::PartiallySolved)) return;
        if (!meter.checkpoint() || !meter.backend_call()) return;
        Request request;
        request.op = command.kind == CommandKind::Limit ? Op::Limit : Op::Integrate;
        request.target = command.expression;
        request.variable = command.variable;
        request.lower = command.lower;
        request.upper = command.upper;
        request.point = command.point;
        request.direction = command.direction;
        Adapter adapter(arena, backend);
        result.backend_attempted = true;
        result.backend_result = adapter.run(request);
        const Response &response = result.backend_result;
        if (gave_up(response, "Giac could not answer the original question: ",
                    "ask Giac the same question, which did not finish", ""))
            return;
        if (arena.failed() || !response.usable()) return;
        const NodeId observed = response.single_value();
        if (observed == kNoNode) return;
        if (result.value == kNoNode) {
            result.value = observed;
            result.answer_only = true;
            return;
        }
        if (response.tag != ResultTag::Exact) return;
        const NodeId difference = arena.binary(Kind::Add, result.value, arena.unary(Kind::Neg, observed));
        if (arena.failed() || !meter.checkpoint() || !meter.backend_call()) return;
        Request comparison;
        comparison.op = Op::IsZero;
        comparison.target = difference;
        const Response checked = adapter.run(comparison);
        result.comparison_attempted = true;
        result.comparison_result = checked;
        if (gave_up(checked, "Giac could not compare the two answers: ",
                    "Giac evaluation followed by exact difference simplification, which did not "
                    "finish",
                    "zero"))
            return;
        Rational residual;
        if (arena.failed() || checked.tag != ResultTag::Exact || checked.single_value() == kNoNode ||
            !evaluate_rational(arena, checked.single_value(), {}, &residual) || !meter.step()) return;
        result.backend_compared = true;
        result.agrees = residual.num == 0;
        CheckPayload compared_record;
        compared_record.target_claim = "the independently computed calculus answers agree";
        compared_record.check_method = "Giac evaluation followed by exact difference simplification";
        compared_record.expected_relation = "zero";
        compared_record.observed_result = print(arena, checked.single_value());
        record_comparison(result.agrees ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                          result.agrees ? "the exact difference is zero"
                                        : "the exact difference is nonzero",
                          std::move(compared_record));
        if (!result.agrees) {
            result.value = kNoNode;
            result.outcome = CalculusOutcome::VerificationFailed;
            result.status = DerivationStatus::VerificationFailed;
            result.detail = "Giac disagrees with the native result, so the answer is withheld";
        }
    }

    CalculusResult finish() {
        const NodeId model = arena.failed() || result.status == DerivationStatus::InvalidInput
            ? kNoNode : call(command.expression);
        if (arena.failed() || meter.stopped()) {
            result.value = kNoNode;
            result.infinity = 0;
            result.does_not_exist = false;
            result.outcome = meter.halt() == Halt::Cancelled ? CalculusOutcome::Cancelled : CalculusOutcome::ResourceExceeded;
            result.status = meter.halt() == Halt::Cancelled ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached;
            result.detail = arena.failed() ? status_name(arena.status()) : halt_name(meter.halt());
            keep_verified_prefix(derivation, mark, arena);
        } else if (!result.answer_only && (result.value != kNoNode || result.infinity != 0 || result.does_not_exist)) {
            result.status = derivation.outcome_from(mark);
        } else if (result.detail.empty()) {
            result.detail = "the native calculation could not establish a complete verified result";
        }
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id =
            command.kind == CommandKind::Tangent ? "calculus.tangent-line.single-variable"
          : command.kind == CommandKind::Linearize ? "calculus.linearization.single-variable"
          : command.kind == CommandKind::Limit ? "calculus.limit.single-variable"
                                               : "calculus.integral.definite.single-variable";
        context.requested_method = command_kind_name(command.kind);
        context.original_expression = derivation.request.original_expression;
        context.normalized_problem_model = model;
        context.angle_convention = angle_mode_name(derivation.request.angle_mode);
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

}

const char *calculus_outcome_name(CalculusOutcome outcome) {
    switch (outcome) {
        case CalculusOutcome::Evaluated: return "evaluated";
        case CalculusOutcome::InfiniteLimit: return "infinite limit";
        case CalculusOutcome::DoesNotExist: return "does not exist";
        case CalculusOutcome::UnsupportedForm: return "unsupported form";
        case CalculusOutcome::InvalidInput: return "invalid input";
        case CalculusOutcome::Refused: return "refused";
        case CalculusOutcome::VerificationFailed: return "verification failed";
        case CalculusOutcome::Cancelled: return "cancelled";
        case CalculusOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

CalculusResult calculus_walkthrough(Arena &arena, Derivation &derivation, const Command &command,
                                   const Budget &budget, Backend *backend) {
    Calculation calculation(arena, derivation, command, budget, backend);
    const auto present = [&arena](NodeId node) { return node < arena.node_count(); };
    const bool complete = command.status == CommandStatus::Ready && present(command.expression) &&
        present(command.variable) && arena.at(command.variable).kind == Kind::Symbol &&
        command.variable_name == arena.text(command.variable) &&
        ((command.kind == CommandKind::DefiniteIntegral && present(command.lower) && present(command.upper)) ||
         ((command.kind == CommandKind::Tangent || command.kind == CommandKind::Linearize) &&
          present(command.point)) ||
         (command.kind == CommandKind::Limit && present(command.point) && command.direction >= -1 && command.direction <= 1));
    if (!complete) {
        calculation.result.outcome = CalculusOutcome::InvalidInput;
        calculation.result.status = DerivationStatus::InvalidInput;
        calculation.result.detail = "a complete calculus command is required";
    } else if (derivation.request.numeric_mode != NumericMode::Exact) {
        calculation.result.detail = "these native calculus walkthroughs currently require Exact mode";
    } else if (derivation.request.angle_mode == AngleMode::Degrees &&
               angle_dependent(arena, command.expression, command.variable)) {
        calculation.result.detail = "the native calculus rules for trigonometric functions assume radians, and degree mode is active";
    } else if (calculation.work()) {
        if (command.kind == CommandKind::DefiniteIntegral) calculation.integral();
        else if (command.kind == CommandKind::Limit) calculation.limit();
        else if (command.kind == CommandKind::Tangent || command.kind == CommandKind::Linearize)
            calculation.tangent();
    }
    const bool radian_only = derivation.request.angle_mode == AngleMode::Degrees && complete &&
                             angle_dependent(arena, command.expression, command.variable);
    if (backend && complete && derivation.request.numeric_mode == NumericMode::Exact && !radian_only)
        calculation.cross_check(*backend);
    return calculation.finish();
}

}
