#include "nps/physics/position_motion.h"

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

Quantity made_quantity(const Rational &value, Dimension dimension) {
    Quantity q;
    q.value = value;
    q.unit.dimension = dimension;
    q.unit.text = si_unit_text(dimension);
    q.unit.scale = Rational{1, 1};
    q.precision.kind = NumberKind::Exact;
    return q;
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

PositionMotionResult solve_position_motion(Arena &arena, Derivation &derivation,
                                            const PositionMotionProblem &problem,
                                            const Budget &budget, Backend *giac) {
    PositionMotionResult result;
    Meter meter(budget);

    ParseResult position = parse(arena, problem.position_expression);
    if (!position.ok()) {
        result.outcome = PositionMotionOutcome::InvalidInput;
        result.detail = "could not parse the position function: " + position.message;
        return result;
    }
    result.position_expression = position.root;

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

    Rational start_position;
    Rational end_position;
    if (!evaluate_rational(arena, position.root, {{"t", start_seconds}}, &start_position) ||
        !evaluate_rational(arena, position.root, {{"t", end_seconds}}, &end_position)) {
        result.outcome = PositionMotionOutcome::UnsupportedForm;
        result.detail = "the position function could not be evaluated at the interval bounds";
        return result;
    }

    Rational displacement;
    Rational elapsed;
    Rational average_velocity;
    if (!rational_sub(end_position, start_position, &displacement) ||
        !rational_sub(end_seconds, start_seconds, &elapsed) ||
        !rational_div(displacement, elapsed, &average_velocity)) {
        result.outcome = PositionMotionOutcome::UnsupportedForm;
        result.detail = "the average velocity could not be formed as an exact rational";
        return result;
    }
    result.average_velocity = made_quantity(average_velocity, dim(1, -1));

    DiffResult velocity =
        differentiate(arena, derivation, position.root, time_variable.root, meter, giac);
    if (velocity.outcome != DiffOutcome::Differentiated) {
        result.outcome = velocity.outcome == DiffOutcome::Cancelled
                             ? PositionMotionOutcome::Cancelled
                         : velocity.outcome == DiffOutcome::ResourceExceeded
                             ? PositionMotionOutcome::ResourceExceeded
                             : PositionMotionOutcome::UnsupportedForm;
        result.detail = "velocity: " + velocity.detail;
        result.status = velocity.status;
        result.cost = meter.cost();
        return result;
    }
    result.velocity_expression = velocity.derivative;

    Rational instantaneous_velocity;
    if (!evaluate_rational(arena, velocity.derivative, {{"t", event_seconds}},
                           &instantaneous_velocity)) {
        result.outcome = PositionMotionOutcome::UnsupportedForm;
        result.detail = "the velocity expression could not be evaluated at the event";
        result.cost = meter.cost();
        return result;
    }
    result.instantaneous_velocity = made_quantity(instantaneous_velocity, dim(1, -1));

    DiffResult acceleration =
        differentiate(arena, derivation, velocity.derivative, time_variable.root, meter, giac);
    if (acceleration.outcome != DiffOutcome::Differentiated) {
        result.outcome = acceleration.outcome == DiffOutcome::Cancelled
                             ? PositionMotionOutcome::Cancelled
                         : acceleration.outcome == DiffOutcome::ResourceExceeded
                             ? PositionMotionOutcome::ResourceExceeded
                             : PositionMotionOutcome::UnsupportedForm;
        result.detail = "acceleration: " + acceleration.detail;
        result.status = acceleration.status;
        result.cost = meter.cost();
        return result;
    }
    result.acceleration_expression = acceleration.derivative;

    Rational instantaneous_acceleration;
    if (!evaluate_rational(arena, acceleration.derivative, {{"t", event_seconds}},
                           &instantaneous_acceleration)) {
        result.outcome = PositionMotionOutcome::UnsupportedForm;
        result.detail = "the acceleration expression could not be evaluated at the event";
        result.cost = meter.cost();
        return result;
    }
    result.instantaneous_acceleration = made_quantity(instantaneous_acceleration, dim(1, -2));

    result.outcome = PositionMotionOutcome::Solved;
    result.status = acceleration.status;
    result.cost = meter.cost();
    return result;
}

}  // namespace nps
