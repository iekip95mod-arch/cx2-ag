#include "nps/steps/matrix_form.h"

#include <array>

#include "nps/core/matrix.h"

namespace nps {
namespace {

VerificationRecord checked(MatrixForm form, VerificationOutcome outcome, std::string detail) {
    VerificationRecord verification;
    verification.method = form == MatrixForm::Echelon ? "exact row echelon form" :
                                                       "exact reduced row echelon form";
    verification.outcome = outcome;
    verification.strength = strength_for(outcome, EvidenceStrength::StructurallyValid);
    verification.detail = std::move(detail);
    return verification;
}

}

VerificationRecord verify_matrix_form(const Arena &arena, NodeId matrix, MatrixForm form) {
    if (arena.failed())
        return checked(form, VerificationOutcome::Inconclusive, "the matrix arena is in a failed state");
    const auto view = MatrixView::from(arena, matrix);
    if (!view)
        return checked(form, VerificationOutcome::Failed, "the result must be a nonempty rectangular matrix");
    if (view->rows() > 4 || view->columns() > 6)
        return checked(form, VerificationOutcome::Inconclusive, "the exact form checker supports matrices up to 4 by 6");
    std::array<Rational, 24> values;
    const size_t columns = view->columns();
    if (!read_exact_matrix(arena, matrix, values))
        return checked(form, VerificationOutcome::Inconclusive,
                       "the matrix requires exact provenance and rational cells within the checked arithmetic bounds");

    size_t next_pivot = 0;
    bool zero_row = false;
    for (size_t row = 0; row < view->rows(); ++row) {
        size_t pivot = 0;
        while (pivot < columns && values[row * columns + pivot].num == 0)
            ++pivot;
        if (pivot == columns) {
            zero_row = true;
            continue;
        }
        if (zero_row)
            return checked(form, VerificationOutcome::Failed, "a nonzero row follows a zero row");
        if (pivot < next_pivot)
            return checked(form, VerificationOutcome::Failed, "leading nonzero entries must move strictly right in successive rows");
        next_pivot = pivot + 1;
        if (form == MatrixForm::ReducedEchelon) {
            const Rational &leading = values[row * columns + pivot];
            if (leading.num != leading.den)
                return checked(form, VerificationOutcome::Failed, "every leading entry in reduced row echelon form must equal one");
            for (size_t above = 0; above < row; ++above) {
                if (values[above * columns + pivot].num != 0)
                    return checked(form, VerificationOutcome::Failed, "every entry above a reduced pivot must equal zero");
            }
        }
    }
    return checked(form, VerificationOutcome::Passed,
                   form == MatrixForm::Echelon ? "zero rows are last and leading entries move strictly right with zeros below each pivot" :
                                                "zero rows are last and unit leading entries move strictly right with zeros elsewhere in each pivot column");
}

}
