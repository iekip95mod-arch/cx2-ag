#include <string>

#include "nps/steps/linear.h"
#include "nps/core/parser.h"
#include "nps/core/canonical.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

struct Solved {
    SolveOutcome outcome;
    std::string solution;
    size_t steps;
    bool all_verified;
    std::string detail;
    std::string status;
    std::string verifications;
};

Solved run(const std::string &equation, const char *unknown) {
    Arena arena;
    Derivation d;
    NodeId eq = parse(arena, equation).root;
    NodeId x = arena.symbol(unknown);
    SolveResult r = solve_linear(arena, d, eq, x);

    Solved out;
    out.outcome = r.outcome;
    out.solution = r.solution == kNoNode ? "" : print(arena, r.solution);
    out.steps = d.size();
    out.detail = r.detail;
    out.status = derivation_status_name(r.status);
    out.verifications = verification_transcript(d);
    out.all_verified = true;
    for (size_t i = 0; i < d.size(); ++i) {
        if (!d.at(static_cast<StepId>(i)).verified())
            out.all_verified = false;
    }
    return out;
}

bool always_cancel(void *) { return true; }

Derivation derive(const char *equation, const Budget &budget = Budget()) {
    Arena arena;
    Derivation d;
    solve_linear(arena, d, parse(arena, equation).root, arena.symbol("x"), budget);
    return d;
}

// VER-010 for this family. Each case is also held to what its kind says about the derivation.
void test_rule_cases(TestSink &t) {
    using nps_tools::RuleCaseKind;
    const char *const kRules[] = {"eq.linear.inverse-operations", "eq.collect-like-terms",
                                  "eq.divide-both-sides", "eq.linear.check-by-substitution",
                                  "eq.linear.inspect-collected-coefficient"};

    const Derivation squared = derive("x^2 = 4");
    const Derivation denominator = derive("1/x = 2");
    t.rule_case(kRules[0], RuleCaseKind::Negative, squared,
                squared.context.derivation_status == DerivationStatus::Unsupported,
                "the linear strategy refuses a squared unknown, a degree-two neighbour");
    t.rule_case(kRules[0], RuleCaseKind::Negative, denominator,
                denominator.context.derivation_status == DerivationStatus::Unsupported,
                "the linear strategy refuses an unknown in a denominator");

    // The widest coefficient an int64 holds, divided out and substituted back.
    const Derivation widest = derive("9223372036854775807x = 9223372036854775807");
    for (size_t i = 0; i < 4; ++i)
        t.rule_case(kRules[i], RuleCaseKind::Boundary, widest,
                    widest.context.derivation_status == DerivationStatus::SolvedAndVerified,
                    std::string(kRules[i]) + " solves at the widest int64 coefficient");
    // A zero coefficient against a zero constant sits between no solution and every value.
    const Derivation zeros = derive("0x = 0");
    t.rule_case("eq.linear.inspect-collected-coefficient", RuleCaseKind::Boundary, zeros,
                zeros.context.derivation_status == DerivationStatus::SolvedAndVerified,
                "a zero coefficient and a zero constant are read as every value");

    // A quotient past int64 once the sides are collected, which #14 found reported as a shape.
    const Derivation quotient = derive("x + 4611686018427387904*(-2) = 0");
    for (size_t i = 0; i < 2; ++i)
        t.rule_case(kRules[i], RuleCaseKind::Regression, quotient,
                    quotient.context.derivation_status == DerivationStatus::ResourceLimitReached,
                    std::string(kRules[i]) +
                        " runs before a quotient past int64 is refused as a resource limit, #14");

    // The validator itself, each case in its own sink so a refusal is read, not counted.
    const Derivation solved = derive("2x + 5 = 13");
    Budget tight;
    tight.max_steps = 1;
    const Derivation halted = derive("2x + 5 = 13", tight);
    // Built by hand, because no engine records a failed check under an answering status.
    Derivation contradicted;
    {
        Step step;
        step.rule_id = "eq.divide-both-sides";
        VerificationRecord failed_check;
        failed_check.method = "substitution";
        failed_check.outcome = VerificationOutcome::Failed;
        step.verifications.push_back(failed_check);
        contradicted.add_transformation(kNoStep, std::move(step), TransformationPayload());
        contradicted.context.derivation_status = DerivationStatus::SolvedAndVerified;
    }
    t.rule_case("eq.divide-both-sides", RuleCaseKind::Negative, contradicted, true,
                "a division whose substitution check failed is recorded as failed");
    const struct {
        const char *rule;
        RuleCaseKind kind;
        const Derivation *derivation;
        const char *what;
        bool holds;
        const char *about;
    } validator[] = {
        {"eq.divide-both-sides", RuleCaseKind::Positive, &solved, "a", true,
         "a positive case holds where the rule ran under an answer"},
        {"eq.divide-both-sides", RuleCaseKind::Positive, &squared, "a", false,
         "and not where it never ran"},
        {"eq.linear.inspect-collected-coefficient", RuleCaseKind::Positive, &solved, "a", false,
         "nor for a rule that never ran, though the derivation it is absent from did answer"},
        {"eq.linear.inverse-operations", RuleCaseKind::Positive, &quotient, "a", false,
         "nor where it ran and the derivation carries no answer"},
        {"eq.divide-both-sides", RuleCaseKind::Positive, &contradicted, "a", false,
         "nor where its own check failed"},
        {"eq.divide-both-sides", RuleCaseKind::Negative, &contradicted, "a", true,
         "a negative case holds where the rule ran and its own check failed"},
        {"eq.linear.inspect-collected-coefficient", RuleCaseKind::Negative, &solved, "a", false,
         "a negative case does not hold for a rule simply absent from an answered derivation"},
        {"eq.divide-both-sides", RuleCaseKind::Negative, &solved, "a", false,
         "nor for a rule that ran and passed"},
        {"eq.linear.inverse-operations", RuleCaseKind::Negative, &halted, "a", false,
         "nor for a derivation stopped by its budget, which is not a refusal"},
        {"eq.divide-both-sides", RuleCaseKind::Negative, &squared, "a", false,
         "nor for a rule the strategy's refusal never reached"},
        {"matrix.det-row-swap", RuleCaseKind::Negative, &squared, "a", false,
         "nor for a rule that is not a strategy at all"},
        {"eq.quadratic.square-root", RuleCaseKind::Negative, &denominator, "a", false,
         "nor for a strategy of another family, whose preconditions never even ran"},
        {"eq.linear.inverse-operations", RuleCaseKind::Negative, &squared, "a", true,
         "while the strategy whose preconditions refused does hold it"},
        {"eq.divide-both-sides", RuleCaseKind::Boundary, &squared, "a", false,
         "a boundary case has to reach the rule"},
        {"eq.divide-both-sides", RuleCaseKind::Regression, &solved, "no issue named", false,
         "a regression case has to name its issue"},
        {"eq.divide-both-sides", RuleCaseKind::Regression, &squared, "#14", false,
         "and has to reach the rule"},
        {"eq.divide-both-sides", RuleCaseKind::Regression, &solved, "#14", true,
         "and holds when it does both"},
    };
    for (const auto &c : validator) {
        TestSink probe;
        probe.rule_case(c.rule, c.kind, *c.derivation, true, c.what);
        // The check and the recorded row each carry the verdict, since either reaches a gate.
        const bool agreed = probe.failures.empty() == c.holds && probe.rule_cases.size() == 1 &&
                            probe.rule_cases[0].passed == c.holds;
        t.check(agreed, std::string("rule case validator: ") + c.about);
    }
}

bool cancel_after_exact_solve(void *context) {
    return static_cast<Derivation *>(context)->size() == 4;
}

}  // namespace

void run_linear_tests(TestSink &t) {
    test_rule_cases(t);
    for (const bool replay : {false, true}) {
        Arena arena;
        const NodeId equation = parse(arena, "2*x=1").root;
        const NodeId variable = arena.symbol("x");
        Derivation remembered;
        if (replay)
            solve_linear(arena, remembered, equation, variable);
        for (const size_t steps : {size_t{3}, size_t{4}, size_t{5}}) {
            Derivation derivation;
            derivation.request.numeric_mode = NumericMode::Decimal;
            if (replay)
                derivation.share_runs(remembered);
            Budget budget;
            budget.max_steps = steps;
            const SolveResult result = solve_linear(arena, derivation, equation, variable, budget);
            const bool completed = steps == 5;
            t.check(result.status == (completed ? DerivationStatus::SolvedAndVerified
                                               : DerivationStatus::ResourceLimitReached) &&
                        result.status == derivation.context.derivation_status &&
                        result.outcome == (completed ? SolveOutcome::Solved : SolveOutcome::ResourceExceeded),
                    "decimal reporting and cache replay honor the terminal step limit");
            t.check(completed ? result.solution != kNoNode && print(arena, result.solution) == "0.5"
                              : result.solution == kNoNode && derivation.size() > 0 &&
                                    derivation.all_verified_from(0),
                    "a refused decimal report retains checked work without an exact fallback answer");
            t.check(!replay || result.cost.replayed > 0, "the decimal replay guard exercises cached steps");
        }
        Derivation cancelled;
        cancelled.request.numeric_mode = NumericMode::Decimal;
        if (replay)
            cancelled.share_runs(remembered);
        Budget budget;
        budget.poll = cancel_after_exact_solve;
        budget.poll_context = &cancelled;
        const SolveResult result = solve_linear(arena, cancelled, equation, variable, budget);
        t.check(result.outcome == SolveOutcome::Cancelled && result.status == DerivationStatus::Cancelled &&
                    result.status == cancelled.context.derivation_status && result.solution == kNoNode &&
                    cancelled.size() == 4 && cancelled.all_verified_from(0),
                "cancellation before decimal reporting retains the checked exact prefix without an answer");
    }
    {
        Arena arena;
        Derivation derivation;
        Budget budget;
        budget.max_steps = 0;
        const SolveResult result = solve_linear(arena, derivation, parse(arena, "0.5*x=1").root,
                                                arena.symbol("x"), budget);
        t.check(result.outcome == SolveOutcome::ResourceExceeded &&
                    result.status == DerivationStatus::ResourceLimitReached &&
                    result.status == derivation.context.derivation_status && result.solution == kNoNode,
                "decimal admission reports its exhausted step budget rather than unsupported input");
        budget.poll = always_cancel;
        Derivation cancelled;
        const SolveResult stopped = solve_linear(arena, cancelled, parse(arena, "0.5*x=1").root,
                                                 arena.symbol("x"), budget);
        t.check(stopped.outcome == SolveOutcome::Cancelled && stopped.status == DerivationStatus::NotRecorded &&
                    stopped.solution == kNoNode && cancelled.size() == 0,
                "cancelled decimal admission stays distinct from a resource or unsupported refusal");
    }
    {
        // A decimal past the exact rational's capacity is room running out, not unsupported input.
        Arena arena;
        Derivation d;
        const SolveResult r = solve_linear(arena, d, parse(arena, "0.1234567890123456789*x=1").root,
                                           arena.symbol("x"));
        t.equal(solve_outcome_name(r.outcome), "resource exceeded",
                "a decimal past the exact rational's capacity is refused as a resource limit");
        t.equal(derivation_status_name(r.status), "resource limit reached",
                "and the status says so, which is what the backend gate reads");
        t.check(r.detail.find("more digits") != std::string::npos,
                "with the refusal naming the capacity it exceeded");
        Derivation exponent;
        Arena second;
        const SolveResult property =
            solve_linear(second, exponent, parse(second, "x^0.5=1").root, second.symbol("x"));
        t.equal(derivation_status_name(property.status), "unsupported",
                "while a decimal exponent stays a property of the input rather than a limit");
    }
    for (const char *source : {"[1,2]=x", "x=0*[1]", "x+0*[1]=2",
                               "x=f([1])", "0*[1]=0", "x=[0.5]"}) {
        Arena arena;
        const NodeId equation = parse(arena, source).root;
        const NodeId unknown = arena.symbol("x");
        Derivation derivation;
        const SolveResult result = solve_linear(arena, derivation, equation, unknown);
        t.check(result.status == DerivationStatus::Unsupported && result.solution == kNoNode &&
                    derivation.size() == 0 && result.cost.rewrites == 0,
                std::string("linear solving refuses collections before scalar work: ") + source);
    }
    {
        Arena arena;
        const NodeId list = arena.list({arena.integer("1"), arena.integer("2")});
        const NodeId equation = arena.binary(Kind::Equals, list, arena.integer("2"));
        Meter meter{Budget()};
        Rational coefficient{7, 1}, constant{9, 1};
        t.check(linear_form(arena, equation, list, meter, &coefficient, &constant) ==
                    LinearForm::NotLinear && coefficient.num == 7 && constant.num == 9 &&
                    meter.cost().rewrites == 0,
                "a list cannot masquerade as an opaque scalar subject in linear analysis");
        const NodeId ordinary = parse(arena, "x=2").root;
        t.check(linear_form(arena, ordinary, list, meter, &coefficient, &constant) ==
                    LinearForm::NotLinear,
                "linear analysis also rejects a list subject absent from the equation");
    }
    {
        struct Case {
            const char *input;
            const char *variable;
            const char *before;
            const char *after;
            const char *action;
        };
        for (const Case &example : {
                 Case{"2x+5=13", "x", "2x-8=0", "x=4",
                      "Add 8 to both sides, then divide both sides by 2"},
                 Case{"-3y+5=0", "y", "-3y+5=0", "y=5/3",
                      "Subtract 5 from both sides, then divide both sides by -3"},
                 Case{"z+7=0", "z", "z+7=0", "z=-7", "Subtract 7 from both sides"},
                 Case{"3t=0", "t", "3t=0", "t=0", "Divide both sides by 3"},
                 Case{"x=0", "x", "x=0", "x=0", "Read off x = 0"},
                 Case{"q/2-1/3=0", "q", "q/2-1/3=0", "q=2/3",
                      "Add 1/3 to both sides, then divide both sides by 0.5"}}) {
            Arena arena;
            Derivation d;
            const NodeId input = parse(arena, example.input).root;
            const SolveResult solved = solve_linear(arena, d, input, arena.symbol(example.variable));
            bool found = false;
            for (size_t index = 0; index < d.size(); ++index) {
                const StepId id = static_cast<StepId>(index);
                if (d.at(id).rule_id != "eq.divide-both-sides") continue;
                found = true;
                const TransformationPayload *step = d.transformation(id);
                t.check(step && d.at(id).verified(), "the isolation instruction belongs to a verified transformation");
                if (!step) continue;
                t.check(canonicalize(arena, step->before) == canonicalize(arena, parse(arena, example.before).root) &&
                            canonicalize(arena, step->after) == canonicalize(arena, parse(arena, example.after).root),
                        "the isolation instruction connects the independently expected equations");
                t.equal(step->concrete_action, example.action, std::string("isolation describes every operation for ") + example.input);
            }
            t.check(found && solved.outcome == SolveOutcome::Solved, "the teaching case reaches linear isolation");
        }
    }
    {
        // A node cap tight enough that the solution never gets built. Reported as solved with a
        // kNoNode solution it reached the bridge as a result with an empty answer, which is what
        // the fuzzer's rewrite-result check calls a solved outcome carrying no solution.
        size_t solved_without_solution = 0;
        size_t ran_out = 0;
        for (size_t cap = 3; cap <= 48; ++cap) {
            Limits limits;
            limits.max_nodes = cap;
            Arena arena(limits);
            Derivation d;
            const NodeId equation = parse(arena, "x = 5").root;
            const NodeId unknown = arena.symbol("x");
            const SolveResult r = solve_linear(arena, d, equation, unknown);
            if (arena.failed())
                ++ran_out;
            if (r.outcome == SolveOutcome::Solved && r.solution == kNoNode)
                ++solved_without_solution;
        }
        t.check(ran_out > 0, "the sweep reaches a node cap the solve cannot fit, so it is testing "
                             "the case");
        t.check(solved_without_solution == 0,
                "and no cap produces a solved outcome carrying no solution");
    }
    {
        Arena arena;
        const NodeId equation = parse(arena, "x = 20.0 - 24.0").root;
        const NodeId unknown = arena.symbol("x");
        Rational value;
        Precision precision;
        t.check(linear_solution_precision(arena, equation, unknown, {}, &value, &precision),
                "the linear analyser carries precision through a decimal difference");
        t.check(value.num == -4 && value.den == 1 && precision.significant_digits == 2 &&
                    precision.last_significant_decimal_place == -1,
                "cancellation changes 20.0 minus 24.0 from three figures to two");

        const NodeId quotient = parse(arena, "y = 20.0 / 4.00").root;
        const NodeId y = arena.symbol("y");
        t.check(linear_solution_precision(arena, quotient, y, {}, &value, &precision) &&
                    value.num == 5 && value.den == 1 && precision.significant_digits == 3 &&
                    precision.last_significant_decimal_place == -2,
                "division retains the fewer significant-figure count");

        const NodeId positive_power = parse(arena, "p = 2.0^3").root;
        const NodeId p = arena.symbol("p");
        t.check(linear_solution_precision(arena, positive_power, p, {}, &value, &precision) &&
                    value.num == 8 && value.den == 1 && precision.significant_digits == 2 &&
                    precision.last_significant_decimal_place == -1,
                "a positive constant power keeps product significant-figure semantics");

        const NodeId negative_power = parse(arena, "q = 2.0^(-3)").root;
        const NodeId q = arena.symbol("q");
        t.check(linear_solution_precision(arena, negative_power, q, {}, &value, &precision) &&
                    value.num == 1 && value.den == 8 && precision.significant_digits == 2 &&
                    precision.last_significant_decimal_place == -2,
                "a negative constant power keeps reciprocal product precision");

        const NodeId positive_boundary = parse(arena, "b = 2^62").root;
        const NodeId b = arena.symbol("b");
        t.check(linear_solution_precision(arena, positive_boundary, b, {}, &value, &precision) &&
                    value.num == INT64_C(4611686018427387904) && value.den == 1 &&
                    precision.kind == NumberKind::Exact,
                "a positive constant power reaches the exact numerator boundary");

        const NodeId negative_boundary = parse(arena, "c = 2^(-62)").root;
        const NodeId c = arena.symbol("c");
        t.check(linear_solution_precision(arena, negative_boundary, c, {}, &value, &precision) &&
                    value.num == 1 && value.den == INT64_C(4611686018427387904) &&
                    precision.kind == NumberKind::Exact,
                "a negative constant power reaches the exact denominator boundary");

        value = Rational{17, 19};
        precision.kind = NumberKind::Measured;
        precision.significant_digits = 7;
        precision.last_significant_decimal_place = -5;
        const NodeId outside_boundary = parse(arena, "r = 2^63").root;
        const NodeId r = arena.symbol("r");
        t.check(!linear_solution_precision(arena, outside_boundary, r, {}, &value, &precision),
                "a constant power outside the rational boundary is refused");
        t.check(value.num == 17 && value.den == 19 && precision.kind == NumberKind::Measured &&
                    precision.significant_digits == 7 &&
                    precision.last_significant_decimal_place == -5,
                "a refused constant power leaves both outputs unchanged");

        const NodeId reciprocal_outside_boundary = parse(arena, "s = 2^(-63)").root;
        const NodeId s = arena.symbol("s");
        t.check(!linear_solution_precision(arena, reciprocal_outside_boundary, s, {}, &value,
                                           &precision) &&
                    value.num == 17 && value.den == 19,
                "a reciprocal outside the denominator boundary is refused atomically");
    }
    {
        Arena arena;
        Derivation d;
        NodeId equation = parse(arena, "x = 4").root;
        Budget cancelling;
        cancelling.poll = always_cancel;
        SolveResult r = solve_linear(arena, d, equation, arena.symbol("x"), cancelling);
        t.equal(solve_outcome_name(r.outcome), "cancelled", "an existing cancel stops a short solve");
        t.check(r.solution == kNoNode, "and no solution is returned");
        t.check(d.size() == 0, "with no partial derivation left behind");
    }

    {
        Solved s = run("2x + 5 = 13", "x");
        t.equal(solve_outcome_name(s.outcome), "solved", "the PRD's worked example solves");
        t.equal(s.solution, "4", "and gives the right answer");
        t.check(s.steps == 4, "as a plan, two transformations and a check");
        t.check(s.all_verified, "with every claim carrying a passed verification");
        // all_verified reads the outcomes and never the sentences, so a detail that reverts to a
        // constant leaves it true. This pins what each of the six checks came back with.
        t.equal(s.verifications,
                "passed, both sides reduced to a coefficient and constant term | "
                "passed, all coefficients and constants are exact rationals | "
                "passed, every registered strategy precondition has passed evidence | "
                "passed, the same quantity was added to both sides, which cannot change the "
                "solution set | "
                "passed, removing the constant preserves equality, and the coefficient 2 was checked to be non-zero | "
                "passed, both sides came out equal",
                "and each of them says which check it was and what it found");
        t.evidence("ALG-003", s.outcome == SolveOutcome::Solved && s.solution == "4" &&
                                  s.steps == 4 && s.all_verified,
                   "the single-variable linear worked example solves exactly with verified steps");
    }

    t.equal(run("x = 4", "x").solution, "4", "an equation already solved stays solved");
    t.equal(run("x + 1 = 0", "x").solution, "-1", "a negative answer keeps its sign");
    t.equal(run("2x = 1", "x").solution, "(1 * (2^(-1)))", "a fractional answer stays exact");
    t.equal(run("3x + 1 = x + 7", "x").solution, "3", "unknowns on both sides collect");
    t.equal(run("-x = 5", "x").solution, "-5", "a negated unknown solves");
    t.equal(run("2(x + 3) = 10", "x").solution, "2", "a bracketed product expands through the rule");
    t.equal(run("x/2 = 3", "x").solution, "6", "a division by a constant solves");
    t.equal(run("6 = 2x", "x").solution, "3", "the unknown may be on the right");

    {
        Solved s = run("x + 1 = x + 2", "x");
        t.equal(solve_outcome_name(s.outcome), "no solution",
                "an equation the unknown cancels out of has no solution");
        t.check(s.solution.empty(), "and offers no answer");
    }
    {
        Solved s = run("x + 1 = x + 1", "x");
        t.equal(solve_outcome_name(s.outcome), "true for every value",
                "an identity is true for every value rather than solved");
    }
    {
        Solved s = run("x^2 = 4", "x");
        t.equal(solve_outcome_name(s.outcome), "not linear in the unknown",
                "a quadratic is refused rather than mangled into a linear answer");
        t.check(!s.detail.empty(), "and says why");
    }
    {
        Solved s = run("x*x = 4", "x");
        t.equal(solve_outcome_name(s.outcome), "not linear in the unknown",
                "and so is a quadratic written as a product");
    }
    {
        Solved s = run("x + y = 4", "x");
        t.equal(solve_outcome_name(s.outcome), "not linear in the unknown",
                "a second unknown is refused rather than treated as a constant");
    }
    {
        Solved s = run("1/x = 4", "x");
        t.equal(solve_outcome_name(s.outcome), "not linear in the unknown",
                "the unknown in a denominator is refused");
        // Clearing this denominator is the third operation STEP-007 names, and multiplying through
        // by x is exactly the move that would admit x = 0 as a candidate the original never had.
        // This solver does not make it, so there is no candidate to check.
        t.evidence("STEP-007", s.solution.empty() && s.steps == 0,
                   "an equation whose denominator holds the unknown is refused rather than cleared, "
                   "so no root arrives that the original form excludes");
    }
    {
        // A base of magnitude one with a vast exponent has to fold without spinning the analyse loop
        // once per unit. The point of these two is that they return at all.
        Solved s = run("x + (-1)^9223372036854775807 = 0", "x");
        t.equal(solve_outcome_name(s.outcome), "solved", "an odd power of minus one folds and solves");
        t.equal(s.solution, "1", "and gives x = 1 rather than hanging");
    }
    {
        Solved s = run("x + 0^9223372036854775807 = 0", "x");
        t.equal(solve_outcome_name(s.outcome), "solved", "a vast power of zero folds and solves");
        t.equal(s.solution, "0", "and gives x = 0 rather than hanging");
    }
    {
        Solved s = run("2x + 5", "x");
        t.equal(solve_outcome_name(s.outcome), "not an equation",
                "an expression is not an equation and says so");
    }

    {
        // The derivation is the product, so check it reads as one rather than only that the answer
        // is right.
        Arena arena;
        Derivation d;
        NodeId eq = parse(arena, "2x + 5 = 13").root;
        solve_linear(arena, d, eq, arena.symbol("x"));

        t.check(d.roots().size() == 1, "the plan is the single root");
        const bool has_plan = d.roots().size() == 1;
        StepId plan = has_plan ? d.roots()[0] : kNoStep;
        t.check(has_plan && d.plan(plan) != nullptr, "the root is a plan record");
        t.check(has_plan && d.at(plan).children.size() == 3, "with three children hanging off it");

        const bool has_expected_children = has_plan && d.at(plan).children.size() == 3;
        StepId first = has_expected_children ? d.at(plan).children[0] : kNoStep;
        StepId second = has_expected_children ? d.at(plan).children[1] : kNoStep;
        const TransformationPayload *p =
            has_expected_children ? d.transformation(first) : nullptr;
        const TransformationPayload *q =
            has_expected_children ? d.transformation(second) : nullptr;
        t.check(p != nullptr, "the first child is a transformation");
        if (p) {
            t.equal(print(arena, p->before), "(((2 * x) + 5) = 13)", "starting from the input");
            t.equal(print(arena, p->after), "(((2 * x) + -8) = 0)", "and collecting to one side");
            t.check(p->reversible, "and the move is reversible");
        }

        StepId last = has_expected_children ? d.at(plan).children[2] : kNoStep;
        const CheckPayload *c = has_expected_children ? d.check(last) : nullptr;
        t.check(c != nullptr, "the last child is a check");
        if (c) {
            t.evidence("VER-003", c->observed_result, "both sides are equal",
                       "and the answer was substituted back rather than asserted");
            t.evidence("ALG-012", c->observed_result, "both sides are equal",
                       "the final candidate is substituted into the equation as it was typed, so "
                       "the check cannot agree with a miscollection it shares");
        }
        t.evidence("STEP-001",
                   has_expected_children && d.plan(plan) != nullptr && p != nullptr &&
                       d.transformation(second) != nullptr && c != nullptr &&
                       d.at(first).rule_id == "eq.collect-like-terms" &&
                       d.at(second).rule_id == "eq.divide-both-sides",
                   "the ordered derivation is the plan, explicit rule applications and final check");
        t.evidence("STEP-017",
                   has_expected_children && d.plan(plan) != nullptr && c != nullptr &&
                       d.at(last).verified(),
                   "the linear walkthrough exposes its plan first and verified check last");
        t.evidence("VER-002",
                   has_expected_children && p != nullptr && p->reversible &&
                       d.at(first).claim == ClaimType::SolutionSetPreserved && d.at(first).verified(),
                   "the equality transformation records reversibility and solution-set verification");
        t.evidence("STEP-006",
                   p != nullptr && q != nullptr && p->reversible && q->reversible &&
                       d.at(first).claim == ClaimType::SolutionSetPreserved &&
                       d.at(second).claim == ClaimType::SolutionSetPreserved,
                   "the linear solver marks both equality-preserving moves as reversible");
    }

    {
        // Exact canonicalization makes this large rational solution fit after reduction.
        Arena arena;
        Derivation d;
        NodeId eq = parse(arena, "4000000000x + 3999999999 = 0").root;
        SolveResult r = solve_linear(arena, d, eq, arena.symbol("x"));
        t.equal(solve_outcome_name(r.outcome), "solved", "a large exact fraction solves");
        t.check(r.solution != kNoNode, "and the exact answer is offered");
        if (r.solution != kNoNode)
            t.equal(print(arena, r.solution), "(-3999999999 * (4000000000^(-1)))",
                    "without approximating the quotient");
        bool any_failed = false;
        bool all_verified = true;
        for (size_t i = 0; i < d.size(); ++i) {
            const Step &s = d.at(static_cast<StepId>(i));
            any_failed = any_failed || s.has_failed_verification();
            all_verified = all_verified && s.verified();
        }
        t.check(!any_failed, "and exact substitution records no verification failure");
        t.check(all_verified, "so every claim is verified");
        t.equal(derivation_status_name(r.status), "solved and verified",
                "with the matching derivation status");
        t.evidence("MATH-002",
                   r.outcome == SolveOutcome::Solved && r.solution != kNoNode &&
                       print(arena, r.solution) == "(-3999999999 * (4000000000^(-1)))" &&
                       !any_failed && all_verified,
                   "large exact rational arithmetic remains symbolic through verified substitution");
    }

    {
        // STEP-025 on the other solver. Two of the four steps fit in the budget, and those two stay:
        // section 15's closing paragraph asks every non-success outcome to keep its verified prefix,
        // and PERF-013 names restoring the last checked state as one of its two acceptable ends.
        // What must not survive is the answer, and the two claims below are about that rather than
        // about the record being empty.
        Arena arena;
        Derivation d;
        NodeId eq = parse(arena, "2x + 5 = 13").root;
        Budget tight;
        tight.max_steps = 2;
        SolveResult r = solve_linear(arena, d, eq, arena.symbol("x"), tight);
        t.equal(solve_outcome_name(r.outcome), "resource exceeded",
                "a step budget stops the solve");
        t.check(d.size() > 0, "keeping the moves that were checked");
        t.check(d.all_verified_from(0), "each of which passed");
        t.check(r.solution == kNoNode, "with no answer offered");
        t.equal(derivation_status_name(d.context.derivation_status), "resource limit reached",
                "and the context records why");
        t.evidence("STEP-025",
                   d.size() > 0 && d.all_verified_from(0) && r.solution == kNoNode &&
                       d.context.derivation_status == DerivationStatus::ResourceLimitReached,
                   "a solve that stopped early keeps the prefix that was checked and offers no "
                   "terminal answer for the goal it did not reach");
        t.evidence("PERF-004",
                   r.outcome == SolveOutcome::ResourceExceeded && r.solution == kNoNode &&
                       d.all_verified_from(0) &&
                       d.context.derivation_status == DerivationStatus::ResourceLimitReached,
                   "a complexity limit returns a typed refusal that offers no answer and leaves "
                   "only checked work in the record; the memory half of \"memory or complexity\" is "
                   "not evidenced here because Budget carries no memory limit to exceed");
        // PERF-013's resource end. The cancellation and backend error ends carry their own rows.
        t.evidence("PERF-013",
                   r.outcome == SolveOutcome::ResourceExceeded && d.size() > 0 &&
                       d.all_verified_from(0) && r.solution == kNoNode &&
                       d.context.derivation_status == DerivationStatus::ResourceLimitReached,
                   "a resource failure part way through a solve keeps every step that was checked, "
                   "offers no answer for the goal it did not reach and records why it stopped, "
                   "which is the last checked state the requirement asks to be restorable");
    }

    {
        Arena arena;
        Derivation d;
        NodeId eq = parse(arena, "2x + 5 = 13").root;
        SolveResult r = solve_linear(arena, d, eq, arena.symbol("x"));
        t.equal(solve_outcome_name(r.outcome), "solved", "the default budget solves it");
        t.equal(derivation_status_name(d.context.derivation_status), "solved and verified",
                "and the context says so");
        t.equal(d.context.problem_family_id, "algebra.linear-equation.one-unknown",
                "under the family that produced it");
        t.evidence("MATH-010", d.context.branch_convention == "real domain",
                   "the linear solver records the real default domain");
    }

    {
        // Exact intermediates may exceed int64 when their reduced numerator and denominator still fit.
        Solved large = run("4000000000x + 3999999999 = 0", "x");
        t.equal(solve_outcome_name(large.outcome), "solved",
                "a reducible exact product does not fail on its intermediate size");
        t.equal(large.solution, "(-3999999999 * (4000000000^(-1)))",
                "and keeps the negative rational exact");
        t.check(large.all_verified, "with exact substitution verified");

        Solved division = run("4000000000x = 3999999999", "x");
        t.equal(solve_outcome_name(division.outcome), "solved", "the bare exact division also solves");
        t.equal(division.solution, "(3999999999 * (4000000000^(-1)))",
                "and keeps the positive rational exact");
        t.check(division.all_verified, "with its substitution verified too");

        Solved cancels = run("4000000000x + 3999999999 = 3999999999x + 4000000000", "x");
        t.equal(solve_outcome_name(cancels.outcome), "solved",
                "coefficients that cancel to something small are still solved");
        t.equal(cancels.solution, "1", "and the answer is right rather than wrapped");
    }

    {
        // A constant that lands exactly on INT64_MIN reaches four negations that were unchecked:
        // the gcd, the sign fix in normalise, moving the right side over and the final -b/a. UBSan
        // flagged every one. The answer would be 2^63, so the honest outcome is a refusal.
        Solved s = run("x + 4611686018427387904*(-2) = 0", "x");
        t.equal(solve_outcome_name(s.outcome), "resource exceeded",
                "a constant at the int64 minimum refuses rather than negating through undefined behaviour");
        t.check(s.solution.empty(), "and offers no answer");
        t.check(s.detail.find("exact integer arithmetic") != std::string::npos, "naming the limit");

        Solved right = run("0 = x + 4611686018427387904*(-2)", "x");
        t.equal(solve_outcome_name(right.outcome), "resource exceeded",
                "and so does the same constant on the right side");
    }

    {
        // Arithmetic running out inside the analysis used to be reported in the words reserved for
        // an equation of the wrong shape, so a caller trying several equations was told the wrong
        // reason for the one it skipped.
        Solved s = run("x = 9000000000*4000000000", "x");
        t.equal(solve_outcome_name(s.outcome), "resource exceeded",
                "a product too large to hold refuses");
        t.check(s.detail.find("exact integer arithmetic") != std::string::npos,
                "and the reason is the arithmetic limit, not the shape");
        t.equal(s.status, "resource limit reached", "with the status a resource limit");

        Solved shape = run("x^2 = 4", "x");
        t.check(shape.detail.find("not linear in x") != std::string::npos,
                "while a genuine quadratic still says it is not linear");
        t.equal(shape.status, "unsupported", "under the unsupported status");
        t.equal(solve_outcome_name(shape.outcome), "not linear in the unknown",
                "and keeps the outcome that names a shape");
    }

    {
        // Each of the three capacity refusals reports running out of room rather than claiming the
        // equation is the wrong shape, which is what the backend gate reads to stop asking Giac.
        Solved analysis = run("x = 9000000000*4000000000", "x");
        t.equal(solve_outcome_name(analysis.outcome), "resource exceeded",
                "arithmetic running out inside the analysis is a capacity refusal");
        t.check(analysis.detail.find("a value grew past") != std::string::npos,
                "reported from the analysis rather than from a later stage");

        Solved coefficients = run("4611686018427387904*x = -4611686018427387904*x", "x");
        t.equal(solve_outcome_name(coefficients.outcome), "resource exceeded",
                "and so is a coefficient that will not fit once the sides are moved together");
        t.check(coefficients.detail.find("the coefficients grew past") != std::string::npos,
                "reported once the two sides are subtracted");

        Solved solution = run("x + 4611686018427387904*(-2) = 0", "x");
        t.equal(solve_outcome_name(solution.outcome), "resource exceeded",
                "and so is a quotient that will not fit at the last step");
        t.check(solution.detail.find("the solution did not fit") != std::string::npos,
                "reported from building the answer rather than from reading the equation");

        Solved shape = run("x + y = 4", "x");
        t.equal(solve_outcome_name(shape.outcome), "not linear in the unknown",
                "while an equation that is genuinely the wrong shape is untouched");
        t.equal(shape.status, "unsupported", "and stays unsupported rather than a resource limit");
    }

    {
        // The rules already work in exact rationals whatever the mode says, so the mode changes the
        // answer line and nothing above it.
        Arena arena;
        Derivation d;
        d.request.numeric_mode = NumericMode::Decimal;
        NodeId eq = parse(arena, "(2*x) = 1").root;
        SolveResult r = solve_linear(arena, d, eq, arena.symbol("x"));
        t.equal(solve_outcome_name(r.outcome), "solved", "decimal mode solves a linear equation");
        t.equal(r.solution == kNoNode ? std::string() : print(arena, r.solution), "0.5",
                "and reports the half as a decimal");
        t.equal(numeric_mode_name(d.context.numeric_mode), "decimal",
                "and the context records the mode it ran under");

        Solved exact = run("(2*x) = 1", "x");
        t.equal(exact.solution, "(1 * (2^(-1)))",
                "while exact mode, the default, leaves the same answer a fraction");

        // A third has no decimal spelling. Rounding it here would be the mode inventing digits.
        Arena recurring_arena;
        Derivation recurring;
        recurring.request.numeric_mode = NumericMode::Decimal;
        NodeId thirds = parse(recurring_arena, "(3*x) = 1").root;
        SolveResult third = solve_linear(recurring_arena, recurring, thirds,
                                         recurring_arena.symbol("x"));
        t.check(third.solution != kNoNode &&
                    print(recurring_arena, third.solution).find('.') == std::string::npos,
                "a solution with no terminating decimal stays a fraction in decimal mode");

        // Solving divides by the coefficient, so a typed decimal is read as a fraction first. The
        // reading has to be on the page: an answer of 2 from a typed 0.5 with no step saying how is
        // the silent reinterpretation the ruling that allows promotion was conditioned on.
        Arena typed_arena;
        Derivation typed;
        NodeId typed_eq = parse(typed_arena, "(0.5*x) = 1").root;
        SolveResult typed_result =
            solve_linear(typed_arena, typed, typed_eq, typed_arena.symbol("x"));
        t.equal(solve_outcome_name(typed_result.outcome), "solved",
                "exact mode solves an equation whose coefficient was typed as a decimal");
        t.equal(typed_result.solution == kNoNode ? std::string()
                                                 : print(typed_arena, typed_result.solution),
                "2", "and reaches the whole number the fraction gives");
        std::string typed_rules;
        for (size_t i = 0; i < typed.size(); ++i)
            typed_rules += typed.at(static_cast<StepId>(i)).rule_id + " ";
        t.check(typed_rules.find("num.decimal-to-rational") != std::string::npos,
                "with the reading of the decimal recorded as a step rather than done silently");
    }
}

}  // namespace nps
