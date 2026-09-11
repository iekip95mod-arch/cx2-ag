#include "unit/adapter_tests.h"

#include <limits>

#include "nps/core/parser.h"
#include "nps/steps/matrix_row.h"
#include "nps/steps/schema.h"
#include "step_invariants.h"

namespace nps {
namespace {

NodeId parsed(Arena &arena, const char *expression) {
    return parse(arena, expression).root;
}

NodeId corrupt_cell(Arena &arena, NodeId matrix, size_t row, size_t column) {
    std::vector<NodeId> rows;
    const MatrixView view = *MatrixView::from(arena, matrix);
    for (size_t r = 0; r < view.rows(); ++r) {
        std::vector<NodeId> cells;
        for (size_t c = 0; c < view.columns(); ++c)
            cells.push_back(r == row && c == column ? arena.integer("999") : view.cell(r, c));
        rows.push_back(arena.list(cells));
    }
    return arena.list(rows);
}

}

void run_matrix_row_tests(TestSink &t) {
    for (bool deep : {false, true}) {
        Limits limits;
        limits.max_depth = 4096;
        Arena arena(limits);
        NodeId expression = arena.integer("0");
        for (size_t depth = 0; depth < (deep ? 2048 : 40); ++depth)
            expression = deep ? arena.unary(Kind::Neg, expression)
                              : arena.binary(Kind::Add, expression, expression);
        Rational value{99, 1};
        t.check(expression != kNoNode && read_matrix_rational(arena, expression, &value) &&
                    value.num == 0 && value.den == 1 && !arena.failed(),
                "exact matrix admission handles deep and shared arithmetic");
        for (NodeId invalid : {kNoNode, static_cast<NodeId>(arena.node_count())})
            t.check(!read_matrix_rational(arena, invalid, &value),
                    "matrix admission refuses invalid roots");
        for (NodeId refused : {arena.decimal("0.5"), arena.symbol("x"),
                               arena.nary(Kind::Neg, {}), arena.nary(Kind::Pow, {expression})}) {
            t.check(!read_matrix_rational(arena, arena.binary(Kind::Add, expression, refused), &value),
                    "matrix admission retains refusals behind shared arithmetic");
        }
        arena.mark_approximate(arena.integer("0"));
        t.check(!read_matrix_rational(arena, expression, &value),
                "matrix admission checks approximate provenance in shared descendants");
    }
    struct RowCase {
        const char *before;
        const char *after;
        MatrixRowOperation operation;
        MatrixRowOperation inverse;
    };
    const RowCase cases[] = {
        {"[[1,2,3],[-1,0,4],[5,6,7]]", "[[5,6,7],[-1,0,4],[1,2,3]]",
         MatrixRowSwap{0, 2}, MatrixRowSwap{0, 2}},
        {"[[1,0,2],[2/3,-4,0]]", "[[1,0,2],[-1,6,0]]",
         MatrixRowScale{1, {-3, 2}}, MatrixRowScale{1, {-2, 3}}},
        {"[[1,2,3],[2,4,8],[0,0,1]]", "[[1,2,3],[0,0,2],[0,0,1]]",
         MatrixRowAddMultiple{1, 0, {-2, 1}}, MatrixRowAddMultiple{1, 0, {2, 1}}},
        {"[[1,2,3],[2,4,6]]", "[[1,2,3],[0,0,0]]",
         MatrixRowAddMultiple{1, 0, {-2, 1}}, MatrixRowAddMultiple{1, 0, {2, 1}}},
        {"[[1/3,-2/5],[2/3,4/5]]", "[[4/3,4/5],[2/3,4/5]]",
         MatrixRowAddMultiple{0, 1, {3, 2}}, MatrixRowAddMultiple{0, 1, {-3, 2}}},
        {"[[0,0],[2,-3]]", "[[-1,3/2],[2,-3]]",
         MatrixRowAddMultiple{0, 1, {2, -4}}, MatrixRowAddMultiple{0, 1, {1, 2}}},
        {"[[2]]", "[[-2]]", MatrixRowScale{0, {-1, 1}}, MatrixRowScale{0, {-1, 1}}},
        {"[[1],[2],[3]]", "[[2],[1],[3]]", MatrixRowSwap{0, 1}, MatrixRowSwap{0, 1}},
        {"[[1,2,3,4,5,6],[7,8,9,10,11,12],[13,14,15,16,17,18],[2,4,6,8,10,12]]",
         "[[1,2,3,4,5,6],[7,8,9,10,11,12],[13,14,15,16,17,18],[1,2,3,4,5,6]]",
         MatrixRowScale{3, {1, 2}}, MatrixRowScale{3, {2, 1}}},
    };
    for (const RowCase &c : cases) {
        Arena arena;
        const NodeId before = parsed(arena, c.before);
        const NodeId after = parsed(arena, c.after);
        const size_t nodes = arena.node_count();
        const MatrixRowCheck check = verify_matrix_row(arena, before, after, c.operation);
        t.check(check.verification.outcome == VerificationOutcome::Passed && check.progress() &&
                    check.changed && check.verification.strength == EvidenceStrength::StructurallyValid,
                std::string("exact reversible row operation: ") + c.before);
        t.check(!check.verification.method.empty() && !check.verification.detail.empty() &&
                    arena.node_count() == nodes,
                "row checking records its method without modifying the input arena");
        t.check(verify_matrix_row(arena, after, before, c.inverse).progress(),
                "the independently supplied inverse restores the original matrix");
        {
            Derivation derivation;
            Meter meter(Budget{});
            const MatrixRowRecord record = record_matrix_row(arena, derivation, meter,
                                                              before, after, c.operation);
            t.check(record.step != kNoStep && record.check.progress() && derivation.size() == 1 &&
                        meter.steps() == 1 && arena.node_count() == nodes,
                    "a verified changing row event records one budgeted transformation");
            const Step &step = derivation.at(record.step);
            const TransformationPayload *change = derivation.transformation(record.step);
            t.check(step.claim == ClaimType::RowEquivalent && step.verified() && change &&
                        change->before == before && change->after == after && change->reversible,
                    "a row record preserves both matrices and claims row equivalence, not equality");
            t.check(change && !change->concrete_action.empty() && !step.explanation_short.empty() &&
                        !step.explanation_detailed.empty(),
                    "a row record supplies an action, the matrix to write and a reason");
            const RuleSchema *schema = rule_schema(step.rule_id);
            t.check(schema && schema->claim == ClaimType::RowEquivalent &&
                        schema->on_failure == FailureBehavior::WithholdResult &&
                        schema->obligation_count == 1 && step.proof_obligations.size() == 1 &&
                        schema->obligations[0].evidence_count == 1 &&
                        step.verifications.size() == 1 &&
                        step.proof_obligations[0].id == schema->obligations[0].id &&
                        step.verifications[0].evidence_id == schema->obligations[0].id &&
                        step.verifications[0].method == schema->obligations[0].evidence[0].method &&
                        step.verifications[0].strength == schema->obligations[0].evidence[0].strength,
                    "the row record discharges its declared obligation at the declared strength");
            invariants::Pass pass;
            std::vector<std::string> broken;
            pass.walk(arena, derivation, false, false, &broken);
            t.check(broken.empty() && pass.find("VER-016")->checked > 0,
                    "recorded row events satisfy the shared derivation invariants");
            const NodeId corrupted = corrupt_cell(arena, after, 0, 0);
            const MatrixRowRecord refused = record_matrix_row(arena, derivation, meter,
                                                               before, corrupted, c.operation);
            t.check(refused.step == kNoStep &&
                        refused.check.verification.outcome == VerificationOutcome::Failed &&
                        derivation.size() == 1 && meter.steps() == 1,
                    "a corrupted event cannot add a teaching step or spend a step budget");
        }
        const MatrixView view = *MatrixView::from(arena, after);
        for (size_t row = 0; row < view.rows(); ++row) {
            for (size_t column = 0; column < view.columns(); ++column) {
                const NodeId corrupted = corrupt_cell(arena, after, row, column);
                const MatrixRowCheck rejected = verify_matrix_row(arena, before, corrupted, c.operation);
                t.check(rejected.verification.outcome == VerificationOutcome::Failed &&
                            rejected.verification.strength == EvidenceStrength::Failed &&
                            !rejected.changed && !rejected.progress(),
                        "corruption of each changed or unchanged row cell fails the event proof");
            }
        }
    }
    struct NoChangeCase { const char *before; const char *after; MatrixRowOperation operation; };
    const NoChangeCase no_changes[] = {
        {"[[2]]", "[[2]]", MatrixRowScale{0, {1, 1}}},
        {"[[0,0]]", "[[0,0]]", MatrixRowScale{0, {-3, 2}}},
        {"[[1,2],[1,2]]", "[[1,2],[1,2]]", MatrixRowSwap{0, 1}},
        {"[[1],[2]]", "[[1],[2]]", MatrixRowAddMultiple{0, 1, {0, 1}}},
        {"[[1],[0]]", "[[1],[0]]", MatrixRowAddMultiple{0, 1, {3, 2}}},
        {"[[1/2]]", "[[2/4]]", MatrixRowScale{0, {1, 1}}},
    };
    for (const NoChangeCase &c : no_changes) {
        Arena arena;
        const NodeId before = parsed(arena, c.before);
        const NodeId after = parsed(arena, c.after);
        Derivation derivation;
        Meter meter(Budget{});
        const MatrixRowRecord record = record_matrix_row(arena, derivation, meter,
                                                          before, after, c.operation);
        t.check(record.step == kNoStep && !record.check.progress() &&
                    record.check.verification.outcome == VerificationOutcome::Passed &&
                    derivation.size() == 0 && meter.steps() == 0,
                "a verified identity never records or charges for a teaching transformation");
        const MatrixRowCheck check = verify_matrix_row(arena, before, after, c.operation);
        t.check(check.verification.outcome == VerificationOutcome::Passed &&
                    !check.changed && !check.progress(),
                "a verified row identity never supplies teaching progress");
        const MatrixView view = *MatrixView::from(arena, after);
        const NodeId corrupt = corrupt_cell(arena, after, view.rows() - 1, view.columns() - 1);
        t.check(verify_matrix_row(arena, before, corrupt, c.operation).verification.outcome ==
                    VerificationOutcome::Failed,
                "an identity event still rejects a changed output cell");
    }
    for (bool mark_after : {false, true}) {
        for (bool mark_row : {false, true}) {
            Arena arena;
            const NodeId before = parsed(arena, "[[1,2],[3,5]]");
            const NodeId after = parsed(arena, "[[1,2],[1,1]]");
            const MatrixRowOperation operation = MatrixRowAddMultiple{1, 0, {-2, 1}};
            t.check(verify_matrix_row(arena, before, after, operation).progress(),
                    "the exact control passes before only a container is marked approximate");
            const NodeId marked = mark_after ? after : before;
            arena.mark_approximate(mark_row ? arena.children(marked)[1] : marked);
            bool exact_cells = true;
            for (NodeId matrix : {before, after}) {
                const auto view = MatrixView::from(arena, matrix);
                t.check(view.has_value(), "container provenance leaves the shape-only matrix view valid");
                for (size_t row = 0; row < view->rows(); ++row) {
                    for (size_t column = 0; column < view->columns(); ++column) {
                        Rational value;
                        exact_cells = exact_cells && read_matrix_rational(arena, view->cell(row, column), &value);
                    }
                }
            }
            t.check(exact_cells, "the container-only fixture leaves every scalar cell exact");
            const MatrixRowCheck check = verify_matrix_row(arena, before, after, operation);
            t.check(check.verification.outcome == VerificationOutcome::Inconclusive &&
                        check.verification.strength == EvidenceStrength::Unsupported && !check.progress(),
                    "approximate before or after root and row identities cannot certify an exact row event");
            Derivation derivation;
            Meter meter(Budget{});
            const MatrixRowRecord record = record_matrix_row(arena, derivation, meter, before, after, operation);
            t.check(record.step == kNoStep && !record.check.progress() && derivation.size() == 0 && meter.steps() == 0,
                    "container-only approximate provenance cannot publish or charge a teaching step");
        }
    }
    const MatrixRowOperation invalid_events[] = {
        MatrixRowSwap{0, 0}, MatrixRowSwap{0, 2},
        MatrixRowScale{2, {1, 1}}, MatrixRowScale{0, {0, 1}}, MatrixRowScale{0, {1, 0}},
        MatrixRowAddMultiple{0, 0, {1, 1}}, MatrixRowAddMultiple{2, 0, {1, 1}},
        MatrixRowAddMultiple{0, 2, {1, 1}}, MatrixRowAddMultiple{0, 1, {1, 0}},
        MatrixRowSwap{std::numeric_limits<size_t>::max(), 1},
    };
    for (const MatrixRowOperation &operation : invalid_events) {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[0],[0]]");
        const MatrixRowCheck check = verify_matrix_row(arena, matrix, matrix, operation);
        t.check(check.verification.outcome == VerificationOutcome::Failed && !check.progress(),
                "invalid event metadata cannot pass because zero rows happen to stay equal");
    }
    for (const char *matrix : {"1", "[]", "[1]", "[[]]", "[[1],[2,3]]", "[[[1]]]"}) {
        Arena arena;
        const NodeId root = parsed(arena, matrix);
        t.check(verify_matrix_row(arena, root, root, MatrixRowScale{0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Failed,
                std::string("malformed row-operation matrix: ") + matrix);
    }
    for (const char *matrix : {"[[x]]", "[[1.0]]", "[[i]]", "[[sin(0)]]", "[[1=1]]",
                               "[[1/0]]", "[[9223372036854775808]]", "[[0*0.5]]",
                               "[[1],[2],[3],[4],[5]]", "[[1,2,3,4,5,6,7]]"}) {
        Arena arena;
        const NodeId root = parsed(arena, matrix);
        const MatrixRowCheck check = verify_matrix_row(arena, root, root, MatrixRowScale{0, {1, 1}});
        t.check(check.verification.outcome == VerificationOutcome::Inconclusive &&
                    check.verification.strength == EvidenceStrength::Unsupported && !check.progress(),
                std::string("unsupported row-operation arithmetic remains inconclusive: ") + matrix);
    }
    for (const char *after : {"[[1,2]]", "[[1],[2]]"}) {
        Arena arena;
        t.check(verify_matrix_row(arena, parsed(arena, "[[1]]"), parsed(arena, after),
                                  MatrixRowScale{0, {1, 1}}).verification.outcome == VerificationOutcome::Failed,
                "a row operation cannot change matrix dimensions");
    }
    {
        Arena arena;
        const NodeId before = parsed(arena, "[[1],[2]]");
        const NodeId after = parsed(arena, "[[1],[4]]");
        for (const MatrixRowOperation &operation : {
                 MatrixRowOperation{MatrixRowScale{1, {3, 1}}},
                 MatrixRowOperation{MatrixRowScale{0, {2, 1}}},
                 MatrixRowOperation{MatrixRowAddMultiple{1, 0, {3, 1}}}})
            t.check(verify_matrix_row(arena, before, after, operation).verification.outcome ==
                        VerificationOutcome::Failed, "wrong row or multiplier fails an otherwise valid result");
    }
    {
        Arena arena;
        const NodeId before = parsed(arena, "[[9223372036854775807],[-9223372036854775807]]");
        const NodeId after = parsed(arena, "[[9223372036854775807],[9223372036854775807]]");
        t.check(verify_matrix_row(arena, before, after, MatrixRowAddMultiple{1, 0, {2, 1}}).progress(),
                "GMP verifies a representable final cell after cancelling a wider product");
        t.check(verify_matrix_row(arena, before, before, MatrixRowScale{0, {2, 1}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "a scaled cell outside the checked rational range does not become a failed equality");
        t.check(verify_matrix_row(arena, after, after, MatrixRowAddMultiple{1, 0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "an added cell outside the checked rational range is inconclusive");
    }
    {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[1]]");
        t.check(verify_matrix_row(arena, matrix, matrix,
                                  MatrixRowScale{0, {1, std::numeric_limits<int64_t>::min()}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "a multiplier whose normalized denominator cannot fit is inconclusive");
        t.check(verify_matrix_row(arena, matrix, parsed(arena, "[[x]]"), MatrixRowScale{0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "an unsupported after-only cell cannot receive an exact certificate");
        t.check(verify_matrix_row(arena, matrix, parsed(arena, "[1]"), MatrixRowScale{0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Failed,
                "a malformed after-only matrix fails the row shape contract");
    }
    {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[1]]");
        arena.mark_approximate(MatrixView::from(arena, matrix)->cell(0, 0));
        t.check(verify_matrix_row(arena, matrix, matrix, MatrixRowScale{0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "approximate provenance cannot certify an exact row identity");
    }
    {
        Limits limits;
        limits.max_nodes = 3;
        Arena arena(limits);
        const NodeId matrix = parsed(arena, "[[1]]");
        arena.integer("2");
        t.check(verify_matrix_row(arena, matrix, matrix, MatrixRowScale{0, {1, 1}})
                    .verification.outcome == VerificationOutcome::Inconclusive,
                "a failed arena never supplies a row certificate");
        Derivation derivation;
        Meter meter(Budget{});
        t.check(record_matrix_row(arena, derivation, meter, matrix, matrix,
                                  MatrixRowScale{0, {1, 1}}).step == kNoStep &&
                    derivation.size() == 0 && meter.steps() == 0,
                "a failed arena cannot publish a row transformation");
    }
    {
        t.check(static_cast<unsigned>(ClaimType::EquivalentExpression) == 0 &&
                    static_cast<unsigned>(ClaimType::NoClaim) == 5 &&
                    static_cast<unsigned>(ClaimType::RowEquivalent) == 6 &&
                    std::string(claim_type_name(ClaimType::RowEquivalent)) == "row equivalent",
                "row equivalence appends its claim and has the public wire spelling");
        Arena arena;
        const NodeId before = parsed(arena, "[[1,2],[3,4]]");
        const NodeId after = parsed(arena, "[[1,2],[0,-2]]");
        const MatrixRowOperation operation = MatrixRowAddMultiple{1, 0, {-3, 1}};
        Derivation derivation;
        Meter meter(Budget{});
        const MatrixRowRecord record = record_matrix_row(arena, derivation, meter, before, after, operation);
        const Step original = derivation.at(record.step);
        const TransformationPayload change = *derivation.transformation(record.step);
        t.check(change.concrete_action == "Add -3 times row 1 to row 2. Keep row 1 unchanged." &&
                    original.rule_id == "matrix.row-add-multiple",
                "row addition names the actual multiplier, source and target for the student");
        for (unsigned mutation = 0; mutation < 5; ++mutation) {
            Step altered = original;
            if (mutation == 0) altered.claim = ClaimType::EquivalentExpression;
            if (mutation == 1) altered.proof_obligations.clear();
            if (mutation == 2) altered.verifications.clear();
            if (mutation == 3) altered.verifications[0].outcome = VerificationOutcome::Failed;
            if (mutation == 4) altered.verifications[0].strength = EvidenceStrength::NumericallyCorroborated;
            Derivation corrupted;
            corrupted.add_transformation(kNoStep, std::move(altered), change);
            invariants::Pass pass;
            std::vector<std::string> broken;
            pass.walk(arena, corrupted, false, false, &broken);
            t.check(!broken.empty() && pass.find("VER-016")->broken > 0,
                    "row schema rejects altered claims, missing evidence and incorrect verification");
        }
        Budget limited;
        limited.max_steps = 0;
        Meter exhausted(limited);
        Derivation withheld;
        const MatrixRowRecord no_room = record_matrix_row(arena, withheld, exhausted,
                                                           before, after, operation);
        t.check(no_room.step == kNoStep && no_room.check.progress() && withheld.size() == 0 &&
                    exhausted.halt() == Halt::StepLimit,
                "a passed mathematical check does not override the recording budget");
        bool cancel = false;
        Budget cancellable;
        cancellable.poll = [](void *flag) { return *static_cast<bool *>(flag); };
        cancellable.poll_context = &cancel;
        Meter stopped(cancellable);
        cancel = true;
        const MatrixRowRecord cancelled = record_matrix_row(arena, withheld, stopped,
                                                             before, after, operation);
        t.check(cancelled.step == kNoStep && withheld.size() == 0 && stopped.steps() == 0 &&
                    stopped.halt() == Halt::Cancelled &&
                    cancelled.check.verification.outcome == VerificationOutcome::NotAttempted,
                "cancellation is polled before checking or publishing the next row event");
        Meter available(Budget{});
        t.check(record_matrix_row(arena, withheld, available, before, after, operation, 42).step == kNoStep &&
                    withheld.size() == 0 && available.steps() == 0,
                "an absent parent cannot receive a row record");
        const StepId parent = withheld.add_plan(kNoStep, Step{}, PlanPayload{});
        const MatrixRowRecord child = record_matrix_row(arena, withheld, available,
                                                        before, after, operation, parent);
        t.check(child.step != kNoStep && withheld.at(child.step).parent == parent &&
                    withheld.at(parent).children == std::vector<StepId>{child.step},
                "a row record joins its supplied plan without adding an extra root");
    }
}

}
