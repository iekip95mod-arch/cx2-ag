#include "nps/steps/implicit.h"

#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/steps/command.h"
#include "golden/golden.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

struct Run {
    Arena arena;
    Derivation derivation;
    Command command;
    ImplicitResult result;
};

void implicit_run(Run &run, const std::string &text, const Budget &budget = Budget()) {
    run.derivation.request.original_expression = text;
    run.command = parse_command(run.arena, text, "x");
    run.result = implicit_differentiate(run.arena, run.derivation, run.command.expression, run.command.variable,
                                        run.command.dependent, budget);
}

bool value_at(Run &run, int64_t x, int64_t y, Rational expected) {
    Rational observed;
    return run.result.derivative != kNoNode &&
           evaluate_rational(run.arena, run.result.derivative, {{"x", {x, 1}}, {"y", {y, 1}}}, &observed) &&
           observed.num == expected.num && observed.den == expected.den;
}

}

void run_implicit_tests(TestSink &t) {
    // CALC-007. Each relation's derivative is read back at points where the textbook answer is known,
    // so a wrong collection or a lost chain rule factor fails here and not only in the record.
    struct Case {
        const char *golden;
        const char *text;
        int64_t x, y;
        Rational slope;
        DerivationStatus status;
    };
    for (const Case &fixture : {Case{"implicit_circle", "implicit(x^2+y^2=25,x,y)", 3, 4, {-3, 4}, DerivationStatus::SolvedAndVerified},
                                Case{"implicit_product", "implicit(x*y=1,x,y)", 2, 1, {-1, 2}, DerivationStatus::SolvedAndVerified},
                                Case{"implicit_mixed", "implicit(y^3+x*y=x^2,x,y)", 1, 1, {1, 4}, DerivationStatus::SolvedAndVerified},
                                // The isolation's own substitution check cannot evaluate cos(y) at its
                                // points, so the record says unchecked even though the final identity held.
                                Case{"implicit_trig", "implicit(sin(y)=x,x,y)", 5, 0, {1, 1}, DerivationStatus::SolvedButUnchecked},
                                Case{"", "implicit(y=x^2,x,y)", 3, 7, {6, 1}, DerivationStatus::SolvedAndVerified},
                                // No sample point folds cos(x+y) off the line x = -y, so the answer
                                // stands unchecked rather than failed.
                                Case{"", "implicit(sin(x+y)=x,x,y)", 1, -1, {0, 1}, DerivationStatus::SolvedButUnchecked}}) {
        Run run;
        implicit_run(run, fixture.text);
        t.check(run.result.outcome == ImplicitOutcome::Differentiated && run.result.derivative != kNoNode &&
                run.result.status == fixture.status,
                "implicit differentiation answers inside its envelope: " + std::string(fixture.text) + ": " +
                implicit_outcome_name(run.result.outcome) + ", " + derivation_status_name(run.result.status) +
                ": " + run.result.detail);
        t.check(value_at(run, fixture.x, fixture.y, fixture.slope),
                "the implicit derivative has the textbook value: " + std::string(fixture.text) + " is " +
                (run.result.derivative == kNoNode ? std::string("missing") : print(run.arena, run.result.derivative)));
        const std::string rendered = render_derivation(run.arena, run.derivation);
        t.check(rendered.find("implicit.chain-rule") != std::string::npos &&
                rendered.find("implicit.collect") != std::string::npos &&
                rendered.find("implicit.isolate") != std::string::npos &&
                rendered.find("implicit.check") != std::string::npos &&
                rendered.find("dydx") != std::string::npos,
                "the walkthrough differentiates both sides, collects, isolates and checks: " + std::string(fixture.text));
        // Every side that mentions y has to pick up the derivative through the chain rule.
        size_t chained = 0, carrying = 0;
        for (size_t i = 0; i < run.derivation.size(); ++i) {
            const Step &recorded = run.derivation.at(static_cast<StepId>(i));
            const TransformationPayload *change = run.derivation.transformation(static_cast<StepId>(i));
            if (recorded.rule_id != "implicit.chain-rule" || !change || change->after == kNoNode) continue;
            if (!depends_on(run.arena, change->before, run.command.dependent)) continue;
            ++chained;
            if (depends_on(run.arena, change->after, run.result.symbol)) ++carrying;
        }
        t.check(chained > 0 && chained == carrying,
                "each side that mentions the dependent variable gains dydx by the chain rule: " + std::string(fixture.text));
        bool divisor = false;
        for (const std::string &condition : run.result.restrictions)
            divisor = divisor || condition.find("not zero") != std::string::npos || condition.find("!=") != std::string::npos;
        t.check(run.result.restrictions.empty() || divisor,
                "a restriction the isolation carries names the divisor it needs: " + std::string(fixture.text));
        if (*fixture.golden)
            check_golden(t, fixture.golden, "problem: " + std::string(fixture.text) + "\nresult: " +
                         (run.result.derivative == kNoNode ? std::string("none") : print(run.arena, run.result.derivative)) +
                         "\n" + rendered);
    }
    {
        // The circle's divisor is y, so isolating the derivative has to say it needs y nonzero.
        Run run;
        implicit_run(run, "implicit(x^2+y^2=25,x,y)");
        bool names_y = false;
        for (const std::string &condition : run.result.restrictions)
            names_y = names_y || condition.find('y') != std::string::npos;
        t.check(names_y, "the circle's derivative is conditional on its divisor being nonzero");
    }
    // The final check says Passed only when it read something, and Inconclusive when it read nothing.
    for (const auto &expected : {std::pair{"implicit(x^2+y^2=25,x,y)", VerificationOutcome::Passed},
                                 std::pair{"implicit(sin(x+y)=x,x,y)", VerificationOutcome::Inconclusive}}) {
        Run run;
        implicit_run(run, expected.first);
        bool found = false;
        for (size_t i = 0; i < run.derivation.size(); ++i) {
            const Step &recorded = run.derivation.at(static_cast<StepId>(i));
            if (recorded.rule_id == "implicit.check" && !recorded.verifications.empty())
                found = recorded.verifications.back().outcome == expected.second;
        }
        t.check(found, "the implicit check records what it could read: " + std::string(expected.first));
    }
    struct Refusal {
        const char *text;
        ImplicitOutcome outcome;
        DerivationStatus status;
        const char *reason = "";
    };
    for (const Refusal &refusal : {
             Refusal{"implicit(x^2=4,x,y)", ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             Refusal{"implicit(x^2+y^2,x,y)", ImplicitOutcome::NotAnEquation, DerivationStatus::InvalidInput},
             Refusal{"implicit(x+y=1,x,x)", ImplicitOutcome::InvalidInput, DerivationStatus::InvalidInput},
             Refusal{"implicit(y-y=x,x,y)", ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported, "cancel"},
             Refusal{"implicit(asin(y)=x,x,y)", ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             Refusal{"implicit(dydx+y=x,x,y)", ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported}}) {
        Run run;
        implicit_run(run, refusal.text);
        t.check(run.result.derivative == kNoNode && !run.result.detail.empty() &&
                run.result.outcome == refusal.outcome && run.result.status == refusal.status &&
                run.result.detail.find(refusal.reason) != std::string::npos,
                "implicit differentiation refuses for the right reason: " + std::string(refusal.text) + ": " +
                implicit_outcome_name(run.result.outcome) + ": " + run.result.detail);
    }
    for (const auto &malformed : {std::pair{"implicit(x^2+y^2=1,x)", CommandStatus::Unsupported},
                                  std::pair{"implicit(x^2+y^2=1,x,y,z)", CommandStatus::Unsupported},
                                  std::pair{"implicit(x^2+y^2=1,x,2)", CommandStatus::Invalid}}) {
        Arena arena;
        const Command command = parse_command(arena, malformed.first, "x");
        t.check(command.kind == CommandKind::Implicit && command.status == malformed.second && !command.detail.empty(),
                "a malformed implicit request is refused before any work: " + std::string(malformed.first) + ": " +
                command.detail);
    }
    {
        Run run;
        run.derivation.request.numeric_mode = NumericMode::Decimal;
        implicit_run(run, "implicit(x*y=1,x,y)");
        t.check(run.result.outcome == ImplicitOutcome::UnsupportedForm && run.result.derivative == kNoNode,
                "implicit differentiation refuses decimal mode rather than approximating");
    }
    {
        // MATH-007's promise reaches here too: a degree-mode request names the mode it ran under,
        // and a relation that leans on a trig identity is refused rather than read as radians.
        Run run;
        run.derivation.request.angle_mode = AngleMode::Degrees;
        implicit_run(run, "implicit(x^2+y^2=25,x,y)");
        t.check(run.result.outcome == ImplicitOutcome::Differentiated &&
                run.derivation.context.angle_convention == "degrees",
                "implicit differentiation without trig answers in degree mode and records it");
        Run trig;
        trig.derivation.request.angle_mode = AngleMode::Degrees;
        implicit_run(trig, "implicit(sin(x)+y^2=25,x,y)");
        t.check(trig.result.derivative == kNoNode && trig.result.outcome == ImplicitOutcome::UnsupportedForm &&
                trig.derivation.context.angle_convention == "degrees",
                "implicit differentiation refuses a trigonometric relation in degree mode rather than reading it as radians");
    }
    for (const char *text : {"implicit(x^2+y^2=25,x,y)", "implicit(y^3+x*y=x^2,x,y)"}) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            Run run;
            Budget budget;
            if (failure == 0) budget.max_steps = 2;
            if (failure == 1) budget.max_rewrites = 4;
            if (failure == 2) budget.poll = [](void *) { return true; };
            implicit_run(run, text, budget);
            t.check(run.result.derivative == kNoNode &&
                    run.result.outcome == (failure == 2 ? ImplicitOutcome::Cancelled : ImplicitOutcome::ResourceExceeded) &&
                    run.result.status == (failure == 2 ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached),
                    "implicit differentiation stops for cancellation and for its budgets: " + std::string(text) +
                    " failure " + std::to_string(failure) + ": " + run.result.detail);
        }
    }
    {
        Run run;
        implicit_run(run, "implicit(x^2+y^2=25,x,y)");
        const bool circle = value_at(run, 3, 4, {-3, 4}) && value_at(run, 0, 5, {0, 1});
        Run cubic;
        implicit_run(cubic, "implicit(y^3+x*y=x^2,x,y)");
        t.evidence("CALC-007", circle && value_at(cubic, 1, 1, {1, 4}) &&
                                   run.result.status == DerivationStatus::SolvedAndVerified,
                   "implicit differentiation finds dy/dx of a circle and a mixed cubic by the chain rule, "
                   "collecting and isolating it with its divisor restriction");
    }
}

}
