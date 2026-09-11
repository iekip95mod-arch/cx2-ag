#ifndef NPS_STEPS_INTEGER_H
#define NPS_STEPS_INTEGER_H

#include <optional>
#include <string_view>

#include "nps/core/task.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class IntegerOutcome : uint8_t {
    Evaluated,
    UnsupportedForm,
    InvalidInput,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

struct IntegerResult {
    IntegerOutcome outcome = IntegerOutcome::UnsupportedForm;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// Literal operands are bounded to 1024 bits, including results larger than int64.
struct IntegerDivisionProof {
    NodeId multiplicand = kNoNode;
    NodeId multiplier = kNoNode;
    NodeId divisor = kNoNode;
    NodeId quotient = kNoNode;
    NodeId remainder = kNoNode;
};

struct IntegerGcdProof {
    NodeId left = kNoNode;
    NodeId right = kNoNode;
    NodeId gcd = kNoNode;
    NodeId left_coefficient = kNoNode;
    NodeId right_coefficient = kNoNode;
};

std::optional<size_t> integer_command_arity(std::string_view name);
const char *integer_outcome_name(IntegerOutcome outcome);
VerificationRecord verify_integer_division(const Arena &arena, const IntegerDivisionProof &proof);
VerificationRecord verify_integer_gcd(const Arena &arena, const IntegerGcdProof &proof);
// Arena and derivation must outlive the continuation.
Coroutine<IntegerResult> integer_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                       NodeId expression, Budget budget = Budget());
IntegerResult integer_method(Arena &arena, Derivation &derivation, NodeId expression,
                              const Budget &budget = Budget());

}
#endif
