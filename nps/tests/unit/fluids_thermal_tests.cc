#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

#include "nps/physics/fluids.h"
#include "nps/physics/thermal.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"
#include "../../src/physics/measurement_support.h"

namespace nps {
namespace {

using Solver = RelationResult (*)(Arena &, Derivation &, const RelationProblem &, const Budget &);

Quantity quantity(const char *text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

struct Run {
    RelationResult result;
    std::string rules;
    std::string unverified;
    std::string family;
    std::string assumptions;
};

Run run(Solver solve, size_t unknown, std::initializer_list<std::pair<size_t, const char *>> knowns,
        const Budget &budget = Budget()) {
    RelationProblem problem;
    problem.unknown = unknown;
    for (const auto &[index, text] : knowns)
        problem.knowns.push_back(RelationKnown{index, quantity(text)});
    Arena arena;
    Derivation derivation;
    Run out;
    out.result = solve(arena, derivation, problem, budget);
    out.family = derivation.context.problem_family_id;
    for (const std::string &assumption : derivation.context.active_assumptions)
        out.assumptions += assumption + " | ";
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        out.rules += step.rule_id + " ";
        if (!step.verified())
            out.unverified += step.rule_id + " ";
    }
    return out;
}

bool contains(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

bool exact(const Run &r, int64_t numerator, int64_t denominator) {
    return r.result.quantity.value.num == numerator && r.result.quantity.value.den == denominator;
}

std::string outcome(const Run &r) {
    return relation_outcome_name(r.result.outcome);
}

// Every model shares the relation engine, so each is held to the same four-part provenance.
bool provenance(const Run &r, const std::string &prefix) {
    return contains(r.rules, (prefix + ".definition").c_str()) && contains(r.rules, (prefix + ".check-dimensions").c_str()) &&
           contains(r.rules, (prefix + ".substitute").c_str()) && contains(r.rules, (prefix + ".check-candidate").c_str()) &&
           r.unverified.empty() && derivation_status_name(r.result.status) == std::string("solved and verified");
}

bool always_cancel(void *) { return true; }

void test_fluids(TestSink &t) {
    {
        const Run r = run(solve_pressure, 0, {{1, "10 N"}, {2, "2 m^2"}});
        t.check(outcome(r) == "solved" && exact(r, 5, 1) && r.result.unit_text == "kg/(m s^2)",
                "pressure is force over area: " + r.result.value_text + " " + r.result.unit_text);
        t.check(provenance(r, "physics.fluids.pressure"), "and the pressure derivation is named and verified: " + r.unverified);
        t.equal(r.family, "physics.fluids.pressure.force-area", "the context names the pressure family");
        t.evidence("PHYS-025", contains(r.assumptions, "perpendicular") && contains(r.assumptions, "uniformly"),
                   "the pressure context records that the force is normal and spread uniformly");
    }
    {
        const Run r = run(solve_pressure, 2, {{0, "5 Pa"}, {1, "10 N"}});
        t.check(outcome(r) == "unsupported unknown" && contains(r.result.detail, "power -1"),
                "an unknown area sits at power minus one and is refused by the linear path: " + r.result.detail);
    }
    {
        const Run r = run(solve_hydrostatic, 0, {{1, "1000 kg/m^3"}, {2, "10 m/s^2"}, {3, "3 m"}});
        t.check(outcome(r) == "solved" && exact(r, 30000, 1), "gauge pressure is rho g h: " + r.result.value_text);
        t.check(provenance(r, "physics.fluids.hydrostatic"), "and the hydrostatic derivation is verified: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "gauge pressure") && contains(r.assumptions, "incompressible"),
                   "the hydrostatic context records gauge pressure and an incompressible fluid at rest");
    }
    {
        const Run r = run(solve_hydrostatic, 3, {{0, "49 kPa"}, {1, "1000 kg/m^3"}, {2, "9.8 m/s^2"}});
        t.check(outcome(r) == "solved" && exact(r, 5, 1) && r.result.value_text == "5.0",
                "a depth solves from a gauge pressure in kilopascals, reported to the measured figures: " +
                    r.result.value_text);
        t.check(contains(r.rules, "physics.fluids.hydrostatic.convert-units") &&
                    contains(r.rules, "physics.fluids.hydrostatic.significant-figures"),
                "the kilopascal is converted and the measured g sets the reported figures");
    }
    {
        const Run r = run(solve_buoyancy, 0, {{1, "1000 kg/m^3"}, {2, "2 L"}, {3, "10 m/s^2"}});
        t.check(outcome(r) == "solved" && exact(r, 20, 1) && r.result.unit_text == "kg m/s^2",
                "the buoyant force is the weight of the displaced fluid: " + r.result.value_text);
        t.check(provenance(r, "physics.fluids.buoyancy") && contains(r.rules, "physics.fluids.buoyancy.convert-units"),
                "and the litre is converted to cubic metres in a verified step: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "below the surface") && contains(r.assumptions, "at rest"),
                   "the buoyancy context records which volume is displaced and that the fluid is at rest");
    }
    {
        const Run r = run(solve_buoyancy, 2, {{0, "20 N"}, {1, "1000 kg/m^3"}, {3, "10 m/s^2"}});
        t.check(outcome(r) == "solved" && exact(r, 1, 500) && r.result.unit_text == "m^3",
                "a displaced volume solves from the buoyant force: " + r.result.value_text);
    }
    {
        const Run r = run(solve_continuity, 0, {{1, "4 cm^2"}, {2, "3 m/s"}, {3, "2 cm^2"}});
        t.check(outcome(r) == "solved" && exact(r, 6, 1), "halving the area doubles the speed: " + r.result.value_text);
        t.check(provenance(r, "physics.fluids.continuity"), "and the continuity derivation is verified: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "incompressible") && contains(r.assumptions, "no fluid enters"),
                   "the continuity context records a steady incompressible flow with nothing added between sections");
    }
    {
        const Run r = run(solve_continuity, 2, {{0, "6 m/s"}, {1, "4 cm^2"}, {3, "2 cm^2"}});
        t.check(outcome(r) == "solved" && exact(r, 3, 1), "an inlet speed solves from the outlet speed: " + r.result.value_text);
    }
    {
        const Run r = run(solve_continuity, 0, {{1, "4 cm^2"}, {2, "3 m/s"}, {3, "2 m/s"}});
        t.equal(outcome(r), "dimension mismatch", "a speed offered as an area is refused");
    }
    {
        const Run r = run(solve_buoyancy, 0, {{1, "1000 kg/m^3"}, {3, "10 m/s^2"}});
        t.check(outcome(r) == "missing known" && contains(r.result.detail, "displaced volume"),
                "a buoyancy problem without the volume names what is missing: " + r.result.detail);
    }
}

void test_thermal(TestSink &t) {
    {
        const Run r = run(solve_sensible_heat, 0, {{1, "500 g"}, {2, "4186 J/(kg*K)"}, {3, "10 K"}});
        t.check(outcome(r) == "solved" && exact(r, 20930, 1) && r.result.unit_text == "kg m^2/s^2",
                "heat is m c dT, with the grams converted: " + r.result.value_text + " " + r.result.unit_text);
        t.check(provenance(r, "physics.thermal.sensible-heat") &&
                    contains(r.rules, "physics.thermal.sensible-heat.convert-units"),
                "and the sensible heat derivation is verified: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "no phase change") && contains(r.assumptions, "kelvin"),
                   "the heat context records that no phase change happens and that dT is in kelvin");
    }
    {
        const Run r = run(solve_sensible_heat, 3, {{0, "41860 J"}, {1, "1 kg"}, {2, "4186 J/(kg*K)"}});
        t.check(outcome(r) == "solved" && exact(r, 10, 1) && r.result.unit_text == "K",
                "a temperature change solves in kelvin: " + r.result.value_text + " " + r.result.unit_text);
    }
    {
        const Run r = run(solve_sensible_heat, 0, {{1, "1 kg"}, {2, "4186 J/(kg*K)"}, {3, "10 m"}});
        t.equal(outcome(r), "dimension mismatch", "a length offered as a temperature change is refused");
    }
    {
        const Run r = run(solve_sensible_heat, 0, {{1, "1 kg"}, {2, "4186 J/(kg*K)"}, {3, "10"}});
        t.equal(outcome(r), "dimension mismatch", "a bare number offered as a temperature change is refused");
    }
    {
        const Run r = run(solve_latent_heat, 0, {{1, "2 kg"}, {2, "334000 J/kg"}});
        t.check(outcome(r) == "solved" && exact(r, 668000, 1), "melting heat is m L: " + r.result.value_text);
        t.check(provenance(r, "physics.thermal.latent-heat"), "and the latent heat derivation is verified: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "transition temperature"),
                   "the latent heat context records that the substance stays at its transition temperature");
    }
    {
        const Run r = run(solve_latent_heat, 1, {{0, "668000 J"}, {2, "334000 J/kg"}});
        t.check(outcome(r) == "solved" && exact(r, 2, 1), "a mass solves from the heat and the latent heat");
    }
    {
        const Run r = run(solve_ideal_gas, 0, {{1, "1 mol"}, {2, "300 K"}, {3, "25 L"}});
        t.check(outcome(r) == "solved" && exact(r, 623584696361493, 6250000000),
                "the ideal gas pressure uses the exact gas constant: " + r.result.value_text);
        t.check(provenance(r, "physics.thermal.ideal-gas"), "and the ideal gas derivation is verified: " + r.unverified);
        t.evidence("PHYS-025", contains(r.assumptions, "ideal") && contains(r.assumptions, "exact in the SI"),
                   "the ideal gas context records the ideal-gas condition and that R is exact");
    }
    {
        const Run r = run(solve_ideal_gas, 2, {{0, "100 kPa"}, {1, "1 mol"}, {3, "25 L"}});
        t.check(outcome(r) == "solved" && exact(r, 62500000000000000, 207861565453831) && r.result.unit_text == "K",
                "a temperature solves exactly from pressure, amount and volume: " + r.result.value_text);
    }
    {
        const Run r = run(solve_ideal_gas, 1, {{0, "100 kPa"}, {2, "300 K"}, {3, "25 L"}});
        t.check(outcome(r) == "solved" && r.result.unit_text == "mol", "an amount solves in moles: " + r.result.unit_text);
    }
    {
        const Run r = run(solve_ideal_gas, 0, {{1, "1 mol"}, {2, "300 J"}, {3, "25 L"}});
        t.equal(outcome(r), "dimension mismatch", "an energy offered as a temperature is refused");
    }
    {
        Budget cancelling;
        cancelling.poll = always_cancel;
        const Run r = run(solve_ideal_gas, 0, {{1, "1 mol"}, {2, "300 K"}, {3, "25 L"}}, cancelling);
        t.check(outcome(r) == "cancelled" && r.result.value == kNoNode, "a cancelled gas run offers no value");
    }
}

}  // namespace

void run_fluids_thermal_tests(TestSink &t) {
    {
        Arena arena;
        Dimension force;
        force.length = 1;
        force.mass = 1;
        force.time = -2;
        Dimension molar;
        molar.length = 2;
        molar.mass = 1;
        molar.time = -2;
        molar.temperature = -1;
        molar.amount = -1;
        t.equal(print(arena, measure::dimension_node(arena, force)), "dimension(1, 1, -2, 0)",
                "a dimension with no temperature or amount records the four powers it always did");
        t.equal(print(arena, measure::dimension_node(arena, molar)), "dimension(2, 1, -2, 0, -1, -1)",
                "and one with them records all six");
    }
    test_fluids(t);
    test_thermal(t);
}

}  // namespace nps
