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

}  // namespace nps

#endif
