#include <string>

#include "nps/physics/vector_cross.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Vector parsed_vector(const std::string &text) {
    Vector value;
    std::string error;
    if (!parse_vector(text, &value, &error)) {
        value.rank = 0;
        value.frame.name = "parse failed: " + error;
    }
    return value;
}

VectorCrossResult solve(const Vector &first, const Vector &second, Derivation *derivation,
                        const Budget &budget = Budget()) {
    Arena arena;
    VectorCrossProblem problem;
    problem.first = first;
    problem.second = second;
    return solve_vector_cross(arena, *derivation, problem, budget);
}

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return true;
    }
    return false;
}

}  // namespace

void run_vector_cross_tests(TestSink &t) {
    {
        Derivation derivation;
        VectorCrossResult result = solve(parsed_vector("1 i + 0 j + 0 k m"),
                                         parsed_vector("0 i + 1 j + 0 k m"), &derivation);
        t.equal(vector_cross_outcome_name(result.outcome), "solved", "two rank-three vectors cross");
        t.check(result.has_value, "a solved cross product carries its structured value");
        t.check(result.value.x.num == 0 && result.value.y.num == 0 && result.value.z.num == 1 &&
                    result.value.z.den == 1,
                "i cross j is exactly k");
        t.equal(derivation.context.problem_family_id,
                "physics.vectors.cartesian-cross-product.three-dimension",
                "the derivation identifies the vector cross product family");
        t.equal(derivation_status_name(result.status), "solved and verified",
                "every recorded claim is verified");
        t.check(has_rule(derivation, "vec.cross.plan"), "the derivation records its plan");
        t.check(has_rule(derivation, "vec.cross.check-rank"), "the derivation checks rank");
        t.check(has_rule(derivation, "vec.cross.check-frame"), "the derivation checks frame");
        t.check(has_rule(derivation, "vec.cross.check-dimension"),
                "the derivation checks the product dimension");
        t.check(has_rule(derivation, "vec.cross.component-i"), "the derivation records the i component");
        t.check(has_rule(derivation, "vec.cross.component-j"), "the derivation records the j component");
        t.check(has_rule(derivation, "vec.cross.component-k"), "the derivation records the k component");
        t.check(has_rule(derivation, "vec.cross.check-orthogonal-first"),
                "the derivation checks orthogonality to the first operand");
        t.check(has_rule(derivation, "vec.cross.check-orthogonal-second"),
                "the derivation checks orthogonality to the second operand");
        t.check(has_rule(derivation, "vec.cross.check-anticommutative"),
                "the derivation checks anticommutativity");
        t.evidence("PHYS-361", result.has_value && has_rule(derivation, "vec.cross.plan") &&
                   has_rule(derivation, "vec.cross.check-orthogonal-first") &&
                   has_rule(derivation, "vec.cross.check-orthogonal-second") &&
                   has_rule(derivation, "vec.cross.check-anticommutative"),
                   "the cross product family exists, converts to SI, expands the determinant and "
                   "verifies orthogonality and anticommutativity");
    }

    {
        Derivation derivation;
        VectorCrossResult result = solve(parsed_vector("1 i + 2 j + 3 k m"),
                                         parsed_vector("4 i + 5 j + 6 k m/s"), &derivation);
        t.equal(vector_cross_outcome_name(result.outcome), "solved",
                "operands with different dimensions are allowed, unlike addition");
        t.check(result.value.x.num == -3 && result.value.y.num == 6 && result.value.z.num == -3,
                "the determinant expansion matches the textbook cross product");
    }

    {
        Derivation derivation;
        VectorCrossResult result =
            solve(parsed_vector("3 i + 4 j m"), parsed_vector("1 i + 2 j + 3 k m"), &derivation);
        t.equal(vector_cross_outcome_name(result.outcome), "rank mismatch",
                "a rank-two operand is refused rather than treated as rank three");
        t.check(result.detail.find("three components") != std::string::npos,
                "the rank refusal names why the cross product needs three components");
    }

    {
        Vector first = parsed_vector("1 i + 2 j + 3 k m");
        Vector second = parsed_vector("1 i + 2 j + 3 k m");
        second.frame.name = "ramp";
        Derivation derivation;
        VectorCrossResult result = solve(first, second, &derivation);
        t.equal(vector_cross_outcome_name(result.outcome), "frame mismatch",
                "different frames are never silently coerced");
        t.check(result.detail.find("explicit basis transformation") != std::string::npos,
                "the frame refusal names the missing operation");
    }
}

}  // namespace nps
