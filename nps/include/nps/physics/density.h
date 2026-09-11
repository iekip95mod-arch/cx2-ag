#ifndef NPS_DENSITY_H
#define NPS_DENSITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// The three typed positions in the defining relation m = rho*V.
enum class DensityVariable : uint8_t {
    Mass,
    Volume,
    Density,
};

const char *density_variable_name(DensityVariable variable);

struct DensityKnown {
    DensityVariable variable = DensityVariable::Mass;
    Quantity quantity;
};

struct DensityProblem {
    DensityVariable unknown = DensityVariable::Mass;
    std::vector<DensityKnown> knowns;
};

enum class DensityOutcome : uint8_t {
    Solved,
    InvalidProblem,
    MissingKnown,
    DuplicateKnown,
    DimensionMismatch,
    // Reserved for a source-backed domain rule. This family currently states none.
    UnphysicalValue,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *density_outcome_name(DensityOutcome outcome);

struct DensityResult {
    DensityOutcome outcome = DensityOutcome::InvalidProblem;
    // The exact SI answer and its precision metadata. Only value_text is rounded for reporting.
    Quantity quantity;
    std::string value_text;
    std::string unit_text;
    std::string detail;
    NodeId value = kNoNode;
    NodeId unknown = kNoNode;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

DensityResult solve_density(Arena &arena, Derivation &derivation, const DensityProblem &problem,
                            const Budget &budget = Budget());

}  // namespace nps

#endif
