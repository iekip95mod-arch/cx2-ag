#include <cstdint>
#include <string>

#include "nps/physics/gravitation.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity quantity(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

struct Run {
    RelationResult result;
    size_t plan = 0;
    size_t transformations = 0;
    size_t checks = 0;
    bool all_verified = true;
    std::string unverified;
    std::string rules;
    std::string context_family;
    std::string assumptions;
    std::string report_rule;
    std::string report_evidence;
};

Run run(const RelationProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    Run run;
    run.result = solve_gravitation(arena, derivation, input, budget);
    run.context_family = derivation.context.problem_family_id;
    for (const std::string &assumption : derivation.context.active_assumptions)
        run.assumptions += assumption + " | ";
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (!run.rules.empty())
            run.rules += ' ';
        run.rules += step.rule_id;
        run.all_verified = run.all_verified && step.verified();
        if (!step.verified())
            run.unverified += step.rule_id + " ";
        if (step.rule_id.ends_with(".significant-figures")) {
            run.report_rule = step.rule_id;
            // The outcome ahead of the sentence, so a passed record and a failed one carrying the
            // same detail cannot read identically here.
            for (const VerificationRecord &check : step.verifications) {
                run.report_evidence += verification_outcome_name(check.outcome);
                run.report_evidence += ", ";
                run.report_evidence += check.detail;
            }
        }
        switch (step.kind) {
            case StepKind::Plan: ++run.plan; break;
            case StepKind::Transformation: ++run.transformations; break;
            case StepKind::Check: ++run.checks; break;
            case StepKind::Branch: break;
        }
    }
    return run;
}

bool contains(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

bool exact_value(const RelationResult &result, int64_t numerator, int64_t denominator) {
    return result.quantity.value.num == numerator && result.quantity.value.den == denominator;
}

bool always_cancel(void *) { return true; }

}  // namespace

void run_gravitation_tests(TestSink &t) {
    {
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("3 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("1 m")));
        const Run solved = run(input);
        t.equal(relation_outcome_name(solved.result.outcome), "solved",
                "gravitation solves the force between two point masses");
        t.check(exact_value(solved.result, 10011, 25000000000000),
                "the force is the exact product of G with both masses over the squared separation");
        t.equal(solved.result.unit_text, "kg m/s^2", "the force is reported in SI base units");
        t.check(solved.plan >= 2 && solved.checks >= 3,
                "the gravitation derivation records plan and check steps for its own and the linear work");
        t.equal(solved.unverified, "", "every recorded gravitation claim has passing evidence");
        t.evidence("PHYS-011", contains(solved.rules, "physics.gravitation.definition") &&
                                   contains(solved.rules, "physics.gravitation.check-dimensions") &&
                                   contains(solved.rules, "physics.gravitation.substitute") &&
                                   contains(solved.rules, "physics.gravitation.check-candidate"),
                   "the gravitation provenance names definition, dimensions, substitution and check");
        t.evidence("PHYS-025",
                   contains(solved.assumptions, "point mass") &&
                       contains(solved.assumptions, "spherically symmetric") &&
                       contains(solved.assumptions, "gravitational constant"),
                   "the gravitation context records the point-mass condition and the constant it used");
        t.equal(solved.context_family, "physics.gravitation.point-masses",
                "the solution context identifies the gravitation family");
        t.check(solved.result.equation != kNoNode && solved.result.substituted != kNoNode &&
                    solved.result.value != kNoNode,
                "the gravitation result retains the symbolic and substituted models");
        t.check(solved.result.cost.steps > 0, "the gravitation run reports what it spent");
    }
    {
        // The separation enters at power minus two, so a case at r other than 1 m is what
        // distinguishes the exponent from a bare factor.
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("3 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("2 m")));
        const Run solved = run(input);
        t.equal(relation_outcome_name(solved.result.outcome), "solved",
                "gravitation solves at a separation other than one metre");
        t.check(exact_value(solved.result, 10011, 100000000000000),
                "doubling the separation divides the gravitational force by four");
    }
    {
        RelationProblem input = gravitation_problem(GravitationVariable::FirstMass);
        input.knowns.push_back(gravitation_known(GravitationVariable::Force, quantity("1 N")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("1 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("1 m")));
        const Run solved = run(input);
        t.equal(relation_outcome_name(solved.result.outcome), "solved",
                "gravitation solves a mass from the force and the separation");
        t.check(exact_value(solved.result, 50000000000000, 3337),
                "the solved mass is the exact quotient of the force by G");
    }
    {
        // The separation enters at power minus two, so isolating it needs a root this path lacks.
        RelationProblem input = gravitation_problem(GravitationVariable::Separation);
        input.knowns.push_back(gravitation_known(GravitationVariable::Force, quantity("1 N")));
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("1 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("1 kg")));
        const Run refused = run(input);
        t.evidence("PHYS-011", relation_outcome_name(refused.result.outcome), "unsupported unknown",
                   "an unknown separation is refused rather than answered by the linear path");
        t.check(contains(refused.result.detail, "power -2"),
                "the refusal names the power that puts the separation outside the envelope");
    }
    {
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 m")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("3 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("1 m")));
        const Run refused = run(input);
        t.equal(relation_outcome_name(refused.result.outcome), "dimension mismatch",
                "a length offered as a mass is refused before substitution");
    }
    {
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("3 kg")));
        const Run refused = run(input);
        t.equal(relation_outcome_name(refused.result.outcome), "missing known",
                "a gravitation problem without the separation is refused");
        t.check(contains(refused.result.detail, "separation"),
                "the refusal names the quantity the problem left out");
    }
    {
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("3 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("1 m")));
        const Run refused = run(input);
        t.equal(relation_outcome_name(refused.result.outcome), "duplicate known",
                "the same gravitation position given twice is refused");
    }
    {
        Budget budget;
        budget.poll = always_cancel;
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, quantity("3 kg")));
        input.knowns.push_back(gravitation_known(GravitationVariable::Separation, quantity("1 m")));
        const Run stopped = run(input, budget);
        t.equal(relation_outcome_name(stopped.result.outcome), "cancelled",
                "a cancelled gravitation run reports cancellation rather than an answer");
        t.check(stopped.result.value == kNoNode,
                "a cancelled gravitation run returns no value to display");
    }
    {
        RelationProblem input = gravitation_problem(GravitationVariable::Force);
        input.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, quantity("2.0 kg")));
        input.knowns.push_back(
            gravitation_known(GravitationVariable::SecondMass, quantity("3.0 kg")));
        input.knowns.push_back(
            gravitation_known(GravitationVariable::Separation, quantity("1.0 m")));
        const Run solved = run(input);
        t.equal(relation_outcome_name(solved.result.outcome), "solved",
                "measured gravitation inputs still solve");
        t.equal(solved.result.value_text, "0.00000000040",
                "the displayed gravitation answer is rounded to the fewest measured figures");
        t.check(exact_value(solved.result, 10011, 25000000000000),
                "rounding the report leaves the exact value alone");
        t.check(solved.result.quantity.precision.kind == NumberKind::Measured &&
                    solved.result.quantity.precision.significant_digits == 2,
                "the reported gravitation quantity carries the combined measured precision");
        t.equal(solved.report_rule, "physics.gravitation.significant-figures",
                "the gravitation reporting step is recorded under its own rule id");
        t.check(solved.rules.rfind("physics.gravitation.significant-figures") >
                    solved.rules.find("physics.gravitation.check-candidate"),
                "gravitation reporting happens after candidate verification");
        t.equal(solved.report_evidence,
                "passed, 0.00000000040 is within half a unit in the last place of "
                "0.00000000040044",
                "the gravitation report step names its outcome and both compared values");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "a reported gravitation answer keeps its verified status");
    }
    {
        t.equal(gravitation_variable_name(GravitationVariable::Force), "gravitational force",
                "the gravitation force position is named for display");
        t.equal(gravitation_variable_name(GravitationVariable::FirstMass), "first mass",
                "the first gravitation mass is named for display");
        t.equal(gravitation_variable_name(GravitationVariable::SecondMass), "second mass",
                "the second gravitation mass is named for display");
        t.equal(gravitation_variable_name(GravitationVariable::Separation), "separation",
                "the gravitation separation is named for display");
    }
}

}  // namespace nps
