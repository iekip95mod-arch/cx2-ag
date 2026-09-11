#include <algorithm>
#include <array>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <gmpxx.h>

#include "nps/cas/giac_typed.h"
#include "nps/cas/matrix_events.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/matrix.h"
#include "nps/steps/matrix_determinant.h"
#include "nps/steps/matrix_row.h"
#include "giac.h"

namespace {
size_t checks = 0;
size_t failures = 0;
int interrupt_mask = 7;
size_t masks = 0;
size_t restores = 0;
size_t resets = 0;
size_t foreign_callbacks = 0;
bool expire_on_mask = false;
std::array<size_t, 4> mutations{};

void check(bool passed, const std::string &what) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cout << "FAIL " << what << '\n';
    }
}

mpq_class rational(const nps::Arena &arena, nps::NodeId expression) {
    const auto &node = arena.at(expression);
    const auto children = arena.children(expression);
    if (node.kind == nps::Kind::Integer)
        return mpq_class(nps::print(arena, expression));
    if (node.kind == nps::Kind::Neg)
        return -rational(arena, children[0]);
    if (node.kind == nps::Kind::Add || node.kind == nps::Kind::Mul) {
        mpq_class accumulated(node.kind == nps::Kind::Add ? 0 : 1);
        for (auto child : children) {
            if (node.kind == nps::Kind::Add)
                accumulated += rational(arena, child);
            else
                accumulated *= rational(arena, child);
        }
        return accumulated;
    }
    if (node.kind == nps::Kind::Pow) {
        const mpq_class exponent = rational(arena, children[1]);
        if (exponent.get_den() != 1 || exponent < -64 || exponent > 64)
            throw std::runtime_error("oracle exponent outside its test envelope");
        const long power = exponent.get_num().get_si();
        const mpq_class base = rational(arena, children[0]);
        mpq_class raised(1);
        for (long count = 0; count < (power < 0 ? -power : power); ++count)
            raised *= base;
        return power < 0 ? mpq_class(1 / raised) : raised;
    }
    throw std::runtime_error("oracle expected exact rational syntax");
}

using Cells = std::vector<std::vector<mpq_class>>;

Cells cells(const nps::Arena &arena, nps::NodeId expression) {
    const auto view = nps::MatrixView::from(arena, expression);
    if (!view || view->rows() != view->columns() || view->rows() > 4)
        throw std::runtime_error("oracle expected a square matrix of order one through four");
    Cells square(view->rows(), std::vector<mpq_class>(view->columns()));
    for (size_t row = 0; row < square.size(); ++row)
        for (size_t column = 0; column < square.size(); ++column)
            square[row][column] = rational(arena, view->cell(row, column));
    return square;
}

mpq_class permutation_determinant(const Cells &square) {
    std::vector<size_t> permutation(square.size());
    std::iota(permutation.begin(), permutation.end(), 0);
    mpq_class total(0);
    do {
        mpq_class term(1);
        size_t inversions = 0;
        for (size_t row = 0; row < square.size(); ++row) {
            term *= square[row][permutation[row]];
            for (size_t other = row + 1; other < square.size(); ++other)
                inversions += permutation[row] > permutation[other];
        }
        total += inversions % 2 == 0 ? term : mpq_class(-term);
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    return total;
}

mpq_class coefficient(nps::Rational factor) {
    mpq_class exact(std::to_string(factor.num) + "/" + std::to_string(factor.den));
    exact.canonicalize();
    return exact;
}

mpq_class elementary_determinant(size_t order, const nps::MatrixRowOperation &operation) {
    Cells elementary(order, std::vector<mpq_class>(order));
    for (size_t row = 0; row < order; ++row)
        elementary[row][row] = 1;
    if (const auto *swap = std::get_if<nps::MatrixRowSwap>(&operation))
        std::swap(elementary[swap->first], elementary[swap->second]);
    else if (const auto *scale = std::get_if<nps::MatrixRowScale>(&operation))
        elementary[scale->row][scale->row] = coefficient(scale->factor);
    else {
        const auto &addition = std::get<nps::MatrixRowAddMultiple>(operation);
        elementary[addition.target][addition.source] = coefficient(addition.factor);
    }
    return permutation_determinant(elementary);
}

struct Row {
    nps::NodeId before;
    nps::NodeId after;
    nps::MatrixRowOperation operation;
};

class Capture final : public nps::Backend {
  public:
    bool eval(const std::string &, std::string *, std::string *) override {
        ++plain_calls;
        return false;
    }

    bool matrix_steps(const nps::Request &request, nps::Arena &arena,
                      nps::MatrixRowSink &sink, nps::TypedResult *answer) override {
        ++calls;
        check(request.op == nps::Op::Ref, "determinants request the actual Giac REF trace");
        class Forward final : public nps::MatrixRowSink {
          public:
            Forward(Capture &capture, nps::MatrixRowSink &sink) : capture_(capture), sink_(sink) {}
            bool row(nps::NodeId before, nps::NodeId after, const nps::MatrixRowOperation &operation) override {
                capture_.rows.push_back({before, after, operation});
                const bool accepted = sink_.row(before, after, operation);
                if (capture_.cancel_after != 0 && capture_.rows.size() == capture_.cancel_after)
                    capture_.cancelled = true;
                return accepted;
            }
          private:
            Capture &capture_;
            nps::MatrixRowSink &sink_;
        } forward(*this, sink);
        return backend_.matrix_steps(request, arena, forward, answer);
    }

    size_t calls = 0;
    size_t plain_calls = 0;
    size_t cancel_after = 0;
    bool cancelled = false;
    std::vector<Row> rows;
  private:
    nps::TypedGiacBackend backend_;
};

void previous_callback(unsigned, const std::string &, const giac::vecteur &, const giac::context *) {
    ++foreign_callbacks;
}

void restored(giac::context *context, int level) {
    check(giac::my_gprintf == previous_callback && giac::step_infolevel(context) == level,
          "determinant capture restores the existing callback and step level");
    check(interrupt_mask == 7 && masks == restores && masks == resets,
          "determinant capture balances graphics reset and interrupt restoration");
    check(foreign_callbacks == 0, "determinant capture does not leak events to the prior callback");
}

struct DeterminantState {
    nps::NodeId matrix = nps::kNoNode;
    mpq_class coefficient = 1;
};

DeterminantState determinant_state(const nps::Arena &arena, nps::NodeId expression) {
    const auto &node = arena.at(expression);
    if (node.kind == nps::Kind::Call && arena.text(expression) == "det")
        return {arena.children(expression)[0], 1};
    if (node.kind == nps::Kind::Mul) {
        DeterminantState state;
        for (auto child : arena.children(expression)) {
            const auto part = determinant_state(arena, child);
            if (part.matrix != nps::kNoNode) {
                if (state.matrix != nps::kNoNode)
                    throw std::runtime_error("determinant state has more than one matrix");
                state.matrix = part.matrix;
            }
            state.coefficient *= part.coefficient;
        }
        return state;
    }
    return {nps::kNoNode, rational(arena, expression)};
}

void verify_trace(nps::Arena &arena, nps::NodeId input, const nps::Derivation &derivation,
                  const std::vector<Row> &rows, const mpq_class &expected) {
    Cells prior = cells(arena, input);
    mpq_class ledger(1);
    size_t changing = 0;
    for (const Row &row : rows) {
        const Cells before = cells(arena, row.before);
        const Cells after = cells(arena, row.after);
        check(before == prior, "actual determinant callbacks form a continuous trace");
        const auto certificate = nps::verify_matrix_row(arena, row.before, row.after, row.operation);
        check(certificate.verification.outcome == nps::VerificationOutcome::Passed,
              "every actual determinant callback has an exact complete row certificate");
        const mpq_class multiplier = elementary_determinant(before.size(), row.operation);
        check(permutation_determinant(after) == multiplier * permutation_determinant(before),
              "an independent permutation certificate agrees with each row determinant effect");
        ledger *= multiplier;
        changing += before != after;
        prior = after;
    }
    mpq_class diagonal(1);
    for (size_t row = 0; row < prior.size(); ++row)
        diagonal *= prior[row][row];
    check(ledger != 0 && diagonal / ledger == expected,
          "the independent elementary ledger and diagonal recover the original determinant");
    const auto triangular = rows.empty() ? input : rows.back().after;
    const auto product = nps::parse(arena, diagonal.get_str()).root;
    const auto factor = nps::parse(arena, ledger.get_str()).root;
    const auto answer = nps::parse(arena, expected.get_str()).root;
    const mpq_class wrong_product = diagonal + 1;
    const mpq_class wrong_answer = expected + 1;
    check(nps::verify_matrix_determinant_diagonal(arena, triangular, product).outcome ==
              nps::VerificationOutcome::Passed &&
              nps::verify_matrix_determinant_diagonal(arena, triangular,
                  nps::parse(arena, wrong_product.get_str()).root).outcome == nps::VerificationOutcome::Failed,
          "the production diagonal certificate accepts the actual product and rejects a changed product");
    check(nps::verify_matrix_determinant_correction(arena, product, factor, answer).outcome ==
              nps::VerificationOutcome::Passed &&
              nps::verify_matrix_determinant_correction(arena, product, factor,
                  nps::parse(arena, wrong_answer.get_str()).root).outcome == nps::VerificationOutcome::Failed,
          "the production correction certificate rejects an incorrect scalar determinant");
    size_t recorded_rows = 0;
    size_t callback_index = 0;
    mpq_class recorded_factor(1);
    for (size_t index = 0; index < derivation.size(); ++index) {
        const auto id = static_cast<nps::StepId>(index);
        const auto &step = derivation.at(id);
        check(step.verified() && !step.has_failed_verification(), "published determinant steps are verified");
        const auto *change = derivation.transformation(id);
        if (!change || step.rule_id.find("matrix.det-row-") != 0)
            continue;
        ++recorded_rows;
        while (callback_index < rows.size() && cells(arena, rows[callback_index].before) ==
               cells(arena, rows[callback_index].after))
            ++callback_index;
        check(callback_index < rows.size(), "each determinant row step has an actual callback");
        if (callback_index >= rows.size())
            continue;
        const Row &row = rows[callback_index++];
        const auto before = determinant_state(arena, change->before);
        const auto after = determinant_state(arena, change->after);
        check(before.matrix != nps::kNoNode && after.matrix != nps::kNoNode &&
                  cells(arena, before.matrix) == cells(arena, row.before) &&
                  cells(arena, after.matrix) == cells(arena, row.after),
              "determinant wrappers preserve the actual callback matrices");
        check(cells(arena, before.matrix) != cells(arena, after.matrix),
              "identity row callbacks do not become teaching transformations");
        const mpq_class factor_before = 1 / before.coefficient;
        const mpq_class factor_after = 1 / after.coefficient;
        const mpq_class multiplier = elementary_determinant(prior.size(), row.operation);
        check(factor_before == recorded_factor && factor_after == factor_before * multiplier,
              "the published determinant ledger matches the independent elementary determinant");
        recorded_factor = factor_after;
        const auto before_factor = nps::parse(arena, factor_before.get_str()).root;
        const auto after_factor = nps::parse(arena, factor_after.get_str()).root;
        check(nps::verify_matrix_determinant_factor(arena, row.operation, before_factor,
                  after_factor).outcome == nps::VerificationOutcome::Passed,
              "the production factor verifier accepts each independently checked recorded ledger");
        mpq_class corrupt = factor_after;
        size_t mutation = 3;
        if (std::holds_alternative<nps::MatrixRowSwap>(row.operation)) {
            corrupt = factor_before;
            mutation = 0;
        } else if (const auto *scale = std::get_if<nps::MatrixRowScale>(&row.operation)) {
            corrupt = factor_before / coefficient(scale->factor);
            mutation = 1;
        } else if (expected == 0) {
            corrupt *= 2;
            mutation = 2;
            check(diagonal / corrupt == 0,
                  "a singular final answer alone cannot expose the corrupted row-add ledger");
        }
        if (corrupt != factor_after) {
            ++mutations[mutation];
            const auto damaged = nps::parse(arena, corrupt.get_str()).root;
            check(nps::verify_matrix_determinant_factor(arena, row.operation, before_factor,
                      damaged).outcome == nps::VerificationOutcome::Failed,
                  "the production factor certificate rejects an independently corrupted ledger");
        }
        ++mutations[3];
        check(nps::verify_matrix_determinant_factor(arena, row.operation, before_factor,
                  arena.integer("0")).outcome == nps::VerificationOutcome::Failed,
              "the production factor certificate rejects a zero accumulator");
        check(!change->concrete_action.empty() && !step.explanation_short.empty(),
              "recorded determinant rows carry concrete Do and Why guidance");
    }
    check(recorded_rows == changing, "each changing callback appears once in determinant guidance");
}

struct Fixture {
    const char *input;
    const char *expected;
};

const Fixture fixtures[] = {
    {"[[5]]", "5"}, {"[[-3/2]]", "-3/2"}, {"[[0]]", "0"},
    {"[[0,2],[3,4]]", "-6"}, {"[[1/2,1/3],[0,1/4]]", "1/8"},
    {"[[1,2],[2,4]]", "0"}, {"[[1,0],[0,1]]", "1"},
    {"[[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0]]", "0"},
    {"[[1,2,0,0],[2,4,0,0],[0,0,3,6],[0,0,6,12]]", "0"},
    {"[[0,1,0,0],[1,0,0,0],[0,0,2,0],[0,0,0,3]]", "-6"},
    {"[[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]]", "16"},
    {"[[2,1,0],[0,3,1],[0,0,4]]", "24"},
    {"[[3037000500,3037000499],[3037000501,3037000500]]", "1"},
    {"[[3037000500,0],[0,3037000500]]", "9223372037000250000"},
};
}

int TCT_Local_Control_Interrupts(int next) {
    const int prior = interrupt_mask;
    next == -1 ? ++masks : ++restores;
    interrupt_mask = next;
    if (expire_on_mask && next == -1) {
        expire_on_mask = false;
        giac::caseval_mod = 1;
        giac::caseval_n = 0;
        giac::caseval_begin = 0;
        giac::caseval_maxtime = -1;
    }
    return prior;
}
extern "C" void reset_gc() { ++resets; }
extern "C" const char *giac_caseval(const char *command) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(command);
}

int main() {
    for (const Fixture &fixture : fixtures) {
        nps::Arena arena;
        const auto input = nps::parse(arena, fixture.input);
        check(input.ok() && permutation_determinant(cells(arena, input.root)) == mpq_class(fixture.expected),
              std::string("the independent permutation oracle establishes ") + fixture.input + " = " + fixture.expected);
    }
    auto *context = reinterpret_cast<giac::context *>(
        const_cast<char *>(giac::caseval("caseval contextptr")));
    giac::os_shell = false;
    giac::my_gprintf = previous_callback;
    for (int level : {0, 1, 2}) {
        giac::step_infolevel(level, context);
        for (const Fixture &fixture : fixtures) {
            nps::Arena arena;
            nps::Derivation derivation;
            Capture backend;
            nps::Adapter adapter(arena, backend);
            const auto input = nps::parse(arena, fixture.input).root;
            const auto determinant = nps::matrix_determinant(arena, adapter, derivation, input);
            check(determinant.outcome == nps::MatrixOutcome::Determined &&
                      determinant.status == nps::DerivationStatus::SolvedAndVerified &&
                      determinant.expression != nps::kNoNode,
                  std::string("actual Giac supplies the verified determinant: ") + fixture.input + ": " + determinant.detail);
            if (determinant.expression != nps::kNoNode) {
                check(!nps::contains_list(arena, determinant.expression) &&
                          rational(arena, determinant.expression) == mpq_class(fixture.expected),
                      "the exact scalar answer matches the independent fixture, including values beyond int64");
                verify_trace(arena, input, derivation, backend.rows, mpq_class(fixture.expected));
            }
            check(backend.calls == 1 && backend.plain_calls == 0 && adapter.call_count() == 1,
                  "a determinant uses exactly one traced backend call and no plain fallback");
            restored(context, level);
        }
    }
    for (size_t mutation : mutations)
        check(mutation > 0, "swap, reciprocal scaling, singular addition and zero ledger mutations all ran");
    for (const char *unsupported : {"[]", "[1,2]", "[[1],[2,3]]", "[[1,2,3],[4,5,6]]",
             "[[[1]]]", "[[x]]", "[[1.0]]", "[[i]]",
             "[[1,0,0,0,0],[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0],[0,0,0,0,1]]"}) {
        nps::Arena arena;
        nps::Derivation derivation;
        Capture backend;
        nps::Adapter adapter(arena, backend);
        const auto input = nps::parse(arena, unsupported).root;
        const auto determinant = nps::matrix_determinant(arena, adapter, derivation, input);
        check(determinant.expression == nps::kNoNode && backend.calls == 0 && backend.plain_calls == 0 &&
                  determinant.outcome != nps::MatrixOutcome::Determined,
              std::string("unsupported determinant input is refused before Giac: ") + unsupported);
    }
    for (size_t scenario = 0; scenario < 9; ++scenario) {
        nps::Limits limits;
        if (scenario == 7) limits.max_nodes = 16;
        nps::Arena arena(limits);
        nps::Derivation derivation;
        Capture backend;
        nps::Adapter adapter(arena, backend);
        nps::Budget budget;
        giac::caseval_mod = 0;
        giac::ctrl_c = scenario == 1;
        giac::interrupted = scenario == 2;
        expire_on_mask = scenario == 3;
        if (scenario == 4) {
            backend.cancel_after = 1;
            budget.poll = [](void *context) { return *static_cast<bool *>(context); };
            budget.poll_context = &backend.cancelled;
        }
        if (scenario == 5) budget.max_steps = 1;
        if (scenario == 6) budget.max_backend_calls = 0;
        const auto input = nps::parse(arena, scenario == 0 ?
            "[[1,3037000500],[-3037000500,1]]" : "[[2,4],[1,3]]").root;
        const auto determinant = nps::matrix_determinant(arena, adapter, derivation, input, budget);
        const auto expected = scenario == 0 ? nps::MatrixOutcome::UnsupportedForm :
            scenario <= 4 ? nps::MatrixOutcome::Cancelled :
            scenario <= 7 ? nps::MatrixOutcome::ResourceExceeded : nps::MatrixOutcome::Determined;
        check(determinant.outcome == expected && (determinant.expression != nps::kNoNode) == (scenario == 8),
              "determinant refusal, interruption, resource exhaustion and recovery scenario " +
                  std::to_string(scenario) + ": " + nps::matrix_outcome_name(determinant.outcome));
        if (arena.failed())
            check(derivation.size() == 0, "arena exhaustion discards unusable determinant records");
        restored(context, 2);
        giac::caseval_mod = 0;
        giac::ctrl_c = false;
        giac::interrupted = false;
        nps::Arena recovery_arena;
        nps::Derivation recovery_derivation;
        Capture recovery_backend;
        nps::Adapter recovery_adapter(recovery_arena, recovery_backend);
        const auto recovery_input = nps::parse(recovery_arena, "[[5]]").root;
        const auto recovery = nps::matrix_determinant(recovery_arena, recovery_adapter, recovery_derivation, recovery_input);
        check(recovery.outcome == nps::MatrixOutcome::Determined &&
                  rational(recovery_arena, recovery.expression) == 5,
              "the request after each determinant failure succeeds");
        restored(context, 2);
    }
    std::cout << "actual Giac determinant: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
