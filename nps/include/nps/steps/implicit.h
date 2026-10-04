#ifndef NPS_IMPLICIT_H
#define NPS_IMPLICIT_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class ImplicitOutcome : uint8_t {
    Differentiated,
    NotAnEquation,
    UnsupportedForm,
    InvalidInput,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *implicit_outcome_name(ImplicitOutcome outcome);

struct ImplicitResult {
    ImplicitOutcome outcome = ImplicitOutcome::UnsupportedForm;
    // The derivative of the dependent variable, an expression in both variables, and the symbol that
    // stands for it in the differentiated equation.
    NodeId derivative = kNoNode;
    NodeId symbol = kNoNode;
    std::vector<std::string> restrictions;
    DerivationStatus status = DerivationStatus::Unsupported;
    std::string detail;
    Cost cost;
};

// CALC-007. Differentiates both sides of an equation with respect to the independent variable,
// reading the dependent variable as a function of it, so every occurrence contributes its partial
// derivative times the unknown derivative. The derivative terms are then collected to one side and
// the derivative is isolated by the rearrangement family. The final check differentiates the whole
// relation afresh and evaluates the chain rule identity exactly at sample points.
ImplicitResult implicit_differentiate(Arena &arena, Derivation &derivation, NodeId equation,
                                      NodeId independent, NodeId dependent,
                                      const Budget &budget = Budget());

}

#endif
