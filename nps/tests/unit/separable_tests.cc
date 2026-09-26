#include <string>
#include <vector>

#include "nps/core/print.h"
#include "nps/steps/separable.h"
#include "golden/golden.h"
#include "step_invariants.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

const char kFamily[] = "calculus.ode.separable.first-order";

struct Run {
    Arena arena;
    Derivation derivation;
    Command command;
    SeparableResult result;
    std::string solution;
    std::string constant;
    std::string rules;
    std::vector<std::string> broken;

    explicit Run(const std::string &text, const Budget &budget = Budget(),
                 NumericMode mode = NumericMode::Exact) {
        derivation.request.original_expression = text;
        derivation.request.numeric_mode = mode;
        command = parse_command(arena, text, "x");
        result = solve_separable(arena, derivation, command, budget);
        if (result.solution != kNoNode) solution = print(arena, result.solution);
        if (result.constant != kNoNode) constant = print(arena, result.constant);
        for (size_t index = 0; index < derivation.size(); ++index) {
            rules += derivation.at(static_cast<StepId>(index)).rule_id;
            rules += ";";
        }
        invariants::Pass audit;
        audit.walk(arena, derivation, false, true, &broken);
    }

    bool records(const char *rule) const { return rules.find(std::string(rule) + ";") != std::string::npos; }
    const char *outcome() const { return separable_outcome_name(result.outcome); }
    std::string record() {
        return "problem: " + derivation.request.original_expression + "\noutcome: " + outcome() +
               "\nresult: " + solution + "\n" + render_derivation(arena, derivation);
    }
};

bool cancel_now(void *) { return true; }

}  // namespace

void run_separable_tests(TestSink &t) {
    {
        Arena arena;
        const Command primed = parse_command(arena, "desolve(y'=x*y,x,y)", "x");
        t.check(primed.kind == CommandKind::Desolve && primed.status == CommandStatus::Ready &&
                    primed.dependent != kNoNode && arena.text(primed.dependent) == "y" &&
                    primed.variable_name == "x" && print(arena, primed.expression) == "(x * y)",
                "desolve reads a primed first derivative as the equation for y in x");
        const Command written = parse_command(arena, "desolve(diff(y,x)=x*y,x,y)", "x");
        t.check(written.kind == CommandKind::Desolve && written.status == CommandStatus::Ready,
                "desolve reads diff(y,x) as the same first derivative");
        const Command pointed = parse_command(arena, "desolve([y'=x*y,y(0)=2],x,y)", "x");
        t.check(pointed.status == CommandStatus::Ready && pointed.initial_point != kNoNode &&
                    print(arena, pointed.initial_point) == "0" &&
                    print(arena, pointed.initial_value) == "2",
                "an initial point written y(0)=2 is read beside the equation");
        for (const char *giac_only : {"desolve(y''=y,x,y)", "desolve(y'=x*y)", "desolve(y'=x*y,x)",
                                      "desolve(y=x,x,y)", "desolve(y'=diff(y),x,y)",
                                      "desolve([y'=x*y,z(0)=2],x,y)", "desolve(3'=x,x,y)",
                                      "desolve(y'=x*y,x,x)"}) {
            t.check(parse_command(arena, giac_only, "x").kind == CommandKind::Unhandled,
                    std::string("a desolve shape this family does not read stays with Giac: ") +
                        giac_only);
        }
    }
    {
        Run run("desolve(y'=x*y,x,y)");
        t.equal(run.outcome(), "solved", "y' = x*y solves by separation");
        t.equal(run.solution, "(y = exp((((x^2) * (2^-1)) + C)))",
                "the logarithm the y side integrates to is inverted into an explicit solution");
        t.check(run.result.explicit_solution && run.result.status == DerivationStatus::SolvedAndVerified,
                "the explicit solution is checked against the equation and verified");
        t.check(run.records("ode.separable.plan") && run.records("ode.separable.separate") &&
                    run.records("calculus.integrate.rules") &&
                    run.records("ode.separable.integrate-both-sides") &&
                    run.records("ode.separable.solve-explicit") &&
                    run.records("ode.separable.check-solution"),
                "the derivation separates, integrates both sides, solves for y and checks");
        t.equal(run.derivation.context.problem_family_id, kFamily,
                "the solution context names the separable family");
        t.check(run.broken.empty(), "every separable step conforms to its rule schema" +
                                        (run.broken.empty() ? std::string() : " " + run.broken.front()));
        t.evidence("CALC-012", run.result.status == DerivationStatus::SolvedAndVerified &&
                                   run.records("ode.separable.check-solution"),
                   "a separable first-order equation is separated, integrated with its constant, "
                   "solved for the dependent variable and checked against the equation");
        check_golden(t, "separable_explicit", run.record());
    }
    {
        Run run("desolve(y'=2*x,x,y)");
        t.equal(run.solution, "(y = ((2 * ((x^2) * (2^-1))) + C))",
                "a right side free of y integrates straight to y");
        t.check(run.result.explicit_solution && !run.records("ode.separable.solve-explicit"),
                "and needs no inversion step because the relation is already explicit");
    }
    {
        Run run("desolve(y'=y^2,x,y)");
        t.equal(run.solution, "(y = (-(((1 * x) + C)^-1)))",
                "a square of y inverts through the reciprocal");
        t.check(run.result.status == DerivationStatus::SolvedAndVerified && run.broken.empty(),
                "and the reciprocal solution is verified against the equation");
    }
    {
        Run run("desolve(y'=x/y^2,x,y)");
        t.equal(run.outcome(), "solved", "y' = x/y^2 solves by separation");
        t.check(!run.result.explicit_solution && run.solution == "(((y^3) * (3^-1)) = (((x^2) * (2^-1)) + C))",
                "a cubic in y is kept as the implicit relation rather than solved for a branch");
        t.check(run.result.status == DerivationStatus::SolvedAndVerified &&
                    !run.records("ode.separable.solve-explicit") && run.broken.empty(),
                "the implicit relation is checked side by side and verified");
        check_golden(t, "separable_implicit", run.record());
    }
    {
        Run run("desolve([y'=x*y,y(0)=2],x,y)");
        t.equal(run.outcome(), "solved", "an initial point gives a particular solution");
        t.equal(run.constant, "ln(2)", "the initial point y(0) = 2 fixes C as ln 2");
        t.equal(run.solution, "(y = exp((((x^2) * (2^-1)) + ln(2))))",
                "the particular solution carries the constant the initial point gave");
        t.check(run.records("ode.separable.initial-condition") &&
                    run.result.status == DerivationStatus::SolvedAndVerified && run.broken.empty(),
                "the constant is its own checked step and the particular solution is verified");
        check_golden(t, "separable_initial_condition", run.record());
    }
    {
        Run run("desolve(y'=x+y,x,y)");
        t.equal(run.outcome(), "unsupported form", "a sum of x and y is refused as not separable");
        t.check(run.result.detail.find("not a product") != std::string::npos &&
                    run.result.solution == kNoNode && run.derivation.size() == 0,
                "the refusal names the failed factorisation and records nothing");
        t.equal(run.derivation.context.problem_family_id, kFamily,
                "a refusal still names the family it refused");
    }
    {
        Run run("desolve(y'=k*y,x,y)");
        t.equal(run.outcome(), "unsupported form", "a parameter outside the two variables is refused");
        t.check(run.result.detail.find("k is neither") != std::string::npos,
                "and the refusal names the parameter");
    }
    {
        Run run("desolve(y'=x*exp(x^2)*y,x,y)");
        t.equal(run.outcome(), "unsupported form",
                "an x factor the integral engine cannot integrate is refused");
        t.check(run.result.detail.find("independent side") != std::string::npos &&
                    run.result.solution == kNoNode,
                "and the refusal names the side that had no antiderivative");
    }
    {
        Run run("desolve([y'=x*y,y(0)=0],x,y)");
        t.equal(run.outcome(), "unsupported form",
                "an initial value where the y factor vanishes is refused");
        t.check(run.result.detail.find("constant one separation does not reach") != std::string::npos,
                "because the solution through it is the constant one separation divided away");
    }
    {
        Run run("desolve([y'=x*y,y(0)=-1],x,y)");
        t.equal(run.outcome(), "unsupported form",
                "an initial value off the logarithm's recorded branch is refused");
        t.check(run.result.detail.find("outside the branch") != std::string::npos,
                "and the refusal says the point is outside the recorded branch");
    }
    {
        Run run("desolve(y'=x*y,x,y)", Budget(), NumericMode::Decimal);
        t.equal(run.outcome(), "unsupported form", "Decimal mode is refused rather than approximated");
    }
    {
        Budget budget;
        budget.poll = cancel_now;
        Run run("desolve(y'=x*y,x,y)", budget);
        t.check(run.result.outcome == SeparableOutcome::Cancelled &&
                    run.result.status == DerivationStatus::Cancelled && run.result.solution == kNoNode,
                "a cancelled solve reports cancellation and withholds the solution");
    }
    {
        Budget budget;
        budget.max_steps = 3;
        Run run("desolve(y'=x*y,x,y)", budget);
        t.check(run.result.outcome == SeparableOutcome::ResourceExceeded &&
                    run.result.status == DerivationStatus::ResourceLimitReached &&
                    run.result.solution == kNoNode,
                "a step budget that runs out reports the resource limit and withholds the solution");
    }
    {
        Arena arena;
        Derivation derivation;
        Command command = parse_command(arena, "desolve(y'=x*y,x,y)", "x");
        command.dependent = kNoNode;
        const SeparableResult result = solve_separable(arena, derivation, command);
        t.check(result.outcome == SeparableOutcome::InvalidInput && derivation.size() == 0,
                "a command missing its dependent variable is refused as invalid input");
    }
}

}  // namespace nps
