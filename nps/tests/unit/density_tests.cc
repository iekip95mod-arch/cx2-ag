#include <cstdint>
#include <string>

#include "nps/core/print.h"
#include "nps/physics/density.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity quantity(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

DensityKnown known(DensityVariable variable, const char *text) {
    DensityKnown entry;
    entry.variable = variable;
    entry.quantity = quantity(text);
    return entry;
}

DensityProblem problem(DensityVariable unknown, const DensityKnown &first,
                       const DensityKnown &second) {
    DensityProblem input;
    input.unknown = unknown;
    input.knowns.push_back(first);
    input.knowns.push_back(second);
    return input;
}

struct Run {
    DensityResult result;
    size_t steps = 0;
    size_t plan = 0;
    size_t transformations = 0;
    size_t checks = 0;
    bool all_verified = true;
    std::string rules;
    std::string context_family;
    std::string report_evidence;
    std::string verifications;
    std::string conversion_detail;
    std::string substitution_detail;
    std::string substitution_before;
    std::string substitution_after;
};

Run run(const DensityProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    Run run;
    run.result = solve_density(arena, derivation, input, budget);
    run.steps = derivation.size();
    run.verifications = verification_transcript(derivation);
    run.context_family = derivation.context.problem_family_id;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (!run.rules.empty())
            run.rules += ' ';
        run.rules += step.rule_id;
        run.all_verified = run.all_verified && step.verified();
        if (step.rule_id == "physics.density.convert-units")
            run.conversion_detail = step.explanation_detailed;
        if (step.rule_id == "physics.density.substitute") {
            run.substitution_detail = step.explanation_detailed;
            const TransformationPayload *payload = derivation.transformation(step.id);
            if (payload) {
                run.substitution_before = print(arena, payload->before);
                run.substitution_after = print(arena, payload->after);
            }
        }
        if (step.rule_id == "physics.density.significant-figures") {
            // The outcome ahead of the sentence, in verification_transcript's format. Without it a
            // passed check and a failed one carrying the same detail read identically here.
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

bool exact_value(const DensityResult &result, int64_t numerator, int64_t denominator) {
    return result.quantity.value.num == numerator && result.quantity.value.den == denominator;
}

bool always_cancel(void *) { return true; }

struct PollAfter {
    int calls = 0;
    int stop_at = 0;
};

bool cancel_after(void *context) {
    PollAfter *poll = static_cast<PollAfter *>(context);
    ++poll->calls;
    return poll->calls >= poll->stop_at;
}

}  // namespace

void run_density_tests(TestSink &t) {
    {
        struct TeachingCase {
            DensityProblem input;
            const char *value, *unit, *substituted;
        };
        const TeachingCase cases[] = {
            {problem(DensityVariable::Mass, known(DensityVariable::Density, "2 g/cm^3"),
                     known(DensityVariable::Volume, "3 cm^3")),
             "0.006", "kg", "(m = (2000 * (3 * (1000000^-1))))"},
            {problem(DensityVariable::Volume, known(DensityVariable::Mass, "500 g"),
                     known(DensityVariable::Density, "2 g/cm^3")),
             "0.00025", "m^3", "((1 * (2^-1)) = (2000 * V))"},
            {problem(DensityVariable::Density, known(DensityVariable::Mass, "1 kg"),
                     known(DensityVariable::Volume, "500 cm^3")),
             "2000", "kg/m^3", "(1 = (rho * (1 * (2000^-1))))"},
            {problem(DensityVariable::Mass, known(DensityVariable::Density, "2000 g/m^3"),
                     known(DensityVariable::Volume, "3 m^3")),
             "6", "kg", "(m = (2 * 3))"},
        };
        for (const TeachingCase &test : cases) {
            const Run solved = run(test.input);
            t.equal(density_outcome_name(solved.result.outcome), "solved",
                    "mixed-unit teaching example solves its requested quantity");
            t.equal(solved.result.value_text, test.value, "teaching example has the expected value");
            t.equal(solved.result.unit_text, test.unit, "answer uses the requested quantity unit");
            t.check(contains(solved.conversion_detail, "kg for mass") &&
                        contains(solved.conversion_detail, "m^3 for volume") &&
                        contains(solved.conversion_detail, "kg/m^3 for density") &&
                        !contains(solved.conversion_detail, "neither unit") &&
                        !contains(solved.conversion_detail, "answer comes out"),
                    "conversion teaching names consistent units without promising a density answer");
            t.equal(solved.substitution_before, "(m = (rho * V))",
                    "substitution starts from the density relation before isolation");
            t.equal(solved.substitution_after, test.substituted,
                    "substitution replaces only the known quantities with their converted values");
            t.check(solved.rules.find("physics.density.convert-units") <
                        solved.rules.find("physics.density.substitute") &&
                        solved.rules.find("physics.density.substitute") <
                        solved.rules.find("eq.linear.inverse-operations"),
                    "recorded conversion and substitution precede the linear solve");
            t.check(contains(solved.substitution_detail, "After converting") &&
                        contains(solved.substitution_detail, "then solve") &&
                        !contains(solved.substitution_detail, "Rearranging first"),
                    "substitution teaching follows the recorded order");
        }
    }
    {
        // 2.0 times 4.98 is 9.96, reported at two figures as 10, whose last figure is in the units
        // place and not the tenths the combined precision took from its operands.
        const Run carry = run(problem(DensityVariable::Mass,
                                      known(DensityVariable::Density, "2.0 kg/m^3"),
                                      known(DensityVariable::Volume, "4.98 m^3")));
        t.equal(carry.result.value_text, "10", "a mass that carries reports at two figures");
        t.check(carry.result.quantity.precision.significant_digits == 2 &&
                    carry.result.quantity.precision.last_significant_decimal_place == 0,
                "and the reported place is the units place its last figure sits in");
    }
    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.equal(density_outcome_name(solved.result.outcome), "solved",
                "density times volume solves mass");
        t.equal(solved.result.value_text, "12", "mass has the exact numeric value");
        t.equal(solved.result.unit_text, "kg", "mass is reported in SI");
        t.check(exact_value(solved.result, 12, 1), "mass remains a typed exact rational");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "mass solve has a verified status");
    }
    {
        const Run solved = run(problem(DensityVariable::Volume,
                                       known(DensityVariable::Mass, "12 kg"),
                                       known(DensityVariable::Density, "4 kg/m^3")));
        t.equal(density_outcome_name(solved.result.outcome), "solved",
                "mass divided by density solves volume");
        t.equal(solved.result.value_text, "3", "volume has the exact numeric value");
        t.equal(solved.result.unit_text, "m^3", "volume is reported in cubic metres");
        t.check(exact_value(solved.result, 3, 1), "volume remains a typed exact rational");
    }
    {
        const Run solved = run(problem(DensityVariable::Density,
                                       known(DensityVariable::Mass, "12 kg"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.equal(density_outcome_name(solved.result.outcome), "solved",
                "mass divided by volume solves density");
        t.equal(solved.result.value_text, "4", "density has the exact numeric value");
        t.equal(solved.result.unit_text, "kg/m^3", "density is reported in SI");
        t.check(exact_value(solved.result, 4, 1), "density remains a typed exact rational");
    }

    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "2 g/cm^3"),
                                       known(DensityVariable::Volume, "3 cm^3")));
        t.equal(solved.result.value_text, "0.006",
                "compound prefixes and a cubic volume convert exactly");
        t.equal(solved.result.unit_text, "kg", "the converted answer is in kilograms");
        t.check(exact_value(solved.result, 3, 500),
                "the non-default-unit result stays exact internally");
        t.check(contains(solved.rules, "physics.density.convert-units"),
                "the exact unit conversion is recorded");
    }
    {
        const Run solved = run(problem(DensityVariable::Volume,
                                       known(DensityVariable::Mass, "500 g"),
                                       known(DensityVariable::Density, "2 g/cm^3")));
        t.equal(solved.result.value_text, "0.00025",
                "a non-default mass and density solve SI volume");
        t.check(exact_value(solved.result, 1, 4000),
                "the small cubic-metre result stays exact");
    }
    {
        const Run solved = run(problem(DensityVariable::Density,
                                       known(DensityVariable::Mass, "1 kg"),
                                       known(DensityVariable::Volume, "500 cm^3")));
        t.equal(solved.result.value_text, "2000", "cubic centimetres convert before division");
        t.equal(solved.result.unit_text, "kg/m^3", "the derived density has its SI unit");
        t.check(exact_value(solved.result, 2000, 1),
                "the converted density stays exact internally");
    }

    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "2.50 kg/m^3"),
                                       known(DensityVariable::Volume, "3.40 m^3")));
        t.equal(solved.result.value_text, "8.50", "measured inputs set final report precision");
        t.check(exact_value(solved.result, 17, 2),
                "final reporting does not replace the exact answer");
        t.check(solved.result.quantity.precision.kind == NumberKind::Measured &&
                    solved.result.quantity.precision.significant_digits == 3,
                "the typed answer carries the combined precision metadata");
        t.check(contains(solved.rules, "physics.density.significant-figures"),
                "rounding is recorded after the solve");
        t.check(solved.rules.rfind("physics.density.significant-figures") >
                    solved.rules.find("physics.density.check-candidate"),
                "reporting happens after candidate verification");
        t.equal(solved.report_evidence,
                "passed, 8.50 is within half a unit in the last place of 8.5",
                "the report step's evidence names its outcome and the two values it compared, so "
                "neither a constant detail nor a changed outcome can stand in for the comparison");
    }
    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "2.5 kg/m^3"),
                                       known(DensityVariable::Volume, "3.40 m^3")));
        t.equal(solved.result.value_text, "8.5",
                "the fewer measured figure count controls the report");
        t.check(solved.result.quantity.precision.significant_digits == 2,
                "the typed precision is the fewer measured count");
    }
    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "2.50 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.equal(solved.result.value_text, "7.50",
                "an exact count does not reduce measured precision");
        t.check(exact_value(solved.result, 15, 2),
                "mixed exact and measured inputs still compute exactly");
    }

    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "-2 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.equal(density_outcome_name(solved.result.outcome), "solved",
                "a signed input is not given an unstated physical constraint");
        t.equal(solved.result.value_text, "-6", "the exact algebra preserves the sign");
    }
    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "0 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.equal(solved.result.value_text, "0", "a zero coefficient still determines zero mass");
    }
    {
        const Run refused = run(problem(DensityVariable::Volume,
                                        known(DensityVariable::Mass, "1 kg"),
                                        known(DensityVariable::Density, "0 kg/m^3")));
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "a zero divisor that yields no solution is not offered");
        t.check(contains(refused.result.detail, "do not determine one volume"),
                "the non-unique solve explains the failed determination");
    }
    {
        const Run refused = run(problem(DensityVariable::Volume,
                                        known(DensityVariable::Mass, "0 kg"),
                                        known(DensityVariable::Density, "0 kg/m^3")));
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "an identity with every volume possible is not called solved");
    }

    {
        DensityProblem input;
        input.unknown = DensityVariable::Mass;
        input.knowns.push_back(known(DensityVariable::Density, "4 kg/m^3"));
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "missing known",
                "one known quantity is insufficient");
        t.equal(refused.result.detail, "missing known volume", "the missing quantity is named");
    }
    {
        DensityProblem input;
        input.unknown = DensityVariable::Mass;
        input.knowns.push_back(known(DensityVariable::Density, "4 kg/m^3"));
        input.knowns.push_back(known(DensityVariable::Density, "5 kg/m^3"));
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "duplicate known",
                "a duplicate quantity is rejected");
        t.equal(refused.result.detail, "density is given twice", "the duplicate is named");
    }
    {
        const Run refused = run(problem(DensityVariable::Mass,
                                        known(DensityVariable::Mass, "12 kg"),
                                        known(DensityVariable::Volume, "3 m^3")));
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "the unknown cannot also be known");
    }
    {
        DensityProblem input;
        input.unknown = static_cast<DensityVariable>(99);
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid typed variable is rejected");
    }
    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[0].quantity.value.den = 0;
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid rational is rejected without using it");
    }
    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[1].quantity.value.num = -6;
        input.knowns[1].quantity.value.den = -2;
        const Run solved = run(input);
        t.equal(solved.result.value_text, "12",
                "a valid noncanonical rational is normalized before use and display");
    }
    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[0].quantity.unit.scale.den = 0;
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid unit conversion scale is rejected");
    }
    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[0].quantity.precision.kind = NumberKind::Measured;
        input.knowns[0].quantity.precision.significant_digits = 0;
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "inconsistent precision metadata is rejected");
    }

    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 s"));
        Arena arena;
        Derivation derivation;
        const DensityResult refused = solve_density(arena, derivation, input);
        t.evidence("VER-007", density_outcome_name(refused.outcome), "dimension mismatch",
                   "a volume supplied as time is rejected");
        t.check(contains(refused.detail, "volume requires L^3"),
                "the mismatch names the required dimension");
        t.check(derivation.size() == 0 && arena.node_count() == 0,
                "dimension validation happens before substitution or derivation");
    }

    {
        const Run overflow = run(problem(
            DensityVariable::Mass,
            known(DensityVariable::Density, "9223372036854775807 kg/m^3"),
            known(DensityVariable::Volume, "2 m^3")));
        t.equal(density_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "an exact product beyond int64 is explicit");
        t.check(contains(overflow.result.detail, "exact integer arithmetic"),
                "solver overflow explains the exact arithmetic limit");
    }
    {
        const Run overflow = run(problem(
            DensityVariable::Mass,
            known(DensityVariable::Density, "9223372036854775807 g/cm^3"),
            known(DensityVariable::Volume, "1 m^3")));
        t.equal(density_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "SI conversion overflow has its own typed outcome");
        t.check(contains(overflow.result.detail, "converting density"),
                "conversion overflow names the quantity");
    }

    {
        Budget budget;
        budget.max_steps = 0;
        Arena arena;
        Derivation derivation;
        const DensityResult stopped = solve_density(
            arena, derivation,
            problem(DensityVariable::Mass, known(DensityVariable::Density, "4 kg/m^3"),
                    known(DensityVariable::Volume, "3 m^3")),
            budget);
        t.equal(density_outcome_name(stopped.outcome), "resource exceeded",
                "a step budget halts density work");
        t.equal(stopped.detail, "step limit", "the exhausted resource is named");
        t.check(derivation.size() == 0 && stopped.value == kNoNode,
                "a resource halt leaves no partial derivation or answer");
    }
    {
        Budget budget;
        budget.max_steps = 7;
        Arena arena;
        Derivation derivation;
        const DensityResult stopped = solve_density(
            arena, derivation,
            problem(DensityVariable::Mass, known(DensityVariable::Density, "4 kg/m^3"),
                    known(DensityVariable::Volume, "3 m^3")),
            budget);
        t.equal(density_outcome_name(stopped.outcome), "resource exceeded",
                "the density and nested linear steps share one step budget");
        t.equal(stopped.detail, "step limit", "the aggregate step limit is reported");
        // STEP-025. The nested linear solve and the density steps share one budget, so a halt can
        // land in either, and what stays is whatever had been checked when it did.
        t.check(derivation.size() > 0 && derivation.all_verified_from(0),
                "an aggregate budget halt keeps only the checked part of both derivations");
        t.check(stopped.value == kNoNode, "and answers nothing for the quantity it was asked for");
    }
    {
        Budget budget;
        budget.poll = always_cancel;
        Arena arena;
        Derivation derivation;
        const DensityResult stopped = solve_density(
            arena, derivation,
            problem(DensityVariable::Mass, known(DensityVariable::Density, "4 kg/m^3"),
                    known(DensityVariable::Volume, "3 m^3")),
            budget);
        t.evidence("PERF-003", density_outcome_name(stopped.outcome), "cancelled",
                   "an existing cancellation stops a density solve");
        t.check(derivation.size() == 0 && stopped.value == kNoNode,
                "cancellation withdraws partial work and the answer");
    }
    {
        PollAfter poll;
        poll.stop_at = 2;
        Budget budget;
        budget.poll = cancel_after;
        budget.poll_context = &poll;
        Arena arena;
        Derivation derivation;
        const DensityResult stopped = solve_density(
            arena, derivation,
            problem(DensityVariable::Mass, known(DensityVariable::Density, "4 kg/m^3"),
                    known(DensityVariable::Volume, "3 m^3")),
            budget);
        t.equal(density_outcome_name(stopped.outcome), "cancelled",
                "cancellation at the nested linear solve is propagated");
        t.check(derivation.size() > 0 && derivation.all_verified_from(0),
                "and the density provenance recorded before it stays, having been checked");
        t.check(stopped.value == kNoNode, "while the answer does not");
        t.equal(derivation_status_name(derivation.context.derivation_status), "cancelled",
                "and the record does not claim nothing was recorded when something was");
    }
    {
        Limits limits;
        limits.max_nodes = 2;
        Arena arena(limits);
        Derivation derivation;
        const DensityResult stopped = solve_density(
            arena, derivation,
            problem(DensityVariable::Mass, known(DensityVariable::Density, "4 kg/m^3"),
                    known(DensityVariable::Volume, "3 m^3")));
        t.equal(density_outcome_name(stopped.outcome), "resource exceeded",
                "an Arena limit is a resource outcome");
        t.check(derivation.size() == 0 && stopped.value == kNoNode,
                "Arena exhaustion offers no partial proof or value");
    }

    {
        const Run solved = run(problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3")));
        t.check(solved.plan >= 2 && solved.transformations >= 3 && solved.checks >= 3,
                "the result contains density and linear plan, transformation and check records");
        t.check(solved.all_verified, "every recorded density claim has passing evidence");
        t.evidence("PHYS-025", contains(solved.rules, "physics.density.definition") &&
                    contains(solved.rules, "physics.density.check-dimensions") &&
                    contains(solved.rules, "physics.density.substitute") &&
                    contains(solved.rules, "eq.divide-both-sides") &&
                    contains(solved.rules, "physics.density.check-candidate"),
                "the provenance names definition, dimensions, substitution, isolation and check");
        t.check(solved.result.equation != kNoNode && solved.result.substituted != kNoNode &&
                    solved.result.unknown != kNoNode && solved.result.value != kNoNode,
                "the typed result retains the symbolic and substituted models");
        t.equal(solved.context_family, "physics.density.mass-volume",
                "the solution context identifies the density family");
        t.check(solved.result.cost.steps == solved.steps,
                "reported step cost matches the complete derivation");
    }
}

}  // namespace nps
