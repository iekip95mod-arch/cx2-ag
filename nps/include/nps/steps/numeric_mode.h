#ifndef NPS_NUMERIC_MODE_H
#define NPS_NUMERIC_MODE_H

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

// The two steps the numeric mode adds to a derivation, in one place because three engines record
// them and PRD section 13.1 wants the handling of a decimal to look the same wherever it happens.
// Both modes promote on the way in, the way the native machine does in its Exact mode, and only the
// report on the way out belongs to decimal mode.
//
// Neither step rounds. A decimal literal is a fraction over a power of ten, so reading it as one is
// exact, and only a fraction whose decimal terminates is written back as a decimal. The promotion is
// recorded rather than silent, which is the whole of what makes reinterpreting what the user typed
// acceptable rather than the behaviour 13.1 warns about.

enum class ModeStep : uint8_t {
    // The expression held nothing this step applies to, and no record was made.
    NothingToDo,
    Recorded,
    // A literal carries more digits than the exact rational can hold. The caller refuses.
    TooManyDigits,
    // A decimal sits in an exponent, where promoting it would launder a measurement into exactness.
    DecimalExponent,
};

// Rewrites every decimal literal as the rational it names, recording the step when there was one.
// *out is the expression to carry on with, unchanged when there was nothing to convert.
ModeStep read_decimals_exactly(Arena &arena, Derivation &derivation, StepId parent,
                               const char *phase, NodeId expression, NodeId *out);

// Why a promotion could not happen, in the one wording every engine refuses with, or nullptr when
// there is nothing to refuse.
const char *promotion_refusal(ModeStep step);

// Whether that refusal is this build running out of room rather than a property of the input.
bool promotion_exhausted(ModeStep step);

// Writes each exact fraction with a terminating decimal back as that decimal, for the answer line.
// Run after the engine's own check, so what was verified is the exact form.
ModeStep report_in_decimals(Arena &arena, Derivation &derivation, StepId parent, const char *phase,
                            NodeId answer, NodeId *out);

}  // namespace nps

#endif
