#include "nps/physics/position_motion.h"

#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/parser.h"
#include "nps/steps/differentiate.h"

namespace nps {
namespace {

Dimension dim(int length, int time) {
    Dimension d;
    d.length = length;
    d.time = time;
    return d;
}

bool time_seconds(const Quantity &q, Rational *out) {
    if (q.unit.dimension != dim(0, 1))
        return false;
    return to_si(q, out);
}

Vector made_vector(const Rational values[3], uint8_t rank, Dimension dimension) {
    Vector v;
    v.x = values[0];
    v.y = values[1];
    v.z = rank == 3 ? values[2] : Rational{0, 1};
    v.rank = rank;
    v.frame.name = default_frame_name();
    v.unit.dimension = dimension;
    v.unit.text = si_unit_text(dimension);
    v.unit.scale = Rational{1, 1};
    v.precision.kind = NumberKind::Exact;
    return v;
}

// Written on the one funnel every outcome leaves by, because each nested engine writes a context of
// its own and the last writer would otherwise leave this family's derivation naming one of theirs.
void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, bool converted_directions) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.motion.position-vector";
    inputs.requested_method =
        "differentiate each component of the supplied position vector twice in t, take the secant "
        "over the declared interval for the average velocity and evaluate both derivatives at the "
        "event time";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions.push_back(
        "each component of the position vector is a function of t alone");
    inputs.active_assumptions.push_back("the axes are independent and share one clock");
    if (converted_directions)
        inputs.angle_convention =
            "each reported vector's direction comes from the component converter, in the angle "
            "measure the problem declared";
    inputs.unit_policy =
        "interval bounds and the event time are converted to seconds exactly, and each reported "
        "vector carries the SI unit of its own dimension";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
    derivation.context.problem_family_envelope_version = "1";
}

// One axis's slice through the whole computation: parse, secant, differentiate twice, evaluate
// twice. Shared by every active component so the family does not carry three near-identical copies
// of the same steps.
struct AxisResult {
    PositionMotionOutcome outcome = PositionMotionOutcome::Solved;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    NodeId position = kNoNode;
    NodeId velocity = kNoNode;
    NodeId acceleration = kNoNode;
    Rational average_velocity;
    Rational instantaneous_velocity;
    Rational instantaneous_acceleration;
};

AxisResult solve_axis(Arena &arena, Derivation &derivation, const std::string &expression,
                      const char *axis_name, const Rational &start_seconds,
                      const Rational &end_seconds, const Rational &event_seconds,
                      NodeId time_variable, Meter &meter, Backend *giac) {
    AxisResult axis;

    ParseResult position = parse(arena, expression);
    if (!position.ok()) {
        axis.outcome = PositionMotionOutcome::InvalidInput;
        axis.detail =
            std::string("could not parse the ") + axis_name + " position function: " + position.message;
        return axis;
    }
    axis.position = position.root;

    Rational start_position;
    Rational end_position;
    if (!evaluate_rational(arena, position.root, {{"t", start_seconds}}, &start_position) ||
        !evaluate_rational(arena, position.root, {{"t", end_seconds}}, &end_position)) {
        axis.outcome = PositionMotionOutcome::UnsupportedForm;
        axis.detail = std::string("the ") + axis_name +
                      " position function could not be evaluated at the interval bounds";
        return axis;
    }

    Rational displacement;
    Rational elapsed;
    if (!rational_sub(end_position, start_position, &displacement) ||
        !rational_sub(end_seconds, start_seconds, &elapsed) ||
        !rational_div(displacement, elapsed, &axis.average_velocity)) {
        axis.outcome = PositionMotionOutcome::UnsupportedForm;
        axis.detail =
            std::string("the ") + axis_name + " average velocity could not be formed as an exact rational";
        return axis;
    }

    DiffResult velocity = differentiate(arena, derivation, position.root, time_variable, meter, giac);
    if (velocity.outcome != DiffOutcome::Differentiated) {
        axis.outcome = velocity.outcome == DiffOutcome::Cancelled
                           ? PositionMotionOutcome::Cancelled
                       : velocity.outcome == DiffOutcome::ResourceExceeded
                           ? PositionMotionOutcome::ResourceExceeded
                           : PositionMotionOutcome::UnsupportedForm;
        axis.detail = std::string(axis_name) + " velocity: " + velocity.detail;
        axis.status = velocity.status;
        return axis;
    }
    axis.velocity = velocity.derivative;

    if (!evaluate_rational(arena, velocity.derivative, {{"t", event_seconds}},
                           &axis.instantaneous_velocity)) {
        axis.outcome = PositionMotionOutcome::UnsupportedForm;
        axis.detail =
            std::string("the ") + axis_name + " velocity expression could not be evaluated at the event";
        return axis;
    }

    DiffResult acceleration =
        differentiate(arena, derivation, velocity.derivative, time_variable, meter, giac);
    if (acceleration.outcome != DiffOutcome::Differentiated) {
        axis.outcome = acceleration.outcome == DiffOutcome::Cancelled
                           ? PositionMotionOutcome::Cancelled
                       : acceleration.outcome == DiffOutcome::ResourceExceeded
                           ? PositionMotionOutcome::ResourceExceeded
                           : PositionMotionOutcome::UnsupportedForm;
        axis.detail = std::string(axis_name) + " acceleration: " + acceleration.detail;
        axis.status = acceleration.status;
        return axis;
    }
    axis.acceleration = acceleration.derivative;

    if (!evaluate_rational(arena, acceleration.derivative, {{"t", event_seconds}},
                           &axis.instantaneous_acceleration)) {
        axis.outcome = PositionMotionOutcome::UnsupportedForm;
        axis.detail = std::string("the ") + axis_name +
                      " acceleration expression could not be evaluated at the event";
        return axis;
    }

    axis.status = acceleration.status;
    return axis;
}

}  // namespace

const char *position_motion_outcome_name(PositionMotionOutcome outcome) {
    switch (outcome) {
        case PositionMotionOutcome::Solved: return "solved";
        case PositionMotionOutcome::InvalidInput: return "invalid input";
        case PositionMotionOutcome::UnsupportedForm: return "unsupported form";
        case PositionMotionOutcome::DimensionMismatch: return "dimension mismatch";
        case PositionMotionOutcome::Cancelled: return "cancelled";
        case PositionMotionOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

namespace {

PositionMotionResult solve_body(Arena &arena, Derivation &derivation,
                                const PositionMotionProblem &problem, const Budget &budget,
                                Backend *giac) {
    PositionMotionResult result;
    Meter meter(budget);

    if (problem.rank != 2 && problem.rank != 3) {
        result.outcome = PositionMotionOutcome::InvalidInput;
        result.detail = "the family supports rank two or rank three motion only";
        return result;
    }
    result.rank = problem.rank;

    ParseResult time_variable = parse(arena, "t");
    if (!time_variable.ok()) {
        result.outcome = PositionMotionOutcome::InvalidInput;
        result.detail = "could not form the time variable";
        return result;
    }

    Rational start_seconds;
    Rational end_seconds;
    Rational event_seconds;
    if (!time_seconds(problem.interval_start, &start_seconds) ||
        !time_seconds(problem.interval_end, &end_seconds) ||
        !time_seconds(problem.event_time, &event_seconds)) {
        result.outcome = PositionMotionOutcome::DimensionMismatch;
        result.detail = "the interval bounds and the event time must be time quantities";
        return result;
    }
    if (rational_equal(start_seconds, end_seconds)) {
        result.outcome = PositionMotionOutcome::InvalidInput;
        result.detail = "the interval has zero duration";
        return result;
    }

    static const char *kAxisNames[3] = {"x", "y", "z"};
    const std::string expressions[3] = {problem.position_x, problem.position_y, problem.position_z};
    const uint8_t active = problem.rank;

    Rational average_velocity[3] = {};
    Rational instantaneous_velocity[3] = {};
    Rational instantaneous_acceleration[3] = {};
    NodeId position_nodes[3] = {kNoNode, kNoNode, kNoNode};
    NodeId velocity_nodes[3] = {kNoNode, kNoNode, kNoNode};
    NodeId acceleration_nodes[3] = {kNoNode, kNoNode, kNoNode};
    DerivationStatus last_status = DerivationStatus::NotRecorded;

    for (uint8_t axis_index = 0; axis_index < active; ++axis_index) {
        AxisResult axis = solve_axis(arena, derivation, expressions[axis_index], kAxisNames[axis_index],
                                     start_seconds, end_seconds, event_seconds, time_variable.root,
                                     meter, giac);
        if (axis.outcome != PositionMotionOutcome::Solved) {
            result.outcome = axis.outcome;
            result.detail = axis.detail;
            result.status = axis.status;
            result.cost = meter.cost();
            return result;
        }
        average_velocity[axis_index] = axis.average_velocity;
        instantaneous_velocity[axis_index] = axis.instantaneous_velocity;
        instantaneous_acceleration[axis_index] = axis.instantaneous_acceleration;
        position_nodes[axis_index] = axis.position;
        velocity_nodes[axis_index] = axis.velocity;
        acceleration_nodes[axis_index] = axis.acceleration;
        last_status = axis.status;
    }

    const Vector vectors[3] = {made_vector(average_velocity, problem.rank, dim(1, -1)),
                               made_vector(instantaneous_velocity, problem.rank, dim(1, -1)),
                               made_vector(instantaneous_acceleration, problem.rank, dim(1, -2))};
    static const char *kVectorNames[3] = {"average velocity", "instantaneous velocity",
                                          "instantaneous acceleration"};
    MagnitudeAngleExpr polars[3];
    bool has_polar[3] = {false, false, false};

    if (giac != nullptr) {
        for (int index = 0; index < 3; ++index) {
            const VectorComponentsResult polar =
                components_to_magnitude_angle(arena, derivation, vectors[index], problem.angle_unit,
                                              *giac, remaining_budget(budget, meter));
            // Nothing may reach Giac after a terminal status, so a halted conversion ends the solve
            // rather than leaving the next two to ask anyway.
            const bool afforded = charge(meter, polar.cost);
            if (!afforded || polar.outcome == VectorComponentsOutcome::Cancelled ||
                polar.outcome == VectorComponentsOutcome::ResourceExceeded) {
                PositionMotionResult halted;
                halted.rank = problem.rank;
                halted.outcome = polar.outcome == VectorComponentsOutcome::Cancelled ||
                                         meter.halt() == Halt::Cancelled
                                     ? PositionMotionOutcome::Cancelled
                                     : PositionMotionOutcome::ResourceExceeded;
                halted.detail =
                    std::string(kVectorNames[index]) + " direction: " + polar.detail;
                halted.status = polar.status;
                halted.cost = meter.cost();
                return halted;
            }
            if (polar.outcome == VectorComponentsOutcome::Solved && polar.has_polar) {
                polars[index] = polar.polar;
                has_polar[index] = true;
            }
        }
    }

    result.position_x = position_nodes[0];
    result.position_y = position_nodes[1];
    result.position_z = position_nodes[2];
    result.velocity_x = velocity_nodes[0];
    result.velocity_y = velocity_nodes[1];
    result.velocity_z = velocity_nodes[2];
    result.acceleration_x = acceleration_nodes[0];
    result.acceleration_y = acceleration_nodes[1];
    result.acceleration_z = acceleration_nodes[2];

    result.average_velocity = vectors[0];
    result.instantaneous_velocity = vectors[1];
    result.instantaneous_acceleration = vectors[2];
    result.average_velocity_polar = polars[0];
    result.instantaneous_velocity_polar = polars[1];
    result.instantaneous_acceleration_polar = polars[2];
    result.has_average_velocity_polar = has_polar[0];
    result.has_instantaneous_velocity_polar = has_polar[1];
    result.has_instantaneous_acceleration_polar = has_polar[2];

    result.outcome = PositionMotionOutcome::Solved;
    result.status = last_status;
    result.cost = meter.cost();
    return result;
}

}  // namespace

PositionMotionResult solve_position_motion(Arena &arena, Derivation &derivation,
                                           const PositionMotionProblem &problem,
                                           const Budget &budget, Backend *giac) {
    const PositionMotionResult result = solve_body(arena, derivation, problem, budget, giac);
    // The angle convention is stated only when a direction was actually reported, because a
    // refusal that never reached the converter has no direction to state one for.
    const bool converted = result.has_average_velocity_polar ||
                           result.has_instantaneous_velocity_polar ||
                           result.has_instantaneous_acceleration_polar;
    record_context(derivation, budget, kNoNode, result.status, converted);
    return result;
}

}  // namespace nps
