#ifndef NPS_STEPS_MATRIX_FORM_H
#define NPS_STEPS_MATRIX_FORM_H

#include "nps/steps/derivation.h"

namespace nps {

enum class MatrixForm { Echelon, ReducedEchelon };

VerificationRecord verify_matrix_form(const Arena &arena, NodeId matrix, MatrixForm form);

}

#endif
