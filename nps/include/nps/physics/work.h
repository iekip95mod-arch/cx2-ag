#ifndef NPS_PHYSICS_WORK_H
#define NPS_PHYSICS_WORK_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

enum class WorkForceProfile : uint8_t {
    Unspecified,
    Constant,
    Variable,
};

const char *work_force_profile_name(WorkForceProfile profile);

struct WorkProblem {
    Vector force;
    Vector displacement;
    WorkForceProfile force_profile = WorkForceProfile::Unspecified;
};

enum class WorkOutcome : uint8_t {
    Solved,
    InvalidProblem,
    ClarificationRequired,
    LawNotApplicable,
    RankMismatch,
    FrameUndeclared,
    FrameMismatch,
    DimensionMismatch,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *work_outcome_name(WorkOutcome outcome);

enum class WorkSign : uint8_t {
    Negative,
    Zero,
    Positive,
};

const char *work_sign_name(WorkSign sign);

struct WorkResult {
    WorkOutcome outcome = WorkOutcome::InvalidProblem;
    Quantity quantity;
    bool has_value = false;
    WorkSign sign = WorkSign::Zero;
    std::string value_text;
    std::string unit_text;
    std::string interpretation;
    std::string detail;
    NodeId value = kNoNode;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    NodeId backend_value = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

WorkResult solve_work(Arena &arena, Derivation &derivation, const WorkProblem &problem,
                      const Budget &budget = Budget(), Backend *giac = nullptr);

}

#endif
