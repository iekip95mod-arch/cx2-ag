#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/power.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

PowerReading compare(const char *left, const char *right, std::string *why = nullptr) {
    Arena arena;
    const ParseResult a = parse(arena, left);
    const ParseResult b = parse(arena, right);
    if (!a.ok() || !b.ok())
        return PowerReading::Unreadable;
    std::string detail;
    const PowerReading reading = power_equivalent(arena, a.root, b.root, &detail);
    if (why)
        *why = detail;
    return reading;
}

void test_equivalence(TestSink &t) {
    t.check(compare("sqrt(x^2)", "abs(x)") == PowerReading::Equal, "the square root of a square is the absolute value");
    t.check(compare("sqrt(x^2)", "x") == PowerReading::Different, "and not the variable, which differs for negative x");
    t.check(compare("sqrt(x)*sqrt(x)", "x") == PowerReading::Equal,
            "a square root times itself is the variable where the root is defined");
    t.check(compare("x^(1/2)*x^(1/3)", "x^(5/6)") == PowerReading::Equal, "rational exponents add");
    t.check(compare("(x^3)^(1/3)", "x") == PowerReading::Equal, "an odd root undoes an odd power for every x");
    t.check(compare("x^3/x", "x^2") == PowerReading::Equal, "a quotient of powers subtracts exponents");
    t.check(compare("x^2*x^3", "x^6") == PowerReading::Different, "and adding them wrongly is caught");
    t.check(compare("(-8)^(1/3)", "-2") == PowerReading::Equal, "the real cube root of a negative number");
    t.check(compare("sqrt(12)", "2*sqrt(3)") == PowerReading::Equal, "an irrational root of a number is compared exactly");
    t.check(compare("sqrt(12)", "5*sqrt(3)") == PowerReading::Different, "and a wrong multiple of it is caught");
    t.check(compare("sqrt(2*x)", "7*sqrt(2*x)") == PowerReading::Different,
            "an irrational coefficient under a root does not make every comparison pass");
    t.check(compare("sqrt(2)+sqrt(3)", "sqrt(5)") == PowerReading::Different, "roots of different numbers do not merge");
    t.check(compare("(-16)^(1/3)", "-2*2^(1/3)") == PowerReading::Equal &&
                compare("(-16)^(1/3)", "2*2^(1/3)") == PowerReading::Different,
            "an irrational odd root of a negative number is negative");
    t.check(compare("sqrt(8)", "2*sqrt(2)") == PowerReading::Equal &&
                compare("sqrt(8)", "2*sqrt(3)") == PowerReading::Different,
            "a root is compared prime by prime rather than by its size");
    t.check(compare("sqrt(1000000007)", "sqrt(1000000007)") == PowerReading::Equal,
            "a large prime under a root is its own radical");
    {
        std::string why;
        const PowerReading old_side = compare("sqrt(1000000007*1000000009)", "sqrt(1000000007*1000000009)", &why);
        t.check(old_side == PowerReading::Unreadable && why.find("cannot compute") != std::string::npos,
                "and a product of two past the trial bound is not guessed: " + why);
        const PowerReading new_side = compare("x", "x + 0*sqrt(1000000007*1000000009)", &why);
        t.check(new_side == PowerReading::Unreadable && why.find("new form") != std::string::npos,
                "nor is a new form that cannot be computed read as different: " + why);
    }
    t.check(compare("sqrt(-4)", "0") == PowerReading::Unreadable, "a form with no real value anywhere is not read as equal");
    t.check(compare("x*y", "x*y") == PowerReading::Unreadable, "two variables are not read");
    t.check(compare("sin(x)", "sin(x)") == PowerReading::Unreadable, "a function other than sqrt or abs is not read");
}

struct Run {
    PowerResult result;
    std::string answer;
    std::string assumptions;
    std::vector<std::string> rules;
    std::vector<std::string> broken;
    bool equivalent = false;
};

Run run(const char *expression, const Budget &budget = Budget()) {
    Run out;
    Arena arena;
    Derivation d;
    const ParseResult parsed = parse(arena, expression);
    if (!parsed.ok()) {
        out.answer = "the test's own input did not parse";
        return out;
    }
    out.result = simplify_powers(arena, d, parsed.root, budget);
    if (out.result.expression != kNoNode) {
        out.answer = print(arena, out.result.expression);
        std::string why;
        out.equivalent = power_equivalent(arena, parsed.root, out.result.expression, &why) == PowerReading::Equal;
    }
    for (const std::string &assumption : d.context.active_assumptions)
        out.assumptions += assumption + "; ";
    for (size_t i = 0; i < d.size(); ++i)
        out.rules.push_back(d.at(static_cast<StepId>(i)).rule_id);
    invariants::Pass audit;
    audit.walk(arena, d, false, true, &out.broken);
    return out;
}

bool has_rule(const Run &r, const char *rule) {
    for (const std::string &id : r.rules) {
        if (id == rule)
            return true;
    }
    return false;
}

std::string outcome(const Run &r) {
    return power_outcome_name(r.result.outcome);
}

std::string status(const Run &r) {
    return derivation_status_name(r.result.status);
}

std::string broken(const Run &r) {
    return r.broken.empty() ? std::string() : ", got " + r.broken.front();
}

void test_laws(TestSink &t) {
    {
        const Run r = run("sqrt(x^2)");
        t.equal(outcome(r), "rewritten", "the square root of a square simplifies");
        t.equal(r.answer, "abs(x)", "to the absolute value, not to x");
        t.check(has_rule(r, "pow.power-of-power") && has_rule(r, "pow.check-values"),
                "by the power of a power law, and the result is checked");
        t.equal(status(r), "solved and verified", "and every step and the check pass");
        t.check(r.broken.empty(), "the record passes the invariant pass" + broken(r));
        t.evidence("ALG-006", r.result.outcome == PowerOutcome::Rewritten && r.answer == "abs(x)" && r.equivalent,
                   "a radical is simplified with its real-domain sign condition kept");
    }
    {
        const Run r = run("sqrt(x)*sqrt(x)");
        t.equal(r.answer, "x", "a square root times itself is the variable");
        t.check(r.assumptions.find("x") != std::string::npos && !r.assumptions.empty(),
                "on the condition that x is not negative, which is published: " + r.assumptions);
        t.check(has_rule(r, "pow.same-base"), "by adding the exponents of a common base");
    }
    {
        const Run r = run("x^3/x");
        t.equal(r.answer, "(x^2)", "a quotient of powers subtracts the exponents");
        t.check(!r.assumptions.empty(), "and keeps x not zero: " + r.assumptions);
    }
    t.equal(run("x^2*x^3").answer, "(x^5)", "a product of powers adds the exponents");
    t.equal(run("(x^2)^3").answer, "(x^6)", "a power of a power multiplies them");
    t.equal(run("(x^3)^(1/3)").answer, "x", "an odd root of an odd power needs no condition");
    t.check(run("(x^3)^(1/3)").assumptions.empty(), "and records none");
    {
        const Run r = run("sqrt(12)");
        t.equal(r.answer, "(2 * sqrt(3))", "the largest square factor comes out of a root");
        t.check(has_rule(r, "pow.numeric-root"), "by the rule for a root of a number");
        t.equal(status(r), "solved and verified", "and the irrational result is verified exactly");
    }
    {
        const Run r = run("sqrt(18*x^2)");
        t.check(r.equivalent && status(r) == "solved and verified" && r.broken.empty(),
                "a root with an irrational coefficient and a square is verified: " + r.answer + broken(r));
        t.check(r.answer.find("abs(x)") != std::string::npos && r.answer.find("sqrt(2)") != std::string::npos,
                "and keeps the root of two beside the absolute value: " + r.answer);
    }
    {
        const Run r = run("sqrt(2*x)*sqrt(2*x)");
        t.check(r.equivalent && status(r) == "solved and verified" && !r.assumptions.empty(),
                "a root of an irrational multiple times itself is verified with its condition: " + r.answer + " " +
                    r.assumptions);
    }
    t.equal(run("(-8)^(1/3)").answer, "-2", "the real cube root of a negative number is negative");
    {
        const Run r = run("(-16)^(1/3)");
        t.check(r.equivalent && status(r) == "solved and verified" && r.answer.find("-") != std::string::npos,
                "and an irrational one keeps its sign outside the root: " + r.answer);
    }
    {
        const Run r = run("sqrt(4*x^2)");
        t.equal(r.answer, "(2 * abs(x))", "a root of a product splits over its nonnegative factors");
        t.check(r.equivalent && r.broken.empty(), "and is checked" + broken(r));
    }
    {
        const Run r = run("x^(1/2)*x^(1/3)");
        t.check(r.equivalent && has_rule(r, "pow.same-base") && !r.assumptions.empty(),
                "rational exponents add with the nonnegative condition recorded: " + r.answer + " " + r.assumptions);
    }
    {
        const Run r = run("x^(3/3)");
        t.check(r.answer == "x" && has_rule(r, "pow.first-power"), "a typed first power is its base: " + r.answer);
    }
    t.equal(outcome(run("x^2")), "already in form", "a single power is already in form");
    {
        const Run r = run("sqrt(-4)");
        t.equal(outcome(r), "no real value", "an even root of a negative number has no real value");
        t.check(r.rules.empty(), "and nothing is recorded");
    }
    t.equal(outcome(run("(-4)^(1/2)")), "no real value", "nor does the same written as an exponent");
    {
        const Run r = run("x*sqrt(1000000007*1000000009)");
        t.equal(outcome(r), "outside envelope", "a check that cannot be computed exactly is a refusal, not a failure");
        t.check(r.result.status == DerivationStatus::Unsupported && r.result.expression == kNoNode,
                "and offers nothing: " + status(r));
    }
    {
        const Run r = run("x^1000000000*x");
        t.equal(outcome(r), "outside envelope", "an enormous exponent is outside the envelope");
        t.check(r.result.detail.find("1024") != std::string::npos && r.rules.empty(),
                "and is refused by its size before any step or check runs: " + r.result.detail);
        std::string why;
        const PowerReading reading = compare("x^2000", "x^2000", &why);
        t.check(reading == PowerReading::Unreadable && why.find("outside") != std::string::npos,
                "nor does the checker try to evaluate it: " + why);
    }
    t.equal(outcome(run("x*y")), "outside envelope", "two variables are outside the envelope");
    t.equal(outcome(run("sin(x)^2")), "outside envelope", "a function other than a root is outside it");
    t.equal(outcome(run("1.5*x")), "outside envelope", "a decimal is outside it");
}

bool cancel_now(void *) {
    return true;
}

void test_budgets(TestSink &t) {
    Budget cancelling;
    cancelling.poll = cancel_now;
    const Run cancelled = run("sqrt(x^2)", cancelling);
    t.equal(outcome(cancelled), "cancelled", "a cancelled rewrite says so");
    t.check(cancelled.result.expression == kNoNode, "and offers nothing");
    Budget short_budget;
    short_budget.max_steps = 2;
    const Run halted = run("sqrt(4*x^2)", short_budget);
    t.equal(outcome(halted), "resource exceeded", "a step budget that runs out is a resource limit");
    t.check(halted.result.expression == kNoNode && halted.broken.empty(),
            "with no expression and a verified prefix" + broken(halted));
}

}  // namespace

void run_power_tests(TestSink &sink) {
    test_equivalence(sink);
    test_laws(sink);
    test_budgets(sink);
}

}  // namespace nps
