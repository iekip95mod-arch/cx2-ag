#ifndef NPS_STEPS_MATRIX_DETERMINANT_H
#define NPS_STEPS_MATRIX_DETERMINANT_H

#include "nps/core/matrix.h"
#include "nps/steps/derivation.h"

namespace nps {

// Numerator and denominator each fit this many bits.
inline constexpr size_t kMatrixDeterminantBits = 4096;

VerificationRecord verify_matrix_determinant_factor(const Arena &arena,
    const MatrixRowOperation &operation, NodeId before_factor, NodeId after_factor);
VerificationRecord verify_matrix_determinant_diagonal(const Arena &arena,
    NodeId matrix, NodeId product);
VerificationRecord verify_matrix_determinant_correction(const Arena &arena,
    NodeId product, NodeId factor, NodeId answer);

}

#endif
