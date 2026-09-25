#ifndef NPS_REARRANGE_H
#define NPS_REARRANGE_H

#include <string>
#include <vector>

#include "nps/cas/giac_adapter.h"
#include "nps/core/ast.h"
#include "nps/core/task.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class RearrangeOutcome : uint8_t {
    Isolated,
    NotAnEquation,
    NotAVariable,
    VariableAbsent,
    VariableRepeated,
    UnsupportedForm,
    VerificationFailed,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *rearrange_outcome_name(RearrangeOutcome o);

struct RearrangeResult {
    RearrangeOutcome outcome = RearrangeOutcome::Refused;
    // The whole rearranged formula, variable = expression, and its right side on its own.
    NodeId formula = kNoNode;
    NodeId expression = kNoNode;
    std::vector<std::string> restrictions;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// ALG-007. Rearranges a formula so the requested variable stands alone on the left, by undoing the
// operations wrapped around it one at a time. Every move is an inverse operation applied to both
// sides and recorded as its own step, which is the product here; the isolated expression is a
// by-product.
//
// The envelope is one occurrence of the variable. A formula that mentions it twice needs the terms
// collected first, which is a different rule, so it is refused rather than half done. So is any
// wrapper with no inverse that keeps a single real value: an even power (two roots), a function
// (no inverse rule here), and the variable in an exponent (a logarithm).
//
// A divisor that is not a literal is recorded as a domain restriction rather than assumed non-zero,
// so the answer says what it depends on.
//
// The backend is optional and is a second opinion only: with one supplied the isolated formula is
// put back into the original and Giac is asked whether the difference is zero, and a backend that
// disagrees fails the answer rather than replacing it.
// Borrowed state must outlive the coroutine, including every suspension.
Coroutine<RearrangeResult> rearrange_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                         NodeId equation, NodeId variable, Meter &meter, Budget budget,
                                         Backend *giac = nullptr);

RearrangeResult rearrange(Arena &arena, Derivation &derivation, NodeId equation, NodeId variable,
                          const Budget &budget = Budget(), Backend *giac = nullptr);
RearrangeResult rearrange(Arena &arena, Derivation &derivation, NodeId equation, NodeId variable,
                          const Budget &budget, Backend *giac, size_t frame_bytes);

// Every occurrence of the symbol replaced by the replacement, rebuilding only what changed.
NodeId substitute_symbol(Arena &arena, NodeId id, NodeId symbol, NodeId replacement);

}  // namespace nps

#endif
