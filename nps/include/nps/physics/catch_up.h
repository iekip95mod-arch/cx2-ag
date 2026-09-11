#ifndef NPS_PHYSICS_CATCH_UP_H
#define NPS_PHYSICS_CATCH_UP_H

#include <cstdint>
#include <string>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

enum class CatchUpMotionModel : uint8_t {
    ConstantVelocity,
    ConstantAcceleration,
};

const char *catch_up_motion_model_name(CatchUpMotionModel model);

struct CatchUpBody {
    std::string name;
    Frame frame;
    Quantity position_at_start;
    Quantity velocity_at_start;
    Quantity acceleration;
    Quantity start_time;
    CatchUpMotionModel motion = CatchUpMotionModel::ConstantVelocity;
};

struct CatchUpProblem {
    CatchUpBody first;
    CatchUpBody second;
};

enum class CatchUpOutcome : uint8_t {
    Solved,
    InvalidProblem,
    DuplicateBody,
    FrameUndeclared,
    FrameMismatch,
    DimensionMismatch,
    NonlinearMotionUnsupported,
    NoMeeting,
    AllTimesMeeting,
    BeforeSharedDomain,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

constexpr bool catch_up_has_result(CatchUpOutcome outcome) {
    return outcome == CatchUpOutcome::Solved || outcome == CatchUpOutcome::NoMeeting ||
           outcome == CatchUpOutcome::AllTimesMeeting;
}

const char *catch_up_outcome_name(CatchUpOutcome outcome);

struct CatchUpResult {
    CatchUpOutcome outcome = CatchUpOutcome::InvalidProblem;
    Quantity event_time;
    Quantity event_position;
    Quantity shared_active_start;
    std::string event_time_text;
    std::string event_position_text;
    std::string time_unit_text;
    std::string position_unit_text;
    std::string detail;
    NodeId time = kNoNode;
    NodeId first_position = kNoNode;
    NodeId second_position = kNoNode;
    NodeId equation = kNoNode;
    NodeId active_domain = kNoNode;
    NodeId substituted = kNoNode;
    NodeId first_at_event = kNoNode;
    NodeId second_at_event = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

CatchUpResult solve_catch_up(Arena &arena, Derivation &derivation,
                             const CatchUpProblem &problem,
                             const Budget &budget = Budget());

}  // namespace nps

#endif
