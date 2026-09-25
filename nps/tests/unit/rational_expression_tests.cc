#include <initializer_list>
#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/rational_expression.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

Poly poly(std::initializer_list<int64_t> low_to_high) {
    Poly out(low_to_high.size());
    size_t i = 0;
    for (int64_t c : low_to_high)
        mpq_set_si(out.at(i++), c, 1);
    out.trim();
    return out;
}

std::string text(const Poly &p) {
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        Rational value;
        if (i)
            out += " ";
        if (!detail::mpq_get_rational(p.at(i), &value))
            return "too large";
        out += std::to_string(value.num);
        if (value.den != 1)
            out += "/" + std::to_string(value.den);
    }
    return out.empty() ? "0" : out;
}

void test_polynomials(TestSink &t) {
    Poly g;
    t.check(poly_gcd(poly({-1, 0, 1}), poly({-1, 1}), &g) && text(g) == "-1 1",
            "the gcd of x^2 - 1 and x - 1 is x - 1");
    t.check(poly_gcd(poly({2, 3, 1}), poly({6, 5, 1}), &g) && text(g) == "2 1",
            "the gcd of x^2 + 3x + 2 and x^2 + 5x + 6 is x + 2");
    t.check(poly_gcd(poly({2, 2}), poly({4}), &g) && text(g) == "1",
            "polynomials with no common root have gcd 1, made monic");
    Poly q, r;
    t.check(poly_divide(poly({-1, 0, 0, 1}), poly({-1, 1}), &q, &r) && text(q) == "1 1 1" && r.zero(),
            "x^3 - 1 divides by x - 1 exactly into x^2 + x + 1");
    t.check(poly_divide(poly({1, 0, 1}), poly({-1, 1}), &q, &r) && text(q) == "1 1" && text(r) == "2",
            "x^2 + 1 over x - 1 leaves quotient x + 1 and remainder 2");
    t.check(!poly_divide(poly({1, 1}), Poly(), &q, &r), "dividing by the zero polynomial is refused");

    Budget budget;
    Meter meter(budget);
    std::vector<Rational> roots;
    std::vector<int> multiplicities;
    Poly rest;
    t.check(poly_rational_roots(poly({-1, -1, 2}), meter, &roots, &multiplicities, &rest) == RootSearch::Found &&
                roots.size() == 2 && rest.degree() == 0,
            "2x^2 - x - 1 has the two rational roots 1 and -1/2 and nothing left over");
    bool half = false;
    for (const Rational &root : roots)
        half = half || (root.num == -1 && root.den == 2);
    t.check(half, "and one of them is the fraction -1/2");
    t.check(poly_rational_roots(poly({2, -3, 0, 1}), meter, &roots, &multiplicities, &rest) == RootSearch::Found &&
                roots.size() == 2 && ((multiplicities[0] == 2) != (multiplicities[1] == 2)),
            "(x - 1)^2 (x + 2) has two distinct roots, one of them repeated");
    t.check(poly_rational_roots(poly({1, 0, 1}), meter, &roots, &multiplicities, &rest) == RootSearch::Found &&
                roots.empty() && text(rest) == "1 0 1",
            "x^2 + 1 has no rational root and is left whole");
    t.check(poly_rational_roots(poly({0, -1, 0, 1}), meter, &roots, &multiplicities, &rest) == RootSearch::Found &&
                roots.size() == 3,
            "x^3 - x has the root zero as well as 1 and -1");

    Poly big = poly({1, 1});
    mpz_ui_pow_ui(mpq_numref(big.at(0)), 2, 400);
    Poly squared;
    t.check(!poly_mul(big, big, &squared), "a product whose coefficients outgrow the bit bound is refused");
}

struct Run {
    RationalResult result;
    std::string answer;
    std::string assumptions;
    std::vector<std::string> rules;
    std::vector<std::string> broken;
};

Run run(const char *expression, RationalGoal goal = RationalGoal::Normal, const Budget &budget = Budget()) {
    Run out;
    Arena arena;
    Derivation d;
    const ParseResult parsed = parse(arena, expression);
    if (!parsed.ok()) {
        out.answer = "the test's own input did not parse";
        return out;
    }
    out.result = rational_expression(arena, d, parsed.root, arena.symbol("x"), goal, budget);
    if (out.result.expression != kNoNode)
        out.answer = print(arena, out.result.expression);
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
    return rational_outcome_name(r.result.outcome);
}

std::string status(const Run &r) {
    return derivation_status_name(r.result.status);
}

std::string broken(const Run &r) {
    return r.broken.empty() ? std::string() : ", got " + r.broken.front();
}

void test_normal(TestSink &t) {
    {
        const Run r = run("(x^2-1)/(x-1)");
        t.equal(outcome(r), "rewritten", "a fraction with a common factor is reduced");
        t.equal(r.answer, "(x + 1)", "to the quotient with the common factor cancelled");
        t.equal(status(r), "solved and verified", "and every step and the final check pass");
        t.check(has_rule(r, "rat.excluded-values") && has_rule(r, "rat.cancel-common-factor") &&
                    has_rule(r, "rat.check-equivalent"),
                "the record names the excluded values, the cancellation and the equivalence check");
        t.check(r.assumptions.find("x") != std::string::npos && r.assumptions.find("1") != std::string::npos,
                "and the value the cancelled factor excluded is still published: " + r.assumptions);
        t.check(r.broken.empty(), "the record passes the invariant pass" + broken(r));
        t.evidence("ALG-005", r.result.outcome == RationalOutcome::Rewritten && r.answer == "(x + 1)" &&
                                  !r.assumptions.empty(),
                   "a cancellation that changes the domain keeps the excluded value as a published condition");
    }
    {
        const Run r = run("x/x");
        t.equal(r.answer, "1", "x over x is 1");
        t.check(!r.assumptions.empty(), "only where x is not zero, which is published: " + r.assumptions);
    }
    {
        const Run r = run("1/x + 1/(x+1)");
        t.equal(r.answer, "(((2 * x) + 1) * (((x^2) + x)^-1))", "a sum of fractions goes over a common denominator");
        t.check(has_rule(r, "rat.common-denominator"), "recorded as a common denominator");
        t.check(r.broken.empty(), "the sum record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("(x+1)/(x-2) * (x-2)/(x+3)");
        t.equal(r.answer, "((x + 1) * ((x + 3)^-1))", "a product of fractions multiplies across and cancels");
        t.check(has_rule(r, "rat.multiply"), "recorded as multiplying numerators and denominators");
        t.check(r.assumptions.find("2") != std::string::npos,
                "and the cancelled x - 2 still excludes 2: " + r.assumptions);
    }
    {
        const Run r = run("x/(x^2-4) - 1/(x-2)");
        t.equal(r.answer, "(-2 * (((x^2) + -4)^-1))", "a difference with a shared factor reduces fully");
    }
    {
        const Run r = run("(2x+2)/(4x+4)");
        t.equal(r.answer, "(1 * (2^-1))", "a constant ratio reduces to that constant");
    }
    t.equal(outcome(run("sqrt(x)/x")), "not rational", "a square root of the variable is not rational");
    t.equal(outcome(run("sin(x)/x")), "not rational", "a function of the variable is not rational");
    t.equal(outcome(run("a*x/x")), "outside envelope", "a second symbol is outside the one-variable envelope");
    t.equal(outcome(run("0.5*x/x")), "outside envelope", "a decimal is outside the exact envelope");
    t.equal(outcome(run("x^13/x")), "outside envelope", "a degree above the bound is refused");
    t.equal(outcome(run("1/(x-x)")), "invalid input", "a denominator that is identically zero is invalid");
    {
        const Run r = run("(2^400*x^2)*(2^400*x)/(x-1)");
        t.equal(outcome(r), "resource exceeded", "coefficients beyond the bit bound are a resource limit");
    }
}

void test_partial_fractions(TestSink &t) {
    const RationalGoal pf = RationalGoal::PartialFractions;
    {
        const Run r = run("1/(x^2-1)", pf);
        t.equal(outcome(r), "rewritten", "a proper fraction over distinct linear factors splits");
        t.equal(r.answer, "(((1 * (2^-1)) * ((x + -1)^-1)) + ((-1 * (2^-1)) * ((x + 1)^-1)))",
                "into one fraction for each factor");
        t.check(has_rule(r, "pf.cover-up") && has_rule(r, "pf.decompose") && !has_rule(r, "pf.divide"),
                "by the cover-up rule, with no division for a proper fraction");
        t.equal(status(r), "solved and verified", "and every coefficient and the final check pass");
        t.check(r.assumptions.find("1") != std::string::npos, "the excluded values are published: " + r.assumptions);
        t.check(r.broken.empty(), "the partial fraction record passes the invariant pass" + broken(r));
        t.evidence("ALG-005", r.result.outcome == RationalOutcome::Rewritten && has_rule(r, "pf.cover-up") &&
                                  !r.assumptions.empty(),
                   "partial fractions keep the excluded values of the denominator as published conditions");
    }
    {
        const Run r = run("(3x+5)/(x^2+4x+3)", pf);
        t.equal(r.answer, "((1 * ((x + 1)^-1)) + (2 * ((x + 3)^-1)))", "the numerators come out as integers when they are");
    }
    {
        const Run r = run("(x^3+x)/(x^2-1)", pf);
        t.check(has_rule(r, "pf.divide"), "an improper fraction is divided first");
        t.equal(r.answer, "(x + (1 * ((x + -1)^-1)) + (1 * ((x + 1)^-1)))", "and its polynomial part leads the sum");
        t.check(r.broken.empty(), "the division record passes the invariant pass" + broken(r));
    }
    {
        const Run r = run("(x^2-1)/(x-1)", pf);
        t.equal(r.answer, "(x + 1)", "a fraction that reduces to a polynomial has no fractions left");
    }
    {
        const Run r = run("1/(x^2+1)", pf);
        t.equal(outcome(r), "outside envelope", "an irreducible quadratic factor is refused");
        t.check(r.result.detail.find("irreducible") != std::string::npos, "and the refusal names it");
        t.check(r.rules.empty(), "before anything is recorded");
    }
    {
        const Run r = run("1/(x-1)^2", pf);
        t.equal(outcome(r), "outside envelope", "a repeated factor is refused");
        t.check(r.result.detail.find("repeated") != std::string::npos, "and the refusal names it");
    }
}

bool cancel_now(void *) {
    return true;
}

void test_budgets(TestSink &t) {
    Budget cancelling;
    cancelling.poll = cancel_now;
    const Run cancelled = run("(x^2-1)/(x-1)", RationalGoal::Normal, cancelling);
    t.equal(outcome(cancelled), "cancelled", "a cancelled rewrite says so");
    t.check(cancelled.result.expression == kNoNode, "and offers no expression");
    Budget short_budget;
    short_budget.max_steps = 2;
    const Run halted = run("(x^2-1)/(x-1)", RationalGoal::Normal, short_budget);
    t.equal(outcome(halted), "resource exceeded", "a step budget that runs out is a resource limit");
    t.check(halted.result.expression == kNoNode && halted.broken.empty(),
            "with no expression and a verified prefix" + broken(halted));
}

}  // namespace

void run_rational_expression_tests(TestSink &sink) {
    test_polynomials(sink);
    test_normal(sink);
    test_partial_fractions(sink);
    test_budgets(sink);
}

}  // namespace nps
