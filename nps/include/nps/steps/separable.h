#ifndef NPS_STEPS_SEPARABLE_H
#define NPS_STEPS_SEPARABLE_H

#include <string>

#include "nps/core/budgets.h"
#include "nps/steps/command.h"
#include "nps/steps/derivation.h"

namespace nps {

// CALC-012. Separates y' = f(x) g(y), integrates each side and solves for y where it can.
enum class SeparableOutcome : uint8_t {
    Solved,
    UnsupportedForm,
    InvalidInput,
    // The final check could not confirm the solution, so it is withheld without calling it wrong.
    Refused,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *separable_outcome_name(SeparableOutcome outcome);

struct SeparableResult {
    SeparableOutcome outcome = SeparableOutcome::UnsupportedForm;
    // y = Y(x) when the relation was solved for y, otherwise H(y) = F(x) + C as integrated.
    NodeId solution = kNoNode;
    bool explicit_solution = false;
    // The value the initial point gave the constant, or kNoNode for the general solution.
    NodeId constant = kNoNode;
    DerivationStatus status = DerivationStatus::Unsupported;
    std::string detail;
    Cost cost;
};

SeparableResult solve_separable(Arena &arena, Derivation &derivation, const Command &command,
                                const Budget &budget = Budget());

}  // namespace nps

#endif
