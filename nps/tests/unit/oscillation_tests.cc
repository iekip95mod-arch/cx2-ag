#include <cstdint>
#include <string>

#include "nps/physics/oscillation.h"
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
    size_t checks = 0;
    bool all_verified = true;
    std::string unverified;
    std::string rules;
    std::string context_family;
    std::string assumptions;
};

Run collect(Arena &arena, Derivation &derivation, const RelationResult &result) {
    Run run;
    run.result = result;
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
        if (step.kind == StepKind::Plan)
            ++run.plan;
        if (step.kind == StepKind::Check)
            ++run.checks;
    }
    static_cast<void>(arena);
    return run;
}

Run run_oscillation(const RelationProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    return collect(arena, derivation, solve_oscillation(arena, derivation, input, budget));
}

Run run_wave(const RelationProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    return collect(arena, derivation, solve_wave(arena, derivation, input, budget));
}

bool contains(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

bool exact_value(const RelationResult &result, int64_t numerator, int64_t denominator) {
    return result.quantity.value.num == numerator && result.quantity.value.den == denominator;
}

bool always_cancel(void *) { return true; }

}  // namespace

void run_oscillation_tests(TestSink &t) {
    {
        struct Case {
            OscillationVariable unknown;
            OscillationVariable first, second;
            const char *first_text, *second_text;
            int64_t numerator, denominator;
            const char *unit;
        };
        const Case cases[] = {
            {OscillationVariable::RestoringForce, OscillationVariable::Stiffness,
             OscillationVariable::Displacement, "200 N/m", "0.05 m", 10, 1, "kg m/s^2"},
            {OscillationVariable::Stiffness, OscillationVariable::RestoringForce,
             OscillationVariable::Displacement, "10 N", "0.05 m", 200, 1, "kg/s^2"},
            {OscillationVariable::Displacement, OscillationVariable::RestoringForce,
             OscillationVariable::Stiffness, "10 N", "200 N/m", 1, 20, "m"},
        };
        for (const Case &test : cases) {
            RelationProblem input = oscillation_problem(test.unknown);
            input.knowns.push_back(oscillation_known(test.first, quantity(test.first_text)));
            input.knowns.push_back(oscillation_known(test.second, quantity(test.second_text)));
            const Run solved = run_oscillation(input);
            t.equal(relation_outcome_name(solved.result.outcome), "solved",
                    "the restoring-force relation solves each of its three positions");
            t.check(exact_value(solved.result, test.numerator, test.denominator),
                    "the solved harmonic quantity is the exact value the relation gives");
            t.equal(solved.result.unit_text, test.unit,
                    "the solved harmonic quantity carries its SI base units");
            t.equal(solved.unverified, "", "every recorded harmonic claim has passing evidence");
        }
    }
    {
        RelationProblem input = oscillation_problem(OscillationVariable::RestoringForce);
        input.knowns.push_back(
            oscillation_known(OscillationVariable::Stiffness, quantity("200 N/m")));
        input.knowns.push_back(
            oscillation_known(OscillationVariable::Displacement, quantity("0.05 m")));
        const Run solved = run_oscillation(input);
        t.evidence("PHYS-011",
                   contains(solved.rules, "physics.oscillation.definition") &&
                       contains(solved.rules, "physics.oscillation.check-dimensions") &&
                       contains(solved.rules, "physics.oscillation.substitute") &&
                       contains(solved.rules, "physics.oscillation.check-candidate"),
                   "the harmonic provenance names definition, dimensions, substitution and check");
        t.evidence("PHYS-025",
                   contains(solved.assumptions, "small-angle") &&
                       contains(solved.assumptions, "undamped"),
                   "the harmonic context records the linear-restoring and undamped conditions");
        t.equal(solved.context_family, "physics.oscillation.restoring-force",
                "the solution context identifies the harmonic family");
        t.check(solved.plan >= 2 && solved.checks >= 3,
                "the harmonic derivation records plan and check steps for its own and the linear work");
    }
    {
        RelationProblem input = oscillation_problem(OscillationVariable::RestoringForce);
        input.knowns.push_back(oscillation_known(OscillationVariable::Stiffness, quantity("200 N")));
        input.knowns.push_back(
            oscillation_known(OscillationVariable::Displacement, quantity("0.05 m")));
        const Run refused = run_oscillation(input);
        t.equal(relation_outcome_name(refused.result.outcome), "dimension mismatch",
                "a force offered as a stiffness is refused before substitution");
    }
    {
        RelationProblem input = oscillation_problem(OscillationVariable::RestoringForce);
        input.knowns.push_back(
            oscillation_known(OscillationVariable::Stiffness, quantity("200 N/m")));
        const Run refused = run_oscillation(input);
        t.equal(relation_outcome_name(refused.result.outcome), "missing known",
                "a harmonic problem without the displacement is refused");
    }
    {
        struct Case {
            WaveVariable unknown;
            WaveVariable first, second;
            const char *first_text, *second_text;
            int64_t numerator, denominator;
            const char *unit;
        };
        const Case cases[] = {
            {WaveVariable::Speed, WaveVariable::Frequency, WaveVariable::Wavelength, "50 s^-1",
             "0.4 m", 20, 1, "m/s"},
            {WaveVariable::Wavelength, WaveVariable::Speed, WaveVariable::Frequency, "20 m/s",
             "50 s^-1", 2, 5, "m"},
            {WaveVariable::Frequency, WaveVariable::Speed, WaveVariable::Wavelength, "20 m/s",
             "0.4 m", 50, 1, "1/s"},
        };
        for (const Case &test : cases) {
            RelationProblem input = wave_problem(test.unknown);
            input.knowns.push_back(wave_known(test.first, quantity(test.first_text)));
            input.knowns.push_back(wave_known(test.second, quantity(test.second_text)));
            const Run solved = run_wave(input);
            t.equal(relation_outcome_name(solved.result.outcome), "solved",
                    "the wave relation solves each of its three positions");
            t.check(exact_value(solved.result, test.numerator, test.denominator),
                    "the solved wave quantity is the exact value the relation gives");
            t.equal(solved.result.unit_text, test.unit,
                    "the solved wave quantity carries its SI base units");
            t.equal(solved.unverified, "", "every recorded wave claim has passing evidence");
        }
    }
    {
        RelationProblem input = wave_problem(WaveVariable::Speed);
        input.knowns.push_back(wave_known(WaveVariable::Frequency, quantity("50 s^-1")));
        input.knowns.push_back(wave_known(WaveVariable::Wavelength, quantity("40 cm")));
        const Run solved = run_wave(input);
        t.equal(relation_outcome_name(solved.result.outcome), "solved",
                "the wave relation converts a prefixed wavelength before substituting");
        t.check(exact_value(solved.result, 20, 1),
                "the converted wavelength gives the same exact wave speed as its metre form");
        t.evidence("PHYS-011", contains(solved.rules, "physics.wave.convert-units"),
                   "the wave derivation records the exact unit conversion it applied");
        t.evidence("PHYS-025",
                   contains(solved.assumptions, "non-dispersive") &&
                       contains(solved.assumptions, "travelling"),
                   "the wave context records the medium conditions the relation rests on");
        t.equal(solved.context_family, "physics.wave.speed-frequency-wavelength",
                "the solution context identifies the wave family");
    }
    {
        RelationProblem input = wave_problem(WaveVariable::Speed);
        input.knowns.push_back(wave_known(WaveVariable::Frequency, quantity("50 m")));
        input.knowns.push_back(wave_known(WaveVariable::Wavelength, quantity("0.4 m")));
        const Run refused = run_wave(input);
        t.equal(relation_outcome_name(refused.result.outcome), "dimension mismatch",
                "a length offered as a frequency is refused before substitution");
    }
    {
        Budget budget;
        budget.poll = always_cancel;
        RelationProblem input = wave_problem(WaveVariable::Speed);
        input.knowns.push_back(wave_known(WaveVariable::Frequency, quantity("50 s^-1")));
        input.knowns.push_back(wave_known(WaveVariable::Wavelength, quantity("0.4 m")));
        const Run stopped = run_wave(input, budget);
        t.equal(relation_outcome_name(stopped.result.outcome), "cancelled",
                "a cancelled wave run reports cancellation rather than an answer");
        t.check(stopped.result.value == kNoNode,
                "a cancelled wave run returns no value to display");
    }
}

}  // namespace nps
