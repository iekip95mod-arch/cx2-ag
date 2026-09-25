#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/trig.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

TrigReading compare(const char *left, const char *right) {
    Arena arena;
    const ParseResult a = parse(arena, left);
    const ParseResult b = parse(arena, right);
    if (!a.ok() || !b.ok())
        return TrigReading::Unreadable;
    std::string why;
    return trig_equivalent(arena, a.root, b.root, &why);
}

void test_equivalence(TestSink &t) {
    t.check(compare("sin(2x)", "2*sin(x)*cos(x)") == TrigReading::Equal, "sin 2x is 2 sin x cos x");
    t.check(compare("cos(2x)", "cos(x)^2 - sin(x)^2") == TrigReading::Equal, "cos 2x is cos squared less sin squared");
    t.check(compare("cos(2x)", "1 - 2*sin(x)^2") == TrigReading::Equal, "and one less twice sin squared");
    t.check(compare("sin(x)^2 + cos(x)^2", "1") == TrigReading::Equal, "the Pythagorean identity holds exactly");
    t.check(compare("sin(x+y)", "sin(x)*cos(y) + cos(x)*sin(y)") == TrigReading::Equal,
            "the angle-sum identity holds in two variables");
    t.check(compare("sin(x+y)", "sin(x)*cos(y) - cos(x)*sin(y)") == TrigReading::Different,
            "and the same identity with a sign flipped does not");
    t.check(compare("sin(x/2)^2", "(1 - cos(x))/2") == TrigReading::Equal, "the half-angle identity holds");
    t.check(compare("sin(-x)", "-sin(x)") == TrigReading::Equal && compare("cos(-x)", "cos(x)") == TrigReading::Equal,
            "sine is odd and cosine is even");
    t.check(compare("sin(3x)", "3*sin(x) - 4*sin(x)^3") == TrigReading::Equal, "the triple angle identity holds");
    t.check(compare("sin(3x)", "3*sin(x) - 3*sin(x)^3") == TrigReading::Different,
            "a triple angle with the wrong coefficient does not");
    t.check(compare("sin(x+1)", "sin(x+1)") == TrigReading::Unreadable, "a constant inside an angle is not read");
    t.check(compare("x*sin(x)", "x*sin(x)") == TrigReading::Unreadable, "a variable outside sine is not read");
    t.check(compare("tan(x)", "tan(x)") == TrigReading::Unreadable, "tan is not read");
}

struct Run {
    TrigResult result;
    std::string answer;
    std::vector<std::string> rules;
    std::vector<std::string> broken;
    bool equivalent = false;
};

Run run(const char *expression, TrigGoal goal, const Budget &budget = Budget()) {
    Run out;
    Arena arena;
    Derivation d;
    const ParseResult parsed = parse(arena, expression);
    if (!parsed.ok()) {
        out.answer = "the test's own input did not parse";
        return out;
    }
    out.result = trig_rewrite(arena, d, parsed.root, goal, budget);
    if (out.result.expression != kNoNode) {
        out.answer = print(arena, out.result.expression);
        std::string why;
        out.equivalent = trig_equivalent(arena, parsed.root, out.result.expression, &why) == TrigReading::Equal;
    }
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
    return trig_outcome_name(r.result.outcome);
}

std::string status(const Run &r) {
    return derivation_status_name(r.result.status);
}

std::string broken(const Run &r) {
    return r.broken.empty() ? std::string() : ", got " + r.broken.front();
}

void test_expand(TestSink &t) {
    {
        const Run r = run("sin(x+y)", TrigGoal::Expand);
        t.equal(outcome(r), "rewritten", "the sine of a sum expands");
        t.equal(r.answer, "((sin(x) * cos(y)) + (cos(x) * sin(y)))", "by the angle-sum identity");
        t.check(has_rule(r, "trig.angle-sum") && has_rule(r, "trig.check-identity"),
                "which is named, and the result is checked");
        t.equal(status(r), "solved and verified", "and every step and the check pass");
        t.check(r.broken.empty(), "the record passes the invariant pass" + broken(r));
        t.evidence("ALG-010", r.result.outcome == TrigOutcome::Rewritten && has_rule(r, "trig.angle-sum") && r.equivalent,
                   "a trigonometric identity is applied, named and checked exactly");
    }
    {
        const Run r = run("cos(x-y)", TrigGoal::Expand);
        t.check(r.equivalent && has_rule(r, "trig.angle-sum"), "the cosine of a difference expands the same way");
    }
    {
        const Run r = run("cos(2x)", TrigGoal::Expand);
        t.check(has_rule(r, "trig.double-angle") && r.equivalent, "a double angle expands by the double-angle identity");
        t.equal(status(r), "solved and verified", "and is verified");
    }
    {
        const Run r = run("sin(3x)", TrigGoal::Expand);
        t.check(has_rule(r, "trig.angle-sum") && has_rule(r, "trig.double-angle") && r.equivalent,
                "a triple angle splits into a sum and then a double angle");
        t.check(r.broken.empty(), "the triple angle record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("sin(-x) + cos(-2x)", TrigGoal::Expand);
        t.check(has_rule(r, "trig.odd") && has_rule(r, "trig.even") && r.equivalent,
                "a negative angle uses the odd and even identities");
    }
    t.equal(outcome(run("sin(x)*cos(y)", TrigGoal::Expand)), "already in form", "nothing to expand is already in form");
    {
        const Run r = run("sin(x+1)", TrigGoal::Expand);
        t.equal(outcome(r), "outside envelope", "a constant inside an angle is outside the envelope");
        t.check(r.rules.empty(), "and is refused before anything is recorded");
    }
    t.equal(outcome(run("x*sin(x)", TrigGoal::Expand)), "outside envelope", "a variable outside sine is refused");
    t.equal(outcome(run("tan(x+y)", TrigGoal::Expand)), "outside envelope", "tan is refused");
    t.equal(outcome(run("x^2+1", TrigGoal::Expand)), "not trigonometric", "an expression with no sine or cosine is not trigonometric");
}

void test_collect(TestSink &t) {
    {
        const Run r = run("sin(x)^2 + cos(x)^2", TrigGoal::Collect);
        t.equal(r.answer, "1", "sine squared plus cosine squared collects to one");
        t.check(has_rule(r, "trig.pythagorean"), "by the Pythagorean identity");
        t.equal(status(r), "solved and verified", "and is verified");
        t.check(r.broken.empty(), "the Pythagorean record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("sin(x)*cos(x)", TrigGoal::Collect);
        t.check(has_rule(r, "trig.double-angle-product") && r.equivalent,
                "a sine times the cosine of the same angle collects by the double angle read backwards");
    }
    {
        const Run r = run("sin(x)^2", TrigGoal::Collect);
        t.check(has_rule(r, "trig.half-angle") && r.equivalent, "a square reduces by the half-angle identity");
        t.check(r.answer.find("cos((2 * x))") != std::string::npos, "to a cosine of the double angle: " + r.answer);
    }
    {
        const Run r = run("2*sin(x)^2 + cos(2x)", TrigGoal::Collect);
        t.equal(r.answer, "1", "twice sine squared plus cosine 2x collects to one");
        t.check(has_rule(r, "trig.collect"), "with the like terms collected at the end");
        t.check(r.broken.empty(), "the collecting record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("sin(x)*cos(y)", TrigGoal::Collect);
        t.equal(outcome(r), "outside envelope", "a product of different angles needs product-to-sum, which is refused");
        t.check(r.result.detail.find("product") != std::string::npos, "and the refusal says so");
    }
    t.equal(outcome(run("sin(x)^3", TrigGoal::Collect)), "outside envelope", "a power above two is refused");
    t.equal(outcome(run("cos(2x)", TrigGoal::Collect)), "already in form", "a linear form is already collected");
}

bool cancel_now(void *) {
    return true;
}

void test_budgets(TestSink &t) {
    Budget cancelling;
    cancelling.poll = cancel_now;
    const Run cancelled = run("sin(x+y)", TrigGoal::Expand, cancelling);
    t.equal(outcome(cancelled), "cancelled", "a cancelled rewrite says so");
    t.check(cancelled.result.expression == kNoNode, "and offers nothing");
    Budget short_budget;
    short_budget.max_steps = 2;
    const Run halted = run("sin(3x)", TrigGoal::Expand, short_budget);
    t.equal(outcome(halted), "resource exceeded", "a step budget that runs out is a resource limit");
    t.check(halted.result.expression == kNoNode && halted.broken.empty(),
            "with no expression and a verified prefix" + broken(halted));
}

}  // namespace

void run_trig_tests(TestSink &sink) {
    test_equivalence(sink);
    test_expand(sink);
    test_collect(sink);
    test_budgets(sink);
}

}  // namespace nps
