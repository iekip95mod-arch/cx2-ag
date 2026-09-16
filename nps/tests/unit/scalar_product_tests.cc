#include <string>

#include "nps/core/print.h"
#include "nps/physics/scalar_product.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Vector parsed_vector(const std::string &text, const char *frame = nullptr) {
    Vector value;
    std::string error;
    if (!parse_vector(text, &value, &error)) {
        value.rank = 0;
        value.frame.name = "parse failed: " + error;
        return value;
    }
    if (frame != nullptr)
        value.frame.name = frame;
    return value;
}

struct Run {
    Run(const Vector &first, const Vector &second, bool angle = false,
        const Budget &budget = Budget()) {
        ScalarProductProblem problem;
        problem.first = first;
        problem.second = second;
        problem.angle = angle;
        result = solve_scalar_product(arena, derivation, problem, budget);
    }

    Arena arena;
    Derivation derivation;
    ScalarProductResult result;
};

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return true;
    }
    return false;
}

const TransformationPayload *moved_by(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        if (derivation.at(id).rule_id == rule)
            return derivation.transformation(id);
    }
    return nullptr;
}

std::string check_detail(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        if (derivation.at(id).rule_id == rule && !derivation.at(id).verifications.empty())
            return derivation.at(id).verifications[0].detail;
    }
    return "no such rule";
}

}  // namespace

void run_scalar_product_tests(TestSink &t) {
    {
        Run solved(parsed_vector("3 i + 4 j N"), parsed_vector("2 i + 1 j m"));
        t.equal(scalar_product_outcome_name(solved.result.outcome), "solved",
                "a force dotted with a displacement is a scalar");
        t.check(solved.result.has_value && solved.result.value.value.num == 10 &&
                    solved.result.value.value.den == 1,
                "3*2 + 4*1 is exactly 10");
        t.equal(solved.result.value_text, "10 kg m^2/s^2",
                "the product carries the product of the two dimensions");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.vectors.cartesian-scalar-product",
                "the derivation identifies the scalar product family");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "every recorded claim is verified");
        t.check(has_rule(solved.derivation, "vec.dot.plan"), "the derivation records its plan");
        t.check(has_rule(solved.derivation, "vec.dot.check-rank") &&
                    has_rule(solved.derivation, "vec.dot.check-frame") &&
                    has_rule(solved.derivation, "vec.dot.check-dimension"),
                "rank, frame and the product dimension are each checked");
        t.check(has_rule(solved.derivation, "vec.dot.component-sum"),
                "the component sum is its own recorded step");
        t.check(has_rule(solved.derivation, "vec.dot.check-commutative"),
                "the derivation checks that the operand order does not matter");
        t.check(!has_rule(solved.derivation, "vec.dot.check-nonzero") &&
                    !has_rule(solved.derivation, "vec.dot.interpret-angle") &&
                    solved.result.angle == ScalarAngle::NotAsked,
                "a product that was not asked for an angle checks nothing about direction");
    }
    {
        Run solved(parsed_vector("3 i + 4 j N"), parsed_vector("2 i + 1 j m"));
        const TransformationPayload *law = moved_by(solved.derivation, "vec.dot.definition");
        const TransformationPayload *numbers = moved_by(solved.derivation, "vec.dot.substitute");
        t.check(law != nullptr && numbers != nullptr,
                "the definition and the substitution are each their own move");
        if (law != nullptr && numbers != nullptr) {
            t.equal(print(solved.arena, law->after), "(dot(a, b) = ((ax * bx) + (ay * by)))",
                    "the definition leaves the component reading in symbols");
            t.check(numbers->before == law->after,
                    "the substitution starts from the form the definition left");
            t.equal(print(solved.arena, numbers->after), "(dot(a, b) = ((3 * 2) + (4 * 1)))",
                    "the substitution is what pairs each component with its partner");
        }
        t.check(solved.derivation.at(static_cast<StepId>(0)).rule_id == "vec.dot.plan" &&
                    check_detail(solved.derivation, "vec.dot.definition") ==
                        "the ranks, frames and dimensions passed",
                "the definition is applied only after its conditions pass");
        t.evidence("PHYS-025",
                   has_rule(solved.derivation, "vec.dot.plan") &&
                       has_rule(solved.derivation, "vec.dot.check-rank") &&
                       has_rule(solved.derivation, "vec.dot.check-frame") &&
                       has_rule(solved.derivation, "vec.dot.check-dimension") &&
                       has_rule(solved.derivation, "vec.dot.definition") &&
                       has_rule(solved.derivation, "vec.dot.substitute") &&
                       has_rule(solved.derivation, "vec.dot.component-sum") &&
                       has_rule(solved.derivation, "vec.dot.check-commutative") &&
                       has_rule(solved.derivation, "vec.dot.check-magnitude-bound"),
                   "the scalar product exposes its model preconditions, both readings of the "
                   "definition, the substitution, the component sum and its two independent checks");
    }
    {
        // The geometric reading recorded without a square root: a b cos(phi) cannot exceed a b in
        // size, which is (a . b)^2 at most (a . a)(b . b) and every term of that is exact.
        Run solved(parsed_vector("3 i + 4 j m"), parsed_vector("6 i + 8 j m"));
        t.equal(scalar_product_outcome_name(solved.result.outcome), "solved",
                "two parallel vectors dot to the product of their magnitudes");
        t.check(solved.result.value.value.num == 50 && solved.result.value.value.den == 1,
                "3*6 + 4*8 is exactly 50");
        t.equal(check_detail(solved.derivation, "vec.dot.check-magnitude-bound"),
                "2500 against 2500",
                "parallel vectors sit exactly on the bound, which is where cos(phi) reaches one");
    }
    {
        Run perpendicular(parsed_vector("1 i + 0 j m"), parsed_vector("0 i + 1 j m"), true);
        t.equal(scalar_product_outcome_name(perpendicular.result.outcome), "solved",
                "perpendicular vectors still have a scalar product");
        t.check(perpendicular.result.value.value.num == 0, "i dotted with j is exactly zero");
        t.equal(scalar_angle_name(perpendicular.result.angle), "perpendicular",
                "a zero product places the angle at a right angle");
        t.check(has_rule(perpendicular.derivation, "vec.dot.angle-plan") &&
                    !has_rule(perpendicular.derivation, "vec.dot.plan"),
                "asking for the angle selects the plan that registers the non-zero precondition");
        t.check(has_rule(perpendicular.derivation, "vec.dot.check-nonzero") &&
                    has_rule(perpendicular.derivation, "vec.dot.interpret-angle"),
                "the angle reading checks both operands point somewhere first");
    }
    {
        Run acute(parsed_vector("2 i + 0 j m"), parsed_vector("1 i + 1 j m"), true);
        t.equal(scalar_angle_name(acute.result.angle), "acute",
                "a positive product places the angle inside a right angle");
        Run obtuse(parsed_vector("2 i + 0 j m"), parsed_vector("-1 i + 1 j m"), true);
        t.equal(scalar_angle_name(obtuse.result.angle), "obtuse",
                "a negative product places the angle outside a right angle");
        t.check(obtuse.result.value.value.num == -2 && obtuse.result.has_value,
                "an obtuse pair still reports its negative product");
    }
    {
        Run refused(parsed_vector("1 i + 0 j m"), parsed_vector("1 i + 0 j + 0 k m"));
        t.equal(scalar_product_outcome_name(refused.result.outcome), "rank mismatch",
                "a rank-two vector cannot be dotted with a rank-three one");
        t.check(!refused.result.has_value && refused.result.value_text.empty(),
                "a rank refusal offers no value");
        t.check(!has_rule(refused.derivation, "vec.dot.definition") &&
                    has_rule(refused.derivation, "vec.dot.check-rank"),
                "the rank refusal is recorded and the definition is never applied");
    }
    {
        Run refused(parsed_vector("1 i + 0 j m", "lab"), parsed_vector("1 i + 0 j m", "road"));
        t.equal(scalar_product_outcome_name(refused.result.outcome), "frame mismatch",
                "two named frames are not silently treated as one");
        t.check(!refused.result.has_value &&
                    !has_rule(refused.derivation, "vec.dot.definition"),
                "the frame refusal applies no definition");
    }
    {
        Run refused(parsed_vector("0 i + 0 j m"), parsed_vector("3 i + 4 j m"), true);
        t.equal(scalar_product_outcome_name(refused.result.outcome), "zero vector",
                "the zero vector has no direction, so it has no angle with anything");
        t.check(refused.result.angle == ScalarAngle::NotAsked && refused.result.angle_text.empty(),
                "the angle refusal places no angle");
        Run solved(parsed_vector("0 i + 0 j m"), parsed_vector("3 i + 4 j m"));
        t.equal(scalar_product_outcome_name(solved.result.outcome), "solved",
                "the same zero vector still has a scalar product, which is what makes the angle "
                "its own refusal");
        t.check(solved.result.value.value.num == 0, "dotting with the zero vector gives zero");
    }
    {
        Run solved(parsed_vector("20 i + 0 j cm"), parsed_vector("0 i + 15 j N"));
        t.equal(scalar_product_outcome_name(solved.result.outcome), "solved",
                "a centimetre length dots with a newton force after conversion");
        t.check(solved.result.value.value.num == 0,
                "perpendicular operands give zero whatever their units");
        t.check(has_rule(solved.derivation, "vec.dot.convert-si"),
                "a non-SI operand records its exact conversion");
        Run si_only(parsed_vector("1 i + 0 j m"), parsed_vector("0 i + 1 j m"));
        t.check(!has_rule(si_only.derivation, "vec.dot.convert-si"),
                "two SI operands record no conversion, so the step above is not unconditional");
    }
    {
        Run solved(parsed_vector("3.0 i + 4.0 j m"), parsed_vector("2.0 i + 1.0 j m"));
        t.equal(scalar_product_outcome_name(solved.result.outcome), "solved",
                "measured components carry their precision through to the report");
        t.check(has_rule(solved.derivation, "vec.dot.report-precision"),
                "a measured product rounds once, at the end");
        Run exact(parsed_vector("3 i + 4 j m"), parsed_vector("2 i + 1 j m"));
        t.check(!has_rule(exact.derivation, "vec.dot.report-precision"),
                "an exact product rounds nothing, so the reporting step is not unconditional");
    }
    {
        Budget budget;
        budget.max_steps = 3;
        Run stopped(parsed_vector("3 i + 4 j N"), parsed_vector("2 i + 1 j m"), false, budget);
        t.equal(scalar_product_outcome_name(stopped.result.outcome), "resource exceeded",
                "a step budget halts the walkthrough rather than truncating the answer");
        t.check(!stopped.result.has_value && stopped.result.value_text.empty(),
                "a halted scalar product offers no value");
        t.equal(derivation_status_name(stopped.result.status), "resource limit reached",
                "the halt is recorded as a resource limit rather than a refusal");
    }
}

}  // namespace nps
