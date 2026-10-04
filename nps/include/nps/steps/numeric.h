#ifndef NPS_STEPS_NUMERIC_H
#define NPS_STEPS_NUMERIC_H

#include <optional>
#include <string_view>

#include "nps/steps/derivation.h"

namespace nps {

// CALC-019 methods a learner asks for by name. Each runs in exact rational arithmetic on a
// polynomial, records every iteration, and states an error bound that a final check proves or
// says it could not prove.
enum class NumericOutcome : uint8_t {
    Approximated,
    OutsideEnvelope,
    InvalidInput,
    NoSignChange,
    DidNotConverge,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

struct NumericResult {
    NumericOutcome outcome = NumericOutcome::OutsideEnvelope;
    DerivationStatus status = DerivationStatus::NotRecorded;
    NodeId value = kNoNode;
    NodeId bound = kNoNode;
    bool bound_certified = false;
    size_t iterations = 0;
    std::string detail;
    Cost cost;
};

std::optional<size_t> numeric_command_arity(std::string_view name);
const char *numeric_outcome_name(NumericOutcome outcome);
NumericResult numeric_method(Arena &arena, Derivation &derivation, NodeId call,
                             const Budget &budget = Budget());

}  // namespace nps

#endif
