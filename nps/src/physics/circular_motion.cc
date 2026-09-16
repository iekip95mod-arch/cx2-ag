#include "nps/physics/circular_motion.h"

#include <string>
#include <utility>

#include "measurement_support.h"

namespace nps {
namespace {

Dimension length_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

Dimension time_dimension() {
    Dimension dimension;
    dimension.time = 1;
    return dimension;
}

Dimension speed_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.time = -1;
    return dimension;
}

Dimension acceleration_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.time = -2;
    return dimension;
}

// pi to 15 significant digits, carried exactly as a rational the same way gravitation.cc carries
// its constant: the approximation error is not propagated, only the exact rational is.
Rational two_pi_constant() {
    Rational value;
    value.num = 628318530717959;
    value.den = 100000000000000;
    return value;
}

Rational reciprocal_two_pi_constant() {
    Rational value = two_pi_constant();
    std::swap(value.num, value.den);
    return value;
}

RelationModel build_acceleration_model() {
    RelationModel model;
    model.family_id = "physics.circular-motion.uniform";
    model.rule_prefix = "physics.circular-motion.acceleration";
    model.equation_text = "a = v^2*r^-1";
    model.strategy_text = "Apply the centripetal acceleration a = v^2/r";
    model.rule_name = "Centripetal acceleration";
    model.method_text =
        "uniform circular motion, exact SI substitution, linear isolation of the requested "
        "quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into a = v^2/r. Leave the "
        "requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the speed is constant, so the motion traces a circle at constant rate";
    model.conditions[1] =
        "the reported acceleration is the magnitude directed toward the centre of the circle";
    model.constant.num = 1;
    model.constant.den = 1;
    model.constant_symbol = nullptr;
    model.target.symbol = "a";
    model.target.name = "centripetal acceleration";
    model.target.dimension = acceleration_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "v";
    model.factors[0].name = "speed";
    model.factors[0].dimension = speed_dimension();
    model.factors[0].power = 2;
    model.factors[1].symbol = "r";
    model.factors[1].name = "radius";
    model.factors[1].dimension = length_dimension();
    model.factors[1].power = -1;
    model.factor_count = 2;
    return model;
}

// Solve for the period given speed and radius: T = 2*pi*r*v^-1.
RelationModel build_period_model() {
    RelationModel model;
    model.family_id = "physics.circular-motion.uniform";
    model.rule_prefix = "physics.circular-motion.period";
    model.equation_text = "T = 2*pi*r*v^-1";
    model.strategy_text = "Apply the uniform circular motion period T = 2*pi*r/v";
    model.rule_name = "Circular motion period";
    model.method_text =
        "uniform circular motion, exact SI substitution, linear isolation of the requested "
        "quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into T = 2*pi*r/v. Leave "
        "the requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the speed is constant, so one period describes every lap";
    model.constant = two_pi_constant();
    model.constant_symbol = nullptr;
    model.constant_note =
        "2*pi is carried as the exact rational 628318530717959/100000000000000 and its "
        "approximation error is not propagated";
    model.target.symbol = "T";
    model.target.name = "period";
    model.target.dimension = time_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "r";
    model.factors[0].name = "radius";
    model.factors[0].dimension = length_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "v";
    model.factors[1].name = "speed";
    model.factors[1].dimension = speed_dimension();
    model.factors[1].power = -1;
    model.factor_count = 2;
    return model;
}

// Solve for the speed given radius and period: v = 2*pi*r*T^-1.
RelationModel build_speed_model() {
    RelationModel model;
    model.family_id = "physics.circular-motion.uniform";
    model.rule_prefix = "physics.circular-motion.speed";
    model.equation_text = "v = 2*pi*r*T^-1";
    model.strategy_text = "Apply the uniform circular motion period, solved for speed";
    model.rule_name = "Circular motion speed";
    model.method_text =
        "uniform circular motion, exact SI substitution, linear isolation of the requested "
        "quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into v = 2*pi*r/T. Leave "
        "the requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the speed is constant, so one period describes every lap";
    model.constant = two_pi_constant();
    model.constant_symbol = nullptr;
    model.constant_note =
        "2*pi is carried as the exact rational 628318530717959/100000000000000 and its "
        "approximation error is not propagated";
    model.target.symbol = "v";
    model.target.name = "speed";
    model.target.dimension = speed_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "r";
    model.factors[0].name = "radius";
    model.factors[0].dimension = length_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "T";
    model.factors[1].name = "period";
    model.factors[1].dimension = time_dimension();
    model.factors[1].power = -1;
    model.factor_count = 2;
    return model;
}

// Solve for the radius given speed and period: r = (1/(2*pi))*v*T.
RelationModel build_radius_model() {
    RelationModel model;
    model.family_id = "physics.circular-motion.uniform";
    model.rule_prefix = "physics.circular-motion.radius";
    model.equation_text = "r = (1/(2*pi))*v*T";
    model.strategy_text = "Apply the uniform circular motion period, solved for radius";
    model.rule_name = "Circular motion radius";
    model.method_text =
        "uniform circular motion, exact SI substitution, linear isolation of the requested "
        "quantity";
    model.substitution_detail =
        "After converting the known quantities to SI, put their values into r = v*T/(2*pi). Leave "
        "the requested quantity as a symbol, then solve the resulting equation for it.";
    model.conditions[0] = "the speed is constant, so one period describes every lap";
    model.constant = reciprocal_two_pi_constant();
    model.constant_symbol = nullptr;
    model.constant_note =
        "1/(2*pi) is carried as the exact rational 100000000000000/628318530717959 and its "
        "approximation error is not propagated";
    model.target.symbol = "r";
    model.target.name = "radius";
    model.target.dimension = length_dimension();
    model.target.power = 1;
    model.factors[0].symbol = "v";
    model.factors[0].name = "speed";
    model.factors[0].dimension = speed_dimension();
    model.factors[0].power = 1;
    model.factors[1].symbol = "T";
    model.factors[1].name = "period";
    model.factors[1].dimension = time_dimension();
    model.factors[1].power = 1;
    model.factor_count = 2;
    return model;
}

const RelationModel &acceleration_model() {
    static const RelationModel model = build_acceleration_model();
    return model;
}

const RelationModel &period_model() {
    static const RelationModel model = build_period_model();
    return model;
}

const RelationModel &speed_model() {
    static const RelationModel model = build_speed_model();
    return model;
}

const RelationModel &radius_model() {
    static const RelationModel model = build_radius_model();
    return model;
}

CircularMotionResult failed(RelationOutcome outcome, DerivationStatus status,
                            const std::string &detail) {
    CircularMotionResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

const CircularMotionKnown *find_known(const CircularMotionProblem &problem,
                                      CircularMotionVariable variable) {
    for (size_t index = 0; index < problem.known_count; ++index) {
        if (problem.knowns[index].variable == variable)
            return &problem.knowns[index];
    }
    return nullptr;
}

// A zero or negative SI value for a quantity that must be strictly positive.
bool non_positive(const Quantity &quantity) {
    Rational si_value;
    if (!to_si(quantity, &si_value))
        return false;
    return si_value.num <= 0;
}

bool zero_valued(const Quantity &quantity) {
    Rational si_value;
    if (!to_si(quantity, &si_value))
        return false;
    return si_value.num == 0;
}

}  // namespace

const char *circular_motion_variable_name(CircularMotionVariable variable) {
    switch (variable) {
        case CircularMotionVariable::Speed: return "speed";
        case CircularMotionVariable::Radius: return "radius";
        case CircularMotionVariable::Period: return "period";
    }
    return "invalid variable";
}

CircularMotionResult solve_circular_motion(Arena &arena, Derivation &derivation,
                                           const CircularMotionProblem &problem,
                                           const Budget &budget) {
    if (problem.unknown != CircularMotionVariable::Speed &&
        problem.unknown != CircularMotionVariable::Radius &&
        problem.unknown != CircularMotionVariable::Period) {
        return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the requested unknown is not speed, radius or period");
    }
    if (problem.known_count != 2) {
        return failed(RelationOutcome::MissingKnown, DerivationStatus::InvalidInput,
                      "uniform circular motion needs exactly two of speed, radius and period");
    }
    for (size_t index = 0; index < problem.known_count; ++index) {
        if (problem.knowns[index].variable == problem.unknown) {
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(circular_motion_variable_name(problem.unknown)) +
                              " is both known and unknown");
        }
    }
    if (problem.knowns[0].variable == problem.knowns[1].variable) {
        return failed(RelationOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                      std::string(circular_motion_variable_name(problem.knowns[0].variable)) +
                          " is given twice");
    }

    const CircularMotionKnown *radius_known = find_known(problem, CircularMotionVariable::Radius);
    if (radius_known != nullptr) {
        std::string detail;
        if (!measure::valid_quantity(radius_known->quantity, &detail))
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "radius: " + detail);
        if (radius_known->quantity.unit.dimension != length_dimension()) {
            return failed(RelationOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          "radius has dimension " +
                              dimension_text(radius_known->quantity.unit.dimension) +
                              ", but requires " + dimension_text(length_dimension()));
        }
        if (non_positive(radius_known->quantity)) {
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "radius must be positive, not zero or negative");
        }
    }
    const CircularMotionKnown *speed_known = find_known(problem, CircularMotionVariable::Speed);
    if (problem.unknown == CircularMotionVariable::Period && speed_known != nullptr) {
        std::string detail;
        if (!measure::valid_quantity(speed_known->quantity, &detail))
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "speed: " + detail);
        if (zero_valued(speed_known->quantity)) {
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "speed must be nonzero to find the period, because the body never "
                          "completes a lap");
        }
    }

    CircularMotionResult result;
    const CircularMotionKnown *period_known = find_known(problem, CircularMotionVariable::Period);

    if (problem.unknown == CircularMotionVariable::Period) {
        RelationProblem sub;
        sub.unknown = kRelationTarget;
        sub.knowns.push_back({1, radius_known->quantity});
        sub.knowns.push_back({2, speed_known->quantity});
        result.unknown_result = solve_relation(arena, derivation, period_model(), sub, budget);
    } else if (problem.unknown == CircularMotionVariable::Speed) {
        RelationProblem sub;
        sub.unknown = kRelationTarget;
        sub.knowns.push_back({1, radius_known->quantity});
        sub.knowns.push_back({2, period_known->quantity});
        result.unknown_result = solve_relation(arena, derivation, speed_model(), sub, budget);
    } else {
        RelationProblem sub;
        sub.unknown = kRelationTarget;
        sub.knowns.push_back({1, speed_known->quantity});
        sub.knowns.push_back({2, period_known->quantity});
        result.unknown_result = solve_relation(arena, derivation, radius_model(), sub, budget);
    }

    if (result.unknown_result.outcome != RelationOutcome::Solved) {
        result.outcome = result.unknown_result.outcome;
        result.status = result.unknown_result.status;
        result.detail = result.unknown_result.detail;
        return result;
    }

    const Quantity speed_quantity =
        problem.unknown == CircularMotionVariable::Speed ? result.unknown_result.quantity
                                                          : speed_known->quantity;
    const Quantity radius_quantity =
        problem.unknown == CircularMotionVariable::Radius ? result.unknown_result.quantity
                                                           : radius_known->quantity;

    RelationProblem accel_problem;
    accel_problem.unknown = kRelationTarget;
    accel_problem.knowns.push_back({1, speed_quantity});
    accel_problem.knowns.push_back({2, radius_quantity});
    result.acceleration_result =
        solve_relation(arena, derivation, acceleration_model(), accel_problem, budget);

    if (result.acceleration_result.outcome != RelationOutcome::Solved) {
        result.outcome = result.acceleration_result.outcome;
        result.status = result.acceleration_result.status;
        result.detail = result.acceleration_result.detail;
        return result;
    }

    result.outcome = RelationOutcome::Solved;
    result.status = result.acceleration_result.status;
    return result;
}

}  // namespace nps
