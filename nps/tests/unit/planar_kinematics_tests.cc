#include <string>

#include "nps/physics/planar_kinematics.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Vector parsed_vector(const char *text, const char *frame = "lab") {
    Vector vector;
    std::string error;
    if (!parse_vector(text, &vector, &error)) {
        vector.rank = 0;
        vector.frame.name = "parse failed: " + error;
        return vector;
    }
    vector.frame.name = frame;
    return vector;
}

Quantity parsed_quantity(const char *text) {
    Quantity quantity;
    std::string error;
    parse_quantity(text, &quantity, &error);
    return quantity;
}

PlanarKinematicsProblem problem(const Vector &velocity, const Vector &acceleration,
                                const char *time = "2 s") {
    PlanarKinematicsProblem input;
    input.body_name = "ball";
    input.initial_velocity = velocity;
    input.acceleration = acceleration;
    input.elapsed_time = parsed_quantity(time);
    return input;
}

struct Run {
    explicit Run(const PlanarKinematicsProblem &problem, const Budget &budget = Budget(),
                 Backend *backend = nullptr)
        : result(solve_planar_kinematics(arena, derivation, problem, budget, backend)) {}

    Arena arena;
    Derivation derivation;
    PlanarKinematicsResult result;
};

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return true;
    }
    return false;
}

bool cancel_now(void *) { return true; }

}  // namespace

void run_planar_kinematics_tests(TestSink &t) {
    {
        Run solved(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.evidence("PHYS-019", planar_kinematics_outcome_name(solved.result.outcome), "solved",
                   "declared axes and a shared time produce a planar displacement");
        t.equal(solved.result.displacement_text, "(6 i - 12 j) m",
                "each axis integrates v0 t + a t^2 / 2 under the same elapsed time");
        t.equal(solved.result.final_velocity_text, "(3 i - 16 j) m/s",
                "the final velocity is the initial velocity plus a t");
        t.check(solved.result.displacement_stage == MotionStage::Interval &&
                    solved.result.final_velocity_stage == MotionStage::State,
                "the displacement spans the interval while the final velocity is a state");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.kinematics.constant-acceleration.two-dimension",
                "the context identifies the planar constant-acceleration family");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.component-i") &&
                    has_rule(solved.derivation, "physics.planar-kinematics.component-j"),
                "both axes are decomposed in their own recorded step");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.check-shared-time"),
                "the shared-time identity is the family's final check");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(20, 0) m/s"), parsed_vector("(0, -10) m/s^2"), "3 s");
        input.projectile = true;
        Run solved(input);
        t.evidence("PHYS-019", planar_kinematics_outcome_name(solved.result.outcome), "solved",
                   "the projectile specialization accepts free fall in the declared axes");
        t.equal(solved.result.displacement_text, "(60 i - 45 j) m",
                "a horizontal launch falls g t^2 / 2 while drifting at its launch speed");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.kinematics.constant-acceleration.projectile.two-dimension",
                "the projectile specialization reports its own family");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.check-projectile"),
                "the projectile precondition is checked before the reading is offered");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(20, 0) m/s"), parsed_vector("(2, -10) m/s^2"));
        input.projectile = true;
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "not a projectile",
                "a horizontal acceleration is refused as a projectile rather than solved");
        t.check(!refused.result.has_value,
                "the projectile refusal offers no displacement");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.elapsed_time_stage = MotionStage::Event;
        Run refused(input);
        t.evidence("PHYS-028", planar_kinematics_outcome_name(refused.result.outcome),
                   "stage mismatch",
                   "an event quantity cannot be substituted where the interval is required");
        t.check(refused.result.detail.find("interval") != std::string::npos,
                "the stage refusal names the stages it received");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.initial_velocity_stage = MotionStage::Interval;
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "stage mismatch",
                "an interval quantity cannot stand in for the state at the initial event");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s", "lab"),
                            parsed_vector("(0, -10) m/s^2", "deck")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "frame mismatch",
                "components in different frames are refused rather than combined");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "dimension mismatch",
                "a velocity supplied as the acceleration is refused");
    }
    {
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2")),
                      budget);
        t.equal(planar_kinematics_outcome_name(cancelled.result.outcome), "cancelled",
                "a cancelled planar solve reports cancellation instead of an answer");
        t.check(!cancelled.result.has_value, "a cancelled solve offers no displacement");
    }
    {
        // km/h and minutes make the exact SI conversion visible in the answer.
        Run converted(problem(parsed_vector("(36, 0) km/h"), parsed_vector("(0, -10) m/s^2"),
                              "1 min"));
        t.equal(planar_kinematics_outcome_name(converted.result.outcome), "solved",
                "mixed units convert exactly before either axis is integrated");
        t.equal(converted.result.displacement_text, "(600 i - 18000 j) m",
                "36 km/h is 10 m/s and one minute is sixty seconds");
    }
}

}  // namespace nps
