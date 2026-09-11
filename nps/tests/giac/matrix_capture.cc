#include <iostream>
#include <string>
#include <vector>

#include "nps/cas/giac_typed.h"
#include "nps/cas/matrix_events.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/matrix_form.h"
#include "nps/steps/matrix_row.h"

#include "giac.h"

namespace {
int interrupt_mask = 7;
size_t mask_calls = 0;
size_t restore_calls = 0;
size_t graphics_resets = 0;
size_t checks = 0;
size_t failures = 0;
size_t foreign_callbacks = 0;
int interrupt_on_reset = 0;

void check(bool passed, const std::string &what) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cout << "FAIL " << what << '\n';
    }
}

void prior_callback(unsigned, const std::string &, const giac::vecteur &, const giac::context *) {
    ++foreign_callbacks;
}

struct Row {
    nps::NodeId before;
    nps::NodeId after;
    nps::MatrixRowOperation operation;
};

class Recorder : public nps::MatrixRowSink {
  public:
    explicit Recorder(nps::Arena &arena) : arena_(arena), meter_(nps::Budget{}) {}

    bool row(nps::NodeId before, nps::NodeId after, const nps::MatrixRowOperation &operation) override {
        ++calls;
        if (nested) {
            nested = false;
            nps::Arena other_arena;
            nps::Request other_request{};
            other_request.op = nps::Op::Rref;
            other_request.target = nps::parse(other_arena, "[[1]]").root;
            Recorder other_sink(other_arena);
            nps::TypedGiacBackend other_backend;
            nps::TypedResult other_answer;
            other_backend.matrix_steps(other_request, other_arena, other_sink, &other_answer);
            check(other_answer.tag == nps::ResultTag::UnsupportedOperation && other_sink.calls == 0,
                  "a nested request cannot replace the active capture");
        }
        if (stop_after != 0 && calls >= stop_after)
            return false;
        const auto record = nps::record_matrix_row(arena_, derivation, meter_, before, after, operation);
        check(record.check.verification.outcome == nps::VerificationOutcome::Passed,
              "every real Giac transition has an exact row certificate");
        if (record.check.verification.outcome != nps::VerificationOutcome::Passed)
            return false;
        rows.push_back({before, after, operation});
        return true;
    }

    size_t calls = 0;
    size_t stop_after = 0;
    bool nested = false;
    nps::Derivation derivation;
    std::vector<Row> rows;

  private:
    nps::Arena &arena_;
    nps::Meter meter_;
};

class ExhaustingSink final : public nps::MatrixRowSink {
  public:
    ExhaustingSink(nps::Arena &arena, bool accept) : arena_(arena), accept_(accept) {}

    bool row(nps::NodeId, nps::NodeId, const nps::MatrixRowOperation &) override {
        ++calls;
        for (int value = 100; !arena_.failed(); ++value)
            arena_.integer(std::to_string(value));
        return accept_;
    }

    size_t calls = 0;

  private:
    nps::Arena &arena_;
    bool accept_;
};

void restored(giac::context *context, int level) {
    check(giac::my_gprintf == prior_callback && giac::step_infolevel(context) == level,
          "the prior callback and step level are restored");
    check(interrupt_mask == 7 && mask_calls == restore_calls && mask_calls == graphics_resets,
          "interrupt masks and graphics resets balance");
    check(!giac::ctrl_c && !giac::interrupted, "a finished capture leaves the next request usable");
}
}

int TCT_Local_Control_Interrupts(int mask) {
    const int previous = interrupt_mask;
    if (mask == -1)
        ++mask_calls;
    else
        ++restore_calls;
    interrupt_mask = mask;
    return previous;
}

extern "C" void reset_gc() {
    ++graphics_resets;
    if (interrupt_on_reset == 1)
        giac::ctrl_c = true;
    if (interrupt_on_reset == 2)
        giac::interrupted = true;
    interrupt_on_reset = 0;
}
extern "C" const char *giac_caseval(const char *expression) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(expression);
}

int main() {
    auto *context = reinterpret_cast<giac::context *>(
        const_cast<char *>(giac::caseval("caseval contextptr")));
    giac::my_gprintf = prior_callback;
    giac::os_shell = false;
    for (bool ctrl : {true, false}) {
        nps::Arena arena;
        nps::Request request{};
        request.op = nps::Op::Rref;
        request.target = nps::parse(arena, "[[2]]").root;
        Recorder recorder(arena);
        nps::TypedGiacBackend backend;
        nps::TypedResult answer;
        giac::ctrl_c = ctrl;
        giac::interrupted = !ctrl;
        const size_t masks = mask_calls;
        backend.matrix_steps(request, arena, recorder, &answer);
        check(answer.tag == nps::ResultTag::Cancelled && answer.value == nps::kNoNode &&
                  recorder.calls == 0 && mask_calls == masks && giac::ctrl_c == ctrl && giac::interrupted == !ctrl,
              "a pending backend interruption is cancelled before initializing the capture context");
        giac::ctrl_c = false;
        giac::interrupted = false;
    }
    const struct {
        const char *input;
        const char *ref;
        const char *rref;
    } cases[] = {
        {"[[2,4,6],[0,3,6]]", "[[1,2,3],[0,1,2]]", "[[1,0,-1],[0,1,2]]"},
        {"[[0,1,2],[1,0,3]]", "[[1,0,3],[0,1,2]]", "[[1,0,3],[0,1,2]]"},
        {"[[1,2,3],[1,2,3]]", "[[1,2,3],[0,0,0]]", "[[1,2,3],[0,0,0]]"},
        {"[[-1/2,1]]", "[[1,-2]]", "[[1,-2]]"},
        {"[[0]]", "[[0]]", "[[0]]"},
        {"[[2]]", "[[1]]", "[[1]]"},
        {"[[0,2]]", "[[0,2]]", "[[0,1]]"},
        {"[[2,2,0,0,2,4],[0,3,3,0,3,6],[0,0,4,4,4,8],[0,0,0,5,5,10]]",
         "[[1,1,0,0,1,2],[0,1,1,0,1,2],[0,0,1,1,1,2],[0,0,0,1,1,2]]",
         "[[1,0,0,0,0,0],[0,1,0,0,1,2],[0,0,1,0,0,0],[0,0,0,1,1,2]]"},
    };
    for (int level : {0, 1, 2}) {
        giac::step_infolevel(level, context);
        for (bool reduced : {false, true}) {
            for (const auto &fixture : cases) {
                nps::Arena arena;
                const auto input = nps::parse(arena, fixture.input);
                const auto expected = nps::parse(arena, reduced ? fixture.rref : fixture.ref);
                check(input.ok() && expected.ok(), "independent matrix fixtures parse");
                nps::Request request{};
                request.op = reduced ? nps::Op::Rref : nps::Op::Ref;
                request.target = input.root;
                Recorder recorder(arena);
                recorder.nested = true;
                nps::TypedGiacBackend backend;
                nps::Adapter adapter(arena, backend);
                const nps::Response answer = adapter.matrix_steps(request, recorder);
                check(adapter.call_count() == 1, "captured reduction uses one allowlisted backend call");
                check(answer.tag == nps::ResultTag::Exact && answer.shape == nps::ResultShape::Matrix,
                      std::string("exact captured result: ") + fixture.input + ": " + answer.detail);
                check(nps::verify_matrix_row(arena, answer.value, expected.root,
                          nps::MatrixRowScale{0, {1, 1}}).verification.outcome == nps::VerificationOutcome::Passed,
                      std::string("the complete answer matches the independent exact matrix: ") +
                          (reduced ? "rref " : "ref ") + fixture.input + " got " + nps::print(arena, answer.value));
                const auto form = reduced ? nps::MatrixForm::ReducedEchelon : nps::MatrixForm::Echelon;
                check(nps::verify_matrix_form(arena, answer.value, form).outcome == nps::VerificationOutcome::Passed,
                      "the complete matrix has the requested final form");
                nps::NodeId previous = input.root;
                for (const Row &row : recorder.rows) {
                    check(nps::verify_matrix_row(arena, previous, row.before,
                              nps::MatrixRowScale{0, {1, 1}}).verification.outcome == nps::VerificationOutcome::Passed,
                          "recorded states are contiguous from the original input");
                    check(nps::verify_matrix_row(arena, row.before, row.after, row.operation)
                              .verification.outcome == nps::VerificationOutcome::Passed,
                          "snapshots remain unchanged after Giac finishes");
                    previous = row.after;
                }
                check(nps::verify_matrix_row(arena, previous, answer.value,
                          nps::MatrixRowScale{0, {1, 1}}).verification.outcome == nps::VerificationOutcome::Passed,
                      "the last recorded state reaches the complete answer");
                restored(context, level);
            }
        }
    }
    for (size_t stop : {size_t{1}, size_t{2}}) {
        nps::Arena arena;
        nps::Request request{};
        request.op = nps::Op::Rref;
        request.target = nps::parse(arena, "[[2,4,6],[1,3,5],[3,2,1]]").root;
        Recorder recorder(arena);
        recorder.stop_after = stop;
        nps::TypedResult answer;
        nps::TypedGiacBackend backend;
        backend.matrix_steps(request, arena, recorder, &answer);
        check(answer.tag == nps::ResultTag::Unevaluated && answer.value == nps::kNoNode,
              "a stopped consumer retains its terminal tag despite setting the backend interrupt flag");
        check(recorder.calls == stop, "delivery stops at the refused transition");
        restored(context, 2);
    }
    for (bool accept : {false, true}) {
        nps::Limits limits;
        limits.max_nodes = 64;
        nps::Arena arena(limits);
        nps::Request request{};
        request.op = nps::Op::Rref;
        request.target = nps::parse(arena, "[[1,2],[1,3]]").root;
        ExhaustingSink sink(arena, accept);
        nps::TypedResult answer;
        nps::TypedGiacBackend backend;
        backend.matrix_steps(request, arena, sink, &answer);
        check(arena.failed() && answer.tag == nps::ResultTag::ResourceFailure &&
                  answer.value == nps::kNoNode && answer.values.empty(),
              "consumer Arena exhaustion withholds the answer regardless of its return value");
        check(sink.calls == 1, "consumer Arena exhaustion stops all later row deliveries");
        restored(context, 2);

        nps::Arena healthy_arena;
        nps::Request healthy_request{};
        healthy_request.op = nps::Op::Rref;
        healthy_request.target = nps::parse(healthy_arena, "[[1,2],[1,3]]").root;
        Recorder recorder(healthy_arena);
        nps::TypedResult healthy_answer;
        backend.matrix_steps(healthy_request, healthy_arena, recorder, &healthy_answer);
        const auto expected = nps::parse(healthy_arena, "[[1,0],[0,1]]");
        check(healthy_answer.tag == nps::ResultTag::Exact && expected.ok() &&
                  nps::verify_matrix_row(healthy_arena, healthy_answer.value, expected.root,
                      nps::MatrixRowScale{0, {1, 1}}).verification.outcome == nps::VerificationOutcome::Passed,
              "a healthy request after consumer Arena exhaustion completes with the expected matrix");
        restored(context, 2);
    }
    for (int flag : {1, 2}) {
        nps::Arena arena;
        nps::Request request{};
        request.op = nps::Op::Rref;
        request.target = nps::parse(arena, "[[2,4],[0,3]]").root;
        Recorder recorder(arena);
        nps::TypedResult answer;
        nps::TypedGiacBackend backend;
        interrupt_on_reset = flag;
        backend.matrix_steps(request, arena, recorder, &answer);
        check(answer.tag == nps::ResultTag::Cancelled && answer.value == nps::kNoNode,
              "interruption during backend return is cancelled before capture restores the flags");
        restored(context, 2);
    }
    check(foreign_callbacks == 0, "capture never forwards request events to the shell callback");
    giac::my_gprintf = nullptr;
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
