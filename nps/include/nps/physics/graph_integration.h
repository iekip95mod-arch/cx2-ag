#ifndef NPS_PHYSICS_GRAPH_INTEGRATION_H
#define NPS_PHYSICS_GRAPH_INTEGRATION_H

#include <string>
#include <vector>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// Issue 377: chapter 2 spends six passages on reading area off a piecewise a(t) or v(t) curve.
// The area under acceleration is the change in velocity, and the area under velocity is the
// change in position, both signed: area below the axis subtracts. Each piece is an algebraic
// expression of t over its own sub-interval, in the SI unit its reading expects, and the family
// composes with the existing definite-integral engine (integrate_particular) for each piece's
// closed form rather than carrying a second integrator. It owns only stitching the pieces across
// the requested interval, checking that they cover it, and keeping the sign.
enum class GraphIntegrationReading : uint8_t {
    // a(t) curve, in m/s^2. The area is the change in velocity.
    AccelerationToVelocity,
    // v(t) curve, in m/s. The area is the change in position.
    VelocityToPosition,
};

const char *graph_integration_reading_name(GraphIntegrationReading reading);

struct GraphIntegrationSegment {
    // Its own sub-interval, which need not equal the query interval: only the part of it that
    // overlaps [interval_start, interval_end] is used.
    Quantity start;
    Quantity end;
    // Function of t over [start, end], read in SI seconds in and the reading's SI unit out.
    std::string curve_expression;
};

struct GraphIntegrationProblem {
    GraphIntegrationReading reading = GraphIntegrationReading::AccelerationToVelocity;
    // What physical quantity the segment expressions' values are in. Acceleration for
    // AccelerationToVelocity, velocity for VelocityToPosition. Stated by the caller rather than
    // inferred, because a bare algebraic expression carries no dimension of its own.
    Dimension curve_dimension;
    Quantity interval_start;
    Quantity interval_end;
    std::vector<GraphIntegrationSegment> segments;
};

enum class GraphIntegrationOutcome : uint8_t {
    Solved,
    InvalidInput,
    UnsupportedForm,
    DimensionMismatch,
    IncompleteCoverage,
    Cancelled,
    ResourceExceeded,
};

const char *graph_integration_outcome_name(GraphIntegrationOutcome outcome);

struct GraphIntegrationResult {
    GraphIntegrationOutcome outcome = GraphIntegrationOutcome::InvalidInput;
    // The signed area: a change in velocity (AccelerationToVelocity) or a change in position
    // (VelocityToPosition), matching problem.reading.
    Quantity change;
    GraphIntegrationReading reading = GraphIntegrationReading::AccelerationToVelocity;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

GraphIntegrationResult solve_graph_integration(Arena &arena, Derivation &derivation,
                                                const GraphIntegrationProblem &problem,
                                                const Budget &budget = Budget(),
                                                Backend *giac = nullptr);

}  // namespace nps

#endif
