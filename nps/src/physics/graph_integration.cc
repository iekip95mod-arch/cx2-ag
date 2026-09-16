#include "nps/physics/graph_integration.h"

#include <algorithm>

#include "nps/core/evaluate.h"
#include "nps/core/parser.h"
#include "nps/steps/integrate.h"

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

// sub_fraction canonicalises through GMP, so the sign of the normalised numerator is enough: no
// separate cross-multiplication is needed to compare two rationals with possibly different signs
// of denominator.
bool less_than(const Rational &a, const Rational &b) {
    Rational difference;
    return rational_sub(a, b, &difference) && difference.num < 0;
}

Dimension expected_curve_dimension(GraphIntegrationReading reading) {
    return reading == GraphIntegrationReading::AccelerationToVelocity ? dim(1, -2) : dim(1, -1);
}

Dimension result_dimension(GraphIntegrationReading reading) {
    return reading == GraphIntegrationReading::AccelerationToVelocity ? dim(1, -1) : dim(1, 0);
}

}  // namespace

const char *graph_integration_reading_name(GraphIntegrationReading reading) {
    switch (reading) {
        case GraphIntegrationReading::AccelerationToVelocity: return "acceleration to velocity";
        case GraphIntegrationReading::VelocityToPosition: return "velocity to position";
    }
    return "unknown";
}

const char *graph_integration_outcome_name(GraphIntegrationOutcome outcome) {
    switch (outcome) {
        case GraphIntegrationOutcome::Solved: return "solved";
        case GraphIntegrationOutcome::InvalidInput: return "invalid input";
        case GraphIntegrationOutcome::UnsupportedForm: return "unsupported form";
        case GraphIntegrationOutcome::DimensionMismatch: return "dimension mismatch";
        case GraphIntegrationOutcome::IncompleteCoverage: return "incomplete coverage";
        case GraphIntegrationOutcome::Cancelled: return "cancelled";
        case GraphIntegrationOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

GraphIntegrationResult solve_graph_integration(Arena &arena, Derivation &derivation,
                                                const GraphIntegrationProblem &problem,
                                                const Budget &budget, Backend *giac) {
    GraphIntegrationResult result;
    result.reading = problem.reading;
    Meter meter(budget);

    Rational interval_start;
    Rational interval_end;
    if (!time_seconds(problem.interval_start, &interval_start) ||
        !time_seconds(problem.interval_end, &interval_end)) {
        result.outcome = GraphIntegrationOutcome::DimensionMismatch;
        result.detail = "the interval bounds must be time quantities";
        return result;
    }
    if (rational_equal(interval_start, interval_end)) {
        result.outcome = GraphIntegrationOutcome::InvalidInput;
        result.detail = "the interval has zero duration";
        return result;
    }
    if (less_than(interval_end, interval_start))
        std::swap(interval_start, interval_end);

    if (problem.curve_dimension != expected_curve_dimension(problem.reading)) {
        result.outcome = GraphIntegrationOutcome::DimensionMismatch;
        result.detail = std::string("a ") + graph_integration_reading_name(problem.reading) +
                         " reading needs a curve in " +
                         si_unit_text(expected_curve_dimension(problem.reading));
        return result;
    }
    if (problem.segments.empty()) {
        result.outcome = GraphIntegrationOutcome::IncompleteCoverage;
        result.detail = "no segments were given to cover the interval";
        return result;
    }

    ParseResult time_variable = parse(arena, "t");
    if (!time_variable.ok()) {
        result.outcome = GraphIntegrationOutcome::InvalidInput;
        result.detail = "could not form the time variable";
        return result;
    }

    Rational covered_to = interval_start;
    Rational total;
    for (const GraphIntegrationSegment &segment : problem.segments) {
        Rational segment_start;
        Rational segment_end;
        if (!time_seconds(segment.start, &segment_start) ||
            !time_seconds(segment.end, &segment_end)) {
            result.outcome = GraphIntegrationOutcome::DimensionMismatch;
            result.detail = "a segment's own bounds must be time quantities";
            return result;
        }
        if (!less_than(segment_start, segment_end)) {
            result.outcome = GraphIntegrationOutcome::InvalidInput;
            result.detail = "a segment's bounds must be given in increasing order";
            return result;
        }
        if (!less_than(covered_to, segment_end)) continue;  // entirely before what remains
        if (less_than(covered_to, segment_start)) {
            result.outcome = GraphIntegrationOutcome::IncompleteCoverage;
            result.detail = "the segments leave a gap in the requested interval";
            return result;
        }

        // segment_start <= covered_to here, so the overlap always begins at covered_to.
        const Rational overlap_start = covered_to;
        const Rational overlap_end = less_than(interval_end, segment_end) ? interval_end : segment_end;
        if (!less_than(overlap_start, overlap_end)) continue;  // touches but does not overlap

        ParseResult curve = parse(arena, segment.curve_expression);
        if (!curve.ok()) {
            result.outcome = GraphIntegrationOutcome::InvalidInput;
            result.detail = "could not parse a segment's curve: " + curve.message;
            return result;
        }

        const IntegrateResult primitive = integrate_particular(
            arena, derivation, curve.root, time_variable.root, meter, overlap_start, giac);
        if (primitive.outcome != IntegrateOutcome::Integrated) {
            result.outcome = primitive.outcome == IntegrateOutcome::Cancelled
                                 ? GraphIntegrationOutcome::Cancelled
                             : primitive.outcome == IntegrateOutcome::ResourceExceeded
                                 ? GraphIntegrationOutcome::ResourceExceeded
                                 : GraphIntegrationOutcome::UnsupportedForm;
            result.detail = "segment: " + primitive.detail;
            result.status = primitive.status;
            result.cost = meter.cost();
            return result;
        }

        Rational upper_value;
        Rational lower_value;
        if (!evaluate_rational(arena, primitive.particular, {{"t", overlap_end}}, &upper_value) ||
            !evaluate_rational(arena, primitive.particular, {{"t", overlap_start}}, &lower_value)) {
            result.outcome = GraphIntegrationOutcome::UnsupportedForm;
            result.detail = "segment: the antiderivative could not be evaluated at its own bounds";
            result.status = primitive.status;
            result.cost = meter.cost();
            return result;
        }

        Rational segment_area;
        Rational running_total;
        if (!rational_sub(upper_value, lower_value, &segment_area) ||
            !rational_add(total, segment_area, &running_total)) {
            result.outcome = GraphIntegrationOutcome::UnsupportedForm;
            result.detail = "segment: the signed area could not be formed as an exact rational";
            result.status = primitive.status;
            result.cost = meter.cost();
            return result;
        }
        total = running_total;
        covered_to = overlap_end;
        result.status = primitive.status;
    }

    if (less_than(covered_to, interval_end)) {
        result.outcome = GraphIntegrationOutcome::IncompleteCoverage;
        result.detail = "the segments do not reach the end of the requested interval";
        result.cost = meter.cost();
        return result;
    }

    result.outcome = GraphIntegrationOutcome::Solved;
    result.change = made_quantity(total, result_dimension(problem.reading));
    result.cost = meter.cost();
    return result;
}

}  // namespace nps
