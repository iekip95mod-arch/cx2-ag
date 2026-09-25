#ifndef NPS_STEPS_TRIG_H
#define NPS_STEPS_TRIG_H

#include <string>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class TrigReading : uint8_t { Equal, Different, Unreadable };

// Whether two expressions are the same trigonometric polynomial, decided exactly. Each is written as a
// Laurent polynomial in e to the i times a base angle for every variable, with exact complex rational
// coefficients, and the two are equal exactly when every coefficient is. Unreadable when either has a
// form outside that envelope, such as a constant inside an angle, a variable outside sin or cos, or tan.
TrigReading trig_equivalent(const Arena &arena, NodeId left, NodeId right, std::string *why);

enum class TrigGoal : uint8_t { Expand, Collect };

enum class TrigOutcome : uint8_t {
    Rewritten,
    AlreadyInForm,
    NotTrigonometric,
    OutsideEnvelope,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *trig_outcome_name(TrigOutcome outcome);

struct TrigResult {
    TrigOutcome outcome = TrigOutcome::OutsideEnvelope;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// ALG-010. Expand applies the angle-sum, double-angle and odd and even identities until no sine or
// cosine of a sum or a whole multiple is left. Collect applies the Pythagorean identity, the
// half-angle identities that reduce a square, and the double-angle identity read backwards, then
// collects like terms. Every step names its identity and the result is checked exactly.
TrigResult trig_rewrite(Arena &arena, Derivation &derivation, NodeId expression, TrigGoal goal,
                        const Budget &budget = Budget());

}  // namespace nps

#endif
