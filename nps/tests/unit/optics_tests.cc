#include <string>

#include "nps/physics/optics.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity quantity(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

// The same spelling read as an exact given, so a teaching case can pin the exact answer without
// the measured rounding step deciding the text for it.
Quantity exact_quantity(const char *text) {
    Quantity parsed = quantity(text);
    parsed.precision = Precision();
    return parsed;
}

OpticsKnown known(OpticsVariable variable, const char *text) {
    OpticsKnown entry;
    entry.variable = variable;
    entry.quantity = exact_quantity(text);
    return entry;
}

OpticsKnown measured(OpticsVariable variable, const char *text) {
    OpticsKnown entry;
    entry.variable = variable;
    entry.quantity = quantity(text);
    return entry;
}

OpticsProblem problem(OpticsRelation relation, OpticsVariable unknown,
                      const std::vector<OpticsKnown> &knowns) {
    OpticsProblem input;
    input.relation = relation;
    input.unknown = unknown;
    input.knowns = knowns;
    return input;
}

struct Run {
    OpticsResult result;
    size_t steps = 0;
    size_t checks = 0;
    bool all_verified = true;
    std::string rules;
    std::string context_family;
    std::string assumptions;
    std::string unverified;
};

Run run(const OpticsProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    Run out;
    out.result = solve_optics(arena, derivation, input, budget);
    out.steps = derivation.size();
    out.context_family = derivation.context.problem_family_id;
    for (const std::string &assumption : derivation.context.active_assumptions) {
        out.assumptions += assumption;
        out.assumptions += " | ";
    }
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (!out.rules.empty())
            out.rules += ' ';
        out.rules += step.rule_id;
        out.all_verified = out.all_verified && step.verified();
        if (!step.verified()) {
            out.unverified += step.rule_id;
            for (const VerificationRecord &v : step.verifications) {
                if (v.outcome == VerificationOutcome::Passed)
                    continue;
                out.unverified += "[" + v.evidence_id + "=" +
                                  verification_outcome_name(v.outcome) + ":" + v.detail + "]";
            }
            out.unverified += ' ';
        }
        if (step.phase == "check")
            ++out.checks;
    }
    return out;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

bool always_cancel(void *) { return true; }

}  // namespace

void run_optics_tests(TestSink &t) {
    {
        const Run solved =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "2"),
                         known(OpticsVariable::SineIncident, "0.3"),
                         known(OpticsVariable::IndexTransmitted, "1")}));
        t.equal(optics_outcome_name(solved.result.outcome), "solved",
                "Snell's law isolates the transmitted sine");
        t.equal(solved.result.value_text, "0.6", "the transmitted sine is exactly 2*0.3/1");
        t.check(solved.result.has_critical_sine && solved.result.critical_sine_text == "0.5",
                "a refraction solve reports the critical sine n2/n1");
        t.equal(solved.context_family, "physics.optics.refraction.snell",
                "the solution context identifies the refraction family");
        t.equal(solved.unverified, "", "every recorded refraction claim has passing evidence");
        t.evidence("PHYS-022",
                   contains(solved.rules, "physics.optics.refraction.snell") &&
                       contains(solved.rules, "physics.optics.check-dimensions") &&
                       contains(solved.rules, "physics.optics.check-domain") &&
                       contains(solved.rules, "physics.optics.sign-convention") &&
                       contains(solved.rules, "physics.optics.substitute") &&
                       contains(solved.rules, "physics.optics.check-candidate"),
                   "refraction provenance names the plan, dimensions, domain, convention, "
                   "substitution and final check");
        t.check(solved.result.equation != kNoNode && solved.result.substituted != kNoNode &&
                    solved.result.unknown != kNoNode && solved.result.value != kNoNode,
                "the typed refraction result retains the symbolic and substituted models");
        t.check(solved.result.cost.steps == solved.steps,
                "reported refraction step cost matches the complete derivation");
    }

    {
        const Run reflected =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "2"),
                         known(OpticsVariable::SineIncident, "0.8"),
                         known(OpticsVariable::IndexTransmitted, "1")}));
        t.evidence("PHYS-022", optics_outcome_name(reflected.result.outcome),
                   "total internal reflection",
                   "an incident sine past the critical sine refuses with total internal reflection");
        t.equal(reflected.result.detail,
                "the transmitted sine would be 1.6, above the critical sine 0.5",
                "the refusal reports the sine Snell's law would have needed and the critical sine "
                "the ray passed");
        t.check(reflected.result.outcome == OpticsOutcome::TotalInternalReflection &&
                    reflected.result.value == kNoNode && reflected.result.value_text.empty(),
                "a totally reflected ray withholds the transmitted sine under the reflection "
                "conclusion rather than under a domain refusal");
        t.check(contains(reflected.rules, "physics.optics.total-internal-reflection") &&
                    reflected.result.critical_sine_text == "0.5",
                "the refusal records the critical-sine rule and the sine that was passed");
        t.check(reflected.result.status == DerivationStatus::SolvedAndVerified &&
                    reflected.unverified.empty(),
                "total internal reflection is a verified conclusion rather than a failed solve");
    }

    {
        const Run entering =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "1"),
                         known(OpticsVariable::SineIncident, "0.6"),
                         known(OpticsVariable::IndexTransmitted, "1.5")}));
        t.equal(optics_outcome_name(entering.result.outcome), "solved",
                "Snell's law isolates the transmitted sine entering the denser medium");
        t.equal(entering.result.value_text, "0.4", "the transmitted sine is exactly 1*0.6/1.5");
        t.check(!entering.result.has_critical_sine && entering.result.critical_sine_text.empty(),
                "the denser direction has no critical angle and reports no critical sine");
    }

    {
        const Run same_medium =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "1.5"),
                         known(OpticsVariable::SineIncident, "0.6"),
                         known(OpticsVariable::IndexTransmitted, "1.5")}));
        t.equal(optics_outcome_name(same_medium.result.outcome), "solved",
                "equal indices leave the ray undeviated");
        t.equal(same_medium.result.value_text, "0.6",
                "the transmitted sine equals the incident sine within one medium");
        t.check(same_medium.result.has_critical_sine &&
                    same_medium.result.critical_sine_text == "1",
                "equal indices put the critical sine at grazing incidence");
    }

    {
        const Run incident_index =
            run(problem(OpticsRelation::Refraction, OpticsVariable::IndexIncident,
                        {known(OpticsVariable::SineIncident, "0.3"),
                         known(OpticsVariable::IndexTransmitted, "1"),
                         known(OpticsVariable::SineTransmitted, "0.6")}));
        t.equal(optics_outcome_name(incident_index.result.outcome), "solved",
                "Snell's law isolates the incident index");
        t.equal(incident_index.result.value_text, "2", "the incident index is exactly 1*0.6/0.3");
        t.check(!incident_index.result.has_critical_sine &&
                    incident_index.result.critical_sine_text.empty(),
                "a solve for the incident index reports no critical sine, because the ratio it "
                "would be read from is the answer");
        t.equal(incident_index.unverified, "",
                "every recorded incident index claim has passing evidence");
    }

    {
        const Run incident_sine =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineIncident,
                        {known(OpticsVariable::IndexIncident, "2"),
                         known(OpticsVariable::IndexTransmitted, "1"),
                         known(OpticsVariable::SineTransmitted, "0.6")}));
        t.equal(optics_outcome_name(incident_sine.result.outcome), "solved",
                "Snell's law isolates the incident sine");
        t.equal(incident_sine.result.value_text, "0.3", "the incident sine is exactly 1*0.6/2");
        t.check(incident_sine.result.has_critical_sine &&
                    incident_sine.result.critical_sine_text == "0.5",
                "a solve for the incident sine still reports the critical sine n2/n1");
    }

    {
        const Run transmitted_index =
            run(problem(OpticsRelation::Refraction, OpticsVariable::IndexTransmitted,
                        {known(OpticsVariable::IndexIncident, "2"),
                         known(OpticsVariable::SineIncident, "0.3"),
                         known(OpticsVariable::SineTransmitted, "0.6")}));
        t.equal(optics_outcome_name(transmitted_index.result.outcome), "solved",
                "Snell's law isolates the transmitted index");
        t.equal(transmitted_index.result.value_text, "1",
                "the transmitted index is exactly 2*0.3/0.6");
        t.check(!transmitted_index.result.has_critical_sine &&
                    transmitted_index.result.critical_sine_text.empty(),
                "a solve for the transmitted index reports no critical sine");
    }

    {
        const Run fractional_order =
            run(problem(OpticsRelation::DoubleSlit, OpticsVariable::FringeOrder,
                        {known(OpticsVariable::SlitSpacing, "1 mm"),
                         known(OpticsVariable::SineFringe, "0.00125"),
                         known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(fractional_order.result.outcome), "unphysical value",
                "a fringe order between two integers is refused rather than reported");
        t.check(contains(fractional_order.result.detail, "a fringe order is an integer"),
                "the refusal says the isolated order is not an integer");
    }

    {
        const Run lens = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                     {known(OpticsVariable::FocalLength, "10 cm"),
                                      known(OpticsVariable::ObjectDistance, "15 cm")}));
        t.equal(optics_outcome_name(lens.result.outcome), "solved",
                "the thin lens equation isolates the image distance");
        t.equal(lens.result.value_text, "0.3", "an object at 15 cm images 30 cm behind a 10 cm lens");
        t.equal(lens.result.unit_text, "m", "the lens answer is reported in SI metres");
        t.check(lens.result.has_magnification && lens.result.magnification_text == "-2",
                "a real lens image carries its inverted lateral magnification");
        t.check(contains(lens.result.convention, "do > 0") &&
                    contains(lens.assumptions, "real side"),
                "the lens sign convention is stated in the result and in the solve context");
        t.equal(lens.unverified, "", "every recorded thin lens claim has passing evidence");
    }

    {
        const Run focal = run(problem(OpticsRelation::ThinLens, OpticsVariable::FocalLength,
                                      {known(OpticsVariable::ObjectDistance, "15 cm"),
                                       known(OpticsVariable::ImageDistance, "30 cm")}));
        t.equal(optics_outcome_name(focal.result.outcome), "solved",
                "the thin lens equation isolates the focal length");
        t.equal(focal.result.value_text, "0.1",
                "an object at 15 cm imaged at 30 cm needs a 10 cm lens");
        t.equal(focal.result.unit_text, "m", "the focal length is reported in SI metres");
        t.check(focal.result.has_magnification && focal.result.magnification_text == "-2",
                "a focal length answer still carries the magnification of the pair it came from");
        t.equal(focal.unverified, "", "every recorded focal length claim has passing evidence");
    }

    {
        const Run object = run(problem(OpticsRelation::ThinLens, OpticsVariable::ObjectDistance,
                                       {known(OpticsVariable::FocalLength, "10 cm"),
                                        known(OpticsVariable::ImageDistance, "30 cm")}));
        t.equal(optics_outcome_name(object.result.outcome), "solved",
                "the thin lens equation isolates the object distance");
        t.equal(object.result.value_text, "0.15",
                "a 10 cm lens imaging at 30 cm had its object 15 cm in front of it");
        t.check(object.result.has_magnification && object.result.magnification_text == "-2",
                "an object distance answer carries the same inverted magnification");
    }

    {
        const Run virtual_image =
            run(problem(OpticsRelation::SphericalMirror, OpticsVariable::ImageDistance,
                        {known(OpticsVariable::FocalLength, "10 cm"),
                         known(OpticsVariable::ObjectDistance, "5 cm")}));
        t.equal(optics_outcome_name(virtual_image.result.outcome), "solved",
                "an object inside the focal length still solves");
        t.equal(virtual_image.result.value_text, "-0.1",
                "an object inside the focal length gives a virtual image behind the mirror");
        t.check(virtual_image.result.magnification_text == "2",
                "the virtual mirror image is upright and magnified");
        t.equal(virtual_image.context_family, "physics.optics.spherical-mirror.image",
                "the mirror relation keeps its own family identity");
    }

    {
        const Run mirror_focal =
            run(problem(OpticsRelation::SphericalMirror, OpticsVariable::FocalLength,
                        {known(OpticsVariable::ObjectDistance, "20 cm"),
                         known(OpticsVariable::ImageDistance, "20 cm")}));
        t.equal(optics_outcome_name(mirror_focal.result.outcome), "solved",
                "the mirror equation isolates the focal length");
        t.equal(mirror_focal.result.value_text, "0.1",
                "an object and its image both at 20 cm sit at the centre of curvature of a 10 cm "
                "mirror");
        t.check(mirror_focal.result.magnification_text == "-1",
                "an object at the centre of curvature images inverted at the same size");
        t.equal(mirror_focal.context_family, "physics.optics.spherical-mirror.image",
                "the mirror keeps its family identity when it is asked for the focal length");
    }

    {
        const Run indeterminate =
            run(problem(OpticsRelation::ThinLens, OpticsVariable::FocalLength,
                        {known(OpticsVariable::ObjectDistance, "1 m"),
                         known(OpticsVariable::ImageDistance, "-1 m")}));
        t.equal(optics_outcome_name(indeterminate.result.outcome), "indeterminate",
                "reciprocals that cancel do not determine a focal length");
        t.check(indeterminate.result.value == kNoNode,
                "an indeterminate lens problem offers no focal length");
    }

    {
        const Run fringe = run(problem(OpticsRelation::DoubleSlit, OpticsVariable::SineFringe,
                                       {known(OpticsVariable::SlitSpacing, "1 mm"),
                                        known(OpticsVariable::FringeOrder, "2"),
                                        known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(fringe.result.outcome), "solved",
                "the two-slit relation isolates the fringe sine");
        t.equal(fringe.result.value_text, "0.001",
                "the second bright fringe of a 500 nm source through a 1 mm spacing");
        t.equal(fringe.context_family, "physics.optics.double-slit.maxima",
                "the two-slit relation keeps its own family identity");
        t.check(contains(fringe.rules, "physics.optics.convert-units"),
                "a millimetre spacing is converted before the two sides are compared");
    }

    {
        const Run spacing = run(problem(OpticsRelation::DoubleSlit, OpticsVariable::SlitSpacing,
                                        {known(OpticsVariable::SineFringe, "0.001"),
                                         known(OpticsVariable::FringeOrder, "2"),
                                         known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(spacing.result.outcome), "solved",
                "the two-slit relation isolates the slit spacing");
        t.equal(spacing.result.value_text, "0.001",
                "the spacing that puts the second 500 nm fringe at a sine of 0.001");
        t.equal(spacing.result.unit_text, "m", "the slit spacing is reported in SI metres");
        t.equal(spacing.unverified, "",
                "every recorded slit spacing claim has passing evidence");
    }

    {
        const Run central = run(problem(OpticsRelation::SingleSlit, OpticsVariable::Wavelength,
                                        {known(OpticsVariable::SlitSpacing, "1 mm"),
                                         known(OpticsVariable::FringeOrder, "0"),
                                         known(OpticsVariable::SineFringe, "0.001")}));
        t.equal(optics_outcome_name(central.result.outcome), "unphysical value",
                "order zero is the single-slit central maximum rather than a minimum");
        t.check(contains(central.result.detail, "central maximum"),
                "the single-slit refusal names the rule it applied");
        t.check(contains(central.rules, "physics.optics.check-domain"),
                "the domain check is the record that refuses it");
    }

    {
        const Run width = run(problem(OpticsRelation::SingleSlit, OpticsVariable::SlitSpacing,
                                      {known(OpticsVariable::SineFringe, "0.0005"),
                                       known(OpticsVariable::FringeOrder, "1"),
                                       known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(width.result.outcome), "solved",
                "the single-slit relation isolates the slit width");
        t.equal(width.result.value_text, "0.001",
                "the width that puts the first 500 nm minimum at a sine of 0.0005");
        t.equal(width.context_family, "physics.optics.single-slit.minima",
                "the single-slit relation keeps its own family identity");
    }

    {
        const Run order_zero = run(problem(OpticsRelation::DoubleSlit, OpticsVariable::SineFringe,
                                           {known(OpticsVariable::SlitSpacing, "1 mm"),
                                            known(OpticsVariable::FringeOrder, "0"),
                                            known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(order_zero.result.outcome), "solved",
                "order zero is the two-slit central maximum and is admissible there");
        t.equal(order_zero.result.value_text, "0",
                "the two-slit central maximum sits on the axis");
    }

    {
        const Run steep = run(problem(OpticsRelation::DoubleSlit, OpticsVariable::SineFringe,
                                      {known(OpticsVariable::SlitSpacing, "0.000001 m"),
                                       known(OpticsVariable::FringeOrder, "3"),
                                       known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(steep.result.outcome), "unphysical value",
                "a fringe order the spacing cannot reach is refused rather than reported");
        t.check(contains(steep.result.detail, "above one"),
                "the refusal says the isolated sine passed one");
    }

    {
        const Run low_index =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "0.5"),
                         known(OpticsVariable::SineIncident, "0.3"),
                         known(OpticsVariable::IndexTransmitted, "1")}));
        t.equal(optics_outcome_name(low_index.result.outcome), "unphysical value",
                "a refractive index below one is refused");
        const Run wide_sine =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {known(OpticsVariable::IndexIncident, "2"),
                         known(OpticsVariable::SineIncident, "1.5"),
                         known(OpticsVariable::IndexTransmitted, "1")}));
        t.equal(optics_outcome_name(wide_sine.result.outcome), "unphysical value",
                "a given sine above one is refused");
    }

    {
        const Run mismatched = run(problem(OpticsRelation::DoubleSlit, OpticsVariable::SineFringe,
                                           {known(OpticsVariable::SlitSpacing, "1 kg"),
                                            known(OpticsVariable::FringeOrder, "2"),
                                            known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(mismatched.result.outcome), "dimension mismatch",
                "a slit spacing given as a mass is refused before substitution");
        const Run missing = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                        {known(OpticsVariable::FocalLength, "10 cm")}));
        t.equal(optics_outcome_name(missing.result.outcome), "missing known",
                "a lens problem without the object distance is refused");
        const Run duplicate = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                          {known(OpticsVariable::FocalLength, "10 cm"),
                                           known(OpticsVariable::ObjectDistance, "15 cm"),
                                           known(OpticsVariable::ObjectDistance, "20 cm")}));
        t.equal(optics_outcome_name(duplicate.result.outcome), "duplicate known",
                "a lens problem giving the object distance twice is refused");
        const Run foreign = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                        {known(OpticsVariable::FocalLength, "10 cm"),
                                         known(OpticsVariable::ObjectDistance, "15 cm"),
                                         known(OpticsVariable::Wavelength, "0.0000005 m")}));
        t.equal(optics_outcome_name(foreign.result.outcome), "invalid problem",
                "a wavelength is not a variable of the thin lens equation");
    }

    {
        const Run rounded =
            run(problem(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                        {measured(OpticsVariable::IndexIncident, "1.50"),
                         measured(OpticsVariable::SineIncident, "0.40"),
                         measured(OpticsVariable::IndexTransmitted, "1.00")}));
        t.equal(optics_outcome_name(rounded.result.outcome), "solved",
                "measured refraction givens still solve");
        t.equal(rounded.result.value_text, "0.60",
                "the answer follows the fewest significant figures among the measured givens");
        t.check(contains(rounded.rules, "physics.optics.significant-figures"),
                "the measured report is a recorded step rather than an unexplained rounding");
    }

    {
        Budget budget;
        budget.max_steps = 2;
        const Run halted = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                       {known(OpticsVariable::FocalLength, "10 cm"),
                                        known(OpticsVariable::ObjectDistance, "15 cm")}),
                               budget);
        t.equal(optics_outcome_name(halted.result.outcome), "resource exceeded",
                "an optics solve stops on its step budget");
        t.check(halted.result.value == kNoNode && halted.result.value_text.empty(),
                "a halted optics solve offers no answer");
        Budget cancelled;
        cancelled.poll = always_cancel;
        const Run stopped = run(problem(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                                        {known(OpticsVariable::FocalLength, "10 cm"),
                                         known(OpticsVariable::ObjectDistance, "15 cm")}),
                                cancelled);
        t.equal(optics_outcome_name(stopped.result.outcome), "cancelled",
                "a cancelled optics solve reports cancellation rather than a failure");
    }
}

}  // namespace nps
