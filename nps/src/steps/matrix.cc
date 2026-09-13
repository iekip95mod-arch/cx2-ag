#include "nps/steps/matrix.h"

#include "nps/cas/matrix_events.h"
#include "nps/core/context.h"
#include "nps/core/print.h"
#include "nps/steps/matrix_row.h"
#include "nps/steps/matrix_determinant.h"

namespace nps {
namespace {

bool within_determinant_work_limit(mpq_srcptr value) {
    return mpz_sizeinbase(mpq_numref(value), 2) <= kMatrixDeterminantFactorBits &&
           mpz_sizeinbase(mpq_denref(value), 2) <= kMatrixDeterminantFactorBits;
}

bool read_scalar(const Arena &arena, NodeId id, mpq_ptr value, size_t depth = 0) {
    if (arena.failed() || arena.is_approximate(id) || depth > 4)
        return false;
    const Node &node = arena.at(id);
    const auto children = arena.children(id);
    if (node.kind == Kind::Integer) {
        const std::string &text = arena.text(id);
        if (text.empty() || text.size() > 1235 || mpz_set_str(mpq_numref(value), text.c_str(), 10))
            return false;
        mpz_set_ui(mpq_denref(value), 1);
        return within_determinant_work_limit(value);
    }
    if (node.kind == Kind::Neg && children.size() == 1) {
        if (!read_scalar(arena, children[0], value, depth + 1))
            return false;
        mpq_neg(value, value);
        return true;
    }
    if (node.kind == Kind::Pow && children.size() == 2) {
        int64_t exponent = 0;
        const NodeId exponent_id = children[1];
        if (!small_integer(arena, exponent_id, &exponent) || exponent != -1 ||
            arena.is_approximate(exponent_id) ||
            (arena.at(exponent_id).kind == Kind::Neg && arena.is_approximate(arena.children(exponent_id)[0])) ||
            !read_scalar(arena, children[0], value, depth + 1) || mpq_sgn(value) == 0)
            return false;
        mpq_inv(value, value);
        return true;
    }
    if (node.kind != Kind::Mul || children.size() != 2)
        return false;
    detail::Mpq right;
    if (!read_scalar(arena, children[0], value, depth + 1) ||
        !read_scalar(arena, children[1], right.get(), depth + 1))
        return false;
    mpq_mul(value, value, right.get());
    return within_determinant_work_limit(value);
}

NodeId scalar_node(Arena &arena, mpq_srcptr value) {
    if (!within_determinant_work_limit(value))
        return kNoNode;
    std::array<char, 1236> text{};
    mpz_get_str(text.data(), 10, mpq_numref(value));
    const NodeId numerator = arena.integer(text.data());
    if (mpz_cmp_ui(mpq_denref(value), 1) == 0)
        return numerator;
    mpz_get_str(text.data(), 10, mpq_denref(value));
    const NodeId denominator = arena.integer(text.data());
    return arena.binary(Kind::Mul, numerator, arena.binary(Kind::Pow, denominator, arena.integer("-1")));
}

std::string scalar_text(mpq_srcptr value) {
    std::array<char, 1236> text{};
    mpz_get_str(text.data(), 10, mpq_numref(value));
    std::string fraction = text.data();
    if (mpz_cmp_ui(mpq_denref(value), 1) != 0) {
        mpz_get_str(text.data(), 10, mpq_denref(value));
        fraction += "/";
        fraction += text.data();
    }
    return fraction;
}

NodeId quotient(Arena &arena, NodeId numerator, NodeId denominator) {
    const Node &factor = arena.at(denominator);
    if (factor.kind == Kind::Integer && factor.small_valid && factor.small == 1)
        return numerator;
    return arena.binary(Kind::Mul, numerator, arena.binary(Kind::Pow, denominator, arena.integer("-1")));
}

bool row_multiplier(const MatrixRowOperation &operation, mpq_ptr multiplier) {
    if (const auto *swap = std::get_if<MatrixRowSwap>(&operation)) {
        if (swap->first == swap->second || swap->first >= 4 || swap->second >= 4)
            return false;
        mpq_set_si(multiplier, -1, 1);
        return true;
    }
    if (const auto *scale = std::get_if<MatrixRowScale>(&operation))
        return scale->row < 4 && scale->factor.num != 0 &&
               detail::mpq_set_rational(multiplier, scale->factor);
    const auto &addition = std::get<MatrixRowAddMultiple>(operation);
    if (addition.target == addition.source || addition.target >= 4 || addition.source >= 4 ||
        addition.factor.den == 0)
        return false;
    mpq_set_ui(multiplier, 1, 1);
    return true;
}

VerificationRecord determinant_check(const char *method, const char *obligation,
    VerificationOutcome outcome, std::string detail) {
    return {method, outcome, strength_for(outcome, EvidenceStrength::StructurallyValid),
            std::move(detail), obligation};
}

bool diagonal_product(const Arena &arena, NodeId matrix, mpq_ptr product) {
    const auto view = MatrixView::from(arena, matrix);
    std::array<Rational, 24> values;
    if (!view || view->rows() != view->columns() || !read_exact_matrix(arena, matrix, values))
        return false;
    mpq_set_ui(product, 1, 1);
    detail::Mpq cell;
    for (size_t index = 0; index < view->rows(); ++index) {
        detail::mpq_set_rational(cell.get(), values[index * view->columns() + index]);
        mpq_mul(product, product, cell.get());
    }
    return true;
}

struct Run final : MatrixRowSink {
    Arena &arena;
    Derivation &derivation;
    NodeId input;
    NodeId current;
    MatrixForm form;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    bool failed = false;
    MatrixOutcome failure = MatrixOutcome::UnsupportedForm;
    std::string detail;
    bool determinant;
    detail::Mpq factor;
    NodeId factor_id = kNoNode;

    Run(Arena &a, Derivation &d, NodeId matrix, MatrixForm f, const Budget &budget, bool det = false)
        : arena(a), derivation(d), input(matrix), current(matrix), form(f), meter(budget), mark(d.mark()), determinant(det) {
        mpq_set_ui(factor.get(), 1, 1);
    }

    const char *method() const { return determinant ? "det" : form == MatrixForm::Echelon ? "ref" : "rref"; }

    MatrixRowRecord determinant_row(NodeId before, NodeId after, const MatrixRowOperation &operation) {
        MatrixRowRecord record;
        record.check = verify_matrix_row(arena, before, after, operation);
        // An unchanged matrix retains the learner's existing determinant factor.
        if (!record.check.progress())
            return record;
        if (!meter.step())
            return record;
        detail::Mpq multiplier, next_factor;
        if (!row_multiplier(operation, multiplier.get())) {
            refuse(MatrixOutcome::VerificationFailed, "the row operation has no reversible determinant effect");
            return record;
        }
        mpq_mul(next_factor.get(), factor.get(), multiplier.get());
        if (!within_determinant_work_limit(next_factor.get())) {
            refuse(MatrixOutcome::ResourceExceeded, "the determinant factor exceeds " +
                std::to_string(kMatrixDeterminantFactorBits) + " bits");
            return record;
        }
        const NodeId next_id = scalar_node(arena, next_factor.get());
        VerificationRecord effect = verify_matrix_determinant_factor(arena, operation, factor_id, next_id);
        if (!running())
            return record;
        if (effect.outcome != VerificationOutcome::Passed) {
            refuse(MatrixOutcome::VerificationFailed, effect.detail);
            return record;
        }
        Step step;
        step.phase = "Track the determinant";
        step.goal = "Reduce the matrix while retaining its original determinant";
        step.claim = ClaimType::EquivalentExpression;
        record.check.verification.evidence_id = "obl.matrix.row-equivalent";
        step.proof_obligations = {{"obl.matrix.row-equivalent", "the actual row operation matches every recorded matrix cell"},
                                 {"obl.matrix.det-factor", "the accumulated nonzero factor follows the row determinant law"}};
        step.verifications = {record.check.verification, effect};
        TransformationPayload change;
        change.before = quotient(arena, arena.call("det", {before}), factor_id);
        change.after = quotient(arena, arena.call("det", {after}), next_id);
        if (!running())
            return record;
        change.reversible = true;
        if (const auto *swap = std::get_if<MatrixRowSwap>(&operation)) {
            step.rule_id = "matrix.det-row-swap";
            step.rule_name = "Swap rows and reverse the determinant sign";
            change.concrete_action = "Swap row " + std::to_string(swap->first + 1) + " with row " +
                                     std::to_string(swap->second + 1) + ". Negate the determinant factor.";
            step.explanation_short = "Swapping two rows negates the determinant.";
        } else if (const auto *scale = std::get_if<MatrixRowScale>(&operation)) {
            step.rule_id = "matrix.det-row-scale";
            step.rule_name = "Scale a row and track the determinant multiplier";
            change.concrete_action = "Multiply row " + std::to_string(scale->row + 1) + " by " +
                                     scalar_text(multiplier.get()) + ". Multiply the determinant factor by the same number.";
            step.explanation_short = "Scaling one row multiplies the determinant by that nonzero number.";
        } else {
            const auto &addition = std::get<MatrixRowAddMultiple>(operation);
            detail::Mpq coefficient;
            detail::mpq_set_rational(coefficient.get(), addition.factor);
            step.rule_id = "matrix.det-row-add-multiple";
            step.rule_name = "Add a row multiple without changing the determinant";
            change.concrete_action = "Add " + scalar_text(coefficient.get()) + " times row " +
                std::to_string(addition.source + 1) + " to row " + std::to_string(addition.target + 1) +
                ". Keep the source row and determinant factor unchanged.";
            step.explanation_short = "Adding a multiple of a different row preserves the determinant.";
        }
        step.explanation_detailed = "The accumulated factor changes from " + scalar_text(factor.get()) + " to " +
            scalar_text(next_factor.get()) + ". Divide the current matrix determinant by this factor to retain the original value.";
        if (!running())
            return record;
        record.step = derivation.add_transformation(plan, std::move(step), std::move(change));
        if (record.step != kNoStep) {
            mpq_set(factor.get(), next_factor.get());
            factor_id = next_id;
        }
        return record;
    }

    bool refuse(MatrixOutcome outcome, std::string reason) {
        failed = true;
        failure = outcome;
        detail = std::move(reason);
        return false;
    }

    bool running() {
        if (failed)
            return false;
        if (arena.failed())
            return refuse(MatrixOutcome::ResourceExceeded, status_name(arena.status()));
        if (!meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? MatrixOutcome::Cancelled : MatrixOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        return true;
    }

    // The verdict rather than a bool. A checker that could not read one of the two matrices has not
    // caught the backend contradicting the trace, and a caller handed only false says it has.
    VerificationRecord equal(NodeId before, NodeId after) const {
        return verify_matrix_row(arena, before, after, MatrixRowScale{0, {1, 1}}).verification;
    }

    bool row(NodeId before, NodeId after, const MatrixRowOperation &operation) override {
        if (!running())
            return false;
        if (!meter.rewrite())
            return running();
        const VerificationRecord start = equal(current, before);
        if (start.outcome != VerificationOutcome::Passed)
            return refuse(start.outcome == VerificationOutcome::Inconclusive ? MatrixOutcome::UnsupportedForm
                                                                            : MatrixOutcome::VerificationFailed,
                          start.outcome == VerificationOutcome::Inconclusive
                              ? start.detail
                              : "a row event does not start at the last verified matrix");
        const MatrixRowRecord record = determinant ? determinant_row(before, after, operation) :
            record_matrix_row(arena, derivation, meter, before, after, operation, plan);
        if (!running())
            return false;
        if (record.check.verification.outcome != VerificationOutcome::Passed)
            return refuse(record.check.verification.outcome == VerificationOutcome::Inconclusive ?
                              MatrixOutcome::UnsupportedForm : MatrixOutcome::VerificationFailed,
                          record.check.verification.detail);
        if (record.check.changed && record.step == kNoStep)
            return refuse(MatrixOutcome::ResourceExceeded, "the verified row operation could not be recorded");
        current = after;
        return true;
    }

    MatrixResult finish(MatrixOutcome outcome, NodeId expression, std::string reason) {
        MatrixResult result;
        result.outcome = outcome;
        result.expression = outcome == MatrixOutcome::Reduced || outcome == MatrixOutcome::Determined ? expression : kNoNode;
        result.detail = std::move(reason);
        if (arena.failed()) {
            result.outcome = MatrixOutcome::ResourceExceeded;
            result.expression = kNoNode;
            result.detail = status_name(arena.status());
        }
        switch (result.outcome) {
            case MatrixOutcome::Reduced:
            case MatrixOutcome::Determined:
                result.status = derivation.outcome_from(mark);
                if (result.status != DerivationStatus::SolvedAndVerified) {
                    result.outcome = MatrixOutcome::VerificationFailed;
                    result.expression = kNoNode;
                }
                break;
            case MatrixOutcome::UnsupportedForm: result.status = DerivationStatus::Unsupported; break;
            case MatrixOutcome::InvalidInput: result.status = DerivationStatus::InvalidInput; break;
            case MatrixOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case MatrixOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case MatrixOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
            case MatrixOutcome::DependencyUnavailable: result.status = DerivationStatus::DependencyUnavailable; break;
        }
        if (result.outcome != MatrixOutcome::Reduced && result.outcome != MatrixOutcome::Determined)
            keep_verified_prefix(derivation, mark, arena);
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = determinant ? "matrix.det.rational" :
            form == MatrixForm::Echelon ? "matrix.ref.rational" : "matrix.rref.rational";
        context.requested_method = method();
        context.normalized_problem_model = input;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = print(arena, input);
        context.numeric_mode = derivation.request.numeric_mode;
        context.angle_convention = "not applicable";
        context.branch_convention = "exact rational row operations";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        if (determinant)
            context.resource_policy += " matrix-cell-bits<=" + std::to_string(kMatrixCellValueBits) +
                " determinant-factor-bits<=" + std::to_string(kMatrixDeterminantFactorBits);
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    MatrixResult stopped() { return finish(failure, kNoNode, detail); }
};

}

VerificationRecord verify_matrix_determinant_factor(const Arena &arena,
    const MatrixRowOperation &operation, NodeId before_factor, NodeId after_factor) {
    detail::Mpq before, after, multiplier;
    VerificationOutcome outcome = VerificationOutcome::Inconclusive;
    std::string detail = "factor certificates require exact nonzero rationals within the determinant factor limit";
    if (read_scalar(arena, before_factor, before.get()) && read_scalar(arena, after_factor, after.get())) {
        outcome = VerificationOutcome::Failed;
        if (mpq_sgn(before.get()) && mpq_sgn(after.get()) && row_multiplier(operation, multiplier.get())) {
            mpq_mul(before.get(), before.get(), multiplier.get());
            if (within_determinant_work_limit(before.get()) && mpq_equal(before.get(), after.get())) {
                outcome = VerificationOutcome::Passed;
                detail = "the nonzero determinant factor follows the exact elementary row multiplier";
            } else {
                detail = "the factor does not match the elementary row determinant multiplier";
            }
        }
    }
    return determinant_check("exact determinant factor law", "obl.matrix.det-factor", outcome, detail);
}

VerificationRecord verify_matrix_determinant_diagonal(const Arena &arena, NodeId matrix, NodeId product) {
    const auto form = verify_matrix_form(arena, matrix, MatrixForm::Echelon);
    detail::Mpq expected, actual;
    VerificationOutcome outcome = form.outcome;
    std::string detail = form.detail;
    if (outcome == VerificationOutcome::Passed) {
        outcome = VerificationOutcome::Inconclusive;
        detail = "a square exact matrix and bounded exact diagonal product are required";
        if (diagonal_product(arena, matrix, expected.get()) && read_scalar(arena, product, actual.get())) {
            outcome = mpq_equal(expected.get(), actual.get()) ? VerificationOutcome::Passed : VerificationOutcome::Failed;
            detail = outcome == VerificationOutcome::Passed ?
                "the scalar equals the GMP product of every diagonal entry in the square echelon matrix" :
                "the scalar differs from the product of the diagonal entries";
        }
    }
    return determinant_check("exact triangular determinant product", "obl.matrix.det-diagonal-product", outcome, detail);
}

VerificationRecord verify_matrix_determinant_correction(const Arena &arena,
    NodeId product, NodeId factor, NodeId answer) {
    detail::Mpq numerator, denominator, actual;
    VerificationOutcome outcome = VerificationOutcome::Inconclusive;
    std::string detail = "the product, factor and answer require exact rationals within the determinant work limit";
    if (read_scalar(arena, product, numerator.get()) && read_scalar(arena, factor, denominator.get()) &&
        read_scalar(arena, answer, actual.get())) {
        outcome = VerificationOutcome::Failed;
        detail = "the determinant factor must be nonzero";
        if (mpq_sgn(denominator.get())) {
            mpq_div(numerator.get(), numerator.get(), denominator.get());
            outcome = within_determinant_work_limit(numerator.get()) && mpq_equal(numerator.get(), actual.get()) ?
                VerificationOutcome::Passed : VerificationOutcome::Failed;
            detail = outcome == VerificationOutcome::Passed ?
                "the answer equals the diagonal product divided by the nonzero accumulated factor" :
                "the answer differs from the exact determinant correction";
        }
    }
    return determinant_check("exact determinant factor correction", "obl.matrix.det-correction", outcome, detail);
}

const char *matrix_outcome_name(MatrixOutcome outcome) {
    switch (outcome) {
        case MatrixOutcome::Reduced: return "reduced";
        case MatrixOutcome::Determined: return "determined";
        case MatrixOutcome::UnsupportedForm: return "unsupported form";
        case MatrixOutcome::InvalidInput: return "invalid input";
        case MatrixOutcome::VerificationFailed: return "verification failed";
        case MatrixOutcome::Cancelled: return "cancelled";
        case MatrixOutcome::ResourceExceeded: return "resource exceeded";
        case MatrixOutcome::DependencyUnavailable: return "dependency unavailable";
    }
    return "unknown";
}

static MatrixResult matrix_operation(Arena &arena, Adapter &adapter, Derivation &derivation,
                           NodeId matrix, MatrixForm form, const Budget &budget, bool determinant) {
    Run run(arena, derivation, matrix, form, budget, determinant);
    if (!run.running())
        return run.stopped();
    const auto input_view = MatrixView::from(arena, matrix);
    if (!input_view)
        return run.finish(MatrixOutcome::InvalidInput, kNoNode, "enter a nonempty rectangular matrix");
    if (determinant && (input_view->rows() != input_view->columns() || input_view->rows() > 4))
        return run.finish(MatrixOutcome::UnsupportedForm, kNoNode, "a determinant requires a square matrix of order 1 through 4");
    if (derivation.request.numeric_mode != NumericMode::Exact)
        return run.finish(MatrixOutcome::UnsupportedForm, kNoNode, "matrix walkthroughs require Exact mode");
    Request request;
    request.op = form == MatrixForm::Echelon ? Op::Ref : Op::Rref;
    request.target = matrix;
    std::string why;
    if (matrix_request_status(arena, request, &why) != ResultTag::Exact)
        return run.finish(MatrixOutcome::UnsupportedForm, kNoNode, why);
    if (determinant) {
        run.factor_id = arena.integer("1");
        if (!run.running())
            return run.stopped();
    }
    if (!run.meter.step() || !run.meter.backend_call()) {
        run.running();
        return run.stopped();
    }
    Step step;
    step.phase = "Row reduction";
    step.goal = std::string("Find ") + run.method() + "(" + print(arena, matrix) + ")";
    step.rule_id = determinant ? "plan.matrix-determinant" : "plan.matrix-method";
    step.rule_name = determinant ? "Reduce and track determinant factors" : "Reduce by reversible row operations";
    step.explanation_short = "Use row swaps, nonzero row scaling and row addition.";
    step.explanation_detailed = "Write the matrix after each operation. Keep every column, including any right-hand-side column.";
    if (determinant) {
        step.explanation_short = "Reduce to echelon form and track how each row operation changes the determinant.";
        step.explanation_detailed = "Start the determinant factor at 1. Multiply the final diagonal entries, then divide by the accumulated factor.";
    }
    step.backend_requests = 1;
    PlanPayload plan;
    plan.strategy_id = step.rule_id;
    plan.selected_strategy = step.rule_name;
    plan.matched_problem_facts.push_back(print(arena, matrix));
    plan.selection_rationale = "The matrix fits the exact rational row-reduction envelope.";
    register_strategy_precondition(plan, step, determinant ? "pre.matrix.square-rational-envelope" : "pre.matrix.rational-envelope",
        determinant ? "a square matrix of order 1 through 4 has exact rational cells and provenance" :
        "a nonempty matrix of at most 4 by 6 has exact rational cells and provenance",
        "exact matrix envelope validation", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed, step.goal);
    run.plan = derivation.add_plan(kNoStep, std::move(step), std::move(plan));
    const Response reply = adapter.matrix_steps(request, run);
    if (!run.running())
        return run.stopped();
    if (reply.tag != ResultTag::Exact || !reply.usable()) {
        const MatrixOutcome outcome = reply.tag == ResultTag::Cancelled ? MatrixOutcome::Cancelled :
            reply.tag == ResultTag::ResourceFailure || reply.tag == ResultTag::Timeout ?
            MatrixOutcome::ResourceExceeded : reply.tag == ResultTag::BackendError ?
            MatrixOutcome::DependencyUnavailable : reply.tag == ResultTag::MalformedResult ?
            MatrixOutcome::VerificationFailed : MatrixOutcome::UnsupportedForm;
        return run.finish(outcome, kNoNode, reply.detail.empty() ? tag_name(reply.tag) : reply.detail);
    }
    // Unreachable today, because giac_adapter.cc:707 rejects a reply read_exact_matrix cannot read
    // before this sees it. Written the same way as the row site anyway, so removing that filter
    // cannot quietly turn an unreadable final matrix into an accusation.
    const VerificationRecord endpoint = run.equal(run.current, reply.value);
    if (endpoint.outcome != VerificationOutcome::Passed)
        return run.finish(endpoint.outcome == VerificationOutcome::Inconclusive ? MatrixOutcome::UnsupportedForm
                                                                                : MatrixOutcome::VerificationFailed,
                          kNoNode,
                          endpoint.outcome == VerificationOutcome::Inconclusive
                              ? endpoint.detail
                              : "the final matrix is not the endpoint of the verified row trace");
    VerificationRecord form_check = verify_matrix_form(arena, reply.value, form);
    if (form_check.outcome != VerificationOutcome::Passed)
        return run.finish(form_check.outcome == VerificationOutcome::Inconclusive ? MatrixOutcome::UnsupportedForm :
                          MatrixOutcome::VerificationFailed, kNoNode, form_check.detail);
    if (determinant) {
        if (!run.meter.step()) {
            run.running();
            return run.stopped();
        }
        detail::Mpq product;
        if (!diagonal_product(arena, reply.value, product.get()))
            return run.finish(MatrixOutcome::UnsupportedForm, kNoNode, "the final diagonal is outside the exact determinant envelope");
        const NodeId product_id = scalar_node(arena, product.get());
        VerificationRecord diagonal = verify_matrix_determinant_diagonal(arena, reply.value, product_id);
        if (!run.running())
            return run.stopped();
        if (diagonal.outcome != VerificationOutcome::Passed)
            return run.finish(MatrixOutcome::VerificationFailed, kNoNode, diagonal.detail);
        Step diagonal_step;
        diagonal_step.phase = "Calculate the determinant";
        diagonal_step.goal = "Multiply the diagonal entries of the echelon matrix";
        diagonal_step.rule_id = "matrix.det-diagonal-product";
        diagonal_step.rule_name = "Multiply the triangular diagonal";
        diagonal_step.claim = ClaimType::EquivalentExpression;
        diagonal_step.explanation_short = "The determinant of a triangular matrix is its diagonal product.";
        diagonal_step.explanation_detailed = "Echelon form puts zeros below the main diagonal. Multiply every diagonal entry, including any zero, and retain the accumulated factor.";
        form_check.evidence_id = "obl.matrix.ref-form";
        diagonal_step.proof_obligations = {{"obl.matrix.trace-complete", "the final matrix ends the verified row trace"},
            {"obl.matrix.ref-form", "the square final matrix has exact row echelon form"},
            {"obl.matrix.det-diagonal-product", "the scalar equals the product of the triangular diagonal"}};
        diagonal_step.verifications = {{"exact row trace continuity", VerificationOutcome::Passed,
            EvidenceStrength::StructurallyValid, "every retained operation is contiguous and the endpoint matches the backend result",
            "obl.matrix.trace-complete"}, form_check, diagonal};
        TransformationPayload diagonal_change;
        diagonal_change.before = quotient(arena, arena.call("det", {reply.value}), run.factor_id);
        diagonal_change.after = quotient(arena, product_id, run.factor_id);
        if (!run.running())
            return run.stopped();
        diagonal_change.reversible = true;
        diagonal_change.concrete_action = "Multiply the diagonal entries: ";
        const auto view = MatrixView::from(arena, reply.value);
        for (size_t index = 0; index < view->rows(); ++index) {
            if (index) diagonal_change.concrete_action += ", ";
            diagonal_change.concrete_action += print(arena, view->cell(index, index));
        }
        diagonal_change.concrete_action += ". Keep the determinant factor in the denominator.";
        if (!run.running())
            return run.stopped();
        const NodeId correction_before = diagonal_change.after;
        derivation.add_transformation(run.plan, std::move(diagonal_step), std::move(diagonal_change));
        if (!run.running() || !run.meter.step()) {
            run.running();
            return run.stopped();
        }
        detail::Mpq answer;
        mpq_div(answer.get(), product.get(), run.factor.get());
        const NodeId answer_id = scalar_node(arena, answer.get());
        VerificationRecord correction = verify_matrix_determinant_correction(arena, product_id, run.factor_id, answer_id);
        if (!run.running())
            return run.stopped();
        if (correction.outcome != VerificationOutcome::Passed)
            return run.finish(MatrixOutcome::VerificationFailed, kNoNode, correction.detail);
        Step final_step;
        final_step.phase = "Recover the original determinant";
        final_step.goal = "Undo the accumulated determinant factor";
        final_step.rule_id = "matrix.det-correction";
        final_step.rule_name = "Correct for the row determinant factors";
        final_step.claim = ClaimType::EquivalentExpression;
        final_step.explanation_short = "Divide the diagonal product by the accumulated determinant factor.";
        final_step.explanation_detailed = "The current matrix determinant is the original determinant multiplied by " +
            scalar_text(run.factor.get()) + ". Division recovers the original value.";
        final_step.proof_obligations = {{"obl.matrix.det-correction", "the original determinant is the diagonal product divided by the accumulated nonzero factor"}};
        final_step.verifications = {correction};
        if (mpq_cmp_ui(run.factor.get(), 1, 1) == 0) {
            CheckPayload check;
            check.target_claim = "The diagonal product is the original determinant";
            check.check_method = correction.method;
            check.expected_relation = "the accumulated determinant factor is 1";
            check.observed_result = print(arena, answer_id);
            final_step.explanation_short = "The determinant factor is 1, so retain the diagonal product.";
            derivation.add_check(run.plan, std::move(final_step), std::move(check));
        } else {
            TransformationPayload change;
            change.before = correction_before;
            change.after = answer_id;
            change.reversible = true;
            change.concrete_action = "Divide " + scalar_text(product.get()) + " by " + scalar_text(run.factor.get()) + ".";
            derivation.add_transformation(run.plan, std::move(final_step), std::move(change));
        }
        return run.finish(MatrixOutcome::Determined, answer_id, "Every row determinant factor, the diagonal product and the final correction were verified.");
    }
    if (!run.meter.step()) {
        run.running();
        return run.stopped();
    }
    Step conclusion;
    conclusion.phase = "Check the final matrix";
    conclusion.goal = "Verify the recorded matrix and its requested row form";
    conclusion.rule_id = form == MatrixForm::Echelon ? "matrix.ref-conclusion" : "matrix.rref-conclusion";
    conclusion.rule_name = form == MatrixForm::Echelon ? "Check row echelon form" : "Check reduced row echelon form";
    conclusion.claim = ClaimType::RowEquivalent;
    conclusion.explanation_short = "Write the final matrix: " + print(arena, reply.value);
    conclusion.explanation_detailed = form_check.detail + ". Every recorded row operation was checked exactly.";
    conclusion.proof_obligations.push_back({"obl.matrix.trace-complete", "the final matrix ends a complete verified row-operation trace"});
    conclusion.verifications.push_back({"exact row trace continuity", VerificationOutcome::Passed,
        EvidenceStrength::StructurallyValid, "each row event starts at the preceding verified matrix and the result matches the endpoint",
        "obl.matrix.trace-complete"});
    form_check.evidence_id = form == MatrixForm::Echelon ? "obl.matrix.ref-form" : "obl.matrix.rref-form";
    conclusion.proof_obligations.push_back({form_check.evidence_id, "the final matrix satisfies the requested exact row form"});
    conclusion.verifications.push_back(form_check);
    CheckPayload check;
    check.target_claim = "The final matrix is row equivalent to the input and has the requested row form";
    check.check_method = form_check.method;
    check.expected_relation = form == MatrixForm::Echelon ? "row echelon form" : "reduced row echelon form";
    check.observed_result = print(arena, reply.value);
    derivation.add_check(run.plan, std::move(conclusion), std::move(check));
    return run.finish(MatrixOutcome::Reduced, reply.value, "Every row operation and the final matrix form were verified.");
}

MatrixResult matrix_method(Arena &arena, Adapter &adapter, Derivation &derivation,
                           NodeId matrix, MatrixForm form, const Budget &budget) {
    return matrix_operation(arena, adapter, derivation, matrix, form, budget, false);
}

MatrixResult matrix_determinant(Arena &arena, Adapter &adapter, Derivation &derivation,
                                NodeId matrix, const Budget &budget) {
    return matrix_operation(arena, adapter, derivation, matrix, MatrixForm::Echelon, budget, true);
}

}
