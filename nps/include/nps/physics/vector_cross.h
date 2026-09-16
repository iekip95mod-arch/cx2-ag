#ifndef NPS_VECTOR_CROSS_H
#define NPS_VECTOR_CROSS_H

#include <cstdint>
#include <string>

#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

struct VectorCrossProblem {
    Vector first;
    Vector second;
};

enum class VectorCrossOutcome : uint8_t {
    Solved,
    RankMismatch,
    FrameMismatch,
    DimensionMismatch,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *vector_cross_outcome_name(VectorCrossOutcome outcome);

struct VectorCrossResult {
    VectorCrossOutcome outcome = VectorCrossOutcome::ResourceExceeded;
    Vector value;
    bool has_value = false;
    std::string value_text;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

VectorCrossResult solve_vector_cross(Arena &arena, Derivation &derivation,
                                     const VectorCrossProblem &problem,
                                     const Budget &budget = Budget());

}  // namespace nps

#endif
