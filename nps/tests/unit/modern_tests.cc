#include <cstdint>
#include <string>

#include "nps/core/print.h"
#include "nps/physics/modern.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity declared(ModernVariable variable, const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    // The family reads its own working units, which the unit table does not carry, so the test
    // attaches the declared unit the way the bridge does.
    Unit unit;
    unit.text = modern_variable_unit(variable);
    switch (variable) {
        case ModernVariable::Wavelength: unit.dimension.length = 1; break;
        case ModernVariable::MassDefect: unit.dimension.mass = 1; break;
        default:
            unit.dimension.length = 2;
            unit.dimension.mass = 1;
            unit.dimension.time = -2;
            break;
    }
    unit.scale.num = 1;
    unit.scale.den = 1;
    parsed.unit = unit;
    return parsed;
}

ModernKnown known(ModernVariable variable, const char *text) {
    ModernKnown entry;
    entry.variable = variable;
    entry.quantity = declared(variable, text);
    return entry;
}

ModernProblem problem(ModernRelation relation, ModernVariable unknown) {
    ModernProblem input;
    input.relation = relation;
    input.unknown = unknown;
    return input;
}

struct Run {
    ModernResult result;
    size_t steps = 0;
    std::string rules;
    std::string context_family;
    std::string substituted;
    std::string equation;
    bool has_unverified = false;
};

Run run(const ModernProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    Run out;
    out.result = solve_modern(arena, derivation, input, budget);
    out.steps = derivation.size();
    out.context_family = derivation.context.problem_family_id;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        out.rules += step.rule_id;
        out.rules += ";";
        for (const VerificationRecord &record : step.verifications) {
            if (record.outcome == VerificationOutcome::Failed)
                out.has_unverified = true;
        }
    }
    if (out.result.substituted != kNoNode)
        out.substituted = print(arena, out.result.substituted);
    if (out.result.equation != kNoNode)
        out.equation = print(arena, out.result.equation);
    return out;
}

bool contains_text(const std::string &haystack, const char *needle) {
    return haystack.find(needle) != std::string::npos;
}

struct PollAfter {
    size_t calls = 0;
    size_t stop_at = 0;
};

bool poll_after(void *context) {
    PollAfter *poll = static_cast<PollAfter *>(context);
    ++poll->calls;
    return poll->calls >= poll->stop_at;
}

}  // namespace

void run_modern_tests(TestSink &t) {
    {
        // A 620.0 nm photon is 1239.8/620.0, reported to the four figures the wavelength carries,
        // which is fewer than the five the tabulated constant is quoted to.
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        input.knowns.push_back(known(ModernVariable::Wavelength, "620.0"));
        const Run solved = run(input);
        t.equal(modern_outcome_name(solved.result.outcome), "solved",
                "a wavelength gives its photon energy");
        t.equal(solved.result.value_text, "2.000", "the photon energy reports to four figures");
        t.equal(solved.result.unit_text, "eV", "the photon energy is reported in electronvolts");
        t.equal(solved.equation, "((E * lambda) = (12398 * (10^-1)))",
                "the model is the Planck relation with its tabulated constant");
        t.equal(solved.context_family, "physics.modern.photon-wavelength",
                "the context names the photon family");
        t.check(!solved.has_unverified, "no recorded verification failed");
        t.check(solved.rules.find("physics.modern.substitute") <
                    solved.rules.find("physics.modern.check-candidate"),
                "substitution is recorded before the final check");
    }
    {
        // The same relation read the other way is the de Broglie wavelength of a momentum in eV.
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::Wavelength);
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "1239.8"));
        const Run solved = run(input);
        t.equal(modern_outcome_name(solved.result.outcome), "solved",
                "an energy gives its wavelength");
        t.equal(solved.result.value_text, "1.0000", "the wavelength reports to five figures");
        t.equal(solved.result.unit_text, "nm", "the wavelength is reported in nanometres");
    }
    {
        ModernProblem input = problem(ModernRelation::Photoelectric,
                                      ModernVariable::KineticEnergy);
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "3.50"));
        input.knowns.push_back(known(ModernVariable::WorkFunction, "2.30"));
        const Run solved = run(input);
        t.equal(modern_outcome_name(solved.result.outcome), "solved",
                "a photon above the threshold gives a maximum kinetic energy");
        t.equal(solved.result.value_text, "1.20", "the photoelectric answer keeps three figures");
        t.equal(solved.equation, "(Kmax = (E + (-phi)))",
                "the model is the Einstein photoelectric equation");
    }
    {
        // Below the threshold the surface emits nothing, which is a refusal rather than a
        // negative kinetic energy.
        ModernProblem input = problem(ModernRelation::Photoelectric,
                                      ModernVariable::KineticEnergy);
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "1.80"));
        input.knowns.push_back(known(ModernVariable::WorkFunction, "2.30"));
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "unphysical value",
                "a photon below the work function is refused");
        t.check(contains_text(refused.result.detail, "no electron is emitted"),
                "and the refusal says why rather than reporting a negative energy");
    }
    {
        // 0.0304 u of mass defect is 28.3 MeV, the helium-4 binding energy to three figures.
        ModernProblem input = problem(ModernRelation::MassEnergy, ModernVariable::RestEnergy);
        input.knowns.push_back(known(ModernVariable::MassDefect, "0.0304"));
        const Run solved = run(input);
        t.equal(modern_outcome_name(solved.result.outcome), "solved",
                "a mass defect gives its binding energy");
        t.equal(solved.result.value_text, "28.3", "the binding energy reports to three figures");
        t.equal(solved.result.unit_text, "MeV", "the binding energy is reported in MeV");
    }
    {
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        input.knowns.push_back(known(ModernVariable::WorkFunction, "2.30"));
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "invalid problem",
                "a variable outside the relation is refused");
    }
    {
        ModernProblem input = problem(ModernRelation::Photoelectric,
                                      ModernVariable::KineticEnergy);
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "3.50"));
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "missing known",
                "a relation short of one known is refused");
    }
    {
        ModernProblem input = problem(ModernRelation::Photoelectric,
                                      ModernVariable::KineticEnergy);
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "3.50"));
        input.knowns.push_back(known(ModernVariable::PhotonEnergy, "4.00"));
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "duplicate known",
                "the same known given twice is refused");
    }
    {
        // A wavelength handed over in metres would be substituted unchanged, because this family
        // has no conversion step to catch it.
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        ModernKnown metres = known(ModernVariable::Wavelength, "0.00000062");
        metres.quantity.unit.text = "m";
        input.knowns.push_back(metres);
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "dimension mismatch",
                "a given in another unit of the same dimension is refused");
        t.check(contains_text(refused.result.detail, "this family reads"),
                "and the refusal names the unit the family reads");
    }
    {
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        ModernKnown wrong = known(ModernVariable::Wavelength, "620");
        wrong.quantity.unit.dimension.time = 1;
        wrong.quantity.unit.text = "nm";
        input.knowns.push_back(wrong);
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "dimension mismatch",
                "a given with the wrong dimension is refused");
    }
    {
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        input.knowns.push_back(known(ModernVariable::Wavelength, "-620"));
        const Run refused = run(input);
        t.equal(modern_outcome_name(refused.result.outcome), "unphysical value",
                "a negative wavelength is refused");
    }
    {
        Budget budget;
        budget.max_steps = 1;
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        input.knowns.push_back(known(ModernVariable::Wavelength, "620"));
        const Run stopped = run(input, budget);
        t.equal(modern_outcome_name(stopped.result.outcome), "resource exceeded",
                "a step budget of one stops the derivation");
        t.check(stopped.result.value == kNoNode, "and no answer is reported after the stop");
    }
    {
        PollAfter poll;
        poll.stop_at = 1;
        Budget budget;
        budget.poll = poll_after;
        budget.poll_context = &poll;
        ModernProblem input = problem(ModernRelation::PhotonWavelength,
                                      ModernVariable::PhotonEnergy);
        input.knowns.push_back(known(ModernVariable::Wavelength, "620"));
        const Run cancelled = run(input, budget);
        t.equal(modern_outcome_name(cancelled.result.outcome), "cancelled",
                "a cancellation poll stops the derivation");
        t.check(cancelled.result.value == kNoNode, "and no answer is reported after cancelling");
    }
    {
        t.check(modern_relation_has(ModernRelation::Photoelectric, ModernVariable::WorkFunction) &&
                    !modern_relation_has(ModernRelation::PhotonWavelength,
                                         ModernVariable::WorkFunction),
                "the relation membership predicate separates the two energy relations");
        t.equal(modern_relation_name(ModernRelation::MassEnergy), "Mass-energy equivalence",
                "each relation names itself");
    }
}

}  // namespace nps
