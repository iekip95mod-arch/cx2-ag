#ifndef NPS_PHYSICS_PLANAR_KINEMATICS_H
#define NPS_PHYSICS_PLANAR_KINEMATICS_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// PHYS-019: the axes are declared rather than assumed, so a component's sign means something.
enum class PlanarAxes : uint8_t {
    RightUp,
};

const char *planar_axes_name(PlanarAxes axes);

// PHYS-028: an event quantity, a state quantity and an interval quantity are different things and
// one cannot stand in for another. The caller declares which stage each input belongs to and the
// family refuses a substitution across stages rather than computing with it.
enum class MotionStage : uint8_t {
    Event,
    State,
    Interval,
};

const char *motion_stage_name(MotionStage stage);

struct PlanarKinematicsProblem {
    std::string body_name;
    // The state at the initial event, and the interval that separates the two events.
    Vector initial_velocity;
    Vector acceleration;
    Quantity elapsed_time;
    MotionStage initial_velocity_stage = MotionStage::State;
    MotionStage acceleration_stage = MotionStage::Interval;
    MotionStage elapsed_time_stage = MotionStage::Interval;
    PlanarAxes axes = PlanarAxes::RightUp;
    // The projectile specialization: the horizontal axis is unaccelerated and the vertical one
    // carries a single downward acceleration.
    bool projectile = false;
};

enum class PlanarKinematicsOutcome : uint8_t {
    Solved,
    InvalidProblem,
    RankMismatch,
    FrameUndeclared,
    FrameMismatch,
    DimensionMismatch,
    StageMismatch,
    NotProjectile,
    NoApex,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *planar_kinematics_outcome_name(PlanarKinematicsOutcome outcome);

struct PlanarKinematicsResult {
    PlanarKinematicsOutcome outcome = PlanarKinematicsOutcome::InvalidProblem;
    Vector displacement;
    Vector final_velocity;
    bool has_value = false;
    // Displacement belongs to the interval and the final velocity to the terminal event's state.
    MotionStage displacement_stage = MotionStage::Interval;
    MotionStage final_velocity_stage = MotionStage::State;
    std::string displacement_text;
    std::string final_velocity_text;
    std::string interpretation;
    std::string detail;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

PlanarKinematicsResult solve_planar_kinematics(Arena &arena, Derivation &derivation,
                                               const PlanarKinematicsProblem &problem,
                                               const Budget &budget = Budget(),
                                               Backend *giac = nullptr);

// The apex is an event the caller cannot supply a time for: it is defined by the vertical velocity
// reaching zero, not by an elapsed time. Reuses the one-dimensional kinematics engine for both
// routes, since the equation table already carries v = v0 + a t and v^2 = v0^2 + 2 a x.
struct PlanarApexProblem {
    std::string body_name;
    Vector initial_velocity;
    Vector acceleration;
    MotionStage initial_velocity_stage = MotionStage::State;
    MotionStage acceleration_stage = MotionStage::Interval;
    PlanarAxes axes = PlanarAxes::RightUp;
};

struct PlanarApexResult {
    PlanarKinematicsOutcome outcome = PlanarKinematicsOutcome::InvalidProblem;
    Quantity time_to_apex;
    Quantity height;
    bool has_value = false;
    std::string time_to_apex_text;
    std::string height_text;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// Route one isolates the time at which the vertical velocity is zero, then substitutes it into the
// displacement equation. Route two reaches the same height directly from v^2 = v0^2 + 2 a x with the
// apex velocity given as zero. The two share the model and the inputs but not the equation, so their
// agreement is recorded as the verification rather than a repeated computation of the same formula.
PlanarApexResult solve_planar_apex(Arena &arena, Derivation &derivation,
                                   const PlanarApexProblem &problem,
                                   const Budget &budget = Budget(), Backend *giac = nullptr);

}  // namespace nps

#endif
