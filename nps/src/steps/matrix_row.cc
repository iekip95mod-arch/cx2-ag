#include "nps/steps/matrix_row.h"

#include <array>

namespace nps {
namespace {

MatrixRowCheck checked(VerificationOutcome outcome, std::string detail, bool changed = false) {
    MatrixRowCheck check;
    check.verification.method = "exact reversible row operation";
    check.verification.outcome = outcome;
    check.verification.strength = strength_for(outcome, EvidenceStrength::StructurallyValid);
    check.verification.detail = std::move(detail);
    check.changed = changed;
    return check;
}

bool equal(const Rational &left, const Rational &right) {
    return left.num == right.num && left.den == right.den;
}

bool add_multiple(const Rational &target, const Rational &source, const Rational &factor,
                  Rational *out) {
    detail::Mpq sum, term, coefficient;
    detail::mpq_set_rational(sum.get(), target);
    detail::mpq_set_rational(term.get(), source);
    detail::mpq_set_rational(coefficient.get(), factor);
    // Keep a cancelling product exact before narrowing the final cell.
    mpq_mul(term.get(), term.get(), coefficient.get());
    mpq_add(sum.get(), sum.get(), term.get());
    return detail::mpq_get_rational(sum.get(), out);
}

}

MatrixRowCheck verify_matrix_row(const Arena &arena, NodeId before, NodeId after,
                                 const MatrixRowOperation &operation) {
    if (arena.failed())
        return checked(VerificationOutcome::Inconclusive, "the matrix arena is in a failed state");
    const auto original = MatrixView::from(arena, before);
    const auto transformed = MatrixView::from(arena, after);
    if (!original || !transformed)
        return checked(VerificationOutcome::Failed, "both row-operation states must be nonempty rectangular matrices");
    if (original->rows() != transformed->rows() || original->columns() != transformed->columns())
        return checked(VerificationOutcome::Failed, "a row operation must preserve matrix dimensions");
    if (original->rows() > 4 || original->columns() > 6)
        return checked(VerificationOutcome::Inconclusive, "the exact row checker supports matrices up to 4 by 6");

    const MatrixRowSwap *swap = std::get_if<MatrixRowSwap>(&operation);
    const MatrixRowScale *scale = std::get_if<MatrixRowScale>(&operation);
    const MatrixRowAddMultiple *addition = std::get_if<MatrixRowAddMultiple>(&operation);
    Rational factor;
    if (swap) {
        if (swap->first >= original->rows() || swap->second >= original->rows() ||
            swap->first == swap->second)
            return checked(VerificationOutcome::Failed, "a row swap requires two distinct existing rows");
    } else {
        const Rational supplied = scale ? scale->factor : addition->factor;
        if ((scale && scale->row >= original->rows()) ||
            (addition && (addition->target >= original->rows() || addition->source >= original->rows() ||
                          addition->target == addition->source)))
            return checked(VerificationOutcome::Failed, "row indices must exist and an added source must differ from its target");
        if (supplied.den == 0 || (scale && supplied.num == 0))
            return checked(VerificationOutcome::Failed, "the multiplier must be rational and a row scale must be nonzero");
        if (!rational_mul(supplied, Rational{1, 1}, &factor))
            return checked(VerificationOutcome::Inconclusive, "the multiplier exceeds the checked rational range");
    }

    std::array<Rational, 24> before_values;
    std::array<Rational, 24> after_values;
    const size_t columns = original->columns();
    if (!read_exact_matrix(arena, before, before_values) || !read_exact_matrix(arena, after, after_values))
        return checked(VerificationOutcome::Inconclusive,
                       "both matrices require exact provenance and rational cells within the checked arithmetic bounds");

    bool changed = false;
    for (size_t row = 0; row < original->rows(); ++row) {
        for (size_t column = 0; column < columns; ++column) {
            const size_t index = row * columns + column;
            Rational expected = before_values[index];
            if (swap && row == swap->first)
                expected = before_values[swap->second * columns + column];
            else if (swap && row == swap->second)
                expected = before_values[swap->first * columns + column];
            else if (scale && row == scale->row) {
                if (!rational_mul(expected, factor, &expected))
                    return checked(VerificationOutcome::Inconclusive, "a scaled cell exceeds the checked rational range");
            } else if (addition && row == addition->target) {
                if (!add_multiple(expected, before_values[addition->source * columns + column], factor, &expected))
                    return checked(VerificationOutcome::Inconclusive, "an added cell exceeds the checked rational range");
            }
            if (!equal(expected, after_values[index]))
                return checked(VerificationOutcome::Failed,
                               "row " + std::to_string(row + 1) + ", column " + std::to_string(column + 1) +
                                   " differs from the exact recorded row operation");
            changed = changed || !equal(before_values[index], after_values[index]);
        }
    }
    return checked(VerificationOutcome::Passed,
                   changed ? "the reversible row operation matches every cell, including unchanged rows"
                           : "the reversible row operation leaves every matrix value unchanged",
                   changed);
}

MatrixRowRecord record_matrix_row(const Arena &arena, Derivation &derivation, Meter &meter,
                                  NodeId before, NodeId after, const MatrixRowOperation &operation,
                                  StepId parent) {
    MatrixRowRecord record;
    if (!meter.checkpoint()) {
        record.check = checked(VerificationOutcome::NotAttempted, halt_name(meter.halt()));
        return record;
    }
    if (parent != kNoStep && parent >= derivation.size()) {
        record.check = checked(VerificationOutcome::Failed, "the parent step does not exist");
        return record;
    }
    record.check = verify_matrix_row(arena, before, after, operation);
    if (!record.check.progress() || !meter.step())
        return record;

    Step step;
    step.phase = "Row reduction";
    step.goal = "Reduce the matrix by reversible row operations";
    step.claim = ClaimType::RowEquivalent;
    step.proof_obligations.push_back({"obl.matrix.row-equivalent",
        "an exact reversible row operation produces every recorded cell"});
    record.check.verification.evidence_id = "obl.matrix.row-equivalent";
    step.verifications.push_back(record.check.verification);
    TransformationPayload change;
    change.before = before;
    change.after = after;
    change.reversible = true;
    if (const auto *swap = std::get_if<MatrixRowSwap>(&operation)) {
        step.rule_id = "matrix.row-swap";
        step.rule_name = "Swap two rows";
        change.concrete_action = "Swap row " + std::to_string(swap->first + 1) +
                                 " with row " + std::to_string(swap->second + 1) + ".";
        step.explanation_short = "Swapping the same two rows again restores the original matrix.";
        step.explanation_detailed = "Move every entry with its row. The order changes, "
                                    "but the row information is preserved.";
    } else {
        const auto *scale = std::get_if<MatrixRowScale>(&operation);
        const auto *addition = std::get_if<MatrixRowAddMultiple>(&operation);
        Rational factor;
        rational_mul(scale ? scale->factor : addition->factor, Rational{1, 1}, &factor);
        std::string coefficient = std::to_string(factor.num);
        if (factor.den != 1)
            coefficient += "/" + std::to_string(factor.den);
        if (scale) {
            step.rule_id = "matrix.row-scale";
            step.rule_name = "Scale a row";
            change.concrete_action = "Multiply every entry in row " + std::to_string(scale->row + 1) +
                                     " by " + coefficient + ".";
            step.explanation_short = "The multiplier is nonzero. Dividing the new row by " +
                                      coefficient + " restores the original row.";
            step.explanation_detailed = "Apply the same multiplier to every entry in the row, "
                                        "including any right-hand-side entry. Keep the other rows unchanged.";
        } else {
            step.rule_id = "matrix.row-add-multiple";
            step.rule_name = "Add a multiple of another row";
            const std::string source = std::to_string(addition->source + 1);
            change.concrete_action = "Add " + coefficient + " times row " + source + " to row " +
                                     std::to_string(addition->target + 1) + ". Keep row " + source + " unchanged.";
            step.explanation_short = "Adding a multiple of a different row is reversible: "
                                      "subtract the same multiple to restore the target row.";
            step.explanation_detailed = "For each column, multiply the source entry by " +
                coefficient + " and add it to the target entry. Change only the target row.";
        }
    }
    record.step = derivation.add_transformation(parent, std::move(step), std::move(change));
    return record;
}

}
