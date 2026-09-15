#include "nps/physics/oscillation.h"

namespace nps {
namespace {

Dimension force_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

Dimension stiffness_dimension() {
    Dimension dimension;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

Dimension length_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

Dimension speed_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.time = -1;
    return dimension;
}

Dimension frequency_dimension() {
    Dimension dimension;
    dimension.time = -1;
    return dimension;
}

RelationModel build_oscillation_model() {
    RelationModel model;
    model.family_id = "physics.oscillation.restoring-force";
    model.rule_prefix = "physics.oscillation";
    model.equation_text = "F = k*x";
    model.strategy_text =
        "Apply the simple harmonic restoring-force magnitude F = k*x about the equilibrium point";
    model.rule_name = "Simple harmonic restoring force";
    model.method_text =
        "simple harmonic motion through its linear restoring force, exact SI substitution, linear "
        "isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into F = k*x. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] =
        "the restoring force is linear in the displacement from equilibrium, which for a pendulum "
        "holds only in the small-angle approximation";
    model.conditions[1] =
        "the magnitudes are related here, with the restoring force directed opposite to the "
        "displacement";
    model.conditions[2] = "the motion is undamped and no driving force acts";
    model.constant.num = 1;
    model.constant.den = 1;
    model.constant_symbol = nullptr;
    model.target.symbol = "F";
    model.target.name = "restoring force";
    model.target.dimension = force_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "k";
    model.factors[0].name = "stiffness";
    model.factors[0].dimension = stiffness_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "x";
    model.factors[1].name = "displacement";
    model.factors[1].dimension = length_dimension();
    model.factors[1].power = 1;
    model.factor_count = 2;
    return model;
}

RelationModel build_wave_model() {
    RelationModel model;
    model.family_id = "physics.wave.speed-frequency-wavelength";
    model.rule_prefix = "physics.wave";
    model.equation_text = "v = f*lambda";
    model.strategy_text = "Apply the mechanical wave relation v = f*lambda";
    model.rule_name = "Mechanical wave relation";
    model.method_text =
        "the mechanical wave relation, exact SI substitution, linear isolation of the requested "
        "quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into v = f*lambda. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] =
        "the medium is uniform and non-dispersive over the band in question, so one speed "
        "describes the wave";
    model.conditions[1] =
        "the wave is periodic and travelling rather than standing, and the frequency is the source "
        "frequency the medium carries unchanged";
    model.constant.num = 1;
    model.constant.den = 1;
    model.constant_symbol = nullptr;
    model.target.symbol = "v";
    model.target.name = "wave speed";
    model.target.dimension = speed_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "f";
    model.factors[0].name = "frequency";
    model.factors[0].dimension = frequency_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "lambda";
    model.factors[1].name = "wavelength";
    model.factors[1].dimension = length_dimension();
    model.factors[1].power = 1;
    model.factor_count = 2;
    return model;
}

}  // namespace

const char *oscillation_variable_name(OscillationVariable variable) {
    switch (variable) {
        case OscillationVariable::RestoringForce: return "restoring force";
        case OscillationVariable::Stiffness: return "stiffness";
        case OscillationVariable::Displacement: return "displacement";
    }
    return "invalid variable";
}

const char *wave_variable_name(WaveVariable variable) {
    switch (variable) {
        case WaveVariable::Speed: return "wave speed";
        case WaveVariable::Frequency: return "frequency";
        case WaveVariable::Wavelength: return "wavelength";
    }
    return "invalid variable";
}

const RelationModel &oscillation_model() {
    static const RelationModel model = build_oscillation_model();
    return model;
}

const RelationModel &wave_model() {
    static const RelationModel model = build_wave_model();
    return model;
}

RelationKnown oscillation_known(OscillationVariable variable, const Quantity &quantity) {
    RelationKnown known;
    known.index = static_cast<size_t>(variable);
    known.quantity = quantity;
    return known;
}

RelationKnown wave_known(WaveVariable variable, const Quantity &quantity) {
    RelationKnown known;
    known.index = static_cast<size_t>(variable);
    known.quantity = quantity;
    return known;
}

RelationProblem oscillation_problem(OscillationVariable unknown) {
    RelationProblem problem;
    problem.unknown = static_cast<size_t>(unknown);
    return problem;
}

RelationProblem wave_problem(WaveVariable unknown) {
    RelationProblem problem;
    problem.unknown = static_cast<size_t>(unknown);
    return problem;
}

RelationResult solve_oscillation(Arena &arena, Derivation &derivation,
                                 const RelationProblem &problem, const Budget &budget) {
    return solve_relation(arena, derivation, oscillation_model(), problem, budget);
}

RelationResult solve_wave(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                          const Budget &budget) {
    return solve_relation(arena, derivation, wave_model(), problem, budget);
}

}  // namespace nps
