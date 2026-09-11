#ifndef NPS_STEPS_MATRIX_ROW_H
#define NPS_STEPS_MATRIX_ROW_H

#include "nps/core/matrix.h"
#include "nps/steps/derivation.h"

namespace nps {

struct MatrixRowCheck {
    VerificationRecord verification;
    bool changed = false;

    // Only changing, verified operations qualify as teaching transformations.
    bool progress() const {
        return verification.outcome == VerificationOutcome::Passed && changed;
    }
};

MatrixRowCheck verify_matrix_row(const Arena &arena, NodeId before, NodeId after,
                                 const MatrixRowOperation &operation);

struct MatrixRowRecord {
    MatrixRowCheck check;
    // Verification can pass even when the recording budget withholds the step.
    StepId step = kNoStep;
};

MatrixRowRecord record_matrix_row(const Arena &arena, Derivation &derivation, Meter &meter,
                                  NodeId before, NodeId after, const MatrixRowOperation &operation,
                                  StepId parent = kNoStep);

}

#endif
