#ifndef NPS_STEPS_POWER_H
#define NPS_STEPS_POWER_H

#include <string>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class PowerReading : uint8_t { Equal, Different, Unreadable };

// Whether two expressions in at most one variable take the same value wherever the first one has a
// real value. The variable is replaced by s^L, with L twice the least common denominator of every
// exponent, so both sides become Laurent polynomials in s on each sign of s, and they are compared
// exactly at more points of each sign than such a difference can have roots.
PowerReading power_equivalent(const Arena &arena, NodeId before, NodeId after, std::string *why);

enum class PowerOutcome : uint8_t {
    Rewritten,
    AlreadyInForm,
    NoRealValue,
    OutsideEnvelope,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *power_outcome_name(PowerOutcome outcome);

struct PowerResult {
    PowerOutcome outcome = PowerOutcome::OutsideEnvelope;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// ALG-006. Applies the power laws and simplifies radicals one law to a step, checking each law's
// real-domain precondition and recording the condition it needs, then checks the result exactly.
PowerResult simplify_powers(Arena &arena, Derivation &derivation, NodeId expression,
                            const Budget &budget = Budget());

}  // namespace nps

#endif
