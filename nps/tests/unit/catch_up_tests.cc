#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "nps/core/print.h"
#include "nps/physics/catch_up.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity quantity(const std::string &text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

CatchUpBody body(const char *name, const std::string &position,
                 const std::string &velocity, const std::string &start) {
    CatchUpBody value;
    value.name = name;
    value.frame.name = "track";
    value.position_at_start = quantity(position);
    value.velocity_at_start = quantity(velocity);
    value.start_time = quantity(start);
    return value;
}

CatchUpProblem problem(const CatchUpBody &first, const CatchUpBody &second) {
    CatchUpProblem value;
    value.first = first;
    value.second = second;
    return value;
}

struct Run {
    CatchUpResult result;
    size_t steps = 0;
    size_t plans = 0;
    size_t transformations = 0;
    size_t checks = 0;
    bool all_verified = true;
    std::string rules;
    std::string context_family;
    std::string context_branch;
    std::string context_assumptions;
    size_t assumptions = 0;
    std::string report_evidence;
    std::string verifications;
};

Run run(const CatchUpProblem &input, const Budget &budget = Budget(),
        const Limits &limits = Limits()) {
    Arena arena(limits);
    Derivation derivation;
    Run observed;
    observed.result = solve_catch_up(arena, derivation, input, budget);
    observed.steps = derivation.size();
    observed.verifications = verification_transcript(derivation);
    observed.context_family = derivation.context.problem_family_id;
    observed.context_branch = derivation.context.branch_convention;
    observed.assumptions = derivation.context.active_assumptions.size();
    for (const std::string &assumption : derivation.context.active_assumptions) {
        if (!observed.context_assumptions.empty())
            observed.context_assumptions += '\n';
        observed.context_assumptions += assumption;
    }
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (!observed.rules.empty())
            observed.rules += ' ';
        observed.rules += step.rule_id;
        observed.all_verified = observed.all_verified && step.verified();
        if (step.rule_id == "physics.catch-up.significant-figures") {
            // The outcome ahead of the sentence, in verification_transcript's format. Without it a
            // passed check and a failed one carrying the same detail read identically here.
            for (const VerificationRecord &check : step.verifications) {
                if (!observed.report_evidence.empty())
                    observed.report_evidence += '\n';
                observed.report_evidence += verification_outcome_name(check.outcome);
                observed.report_evidence += ", ";
                observed.report_evidence += check.detail;
            }
        }
        switch (step.kind) {
            case StepKind::Plan: ++observed.plans; break;
            case StepKind::Transformation: ++observed.transformations; break;
            case StepKind::Check: ++observed.checks; break;
            case StepKind::Branch: break;
        }
    }
    return observed;
}

CatchUpProblem reference_problem() {
    return problem(body("Atlas", "0 m", "2 m/s", "0 s"),
                   body("Boreal", "0 m", "4 m/s", "5 s"));
}

bool contains(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

bool exact(const Quantity &quantity, int64_t numerator, int64_t denominator) {
    return quantity.value.num == numerator && quantity.value.den == denominator;
}

void preorder(const Derivation &derivation, StepId id, std::vector<StepId> *order) {
    order->push_back(id);
    for (StepId child : derivation.at(id).children)
        preorder(derivation, child, order);
}

size_t rule_position(const Derivation &derivation, const std::vector<StepId> &order,
                     const char *rule) {
    for (size_t row = 0; row < order.size(); ++row)
        if (derivation.at(order[row]).rule_id == rule)
            return row;
    return order.size();
}

void test_event_teaching_order(TestSink &t) {
    for (const char *velocity : {"4 m/s", "-4 m/s", "4.00 m/s"}) {
        const bool negative = velocity[0] == '-';
        Arena arena;
        Derivation derivation;
        const CatchUpResult solved = solve_catch_up(
            arena, derivation,
            problem(body("Atlas", "0 m", negative ? "-2 m/s" : "2 m/s", "0 s"),
                    body("Boreal", "0 m", velocity, "5 s")));
        t.check(solved.outcome == CatchUpOutcome::Solved &&
                    exact(solved.event_time, 10, 1) &&
                    exact(solved.event_position, negative ? -20 : 20, 1),
                "teaching order preserves positive, negative and measured event values");
        std::vector<StepId> order;
        for (StepId root : derivation.roots())
            preorder(derivation, root, &order);
        const size_t algebra = rule_position(derivation, order, "eq.linear.inverse-operations");
        t.check(order.size() == derivation.size() && algebra < order.size() &&
                    derivation.at(order[algebra]).parent == derivation.roots().front(),
                "event algebra belongs to the displayed physics plan");
        const size_t produced = rule_position(derivation, order, "eq.divide-both-sides");
        const size_t checked = rule_position(derivation, order, "eq.linear.check-by-substitution");
        t.check(produced < checked && checked < order.size(),
                "the candidate time is derived and algebraically checked");
        for (const char *consumer : {"physics.catch-up.shared-domain",
                                     "physics.catch-up.verify-first-position",
                                     "physics.catch-up.verify-second-position"}) {
            const size_t consumed = rule_position(derivation, order, consumer);
            t.check(consumed < order.size() && checked < consumed,
                    std::string("derive and check event time before ") + consumer);
        }
        const size_t report = rule_position(derivation, order, "physics.catch-up.significant-figures");
        if (report < order.size())
            t.check(checked < report, "measured reporting follows the exact event derivation");
        t.check(derivation.all_verified_from(0), "nesting preserves all catch-up verification");
        const TransformationPayload *isolation =
            produced < order.size() ? derivation.transformation(order[produced]) : nullptr;
        t.check(isolation && print(arena, isolation->after) == "(t_event = 10)",
                "the written candidate remains the exact meeting time");
    }

    Arena arena;
    Derivation derivation;
    const CatchUpResult rejected = solve_catch_up(
        arena, derivation, problem(body("First", "0 m", "1 m/s", "0 s"),
                                   body("Second", "10 m", "2 m/s", "5 s")));
    std::vector<StepId> order;
    for (StepId root : derivation.roots())
        preorder(derivation, root, &order);
    t.check(rejected.outcome == CatchUpOutcome::BeforeSharedDomain &&
                rejected.event_time_text.empty() && rejected.event_position_text.empty(),
            "ordering does not promote a rejected event to an answer");
    t.check(rule_position(derivation, order, "eq.linear.check-by-substitution") <
                rule_position(derivation, order, "physics.catch-up.shared-domain"),
            "a rejected candidate is derived before its active-domain rejection");

    for (size_t max_steps : {8u, 9u}) {
        Arena limited_arena;
        Derivation prefix;
        Budget budget;
        budget.max_steps = max_steps;
        const CatchUpResult stopped =
            solve_catch_up(limited_arena, prefix, reference_problem(), budget);
        std::vector<StepId> prefix_order;
        for (StepId root : prefix.roots())
            preorder(prefix, root, &prefix_order);
        t.check(stopped.outcome == CatchUpOutcome::ResourceExceeded &&
                    stopped.status == DerivationStatus::ResourceLimitReached &&
                    stopped.event_time_text.empty(),
                "a late resource halt withholds the final answer");
        t.check(prefix.size() == max_steps && prefix_order.size() == prefix.size() &&
                    prefix.all_verified_from(0),
                "a late resource halt retains every completed verified record");
        t.check(prefix.roots().size() == 1 &&
                    rule_position(prefix, prefix_order, "eq.linear.check-by-substitution") <
                        rule_position(prefix, prefix_order, "physics.catch-up.shared-domain"),
                "a retained prefix keeps the candidate derivation before its dependent check");
    }
}

bool always_cancel(void *) { return true; }

struct PollAfter {
    int calls = 0;
    int stop_at = 0;
};

bool cancel_after(void *context) {
    PollAfter *poll = static_cast<PollAfter *>(context);
    ++poll->calls;
    return poll->calls >= poll->stop_at;
}

}  // namespace

void run_catch_up_tests(TestSink &t) {
    test_event_teaching_order(t);
    {
        const Run solved = run(reference_problem());
        t.evidence("PHYS-028", catch_up_outcome_name(solved.result.outcome), "solved",
                   "two named bodies and their active intervals survive the catch-up solve");
        t.equal(solved.result.event_time_text, "10", "the delayed-start meeting time is exact");
        t.equal(solved.result.time_unit_text, "s", "the event time is reported in SI seconds");
        t.equal(solved.result.event_position_text, "20",
                "the equal-position event reports its position");
        t.equal(solved.result.position_unit_text, "m",
                "the event position is reported in SI metres");
        t.check(exact(solved.result.event_time, 10, 1) &&
                    exact(solved.result.event_position, 20, 1),
                "event time and position remain exact typed quantities");
        t.check(exact(solved.result.shared_active_start, 5, 1),
                "the later start defines the shared active boundary");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "the reference catch-up solve is fully verified");
        t.check(solved.result.time != kNoNode && solved.result.first_position != kNoNode &&
                    solved.result.second_position != kNoNode &&
                    solved.result.equation != kNoNode &&
                    solved.result.active_domain != kNoNode &&
                    solved.result.substituted != kNoNode &&
                    solved.result.first_at_event != kNoNode &&
                    solved.result.second_at_event != kNoNode,
                "the typed result retains both original laws, equality, domain and substitutions");
    }

    {
        const Run solved = run(problem(body("Lead", "0 m", "36 km/h", "0 min"),
                                       body("Chaser", "0 cm", "72 km/h", "5 s")));
        t.equal(catch_up_outcome_name(solved.result.outcome), "solved",
                "compatible mixed units solve through exact SI conversion");
        t.check(exact(solved.result.event_time, 10, 1) &&
                    exact(solved.result.event_position, 100, 1),
                "mixed unit conversion preserves the exact event");
    }

    for (int delay = 1; delay <= 5; ++delay) {
        const Run solved = run(problem(
            body("First", "0 m", "1 m/s", "0 s"),
            body("Second", "0 m", "2 m/s", std::to_string(delay) + " s")));
        t.check(solved.result.outcome == CatchUpOutcome::Solved &&
                    exact(solved.result.event_time, 2 * delay, 1) &&
                    exact(solved.result.event_position, 2 * delay, 1),
                "delayed-start parameter variants preserve the equal-position invariant");
    }

    {
        const Run boundary = run(problem(body("First", "0 m", "1 m/s", "0 s"),
                                         body("Second", "5 m", "2 m/s", "5 s")));
        t.equal(catch_up_outcome_name(boundary.result.outcome), "solved",
                "a meeting exactly at the later start is admissible");
        t.check(exact(boundary.result.event_time, 5, 1) &&
                    exact(boundary.result.event_position, 5, 1),
                "the shared-domain boundary is inclusive");
    }

    {
        const Run missed = run(problem(body("First", "0 m", "1 m/s", "0 s"),
                                       body("Second", "1 m", "1 m/s", "0 s")));
        t.equal(catch_up_outcome_name(missed.result.outcome), "no meeting",
                "parallel distinct position laws have no meeting");
        t.check(missed.result.status == DerivationStatus::SolvedAndVerified &&
                    missed.all_verified,
                "a no-meeting result completes every applicable plan precondition");
        t.check(catch_up_has_result(missed.result.outcome),
                "a verified empty solution set is a terminal mathematical result");
        t.check(missed.result.event_time_text.empty() &&
                    missed.result.event_position_text.empty(),
                "a no-meeting classification offers no fabricated event");
    }
    {
        const Run coincident = run(problem(body("First", "0 m", "1 m/s", "0 s"),
                                           body("Second", "5 m", "1 m/s", "5 s")));
        t.equal(catch_up_outcome_name(coincident.result.outcome),
                "meeting at every active time",
                "coincident active-interval laws remain underdetermined");
        t.check(coincident.result.status == DerivationStatus::SolvedAndVerified &&
                    coincident.all_verified,
                "an all-times result restricts its verified plan to the shared active domain");
        t.check(catch_up_has_result(coincident.result.outcome),
                "a verified all-times solution set is a terminal mathematical result");
        t.check(exact(coincident.result.shared_active_start, 5, 1),
                "the all-times outcome still identifies its shared interval");
        t.check(coincident.result.event_time_text.empty(),
                "an all-times meeting is not forced to one numeric event");
    }
    {
        const Run before = run(problem(body("First", "0 m", "1 m/s", "0 s"),
                                       body("Second", "10 m", "2 m/s", "5 s")));
        t.evidence("PHYS-029", catch_up_outcome_name(before.result.outcome),
                   "meeting before shared active time",
                   "an algebraic root outside the shared physical domain is not offered");
        t.check(contains(before.result.detail, "before the shared active interval at 5 s"),
                "the domain refusal names the later start boundary");
        t.check(!catch_up_has_result(before.result.outcome),
                "a rejected out-of-domain candidate is not a terminal result");
        t.check(before.result.event_time_text.empty() &&
                    before.result.event_position_text.empty(),
                "a pre-start root has no reported event");
        // The rejecting half of STEP-007, and the only place the engine has one. Setting two
        // position laws equal is an operation over the whole real line, so it can hand back a time
        // that solves the algebra and lies outside the interval both bodies are moving in. t = 0
        // here is that root: it satisfies the equation and is not an answer to the question.
        t.evidence("STEP-007", !catch_up_has_result(before.result.outcome) &&
                                   before.result.event_time_text.empty(),
                   "a candidate the algebra produced but the original problem does not admit is "
                   "caught by the check and withheld, not reported");
    }

    {
        CatchUpProblem input = reference_problem();
        input.first.motion = CatchUpMotionModel::ConstantAcceleration;
        input.first.acceleration = quantity("1 m/s^2");
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome),
                "nonlinear motion unsupported",
                "a quadratic position law is refused instead of sent to the linear solver");
        t.check(contains(refused.result.detail, "nonlinear in time"),
                "the unsupported motion explains why the selected solver does not apply");
        t.check(!contains(refused.context_assumptions,
                          "first body acceleration is zero"),
                "a refused nonzero acceleration is not recorded as a zero assumption");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.motion = CatchUpMotionModel::ConstantAcceleration;
        input.first.acceleration = quantity("0 m/s^2");
        input.first.acceleration.value.den = 2;
        const Run solved = run(input);
        t.equal(catch_up_outcome_name(solved.result.outcome), "solved",
                "an exact zero acceleration reduces to the constant-velocity law");
        t.check(contains(solved.context_assumptions,
                         "first body acceleration is zero on its active interval"),
                "a normalized exact zero acceleration is recorded as an active assumption");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.motion = CatchUpMotionModel::ConstantAcceleration;
        input.first.acceleration = quantity("0.0 m/s^2");
        const Run solved = run(input);
        t.equal(catch_up_outcome_name(solved.result.outcome), "solved",
                "a measured zero acceleration still reduces to the constant-velocity law");
        t.check(contains(solved.context_assumptions,
                         "first body acceleration is zero on its active interval"),
                "the measured zero acceleration is recorded without calling it exact");
        t.check(!contains(solved.context_assumptions, "exact zero"),
                "the measured zero acceleration is not upgraded to exact input");
        t.check(solved.result.event_time.precision.kind == NumberKind::Measured &&
                    solved.result.event_time.precision.significant_digits == 1,
                "the event time retains the measured acceleration precision");
        t.check(solved.result.event_position.precision.kind == NumberKind::Measured &&
                    solved.result.event_position.precision.significant_digits == 1,
                "the event position retains the measured acceleration precision");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.motion = static_cast<CatchUpMotionModel>(99);
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid typed motion model is rejected");
    }

    {
        CatchUpProblem input = reference_problem();
        input.second.name = input.first.name;
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "duplicate body",
                "two records cannot silently identify the same body");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.name.clear();
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "invalid problem",
                "an unnamed body is rejected");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.frame.name.clear();
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "frame undeclared",
                "an undeclared coordinate frame is rejected");
    }
    {
        CatchUpProblem input = reference_problem();
        input.second.frame.name = "other track";
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "frame mismatch",
                "positions from different coordinate frames are not equated");
    }
    {
        Limits limits;
        limits.max_input_bytes = 5;
        const Run stopped = run(reference_problem(), Budget(), limits);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "resource exceeded",
                "body identity is bounded by the existing input byte limit");
        t.check(stopped.steps == 0, "an identity limit leaves no partial derivation");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.position_at_start.value.den = 0;
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid exact position is rejected before arithmetic");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.velocity_at_start.unit.scale.den = 0;
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid velocity scale is rejected before conversion");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.start_time.precision.kind = NumberKind::Measured;
        input.first.start_time.precision.significant_digits = 0;
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "invalid problem",
                "inconsistent precision metadata is rejected");
    }

    {
        CatchUpProblem input = reference_problem();
        input.second.velocity_at_start = quantity("4 s");
        Arena arena;
        Derivation derivation;
        const CatchUpResult refused = solve_catch_up(arena, derivation, input);
        t.evidence("PHYS-002", catch_up_outcome_name(refused.outcome),
                   "dimension mismatch",
                   "a time supplied as velocity is rejected before model construction");
        t.check(contains(refused.detail, "velocity at start has dimension T") &&
                    contains(refused.detail, "requires L T^-1"),
                "the dimensional mismatch names both observed and required dimensions");
        t.check(arena.node_count() == 0 && derivation.size() == 0,
                "dimension validation precedes AST and derivation construction");
    }
    {
        CatchUpProblem input = reference_problem();
        input.second.start_time = quantity("5 m");
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "dimension mismatch",
                "a length supplied as a start time is rejected");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.motion = CatchUpMotionModel::ConstantAcceleration;
        input.first.acceleration = quantity("1 m/s");
        const Run refused = run(input);
        t.equal(catch_up_outcome_name(refused.result.outcome), "dimension mismatch",
                "a nonlinear model still validates its acceleration dimension");
    }

    {
        CatchUpProblem input = reference_problem();
        input.first.position_at_start = quantity("9223372036854775807 km");
        const Run overflow = run(input);
        t.equal(catch_up_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "SI conversion overflow has a typed outcome");
        t.check(contains(overflow.result.detail, "converting Atlas position"),
                "conversion overflow names the body and field");
    }
    {
        CatchUpProblem input = reference_problem();
        input.first.start_time.value.num = std::numeric_limits<int64_t>::max();
        input.second.start_time.value.num = std::numeric_limits<int64_t>::min();
        const Run overflow = run(input);
        t.equal(catch_up_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "an unrepresentable boundary comparison is refused");
    }
    {
        CatchUpProblem input = problem(
            body("First", "9223372036854775807 m", "1 m/s", "0 s"),
            body("Second", "9223372036854775806 m", "2 m/s", "0 s"));
        const Run overflow = run(input);
        t.equal(catch_up_outcome_name(overflow.result.outcome), "resource exceeded",
                "positions this large exhaust exact arithmetic while the linear answer is being "
                "substituted back, which withholds the candidate before the event position is "
                "reached");
        t.check(overflow.result.event_time_text.empty(),
                "an unverifiable large event is not reported");
    }

    {
        const Run solved = run(problem(body("Atlas", "0 m", "2.00 m/s", "0 s"),
                                       body("Boreal", "0 m", "4.00 m/s", "5.00 s")));
        t.evidence("PHYS-015", solved.result.event_time_text, "10.0",
                   "event time is rounded only for its final measured report");
        t.equal(solved.result.event_position_text, "20.0",
                "event position follows the same measured precision policy");
        t.check(exact(solved.result.event_time, 10, 1) &&
                    exact(solved.result.event_position, 20, 1),
                "reported precision does not replace either exact result");
        t.check(solved.result.event_time.precision.kind == NumberKind::Measured &&
                    solved.result.event_time.precision.significant_digits == 3,
                "the typed result retains the limiting significant-figure count");
        t.check(contains(solved.rules, "physics.catch-up.significant-figures"),
                "final-only rounding is present in the derivation");
        t.equal(solved.report_evidence,
                "passed, 10.0 is within half a unit in the last place of 10\n"
                "passed, 20.0 is within half a unit in the last place of 20",
                "each report step's evidence names its outcome and the two values it compared, so "
                "neither a constant detail nor a changed outcome can stand in for the comparison");
    }
    {
        // 2t = 4(t - 4.98) meets at exactly 9.96, and the inputs combine to two figures. Rounded
        // there the answer is 10, whose last figure is in the units place, not the tenths the
        // operands were written to. The two answers also have different places, so one shared
        // precision described whichever was assigned second.
        const Run carry = run(problem(body("Atlas", "0 m", "2.0 m/s", "0 s"),
                                      body("Boreal", "0 m", "4.0 m/s", "4.98 s")));
        t.equal(carry.result.event_time_text, "10", "the meeting time reports at two figures");
        t.check(carry.result.event_time.precision.last_significant_decimal_place == 0,
                "and the reported time's place is the units place its last figure sits in");
        t.equal(carry.result.event_position_text, "20", "the meeting position reports at two "
                                                        "figures");
        t.check(carry.result.event_position.precision.last_significant_decimal_place == 0,
                "and 20 ends in the units place too, the second figure being the zero");
    }
    {
        // The meeting position is a sum, x0 plus v times the elapsed time, so it is governed by
        // decimal places and not by the figure count the event time is governed by. The exact
        // position is 1001.001 m. Reported to the fewest figures among the givens it was 1000,
        // which throws away the tenth Atlas position was written to and is a metre out.
        const Run wide = run(problem(body("Atlas", "1000.0 m", "0.10 m/s", "0 s"),
                                     body("Boreal", "0 m", "100.0 m/s", "0 s")));
        t.equal(wide.result.event_position_text, "1001.0",
                "a meeting position keeps the coarsest place its own terms reach");
        t.check(wide.result.event_position.precision.last_significant_decimal_place == -1 &&
                    wide.result.event_position.precision.significant_digits == 5,
                "and both its precision fields describe that place");
        t.equal(wide.result.event_time_text, "10",
                "while the event time is a quotient and stays governed by the figure count");
        const Run swapped = run(problem(body("Boreal", "0 m", "100.0 m/s", "0 s"),
                                        body("Atlas", "1000.0 m", "0.10 m/s", "0 s")));
        t.equal(swapped.result.event_position_text, "1001.0",
                "the two position laws are two routes to one value, so which body was listed first "
                "does not change the place the answer is reported to");
    }
    {
        // The same meeting with Atlas position written as 1.0 km, which pins the hundreds and not
        // the tenths the same numeral in metres would. The place has to be converted with the
        // value: read straight off the kilometre literal it would claim a tenth of a metre here.
        const Run scaled = run(problem(body("Atlas", "1.0 km", "0.10 m/s", "0 s"),
                                       body("Boreal", "0 m", "100.0 m/s", "0 s")));
        t.equal(scaled.result.event_position_text, "1000",
                "a kilometre written to a tenth pins the meeting position to the hundred metres");
        t.check(scaled.result.event_position.precision.last_significant_decimal_place == 2,
                "and the reported place is in the metres the answer is written in");
    }
    {
        // The same meeting ten times faster, where the two answers land at different magnitudes:
        // 9.96 s reports as 9.96 and 199.2 m as 199, so one place cannot describe both.
        const Run apart = run(problem(body("Atlas", "0 m", "20.0 m/s", "0 s"),
                                      body("Boreal", "0 m", "40.0 m/s", "4.98 s")));
        t.equal(apart.result.event_time_text, "9.96", "the time reports to three figures");
        t.equal(apart.result.event_position_text, "199", "and the position to three figures");
        t.check(apart.result.event_time.precision.last_significant_decimal_place == -2 &&
                    apart.result.event_position.precision.last_significant_decimal_place == 0,
                "each answer carries the place its own value reaches, which one shared precision "
                "could not say");
    }
    {
        const Run solved = run(problem(body("First", "0 m", "1.0 m/s", "0 s"),
                                       body("Second", "0 m", "2.0 m/s", "0 s")));
        t.equal(catch_up_outcome_name(solved.result.outcome), "solved",
                "a measured-input event at zero remains reportable");
        t.equal(solved.result.event_time_text, "0",
                "zero uses the existing exact zero spelling");
        t.equal(solved.result.event_position_text, "0.0",
                "the position is exactly zero but only known to the tenth the event time is, and "
                "the sum rule says so where the figure count could not");
        t.check(solved.result.event_time.precision.kind == NumberKind::Measured &&
                    solved.result.event_time.precision.significant_digits == 2,
                "zero retains measured precision in typed metadata");
    }
    {
        const Run distant = run(problem(body("Atlas", "10.00 km", "-10 m/s", "0 s"),
                                        body("Boreal", "-9.99 km", "10 m/s", "0 s")));
        t.equal(catch_up_outcome_name(distant.result.outcome), "solved",
                "a meeting whose position rounds away to zero at its own coarse place is still a "
                "solved problem");
        t.equal(derivation_status_name(distant.result.status), "solved and verified",
                "and the rounding is checked against the place it was rounded at rather than "
                "against a place counted off the characters of the answer");
        t.equal(distant.result.event_position_text, "0",
                "zero is the correct report at the coarse place both position laws reach");
        const Run offset = run(problem(body("Atlas", "11.00 km", "-10 m/s", "0 s"),
                                       body("Boreal", "-8.99 km", "10 m/s", "0 s")));
        t.equal(derivation_status_name(offset.result.status), "solved and verified",
                "while the same shape meeting away from the origin rounds to a numeral with "
                "figures in it and passes, so the rows above measure the zero and not a check "
                "that never ran");
        t.equal(offset.result.event_position_text, "1000",
                "and the rounding it passed is one that changed the text");
    }

    {
        Budget budget;
        budget.poll = always_cancel;
        const Run stopped = run(reference_problem(), budget);
        t.evidence("PERF-003", catch_up_outcome_name(stopped.result.outcome), "cancelled",
                   "an existing cancellation stops catch-up work immediately");
        t.check(stopped.steps == 0 && stopped.result.event_time_text.empty(),
                "cancellation leaves no partial derivation or answer");
    }
    {
        PollAfter poll;
        poll.stop_at = 2;
        Budget budget;
        budget.poll = cancel_after;
        budget.poll_context = &poll;
        const Run stopped = run(reference_problem(), budget);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "cancelled",
                "nested linear-solver cancellation propagates to the family");
        t.check(stopped.steps == 0, "nested cancellation rewinds catch-up provenance");
    }
    {
        Budget budget;
        budget.max_steps = 0;
        const Run stopped = run(reference_problem(), budget);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "resource exceeded",
                "a zero step budget halts before provenance is added");
        t.equal(stopped.result.detail, "step limit", "the exhausted resource is named");
        t.check(stopped.steps == 0, "a step halt leaves no partial derivation");
    }
    {
        Budget budget;
        budget.max_steps = 4;
        const Run stopped = run(reference_problem(), budget);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "resource exceeded",
                "catch-up and nested algebra share one step budget");
        t.check(stopped.steps == 0, "an aggregate step halt rewinds both derivations");
    }
    {
        Budget budget;
        budget.max_rewrites = 0;
        const Run stopped = run(reference_problem(), budget);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "resource exceeded",
                "the nested algebra honors the family rewrite budget");
        t.check(stopped.steps == 0, "a rewrite halt leaves no partial derivation");
    }
    {
        Limits limits;
        limits.max_nodes = 2;
        const Run stopped = run(reference_problem(), Budget(), limits);
        t.equal(catch_up_outcome_name(stopped.result.outcome), "resource exceeded",
                "an Arena limit is a resource outcome");
        t.check(stopped.steps == 0 && stopped.result.event_time_text.empty(),
                "Arena exhaustion offers no partial proof or value");
    }

    {
        const Run solved = run(reference_problem());
        t.evidence("PHYS-025",
                   contains(solved.rules, "physics.catch-up.constant-velocity") &&
                       contains(solved.rules, "physics.catch-up.check-dimensions") &&
                       contains(solved.rules, "physics.catch-up.equal-position") &&
                       contains(solved.rules, "eq.divide-both-sides") &&
                       contains(solved.rules, "physics.catch-up.shared-domain") &&
                       contains(solved.rules, "physics.catch-up.verify-first-position") &&
                       contains(solved.rules, "physics.catch-up.verify-second-position"),
                   "provenance names model selection, dimensions, algebra, domain and both substitutions");
        t.check(solved.plans >= 2 && solved.transformations >= 3 && solved.checks >= 5,
                "the result carries catch-up and algebra plan, transformation and check records");
        t.check(solved.all_verified,
                "every claim in a successful catch-up derivation has passing evidence");
        t.equal(solved.context_family, "physics.kinematics.catch-up.equal-position",
                "the solution context identifies the catch-up family");
        t.equal(solved.context_branch,
                "event time is in both bodies' active intervals",
                "the solution context preserves the physical domain convention");
        t.check(solved.assumptions == 3,
                "the solution context lists both body laws and the coordinate convention");
        t.check(solved.result.cost.steps == solved.steps,
                "reported step cost matches the complete derivation");
    }

    // PHYS-027 against known answers, which is the layer the invariant pass cannot supply: it reads
    // the two fields but cannot tell from a string which kind it is looking at. These conditions are
    // about bodies and start times rather than about the form of an expression, so every one of them
    // belongs on the physical side and the mathematical side of this family stays empty.
    {
        Arena arena;
        Derivation derivation;
        Budget budget;
        solve_catch_up(arena, derivation, reference_problem(), budget);
        size_t physical = 0;
        size_t mathematical = 0;
        bool physics_rule_holds_mathematical = false;
        for (size_t index = 0; index < derivation.size(); ++index) {
            const Step &step = derivation.at(static_cast<StepId>(index));
            physical += step.assumptions_before.size();
            mathematical += step.domain_restrictions.size();
            if (!step.domain_restrictions.empty() &&
                step.rule_id.compare(0, 8, "physics.") == 0)
                physics_rule_holds_mathematical = true;
        }
        t.check(physical > 0,
                "catch-up records its modelling assumptions on the steps that rest on them");
        t.check(mathematical == 0 && !physics_rule_holds_mathematical,
                "no catch-up condition is filed as a mathematical domain restriction, which is "
                "what canonical.h derives from an expression rather than what a rule assumes");
    }
}

}  // namespace nps
