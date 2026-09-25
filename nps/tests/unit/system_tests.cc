#include <array>
#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/system.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

struct Solved {
    SystemResult result;
    std::string answer;
    std::vector<std::string> rules;
    std::string last_strength;
    std::vector<std::string> broken;
};

Solved run(const char *equations, const char *unknowns, const Budget &budget = Budget(),
           NumericMode mode = NumericMode::Exact, SystemMethod method = SystemMethod::Elimination) {
    Solved out;
    Arena arena;
    Derivation d;
    d.request.numeric_mode = mode;
    const ParseResult system = parse(arena, equations);
    const ParseResult names = parse(arena, unknowns);
    if (!system.ok() || !names.ok()) {
        out.answer = "the test's own input did not parse";
        return out;
    }
    out.result = solve_linear_system(arena, d, system.root, names.root, budget, method);
    for (size_t i = 0; i < out.result.solutions.size(); ++i) {
        if (i)
            out.answer += ", ";
        out.answer += print(arena, out.result.solutions[i]);
    }
    for (size_t i = 0; i < d.size(); ++i)
        out.rules.push_back(d.at(static_cast<StepId>(i)).rule_id);
    if (d.size() > 0 && !d.at(static_cast<StepId>(d.size() - 1)).verifications.empty())
        out.last_strength =
            evidence_strength_name(d.at(static_cast<StepId>(d.size() - 1)).verifications.front().strength);
    invariants::Pass audit;
    audit.walk(arena, d, false, true, &out.broken);
    return out;
}

bool has_rule(const Solved &s, const char *rule) {
    for (const std::string &id : s.rules) {
        if (id == rule)
            return true;
    }
    return false;
}

std::string first_broken(const Solved &s) {
    return s.broken.empty() ? std::string() : ", got " + s.broken.front();
}

std::string outcome(const Solved &s) {
    return system_outcome_name(s.result.outcome);
}

std::string status(const Solved &s) {
    return derivation_status_name(s.result.status);
}

LinearRowRead read(const char *equation, const char *unknowns, std::array<Rational, 6> *row) {
    Arena arena;
    const ParseResult parsed = parse(arena, equation);
    const ParseResult names = parse(arena, unknowns);
    if (!parsed.ok() || !names.ok())
        return LinearRowRead::NotLinear;
    std::vector<NodeId> symbols;
    for (NodeId name : arena.children(names.root))
        symbols.push_back(name);
    Budget budget;
    Meter meter(budget);
    return read_linear_row(arena, parsed.root, symbols, meter,
                           std::span<Rational>(row->data(), symbols.size() + 1));
}

bool row_is(const std::array<Rational, 6> &row, std::initializer_list<Rational> expected) {
    size_t i = 0;
    for (const Rational &value : expected) {
        if (!rational_equal(row[i], value))
            return false;
        ++i;
    }
    return true;
}

void test_reader(TestSink &t) {
    std::array<Rational, 6> row{};
    t.check(read("2x + 3y = 5", "[x, y]", &row) == LinearRowRead::Read &&
                row_is(row, {{2, 1}, {3, 1}, {5, 1}}),
            "a linear equation reads as its coefficients and right-hand side");
    t.check(read("2(x - y) = y + 4", "[x, y]", &row) == LinearRowRead::Read &&
                row_is(row, {{2, 1}, {-3, 1}, {4, 1}}),
            "terms on both sides are collected, including a distributed constant");
    t.check(read("x/2 + y = 1 - x", "[x, y]", &row) == LinearRowRead::Read &&
                row_is(row, {{3, 2}, {1, 1}, {1, 1}}),
            "a coefficient that is a fraction stays exact");
    t.check(read("0 = 3", "[x, y]", &row) == LinearRowRead::Read &&
                row_is(row, {{0, 1}, {0, 1}, {3, 1}}),
            "an equation with no unknown in it is a row of zeros, not a refusal");
    t.check(read("x*y = 1", "[x, y]", &row) == LinearRowRead::NotLinear,
            "a product of two unknowns is not linear");
    t.check(read("x/y = 1", "[x, y]", &row) == LinearRowRead::NotLinear,
            "an unknown in a denominator is not linear");
    t.check(read("x^2 + y = 1", "[x, y]", &row) == LinearRowRead::NotLinear,
            "an unknown under a power other than one is not linear");
    t.check(read("sin(x) + y = 1", "[x, y]", &row) == LinearRowRead::NotLinear,
            "an unknown inside a function is not linear");
    t.check(read("a*x + y = 1", "[x, y]", &row) == LinearRowRead::OtherSymbol,
            "a symbol that is not one of the unknowns is named as such rather than read");
    t.check(read("1.5x + y = 1", "[x, y]", &row) == LinearRowRead::Inexact,
            "a decimal coefficient is outside the exact envelope");
    t.check(read("x + y", "[x, y]", &row) == LinearRowRead::NotLinear,
            "an expression that is not an equation is not a row");
}

void test_unique(TestSink &t) {
    {
        const Solved s = run("[x + y = 3, x - y = 1]", "[x, y]");
        t.equal(outcome(s), "solved", "a two by two system with one solution is solved");
        t.equal(s.answer, "(x = 2), (y = 1)", "with each unknown read off the reduced matrix");
        t.equal(status(s), "solved and verified", "and every step and the substitution check passed");
        t.check(has_rule(s, "system.augmented-matrix"), "the system is written as an augmented matrix");
        t.check(has_rule(s, "matrix.row-add-multiple"),
                "and reduced by the row operations the matrix rules already verify");
        t.check(has_rule(s, "matrix.rref-conclusion"), "to a reduced row echelon form that is checked");
        t.check(has_rule(s, "system.read-solution") && has_rule(s, "system.check-by-substitution"),
                "then read back and substituted into every original equation");
        t.equal(s.last_strength, "candidate checked", "and the substitution is a candidate check");
        t.check(s.broken.empty(), "the record passes the invariant pass" + first_broken(s));
        t.evidence("ALG-013", s.result.outcome == SystemOutcome::Solved && s.answer == "(x = 2), (y = 1)",
                   "a linear system is solved with explicit elimination steps and checked by substitution");
    }
    {
        const Solved s = run("[x + y + z = 6, 2y + 5z = -4, 2x + 5y - z = 27]", "[x, y, z]");
        t.equal(s.answer, "(x = 5), (y = 3), (z = -2)", "a three by three system is solved exactly");
        t.equal(status(s), "solved and verified", "and verified");
        t.check(s.broken.empty(), "the three by three record passes the invariant pass" + first_broken(s));
    }
    {
        const Solved s = run("[y = 2, x + y = 5]", "[x, y]");
        t.equal(s.answer, "(x = 3), (y = 2)", "a zero in the first pivot position is handled");
        t.check(has_rule(s, "matrix.row-swap"), "by swapping a row with a nonzero entry into place");
    }
    {
        const Solved s = run("[2x + 4y = 1, x - y = 0]", "[x, y]");
        t.equal(s.answer, "(x = (1 * (6^-1))), (y = (1 * (6^-1)))", "fractional answers stay exact");
        t.check(has_rule(s, "matrix.row-scale"), "and a pivot is scaled to one");
    }
    {
        const Solved s = run("[x + y = 2, 0 = 0, x - y = 0]", "[x, y]");
        t.equal(outcome(s), "solved", "an equation that always holds is a zero row");
        t.equal(s.answer, "(x = 1), (y = 1)", "and does not change the solution");
    }
}

void test_no_solution(TestSink &t) {
    {
        const Solved s = run("[x + y = 1, 2x + 2y = 3]", "[x, y]");
        t.equal(outcome(s), "no solution", "parallel equations have no common solution");
        t.check(s.result.solutions.empty(), "and no values are offered");
        t.check(has_rule(s, "system.inconsistent-row"),
                "the reduced matrix shows the row that reads zero equals a nonzero number");
        t.equal(status(s), "solved and verified", "and the empty solution set is a verified answer");
        t.check(s.broken.empty(), "the record passes the invariant pass" + first_broken(s));
    }
    {
        const Solved s = run("[x = 1, 0 = 3]", "[x]");
        t.equal(outcome(s), "no solution", "an equation that never holds leaves no solution");
    }
}

void test_family(TestSink &t) {
    const Solved s = run("[x + y + z = 2, x - y = 0]", "[x, y, z]");
    t.equal(outcome(s), "solution family", "fewer independent equations than unknowns leaves a family");
    t.equal(s.answer, "(x = (1 + ((-1 * (2^-1)) * z))), (y = (1 + ((-1 * (2^-1)) * z))), (z = z)",
            "written with the free unknown as its own parameter");
    t.check(has_rule(s, "system.check-family-by-sampling"),
            "the family is checked by substituting several values of the free unknown");
    t.equal(s.last_strength, "numerically corroborated",
            "which is sampling rather than proof, and the recorded strength says so");
    t.equal(status(s), "solved and verified", "with every check in the record passing");
    t.check(s.broken.empty(), "the record passes the invariant pass" + first_broken(s));
}

void test_refusals(TestSink &t) {
    t.equal(outcome(run("[x*y = 1, x + y = 2]", "[x, y]")), "not linear",
            "a product of unknowns is refused as not linear");
    t.equal(outcome(run("[a*x + y = 1, x - y = 0]", "[x, y]")), "outside envelope",
            "a symbolic coefficient is outside the rational envelope");
    t.equal(outcome(run("[0.5x + y = 1, x - y = 0]", "[x, y]")), "outside envelope",
            "a decimal coefficient is outside the exact envelope");
    t.equal(outcome(run("[x = 1, y = 1, z = 1, w = 1, x + y = 2]", "[x, y, z, w]")),
            "outside envelope", "five equations exceed the four row matrix envelope");
    t.equal(outcome(run("[a + b + c + d + e + f = 1]", "[a, b, c, d, e, f]")), "outside envelope",
            "six unknowns exceed the six column augmented matrix");
    t.equal(outcome(run("[x + y = 1, x - y = 0]", "[x, x]")), "invalid input",
            "an unknown named twice is invalid");
    t.equal(outcome(run("[x + y, x - y = 0]", "[x, y]")), "invalid input",
            "every item has to be an equation");
    t.equal(outcome(run("x + y = 1", "[x, y]")), "invalid input", "the equations have to be a list");
    t.equal(outcome(run("[x + y = 1]", "[x, 2]")), "invalid input", "every unknown has to be a symbol");
    t.equal(outcome(run("[x + y = 3, x - y = 1]", "[x, y]", Budget(), NumericMode::Decimal)),
            "outside envelope", "a decimal numeric mode is refused rather than rounded");
}

Solved by_substitution(const char *equations, const char *unknowns, const Budget &budget = Budget()) {
    return run(equations, unknowns, budget, NumericMode::Exact, SystemMethod::Substitution);
}

void test_substitution(TestSink &t) {
    {
        const Solved s = by_substitution("[x + y = 3, x - y = 1]", "[x, y]");
        t.equal(outcome(s), "solved", "substitution solves a two by two system");
        t.equal(s.answer, "(x = 2), (y = 1)", "with the same answer elimination gives");
        t.check(has_rule(s, "plan.system-substitution") && has_rule(s, "system.isolate-unknown") &&
                    has_rule(s, "system.substitute") && has_rule(s, "system.back-substitute"),
                "by solving for one unknown, substituting it and working back");
        t.check(!has_rule(s, "system.augmented-matrix") && !has_rule(s, "matrix.row-add-multiple"),
                "and without writing a matrix");
        t.equal(s.last_strength, "candidate checked", "then checks the answer in every original equation");
        t.equal(status(s), "solved and verified", "and every step is verified");
        t.check(s.broken.empty(), "the substitution record passes the invariant pass" + first_broken(s));
        t.evidence("ALG-013", s.result.outcome == SystemOutcome::Solved && s.answer == "(x = 2), (y = 1)",
                   "a linear system is solved with explicit substitution steps and checked in every equation");
    }
    {
        const Solved s = by_substitution("[x + y + z = 6, 2y + 5z = -4, 2x + 5y - z = 27]", "[x, y, z]");
        t.equal(s.answer, "(x = 5), (y = 3), (z = -2)", "substitution solves a three by three system exactly");
        t.equal(status(s), "solved and verified", "and verifies it");
        t.check(s.broken.empty(), "the three by three substitution passes the invariant pass" + first_broken(s));
    }
    {
        const Solved s = by_substitution("[2x + 4y = 1, x - y = 0]", "[x, y]");
        t.equal(s.answer, "(x = (1 * (6^-1))), (y = (1 * (6^-1)))", "substitution keeps fractions exact");
    }
    {
        const Solved s = by_substitution("[y = 2, x + y = 5]", "[x, y]");
        t.equal(s.answer, "(x = 3), (y = 2)", "an equation missing the first unknown is solved for the one it has");
    }
    {
        const Solved s = by_substitution("[x + y = 1, 2x + 2y = 3]", "[x, y]");
        t.equal(outcome(s), "no solution", "substitution finds that parallel equations have no solution");
        t.check(has_rule(s, "system.contradiction") && s.result.solutions.empty(),
                "from an equation that is false for every value");
        t.check(s.broken.empty(), "the contradiction record passes the invariant pass" + first_broken(s));
    }
    {
        const Solved s = by_substitution("[x + y = 2, 2x + 2y = 4, x - y = 0]", "[x, y]");
        t.equal(s.answer, "(x = 1), (y = 1)", "a dependent equation is dropped once it holds for every value");
        t.check(has_rule(s, "system.identity-equation"), "and the drop is recorded");
    }
    {
        const Solved s = by_substitution("[x + y + z = 2, x - y = 0]", "[x, y, z]");
        const Solved e = run("[x + y + z = 2, x - y = 0]", "[x, y, z]");
        t.equal(outcome(s), "solution family", "substitution leaves a family when unknowns stay free");
        t.equal(s.answer, e.answer, "and writes the same family elimination does");
        t.equal(s.last_strength, "numerically corroborated", "checked by sampling the free unknown");
        t.check(s.broken.empty(), "the family record passes the invariant pass" + first_broken(s));
    }
    {
        Budget short_budget;
        short_budget.max_steps = 3;
        const Solved halted = by_substitution("[x + y = 3, x - y = 1]", "[x, y]", short_budget);
        t.equal(outcome(halted), "resource exceeded", "a substitution that runs out of steps says so");
        t.check(halted.result.solutions.empty() && halted.broken.empty(),
                "and keeps only a verified prefix" + first_broken(halted));
    }
}

bool cancel_now(void *) {
    return true;
}

void test_budgets(TestSink &t) {
    Budget cancelling;
    cancelling.poll = cancel_now;
    const Solved cancelled = run("[x + y = 3, x - y = 1]", "[x, y]", cancelling);
    t.equal(outcome(cancelled), "cancelled", "a cancelled solve says it was cancelled");
    t.equal(status(cancelled), "cancelled", "with the cancelled status");
    t.check(cancelled.result.solutions.empty(), "and offers nothing");
    const Solved cancelled_substitution =
        run("[x + y = 3, x - y = 1]", "[x, y]", cancelling, NumericMode::Exact, SystemMethod::Substitution);
    t.equal(outcome(cancelled_substitution), "cancelled", "a cancelled substitution says it was cancelled");
    t.check(cancelled_substitution.result.solutions.empty(), "and offers nothing either");

    Budget short_budget;
    short_budget.max_steps = 3;
    const Solved halted = run("[x + y = 3, x - y = 1]", "[x, y]", short_budget);
    t.equal(outcome(halted), "resource exceeded", "a step budget that runs out is a resource limit");
    t.equal(status(halted), "resource limit reached", "and not a failed verification");
    t.check(halted.result.solutions.empty(), "with no answer offered");
    t.check(halted.broken.empty(), "and the kept prefix passes the invariant pass" + first_broken(halted));
}

}  // namespace

void run_system_tests(TestSink &sink) {
    test_reader(sink);
    test_unique(sink);
    test_no_solution(sink);
    test_family(sink);
    test_refusals(sink);
    test_substitution(sink);
    test_budgets(sink);
}

}  // namespace nps
