#ifndef NPS_PHYSICS_POSITION_MOTION_H
#define NPS_PHYSICS_POSITION_MOTION_H

#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/physics/planar_kinematics.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// Issue 342: kinematics.h's KinematicsProblem has one slot per constant-acceleration symbol and
// nothing to differentiate. Here the motion is given as a position function of time, x(t), in SI
// (meters, seconds), and the three results are reached by three different operations rather than
// three lookups in one equation table: a secant over the declared interval, and two calls into the
// existing differentiation engine at nps/include/nps/steps/differentiate.h. Reuses MotionStage from
// planar_kinematics.h, PHYS-028's Event/State/Interval distinction, rather than inventing a second
// one: average velocity belongs to the interval and the two instantaneous results belong to the
// event.
struct PositionMotionProblem {
    std::string body_name;
    // "50*t + 10*t^2". Read as SI: x in meters, t in seconds.
    std::string position_expression;
    // The two endpoints of the interval part (a) averages over.
    Quantity interval_start;
    Quantity interval_end;
    // The instant parts (b) and (c) evaluate at. Independent of the interval bounds because a
    // problem can ask for an instantaneous reading anywhere, not only at the interval's far end.
    Quantity event_time;
};

enum class PositionMotionOutcome : uint8_t {
    Solved,
    InvalidInput,
    UnsupportedForm,
    DimensionMismatch,
    Cancelled,
    ResourceExceeded,
};

const char *position_motion_outcome_name(PositionMotionOutcome outcome);

struct PositionMotionResult {
    PositionMotionOutcome outcome = PositionMotionOutcome::InvalidInput;
    Quantity average_velocity;
    Quantity instantaneous_velocity;
    Quantity instantaneous_acceleration;
    static constexpr MotionStage kAverageVelocityStage = MotionStage::Interval;
    static constexpr MotionStage kInstantaneousStage = MotionStage::Event;
    // The differentiation engine's own symbolic results, so the walkthrough shows dx/dt and
    // d2x/dt2 rather than only their values at the event.
    NodeId position_expression = kNoNode;
    NodeId velocity_expression = kNoNode;
    NodeId acceleration_expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

PositionMotionResult solve_position_motion(Arena &arena, Derivation &derivation,
                                            const PositionMotionProblem &problem,
                                            const Budget &budget = Budget(),
                                            Backend *giac = nullptr);

}  // namespace nps

#endif
