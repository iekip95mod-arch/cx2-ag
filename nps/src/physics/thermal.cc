#include "nps/physics/thermal.h"

namespace nps {
namespace {

Dimension dimension(int length, int mass, int time, int temperature, int amount) {
    Dimension d;
    d.length = length;
    d.mass = mass;
    d.time = time;
    d.temperature = temperature;
    d.amount = amount;
    return d;
}

const Dimension kEnergy = dimension(2, 1, -2, 0, 0);
const Dimension kMass = dimension(0, 1, 0, 0, 0);
const Dimension kSpecificHeat = dimension(2, 0, -2, -1, 0);
const Dimension kTemperature = dimension(0, 0, 0, 1, 0);
const Dimension kSpecificEnergy = dimension(2, 0, -2, 0, 0);
const Dimension kPressure = dimension(-1, 1, -2, 0, 0);
const Dimension kAmount = dimension(0, 0, 0, 0, 1);
const Dimension kVolume = dimension(3, 0, 0, 0, 0);
const Dimension kMolarEntropy = dimension(2, 1, -2, -1, -1);

RelationTerm term(const char *symbol, const char *name, const Dimension &dimension, int power) {
    RelationTerm t;
    t.symbol = symbol;
    t.name = name;
    t.dimension = dimension;
    t.power = power;
    return t;
}

RelationModel build_sensible_heat() {
    RelationModel model;
    model.family_id = "physics.thermal.sensible-heat";
    model.rule_prefix = "physics.thermal.sensible-heat";
    model.equation_text = "Q = m*c*dT";
    model.strategy_text = "Apply the heat capacity relation Q = m*c*dT";
    model.rule_name = "Heat for a temperature change";
    model.method_text = "calorimetry with a constant specific heat, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into Q = m*c*dT. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "no phase change happens over the temperature change";
    model.conditions[1] = "the specific heat is constant over the temperature range";
    model.conditions[2] =
        "the temperature change is given in kelvin, which is the same size as a change in degrees Celsius";
    model.constant.num = 1;
    model.constant.den = 1;
    model.target = term("Q", "heat", kEnergy, 1);
    model.factors[0] = term("m", "mass", kMass, 1);
    model.factors[1] = term("c", "specific heat", kSpecificHeat, 1);
    model.factors[2] = term("dT", "temperature change", kTemperature, 1);
    model.factor_count = 3;
    return model;
}

RelationModel build_latent_heat() {
    RelationModel model;
    model.family_id = "physics.thermal.latent-heat";
    model.rule_prefix = "physics.thermal.latent-heat";
    model.equation_text = "Q = m*L";
    model.strategy_text = "Apply the latent heat relation Q = m*L";
    model.rule_name = "Heat for a phase change";
    model.method_text = "latent heat at the transition temperature, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into Q = m*L. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the substance stays at its transition temperature throughout";
    model.conditions[1] = "the whole mass changes phase and nothing else absorbs the heat";
    model.constant.num = 1;
    model.constant.den = 1;
    model.target = term("Q", "heat", kEnergy, 1);
    model.factors[0] = term("m", "mass", kMass, 1);
    model.factors[1] = term("L", "latent heat", kSpecificEnergy, 1);
    model.factor_count = 2;
    return model;
}

RelationModel build_ideal_gas() {
    RelationModel model;
    model.family_id = "physics.thermal.ideal-gas";
    model.rule_prefix = "physics.thermal.ideal-gas";
    model.equation_text = "P = n*R*T*V^-1";
    model.strategy_text = "Apply the ideal gas law P*V = n*R*T";
    model.rule_name = "Ideal gas law";
    model.method_text = "ideal gas law, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into P = n*R*T/V. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the gas is ideal, with negligible molecular volume and no forces between molecules";
    model.conditions[1] = "the temperature is absolute, in kelvin";
    model.conditions[2] = "the gas is in equilibrium, so one pressure and one temperature describe it";
    // 8.31446261815324 J mol^-1 K^-1, exact since the 2019 SI fixed the Boltzmann and Avogadro constants.
    model.constant.num = 831446261815324;
    model.constant.den = 100000000000000;
    model.constant_dimension = kMolarEntropy;
    model.constant_symbol = "R";
    model.constant_note =
        "the molar gas constant 8.31446261815324 J mol^-1 K^-1 is exact in the SI, so nothing is "
        "approximated by using it";
    model.target = term("P", "pressure", kPressure, 1);
    model.factors[0] = term("n", "amount of gas", kAmount, 1);
    model.factors[1] = term("T", "absolute temperature", kTemperature, 1);
    model.factors[2] = term("V", "volume", kVolume, -1);
    model.factor_count = 3;
    return model;
}

}  // namespace

const RelationModel &sensible_heat_model() {
    static const RelationModel model = build_sensible_heat();
    return model;
}

const RelationModel &latent_heat_model() {
    static const RelationModel model = build_latent_heat();
    return model;
}

const RelationModel &ideal_gas_model() {
    static const RelationModel model = build_ideal_gas();
    return model;
}

RelationResult solve_sensible_heat(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                   const Budget &budget) {
    return solve_relation(arena, derivation, sensible_heat_model(), problem, budget);
}

RelationResult solve_latent_heat(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                 const Budget &budget) {
    return solve_relation(arena, derivation, latent_heat_model(), problem, budget);
}

RelationResult solve_ideal_gas(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                               const Budget &budget) {
    return solve_relation(arena, derivation, ideal_gas_model(), problem, budget);
}

}  // namespace nps
