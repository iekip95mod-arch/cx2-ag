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

PositionMotionProblem problem(const char *position_x, const char *position_y, const char *start,
                              const char *end, const char *event, uint8_t rank = 2,
                              const char *position_z = "") {
    PositionMotionProblem p;
    p.body_name = "proton";
    p.position_x = position_x;
    p.position_y = position_y;
    p.position_z = position_z;
    p.rank = rank;
    p.interval_start = parsed_quantity(start);
    p.interval_end = parsed_quantity(end);
    p.event_time = parsed_quantity(event);
    return p;
}

bool cancel_now(void *) { return true; }

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
//
// Issue 379: r(t) = x(t) i + y(t) j (+ z(t) k at rank three), so every case below carries a second
// component alongside the original x(t), and one case exercises a genuine rank-three vector where
// all three axes move differently.
void run_position_motion_tests(TestSink &t) {
    {
        Run solved(problem("50*t + 10*t^2", "0", "0 s", "3.0 s", "3.0 s"));
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
               "a two-dimensional position vector solves rather than being refused for having no "
               "constant-acceleration slot");
        t.equal(rational_text(solved.result.average_velocity.x), "80",
               "the x average velocity over the declared interval is the secant, not the "
               "derivative");
        t.equal(rational_text(solved.result.average_velocity.y), "0",
               "a y(t) that never moves has zero average velocity");
        t.equal(solved.result.average_velocity.unit.text, "m/s",
               "the average velocity vector is reported in m/s");
        t.check(solved.result.average_velocity.rank == 2,
               "the reported vector carries the rank the problem declared");
        t.equal(rational_text(solved.result.instantaneous_velocity.x), "110",
               "the instantaneous x velocity at the event comes from the differentiation engine");
        t.check(rational_text(solved.result.average_velocity.x) !=
                    rational_text(solved.result.instantaneous_velocity.x),
                "the secant over the interval and the derivative at the event are not "
                "substitutable, so 80 m/s and 110 m/s stay distinct");
        t.equal(rational_text(solved.result.instantaneous_acceleration.x), "20",
               "the instantaneous x acceleration is the second derivative");
        t.equal(solved.result.instantaneous_acceleration.unit.text, "m/s^2",
               "the instantaneous acceleration vector is reported in m/s^2");
        t.check(solved.result.velocity_x != kNoNode && solved.result.velocity_y != kNoNode,
                "each active axis's own symbolic velocity expression is exposed for the "
                "walkthrough");
        t.check(solved.result.acceleration_x != kNoNode && solved.result.acceleration_y != kNoNode,
                "each active axis's own symbolic acceleration expression is exposed for the "
                "walkthrough");
        t.check(solved.derivation.size() > 0,
                "the differentiation engine's own steps appear in the derivation rather than "
                "being reimplemented here");
    }
    {
        // A genuine rank-three vector: three different functions of t on three different axes, so
        // the widened engine is exercised on all three components rather than only x and a silent
        // y.
        Run solved(problem("2*t", "3*t^2", "0 s", "2 s", "2 s", 3, "t^3"));
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
               "a rank-three position vector solves");
        t.equal(rational_text(solved.result.instantaneous_velocity.x), "2",
               "dx/dt of 2t is the constant 2");
        t.equal(rational_text(solved.result.instantaneous_velocity.y), "12",
               "dy/dt of 3t^2 evaluated at t=2 is 12");
        t.equal(rational_text(solved.result.instantaneous_velocity.z), "12",
               "dz/dt of t^3 evaluated at t=2 is 12");
        t.equal(rational_text(solved.result.instantaneous_acceleration.z), "12",
               "d2z/dt2 of t^3 evaluated at t=2 is 12");
        t.check(solved.result.instantaneous_velocity.rank == 3,
               "a rank-three problem reports a rank-three vector");
        t.check(solved.result.velocity_z != kNoNode && solved.result.acceleration_z != kNoNode,
                "the z axis's own symbolic expressions are exposed alongside x and y");
    }
    {
        // A rank the family does not support, neither two nor three, is refused rather than
        // guessed at.
        Run refused(problem("50*t", "0", "0 s", "3.0 s", "3.0 s", 1));
        t.equal(position_motion_outcome_name(refused.result.outcome), "invalid input",
               "an unsupported rank is refused as invalid input");
        t.check(refused.result.detail.find("rank") != std::string::npos,
                "the refusal names the unsupported rank");
    }
    {
        // The exponent depends on the differentiation variable, which the power rule at
        // nps/src/steps/differentiate.cc:396 explicitly refuses rather than silently applying the
        // wrong rule. Only the y axis is unsupported, so the refusal has to be reached through the
        // per-axis loop rather than only ever seeing the first axis fail.
        Run solved(problem("2*t", "t^t", "0 s", "2 s", "2 s"));
        t.check(solved.result.outcome == PositionMotionOutcome::UnsupportedForm,
               "a position vector whose y component the differentiation engine cannot handle is "
               "refused rather than answered wrong");
        t.check(solved.result.detail.find("y ") != std::string::npos,
                "the refusal names which axis could not be differentiated");
    }
    {
        // An unparseable position expression is refused before any differentiation is attempted.
        Run refused(problem("50*t + (", "0", "0 s", "3.0 s", "3.0 s"));
        t.check(refused.result.outcome == PositionMotionOutcome::InvalidInput,
               "a position function that fails to parse is refused as invalid input");
        t.check(refused.result.detail.find("x") != std::string::npos,
                "the refusal names which axis could not be parsed");
    }
    {
        // Start and end at the same instant leave no interval for the secant to average over.
        Run refused(problem("50*t + 10*t^2", "0", "3.0 s", "3.0 s", "3.0 s"));
        t.equal(position_motion_outcome_name(refused.result.outcome), "invalid input",
               "a zero-duration interval is refused rather than dividing by zero");
        t.check(refused.result.detail.find("duration") != std::string::npos,
                "the refusal names the zero-duration interval");
    }
    {
        // A non-time quantity in the interval start cannot stand in for a time bound.
        Run refused(problem("50*t + 10*t^2", "0", "3.0 m", "5.0 s", "3.0 s"));
        t.equal(position_motion_outcome_name(refused.result.outcome), "dimension mismatch",
               "a length supplied where the interval start expects a time is refused");
    }
    {
        // A non-time quantity in the event time cannot stand in for a time bound either.
        Run refused(problem("50*t + 10*t^2", "0", "0 s", "3.0 s", "3.0 kg"));
        t.equal(position_motion_outcome_name(refused.result.outcome), "dimension mismatch",
               "a mass supplied where the event time expects a time is refused");
    }
    {
        // A meter that is already cancelled on entry halts the first differentiate() call, so the
        // Cancelled branch of the axis outcome mapping (position_motion.cc) is reached rather than
        // only ever seeing Differentiated or UnsupportedForm out of that call.
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem("50*t + 10*t^2", "0", "0 s", "3.0 s", "3.0 s"), budget);
        t.equal(position_motion_outcome_name(cancelled.result.outcome), "cancelled",
               "a cancelled meter reports cancellation instead of an answer");
        t.check(cancelled.result.detail.find("velocity") != std::string::npos,
                "the cancellation is reported from the velocity differentiation phase");
    }
    {
        // A step budget too small for the differentiation engine to finish exhausts on the first
        // differentiate() call, reaching the ResourceExceeded branch of the same mapping.
        Budget budget;
        budget.max_steps = 1;
        Run exhausted(problem("50*t + 10*t^2", "0", "0 s", "3.0 s", "3.0 s"), budget);
        t.equal(position_motion_outcome_name(exhausted.result.outcome), "resource exceeded",
               "a step budget too small to differentiate reports resource exhaustion instead of "
               "an answer");
        t.check(exhausted.result.detail.find("velocity") != std::string::npos,
                "the resource exhaustion is reported from the velocity differentiation phase");
    }
}

}  // namespace nps
