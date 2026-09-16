#include <cstdint>
#include <string>

#include "nps/physics/circular_motion.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity quantity(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

CircularMotionKnown known(CircularMotionVariable variable, const Quantity &value) {
    CircularMotionKnown result;
    result.variable = variable;
    result.quantity = value;
    return result;
}

CircularMotionProblem problem(CircularMotionVariable unknown, const CircularMotionKnown &first,
                              const CircularMotionKnown &second) {
    CircularMotionProblem result;
    result.unknown = unknown;
    result.knowns[0] = first;
    result.knowns[1] = second;
    result.known_count = 2;
    return result;
}

bool exact_value(const RelationResult &result, int64_t numerator, int64_t denominator) {
    return result.quantity.value.num == numerator && result.quantity.value.den == denominator;
}

bool contains(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

std::string assumptions_of(const Derivation &derivation) {
    std::string text;
    for (const std::string &assumption : derivation.context.active_assumptions)
        text += assumption + " | ";
    return text;
}

}  // namespace

void run_circular_motion_tests(TestSink &t) {
    {
        // T = 2*pi*r/v at r = 1 m, v = 1 m/s gives exactly the engine's rational 2*pi, and the
        // acceleration a = v^2/r comes out to exactly one metre per second squared.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Period, known(CircularMotionVariable::Speed, quantity("1 m/s")),
                   known(CircularMotionVariable::Radius, quantity("1 m")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "solved",
                "circular motion solves the period from speed and radius");
        t.check(exact_value(solved.unknown_result, 628318530717959, 100000000000000),
                "the period is exactly 2*pi radius over speed");
        t.equal(solved.unknown_result.unit_text, "s", "the period is reported in seconds");
        t.check(exact_value(solved.acceleration_result, 1, 1),
                "the centripetal acceleration is exactly v^2/r at these values");
        t.equal(solved.acceleration_result.unit_text, "m/s^2",
                "the acceleration is reported in SI base units");
        t.evidence("PHYS-011",
                   contains(assumptions_of(derivation),
                            "directed toward the centre of the circle"),
                   "the derivation records that the acceleration points at the centre");
    }
    {
        // r = 1 m and T set to the same exact 2*pi rational the engine uses makes v = 2*pi*r/T
        // cancel to exactly one metre per second.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Speed, known(CircularMotionVariable::Radius, quantity("1 m")),
                   known(CircularMotionVariable::Period, quantity("6.28318530717959 s")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "solved",
                "circular motion solves the speed from radius and period");
        t.check(exact_value(solved.unknown_result, 1, 1),
                "the speed cancels exactly to one metre per second at these values");
        t.check(exact_value(solved.acceleration_result, 1, 1),
                "the centripetal acceleration is exactly v^2/r at these values");
    }
    {
        // The same cancellation solves for the radius given speed and period instead.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Radius, known(CircularMotionVariable::Speed, quantity("1 m/s")),
                   known(CircularMotionVariable::Period, quantity("6.28318530717959 s")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "solved",
                "circular motion solves the radius from speed and period");
        t.check(exact_value(solved.unknown_result, 1, 1),
                "the radius cancels exactly to one metre at these values");
        t.check(exact_value(solved.acceleration_result, 1, 1),
                "the centripetal acceleration is exactly v^2/r at these values");
    }
    {
        // A zero or negative radius is refused rather than answered with a wrong number.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Period, known(CircularMotionVariable::Speed, quantity("1 m/s")),
                   known(CircularMotionVariable::Radius, quantity("0 m")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "invalid problem",
                "a zero radius is refused rather than solved");
        t.check(contains(solved.detail, "radius must be positive"),
                "the refusal names the radius as the problem");
    }
    {
        // A negative radius is refused the same way a zero one is.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Period, known(CircularMotionVariable::Speed, quantity("1 m/s")),
                   known(CircularMotionVariable::Radius, quantity("-1 m")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "invalid problem",
                "a negative radius is refused rather than solved");
    }
    {
        // A zero speed cannot complete a lap, so the period is refused rather than dividing by
        // zero.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Period, known(CircularMotionVariable::Speed, quantity("0 m/s")),
                   known(CircularMotionVariable::Radius, quantity("1 m")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "invalid problem",
                "a zero speed with a period requested is refused rather than solved");
        t.check(contains(solved.detail, "nonzero"),
                "the refusal explains that the speed must be nonzero");
    }
    {
        // A quantity whose dimension is not the one the slot wants is a dimension mismatch, not
        // a wrong number.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Period, known(CircularMotionVariable::Speed, quantity("1 m/s")),
                   known(CircularMotionVariable::Radius, quantity("1 kg")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "dimension mismatch",
                "a radius given in kilograms is refused as a dimension mismatch");
    }
    {
        // The same variable given twice is a duplicate known, not two independent facts.
        Arena arena;
        Derivation derivation;
        CircularMotionProblem input;
        input.unknown = CircularMotionVariable::Period;
        input.knowns[0] = known(CircularMotionVariable::Radius, quantity("1 m"));
        input.knowns[1] = known(CircularMotionVariable::Radius, quantity("2 m"));
        input.known_count = 2;
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "duplicate known",
                "giving the radius twice is refused as a duplicate known");
    }
    {
        // Only one known is not enough to determine a unique answer.
        Arena arena;
        Derivation derivation;
        CircularMotionProblem input;
        input.unknown = CircularMotionVariable::Period;
        input.knowns[0] = known(CircularMotionVariable::Radius, quantity("1 m"));
        input.known_count = 1;
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "missing known",
                "one known quantity is refused rather than guessed at");
    }
    {
        // The unknown cannot also be given as a known.
        Arena arena;
        Derivation derivation;
        const CircularMotionProblem input =
            problem(CircularMotionVariable::Radius, known(CircularMotionVariable::Radius, quantity("1 m")),
                   known(CircularMotionVariable::Speed, quantity("1 m/s")));
        const CircularMotionResult solved = solve_circular_motion(arena, derivation, input);
        t.equal(relation_outcome_name(solved.outcome), "invalid problem",
                "the requested unknown cannot also be given as a known");
    }
}

}  // namespace nps
