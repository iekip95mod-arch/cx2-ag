#include "unit/adapter_tests.h"
#include "golden/golden.h"

#include "nps/cas/matrix_events.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/matrix.h"
#include "nps/steps/matrix_determinant.h"
#include "nps/steps/matrix_row.h"
#include "nps/steps/schema.h"
#include "step_invariants.h"

namespace nps {
namespace {

struct Event {
    const char *before;
    const char *after;
    MatrixRowOperation operation;
};

class RecordedBackend final : public Backend {
  public:
    std::vector<Event> events;
    std::string answer;
    ResultTag tag = ResultTag::Exact;
    Derivation *derivation = nullptr;
    bool *cancel = nullptr;
    size_t cancel_after = 0;
    bool ignore_stop = false;
    bool plan_before_call = false;
    size_t calls = 0;
    size_t delivered = 0;
    size_t plain_calls = 0;

    bool eval(const std::string &, std::string *, std::string *) override {
        ++plain_calls;
        return false;
    }

    bool matrix_steps(const Request &, Arena &arena, MatrixRowSink &sink, TypedResult *out) override {
        ++calls;
        plan_before_call = derivation && derivation->size() > 0 &&
            derivation->at(0).rule_id == "plan.matrix-method" && derivation->at(0).verified();
        for (const Event &event : events) {
            const NodeId before = parse(arena, event.before).root;
            const NodeId after = parse(arena, event.after).root;
            ++delivered;
            const bool accepted = sink.row(before, after, event.operation);
            if (cancel && delivered == cancel_after)
                *cancel = true;
            if (!accepted && !ignore_stop) {
                out->tag = ResultTag::UnsupportedOperation;
                return true;
            }
        }
        out->tag = tag;
        out->shape = ResultShape::Matrix;
        out->value = parse(arena, answer).root;
        return true;
    }
};

std::vector<Event> reduction() {
    return {
        {"[[2,4,6],[0,3,6]]", "[[1,2,3],[0,3,6]]", MatrixRowScale{0, {1, 2}}},
        {"[[1,2,3],[0,3,6]]", "[[1,2,3],[0,1,2]]", MatrixRowScale{1, {1, 3}}},
        {"[[1,2,3],[0,1,2]]", "[[1,0,-1],[0,1,2]]", MatrixRowAddMultiple{0, 1, {-2, 1}}},
    };
}

bool cancelled(void *context) { return *static_cast<bool *>(context); }

size_t transformations(const Derivation &derivation) {
    size_t count = 0;
    for (size_t index = 0; index < derivation.size(); ++index)
        count += derivation.at(static_cast<StepId>(index)).kind == StepKind::Transformation;
    return count;
}

}

void run_determinant_tests(TestSink &t);

void run_matrix_tests(TestSink &t) {
    struct MatrixCase {
        const char *fixture;
        const char *input;
        const char *answer;
        MatrixForm form;
        std::vector<Event> events;
    };
    auto ref_events = reduction();
    ref_events.pop_back();
    const std::vector<MatrixCase> cases = {
        {"matrix_ref_scaled", "[[2,4,6],[0,3,6]]", "[[1,2,3],[0,1,2]]", MatrixForm::Echelon, ref_events},
        {"matrix_rref_scaled", "[[2,4,6],[0,3,6]]", "[[1,0,-1],[0,1,2]]", MatrixForm::ReducedEchelon, reduction()},
        {"matrix_rref_swap", "[[0,1,2],[1,0,3]]", "[[1,0,3],[0,1,2]]", MatrixForm::ReducedEchelon,
         {{"[[0,1,2],[1,0,3]]", "[[1,0,3],[0,1,2]]", MatrixRowSwap{0, 1}}}},
        {"matrix_ref_swap", "[[0,1,2],[1,0,3]]", "[[1,0,3],[0,1,2]]", MatrixForm::Echelon,
         {{"[[0,1,2],[1,0,3]]", "[[1,0,3],[0,1,2]]", MatrixRowSwap{0, 1}}}},
        {"matrix_ref_dependent", "[[1,2,3],[2,4,6]]", "[[1,2,3],[0,0,0]]", MatrixForm::Echelon,
         {{"[[1,2,3],[2,4,6]]", "[[1,2,3],[0,0,0]]", MatrixRowAddMultiple{1, 0, {-2, 1}}}}},
        {"matrix_rref_fraction", "[[-1/2,1]]", "[[1,-2]]", MatrixForm::ReducedEchelon,
         {{"[[-1/2,1]]", "[[1,-2]]", MatrixRowScale{0, {-2, 1}}}}},
        {"matrix_rref_identity", "[[1,0],[0,1]]", "[[1,0],[0,1]]", MatrixForm::ReducedEchelon, {}},
        {"matrix_ref_zero", "[[0,0],[0,0]]", "[[0,0],[0,0]]", MatrixForm::Echelon, {}},
        {"", "[[0,0]]", "[[0,0]]", MatrixForm::ReducedEchelon, {}},
        {"", "[[2,3],[0,0]]", "[[2,3],[0,0]]", MatrixForm::Echelon, {}},
        {"", "[[1]]", "[[1]]", MatrixForm::ReducedEchelon,
         {{"[[1]]", "[[1]]", MatrixRowScale{0, {1, 1}}}}},
    };
    for (const MatrixCase &c : cases) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = std::string(c.form == MatrixForm::Echelon ? "ref(" : "rref(") + c.input + ")";
        RecordedBackend backend;
        backend.answer = c.answer;
        backend.events = c.events;
        backend.derivation = &derivation;
        Adapter adapter(arena, backend);
        const NodeId input = parse(arena, c.input).root;
        const MatrixResult result = matrix_method(arena, adapter, derivation, input, c.form);
        t.check(result.outcome == MatrixOutcome::Reduced && result.status == DerivationStatus::SolvedAndVerified &&
                    verify_matrix_row(arena, parse(arena, c.answer).root, result.expression,
                                      MatrixRowScale{0, {1, 1}}).verification.outcome == VerificationOutcome::Passed,
                std::string("matrix method reaches the independently expected final matrix: ") + c.input);
        t.check(backend.plan_before_call && backend.calls == 1 && backend.plain_calls == 0 &&
                    adapter.call_count() == 1 && result.cost.backend_calls == 1,
                "the verified plan precedes one traced backend call without eval fallback");
        size_t changing_events = 0;
        for (const Event &event : c.events)
            changing_events += std::string(event.before) != event.after;
        t.check(transformations(derivation) == changing_events && derivation.size() == changing_events + 2 &&
                    result.cost.steps == derivation.size(),
                "identity rows and already reduced matrices receive no fabricated transformations");
        t.check(derivation.all_verified_from(0) && derivation.context.derivation_status == result.status &&
                    derivation.context.normalized_problem_model == input &&
                    derivation.context.original_expression == derivation.request.original_expression &&
                    derivation.context.problem_family_envelope_version == "1" &&
                    derivation.context.problem_family_id == (c.form == MatrixForm::Echelon ? "matrix.ref.rational" : "matrix.rref.rational"),
                "matrix context retains family, envelope, model, original request and final status");
        const Step &last = derivation.at(static_cast<StepId>(derivation.size() - 1));
        t.check(last.kind == StepKind::Check && last.claim == ClaimType::RowEquivalent &&
                    last.verifications.size() == 2 && last.proof_obligations.size() == 2 &&
                    derivation.check(last.id) && !derivation.check(last.id)->observed_result.empty(),
                "the final check proves trace completion and row form separately");
        invariants::Pass pass;
        std::vector<std::string> failures;
        pass.walk(arena, derivation, false, false, &failures);
        t.check(failures.empty(), "matrix plans, row events and conclusions satisfy shared schema invariants");
        for (size_t index = 0; index < derivation.size(); ++index) {
            const Step &record = derivation.at(static_cast<StepId>(index));
            const RuleSchema *schema = rule_schema(record.rule_id);
            const PlanPayload *strategy = derivation.plan(record.id);
            t.check(schema && schema->on_failure == FailureBehavior::WithholdResult &&
                        schema->claim == record.claim && schema->obligation_count ==
                            record.proof_obligations.size() + (strategy ? strategy->preconditions.size() : 0),
                    "every matrix record declares its claim and withholding obligations");
        }
        if (*c.fixture != '\0') {
            const std::string record = "problem: " + derivation.request.original_expression +
                "\noutcome: " + matrix_outcome_name(result.outcome) + "\nresult: " + print(arena, result.expression) +
                "\ndetail: " + result.detail + "\n" + render_derivation(arena, derivation);
            check_golden(t, c.fixture, record);
        }
    }
    for (size_t defect = 0; defect < 4; ++defect) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = reduction();
        backend.answer = "[[1,0,-1],[0,1,2]]";
        backend.ignore_stop = true;
        if (defect == 0) backend.events[1].after = "[[1,2,3],[0,1,9]]";
        if (defect == 1) backend.events.erase(backend.events.begin() + 1);
        if (defect == 2) backend.events.pop_back();
        if (defect == 3) backend.events[1].before = "[[1,2,8],[0,3,6]]";
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation,
            parse(arena, "[[2,4,6],[0,3,6]]").root, MatrixForm::ReducedEchelon);
        t.check(result.outcome == MatrixOutcome::VerificationFailed && result.expression == kNoNode &&
                    result.status == DerivationStatus::VerificationFailed && !result.detail.empty(),
                "corrupt, missing, skipped or disconnected events cannot yield a matrix answer");
        t.check(transformations(derivation) == (defect == 2 ? 2 : 1) && derivation.all_verified_from(0),
                "a rejected trace preserves only its verified changing prefix even if the producer ignores stop");
    }
    {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.answer = "[[1,2],[0,1]]";
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation,
            parse(arena, backend.answer).root, MatrixForm::ReducedEchelon);
        t.check(result.outcome == MatrixOutcome::VerificationFailed && result.expression == kNoNode && derivation.size() == 0,
                "a continuous identity trace still requires the requested final row form");
        check_golden(t, "matrix_rref_form_refused", "outcome: " + std::string(matrix_outcome_name(result.outcome)) +
            "\ndetail: " + result.detail + "\n" + render_derivation(arena, derivation));
    }
    for (size_t cancel_after : {size_t{0}, size_t{1}, size_t{3}}) {
        Arena arena;
        Derivation derivation;
        bool cancel = cancel_after == 0;
        RecordedBackend backend;
        backend.events = reduction();
        backend.answer = "[[1,0,-1],[0,1,2]]";
        backend.cancel = &cancel;
        backend.cancel_after = cancel_after;
        Budget budget;
        budget.poll = cancelled;
        budget.poll_context = &cancel;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation,
            parse(arena, "[[2,4,6],[0,3,6]]").root, MatrixForm::ReducedEchelon, budget);
        t.check(result.outcome == MatrixOutcome::Cancelled && result.status == DerivationStatus::Cancelled &&
                    result.expression == kNoNode && transformations(derivation) == cancel_after &&
                    backend.calls == (cancel_after == 0 ? 0 : 1),
                "cancellation before, during and after a trace withholds the answer and retains verified rows");
    }
    for (size_t limit = 0; limit < 4; ++limit) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = reduction();
        backend.answer = "[[1,0,-1],[0,1,2]]";
        Budget budget;
        if (limit == 0) budget.max_backend_calls = 0;
        if (limit == 1) budget.max_steps = 0;
        if (limit == 2) budget.max_steps = 2;
        if (limit == 3) budget.max_rewrites = 0;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation,
            parse(arena, "[[2,4,6],[0,3,6]]").root, MatrixForm::ReducedEchelon, budget);
        t.check(result.outcome == MatrixOutcome::ResourceExceeded && result.expression == kNoNode &&
                    result.status == DerivationStatus::ResourceLimitReached &&
                    transformations(derivation) == (limit == 2 ? 1 : 0) &&
                    backend.calls == (limit < 2 ? 0 : 1),
                "shared backend, step and rewrite limits stop matrix work at the recorded prefix");
    }
    for (const char *input : {"[]", "[1]", "[[x]]", "[[1.0]]", "[[1/0]]", "[[1],[0],[0],[0],[0]]", "[[1,0,0,0,0,0,0]]"}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation, parse(arena, input).root, MatrixForm::Echelon);
        t.check((result.outcome == MatrixOutcome::InvalidInput || result.outcome == MatrixOutcome::UnsupportedForm) &&
                    result.expression == kNoNode && backend.calls == 0 && derivation.size() == 0,
                std::string("matrix walkthrough input refusal precedes backend work: ") + input);
    }
    for (size_t node_limit : {size_t{3}, size_t{12}}) {
        Limits limits;
        limits.max_nodes = node_limit;
        Arena arena(limits);
        Derivation derivation;
        RecordedBackend backend;
        backend.events = reduction();
        backend.answer = "[[1,0,-1],[0,1,2]]";
        const NodeId input = parse(arena, "[[2,4,6],[0,3,6]]").root;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation, input, MatrixForm::ReducedEchelon);
        t.check(arena.failed() && result.outcome == MatrixOutcome::ResourceExceeded && result.expression == kNoNode &&
                    derivation.size() == 0 && backend.calls == (node_limit == 3 ? 0 : 1),
                "Arena failure before or during tracing discards records whose storage is no longer trustworthy");
    }
    {
        Arena arena;
        Derivation derivation;
        derivation.request.numeric_mode = NumericMode::Decimal;
        RecordedBackend backend;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation, parse(arena, "[[1]]").root, MatrixForm::Echelon);
        t.check(result.outcome == MatrixOutcome::UnsupportedForm && backend.calls == 0,
                "matrix walkthroughs require Exact numeric mode");
    }
    for (size_t prefix : {size_t{0}, size_t{1}, size_t{3}}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = reduction();
        backend.events.resize(prefix);
        backend.answer = "[[1,0,-1],[0,1,2]]";
        backend.tag = ResultTag::Cancelled;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation,
            parse(arena, "[[2,4,6],[0,3,6]]").root, MatrixForm::ReducedEchelon);
        t.check(result.outcome == MatrixOutcome::Cancelled && result.status == DerivationStatus::Cancelled &&
                    result.expression == kNoNode && transformations(derivation) == prefix && backend.calls == 1,
                "typed backend cancellation preserves verified rows without requiring a budget cancel flag");
    }
    t.equal(tag_name(ResultTag::Cancelled), "cancelled", "typed cancellation has a distinct result name");
    for (ResultTag tag : {ResultTag::Approximate, ResultTag::UnsupportedOperation, ResultTag::Unevaluated, ResultTag::ResourceFailure,
                         ResultTag::Timeout, ResultTag::BackendError}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.answer = "[[1]]";
        backend.tag = tag;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation, parse(arena, "[[1]]").root, MatrixForm::Echelon);
        const MatrixOutcome expected = tag == ResultTag::Approximate || tag == ResultTag::UnsupportedOperation || tag == ResultTag::Unevaluated ?
            MatrixOutcome::UnsupportedForm :
            tag == ResultTag::BackendError ? MatrixOutcome::DependencyUnavailable : MatrixOutcome::ResourceExceeded;
        t.check(result.outcome == expected && result.expression == kNoNode && derivation.size() == 0,
                "backend approximation, resource failure and unavailable dependency remain distinct");
    }
    // A row event the checker cannot read, against one it can read and that genuinely contradicts
    // the trace. The two came out identical before the repair, so the second row is the control
    // that keeps the first one honest.
    {
        struct Start {
            const char *before;
            MatrixOutcome outcome;
            DerivationStatus status;
            const char *detail;
        };
        const Start starts[] = {
            {"[[2.0,4,6],[0,3,6]]", MatrixOutcome::UnsupportedForm, DerivationStatus::Unsupported, nullptr},
            {"[[9,9,9],[9,9,9]]", MatrixOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
             "a row event does not start at the last verified matrix"},
        };
        for (const Start &start : starts) {
            Arena arena;
            Derivation derivation;
            RecordedBackend backend;
            backend.events = {{start.before, "[[1,2,3],[0,3,6]]", MatrixRowScale{0, {1, 2}}}};
            backend.answer = "[[1,0,-1],[0,1,2]]";
            backend.ignore_stop = true;
            Adapter adapter(arena, backend);
            const MatrixResult result = matrix_method(arena, adapter, derivation,
                parse(arena, "[[2,4,6],[0,3,6]]").root, MatrixForm::ReducedEchelon);
            t.check(result.outcome == start.outcome && result.status == start.status &&
                        result.expression == kNoNode,
                    std::string("a row event the checker could not read is not the backend "
                                "contradicting the trace: ") + start.before + " gave " +
                        matrix_outcome_name(result.outcome));
            if (start.detail != nullptr)
                t.equal(result.detail, start.detail,
                        "and a readable row event that really does disagree keeps its accusation");
            else
                t.check(result.detail != std::string("a row event does not start at the last "
                                                     "verified matrix"),
                        "and it does not borrow the disagreement's wording either");
        }
    }
    {
        class PlainBackend final : public Backend {
          public:
            size_t calls = 0;
            bool eval(const std::string &, std::string *, std::string *) override { ++calls; return true; }
        } backend;
        Arena arena;
        Derivation derivation;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_method(arena, adapter, derivation, parse(arena, "[[1]]").root, MatrixForm::Echelon);
        t.check(result.outcome == MatrixOutcome::DependencyUnavailable && result.status == DerivationStatus::DependencyUnavailable &&
                    result.expression == kNoNode && backend.calls == 0,
                "a backend without row events cannot fall back to an unrecorded evaluation");
    }
    run_determinant_tests(t);
}

void run_determinant_tests(TestSink &t) {
    struct Fixture {
        const char *input;
        const char *echelon;
        const char *answer;
        std::vector<Event> events;
        size_t progress;
        const char *golden = nullptr;
        bool corrected = false;
    };
    const Fixture fixtures[] = {
        {"[[5]]", "[[1]]", "5", {{"[[5]]", "[[1]]", MatrixRowScale{0, {1, 5}}}}, 1, "matrix_det_scalar", true},
        {"[[-3/2]]", "[[1]]", "-3/2", {{"[[-3/2]]", "[[1]]", MatrixRowScale{0, {-2, 3}}}}, 1, nullptr, true},
        {"[[0,2],[3,4]]", "[[3,4],[0,2]]", "-6",
         {{"[[0,2],[3,4]]", "[[3,4],[0,2]]", MatrixRowSwap{0, 1}}}, 1, "matrix_det_swap", true},
        {"[[1/2,1/3],[0,1/4]]", "[[1/2,1/3],[0,1/4]]", "1/8", {}, 0, "matrix_det_fraction"},
        {"[[1,2],[2,4]]", "[[1,2],[0,0]]", "0",
         {{"[[1,2],[2,4]]", "[[1,2],[0,0]]", MatrixRowAddMultiple{1, 0, {-2, 1}}}}, 1, "matrix_det_singular"},
        {"[[1,0],[0,1]]", "[[1,0],[0,1]]", "1", {}, 0, "matrix_det_identity"},
        {"[[0]]", "[[0]]", "0", {{"[[0]]", "[[0]]", MatrixRowScale{0, {-7, 3}}}}, 0},
        {"[[0,0],[0,0]]", "[[0,0],[0,0]]", "0",
         {{"[[0,0],[0,0]]", "[[0,0],[0,0]]", MatrixRowSwap{0, 1}},
          {"[[0,0],[0,0]]", "[[0,0],[0,0]]", MatrixRowAddMultiple{0, 1, {3, 1}}}}, 0},
        {"[[1,2],[1,2]]", "[[1,2],[0,0]]", "0",
         {{"[[1,2],[1,2]]", "[[1,2],[1,2]]", MatrixRowSwap{0, 1}},
          {"[[1,2],[1,2]]", "[[1,2],[0,0]]", MatrixRowAddMultiple{1, 0, {-1, 1}}}}, 1},
        {"[[3037000500,0],[0,3037000500]]", "[[1,0],[0,1]]", "9223372037000250000",
         {{"[[3037000500,0],[0,3037000500]]", "[[1,0],[0,3037000500]]", MatrixRowScale{0, {1, 3037000500}}},
          {"[[1,0],[0,3037000500]]", "[[1,0],[0,1]]", MatrixRowScale{1, {1, 3037000500}}}}, 2, "matrix_det_large", true},
        {"[[2,0,0,0],[0,-3,0,0],[0,0,4,0],[0,0,0,5]]", "[[2,0,0,0],[0,-3,0,0],[0,0,4,0],[0,0,0,5]]", "-120", {}, 0},
    };
    for (const Fixture &fixture : fixtures) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = std::string("det(") + fixture.input + ")";
        RecordedBackend backend;
        backend.events = fixture.events;
        backend.answer = fixture.echelon;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, fixture.input).root);
        t.check(result.outcome == MatrixOutcome::Determined && result.status == DerivationStatus::SolvedAndVerified,
                std::string("determinant succeeds with verified trace: ") + fixture.input);
        t.check(verify_matrix_determinant_correction(arena, parse(arena, fixture.answer).root,
                    arena.integer("1"), result.expression).outcome == VerificationOutcome::Passed,
                std::string("determinant has independently expected exact scalar: ") + fixture.answer);
        t.check(backend.calls == 1 && backend.plain_calls == 0, "determinant uses one row stream and no answer-only evaluation");
        const bool correction_changes = derivation.transformation(static_cast<StepId>(derivation.size() - 1)) != nullptr;
        t.check(derivation.size() == fixture.progress + 3 &&
                correction_changes == fixture.corrected &&
                transformations(derivation) == fixture.progress + 1 + fixture.corrected,
                "determinant records only changing row events, diagonal product and correction");
        if (!fixture.corrected) {
            const auto *check = derivation.check(static_cast<StepId>(derivation.size() - 1));
            t.check(check && check->expected_relation == "the accumulated determinant factor is 1",
                    "discarded identity row events leave the displayed determinant ledger unchanged at one");
        }
        t.check(derivation.context.problem_family_id == "matrix.det.rational" &&
                derivation.context.requested_method == "det" &&
                derivation.context.resource_policy.find("determinant-bits<=4096") != std::string::npos,
                "determinant context identifies family, method and bounded GMP policy");
        invariants::Pass pass;
        std::vector<std::string> failures;
        pass.walk(arena, derivation, false, false, &failures);
        t.check(failures.empty(), "determinant records satisfy teaching and verification invariants");
        for (size_t index = 0; index < derivation.size(); ++index) {
            const Step &step = derivation.at(static_cast<StepId>(index));
            const RuleSchema *schema = rule_schema(step.rule_id);
            const PlanPayload *strategy = derivation.plan(step.id);
            t.check(schema && schema->claim == step.claim && schema->obligation_count ==
                    step.proof_obligations.size() + (strategy ? strategy->preconditions.size() : 0),
                    "determinant rule claims and obligations match registered schema");
        }
        if (fixture.golden)
            check_golden(t, fixture.golden, render_derivation(arena, derivation));
    }
    {
        Arena arena;
        const auto scalar = [&](const char *text) { return parse(arena, text).root; };
        const auto passed = [](const VerificationRecord &record) { return record.outcome == VerificationOutcome::Passed; };
        t.check(passed(verify_matrix_determinant_factor(arena, MatrixRowSwap{0, 1}, scalar("2/3"), scalar("-2/3"))),
                "row swap negates the determinant factor");
        t.check(!passed(verify_matrix_determinant_factor(arena, MatrixRowSwap{0, 1}, scalar("2/3"), scalar("2/3"))),
                "missing swap sign is rejected by production ledger checker");
        t.check(passed(verify_matrix_determinant_factor(arena, MatrixRowScale{0, {-2, 3}}, scalar("3/5"), scalar("-2/5"))),
                "row scale multiplies the factor exactly");
        t.check(!passed(verify_matrix_determinant_factor(arena, MatrixRowScale{0, {2, 3}}, scalar("1"), scalar("3/2"))),
                "inverse scale mutation is rejected by production ledger checker");
        t.check(!passed(verify_matrix_determinant_factor(arena, MatrixRowAddMultiple{1, 0, {3, 1}}, scalar("1"), scalar("3"))),
                "row addition cannot multiply the determinant factor even when final determinant is zero");
        t.check(!passed(verify_matrix_determinant_factor(arena, MatrixRowScale{0, {0, 1}}, scalar("1"), scalar("0"))) &&
                !passed(verify_matrix_determinant_factor(arena, MatrixRowSwap{0, 1}, scalar("0"), scalar("0"))),
                "zero scale and zero determinant factor cannot form certificates");
        t.check(passed(verify_matrix_determinant_diagonal(arena, scalar("[[3037000500,0],[0,3037000500]]"), scalar("9223372037000250000"))),
                "diagonal product checker keeps integers beyond int64");
        t.check(!passed(verify_matrix_determinant_diagonal(arena, scalar("[[1,2],[3,4]]"), scalar("4"))) &&
                !passed(verify_matrix_determinant_diagonal(arena, scalar("[[1,0],[0,2]]"), scalar("3"))),
                "diagonal product requires row form and the actual product");
        t.check(passed(verify_matrix_determinant_correction(arena, scalar("1"), scalar("1/9223372037000250000"), scalar("9223372037000250000"))) &&
                !passed(verify_matrix_determinant_correction(arena, scalar("1"), scalar("1/2"), scalar("1/2"))),
                "correction divides by the GMP ledger and rejects multiplication mutation");
        t.check(!passed(verify_matrix_determinant_correction(arena, scalar("0"), scalar("0"), scalar("0"))) &&
                !passed(verify_matrix_determinant_correction(arena, scalar("1.0"), scalar("1"), scalar("1"))),
                "zero divisor and approximate certificate values are refused");
    }
    for (const char *input : {"[]", "[1,2]", "[[1,2]]", "[[1],[2]]", "[[1,2],[3]]", "[[[1]]]",
                              "[[x]]", "[[1.0]]", "[[i]]", "[[1/0]]", "[[1,0,0,0,0],[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0],[0,0,0,0,1]]"}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, input).root);
        t.check(result.expression == kNoNode && backend.calls == 0 && derivation.size() == 0,
                std::string("determinant refuses before backend: ") + input);
    }
    for (size_t maximum : {size_t{0}, size_t{1}, size_t{2}, size_t{3}}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = {{"[[2]]", "[[1]]", MatrixRowScale{0, {1, 2}}}};
        backend.answer = "[[1]]";
        Adapter adapter(arena, backend);
        Budget budget;
        budget.max_steps = maximum;
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, "[[2]]").root, budget);
        t.check(result.outcome == MatrixOutcome::ResourceExceeded && result.expression == kNoNode &&
                derivation.all_verified_from(0) && derivation.size() <= maximum,
                "step exhaustion withholds determinant and preserves only completed verified prefix");
    }
    for (size_t cycles : {size_t{65}, size_t{66}}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        for (size_t cycle = 0; cycle < cycles; ++cycle) {
            backend.events.push_back({"[[1,0],[1,0]]", "[[9223372036854775807,0],[1,0]]",
                MatrixRowScale{0, {9223372036854775807LL, 1}}});
            backend.events.push_back({"[[9223372036854775807,0],[1,0]]", "[[1,0],[1,0]]",
                MatrixRowAddMultiple{0, 1, {-9223372036854775806LL, 1}}});
        }
        backend.events.push_back({"[[1,0],[1,0]]", "[[1,0],[0,0]]", MatrixRowAddMultiple{1, 0, {-1, 1}}});
        backend.answer = "[[1,0],[0,0]]";
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, "[[1,0],[1,0]]").root);
        if (cycles == 65) {
            t.check(result.outcome == MatrixOutcome::Determined && print(arena, result.expression) == "0",
                    "a 4095-bit accumulated determinant factor stays exact within the resource policy");
        } else {
            t.check(result.outcome == MatrixOutcome::ResourceExceeded && result.expression == kNoNode &&
                    result.detail.find("4096 bits") != std::string::npos && !arena.failed() &&
                    derivation.size() == 131 && backend.delivered == 131 && derivation.all_verified_from(0),
                    "the neighboring factor overflow stops delivery and preserves the verified determinant prefix");
        }
    }
    {
        Arena arena;
        const NodeId reciprocal = arena.binary(Kind::Pow, arena.integer("5"), arena.integer("-1"));
        const NodeId parsed = parse(arena, "1/5").root;
        const NodeId one = arena.integer("1");
        t.check(verify_matrix_determinant_factor(arena, MatrixRowScale{0, {1, 5}}, one, reciprocal).outcome == VerificationOutcome::Passed &&
                verify_matrix_determinant_factor(arena, MatrixRowScale{0, {1, 5}}, one, parsed).outcome == VerificationOutcome::Passed,
                "generated and parser reciprocal exponent spellings produce the same determinant certificate");
    }
    for (bool child : {false, true}) {
        Arena arena;
        const NodeId base = arena.integer("5");
        const NodeId exponent_magnitude = arena.integer("1");
        const NodeId exponent = arena.unary(Kind::Neg, exponent_magnitude);
        const NodeId reciprocal = arena.binary(Kind::Pow, base, exponent);
        const NodeId one = arena.integer("1");
        arena.mark_approximate(child ? exponent_magnitude : exponent);
        t.check(verify_matrix_determinant_factor(arena, MatrixRowScale{0, {1, 5}}, one, reciprocal).outcome != VerificationOutcome::Passed,
                "approximate reciprocal exponent or magnitude cannot enter an exact determinant certificate");
    }
    for (size_t stop_after : {size_t{0}, size_t{1}, size_t{2}}) {
        Arena arena;
        Derivation derivation;
        bool cancel = stop_after == 0;
        RecordedBackend backend;
        backend.events = fixtures[9].events;
        backend.answer = fixtures[9].echelon;
        backend.cancel = &cancel;
        backend.cancel_after = stop_after;
        Adapter adapter(arena, backend);
        Budget budget;
        budget.poll = cancelled;
        budget.poll_context = &cancel;
        const NodeId input = parse(arena, fixtures[9].input).root;
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, input, budget);
        t.check(result.outcome == MatrixOutcome::Cancelled && result.expression == kNoNode && derivation.all_verified_from(0) &&
                    derivation.size() == (stop_after == 0 ? 0 : stop_after + 1),
                "cancellation before, between or after row events withholds the determinant and retains the exact prefix");
        cancel = false;
        backend.cancel = nullptr;
        Derivation recovered;
        const MatrixResult retry = matrix_determinant(arena, adapter, recovered, input, budget);
        t.check(retry.outcome == MatrixOutcome::Determined, "a fresh determinant request succeeds after cancellation");
    }
    for (size_t maximum : {size_t{8}, size_t{12}, size_t{18}, size_t{23}, size_t{24}}) {
        Limits limits;
        limits.max_nodes = maximum;
        Arena arena(limits);
        Derivation derivation;
        RecordedBackend backend;
        backend.events = fixtures[9].events;
        backend.answer = fixtures[9].echelon;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, fixtures[9].input).root);
        if (maximum == 24) {
            t.check(!arena.failed() && result.outcome == MatrixOutcome::Determined && arena.node_count() == maximum,
                    "the neighboring exact Arena capacity permits a complete determinant");
            continue;
        }
        t.check(arena.failed() && result.outcome == MatrixOutcome::ResourceExceeded &&
                    result.expression == kNoNode && derivation.size() == 0,
                "Arena failure during determinant factors or expressions discards all unsafe records at " +
                std::to_string(maximum) + " nodes: " + matrix_outcome_name(result.outcome) +
                ", allocated " + std::to_string(arena.node_count()));
    }
    for (size_t fixture_index : {size_t{0}, size_t{5}, size_t{9}}) {
        bool completed = false;
        for (size_t maximum = 0; maximum <= 64; ++maximum) {
            Limits limits;
            limits.max_nodes = maximum;
            Arena arena(limits);
            Derivation derivation;
            RecordedBackend backend;
            backend.events = fixtures[fixture_index].events;
            backend.answer = fixtures[fixture_index].echelon;
            Adapter adapter(arena, backend);
            const MatrixResult result = matrix_determinant(arena, adapter, derivation,
                parse(arena, fixtures[fixture_index].input).root);
            if (!arena.failed()) {
                completed = result.outcome == MatrixOutcome::Determined && result.expression != kNoNode;
                break;
            }
            t.check(result.outcome == MatrixOutcome::ResourceExceeded && result.expression == kNoNode && derivation.size() == 0,
                    "every allocation cap across determinant row, diagonal and final scalar construction safely withholds all records");
        }
        t.check(completed, "allocation-cap sweep reaches a healthy determinant with the same backend trace");
    }
    for (int fault = 0; fault < 4; ++fault) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = fixtures[9].events;
        backend.answer = fixtures[9].echelon;
        backend.ignore_stop = true;
        if (fault == 0) backend.events[0].operation = MatrixRowScale{0, {1, 5}};
        if (fault == 1) backend.events.erase(backend.events.begin());
        if (fault == 2) backend.answer = "[[2,0],[0,1]]";
        if (fault == 3) backend.events[0].after = "[[1,0],[0,9]]";
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, fixtures[9].input).root);
        t.check(result.outcome == MatrixOutcome::VerificationFailed && result.expression == kNoNode &&
                    derivation.all_verified_from(0) && derivation.size() <= 3,
                "incorrect row multiplier, missing transition, wrong endpoint and changed untouched row all withhold determinant");
    }
    {
        Arena arena;
        Derivation derivation;
        derivation.request.numeric_mode = NumericMode::Decimal;
        RecordedBackend backend;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, "[[2]]").root);
        t.check(result.outcome == MatrixOutcome::UnsupportedForm && backend.calls == 0,
                "determinant walkthrough refuses Decimal mode before backend work");
    }
    for (const ResultTag tag : {ResultTag::Cancelled, ResultTag::ResourceFailure, ResultTag::Timeout,
            ResultTag::BackendError, ResultTag::MalformedResult, ResultTag::UnsupportedOperation, ResultTag::Approximate}) {
        Arena arena;
        Derivation derivation;
        RecordedBackend backend;
        backend.events = {{"[[2]]", "[[1]]", MatrixRowScale{0, {1, 2}}}};
        backend.answer = "[[1]]";
        backend.tag = tag;
        Adapter adapter(arena, backend);
        const MatrixResult result = matrix_determinant(arena, adapter, derivation, parse(arena, "[[2]]").root);
        const MatrixOutcome expected = tag == ResultTag::Cancelled ? MatrixOutcome::Cancelled :
            tag == ResultTag::ResourceFailure || tag == ResultTag::Timeout ? MatrixOutcome::ResourceExceeded :
            tag == ResultTag::BackendError ? MatrixOutcome::DependencyUnavailable :
            tag == ResultTag::MalformedResult ? MatrixOutcome::VerificationFailed : MatrixOutcome::UnsupportedForm;
        t.check(result.outcome == expected && result.expression == kNoNode && derivation.size() == 2 &&
                    derivation.all_verified_from(0),
                "determinant retains the verified prefix with truthful backend failure classification");
    }
}

}
