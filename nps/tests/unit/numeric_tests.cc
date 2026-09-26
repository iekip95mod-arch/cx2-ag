#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/numeric.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

struct Run {
    NumericResult result;
    std::string value;
    std::string bound;
    std::vector<std::string> rules;
    std::vector<std::string> broken;
    std::string assumptions;
};

Run run(const char *text, const Budget &budget = Budget()) {
    Run out;
    Arena arena;
    Derivation d;
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        out.value = "the test's own input did not parse";
        return out;
    }
    out.result = numeric_method(arena, d, parsed.root, budget);
    if (out.result.value != kNoNode)
        out.value = print(arena, out.result.value);
    if (out.result.bound != kNoNode)
        out.bound = print(arena, out.result.bound);
    for (size_t i = 0; i < d.size(); ++i)
        out.rules.push_back(d.at(static_cast<StepId>(i)).rule_id);
    for (const std::string &a : d.context.active_assumptions)
        out.assumptions += a + "; ";
    invariants::Pass audit;
    audit.walk(arena, d, false, true, &out.broken);
    return out;
}

size_t count(const Run &r, const char *rule) {
    size_t n = 0;
    for (const std::string &id : r.rules)
        n += id == rule ? 1 : 0;
    return n;
}

std::string outcome(const Run &r) {
    return numeric_outcome_name(r.result.outcome);
}

std::string status(const Run &r) {
    return derivation_status_name(r.result.status);
}

std::string broken(const Run &r) {
    return r.broken.empty() ? std::string() : ", got " + r.broken.front();
}

void test_bisection(TestSink &t) {
    {
        const Run r = run("bisect(x^2-2, x, 1, 2, 1/100)");
        t.equal(outcome(r), "approximated", "bisection brackets the square root of two");
        t.equal(r.value, "(181 * (128^-1))", "and answers the midpoint of the last bracket: " + r.value);
        t.equal(r.bound, "(1 * (128^-1))", "with half the bracket width as its bound");
        t.check(count(r, "num.bisect-halve") == 6 && count(r, "num.bisect-check") == 1,
                "each of the six halvings is a step and the last bracket is checked");
        t.check(r.result.bound_certified && status(r) == "numerically approximated",
                "the bound is certified and the answer is marked approximate: " + status(r));
        t.check(r.broken.empty(), "the bisection record passes the invariant pass" + broken(r));
        t.check(r.assumptions.find("continuous") != std::string::npos, "and names continuity: " + r.assumptions);
        t.evidence("VER-018", r.result.outcome == NumericOutcome::Approximated && r.result.bound_certified &&
                                   count(r, "num.bisect-halve") == 6,
                   "a bisection records every halving, its sign-change precondition, its stopping bound and the certificate");
    }
    {
        const Run r = run("bisect(x^2-4, x, 0, 4, 1/10)");
        t.check(outcome(r) == "approximated" && r.value == "2" && r.bound == "0",
                "a midpoint that is an exact root ends the search with a zero bound: " + r.value + " " + r.bound);
    }
    {
        const Run r = run("bisect(x^2+1, x, -1, 1, 1/10)");
        t.equal(outcome(r), "no sign change", "no sign change between the ends is refused");
        t.check(r.rules.empty() && r.result.value == kNoNode, "before anything is recorded");
    }
    t.equal(outcome(run("bisect(x^2-2, x, 2, 1, 1/10)")), "invalid input", "a reversed bracket is invalid");
    t.equal(outcome(run("bisect(x^2-2, x, 1, 2, 0)")), "invalid input", "a zero tolerance is invalid");
    t.equal(outcome(run("bisect(sin(x), x, 1, 4, 1/10)")), "outside envelope", "a function other than a polynomial is refused");
    t.equal(outcome(run("bisect(x*y, x, 1, 4, 1/10)")), "outside envelope", "and so is a second variable");
    {
        const Run r = run("bisect(x^2-2, x, 1, 2, 1/2^60)");
        t.equal(outcome(r), "did not converge", "a tolerance past the halving cap does not converge");
        t.evidence("VER-018", r.result.value == kNoNode && count(r, "num.bisect-halve") > 0 && r.broken.empty(),
                   "a method that cannot meet its tolerance keeps its verified iterations and offers no answer" + broken(r));
    }
}

void test_newton(TestSink &t) {
    {
        const Run r = run("newtonroot(x^2-2, x, 1, 1/1000)");
        t.equal(outcome(r), "approximated", "Newton's method finds the square root of two");
        t.check(count(r, "num.newton-derivative") == 1 && count(r, "num.newton-iterate") >= 3 &&
                    count(r, "num.newton-check") == 1,
                "recording the derivative, each iterate and the final check");
        t.check(r.result.bound_certified && r.bound == "(1 * (1000^-1))",
                "the sign change across the tolerance proves the bound: " + r.bound);
        t.equal(r.value, "(707107 * (500000^-1))", "at 1.414214, each iterate rounded to millionths: " + r.value);
        t.check(r.broken.empty(), "the Newton record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("newtonroot(x^2, x, 1, 1/1000)");
        t.equal(outcome(r), "approximated", "a double root still returns the iterate");
        t.check(!r.result.bound_certified && status(r) == "solved but unchecked",
                "but no sign change proves it, so the answer is not called verified: " + status(r));
    }
    {
        const Run r = run("newtonroot(x^2-2, x, 0, 1/1000)");
        t.equal(outcome(r), "did not converge", "a zero derivative at the start stops the method");
        t.check(r.result.detail.find("derivative is zero") != std::string::npos, "and says why: " + r.result.detail);
    }
    {
        const Run r = run("newtonroot(x^2+1, x, 2, 1/1000)");
        t.equal(outcome(r), "did not converge", "a polynomial with no real root never converges");
        t.check(count(r, "num.newton-iterate") > 0 && r.result.value == kNoNode,
                "and its iterates stay on the record without an answer");
    }
}

void test_quadrature(TestSink &t) {
    {
        const Run r = run("trapsum(x^2, x, 0, 1, 4)");
        t.equal(outcome(r), "approximated", "the trapezoid rule approximates an integral");
        t.equal(r.value, "(11 * (32^-1))", "as eleven thirty-seconds on four intervals");
        t.equal(r.bound, "(1 * (96^-1))", "with the bound (b-a) h^2 max|f''| / 12");
        t.check(r.result.bound_certified && count(r, "num.quad-check") == 1,
                "and the exact integral is inside the bound");
        t.check(r.broken.empty(), "the trapezoid record passes the invariant pass" + broken(r));
        t.evidence("VER-018", r.result.bound_certified && count(r, "num.quad-bound") == 1,
                   "a quadrature rule states its error bound and checks it against the exact integral");
    }
    {
        const Run r = run("simpsum(x^3, x, 0, 2, 2)");
        t.check(outcome(r) == "approximated" && r.value == "4" && r.bound == "0",
                "Simpson's rule is exact on a cubic, and its bound says so: " + r.value + " " + r.bound);
    }
    {
        const Run r = run("simpsum(x^4, x, 0, 1, 2)");
        t.check(r.value == "(5 * (24^-1))" && r.bound == "(1 * (120^-1))" && r.result.bound_certified,
                "Simpson on a quartic is off by a certified amount: " + r.value + " " + r.bound);
    }
    t.equal(outcome(run("simpsum(x^2, x, 0, 1, 3)")), "invalid input", "Simpson needs an even number of intervals");
    t.equal(outcome(run("trapsum(x^2, x, 0, 1, 0)")), "invalid input", "and a count of at least one");
    t.equal(outcome(run("trapsum(x^2, x, 0, 1, 100000)")), "outside envelope", "a count past the cap is refused");
}

bool cancel_now(void *) {
    return true;
}

void test_budgets(TestSink &t) {
    Budget cancelling;
    cancelling.poll = cancel_now;
    const Run cancelled = run("bisect(x^2-2, x, 1, 2, 1/100)", cancelling);
    t.check(outcome(cancelled) == "cancelled" && cancelled.result.value == kNoNode, "a cancelled method offers nothing");
    Budget short_budget;
    short_budget.max_steps = 3;
    const Run halted = run("bisect(x^2-2, x, 1, 2, 1/100)", short_budget);
    t.check(outcome(halted) == "resource exceeded" && halted.result.value == kNoNode && halted.broken.empty(),
            "a step budget that runs out keeps a verified prefix" + broken(halted));
}

}  // namespace

void run_numeric_tests(TestSink &sink) {
    test_bisection(sink);
    test_newton(sink);
    test_quadrature(sink);
    test_budgets(sink);
}

}  // namespace nps
