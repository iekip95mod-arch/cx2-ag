#include <cstdint>
#include <string>
#include <utility>

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

Quantity measured(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity_with_uncertainty(text, &parsed, &error);
    return parsed;
}

DensityKnown uncertain_known(DensityVariable variable, const char *text) {
    DensityKnown entry;
    entry.variable = variable;
    entry.quantity = measured(text);
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
    std::string propagation_evidence;
    std::string propagation_assumption;
    std::string uncertainty_evidence;
    // Kept so a VER-010 case can be held to the record rather than to the returned result alone.
    Derivation derivation;
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
        if (step.rule_id == "physics.density.propagate-uncertainty") {
            for (const VerificationRecord &check : step.verifications) {
                run.propagation_evidence += verification_outcome_name(check.outcome);
                run.propagation_evidence += ", ";
                run.propagation_evidence += check.detail;
            }
            for (const std::string &assumption : step.assumptions_before)
                run.propagation_assumption += assumption;
        }
        if (step.rule_id == "physics.density.check-uncertainty") {
            for (const VerificationRecord &check : step.verifications) {
                run.uncertainty_evidence += verification_outcome_name(check.outcome);
                run.uncertainty_evidence += ", ";
                run.uncertainty_evidence += check.detail;
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
    run.derivation = std::move(derivation);
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
        const Run one =
            run(problem(DensityVariable::Mass,
                        uncertain_known(DensityVariable::Density, "1000 +/- 5 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")));
        t.equal(density_outcome_name(one.result.outcome), "solved",
                "two stated uncertainties still solve the mass");
        t.check(exact_value(one.result, 2, 1), "whose exact value is unaffected by them");
        t.equal(uncertainty_state_name(one.result.quantity.precision.uncertainty), "known",
                "and the route propagates them rather than giving up on them");
        t.check(rational_equal(one.result.quantity.precision.variance, Rational{101, 10000}),
                "the combined variance is the exact first-order sum");
        t.equal(one.result.value_text, "2.00",
                "the value is reported to the decimal place of the uncertainty's last figure");

        const Run two =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "1000 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")));
        t.equal(uncertainty_state_name(two.result.quantity.precision.uncertainty), "known",
                "an exact given contributes no uncertainty of its own");
        t.check(rational_equal(two.result.quantity.precision.variance, Rational{1, 100}),
                "so the variance is the one measured term alone");
        t.equal(two.result.value_text, "2.00", "and the value still follows that term's place");

        const Run three =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "1000.0 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")));
        t.equal(uncertainty_state_name(three.result.quantity.precision.uncertainty), "unstated",
                "a measured given with no stated uncertainty leaves the answer's unknown");
        t.equal(three.result.value_text, "2.0",
                "and the value keeps the significant-figure rule rather than a place it has not got");
        t.evidence("PHYS-020",
                   one.result.value_text + " +/- " + one.result.uncertainty_text + " " +
                       one.result.unit_text,
                   "2.00 +/- 0.11 kg",
                   "a measured density and volume report a mass with the uncertainty they imply");
        t.equal(one.propagation_evidence,
                "passed, dm/dV = 1000, dm/drho = 0.002, giving known, which the squared relative "
                "uncertainties reach too",
                "the propagation records both partials and a second route to the same variance");
        t.equal(one.propagation_assumption, "the two given measurements are independent",
                "and names the assumption first-order quadrature rests on");
        t.equal(one.uncertainty_evidence,
                "passed, 0.11 is the smallest two-figure value whose square covers the variance",
                "the reported root is read back and judged against the exact variance");
        t.equal(derivation_status_name(one.result.status), "solved and verified",
                "so a propagated answer is verified rather than merely solved");
        t.equal(two.result.uncertainty_text, "0.10",
                "an exact root is reported as it stands rather than pushed up a figure");
        t.check(three.result.uncertainty_text.empty() && three.propagation_evidence.empty(),
                "an unpropagated uncertainty reports no root and records no propagation");
        t.equal(derivation_status_name(three.result.status), "solved and verified",
                "and does not leave a sound derivation looking unchecked");

        // The sign of each partial, which the variance squares away and only the record keeps.
        const Run as_density = run(problem(
            DensityVariable::Density, uncertain_known(DensityVariable::Mass, "2.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Volume, "0.50 +/- 0.01 m^3")));
        t.check(contains(as_density.propagation_evidence, "drho/dm = 2, drho/dV = -8"),
                "a density divides by volume, so its volume partial is negative");
        t.equal(as_density.result.value_text + " +/- " + as_density.result.uncertainty_text,
                "4.00 +/- 0.22", "and the root of 29/625 rounds up to two figures");
        const Run as_volume = run(problem(
            DensityVariable::Volume, uncertain_known(DensityVariable::Mass, "2.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Density, "4.0 +/- 0.2 kg/m^3")));
        t.check(contains(as_volume.propagation_evidence, "dV/dm = 0.25, dV/drho = -0.125"),
                "a volume divides by density, so its density partial is negative");
        t.equal(as_volume.result.value_text + " +/- " + as_volume.result.uncertainty_text,
                "0.500 +/- 0.036", "and the root of 1/800 rounds up to two figures");

        const Run prefixed =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "2 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 cm^3")));
        t.equal(uncertainty_state_name(prefixed.result.quantity.precision.uncertainty), "known",
                "a prefixed given carries its uncertainty through the SI conversion");
        t.equal(prefixed.result.value_text + " +/- " + prefixed.result.uncertainty_text,
                "0.00000400 +/- 0.00000020",
                "which is the given uncertainty scaled by the cubic prefix, not by its root");

        const Run exact_pair = run(problem(DensityVariable::Mass,
                                           known(DensityVariable::Density, "4 kg/m^3"),
                                           known(DensityVariable::Volume, "3 m^3")));
        t.evidence("PHYS-020",
                   uncertainty_state_name(exact_pair.result.quantity.precision.uncertainty), "none",
                   "two exact givens carry no uncertainty metadata onto the answer");
        t.check(exact_pair.result.uncertainty_text.empty() &&
                    exact_pair.propagation_evidence.empty(),
                "and reach neither the reported root nor the propagation step");
        const Run measured_pair = run(problem(DensityVariable::Mass,
                                              known(DensityVariable::Density, "4.0 kg/m^3"),
                                              known(DensityVariable::Volume, "3.0 m^3")));
        t.equal(uncertainty_state_name(measured_pair.result.quantity.precision.uncertainty), "none",
                "and measurements that state none between them state none together");

        // The partial, not the stated square, is what outgrows exact arithmetic here.
        const Run huge = run(problem(
            DensityVariable::Density, uncertain_known(DensityVariable::Mass, "10.0 +/- 0.1 kg"),
            known(DensityVariable::Volume, "0.000000001 m^3")));
        t.equal(density_outcome_name(huge.result.outcome), "solved",
                "a partial that does not fit exact arithmetic leaves the value standing");
        t.equal(uncertainty_state_name(huge.result.quantity.precision.uncertainty), "too large",
                "and says the uncertainty outgrew it rather than reporting none");
        t.check(huge.result.uncertainty_text.empty() && huge.propagation_evidence.empty(),
                "with no root reported and no propagation claimed");

        // 0.04 +/- 2: the uncertainty's last figure sits left of the value's leading one, so there
        // is no place to report the value to that keeps a figure of it.
        const Run wide = run(problem(
            DensityVariable::Mass, uncertain_known(DensityVariable::Density, "0.02 +/- 1 kg/m^3"),
            known(DensityVariable::Volume, "2 m^3")));
        t.equal(wide.result.value_text + " +/- " + wide.result.uncertainty_text, "0.04 +/- 2.0",
                "an uncertainty wider than the value keeps the value's own figure count");
        t.check(wide.result.quantity.precision.last_significant_decimal_place == -2,
                "and the reported place stays the value's rather than the uncertainty's");

        // 0.040 +/- 0.20: the root's last figure sits at the value's leading one rather than left of
        // it, so one figure of the value survives there and the value follows the root.
        const Run at_lead = run(problem(
            DensityVariable::Mass, uncertain_known(DensityVariable::Density, "0.020 +/- 0.1 kg/m^3"),
            known(DensityVariable::Volume, "2 m^3")));
        t.equal(at_lead.result.value_text + " +/- " + at_lead.result.uncertainty_text,
                "0.04 +/- 0.20", "an uncertainty reaching the value's leading figure is still followed");
        t.check(at_lead.result.quantity.precision.last_significant_decimal_place == -2 &&
                    at_lead.result.quantity.precision.significant_digits == 1,
                "leaving the value the one figure that place keeps of it");

        // A stated spread of zero, #588. Zero has no two-figure root, so the answer reports none and
        // keeps the figures the same givens without the spread give it.
        const Run zero_spread =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "1000 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0 m^3")));
        const Run no_spread =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "1000 kg/m^3"),
                        known(DensityVariable::Volume, "0.0020 m^3")));
        t.equal(zero_spread.result.value_text, no_spread.result.value_text,
                "a stated spread of zero costs the answer none of its figures");
        t.equal(zero_spread.result.value_text, "2.0",
                "which is the significant-figure rule's own answer");
        t.check(zero_spread.result.uncertainty_text.empty() &&
                    zero_spread.uncertainty_evidence.empty(),
                "with no root reported and no rounding check claiming one");
        t.equal(uncertainty_state_name(zero_spread.result.quantity.precision.uncertainty),
                "not propagated",
                "and a first-order variance of zero says so rather than reading as a known zero");
        t.equal(derivation_status_name(zero_spread.result.status), "solved and verified",
                "while the derivation it came from still stands");
        const Run zero_spread_prefixed =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "2 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "2.0 +/- 0 cm^3")));
        t.equal(uncertainty_state_name(zero_spread_prefixed.result.quantity.precision.uncertainty),
                "not propagated",
                "and a prefixed given's zero spread reads the same as an unprefixed one");

        // A zero answer leaves the relative form nothing to divide by, and the definition's own
        // variance identity closes exactly there because one of its two terms carries a zero factor.
        const Run zero_density = run(problem(
            DensityVariable::Mass, uncertain_known(DensityVariable::Density, "0.0 +/- 0.1 kg/m^3"),
            uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 m^3")));
        t.check(rational_equal(zero_density.result.quantity.precision.variance, Rational{1, 25}),
                "a measured density of zero leaves the volume squared times its own variance");
        t.equal(zero_density.result.value_text + " +/- " + zero_density.result.uncertainty_text,
                "0.00 +/- 0.20", "and the zero answer is written to the root's place like any other");
        t.equal(derivation_status_name(zero_density.result.status), "solved and verified",
                "with the propagation witnessed rather than left unchecked");
        t.check(contains(zero_density.propagation_evidence,
                         "which the density definition's own variance identity reaches too"),
                "by the form that closes when there is no answer to divide by");
        const Run zero_volume = run(problem(
            DensityVariable::Mass, uncertain_known(DensityVariable::Density, "2.0 +/- 0.1 kg/m^3"),
            uncertain_known(DensityVariable::Volume, "0.0 +/- 0.1 m^3")));
        const Run zero_mass_density = run(problem(
            DensityVariable::Density, uncertain_known(DensityVariable::Mass, "0.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 m^3")));
        const Run zero_mass_volume = run(problem(
            DensityVariable::Volume, uncertain_known(DensityVariable::Mass, "0.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Density, "4.0 +/- 0.2 kg/m^3")));
        t.equal(std::string(derivation_status_name(zero_volume.result.status)) + ", " +
                    derivation_status_name(zero_mass_density.result.status) + ", " +
                    derivation_status_name(zero_mass_volume.result.status),
                "solved and verified, solved and verified, solved and verified",
                "and the three other shapes that reach a zero answer are witnessed the same way");
        t.equal(zero_volume.result.value_text + " +/- " + zero_volume.result.uncertainty_text,
                "0.00 +/- 0.20", "a zero volume leaves the density term alone");
        t.equal(zero_mass_density.result.value_text + " +/- " +
                    zero_mass_density.result.uncertainty_text,
                "0.000 +/- 0.050", "a zero density from a zero mass takes the mass term alone");
        t.equal(zero_mass_volume.result.value_text + " +/- " +
                    zero_mass_volume.result.uncertainty_text,
                "0.000 +/- 0.025", "and so does the zero volume a zero mass gives");

        // The identity needs a given squared, which four thousand million cubic meters does not
        // leave room for, so nothing witnesses the propagation and the uncertainty is withheld.
        const Run unwitnessed = run(problem(
            DensityVariable::Mass, uncertain_known(DensityVariable::Density, "0.0 +/- 0.1 kg/m^3"),
            uncertain_known(DensityVariable::Volume, "4000000000.0 +/- 0.1 m^3")));
        t.equal(density_outcome_name(unwitnessed.result.outcome), "solved",
                "a check that outgrows exact arithmetic leaves the answer standing");
        t.equal(derivation_status_name(unwitnessed.result.status), "solved and verified",
                "and the derivation of that answer reads verified rather than unchecked");
        t.equal(uncertainty_state_name(unwitnessed.result.quantity.precision.uncertainty),
                "too large", "with the state saying the uncertainty is what outgrew the arithmetic");
        t.check(unwitnessed.result.uncertainty_text.empty() &&
                    unwitnessed.propagation_evidence.empty(),
                "so no root is reported and no propagation claim is left for nothing to witness");
        t.equal(unwitnessed.result.value_text, "0.0",
                "while the value keeps the significant-figure report it would have had anyway");

        // Overflow is not a zero answer, and the definition identity holds only at the zero. These
        // three have no zero in them, so an overflowing relative form leaves nothing to witness.
        const Run wide_density = run(problem(
            DensityVariable::Density,
            uncertain_known(DensityVariable::Mass, "2000000000.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 m^3")));
        const Run wide_volume = run(problem(
            DensityVariable::Volume,
            uncertain_known(DensityVariable::Mass, "2000000000.0 +/- 0.1 kg"),
            uncertain_known(DensityVariable::Density, "2.0 +/- 0.1 kg/m^3")));
        const Run wide_mass = run(problem(
            DensityVariable::Mass,
            uncertain_known(DensityVariable::Density, "2000000000.0 +/- 0.1 kg/m^3"),
            uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 m^3")));
        t.equal(std::string(density_outcome_name(wide_density.result.outcome)) + ", " +
                    density_outcome_name(wide_volume.result.outcome) + ", " +
                    density_outcome_name(wide_mass.result.outcome),
                "solved, solved, solved",
                "an overflowing propagation is not a verification failure for any unknown");
        t.equal(std::string(derivation_status_name(wide_density.result.status)) + ", " +
                    derivation_status_name(wide_volume.result.status) + ", " +
                    derivation_status_name(wide_mass.result.status),
                "solved and verified, solved and verified, solved and verified",
                "and each value stands on the checks that did run");
        t.equal(wide_density.result.value_text + ", " + wide_volume.result.value_text + ", " +
                    wide_mass.result.value_text,
                "1000000000, 1000000000, 4000000000",
                "at the figures the givens between them were written with");
        t.equal(std::string(uncertainty_state_name(
                    wide_density.result.quantity.precision.uncertainty)) +
                    ", " +
                    uncertainty_state_name(wide_volume.result.quantity.precision.uncertainty) +
                    ", " + uncertainty_state_name(wide_mass.result.quantity.precision.uncertainty),
                "too large, too large, too large",
                "with the uncertainty withheld rather than failed against an identity that does not "
                "hold for a quotient");
        t.check(wide_density.result.uncertainty_text.empty() &&
                    wide_volume.result.uncertainty_text.empty() &&
                    wide_mass.result.uncertainty_text.empty(),
                "and no root offered for any of the three");
    }
    {
        // VER-010 for the two rules PHYS-020 adds. Their positive cases come off the golden
        // fixture, so these are the boundary and regression kinds the invariant pass cannot read.
        using nps_tools::RuleCaseKind;
        const Run exact_given =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "1000 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")));
        t.rule_case("physics.density.propagate-uncertainty", RuleCaseKind::Boundary,
                    exact_given.derivation,
                    rational_equal(exact_given.result.quantity.precision.variance,
                                   Rational{1, 100}) &&
                        contains(exact_given.propagation_evidence, "dm/drho = 0.002"),
                    "an exact given still has a partial and contributes no variance through it");

        // 9901/1000000 is the variance whose root is 0.099 before rounding, so the round up
        // reaches a third figure and the reported place moves one left to keep two.
        const Run carried =
            run(problem(DensityVariable::Mass,
                        uncertain_known(DensityVariable::Density, "1000 +/- 5 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.000099 m^3")));
        t.check(rational_equal(carried.result.quantity.precision.variance, Rational{9901, 1000000}),
                "a volume uncertainty two places finer gives a variance just under a hundredth");
        t.rule_case("physics.density.check-uncertainty", RuleCaseKind::Boundary, carried.derivation,
                    carried.result.uncertainty_text == "0.10" &&
                        carried.result.quantity.precision.last_significant_decimal_place == -2,
                    "a root rounding up into a third figure is reported one place left as 0.10");

        // The scale, not the root, is what the cubic prefix multiplies the variance by.
        const Run prefixed_case =
            run(problem(DensityVariable::Mass, known(DensityVariable::Density, "2 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "2.0 +/- 0.1 cm^3")));
        const bool scaled_through =
            prefixed_case.result.uncertainty_text == "0.00000020" &&
            prefixed_case.result.quantity.precision.uncertainty == UncertaintyState::Known;
        t.rule_case("physics.density.propagate-uncertainty", RuleCaseKind::Regression,
                    prefixed_case.derivation, scaled_through,
                    "a prefixed given's variance is scaled by the SI scale squared, #161");
        t.rule_case("physics.density.check-uncertainty", RuleCaseKind::Regression,
                    prefixed_case.derivation, scaled_through,
                    "and the root checked back is the scaled one rather than the typed one, #161");
    }
    {
        // The eleven recorded steps of the first worked example, with the propagation ninth.
        Budget budget;
        budget.max_steps = 8;
        const Run stopped =
            run(problem(DensityVariable::Mass,
                        uncertain_known(DensityVariable::Density, "1000 +/- 5 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")),
                budget);
        t.equal(density_outcome_name(stopped.result.outcome), "resource exceeded",
                "a budget that runs out at the propagation halts there");
        t.check(contains(stopped.rules, "physics.density.check-candidate") &&
                    !contains(stopped.rules, "physics.density.propagate-uncertainty"),
                "keeping the checked work before it and recording no propagation");
        t.check(stopped.result.value == kNoNode && stopped.result.uncertainty_text.empty(),
                "and answering neither the value nor its uncertainty");

        Budget later;
        later.max_steps = 9;
        const Run at_check =
            run(problem(DensityVariable::Mass,
                        uncertain_known(DensityVariable::Density, "1000 +/- 5 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")),
                later);
        t.equal(density_outcome_name(at_check.result.outcome), "resource exceeded",
                "one step further the uncertainty check is where it runs out");
        t.check(contains(at_check.rules, "physics.density.propagate-uncertainty") &&
                    !contains(at_check.rules, "physics.density.check-uncertainty"),
                "keeping the propagation and recording no check of its reported root");
        t.check(at_check.result.uncertainty_text.empty(),
                "which is not reported, having been checked by nothing");

        PollAfter poll;
        poll.stop_at = 3;
        Budget canceling;
        canceling.poll = cancel_after;
        canceling.poll_context = &poll;
        const Run canceled =
            run(problem(DensityVariable::Mass,
                        uncertain_known(DensityVariable::Density, "1000 +/- 5 kg/m^3"),
                        uncertain_known(DensityVariable::Volume, "0.0020 +/- 0.0001 m^3")),
                canceling);
        t.equal(density_outcome_name(canceled.result.outcome), "cancelled",
                "a canceled uncertain solve answers the same way one with no uncertainty does");
        t.check(!contains(canceled.rules, "physics.density.propagate-uncertainty") &&
                    !contains(canceled.rules, "physics.density.check-uncertainty"),
                "reaching neither new step, because the poll stops inside the linear solve");
        t.check(canceled.result.uncertainty_text.empty(),
                "and reports no uncertainty, having checked none");
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
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[0].quantity.precision.uncertainty = UncertaintyState::Known;
        input.knowns[0].quantity.precision.variance = Rational{1, 100};
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "an exact quantity carrying a stated uncertainty is rejected rather than propagated");
        t.check(contains(refused.result.detail, "an exact value cannot state an uncertainty"),
                "and the refusal says which half of the precision disagrees with the other");
    }
    {
        DensityProblem input = problem(DensityVariable::Mass,
                                       known(DensityVariable::Density, "4.0 kg/m^3"),
                                       known(DensityVariable::Volume, "3 m^3"));
        input.knowns[0].quantity.precision.variance = Rational{1, 100};
        const Run refused = run(input);
        t.equal(density_outcome_name(refused.result.outcome), "invalid problem",
                "and so is a measurement that says it states none while carrying a variance");
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
        t.equal(density_outcome_name(overflow.result.outcome), "resource exceeded",
                "an exact product beyond int64 arrives as the solver's own capacity refusal rather "
                "than through a status sniff");
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
