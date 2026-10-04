#include "nps/physics/fluids.h"

namespace nps {
namespace {

Dimension dimension(int length, int mass, int time) {
    Dimension d;
    d.length = length;
    d.mass = mass;
    d.time = time;
    return d;
}

const Dimension kPressure = dimension(-1, 1, -2);
const Dimension kForce = dimension(1, 1, -2);
const Dimension kArea = dimension(2, 0, 0);
const Dimension kVolume = dimension(3, 0, 0);
const Dimension kLength = dimension(1, 0, 0);
const Dimension kDensity = dimension(-3, 1, 0);
const Dimension kAcceleration = dimension(1, 0, -2);
const Dimension kSpeed = dimension(1, 0, -1);

RelationTerm term(const char *symbol, const char *name, const Dimension &dimension, int power) {
    RelationTerm t;
    t.symbol = symbol;
    t.name = name;
    t.dimension = dimension;
    t.power = power;
    return t;
}

void unit_constant(RelationModel *model) {
    model->constant.num = 1;
    model->constant.den = 1;
}

RelationModel build_pressure() {
    RelationModel model;
    model.family_id = "physics.fluids.pressure.force-area";
    model.rule_prefix = "physics.fluids.pressure";
    model.equation_text = "P = F*A^-1";
    model.strategy_text = "Apply the definition of pressure P = F/A";
    model.rule_name = "Pressure as force per area";
    model.method_text = "definition of pressure, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into P = F/A. Leave the requested "
        "quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the force acts perpendicular to the surface";
    model.conditions[1] = "the force is spread uniformly over the area, so one pressure describes it";
    unit_constant(&model);
    model.target = term("P", "pressure", kPressure, 1);
    model.factors[0] = term("F", "force", kForce, 1);
    model.factors[1] = term("A", "area", kArea, -1);
    model.factor_count = 2;
    return model;
}

RelationModel build_hydrostatic() {
    RelationModel model;
    model.family_id = "physics.fluids.hydrostatic-pressure";
    model.rule_prefix = "physics.fluids.hydrostatic";
    model.equation_text = "P = rho*g*h";
    model.strategy_text = "Apply the hydrostatic gauge pressure P = rho*g*h";
    model.rule_name = "Hydrostatic gauge pressure";
    model.method_text = "hydrostatic equilibrium, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into P = rho*g*h. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the fluid is at rest and incompressible, with one uniform density";
    model.conditions[1] = "the gravitational acceleration is uniform over the depth";
    model.conditions[2] =
        "the pressure is gauge pressure, measured relative to the pressure at the free surface";
    unit_constant(&model);
    model.target = term("P", "gauge pressure", kPressure, 1);
    model.factors[0] = term("rho", "fluid density", kDensity, 1);
    model.factors[1] = term("g", "gravitational acceleration", kAcceleration, 1);
    model.factors[2] = term("h", "depth", kLength, 1);
    model.factor_count = 3;
    return model;
}

RelationModel build_buoyancy() {
    RelationModel model;
    model.family_id = "physics.fluids.buoyancy.archimedes";
    model.rule_prefix = "physics.fluids.buoyancy";
    model.equation_text = "F = rho*V*g";
    model.strategy_text = "Apply Archimedes' principle F = rho*V*g";
    model.rule_name = "Archimedes' principle";
    model.method_text = "Archimedes' principle, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into F = rho*V*g. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the fluid is at rest with one uniform density";
    model.conditions[1] = "the displaced volume is the part of the body below the surface";
    model.conditions[2] = "the gravitational acceleration is uniform over the body";
    unit_constant(&model);
    model.target = term("F", "buoyant force", kForce, 1);
    model.factors[0] = term("rho", "fluid density", kDensity, 1);
    model.factors[1] = term("V", "displaced volume", kVolume, 1);
    model.factors[2] = term("g", "gravitational acceleration", kAcceleration, 1);
    model.factor_count = 3;
    return model;
}

RelationModel build_continuity() {
    RelationModel model;
    model.family_id = "physics.fluids.continuity.incompressible";
    model.rule_prefix = "physics.fluids.continuity";
    model.equation_text = "v2 = A1*v1*A2^-1";
    model.strategy_text = "Apply the continuity equation A1*v1 = A2*v2";
    model.rule_name = "Continuity of an incompressible flow";
    model.method_text = "conservation of volume flow rate, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into v2 = A1*v1/A2. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the flow is steady and the fluid is incompressible";
    model.conditions[1] = "the speed is uniform across each cross section";
    model.conditions[2] = "no fluid enters or leaves between the two cross sections";
    unit_constant(&model);
    model.target = term("v2", "outlet speed", kSpeed, 1);
    model.factors[0] = term("A1", "inlet area", kArea, 1);
    model.factors[1] = term("v1", "inlet speed", kSpeed, 1);
    model.factors[2] = term("A2", "outlet area", kArea, -1);
    model.factor_count = 3;
    return model;
}

}  // namespace

const RelationModel &pressure_model() {
    static const RelationModel model = build_pressure();
    return model;
}

const RelationModel &hydrostatic_model() {
    static const RelationModel model = build_hydrostatic();
    return model;
}

const RelationModel &buoyancy_model() {
    static const RelationModel model = build_buoyancy();
    return model;
}

const RelationModel &continuity_model() {
    static const RelationModel model = build_continuity();
    return model;
}

RelationResult solve_pressure(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                              const Budget &budget) {
    return solve_relation(arena, derivation, pressure_model(), problem, budget);
}

RelationResult solve_hydrostatic(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                 const Budget &budget) {
    return solve_relation(arena, derivation, hydrostatic_model(), problem, budget);
}

RelationResult solve_buoyancy(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                              const Budget &budget) {
    return solve_relation(arena, derivation, buoyancy_model(), problem, budget);
}

RelationResult solve_continuity(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                const Budget &budget) {
    return solve_relation(arena, derivation, continuity_model(), problem, budget);
}

}  // namespace nps
