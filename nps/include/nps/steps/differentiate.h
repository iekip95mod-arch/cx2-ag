#ifndef NPS_DIFFERENTIATE_H
#define NPS_DIFFERENTIATE_H

#include "nps/cas/giac_adapter.h"
#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class DiffOutcome : uint8_t {
    Differentiated,
    UnsupportedForm,
    NotAVariable,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *diff_outcome_name(DiffOutcome o);

struct DiffResult {
    DiffOutcome outcome = DiffOutcome::Refused;
    NodeId derivative = kNoNode;
    std::string detail;
    // Section 15's outcome, stated by the rule that decided it rather than inferred from the
    // outcome later. It is what the derivation's SolutionContext records.
    DerivationStatus status = DerivationStatus::NotRecorded;
    // What it spent, for the budgets PERF-010 freezes.
    Cost cost;
};

// Differentiates by rule, recording one step per rule application and nesting the steps for the
// parts, which is what PRD section 9 means by nested step expansion: the product rule's record has
// the two derivatives it needed as children rather than as an unexplained jump.
//
// Rules covered: constant, the variable itself, sum, constant multiple (a negation included),
// product, quotient, integer power, chain, and the usual exponential, logarithmic and trigonometric
// functions. Anything else stops explicitly.
//
// The budget is PERF-003's cancellation and PERF-008's rewrite and step limits. Halting for either
// reason, or stopping at a form with no rule, keeps the run of checked steps and labels it with the
// outcome rather than rewinding. PERF-009 forbids a partly accepted or mislabeled derivation, and a
// checked prefix labelled resource limit reached is neither. A halt landing before anything was
// checked keeps nothing, which is that same rule reaching an empty prefix and not a second rule.
// VER-004. Supplying a backend adds one check step at the end: Giac differentiates the same input
// and the two answers are canonicalized and compared. What that check is worth depends on whether
// Giac had already been asked for something below it, so the rule reports whether the answer was
// independent of the checker rather than leaving a reader to assume it was.
DiffResult differentiate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                         const Budget &budget = Budget(), Backend *giac = nullptr);
// Nested work uses the parent's meter and returns its cumulative cost.
DiffResult differentiate(Arena &arena, Derivation &derivation, NodeId expression, NodeId variable,
                         Meter &meter, Backend *giac = nullptr);

}  // namespace nps

#endif
