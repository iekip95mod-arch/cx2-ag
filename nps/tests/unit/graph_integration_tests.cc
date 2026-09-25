#include <string>

#include "nps/physics/graph_integration.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity parsed_quantity(const char *text) {
    Quantity quantity;
    std::string error;
    parse_quantity(text, &quantity, &error);
    return quantity;
}

Dimension acceleration_dim() {
    Dimension d;
    d.length = 1;
    d.time = -2;
    return d;
}

Dimension velocity_dim() {
    Dimension d;
    d.length = 1;
    d.time = -1;
    return d;
}

GraphIntegrationSegment segment(const char *start, const char *end, const char *curve) {
    GraphIntegrationSegment s;
    s.start = parsed_quantity(start);
    s.end = parsed_quantity(end);
    s.curve_expression = curve;
    return s;
}

struct Run {
    explicit Run(const GraphIntegrationProblem &problem, const Budget &budget = Budget())
        : result(solve_graph_integration(arena, derivation, problem, budget)) {}

    Arena arena;
    Derivation derivation;
    GraphIntegrationResult result;
};

bool cancel_now(void *) { return true; }

const char kFamily[] = "physics.kinematics.motion-graphs.piecewise-area";

std::string family(const Run &run) { return run.derivation.context.problem_family_id; }

GraphIntegrationProblem acceleration_problem(const char *start, const char *end) {
    GraphIntegrationProblem p;
    p.reading = GraphIntegrationReading::AccelerationToVelocity;
    p.curve_dimension = acceleration_dim();
    p.interval_start = parsed_quantity(start);
    p.interval_end = parsed_quantity(end);
    return p;
}

}  // namespace

// Issue 377: chapter 2's six passages on reading area off an a(t) or v(t) curve. Every case
// composes with the existing definite-integral engine (integrate_particular) one piece at a time
// and only the stitching across pieces, the coverage check and the sign belong to this family.
void run_graph_integration_tests(TestSink &t) {
    {
        // A single constant-acceleration segment: the area of a rectangle is base times height,
        // and a constant curve is the trap this family has to get right, since a symmetric or
        // zero-mean curve would pass even with the sign dropped. This one is asymmetric: 4 m/s^2
        // held for 5 s changes velocity by +20 m/s, not by its magnitude alone.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run solved(p);
        t.check(solved.result.outcome == GraphIntegrationOutcome::Solved,
               "a single constant-acceleration segment solves");
        t.equal(rational_text(solved.result.change.value), "20",
               "the area of a constant 4 m/s^2 held for 5 s is 20 m/s");
        t.equal(family(solved), kFamily,
               "a solved area records its own family rather than the nested integral's");
        t.equal(solved.result.change.unit.text, "m/s",
               "an acceleration curve's area is reported in m/s");
    }
    {
        // The sign trap named above, made concrete: a segment below the axis subtracts rather
        // than adding its magnitude. -3 m/s^2 for 2 s changes velocity by -6 m/s, not +6.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("2 s");
        p.segments.push_back(segment("0 s", "2 s", "-3"));
        Run solved(p);
        t.check(solved.result.outcome == GraphIntegrationOutcome::Solved,
               "a segment below the axis still solves");
        t.equal(rational_text(solved.result.change.value), "-6",
               "area below the axis subtracts rather than reporting a magnitude");
    }
    {
        // Two segments whose signed areas cancel: +6 m/s^2 for 2 s then -4 m/s^2 for 3 s, which
        // is +12 then -12, netting zero. A test that only checked this case would pass even with
        // the sign handling removed, which is exactly the trap named in the issue, so it is paired
        // above with the asymmetric single-segment cases rather than standing alone.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "2 s", "6"));
        p.segments.push_back(segment("2 s", "5 s", "-4"));
        Run solved(p);
        t.check(solved.result.outcome == GraphIntegrationOutcome::Solved,
               "two segments whose signed areas cancel still solve");
        t.equal(rational_text(solved.result.change.value), "0",
               "opposite signed areas net to zero rather than adding as magnitudes");
    }
    {
        // A velocity curve reads as a change in position instead, exercising the other half of
        // the family's two readings on a non-constant, closed-form segment.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::VelocityToPosition;
        p.curve_dimension = velocity_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("4 s");
        p.segments.push_back(segment("0 s", "4 s", "2*t"));
        Run solved(p);
        t.check(solved.result.outcome == GraphIntegrationOutcome::Solved,
               "a velocity curve solves for a change in position");
        t.equal(rational_text(solved.result.change.value), "16",
               "the area under v(t) = 2t from 0 to 4 s is 16 m");
        t.equal(solved.result.change.unit.text, "m",
               "a velocity curve's area is reported in m");
    }
    {
        // Equal interval endpoints leave no area to read, refused rather than reporting zero.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("3 s");
        p.interval_end = parsed_quantity("3 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "invalid input",
               "a zero-duration interval is refused rather than dividing by zero");
        t.equal(family(refused), kFamily,
               "a zero-duration refusal records the family");
        t.check(refused.result.detail.find("duration") != std::string::npos,
               "the refusal names the zero-duration interval");
    }
    {
        // The curve is declared in the wrong dimension for the requested reading: a velocity
        // curve cannot answer an acceleration-to-velocity reading.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = velocity_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "dimension mismatch",
               "a curve declared in the wrong dimension for the reading is refused");
        t.equal(family(refused), kFamily,
               "a curve dimension refusal records the family");
    }
    {
        // The segments leave a gap in the middle of the requested interval.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "2 s", "4"));
        p.segments.push_back(segment("3 s", "5 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "incomplete coverage",
               "a gap between segments is refused rather than skipped over");
        t.equal(family(refused), kFamily,
               "a gap refusal after a nested integral records the family");
    }
    {
        // The segments stop short of the interval's own end.
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "3 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "incomplete coverage",
               "segments that stop short of the interval's end are refused");
        t.equal(family(refused), kFamily,
               "a short coverage refusal records the family");
    }
    {
        // A meter already cancelled on entry halts the first integrate_particular() call.
        Budget budget;
        budget.poll = cancel_now;
        GraphIntegrationProblem p;
        p.reading = GraphIntegrationReading::AccelerationToVelocity;
        p.curve_dimension = acceleration_dim();
        p.interval_start = parsed_quantity("0 s");
        p.interval_end = parsed_quantity("5 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run cancelled(p, budget);
        t.equal(graph_integration_outcome_name(cancelled.result.outcome), "cancelled",
               "a cancelled meter reports cancellation instead of an answer");
        t.equal(family(cancelled), kFamily,
               "a cancelled solve records the family");
    }
    {
        // Control. A fresh derivation names no family, and the nested integral engine names its
        // own, so the rows above can only pass when this family stamps itself last.
        Derivation fresh;
        t.check(fresh.context.problem_family_id != kFamily,
               "a derivation nobody solved does not already name the family");
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run solved(p);
        t.check(solved.derivation.size() > 0,
               "the nested integral engine recorded steps into the same derivation");
        t.check(family(solved) != "calculus.integral.indefinite.single-variable",
               "the nested integral engine's family does not survive as the answer's family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 m", "5 s");
        p.segments.push_back(segment("0 s", "5 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "dimension mismatch",
               "an interval bound that is not a time is refused");
        t.equal(family(refused), kFamily, "an interval bound refusal records the family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "incomplete coverage",
               "no segments at all is refused");
        t.equal(family(refused), kFamily, "an empty segment list refusal records the family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        p.segments.push_back(segment("0 m", "5 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "dimension mismatch",
               "a segment bound that is not a time is refused");
        t.equal(family(refused), kFamily, "a segment bound refusal records the family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        p.segments.push_back(segment("5 s", "0 s", "4"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "invalid input",
               "a segment written backwards is refused");
        t.equal(family(refused), kFamily, "a backwards segment refusal records the family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        p.segments.push_back(segment("0 s", "5 s", "4 +"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "invalid input",
               "an unparseable curve is refused");
        t.equal(family(refused), kFamily, "an unparseable curve refusal records the family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("0 s", "5 s");
        p.segments.push_back(segment("0 s", "5 s", "t^t"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "unsupported form",
               "a curve the integral engine cannot integrate is refused");
        t.equal(family(refused), kFamily,
               "a refusal inside the nested integral engine still records this family");
    }
    {
        GraphIntegrationProblem p = acceleration_problem("1 s", "2 s");
        p.segments.push_back(segment("1 s", "2 s", "1/t"));
        Run refused(p);
        t.equal(graph_integration_outcome_name(refused.result.outcome), "unsupported form",
               "an antiderivative with no exact rational value at its bounds is refused");
        t.equal(family(refused), kFamily, "an inexact bound value refusal records the family");
    }
}

}  // namespace nps
