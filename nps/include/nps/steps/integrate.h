#ifndef NPS_INTEGRATE_H
#define NPS_INTEGRATE_H

#include <optional>

#include "nps/core/ast.h"
#include "nps/core/rational.h"
#include "nps/steps/derivation.h"

namespace nps {

class Backend;

enum class IntegrateOutcome : uint8_t {
    Integrated,
    UnsupportedForm,
    NotAVariable,
    VerificationFailed,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *integrate_outcome_name(IntegrateOutcome o);

struct IntegrateResult {
    IntegrateOutcome outcome = IntegrateOutcome::Refused;
    // The general antiderivative, constant of integration included. What the user is shown.
    NodeId antiderivative = kNoNode;
    // The same without the constant, for a cross-check against an engine that omits it.
    NodeId particular = kNoNode;
    std::string detail;
    // Section 15's outcome, stated by the rule that decided it rather than inferred from the
    // outcome later. It is what the derivation's SolutionContext records.
    DerivationStatus status = DerivationStatus::NotRecorded;
    // What it spent, the derivative check included.
    Cost cost;
};

// Verify native antiderivative rules by differentiation, with an optional metered Giac identity check.
IntegrateResult integrate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                          const Budget &budget = Budget(), Backend *backend = nullptr);
// The point selects a real logarithm branch, while recorded restrictions still require proof.
IntegrateResult integrate_particular(Arena &arena, Derivation &derivation, NodeId expression,
                                    NodeId variable, Meter &meter,
                                    std::optional<Rational> branch_point = std::nullopt,
                                    Backend *backend = nullptr);

}  // namespace nps

#endif
