#ifndef NPS_STEPS_MATRIX_H
#define NPS_STEPS_MATRIX_H

#include "nps/cas/giac_adapter.h"
#include "nps/steps/matrix_form.h"

namespace nps {

enum class MatrixOutcome : uint8_t {
    Reduced,
    UnsupportedForm,
    InvalidInput,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
    DependencyUnavailable,
    Determined,
};

struct MatrixResult {
    MatrixOutcome outcome = MatrixOutcome::UnsupportedForm;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

const char *matrix_outcome_name(MatrixOutcome outcome);
MatrixResult matrix_method(Arena &arena, Adapter &adapter, Derivation &derivation,
                           NodeId matrix, MatrixForm form, const Budget &budget = Budget());

MatrixResult matrix_determinant(Arena &arena, Adapter &adapter, Derivation &derivation,
                                NodeId matrix, const Budget &budget = Budget());

}

#endif
