#ifndef NPS_VECTOR_ADDITION_H
#define NPS_VECTOR_ADDITION_H

#include <cstdint>
#include <string>

#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

struct VectorAdditionProblem {
    Vector first;
    Vector second;
};

enum class VectorAdditionOutcome : uint8_t {
    Solved,
    RankMismatch,
    FrameMismatch,
    DimensionMismatch,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *vector_addition_outcome_name(VectorAdditionOutcome outcome);

struct VectorAdditionResult {
    VectorAdditionOutcome outcome = VectorAdditionOutcome::ResourceExceeded;
    Vector value;
    bool has_value = false;
    std::string value_text;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

VectorAdditionResult solve_vector_addition(Arena &arena, Derivation &derivation,
                                           const VectorAdditionProblem &problem,
                                           const Budget &budget = Budget());

}  // namespace nps

#endif
