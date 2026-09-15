#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "nps/core/print.h"
#include "nps/physics/relative_motion.h"
#include "nps/physics/vector_components.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

Vector parsed_vector(const char *text, const char *frame = "lab") {
    Vector vector;
    std::string error;
    if (!parse_vector(text, &vector, &error)) {
        vector.rank = 0;
        vector.frame.name = "parse failed: " + error;
        return vector;
    }
    vector.frame.name = frame;
    return vector;
}

RelativeMotionProblem problem(const Vector &subject, const Vector &reference) {
    RelativeMotionProblem input;
    input.subject_name = "drone";
    input.reference_name = "wind";
    input.subject_velocity = subject;
    input.reference_velocity = reference;
    return input;
}

struct Run {
    explicit Run(const RelativeMotionProblem &problem, const Budget &budget = Budget(),
                 Backend *backend = nullptr, const Limits &limits = Limits())
        : arena(limits),
          result(solve_relative_motion(arena, derivation, problem, budget, backend)) {}

    Arena arena;
    Derivation derivation;
    RelativeMotionResult result;
};

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return true;
    }
    return false;
}

StepId step_with_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return static_cast<StepId>(index);
    }
    return kNoStep;
}

bool has_failed_verification(const Derivation &derivation) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).has_failed_verification())
            return true;
    }
    return false;
}

size_t rule_index(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return index;
    }
    return derivation.size();
}

const CheckPayload *check_payload(const Derivation &derivation, const char *rule) {
    const size_t index = rule_index(derivation, rule);
    if (index == derivation.size())
        return nullptr;
    return derivation.check(static_cast<StepId>(index));
}

const PlanPayload *root_plan(const Derivation &derivation) {
    if (derivation.roots().empty())
        return nullptr;
    return derivation.plan(derivation.roots().front());
}

class SequenceBackend : public Backend {
  public:
    explicit SequenceBackend(std::vector<std::string> replies)
        : replies_(std::move(replies)) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "no scripted reply";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    size_t next_ = 0;
};

class FailingBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *, std::string *error) override {
        commands.push_back(command);
        *error = "backend down";
        return false;
    }

    std::vector<std::string> commands;
};

bool cancel_now(void *) { return true; }

RelativeMotionIdentity identity(RelativeMotionUnknown unknown) {
    RelativeMotionIdentity input;
    input.subject_name = "plane";
    input.medium_name = "wind";
    input.reference_name = "earth";
    input.unknown = unknown;
    // Problem 5 of the chapters 1-4 test, with the airspeed already resolved into components.
    input.subject_relative_to_medium = parsed_vector("(171, 469.8) km/h");
    input.medium_relative_to_reference = parsed_vector("(-171, -69.8) km/h");
    input.subject_relative_to_reference = parsed_vector("(0, 400) km/h");
    return input;
}

struct IdentityRun {
    explicit IdentityRun(const RelativeMotionIdentity &problem, const Budget &budget = Budget())
        : arena(), result(solve_relative_motion_identity(arena, derivation, problem, budget)) {}

    Arena arena;
    Derivation derivation;
    RelativeMotionResult result;
};

bool exact_components(const Vector &value, int64_t xn, int64_t xd, int64_t yn, int64_t yd) {
    return value.x.num == xn && value.x.den == xd && value.y.num == yn && value.y.den == yd;
}

}  // namespace

void run_relative_motion_tests(TestSink &t) {
    {
        // The wind is the unknown, which is the arrangement the worked problem asks for.
        IdentityRun wind(identity(RelativeMotionUnknown::MediumRelativeToReference));
        t.equal(relative_motion_outcome_name(wind.result.outcome), "solved",
                "a three-frame statement solves for its medium velocity");
        t.check(wind.result.has_value && exact_components(wind.result.velocity, -95, 2, -349, 18),
                "the wind velocity is the plane over ground minus the plane through the air");
        t.equal(relative_direction_name(wind.result.direction), "southwest",
                "and points into the third quadrant of the declared axes");
        t.check(has_rule(wind.derivation, "physics.relative-motion.subscript-cancellation"),
                "the cancellation of the inner frame is recorded as its own check");
        t.check(has_rule(wind.derivation, "physics.relative-motion.isolate-unknown"),
                "and the rearrangement that isolates the unknown is recorded");
        t.check(step_with_rule(wind.derivation, "physics.relative-motion.isolate-unknown") <
                    step_with_rule(wind.derivation, "physics.relative-motion.definition"),
                "with the symbolic rearrangement recorded before any number is substituted");
        t.check(wind.result.interpretation.find("medium relative to reference") !=
                    std::string::npos,
                "and the interpretation names which of the three velocities was unknown");

        // The other two arrangements of the same identity, each recovering a known input.
        IdentityRun airspeed(identity(RelativeMotionUnknown::SubjectRelativeToMedium));
        t.check(airspeed.result.has_value &&
                    exact_components(airspeed.result.velocity, 95, 2, 261, 2),
                "the same identity solves for the subject velocity through the medium");
        IdentityRun ground(identity(RelativeMotionUnknown::SubjectRelativeToReference));
        t.check(ground.result.has_value && exact_components(ground.result.velocity, 0, 1, 1000, 9),
                "and for the subject velocity over the reference, by reversing the subscripts of "
                "the medium term");
        t.equal(relative_direction_name(ground.result.direction), "north",
                "which is due north for this fixture");

        RelativeMotionIdentity repeated = identity(RelativeMotionUnknown::MediumRelativeToReference);
        repeated.medium_name = repeated.reference_name;
        IdentityRun collided(repeated);
        t.equal(relative_motion_outcome_name(collided.result.outcome), "invalid problem",
                "two frames sharing a name is refused rather than cancelled away");
    }
    {
        Run solved(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")));
        t.evidence("PHYS-001", relative_motion_outcome_name(solved.result.outcome), "solved",
                   "two framed velocity vectors produce an exact relative velocity");
        t.equal(solved.result.value_text, "(6 i - 5 j) m/s",
                "subject minus reference is reported in Cartesian components");
        t.check(solved.result.has_value && solved.result.velocity.x.num == 6 &&
                    solved.result.velocity.x.den == 1 && solved.result.velocity.y.num == -5 &&
                    solved.result.velocity.y.den == 1,
                "the structured answer retains both exact rational components");
        t.equal(relative_direction_name(solved.result.direction), "southeast",
                "a negative north component is interpreted as south");
        t.check(solved.result.interpretation.find("southeast relative to wind") !=
                    std::string::npos,
                "the direction states whose motion is relative to whom");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.kinematics.relative-motion.components.two-dimension",
                "the context identifies the relative-motion family");
        const PlanPayload *plan = root_plan(solved.derivation);
        const std::string model = print(solved.arena, solved.derivation.context.normalized_problem_model);
        const std::string equation = print(solved.arena, solved.result.equation);
        t.evidence(
            "PHYS-003",
            plan != nullptr && plan->matched_problem_facts.size() == 2 &&
                model.find("framed_vector(vector(10, -2)") != std::string::npos &&
                model.find("framed_vector(vector(4, 3)") != std::string::npos &&
                equation.find("relative_velocity(drone, wind)") != std::string::npos &&
                solved.derivation.at(solved.derivation.roots().front()).goal ==
                    "Find the subject velocity relative to the reference" &&
                solved.derivation.context.active_assumptions.size() == 2 &&
                solved.derivation.context.active_assumptions[1].find("positive i is east") !=
                    std::string::npos,
            "the normalized model lists both known vectors, the plan names the unknown, and the "
            "context lists assumptions and axes. The subject and reference are known quantities "
            "rather than assumptions, so they are counted in the plan's matched facts above and no "
            "longer in this list");
        t.evidence("PHYS-004",
                   has_rule(solved.derivation, "physics.relative-motion.check-rank") &&
                       has_rule(solved.derivation, "physics.relative-motion.check-frame-match") &&
                       has_rule(solved.derivation,
                                "physics.relative-motion.check-input-dimensions") &&
                       has_rule(solved.derivation, "physics.relative-motion.definition"),
                   "model checks precede the relative-velocity definition");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "local exact subtraction verifies every recorded claim");
        t.check(solved.result.equation != kNoNode && solved.result.substituted != kNoNode,
                "the answer retains the governing and SI-substituted equations");
        t.check(solved.result.cost.steps == solved.derivation.size(),
                "every charged relative-motion step has provenance");
    }

    {
        SequenceBackend backend({"7", "-6"});
        Run solved(problem(parsed_vector("(36, -18) km/h"), parsed_vector("(3, 1) m/s")),
                   Budget(), &backend);
        t.equal(solved.result.value_text, "(7 i - 6 j) m/s",
                "mixed velocity units convert exactly before subtraction");
        t.evidence("PHYS-006",
                   has_rule(solved.derivation, "physics.relative-motion.convert-si"),
                   "mixed-unit relative motion records its exact SI conversion");
        const size_t inputs =
            rule_index(solved.derivation, "physics.relative-motion.check-input-dimensions");
        const size_t conversion =
            rule_index(solved.derivation, "physics.relative-motion.convert-si");
        const size_t difference =
            rule_index(solved.derivation, "physics.relative-motion.check-result-dimension");
        const CheckPayload *derived =
            check_payload(solved.derivation, "physics.relative-motion.check-result-dimension");
        t.evidence("PHYS-002",
                   inputs < conversion && conversion < difference &&
                       difference < solved.derivation.size() && derived != nullptr &&
                       derived->expected_relation == "L T^-1" &&
                       derived->observed_result == "L T^-1",
                   "the relative-motion derivation checks the input dimensions, substitutes the SI "
                   "values, then reads the difference dimension again from what it substituted");
        t.check(solved.result.cost.backend_calls == 2 && backend.commands.size() == 2,
                "Giac checks each relative component exactly once");
        t.check(has_rule(solved.derivation, "physics.relative-motion.component-i") &&
                    has_rule(solved.derivation, "physics.relative-motion.component-j"),
                "both component subtractions have separate provenance");
        const PlanPayload *plan = root_plan(solved.derivation);
        t.evidence(
            "PHYS-025",
            plan != nullptr && !plan->selected_strategy.empty() &&
                plan->applicability_conditions.size() == 5 &&
                solved.derivation.context.active_assumptions.size() == 2 &&
                has_rule(solved.derivation, "physics.relative-motion.definition") &&
                has_rule(solved.derivation, "physics.relative-motion.check-rank") &&
                has_rule(solved.derivation, "physics.relative-motion.check-frame-declared") &&
                has_rule(solved.derivation, "physics.relative-motion.check-frame-match") &&
                has_rule(solved.derivation, "physics.relative-motion.check-input-dimensions") &&
                has_rule(solved.derivation, "physics.relative-motion.check-result-dimension") &&
                has_rule(solved.derivation, "physics.relative-motion.component-i") &&
                has_rule(solved.derivation, "physics.relative-motion.component-j") &&
                has_rule(solved.derivation, "physics.relative-motion.interpret-direction") &&
                solved.result.status == DerivationStatus::SolvedAndVerified,
            "the module exposes model conditions, law, axes, checks and component algebra");
    }

    {
        Run solved(problem(parsed_vector("(10.0, -2.0) m/s"),
                           parsed_vector("(4.00, 3.00) m/s")));
        t.equal(solved.result.value_text, "(6.0 i - 5.0 j) m/s",
                "measured relative velocity rounds once at the shared decimal place");
        t.check(solved.result.velocity.x.num == 6 && solved.result.velocity.y.num == -5,
                "measured reporting does not replace exact internal components");
        t.check(has_rule(solved.derivation, "physics.relative-motion.significant-figures"),
                "final-only relative-motion precision has provenance");
        // The record used to cite the rounder as its own evidence, with an outcome no input changed.
        const StepId rounding =
            step_with_rule(solved.derivation, "physics.relative-motion.significant-figures");
        t.check(rounding != kNoStep, "the rounding step is recorded");
        if (rounding != kNoStep) {
            const Step &step = solved.derivation.at(rounding);
            t.check(step.verifications.size() == 1 &&
                        step.verifications[0].method ==
                            std::string("exact comparison against the unrounded value"),
                    "the rounding step cites the comparison that judged it rather than the "
                    "function that produced it");
            t.equal(step.verifications[0].detail,
                    "(6.0 i - 5.0 j) m/s is within half a unit in the last place of "
                    "(6 i - 5 j) m/s",
                    "and names both of the values it was between, which a literal outcome could "
                    "not have done");
        }
    }
    {
        Run solved(
            problem(parsed_vector("(1.0, 2.0) km/s"), parsed_vector("(300, 400) m/s")));
        t.equal(solved.result.value_text, "(700 i + 1600 j) m/s",
                "measured mixed-unit relative motion reports at the converted hundreds place");
        t.check(solved.result.velocity.x.num == 700 && solved.result.velocity.x.den == 1 &&
                    solved.result.velocity.y.num == 1600 && solved.result.velocity.y.den == 1,
                "mixed-unit precision metadata does not replace exact relative components");
        t.check(solved.result.velocity.precision.kind == NumberKind::Measured &&
                    solved.result.velocity.precision.significant_digits == 1 &&
                    solved.result.velocity.precision.last_significant_decimal_place == 2,
                "relative motion retains the conservatively shared SI precision metadata");
    }

    {
        Run solved(problem(parsed_vector("(-2, 4) m/s"), parsed_vector("(1, 1) m/s")));
        t.equal(relative_direction_name(solved.result.direction), "northwest",
                "negative i and positive j map to northwest");
    }
    {
        Run solved(problem(parsed_vector("(2, 4) m/s"), parsed_vector("(2, 4) m/s")));
        t.equal(relative_direction_name(solved.result.direction), "stationary",
                "equal velocities produce zero relative motion");
        t.check(solved.result.interpretation.find("zero velocity relative") != std::string::npos,
                "zero relative motion is interpreted without inventing a direction");
    }
    {
        Run relative(problem(parsed_vector("(1, -1) m/s", "ground"),
                             parsed_vector("(4, 3) m/s", "ground")));
        SequenceBackend backend(
            {"0", "atan2(-4,-3)", "0", "atan2(-4,-3)*180/pi", "0"});
        Arena polar_arena;
        Derivation polar_derivation;
        const VectorComponentsResult polar = components_to_magnitude_angle(
            polar_arena, polar_derivation, relative.result.velocity, AngleUnit::Degrees, backend);
        const std::string angle = polar.has_polar ? print(polar_arena, polar.polar.angle) : "";
        t.evidence(
            "PHYS-016",
            relative.result.has_value && relative.result.velocity.frame.name == "ground" &&
                relative.derivation.context.active_assumptions.size() == 2 &&
                relative.derivation.context.active_assumptions[1].find("positive i is east") !=
                    std::string::npos &&
                polar.has_polar && print(polar_arena, polar.polar.magnitude) == "5" &&
                polar.polar.angle_unit == AngleUnit::Degrees &&
                angle.find("atan2") != std::string::npos &&
                angle.find("-4") != std::string::npos &&
                angle.find("-3") != std::string::npos,
            "declared Cartesian components reconstruct magnitude and quadrant-correct direction with atan2");
    }

    {
        Run refused(problem(parsed_vector("(1, 2, 3) m/s"), parsed_vector("(1, 2, 3) m/s")));
        t.equal(relative_motion_outcome_name(refused.result.outcome), "rank mismatch",
                "the 2D family refuses 3D velocities explicitly");
        t.check(!refused.result.has_value &&
                    !has_rule(refused.derivation, "physics.relative-motion.definition"),
                "rank failure exposes no answer and applies no law");
    }
    {
        Vector subject = parsed_vector("(1, 2) m/s");
        subject.frame.name.clear();
        Run refused(problem(subject, parsed_vector("(1, 2) m/s")));
        t.equal(relative_motion_outcome_name(refused.result.outcome), "frame undeclared",
                "an undeclared velocity frame is refused");
    }
    {
        Run refused(problem(parsed_vector("(1, 2) m/s", "ground"),
                            parsed_vector("(1, 2) m/s", "air")));
        t.equal(relative_motion_outcome_name(refused.result.outcome), "frame mismatch",
                "different velocity frames are never silently coerced");
        t.check(refused.result.detail.find("explicit basis transformation") != std::string::npos,
                "the frame refusal names the missing operation");
    }
    {
        Run refused(problem(parsed_vector("(1, 2) m"), parsed_vector("(1, 2) m")));
        t.equal(relative_motion_outcome_name(refused.result.outcome), "dimension mismatch",
                "equal non-velocity dimensions do not satisfy the physical roles");
        t.check(refused.result.detail.find("L T^-1") != std::string::npos,
                "the dimension refusal names the required velocity dimension");
    }
    {
        Run refused(problem(parsed_vector("(1, 2) m/s"), parsed_vector("(1, 2) s")));
        t.equal(relative_motion_outcome_name(refused.result.outcome), "dimension mismatch",
                "subject and reference dimensions are checked independently");
    }

    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1, 2) m/s"), parsed_vector("(1, 2) m/s"));
        input.subject_velocity.x.den = 0;
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid exact component is rejected before model construction");
        t.check(refused.derivation.size() == 0,
                "invalid exact input never enters the derivation graph");
    }
    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1, 2) m/s"), parsed_vector("(1, 2) m/s"));
        input.subject_name = input.reference_name;
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "invalid problem",
                "subject and reference identities must remain distinct");
    }
    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1, 2) m/s"), parsed_vector("(1, 2) m/s"));
        input.axes = static_cast<RelativeMotionAxes>(99);
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid coordinate convention is rejected");
    }
    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1.0, 2.0) m/s"), parsed_vector("(1, 2) m/s"));
        input.subject_velocity.precision.significant_digits = 0;
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "invalid problem",
                "inconsistent measured precision is rejected");
    }

    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1, 0) km/s"), parsed_vector("(0, 0) m/s"));
        input.subject_velocity.x.num = std::numeric_limits<int64_t>::max();
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "arithmetic overflow",
                "SI conversion overflow is distinct from invalid input");
        t.check(!refused.result.has_value && refused.result.cost.steps > 0 &&
                    refused.derivation.size() == 0 && refused.derivation.roots().empty() &&
                    refused.result.equation == kNoNode && refused.result.substituted == kNoNode,
                "conversion overflow rewinds every partial step, claim and result handle");
    }
    {
        RelativeMotionProblem input =
            problem(parsed_vector("(1, 0) m/s"), parsed_vector("(-1, 0) m/s"));
        input.subject_velocity.x.num = std::numeric_limits<int64_t>::max();
        Run refused(input);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "arithmetic overflow",
                "a component difference outside int64 is refused after exact GMP arithmetic");
    }

    {
        SequenceBackend backend({"8"});
        Run refused(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")),
                    Budget(), &backend);
        t.equal(relative_motion_outcome_name(refused.result.outcome), "verification failed",
                "a disagreeing backend withholds the relative velocity");
        t.check(!refused.result.has_value && has_failed_verification(refused.derivation),
                "the disagreement exposes no answer and remains visible in the derivation");
    }
    {
        // VER-014 asks that an unavailable verifier is not treated as passed and that the derivation
        // becomes partial. Before this it withdrew the subtraction vector_sub had already done, and
        // it stopped on the i axis so the j component was never even worked out.
        FailingBackend backend;
        Run partial(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")),
                    Budget(), &backend);
        t.equal(relative_motion_outcome_name(partial.result.outcome), "solved",
                "an unavailable backend does not withdraw the local subtraction");
        t.evidence("VER-014",
                   partial.result.has_value && backend.commands.size() == 2 &&
                       partial.result.velocity.x.num == 6 && partial.result.velocity.y.num == -5 &&
                       partial.result.status == DerivationStatus::SolvedButUnchecked &&
                       !has_failed_verification(partial.derivation),
                   "an unavailable verifier leaves both components computed and the derivation "
                   "partial, with nothing recorded as a failed check");
    }

    {
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")),
                      budget);
        t.equal(relative_motion_outcome_name(cancelled.result.outcome), "cancelled",
                "an existing cancellation stops relative-motion work");
        t.check(cancelled.derivation.size() == 0 && !cancelled.result.has_value,
                "cancellation rewinds every partial step and answer");
    }
    {
        Budget budget;
        budget.max_steps = 1;
        Run halted(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")), budget);
        t.equal(relative_motion_outcome_name(halted.result.outcome), "resource exceeded",
                "the shared step budget bounds relative-motion derivation work");
        t.check(halted.derivation.size() == 0 && !halted.result.has_value,
                "a step limit rewinds every partial step and answer");
    }
    {
        Limits limits;
        limits.max_input_bytes = 5;
        Run halted(problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s")),
                   Budget(), nullptr, limits);
        t.equal(relative_motion_outcome_name(halted.result.outcome), "resource exceeded",
                "the shared input limit bounds relative-motion body and frame identifiers");
        t.check(halted.derivation.size() == 0 && !halted.result.has_value &&
                    halted.derivation.context.active_assumptions.empty(),
                "an identity limit publishes neither partial work nor oversized context text");
    }

    {
        const RelativeMotionProblem input =
            problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s"));
        bool every_failure_refused = true;
        bool saw_late_failure = false;
        bool saw_success = false;
        for (size_t max_nodes = 0; max_nodes <= 128; ++max_nodes) {
            Limits limits;
            limits.max_nodes = max_nodes;
            Run run(input, Budget(), nullptr, limits);
            if (run.arena.failed()) {
                every_failure_refused =
                    every_failure_refused &&
                    run.result.outcome == RelativeMotionOutcome::ResourceExceeded &&
                    !run.result.has_value && run.derivation.size() == 0;
                saw_late_failure = saw_late_failure || run.result.cost.steps > 0;
            } else if (run.result.outcome == RelativeMotionOutcome::Solved) {
                saw_success = true;
            }
        }
        t.check(every_failure_refused && saw_late_failure && saw_success,
                "every arena exhaustion point refuses and rewinds instead of returning solved");
    }

    {
        // The same sweep with a backend attached, because reading its reply allocates too and the
        // answer now survives a backend that will not certify it. A long reply is what reaches the
        // window between the request going out and the comparison finishing.
        const RelativeMotionProblem input =
            problem(parsed_vector("(10, -2) m/s"), parsed_vector("(4, 3) m/s"));
        bool every_failure_refused = true;
        bool saw_failure_after_the_backend_answered = false;
        bool saw_success = false;
        for (size_t max_nodes = 0; max_nodes <= 512; ++max_nodes) {
            Limits limits;
            limits.max_nodes = max_nodes;
            SequenceBackend backend({"6+0+0+0+0+0+0+0+0+0+0", "-5+0+0+0+0+0+0+0+0+0+0"});
            Run run(input, Budget(), &backend, limits);
            if (run.arena.failed()) {
                every_failure_refused = every_failure_refused && !run.result.has_value &&
                                        run.result.outcome !=
                                            RelativeMotionOutcome::VerificationFailed;
                saw_failure_after_the_backend_answered =
                    saw_failure_after_the_backend_answered || !backend.commands.empty();
            } else if (run.result.outcome == RelativeMotionOutcome::Solved) {
                saw_success = true;
            }
        }
        t.check(every_failure_refused && saw_failure_after_the_backend_answered && saw_success,
                "an arena that runs out while reading or comparing the backend reply is a resource "
                "halt, never a disagreement and never an answer");
    }

    // STEP-023 and STEP-019. The requirement is that the backend proposes and checks but never
    // produces an accepted transition, and the way to ask that of a record is to run the same
    // problem under different backends and see whether the walkthrough moves. It does not move
    // here: the transitions come from vector_sub either way and Giac only compares against them.
    {
        const auto walkthrough = [](const Derivation &d, const Arena &arena) {
            std::string trace;
            for (size_t index = 0; index < d.size(); ++index) {
                const StepId id = static_cast<StepId>(index);
                const TransformationPayload *t = d.transformation(id);
                if (t == nullptr)
                    continue;
                trace += d.at(id).rule_id;
                trace += " => ";
                trace += t->after == kNoNode ? "unfilled" : print(arena, t->after);
                trace += "\n";
            }
            return trace;
        };

        const RelativeMotionProblem input =
            problem(parsed_vector("(36, -18) km/h"), parsed_vector("(3, 1) m/s"));

        Run alone(input);
        SequenceBackend agreeing({"7", "-6"});
        Run checked(input, Budget(), &agreeing);
        // Deliberately wrong answers. If Giac's reply could reach the record, this run's steps
        // would carry 5 and 5 instead of 7 and -6, so the comparison below is what separates a
        // backend that checks from a backend that decides.
        SequenceBackend contradicting({"5", "5"});
        Run disputed(input, Budget(), &contradicting);
        FailingBackend broken;
        Run unavailable(input, Budget(), &broken);

        const std::string steps_alone = walkthrough(alone.derivation, alone.arena);
        const std::string steps_disputed = walkthrough(disputed.derivation, disputed.arena);
        const std::string steps_unavailable = walkthrough(unavailable.derivation, unavailable.arena);
        const auto is_prefix = [](const std::string &whole, const std::string &part) {
            return part.size() <= whole.size() && whole.compare(0, part.size(), part) == 0;
        };

        t.check(!steps_alone.empty(), "the backend-free run produced a walkthrough to compare");
        t.equal(walkthrough(checked.derivation, checked.arena), steps_alone,
                "STEP-023: an agreeing backend leaves every transition exactly as it was");

        // A backend that disagrees can stop the derivation but cannot bend it: what survives is
        // the same steps in the same order, cut short, rather than different steps arriving at the
        // backend's number. Asked as a prefix rather than as equality because the disagreement is
        // supposed to end the run, and 9 of the 11 steps is what ending it looks like.
        t.check(steps_disputed.size() < steps_alone.size(),
                "a contradicted cross-check ends the derivation early");
        t.evidence("STEP-023", is_prefix(steps_alone, steps_disputed) && !disputed.result.has_value,
                   "a backend answering differently alters no accepted transition and yields no "
                   "answer of its own: the steps that survive are the rules' own, cut short, so "
                   "the walkthrough is generated forward rather than worked backward from what "
                   "the backend returned");
        // A backend that fails outright is the strongest form of the same requirement: it proposed
        // nothing and checked nothing, so it cuts nothing either and the walkthrough is the
        // backend-free one exactly. What it costs is the check, which the status carries.
        t.equal(steps_unavailable, steps_alone,
                "and a backend that fails outright leaves the walkthrough whole");
        t.check(unavailable.result.has_value &&
                    unavailable.result.status == DerivationStatus::SolvedButUnchecked,
                "keeping the answer the rules reached, marked as the unchecked one it now is");

        // The disagreement has to be visible somewhere, or the run above would only show that the
        // record ignores the backend, which fails the requirement from the other side: a
        // cross-check nobody reads is not a cross-check.
        t.check(!has_failed_verification(alone.derivation) &&
                    has_failed_verification(disputed.derivation),
                "while the disagreement is recorded as a failed verification on the step it "
                "contradicts");
        t.evidence("STEP-019",
                   has_failed_verification(disputed.derivation) && !disputed.result.has_value &&
                       is_prefix(steps_alone, steps_disputed),
                   "a backend answer the rule's own result contradicts is kept as a diagnostic "
                   "cross-check on the step and refused as a walkthrough, rather than presented "
                   "as one");

        // The record itself, not only the status. Each component obligation must be discharged by a
        // method its schema promises before the run can call the walkthrough solved and verified.
        struct Audited { const char *label; const Run *run; };
        const Audited audited[] = {
            {"no backend", &alone},
            {"an agreeing backend", &checked},
            {"a backend that failed outright", &unavailable}};
        for (const Audited &row : audited) {
            invariants::Pass pass;
            std::vector<std::string> violations;
            pass.walk(row.run->arena, row.run->derivation, false, true, &violations);
            std::string all;
            for (size_t i = 0; i < violations.size(); ++i) {
                all += (i ? " | " : ", got ") + violations[i];
            }
            t.check(violations.empty(),
                    std::string("every relative-motion obligation is discharged with ") +
                        row.label + all);
            t.check(row.run->result.status != DerivationStatus::SolvedAndVerified ||
                        violations.empty(),
                    std::string("relative motion reports verified only with schema-recognised evidence for ") +
                        row.label);
        }
    }
}

}  // namespace nps
