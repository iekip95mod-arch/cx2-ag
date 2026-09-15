#include <string>

#include "nps/physics/position_motion.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity parsed_quantity(const char *text) {
    Quantity quantity;
    std::string error;
    parse_quantity(text, &quantity, &error);
    return quantity;
}

PositionMotionProblem problem(const char *position_expression, const char *start, const char *end,
                              const char *event) {
    PositionMotionProblem p;
    p.body_name = "proton";
    p.position_expression = position_expression;
    p.interval_start = parsed_quantity(start);
    p.interval_end = parsed_quantity(end);
    p.event_time = parsed_quantity(event);
    return p;
}

struct Run {
    explicit Run(const PositionMotionProblem &problem, const Budget &budget = Budget())
        : result(solve_position_motion(arena, derivation, problem, budget)) {}

    Arena arena;
    Derivation derivation;
    PositionMotionResult result;
};

}  // namespace

// Issue 342, problem 2 of the PHYS 2410 Chapters 1-4 test: a proton along x = 50t + 10t^2. The
// average velocity over the first 3.0 s, the instantaneous velocity and the instantaneous
// acceleration at t = 3.0 s are three different operations on the same supplied function rather
// than three lookups in a constant-acceleration equation table.
void run_position_motion_tests(TestSink &t) {
    {
        Run solved(problem("50*t + 10*t^2", "0 s", "3.0 s", "3.0 s"));
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
               "a position function of time solves rather than being refused for having no "
               "constant-acceleration slot");
        t.equal(rational_text(solved.result.average_velocity.value), "80",
               "the average velocity over the declared interval is the secant, not the "
               "derivative");
        t.equal(solved.result.average_velocity.unit.text, "m/s",
               "the average velocity is reported in m/s");
        t.equal(rational_text(solved.result.instantaneous_velocity.value), "110",
               "the instantaneous velocity at the event comes from the differentiation engine");
        t.equal(solved.result.instantaneous_velocity.unit.text, "m/s",
               "the instantaneous velocity is reported in m/s");
        t.check(rational_text(solved.result.average_velocity.value) !=
                    rational_text(solved.result.instantaneous_velocity.value),
                "the secant over the interval and the derivative at the event are not "
                "substitutable, so 80 m/s and 110 m/s stay distinct");
        t.equal(rational_text(solved.result.instantaneous_acceleration.value), "20",
               "the instantaneous acceleration is the second derivative");
        t.equal(solved.result.instantaneous_acceleration.unit.text, "m/s^2",
               "the instantaneous acceleration is reported in m/s^2");
        t.check(solved.result.velocity_expression != kNoNode,
                "the velocity's own symbolic expression is exposed for the walkthrough");
        t.check(solved.result.acceleration_expression != kNoNode,
                "the acceleration's own symbolic expression is exposed for the walkthrough");
        t.check(solved.derivation.size() > 0,
                "the differentiation engine's own steps appear in the derivation rather than "
                "being reimplemented here");
    }
    {
        // A linear position function: the velocity is constant and the acceleration is zero,
        // which the second differentiation has to reach rather than assume.
        Run solved(problem("5*t + 3", "0 s", "2 s", "2 s"));
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
               "a linear position function solves");
        t.equal(rational_text(solved.result.average_velocity.value), "5",
               "a linear position function's average velocity matches its constant slope");
        t.equal(rational_text(solved.result.instantaneous_velocity.value), "5",
               "a linear position function's instantaneous velocity matches its average velocity "
               "everywhere");
        t.equal(rational_text(solved.result.instantaneous_acceleration.value), "0",
               "a linear position function has zero acceleration");
    }
    {
        // The exponent depends on the differentiation variable, which the power rule at
        // nps/src/steps/differentiate.cc:396 explicitly refuses rather than silently applying the
        // wrong rule.
        Run solved(problem("t^t", "0 s", "2 s", "2 s"));
        t.check(solved.result.outcome == PositionMotionOutcome::UnsupportedForm,
               "a position function the differentiation engine cannot handle is refused rather "
               "than answered wrong");
        t.check(!solved.result.detail.empty(),
                "the refusal names what it could not do");
    }
}

}  // namespace nps
