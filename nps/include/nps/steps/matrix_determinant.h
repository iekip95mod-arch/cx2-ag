#ifndef NPS_STEPS_MATRIX_DETERMINANT_H
#define NPS_STEPS_MATRIX_DETERMINANT_H

#include <limits>

#include "nps/core/matrix.h"
#include "nps/steps/derivation.h"

namespace nps {

// Cell admission follows canonical Rational bounds while the repeated factor ledger uses GMP.
inline constexpr int64_t kMatrixCellNumeratorMinimum = std::numeric_limits<int64_t>::min();
inline constexpr int64_t kMatrixCellNumeratorMaximum = std::numeric_limits<int64_t>::max();
inline constexpr int64_t kMatrixCellDenominatorMinimum = 1;
inline constexpr int64_t kMatrixCellDenominatorMaximum = std::numeric_limits<int64_t>::max();
inline constexpr size_t kMatrixDeterminantFactorBits = 4096;

VerificationRecord verify_matrix_determinant_factor(const Arena &arena,
    const MatrixRowOperation &operation, NodeId before_factor, NodeId after_factor);
VerificationRecord verify_matrix_determinant_diagonal(const Arena &arena,
    NodeId matrix, NodeId product);
VerificationRecord verify_matrix_determinant_correction(const Arena &arena,
    NodeId product, NodeId factor, NodeId answer);

}

#endif
