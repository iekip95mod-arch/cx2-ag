#include "nps/physics/gravitation.h"

namespace nps {
namespace {

Dimension force_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

Dimension mass_dimension() {
    Dimension dimension;
    dimension.mass = 1;
    return dimension;
}

Dimension length_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

Dimension constant_dimension() {
    Dimension dimension;
    dimension.length = 3;
    dimension.mass = -1;
    dimension.time = -2;
    return dimension;
}

RelationModel build_model() {
    RelationModel model;
    model.family_id = "physics.gravitation.point-masses";
    model.rule_prefix = "physics.gravitation";
    model.equation_text = "F = G*m1*m2*r^-2";
    model.strategy_text = "Apply Newton's law of gravitation F = G*m1*m2/r^2";
    model.rule_name = "Newton's law of gravitation";
    model.method_text =
        "Newtonian gravitation, exact SI substitution, linear isolation of the requested quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into F = G*m1*m2/r^2. Leave "
        "the requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] =
        "each body is a point mass, or a sphere whose mass distribution is spherically symmetric "
        "so its field outside equals that of a point mass at its centre";
    model.conditions[1] =
        "the separation is measured between the two centres and no third body contributes";
    // 6.674e-11 m^3 kg^-1 s^-2 carried exactly as a rational, so the derivation stays exact.
    model.constant.num = 6674;
    model.constant.den = 100000000000000;
    model.constant_dimension = constant_dimension();
    model.constant_symbol = "G";
    model.constant_note =
        "the tabulated gravitational constant 6.674e-11 m^3 kg^-1 s^-2 is used exactly and its "
        "measurement uncertainty is not propagated";
    model.target.symbol = "F";
    model.target.name = "gravitational force";
    model.target.dimension = force_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "m1";
    model.factors[0].name = "first mass";
    model.factors[0].dimension = mass_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "m2";
    model.factors[1].name = "second mass";
    model.factors[1].dimension = mass_dimension();
    model.factors[1].power = 1;
    model.factors[2].symbol = "r";
    model.factors[2].name = "separation";
    model.factors[2].dimension = length_dimension();
    model.factors[2].power = -2;
    model.factor_count = 3;
    return model;
}

}  // namespace

const char *gravitation_variable_name(GravitationVariable variable) {
    switch (variable) {
        case GravitationVariable::Force: return "gravitational force";
        case GravitationVariable::FirstMass: return "first mass";
        case GravitationVariable::SecondMass: return "second mass";
        case GravitationVariable::Separation: return "separation";
    }
    return "invalid variable";
}

const RelationModel &gravitation_model() {
    static const RelationModel model = build_model();
    return model;
}

RelationKnown gravitation_known(GravitationVariable variable, const Quantity &quantity) {
    RelationKnown known;
    known.index = static_cast<size_t>(variable);
    known.quantity = quantity;
    return known;
}

RelationProblem gravitation_problem(GravitationVariable unknown) {
    RelationProblem problem;
    problem.unknown = static_cast<size_t>(unknown);
    return problem;
}

RelationResult solve_gravitation(Arena &arena, Derivation &derivation,
                                 const RelationProblem &problem, const Budget &budget) {
    return solve_relation(arena, derivation, gravitation_model(), problem, budget);
}

}  // namespace nps
