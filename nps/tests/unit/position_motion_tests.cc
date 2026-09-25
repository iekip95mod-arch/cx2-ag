#include <string>
#include <utility>
#include <vector>

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

// The scripted backend relative_motion_tests.cc drives its own polar cross-check through. Copied
// rather than shared because that file keeps it in its anonymous namespace, as
// planar_kinematics_tests.cc already does for the same reason.
class SequenceBackend : public Backend {
  public:
    explicit SequenceBackend(std::vector<std::string> replies) : replies_(std::move(replies)) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "no scripted reply";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    size_t next_ = 0;
};

struct Run {
    explicit Run(const PositionMotionProblem &problem, const Budget &budget = Budget(),
                 Backend *backend = nullptr)
        : result(solve_position_motion(arena, derivation, problem, budget, backend)) {}

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
        t.check(!solved.result.has_average_velocity_polar &&
                    !solved.result.has_instantaneous_velocity_polar &&
                    !solved.result.has_instantaneous_acceleration_polar,
                "with no backend there is nothing to compute a magnitude or an angle with, so "
                "none is reported");
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
    {
        // Issue 400: r(t) = 3t i + 2t^2 j, so the three answers are (3, 4), (3, 8) and (0, 4) m/s
        // and m/s^2. The first four replies are the differentiation engine's own Giac check, and
        // the ten after them are the three magnitude and direction conversions in order.
        SequenceBackend backend({"3", "0", "4*t", "4", "0", "atan2(4,3)", "0", "sqrt(73)", "0",
                                 "atan2(8,3)", "0", "0", "atan2(4,0)", "0"});
        Run solved(problem("3*t", "2*t^2", "0 s", "2 s", "2 s"), Budget(), &backend);
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
                "a supplied backend leaves the component answers solved");
        t.check(solved.result.has_average_velocity_polar &&
                    solved.result.has_instantaneous_velocity_polar &&
                    solved.result.has_instantaneous_acceleration_polar,
                "all three answers carry a magnitude and a direction when a backend is supplied");
        t.equal(print(solved.arena, solved.result.average_velocity_polar.magnitude), "5",
                "the average velocity magnitude is the exact 5 m/s the components imply");
        const std::string average_angle =
            print(solved.arena, solved.result.average_velocity_polar.angle);
        t.check(average_angle.find("atan2") != std::string::npos &&
                    average_angle.find("4") != std::string::npos &&
                    average_angle.find("3") != std::string::npos,
                "the average velocity direction is the quadrant-aware atan2 of its own "
                "components rather than a second angle computed here");
        t.check(solved.result.average_velocity_polar.angle_unit == AngleUnit::Radians,
                "the reported angle names the measure the problem declared");
        t.equal(solved.result.average_velocity_polar.unit.text, "m/s",
                "the magnitude keeps the vector's own unit");
        t.check(solved.result.average_velocity_polar.rank == 2 &&
                    solved.result.average_velocity_polar.polar_angle == kNoNode,
                "a rank-two direction is one angle, with no polar angle to report");
        t.equal(print(solved.arena, solved.result.instantaneous_velocity_polar.magnitude),
                "sqrt(73)",
                "the instantaneous velocity magnitude stays exact rather than being approximated");
        t.equal(print(solved.arena, solved.result.instantaneous_acceleration_polar.magnitude), "4",
                "the instantaneous acceleration carries its own magnitude, not the velocity's");
        t.check(backend.commands.size() == 14,
                "the three conversions reach the backend through the existing converter rather "
                "than a second path");
    }
    {
        // r(t) = 3t i + 4t j accelerates nowhere, and a zero vector has no direction. The one
        // answer that cannot have an angle withholds its own, and the two that can still report.
        SequenceBackend backend(
            {"3", "0", "4", "0", "0", "atan2(4,3)", "0", "0", "atan2(4,3)", "0", "0"});
        Run solved(problem("3*t", "4*t", "0 s", "2 s", "2 s"), Budget(), &backend);
        t.check(solved.result.outcome == PositionMotionOutcome::Solved,
                "a zero acceleration does not sink the answers that did convert");
        t.check(solved.result.has_average_velocity_polar &&
                    solved.result.has_instantaneous_velocity_polar,
                "the two moving answers still report a magnitude and a direction");
        t.check(!solved.result.has_instantaneous_acceleration_polar,
                "the zero acceleration reports no direction rather than inventing one");
        t.equal(rational_text(solved.result.instantaneous_acceleration.x), "0",
                "the acceleration components are still exposed with the direction withheld");
    }
    {
        // The same problem in degrees, which costs each conversion the two extra calls that turn
        // the radian atan2 into the declared measure.
        PositionMotionProblem degrees = problem("3*t", "2*t^2", "0 s", "2 s", "2 s");
        degrees.angle_unit = AngleUnit::Degrees;
        SequenceBackend backend({"3",
                                 "0",
                                 "4*t",
                                 "4",
                                 "0",
                                 "atan2(4,3)",
                                 "0",
                                 "atan2(4,3)*180/pi",
                                 "0",
                                 "sqrt(73)",
                                 "0",
                                 "atan2(8,3)",
                                 "0",
                                 "atan2(8,3)*180/pi",
                                 "0",
                                 "0",
                                 "atan2(4,0)",
                                 "0",
                                 "atan2(4,0)*180/pi",
                                 "0"});
        Run solved(degrees, Budget(), &backend);
        t.check(solved.result.has_average_velocity_polar &&
                    solved.result.average_velocity_polar.angle_unit == AngleUnit::Degrees,
                "a problem that declares degrees gets its direction in degrees");
        const std::string angle = print(solved.arena, solved.result.average_velocity_polar.angle);
        t.check(angle.find("180") != std::string::npos,
                "the degree angle is the converted one rather than the radian value relabelled");
    }
    {
        // A rank-three answer needs two angles before it names a direction, and the polar angle is
        // the one a rank-two conversion has no slot for.
        SequenceBackend backend({"2",         "0",          "6*t",        "6",
                                 "3*t^2",     "6*t",        "sqrt(56)",   "0",
                                 "atan2(6,2)", "0",         "atan2(sqrt(40),4)", "0",
                                 "sqrt(292)", "0",          "atan2(12,2)", "0",
                                 "atan2(sqrt(148),12)", "0", "sqrt(180)", "0",
                                 "atan2(6,0)", "0",         "atan2(6,12)", "0"});
        Run solved(problem("2*t", "3*t^2", "0 s", "2 s", "2 s", 3, "t^3"), Budget(), &backend);
        t.check(solved.result.has_instantaneous_velocity_polar &&
                    solved.result.instantaneous_velocity_polar.rank == 3,
                "a rank-three answer reports a rank-three direction");
        t.check(solved.result.instantaneous_velocity_polar.polar_angle != kNoNode,
                "the spherical polar angle is exposed alongside the azimuth");
    }
    {
        // A backend budget that runs out inside the first conversion is terminal for the solve:
        // the contract forbids another Giac call after exhaustion, so the second and third
        // conversions are never attempted and the command count stops where the budget did.
        Budget budget;
        budget.max_backend_calls = 5;
        SequenceBackend backend({"3", "0", "4*t", "4", "0", "atan2(4,3)", "0"});
        Run exhausted(problem("3*t", "2*t^2", "0 s", "2 s", "2 s"), budget, &backend);
        t.equal(position_motion_outcome_name(exhausted.result.outcome), "resource exceeded",
                "a conversion that exhausts the backend budget reports exhaustion rather than a "
                "partial answer");
        t.check(backend.commands.size() == 5,
                "no further backend call is made after the terminal status");
        t.check(exhausted.result.detail.find("average velocity direction") != std::string::npos,
                "the refusal names the conversion that ran out");
        t.check(!exhausted.result.has_average_velocity_polar &&
                    exhausted.result.average_velocity.unit.text.empty(),
                "an exhausted solve exposes no answer at all");
    }
    {
        // Issue 416. The family composes two engines that each write a context of their own, so
        // before this the field carried whichever of them ran last: the differentiation engine with
        // no backend, and the component converter with one. Both are asserted, because a fix that
        // only covers the backend route leaves the commoner one still borrowing.
        Run without(problem("3*t", "2*t^2", "0 s", "2 s", "2 s"));
        t.equal(without.derivation.context.problem_family_id, "physics.motion.position-vector",
                "with no backend the context names this family rather than the nested derivative "
                "engine it borrowed from");
        t.equal(without.derivation.context.problem_family_envelope_version, "1",
                "and it records the envelope version the catalog declares");
        t.check(without.derivation.context.derivation_status == without.result.status,
                "the context binds to the outcome this family reached");
        SequenceBackend backend({"3", "0", "4*t", "4", "0", "atan2(4,3)", "0", "sqrt(73)", "0",
                                 "atan2(8,3)", "0", "0", "atan2(4,0)", "0"});
        Run with(problem("3*t", "2*t^2", "0 s", "2 s", "2 s"), Budget(), &backend);
        t.equal(with.derivation.context.problem_family_id, "physics.motion.position-vector",
                "and with a backend it still names this family rather than the component converter "
                "that wrote the context last");
        t.equal(with.derivation.context.problem_family_envelope_version, "1",
                "with the same envelope version on both routes");
        t.check(!with.derivation.context.requested_method.empty(),
                "the context states the method this family ran rather than a nested engine's");
        std::string assumptions;
        for (const std::string &one : without.derivation.context.active_assumptions)
            assumptions += one + " | ";
        t.evidence("PHYS-025",
                   assumptions.find("function of t alone") != std::string::npos &&
                       assumptions.find("share one clock") != std::string::npos &&
                       without.derivation.context.problem_family_id ==
                           "physics.motion.position-vector" &&
                       !without.derivation.context.requested_method.empty() &&
                       !without.derivation.context.unit_policy.empty() &&
                       without.derivation.size() > 0,
                   "the position-vector family records its one-variable component condition, the "
                   "shared clock, the method it ran and the unit policy it reported under");
    }
}

}  // namespace nps
