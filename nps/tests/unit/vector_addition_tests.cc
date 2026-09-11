#include <cstdint>
#include <limits>
#include <string>

#include "nps/physics/vector_addition.h"
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

VectorAdditionResult solve(const Vector &first, const Vector &second, Derivation *derivation,
                           const Budget &budget = Budget()) {
    Arena arena;
    VectorAdditionProblem problem;
    problem.first = first;
    problem.second = second;
    return solve_vector_addition(arena, *derivation, problem, budget);
}

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return true;
    }
    return false;
}

bool cancel_now(void *) { return true; }

}  // namespace

void run_vector_addition_tests(TestSink &t) {
    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("3 i + 4 j m/s"), parsed_vector("1 i - 2 j m/s"), &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "solved", "two compatible vectors add");
        t.equal(result.value_text, "(4 i + 2 j) m/s", "the exact Cartesian sum is reported");
        t.check(result.has_value, "a solved vector addition carries its structured value");
        t.equal(result.value.frame.name, default_frame_name(), "the result retains the stated frame");
        t.equal(derivation.context.problem_family_id,
                "physics.vectors.cartesian-addition.two-dimension",
                "the derivation identifies the vector addition family");
        t.equal(derivation_status_name(result.status), "solved and verified",
                "every recorded claim is verified");
        t.check(has_rule(derivation, "vec.add.plan"), "the derivation records its plan");
        t.check(has_rule(derivation, "vec.add.check-rank"), "the derivation checks rank");
        t.check(has_rule(derivation, "vec.add.check-frame"), "the derivation checks frame");
        t.check(has_rule(derivation, "vec.add.check-dimension"), "the derivation checks dimension");
        t.check(has_rule(derivation, "vec.add.component-i"), "the derivation records i addition");
        t.check(has_rule(derivation, "vec.add.component-j"), "the derivation records j addition");
        t.evidence("PHYS-025", has_rule(derivation, "vec.add.plan") &&
                   has_rule(derivation, "vec.add.check-frame") && has_rule(derivation, "vec.add.check-rank") &&
                   has_rule(derivation, "vec.add.check-dimension") && has_rule(derivation, "vec.add.component-i") &&
                   has_rule(derivation, "vec.add.component-j") && result.value.frame.name == default_frame_name(),
                   "vector addition exposes its coordinate frame, model preconditions, component laws and checks");
    }

    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("(1, 2) km"), parsed_vector("(500, 500) m"), &derivation);
        t.equal(result.value_text, "(1500 i + 2500 j) m", "mixed compatible units convert exactly to SI");
        t.check(has_rule(derivation, "vec.add.convert-si"), "an actual unit conversion is recorded");
        t.check(result.value.x.num == 1500 && result.value.x.den == 1,
                "the structured result retains the exact SI component");
    }
    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("(1.0, 2.0) km/s"), parsed_vector("(-300, -400) m/s"),
                  &derivation);
        t.equal(result.value_text, "(700 i + 1600 j) m/s",
                "measured mixed-unit addition reports at the converted hundreds place");
        t.check(result.has_value && result.value.precision.kind == NumberKind::Measured &&
                    result.value.precision.significant_digits == 1 &&
                    result.value.precision.last_significant_decimal_place == 2,
                "mixed-unit addition retains conservatively shared SI precision metadata");
    }

    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("(1.20 i + 2.0 j) m"), parsed_vector("(2.3 i + 3.00 j) m"),
                  &derivation);
        t.equal(result.value_text, "(3.5 i + 5.0 j) m", "precision is applied to the final vector once");
        t.check(result.value.x.num == 7 && result.value.x.den == 2,
                "the structured component remains exact before reporting");
        t.check(has_rule(derivation, "vec.add.report-precision"), "the final-only precision step is recorded");
    }

    {
        // A component whose rounding carries. 5.0 + 4.96 is 9.96 at the tenths place, so the report
        // is 10.0, and it used to read 10 beside a j component that kept its tenths.
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("(5.0, 1.0) m"), parsed_vector("(4.96, 1.0) m"), &derivation);
        t.equal(result.value_text, "(10.0 i + 2.0 j) m",
                "a component that rounds up into a new digit keeps the tenths place its sum has");
        t.check(result.has_value && result.value.precision.kind == NumberKind::Measured &&
                    result.value.precision.last_significant_decimal_place == -1,
                "and the shared precision keeps the tenths place");
    }

    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("1 i + 2 j + 3 k m"), parsed_vector("1 i + 2 j + 3 k m"),
                  &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "rank mismatch",
                "the 2D archetype refuses 3D vectors explicitly");
        t.check(!result.has_value, "a rank refusal carries no answer");
    }

    {
        Vector first = parsed_vector("1 i + 2 j m");
        Vector second = parsed_vector("3 i + 4 j m");
        second.frame.name = "ramp";
        Derivation derivation;
        VectorAdditionResult result = solve(first, second, &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "frame mismatch",
                "different frames are never silently coerced");
        t.check(result.detail.find("explicit basis transformation") != std::string::npos,
                "the frame refusal names the missing operation");
    }

    {
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("1 i + 2 j m"), parsed_vector("3 i + 4 j s"), &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "dimension mismatch",
                "different physical dimensions are refused explicitly");
    }

    {
        Vector first = parsed_vector("1 i + 0 j km");
        Vector second = parsed_vector("0 i + 0 j km");
        first.x.num = std::numeric_limits<int64_t>::max();
        Derivation derivation;
        VectorAdditionResult result = solve(first, second, &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "arithmetic overflow",
                "SI conversion overflow is distinct from an invalid vector");
        t.check(!result.has_value, "overflow never exposes a partial answer");
    }

    {
        Vector first = parsed_vector("1.0 i + 0 j m");
        Vector second = parsed_vector("0 i + 0 j m");
        first.x.num = std::numeric_limits<int64_t>::max();
        first.precision.kind = NumberKind::Measured;
        first.precision.significant_digits = 18;
        // A place below the deepest one the printer reaches. The largest int64 at the tenths place
        // used to be refused too, and now prints exactly, so it no longer stands in for this.
        first.precision.last_significant_decimal_place = -19;
        Derivation derivation;
        VectorAdditionResult result = solve(first, second, &derivation);
        t.equal(vector_addition_outcome_name(result.outcome), "arithmetic overflow",
                "unverifiable measured reporting is refused rather than relabeled exact");
        t.check(!result.has_value &&
                    result.detail ==
                        "reporting the measured precision exceeds exact integer arithmetic",
                "a reporting overflow exposes neither a value nor a false precision claim");
    }

    {
        const Vector first = parsed_vector("1 i + 2 j m");
        const Vector second = parsed_vector("3 i + 4 j m");
        bool every_failure_refused = true;
        bool saw_late_failure = false;
        bool saw_success = false;
        for (size_t max_nodes = 0; max_nodes <= 96; ++max_nodes) {
            Limits limits;
            limits.max_nodes = max_nodes;
            Arena arena(limits);
            Derivation derivation;
            VectorAdditionProblem problem;
            problem.first = first;
            problem.second = second;
            const VectorAdditionResult result =
                solve_vector_addition(arena, derivation, problem);
            if (arena.failed()) {
                every_failure_refused =
                    every_failure_refused &&
                    result.outcome == VectorAdditionOutcome::ResourceExceeded && !result.has_value &&
                    derivation.size() == 0;
                saw_late_failure = saw_late_failure || result.cost.steps > 0;
            } else if (result.outcome == VectorAdditionOutcome::Solved) {
                saw_success = true;
            }
        }
        t.check(every_failure_refused && saw_late_failure && saw_success,
                "every arena exhaustion point refuses and rewinds instead of returning solved");
    }

    {
        Budget budget;
        budget.poll = cancel_now;
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("1 i + 2 j m"), parsed_vector("3 i + 4 j m"), &derivation, budget);
        t.equal(vector_addition_outcome_name(result.outcome), "cancelled",
                "cancellation is reported explicitly");
        t.check(derivation.size() == 0, "cancellation leaves no partial derivation");
    }

    {
        Budget budget;
        budget.max_steps = 1;
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("1 i + 2 j m"), parsed_vector("3 i + 4 j m"), &derivation, budget);
        t.equal(vector_addition_outcome_name(result.outcome), "resource exceeded",
                "a step budget halt is reported explicitly");
        t.check(derivation.size() == 0, "a resource halt rewinds the partial derivation");
    }

    {
        Budget budget;
        budget.max_rewrites = 0;
        Derivation derivation;
        VectorAdditionResult result =
            solve(parsed_vector("1 i + 2 j m"), parsed_vector("3 i + 4 j m"), &derivation, budget);
        t.equal(vector_addition_outcome_name(result.outcome), "resource exceeded",
                "a rewrite budget halt is reported explicitly");
        // STEP-025: what survives a halt is what was checked, and no component work that was not.
        t.check(derivation.all_verified_from(0),
                "a rewrite halt exposes no unchecked component work");
        t.check(!result.has_value, "and no resultant");
    }
}

}  // namespace nps
