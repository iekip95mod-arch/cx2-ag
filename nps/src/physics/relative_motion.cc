#include "nps/physics/relative_motion.h"

#include <utility>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"

namespace nps {
namespace {

Dimension velocity_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.time = -1;
    return dimension;
}

RelativeMotionResult failed(RelativeMotionOutcome outcome, DerivationStatus status,
                            const std::string &detail) {
    RelativeMotionResult failure;
    failure.outcome = outcome;
    failure.status = status;
    failure.detail = detail;
    return failure;
}

bool valid_axes(RelativeMotionAxes axes) {
    switch (axes) {
        case RelativeMotionAxes::EastNorth: return true;
    }
    return false;
}

bool normalized(const Rational &source, Rational *value) {
    *value = source;
    return normalise(&value->num, &value->den);
}

bool valid_vector(const Vector &vector, std::string *detail) {
    const Rational components[3] = {vector.x, vector.y, vector.z};
    const char *names[3] = {"x", "y", "z"};
    const size_t active = vector.rank == 3 ? 3 : 2;
    Rational normalized_value;
    for (size_t axis = 0; axis < active; ++axis) {
        if (!normalized(components[axis], &normalized_value)) {
            *detail = std::string("the ") + names[axis] + " component has an invalid exact value";
            return false;
        }
    }
    if (!normalized(vector.unit.scale, &normalized_value) || normalized_value.num <= 0) {
        *detail = "the unit has an invalid SI conversion scale";
        return false;
    }
    switch (vector.precision.kind) {
        case NumberKind::Exact:
            if (vector.precision.significant_digits != 0) {
                *detail = "an exact vector cannot carry a measured significant-figure count";
                return false;
            }
            return true;
        case NumberKind::Measured:
            if (vector.precision.significant_digits == 0 ||
                vector.precision.significant_digits > 18) {
                *detail = "a measured vector needs between 1 and 18 significant figures";
                return false;
            }
            return true;
    }
    *detail = "the vector has an invalid precision kind";
    return false;
}

NodeId rational_node(Arena &arena, const Rational &source) {
    Rational value;
    if (!normalized(source, &value))
        return kNoNode;
    if (value.den == 1)
        return arena.integer(integer_text(value.num));
    const NodeId numerator = arena.integer(integer_text(value.num));
    const NodeId denominator = arena.integer(integer_text(value.den));
    return arena.binary(Kind::Mul, numerator,
                        arena.binary(Kind::Pow, denominator, arena.integer("-1")));
}

NodeId dimension_node(Arena &arena, const Dimension &dimension) {
    int powers[kDimensionCount];
    dimension_powers(dimension, powers);
    std::vector<NodeId> exponents;
    for (int power : powers)
        exponents.push_back(arena.integer(integer_text(power)));
    return arena.call("dimension", exponents);
}

NodeId vector_node(Arena &arena, const Vector &vector) {
    std::vector<NodeId> components;
    components.push_back(rational_node(arena, vector.x));
    components.push_back(rational_node(arena, vector.y));
    if (vector.rank == 3)
        components.push_back(rational_node(arena, vector.z));
    return arena.call("vector", components);
}

NodeId vector_model_node(Arena &arena, const Vector &vector) {
    return arena.call("framed_vector",
                      {vector_node(arena, vector), rational_node(arena, vector.unit.scale),
                       arena.symbol(vector.frame.name), dimension_node(arena, vector.unit.dimension),
                       arena.integer(integer_text(vector.rank))});
}

NodeId problem_model(Arena &arena, const RelativeMotionProblem &problem) {
    return arena.call("relative_motion_problem",
                      {arena.symbol(problem.subject_name), arena.symbol(problem.reference_name),
                       vector_model_node(arena, problem.subject_velocity),
                       vector_model_node(arena, problem.reference_velocity),
                       arena.symbol(relative_motion_axes_name(problem.axes))});
}

NodeId relative_equation(Arena &arena, const RelativeMotionProblem &problem, const Vector &subject,
                         const Vector &reference) {
    const NodeId relative = arena.call(
        "relative_velocity",
        {arena.symbol(problem.subject_name), arena.symbol(problem.reference_name)});
    const NodeId difference =
        arena.binary(Kind::Add, vector_model_node(arena, subject),
                     arena.unary(Kind::Neg, vector_model_node(arena, reference)));
    return arena.binary(Kind::Equals, relative, difference);
}

bool needs_conversion(const Vector &vector) {
    Rational scale;
    return !normalized(vector.unit.scale, &scale) || scale.num != 1 || scale.den != 1;
}

VerificationRecord verification(const char *method, const std::string &detail,
                                EvidenceStrength passing, VerificationOutcome outcome) {
    VerificationRecord record;
    record.method = method;
    record.detail = detail;
    record.outcome = outcome;
    record.strength = strength_for(outcome, passing);
    return record;
}

bool add_check(Derivation &derivation, Meter &meter, StepId parent, const char *rule_id,
               const char *rule_name, const std::string &goal, const std::string &explanation,
               const char *obligation_id, const std::string &obligation, const char *method,
               const std::string &verification_detail, EvidenceStrength passing,
               VerificationOutcome outcome, const std::string &target,
               const std::string &expected, const std::string &observed,
               size_t backend_requests = 0) {
    if (!meter.step())
        return false;
    Step step;
    step.phase = "check";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.claim = ClaimType::Definition;
    step.proof_obligations.push_back({obligation_id, obligation});
    step.verifications.push_back(verification(method, verification_detail, passing, outcome));
    step.backend_requests = static_cast<uint32_t>(backend_requests);
    CheckPayload payload;
    payload.target_claim = target;
    payload.check_method = method;
    payload.expected_relation = expected;
    payload.observed_result = observed;
    derivation.add_check(parent, std::move(step), std::move(payload));
    return true;
}

bool add_transformation(Derivation &derivation, Meter &meter, StepId parent, Step step,
                        NodeId before, const std::string &action, NodeId after, bool reversible) {
    if (!meter.rewrite() || !meter.step())
        return false;
    TransformationPayload payload;
    payload.before = before;
    payload.after = after;
    payload.concrete_action = action;
    payload.reversible = reversible;
    derivation.add_transformation(parent, std::move(step), std::move(payload));
    return true;
}

// detailed has no default on purpose. STEP-021 wants every transformation to say how to recognise
// its rule again, and a builder that lets the field be left out is why it was empty here.
Step transformation_step(const char *rule_id, const char *rule_name, const std::string &goal,
                         const std::string &explanation, const std::string &detailed,
                         ClaimType claim,
                         const VerificationRecord &record, size_t backend_requests = 0) {
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = claim;
    step.verifications.push_back(record);
    step.backend_requests = static_cast<uint32_t>(backend_requests);
    return step;
}

RelativeDirection direction_of(const Vector &velocity) {
    const int horizontal = velocity.x.num < 0 ? -1 : velocity.x.num > 0 ? 1 : 0;
    const int vertical = velocity.y.num < 0 ? -1 : velocity.y.num > 0 ? 1 : 0;
    if (horizontal == 0 && vertical == 0)
        return RelativeDirection::Stationary;
    if (vertical == 0)
        return horizontal > 0 ? RelativeDirection::East : RelativeDirection::West;
    if (horizontal == 0)
        return vertical > 0 ? RelativeDirection::North : RelativeDirection::South;
    if (horizontal > 0)
        return vertical > 0 ? RelativeDirection::Northeast : RelativeDirection::Southeast;
    return vertical > 0 ? RelativeDirection::Northwest : RelativeDirection::Southwest;
}

std::string direction_interpretation(const RelativeMotionProblem &problem,
                                     RelativeDirection direction) {
    if (direction == RelativeDirection::Stationary) {
        return problem.subject_name + " has zero velocity relative to " + problem.reference_name;
    }
    return problem.subject_name + " moves " + relative_direction_name(direction) + " relative to " +
           problem.reference_name + " in the declared east-north axes";
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const RelativeMotionProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.kinematics.relative-motion.components.two-dimension";
    inputs.requested_method =
        "validate two velocity vectors, convert to SI, subtract matching components, verify, and "
        "interpret direction";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    if (model != kNoNode) {
        if (!problem.subject_velocity.frame.name.empty() &&
            problem.subject_velocity.frame == problem.reference_velocity.frame) {
            inputs.active_assumptions.push_back("both velocities use frame " +
                                                problem.subject_velocity.frame.name);
        } else {
            inputs.active_assumptions.push_back("subject frame is " +
                                                problem.subject_velocity.frame.name);
            inputs.active_assumptions.push_back("reference frame is " +
                                                problem.reference_velocity.frame.name);
        }
        if (valid_axes(problem.axes))
            inputs.active_assumptions.push_back("positive i is east and positive j is north");
    }
    inputs.unit_policy = "exact SI conversion and component subtraction, final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

RelativeMotionResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                                const RelativeMotionProblem &problem, Backend *giac,
                                NodeId *model) {
    if (!valid_axes(problem.axes)) {
        return failed(RelativeMotionOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the coordinate convention is invalid");
    }
    if (problem.subject_name.empty() || problem.reference_name.empty() ||
        problem.subject_name == problem.reference_name) {
        return failed(RelativeMotionOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "subject and reference must have distinct nonempty names");
    }
    size_t remaining_identity_bytes = arena.limits().max_input_bytes;
    const std::string *identity_fields[4] = {
        &problem.subject_name, &problem.reference_name, &problem.subject_velocity.frame.name,
        &problem.reference_velocity.frame.name};
    for (const std::string *field : identity_fields) {
        if (field->size() > remaining_identity_bytes) {
            return failed(RelativeMotionOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached,
                          "body and frame identifiers exceed the input byte limit");
        }
        remaining_identity_bytes -= field->size();
    }
    std::string invalid_detail;
    if (!valid_vector(problem.subject_velocity, &invalid_detail)) {
        return failed(RelativeMotionOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "subject velocity: " + invalid_detail);
    }
    if (!valid_vector(problem.reference_velocity, &invalid_detail)) {
        return failed(RelativeMotionOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "reference velocity: " + invalid_detail);
    }

    *model = problem_model(arena, problem);
    if (arena.failed()) {
        return failed(RelativeMotionOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }

    PlanPayload plan;
    plan.strategy_id = "physics.relative-motion.plan";
    plan.selected_strategy = "Relative velocity from Cartesian components";
    plan.matched_problem_facts.push_back("subject: " + problem.subject_name);
    plan.matched_problem_facts.push_back("reference: " + problem.reference_name);
    plan.alternatives_considered.push_back("explicit frame transformation from a supplied basis");
    plan.selection_rationale =
        giac ? "the existing exact vector subtraction produces the candidate and Giac checks each component"
             : "the existing exact vector subtraction produces and checks both components";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find the subject velocity relative to the reference";
    plan_step.rule_id = "physics.relative-motion.plan";
    plan_step.rule_name = "Cartesian relative-motion plan";
    plan_step.explanation_short =
        "Check roles, dimensions and frame before applying relative velocity component by component";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.relative-motion.rank-two",
        "both inputs are two-dimensional velocities", "rank comparison",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before subtracting components");
    register_strategy_precondition(
        plan, plan_step, "pre.relative-motion.frames-declared",
        "both velocities declare named frames", "frame declaration",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before comparing frame identity");
    register_strategy_precondition(
        plan, plan_step, "pre.relative-motion.frames-match",
        "both velocities use the same frame", "frame identity",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before subtracting components");
    register_strategy_precondition(
        plan, plan_step, "pre.relative-motion.axes",
        "positive i is east and positive j is north", "coordinate convention validation",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        "the supplied coordinate convention is registered and valid");
    register_strategy_precondition(
        plan, plan_step, "pre.relative-motion.velocity-dimensions",
        "both inputs have velocity dimension", "dimensional analysis",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked before applying relative velocity");
    if (!meter.step())
        return RelativeMotionResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool rank_ok = problem.subject_velocity.rank == 2 && problem.reference_velocity.rank == 2;
    const std::string ranks = "ranks " + std::to_string(problem.subject_velocity.rank) + " and " +
                              std::to_string(problem.reference_velocity.rank);
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.check-rank",
                   "Relative-motion vector rank", "Check both velocity ranks",
                   "This family resolves motion in one Cartesian plane",
                   "obl.relative-motion.rank-two", "both velocity vectors have rank two",
                   "rank comparison", ranks, EvidenceStrength::StructurallyValid,
                   rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "both velocities are two-dimensional", "rank 2 and rank 2", ranks)) {
        return RelativeMotionResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.relative-motion.rank-two",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, ranks);
    if (!rank_ok) {
        return failed(RelativeMotionOutcome::RankMismatch, DerivationStatus::InvalidInput,
                      "relative motion requires two velocity vectors with two components");
    }

    const bool frames_declared = !problem.subject_velocity.frame.name.empty() &&
                                 !problem.reference_velocity.frame.name.empty();
    const std::string frames = "frames '" + problem.subject_velocity.frame.name + "' and '" +
                               problem.reference_velocity.frame.name + "'";
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.check-frame-declared",
                   "Declared velocity frames", "Check that both velocity frames are declared",
                   "A component vector needs a named Cartesian frame",
                   "obl.relative-motion.frames-declared",
                   "both velocity vectors declare a nonempty frame", "frame declaration", frames,
                   EvidenceStrength::StructurallyValid,
                   frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "both velocities name their frame", "two nonempty frame names", frames)) {
        return RelativeMotionResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.relative-motion.frames-declared",
        frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_declared) {
        return failed(RelativeMotionOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                      "both velocities must declare a named frame");
    }

    const bool frames_match = problem.subject_velocity.frame == problem.reference_velocity.frame;
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.check-frame-match",
                   "Matching velocity frames", "Check that both velocity frames match",
                   "Subtracting components across frames needs an explicit basis transformation",
                   "obl.relative-motion.frames-match", "both velocities use the same frame",
                   "frame identity", frames, EvidenceStrength::StructurallyValid,
                   frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "subject and reference velocities share one frame", "identical frame names",
                   frames)) {
        return RelativeMotionResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.relative-motion.frames-match",
        frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_match) {
        return failed(RelativeMotionOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                      "cannot subtract velocities in frame " + problem.subject_velocity.frame.name +
                          " and frame " + problem.reference_velocity.frame.name +
                          " without an explicit basis transformation");
    }

    const Dimension required = velocity_dimension();
    const bool dimensions_ok = problem.subject_velocity.unit.dimension == required &&
                               problem.reference_velocity.unit.dimension == required;
    const std::string dimensions =
        "subject " + dimension_text(problem.subject_velocity.unit.dimension) + ", reference " +
        dimension_text(problem.reference_velocity.unit.dimension);
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.check-input-dimensions",
                   "Velocity dimensions", "Check both physical quantity roles",
                   "Relative velocity subtracts two quantities with dimension L T^-1",
                   "obl.relative-motion.velocity-dimensions",
                   "both supplied vectors are velocities", "dimensional analysis", dimensions,
                   EvidenceStrength::DimensionallyValid,
                   dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "both inputs have velocity dimension", "subject L T^-1, reference L T^-1",
                   dimensions)) {
        return RelativeMotionResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.relative-motion.velocity-dimensions",
        dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, dimensions);
    if (!dimensions_ok) {
        return failed(RelativeMotionOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "relative motion requires subject and reference velocities with dimension L "
                      "T^-1, but received " +
                          dimensions);
    }

    const NodeId equation = relative_equation(arena, problem, problem.subject_velocity,
                                               problem.reference_velocity);
    if (arena.failed()) {
        return failed(RelativeMotionOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    Step law = transformation_step(
        "physics.relative-motion.definition", "Relative velocity definition",
        "Apply the relative velocity relation", "Use v(subject/reference) = v(subject) - v(reference)",
        "Reach for this whenever a question asks how fast one thing is moving as seen from another "
        "that is itself moving: a car from another car, a boat from the water, a plane from the "
        "air. Subtracting the reference's velocity is what it means to watch from the reference: "
        "if you were moving alongside it, its own velocity would look like zero and everything "
        "else would shift by the same amount. The order matters, because subject minus reference "
        "and reference minus subject point opposite ways.",
        ClaimType::Definition,
        verification("checked physical-law applicability",
                     "the roles, ranks, frames and velocity dimensions passed",
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    law.proof_obligations.push_back(
        {"obl.relative-motion.definition-after-checks",
         "the relative velocity definition is applied only after its conditions pass"});
    if (!add_transformation(derivation, meter, plan_id, std::move(law), *model,
                            "Apply v(subject/reference) = v(subject) - v(reference)", equation,
                            false)) {
        return RelativeMotionResult();
    }

    Vector subject_si;
    Vector reference_si;
    if (!to_si(problem.subject_velocity, &subject_si) ||
        !to_si(problem.reference_velocity, &reference_si)) {
        return failed(RelativeMotionOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "a velocity component does not fit exact arithmetic after SI conversion");
    }
    const NodeId substituted = relative_equation(arena, problem, subject_si, reference_si);
    if (arena.failed()) {
        return failed(RelativeMotionOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    if (needs_conversion(problem.subject_velocity) || needs_conversion(problem.reference_velocity)) {
        Step conversion = transformation_step(
            "physics.relative-motion.convert-si", "Exact SI conversion",
            "Convert both velocity vectors to SI",
            "Apply each declared unit's exact scale to both active components",
            "Reach for this whenever the two velocities are written in different units, or in any "
            "unit that is not metres per second. Subtracting a speed in km/h from one in m/s would "
            "be subtracting two different things, so both go onto the same scale first. The scale "
            "factors are exact, so this loses nothing and nothing is rounded yet.",
            ClaimType::EquivalentExpression,
            verification("unit table", "both velocity scales were applied exactly",
                         EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        conversion.proof_obligations.push_back(
            {"obl.physics.converts-by-table",
             "the SI form comes from the unit table's exact factor for the entered unit"});
        if (!add_transformation(derivation, meter, plan_id, std::move(conversion), equation,
                                "Convert both velocities to m/s", substituted, true)) {
            return RelativeMotionResult();
        }
    }

    Vector relative;
    std::string subtraction_error;
    if (!vector_sub(subject_si, reference_si, &relative, &subtraction_error)) {
        return failed(RelativeMotionOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached, subtraction_error);
    }
    relative.unit.text = si_unit_text(required);
    // True on every reachable path, because the input check above pins both dimensions and
    // vector_sub carries the left one through. Kept so a units-layer fault refuses rather than
    // answering, which is what mutating si_result shows.
    const bool output_dimension_ok = relative.unit.dimension == required;
    const std::string output_dimension = dimension_text(relative.unit.dimension);
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.check-result-dimension",
                   "Relative velocity dimension", "Check the result dimension",
                   "Subtracting two velocities preserves velocity dimension",
                   "obl.relative-motion.result-dimension",
                   "the relative velocity has dimension L T^-1", "dimensional analysis",
                   output_dimension, EvidenceStrength::DimensionallyValid,
                   output_dimension_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "the component result remains a velocity", "L T^-1", output_dimension)) {
        return RelativeMotionResult();
    }
    if (!output_dimension_ok) {
        return failed(RelativeMotionOutcome::VerificationFailed,
                      DerivationStatus::VerificationFailed,
                      "vector subtraction returned dimension " + output_dimension +
                          " instead of L T^-1");
    }

    const Rational subject_components[2] = {subject_si.x, subject_si.y};
    const Rational reference_components[2] = {reference_si.x, reference_si.y};
    const Rational relative_components[2] = {relative.x, relative.y};
    const char *axis_names[2] = {"i", "j"};
    for (size_t axis = 0; axis < 2; ++axis) {
        const NodeId before = arena.binary(
            Kind::Add, rational_node(arena, subject_components[axis]),
            arena.unary(Kind::Neg, rational_node(arena, reference_components[axis])));
        const NodeId after = rational_node(arena, relative_components[axis]);
        if (arena.failed()) {
            return failed(RelativeMotionOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached, status_name(arena.status()));
        }

        VerificationOutcome check_outcome = VerificationOutcome::Passed;
        std::string check_detail;
        bool usable_exact = true;
        bool agrees = true;
        size_t backend_requests = 0;
        if (giac) {
            if (!meter.backend_call())
                return RelativeMotionResult();
            Request request;
            request.op = Op::Simplify;
            request.target = before;
            Adapter adapter(arena, *giac);
            const Response response = adapter.run(request);
            backend_requests = adapter.call_count();
            usable_exact = response.tag == ResultTag::Exact && response.value != kNoNode;
            agrees = false;
            if (usable_exact) {
                const NodeId backend_value = canonicalize(arena, response.value);
                const NodeId local_value = canonicalize(arena, after);
                agrees = !arena.failed() && backend_value == local_value;
            }
            if (arena.failed()) {
                return failed(RelativeMotionOutcome::ResourceExceeded,
                              DerivationStatus::ResourceLimitReached, status_name(arena.status()));
            }
            check_outcome = !usable_exact ? VerificationOutcome::Inconclusive
                            : agrees      ? VerificationOutcome::Passed
                                          : VerificationOutcome::Failed;
            check_detail = !usable_exact
                               ? std::string("Giac returned ") + tag_name(response.tag) +
                                     (response.detail.empty() ? "" : ": " + response.detail)
                               : agrees ? "Giac's exact component matches vector_sub"
                                        : "Giac's exact component differs from vector_sub";
        }

        Step component = transformation_step(
            axis == 0 ? "physics.relative-motion.component-i"
                      : "physics.relative-motion.component-j",
            "Relative velocity component",
            std::string("Subtract the ") + axis_names[axis] + " velocity components",
            "Subtract the reference component from the subject component",
            "Reach for this once both velocities are in the same units and the subtraction is ready "
            "to happen. Vectors subtract one axis at a time, so the i components make the answer's "
            "i component and the j components make its j, with no interaction between the two. "
            "Each of those is ordinary subtraction of numbers, which is the reason for splitting "
            "the velocities into components in the first place.",
            ClaimType::EquivalentExpression,
            verification("nps vector_sub", rational_text(relative_components[axis]),
                         EvidenceStrength::StructurallyValid, VerificationOutcome::Passed),
            backend_requests);
        // The backend's opinion sits beside vector_sub's own record rather than replacing it, which
        // is what work.cc already does. Replacing it left the step with nothing that passed whenever
        // the comparison came back inconclusive, and MVP criterion 4 forbids exactly that.
        if (giac)
            component.verifications.push_back(
                verification("Giac Simplify and local canonical comparison", check_detail,
                             EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
                             check_outcome));
        component.proof_obligations.push_back(
            {axis == 0 ? "obl.relative-motion.component-i" : "obl.relative-motion.component-j",
             std::string("the ") + axis_names[axis] +
                 " component equals subject velocity minus reference velocity"});
        const std::string action = rational_text(subject_components[axis]) + " - " +
                                   rational_text(reference_components[axis]) + " = " +
                                   rational_text(relative_components[axis]);
        if (!add_transformation(derivation, meter, plan_id, std::move(component), before, action,
                                after, true)) {
            return RelativeMotionResult();
        }
        // A backend that would not certify this axis never checked it, so vector_sub's component
        // stands and the remaining axis still gets subtracted. The inconclusive record reaches
        // outcome_from as solved but unchecked.
        if (usable_exact && !agrees) {
            return failed(RelativeMotionOutcome::VerificationFailed,
                          DerivationStatus::VerificationFailed,
                          "Giac and the local exact vector operation disagree, so no relative "
                          "velocity is offered");
        }
    }

    const RelativeDirection direction = direction_of(relative);
    const std::string interpretation = direction_interpretation(problem, direction);
    if (!add_check(derivation, meter, plan_id, "physics.relative-motion.interpret-direction",
                   "Relative direction", "Interpret the component signs",
                   "The declared east-north axes give physical meaning to each exact component sign",
                   "obl.relative-motion.direction-interpreted",
                   "the relative velocity direction follows both component signs",
                   "exact rational sign", interpretation, EvidenceStrength::StructurallyValid,
                   VerificationOutcome::Passed,
                   "the direction is interpreted in the declared axes", "east-north sign mapping",
                   relative_direction_name(direction))) {
        return RelativeMotionResult();
    }

    std::string reported;
    HalfPlace rounding = HalfPlace::Within;
    if (!reported_vector_text(relative, &reported, rounding)) {
        return failed(RelativeMotionOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "reporting the measured precision exceeds exact integer arithmetic");
    }
    Vector exact_display = relative;
    exact_display.precision = Precision();
    std::string exact_text;
    HalfPlace exact_rounding = HalfPlace::Within;
    if (!reported_vector_text(exact_display, &exact_text, exact_rounding)) {
        return failed(RelativeMotionOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "the exact relative velocity cannot be formatted");
    }
    if (rounding == HalfPlace::Outside) {
        return failed(RelativeMotionOutcome::VerificationFailed,
                      DerivationStatus::VerificationFailed,
                      reported + " is further than half a unit in its last place from " +
                          exact_text);
    }
    if (reported != exact_text || rounding == HalfPlace::Unreadable) {
        const bool compared = rounding == HalfPlace::Within;
        Step precision = transformation_step(
            "physics.relative-motion.significant-figures", "Significant figures",
            "Report the relative velocity to the measured precision",
            "Round only after exact conversion, subtraction and verification",
            compared ? "Reach for this once, at the very end, and never partway through. A measured "
                       "value is only as good as the figures it was written with, so the answer is "
                       "reported to the fewest significant figures among the measurements it came "
                       "from. Rounding a component before subtracting would throw away figures the "
                       "final rounding cannot get back, which is why every step above this one "
                       "keeps the exact value."
                     : "The rounded spelling could not be read back as a decimal, so it was never "
                       "compared against the exact value. The exact value is reported instead, "
                       "since showing a rounding nothing checked would be showing an answer with "
                       "no evidence behind it.",
            ClaimType::NoClaim,
            verification("exact comparison against the unrounded value",
                         compared ? reported + " is within half a unit in the last place of " +
                                        exact_text
                                  : reported + " could not be read back as a decimal, so it was "
                                               "never compared against " +
                                        exact_text,
                         EvidenceStrength::CandidateChecked,
                         compared ? VerificationOutcome::Passed
                                  : VerificationOutcome::Inconclusive));
        const std::string action = compared ? "Report " + exact_text + " as " + reported
                                            : "Report " + exact_text + " unrounded";
        if (!compared)
            reported = exact_text;
        const NodeId exact_vector = vector_node(arena, relative);
        if (arena.failed()) {
            return failed(RelativeMotionOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached, status_name(arena.status()));
        }
        if (!add_transformation(derivation, meter, plan_id, std::move(precision), exact_vector,
                                action, exact_vector, false)) {
            return RelativeMotionResult();
        }
    }

    RelativeMotionResult solved;
    solved.outcome = RelativeMotionOutcome::Solved;
    solved.velocity = relative;
    solved.has_value = true;
    solved.direction = direction;
    solved.value_text = reported;
    solved.interpretation = interpretation;
    solved.equation = equation;
    solved.substituted = substituted;
    solved.status = derivation.outcome_from(0);
    return solved;
}

}  // namespace

const char *relative_motion_axes_name(RelativeMotionAxes axes) {
    switch (axes) {
        case RelativeMotionAxes::EastNorth: return "east-north";
    }
    return "invalid";
}

const char *relative_motion_outcome_name(RelativeMotionOutcome outcome) {
    switch (outcome) {
        case RelativeMotionOutcome::Solved: return "solved";
        case RelativeMotionOutcome::InvalidProblem: return "invalid problem";
        case RelativeMotionOutcome::RankMismatch: return "rank mismatch";
        case RelativeMotionOutcome::FrameUndeclared: return "frame undeclared";
        case RelativeMotionOutcome::FrameMismatch: return "frame mismatch";
        case RelativeMotionOutcome::DimensionMismatch: return "dimension mismatch";
        case RelativeMotionOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case RelativeMotionOutcome::VerificationFailed: return "verification failed";
        case RelativeMotionOutcome::Cancelled: return "cancelled";
        case RelativeMotionOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

const char *relative_direction_name(RelativeDirection direction) {
    switch (direction) {
        case RelativeDirection::Stationary: return "stationary";
        case RelativeDirection::East: return "east";
        case RelativeDirection::West: return "west";
        case RelativeDirection::North: return "north";
        case RelativeDirection::South: return "south";
        case RelativeDirection::Northeast: return "northeast";
        case RelativeDirection::Northwest: return "northwest";
        case RelativeDirection::Southeast: return "southeast";
        case RelativeDirection::Southwest: return "southwest";
    }
    return "unknown";
}

RelativeMotionResult solve_relative_motion(Arena &arena, Derivation &derivation,
                                            const RelativeMotionProblem &problem,
                                            const Budget &budget, Backend *giac) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    RelativeMotionResult result = solve_body(arena, derivation, meter, problem, giac, &model);

    const bool arithmetic_overflow = result.outcome == RelativeMotionOutcome::ArithmeticOverflow;
    const bool halted = meter.stopped() || result.outcome == RelativeMotionOutcome::Cancelled ||
                        result.outcome == RelativeMotionOutcome::ResourceExceeded ||
                        arithmetic_overflow;
    // STEP-025: keep the run of records that were checked. A precondition still waiting for its
    // check point leaves the plan unverified, so a halt before that point keeps nothing, which is
    // what actually happened.
    if (halted) {
        const bool cancelled = result.outcome == RelativeMotionOutcome::Cancelled ||
                               meter.halt() == Halt::Cancelled;
        // An overflow is not a halt. The solve did not stop early, it computed a value exact
        // integer arithmetic here cannot hold, so what it recorded is wrong rather than short and
        // STEP-025's prefix does not apply to it.
        bool kept = false;
        if (arithmetic_overflow)
            derivation.rewind_to(mark);
        else
            kept = keep_verified_prefix(derivation, mark, arena);
        RelativeMotionResult stopped;
        stopped.outcome = cancelled          ? RelativeMotionOutcome::Cancelled
                          : arithmetic_overflow ? RelativeMotionOutcome::ArithmeticOverflow
                                                : RelativeMotionOutcome::ResourceExceeded;
        stopped.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        stopped.status = !cancelled ? DerivationStatus::ResourceLimitReached
                         : kept     ? DerivationStatus::Cancelled
                                    : DerivationStatus::NotRecorded;
        stopped.cost = meter.cost();
        const NodeId context_model = model < arena.node_count() ? model : kNoNode;
        record_context(derivation, budget, context_model, stopped.status, problem);
        return stopped;
    }

    if (result.outcome == RelativeMotionOutcome::Solved) {
        result.status = derivation.outcome_from(mark);
    }
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

namespace {

// v(X/Y) read backwards. Reversing a pair of subscripts negates the vector, which is the move that
// turns the sum in the identity into the subtraction the component solver already performs.
bool reversed_subscripts(const Vector &source, Vector *out) {
    *out = source;
    const Rational components[3] = {source.x, source.y, source.z};
    Rational *targets[3] = {&out->x, &out->y, &out->z};
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!negate_fraction(components[axis].num, components[axis].den, &targets[axis]->num,
                             &targets[axis]->den))
            return false;
    }
    return true;
}

bool absolute_value(const Rational &source, Rational *value) {
    *value = source;
    if (source.num >= 0)
        return true;
    return negate_fraction(source.num, source.den, &value->num, &value->den);
}

// The cardinal nearest a velocity is the axis carrying the larger component, and the angle a
// bearing quotes is the one turned from that axis toward the other. A vector exactly on a diagonal
// is the same distance from two cardinals, so the east-west axis is kept and the choice stays
// defined rather than following whichever comparison rounded first.
bool cardinal_reference(const Vector &velocity, RelativeDirection *reference,
                        RelativeDirection *sense, Rational *along, Rational *across) {
    if (velocity.x.num == 0 && velocity.y.num == 0)
        return false;
    Rational east_west, north_south;
    if (!absolute_value(velocity.x, &east_west) || !absolute_value(velocity.y, &north_south))
        return false;
    Rational difference;
    if (!sub_fraction(east_west.num, east_west.den, north_south.num, north_south.den,
                      &difference.num, &difference.den))
        return false;
    if (difference.num >= 0) {
        *reference = velocity.x.num > 0 ? RelativeDirection::East : RelativeDirection::West;
        *sense = velocity.y.num > 0   ? RelativeDirection::North
                 : velocity.y.num < 0 ? RelativeDirection::South
                                      : RelativeDirection::Stationary;
        *along = east_west;
        *across = north_south;
        return true;
    }
    *reference = velocity.y.num > 0 ? RelativeDirection::North : RelativeDirection::South;
    *sense = velocity.x.num > 0   ? RelativeDirection::East
             : velocity.x.num < 0 ? RelativeDirection::West
                                  : RelativeDirection::Stationary;
    *along = north_south;
    *across = east_west;
    return true;
}

NodeId relative_symbol(Arena &arena, const std::string &subject, const std::string &reference) {
    return arena.call("relative_velocity", {arena.symbol(subject), arena.symbol(reference)});
}

std::string pair_text(const std::string &subject, const std::string &reference) {
    return "v(" + subject + "/" + reference + ")";
}

}  // namespace

const char *relative_motion_unknown_name(RelativeMotionUnknown unknown) {
    switch (unknown) {
        case RelativeMotionUnknown::SubjectRelativeToReference:
            return "subject relative to reference";
        case RelativeMotionUnknown::SubjectRelativeToMedium: return "subject relative to medium";
        case RelativeMotionUnknown::MediumRelativeToReference:
            return "medium relative to reference";
    }
    return "unknown";
}

RelativeBearing relative_motion_bearing(Arena &arena, Derivation &derivation,
                                        const Vector &velocity, AngleUnit angle_unit, Backend &giac,
                                        RelativeMotionAxes axes, const Budget &budget) {
    RelativeBearing bearing;
    bearing.angle_unit = angle_unit;
    if (!valid_axes(axes))
        return bearing;
    Rational along, across;
    if (!cardinal_reference(velocity, &bearing.reference, &bearing.sense, &along, &across))
        return bearing;

    const std::string turning =
        bearing.sense == RelativeDirection::Stationary
            ? std::string("the velocity lies on ") + relative_direction_name(bearing.reference) +
                  ", so the angle from that cardinal is zero"
            : std::string("the angle is measured from ") +
                  relative_direction_name(bearing.reference) + " turning toward " +
                  relative_direction_name(bearing.sense);
    bearing.convention = turning + ", in the declared " + relative_motion_axes_name(axes) + " axes";

    Meter meter(budget);
    if (!add_check(derivation, meter, kNoStep, "physics.relative-motion.bearing-convention",
                   "Bearing convention", "Name the cardinal the reported angle is measured from",
                   "The nearest cardinal is the axis with the larger component, and the angle turns "
                   "from it toward the other axis",
                   "obl.relative-motion.bearing-convention-stated",
                   "the reported angle names both the cardinal it starts from and the one it turns "
                   "toward",
                   "nearest cardinal by component magnitude", bearing.convention,
                   EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
                   "the bearing is quoted from a named cardinal",
                   "an angle of at most forty five degrees from the nearest cardinal",
                   bearing.convention)) {
        return bearing;
    }

    // Measuring from the cardinal rather than from the positive x axis is a reflection of the
    // components onto the first octant, so the existing converter returns the offset angle itself
    // and no second way to compute a magnitude or an angle appears here.
    Vector folded = velocity;
    folded.rank = 2;
    folded.x = along;
    folded.y = across;
    folded.z = Rational();
    const VectorComponentsResult polar =
        components_to_magnitude_angle(arena, derivation, folded, angle_unit, giac, budget);
    bearing.cost = polar.cost;
    bearing.cost.steps += meter.cost().steps;
    bearing.cost.rewrites += meter.cost().rewrites;
    if (polar.outcome != VectorComponentsOutcome::Solved || !polar.has_polar)
        return bearing;

    bearing.has_bearing = true;
    bearing.magnitude = polar.polar.magnitude;
    bearing.angle = polar.polar.angle;
    bearing.angle_unit = polar.polar.angle_unit;
    const std::string unit_text =
        velocity.unit.text.empty() ? std::string() : " " + velocity.unit.text;
    if (bearing.sense == RelativeDirection::Stationary) {
        bearing.text = print(arena, bearing.magnitude) + unit_text + " due " +
                       relative_direction_name(bearing.reference);
        return bearing;
    }
    bearing.text = print(arena, bearing.magnitude) + unit_text + " at " +
                   print(arena, bearing.angle) +
                   (bearing.angle_unit == AngleUnit::Degrees ? " degrees " : " radians ") +
                   relative_direction_name(bearing.sense) + " of " +
                   relative_direction_name(bearing.reference);
    return bearing;
}

RelativeMotionResult solve_relative_motion_identity(Arena &arena, Derivation &derivation,
                                                    const RelativeMotionIdentity &problem,
                                                    const Budget &budget, Backend *giac) {
    if (problem.subject_name.empty() || problem.medium_name.empty() ||
        problem.reference_name.empty() || problem.subject_name == problem.medium_name ||
        problem.medium_name == problem.reference_name ||
        problem.subject_name == problem.reference_name) {
        return failed(RelativeMotionOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the three frames must have distinct nonempty names");
    }

    const std::string &a = problem.subject_name;
    const std::string &b = problem.medium_name;
    const std::string &c = problem.reference_name;

    RelativeMotionProblem inner;
    inner.axes = problem.axes;
    std::string rearrangement;
    // The RHS of the isolated identity, named explicitly per branch rather than derived from
    // inner.subject_name/reference_name, because the frame that plays "subject" in the inner
    // two-frame problem is not always the frame ("a") that anchors both RHS terms in the outer
    // three-frame identity.
    std::string add_subject, add_reference, sub_subject, sub_reference;
    switch (problem.unknown) {
        case RelativeMotionUnknown::SubjectRelativeToReference:
            inner.subject_name = a;
            inner.reference_name = c;
            inner.subject_velocity = problem.subject_relative_to_medium;
            if (!reversed_subscripts(problem.medium_relative_to_reference,
                                     &inner.reference_velocity)) {
                return failed(RelativeMotionOutcome::ArithmeticOverflow,
                              DerivationStatus::ResourceLimitReached,
                              "reversing the subscripts overflows exact integer arithmetic");
            }
            add_subject = a;
            add_reference = b;
            sub_subject = c;
            sub_reference = b;
            rearrangement = pair_text(a, c) + " = " + pair_text(a, b) + " - " + pair_text(c, b);
            break;
        case RelativeMotionUnknown::SubjectRelativeToMedium:
            inner.subject_name = a;
            inner.reference_name = b;
            inner.subject_velocity = problem.subject_relative_to_reference;
            inner.reference_velocity = problem.medium_relative_to_reference;
            add_subject = a;
            add_reference = c;
            sub_subject = b;
            sub_reference = c;
            rearrangement = pair_text(a, b) + " = " + pair_text(a, c) + " - " + pair_text(b, c);
            break;
        case RelativeMotionUnknown::MediumRelativeToReference:
            inner.subject_name = b;
            inner.reference_name = c;
            inner.subject_velocity = problem.subject_relative_to_reference;
            inner.reference_velocity = problem.subject_relative_to_medium;
            add_subject = a;
            add_reference = c;
            sub_subject = a;
            sub_reference = b;
            rearrangement = pair_text(b, c) + " = " + pair_text(a, c) + " - " + pair_text(a, b);
            break;
    }

    Meter meter(budget);
    const size_t mark = derivation.mark();

    const std::string identity_text =
        pair_text(a, c) + " = " + pair_text(a, b) + " + " + pair_text(b, c);
    if (!add_check(derivation, meter, kNoStep, "physics.relative-motion.subscript-cancellation",
                   "Subscript cancellation", "Check that the inner frames cancel",
                   "The medium appears as the second subscript of one velocity and the first of "
                   "the next, so the chain closes on the outer pair",
                   "obl.relative-motion.subscript-cancellation",
                   "the inner subscript " + b + " cancels between the two added velocities",
                   "subscript chain", identity_text, EvidenceStrength::StructurallyValid,
                   VerificationOutcome::Passed, "the three frames chain into one identity",
                   "the inner frame appears once on each side of the addition", identity_text)) {
        return RelativeMotionResult();
    }

    const NodeId identity =
        arena.binary(Kind::Equals, relative_symbol(arena, a, c),
                     arena.binary(Kind::Add, relative_symbol(arena, a, b),
                                  relative_symbol(arena, b, c)));
    const NodeId isolated = arena.binary(
        Kind::Equals, relative_symbol(arena, inner.subject_name, inner.reference_name),
        arena.binary(Kind::Add, relative_symbol(arena, add_subject, add_reference),
                     arena.unary(Kind::Neg,
                                 relative_symbol(arena, sub_subject, sub_reference))));
    if (arena.failed()) {
        return failed(RelativeMotionOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }

    Step isolate = transformation_step(
        "physics.relative-motion.isolate-unknown", "Isolate the unknown velocity",
        "Rearrange the identity for " + pair_text(inner.subject_name, inner.reference_name),
        "Move the known velocities to one side before any number is substituted",
        "Reach for this whenever the velocity you want is not the one the identity already has on "
        "its left. Which of the three is unknown is a property of the problem rather than of the "
        "relation, so the relation is rearranged symbolically first and the numbers go in "
        "afterwards. Reversing a pair of subscripts negates that velocity, which is how an "
        "addition on one side becomes a subtraction on the other.",
        ClaimType::EquivalentExpression,
        verification("symbolic rearrangement of the subscript identity",
                     identity_text + " rearranged to " + rearrangement,
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    isolate.proof_obligations.push_back(
        {"obl.relative-motion.isolate-before-substitute",
         "the unknown is isolated symbolically before a number is substituted"});
    if (!add_transformation(derivation, meter, kNoStep, std::move(isolate), identity,
                            "Rearrange to " + rearrangement, isolated, true)) {
        return RelativeMotionResult();
    }

    RelativeMotionResult result = solve_relative_motion(arena, derivation, inner, budget, giac);
    if (result.outcome == RelativeMotionOutcome::Solved) {
        result.interpretation += ", solving the identity " + identity_text + " for its " +
                                 relative_motion_unknown_name(problem.unknown) + " term";
        if (giac != nullptr) {
            result.bearing = relative_motion_bearing(arena, derivation, result.velocity,
                                                     AngleUnit::Degrees, *giac, problem.axes,
                                                     budget);
            result.cost.steps += result.bearing.cost.steps;
            result.cost.rewrites += result.bearing.cost.rewrites;
            result.cost.backend_calls += result.bearing.cost.backend_calls;
            if (result.bearing.has_bearing) {
                result.interpretation +=
                    ", reported as " + result.bearing.text + ", where " + result.bearing.convention;
            }
        }
        result.status = derivation.outcome_from(mark);
    }
    result.cost.steps += meter.cost().steps;
    result.cost.rewrites += meter.cost().rewrites;
    return result;
}

}  // namespace nps
