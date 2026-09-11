#include "unit/adapter_tests.h"

#include <array>
#include <limits>

#include "nps/core/matrix.h"
#include "nps/core/parser.h"
#include "nps/steps/matrix_form.h"

namespace nps {
namespace {

NodeId parsed(Arena &arena, const char *expression) {
    return parse(arena, expression).root;
}

NodeId changed_cell(Arena &arena, NodeId matrix, size_t row, size_t column, const char *value) {
    const MatrixView view = *MatrixView::from(arena, matrix);
    std::vector<NodeId> rows;
    for (size_t r = 0; r < view.rows(); ++r) {
        std::vector<NodeId> cells;
        for (size_t c = 0; c < view.columns(); ++c)
            cells.push_back(r == row && c == column ? parsed(arena, value) : view.cell(r, c));
        rows.push_back(arena.list(cells));
    }
    return arena.list(rows);
}

}

void run_matrix_form_tests(TestSink &t) {
    struct FormCase {
        const char *matrix;
        bool echelon;
        bool reduced;
    };
    {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[1/2,1+2],[-3,2^3]]");
        std::array<Rational, 5> cells;
        cells.fill(Rational{17, 19});
        t.check(read_exact_matrix(arena, matrix), "matrix exact admission can validate without an output span");
        t.check(read_exact_matrix(arena, matrix, cells) && cells[0].num == 1 && cells[0].den == 2 &&
                    cells[1].num == 3 && cells[1].den == 1 && cells[2].num == -3 && cells[2].den == 1 &&
                    cells[3].num == 8 && cells[3].den == 1 && cells[4].num == 17 && cells[4].den == 19,
                "matrix admission writes exact arithmetic cells in row-major order and preserves unused capacity");
        std::array<Rational, 3> short_span;
        short_span.fill(Rational{17, 19});
        t.check(!read_exact_matrix(arena, matrix, short_span), "matrix admission refuses an output span smaller than the matrix");
        bool unchanged = true;
        for (const Rational &cell : short_span)
            unchanged = unchanged && cell.num == 17 && cell.den == 19;
        t.check(unchanged, "an undersized span is refused before any cell is written");
        std::array<Rational, 1> singleton;
        t.check(read_exact_matrix(arena, parsed(arena, "[[5/7]]"), singleton) &&
                    singleton[0].num == 5 && singleton[0].den == 7,
                "an exact one-by-one matrix fills a one-cell span without losing its shape");
    }
    for (const char *expression : {"1", "[]", "[1]", "[[]]", "[[1],[2,3]]", "[[[1]]]", "[[x]]",
                                  "[[1.0]]", "[[0*0.5]]", "[[1/0]]", "[[sin(0)]]", "[[1=1]]",
                                  "[[9223372036854775808]]"}) {
        Arena arena;
        t.check(!read_exact_matrix(arena, parsed(arena, expression)),
                std::string("shared exact matrix admission refuses unsupported shape or arithmetic: ") + expression);
    }
    const FormCase cases[] = {
        {"[[0]]", true, true},
        {"[[1]]", true, true},
        {"[[2]]", true, false},
        {"[[-1/2]]", true, false},
        {"[[0,0,0],[0,0,0]]", true, true},
        {"[[1],[0],[0],[0]]", true, true},
        {"[[0],[1]]", false, false},
        {"[[1,0,3],[0,1,2]]", true, true},
        {"[[2,4,6],[0,3,6]]", true, false},
        {"[[1,2,3],[0,1,2]]", true, false},
        {"[[0,1,2],[1,0,3]]", false, false},
        {"[[1,2,3],[1,2,3]]", false, false},
        {"[[1,2,3],[0,0,0]]", true, true},
        {"[[0,0,0],[0,0,1]]", false, false},
        {"[[0,1,0],[0,0,0],[0,0,1]]", false, false},
        {"[[0,1,0],[0,0,1],[0,0,0]]", true, true},
        {"[[0,0,1],[0,0,1]]", false, false},
        {"[[1,2,0],[0,0,1]]", true, true},
        {"[[1,2,3],[0,0,1]]", true, false},
        {"[[0,0,2]]", true, false},
        {"[[0,0,1]]", true, true},
        {"[[0,1,2/3,0,5/7],[0,0,0,1,-3/2]]", true, true},
        {"[[0,-3/5,2/3,0,5/7],[0,0,0,7/4,-3/2]]", true, false},
        {"[[1,2,0,3,0,4],[0,0,1,5,0,6],[0,0,0,0,1,7],[0,0,0,0,0,0]]", true, true},
        {"[[1,0,0,0,0,0],[0,1,0,0,0,0],[0,0,1,0,0,0],[0,0,0,0,0,1]]", true, true},
        {"[[1+2-3,2/2,1/3],[0,0,0]]", true, true},
        {"[[9223372036854775807,1]]", true, false},
    };
    for (const FormCase &c : cases) {
        Arena arena;
        const NodeId matrix = parsed(arena, c.matrix);
        const size_t nodes = arena.node_count();
        for (MatrixForm form : {MatrixForm::Echelon, MatrixForm::ReducedEchelon}) {
            const bool expected = form == MatrixForm::Echelon ? c.echelon : c.reduced;
            const VerificationRecord check = verify_matrix_form(arena, matrix, form);
            t.check(check.outcome == (expected ? VerificationOutcome::Passed : VerificationOutcome::Failed),
                    std::string(form == MatrixForm::Echelon ? "REF: " : "RREF: ") + c.matrix);
            t.check(check.strength == (expected ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed) &&
                        !check.detail.empty() && check.evidence_id.empty() && arena.node_count() == nodes,
                    "form checks carry honest metadata and do not alter the arena");
            t.equal(check.method, form == MatrixForm::Echelon ? "exact row echelon form" : "exact reduced row echelon form",
                    "the form check identifies the exact condition verified");
        }
    }
    {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[1,2,0,3,0,4],[0,0,1,5,0,6],[0,0,0,0,1,7],[0,0,0,0,0,0]]");
        const std::array<size_t, 3> pivots = {0, 2, 4};
        for (size_t row = 0; row < 3; ++row) {
            const NodeId scaled = changed_cell(arena, matrix, row, pivots[row], "-2/3");
            t.check(verify_matrix_form(arena, scaled, MatrixForm::Echelon).outcome == VerificationOutcome::Passed &&
                        verify_matrix_form(arena, scaled, MatrixForm::ReducedEchelon).outcome == VerificationOutcome::Failed,
                    "changing a pivot scale preserves REF but invalidates RREF");
            for (size_t other = 0; other < 4; ++other) {
                if (other == row)
                    continue;
                const NodeId corrupted = changed_cell(arena, matrix, other, pivots[row], "5/7");
                t.check(verify_matrix_form(arena, corrupted, MatrixForm::ReducedEchelon).outcome == VerificationOutcome::Failed,
                        "each off-pivot entry in a pivot column is checked");
                if (other > row)
                    t.check(verify_matrix_form(arena, corrupted, MatrixForm::Echelon).outcome == VerificationOutcome::Failed,
                            "a nonzero entry below any pivot invalidates REF");
            }
            for (size_t column : {size_t{1}, size_t{3}, size_t{5}}) {
                if (column < pivots[row])
                    continue;
                for (const char *value : {"0", "-17/19", "9223372036854775807"}) {
                    const NodeId changed = changed_cell(arena, matrix, row, column, value);
                    t.check(verify_matrix_form(arena, changed, MatrixForm::ReducedEchelon).outcome == VerificationOutcome::Passed,
                            "non-pivot columns to the right admit arbitrary exact values");
                }
            }
        }
    }
    for (const char *expression : {"1", "[]", "[1]", "[[]]", "[[1],[2,3]]", "[[[1]]]", "[[1],[2],3]"}) {
        Arena arena;
        const NodeId matrix = parsed(arena, expression);
        for (MatrixForm form : {MatrixForm::Echelon, MatrixForm::ReducedEchelon})
            t.check(verify_matrix_form(arena, matrix, form).outcome == VerificationOutcome::Failed,
                    std::string("malformed matrix shape fails: ") + expression);
    }
    for (const char *expression : {"[[x]]", "[[1.0]]", "[[i]]", "[[sin(0)]]", "[[1=1]]", "[[1/0]]",
                                  "[[9223372036854775808]]", "[[0*0.5]]", "[[1,0],[2,x]]",
                                  "[[1],[0],[0],[0],[0]]", "[[1,0,0,0,0,0,0]]"}) {
        Arena arena;
        const NodeId matrix = parsed(arena, expression);
        for (MatrixForm form : {MatrixForm::Echelon, MatrixForm::ReducedEchelon}) {
            const VerificationRecord check = verify_matrix_form(arena, matrix, form);
            t.check(check.outcome == VerificationOutcome::Inconclusive && check.strength == EvidenceStrength::Unsupported,
                    std::string("unsupported matrix arithmetic or size is inconclusive: ") + expression);
        }
    }
    for (size_t level = 0; level < 3; ++level) {
        Arena arena;
        const NodeId matrix = parsed(arena, "[[1,0],[0,1]]");
        const NodeId row = arena.children(matrix)[1];
        t.check(verify_matrix_form(arena, matrix, MatrixForm::ReducedEchelon).outcome == VerificationOutcome::Passed,
                "the unmarked exact matrix establishes form before provenance changes");
        arena.mark_approximate(level == 0 ? matrix : level == 1 ? row : arena.children(row)[0]);
        const auto view = MatrixView::from(arena, matrix);
        t.check(view.has_value(), "approximate matrix, row and cell identities preserve matrix shape");
        std::array<Rational, 4> cells;
        t.check(!read_exact_matrix(arena, matrix) && !read_exact_matrix(arena, matrix, cells),
                "shared exact admission rejects root, row and cell provenance with or without output cells");
        if (level < 2) {
            bool exact_cells = true;
            for (size_t r = 0; r < view->rows(); ++r) {
                for (size_t column = 0; column < view->columns(); ++column) {
                    Rational value;
                    exact_cells = exact_cells && read_matrix_rational(arena, view->cell(r, column), &value);
                }
            }
            t.check(exact_cells, "form provenance fixtures mark only the root or row, not any cell");
        }
        for (MatrixForm form : {MatrixForm::Echelon, MatrixForm::ReducedEchelon}) {
            const VerificationRecord check = verify_matrix_form(arena, matrix, form);
            t.check(check.outcome == VerificationOutcome::Inconclusive && check.strength == EvidenceStrength::Unsupported,
                    "approximate matrix, row and cell provenance cannot establish exact form");
        }
    }
    {
        Arena arena;
        for (NodeId missing : {kNoNode, std::numeric_limits<NodeId>::max() - 1})
            t.check(verify_matrix_form(arena, missing, MatrixForm::Echelon).outcome == VerificationOutcome::Failed,
                    "an absent matrix fails the shape contract");
    }
    {
        Limits limits;
        limits.max_nodes = 3;
        Arena arena(limits);
        const NodeId matrix = parsed(arena, "[[1]]");
        arena.integer("2");
        t.check(arena.failed(), "the failed-arena fixture exhausts its node budget");
        t.check(!read_exact_matrix(arena, matrix), "shared exact matrix admission rejects a failed arena");
        for (MatrixForm form : {MatrixForm::Echelon, MatrixForm::ReducedEchelon})
            t.check(verify_matrix_form(arena, matrix, form).outcome == VerificationOutcome::Inconclusive,
                    "a failed arena cannot establish matrix form");
    }
}

}
