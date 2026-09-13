#ifndef NPS_STEPS_MATRIX_DETERMINANT_H
#define NPS_STEPS_MATRIX_DETERMINANT_H

#include <limits>

#include "nps/core/matrix.h"
#include "nps/steps/derivation.h"

namespace nps {

// Cell admission and the repeated row-factor ledger have different numeric envelopes.
inline constexpr size_t kMatrixCellValueBits = std::numeric_limits<int64_t>::digits;
inline constexpr size_t kMatrixDeterminantFactorBits = 4096;

VerificationRecord verify_matrix_determinant_factor(const Arena &arena,
    const MatrixRowOperation &operation, NodeId before_factor, NodeId after_factor);
VerificationRecord verify_matrix_determinant_diagonal(const Arena &arena,
    NodeId matrix, NodeId product);
VerificationRecord verify_matrix_determinant_correction(const Arena &arena,
    NodeId product, NodeId factor, NodeId answer);

}

#endif
