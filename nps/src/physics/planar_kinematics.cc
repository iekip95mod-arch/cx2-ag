#include "nps/physics/planar_kinematics.h"

#include <utility>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "nps/physics/kinematics.h"
#include "measurement_support.h"

namespace nps {
namespace {

using measure::dimension_node;

Dimension velocity_dimension() {
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

Dimension time_dimension() {
    Dimension dimension;
    dimension.time = 1;
    return dimension;
}

Dimension length_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

PlanarKinematicsResult failed(PlanarKinematicsOutcome outcome, DerivationStatus status,
                              const std::string &detail) {
    PlanarKinematicsResult failure;
    failure.outcome = outcome;
    failure.status = status;
    failure.detail = detail;
    return failure;
}

bool valid_axes(PlanarAxes axes) {
    switch (axes) {
        case PlanarAxes::RightUp: return true;
    }
    return false;
}

using measure::normalize_copy;
using measure::normalized_rational_node;
using measure::verification;
using measure::add_check;
using measure::add_transformation;
using measure::transformation_step;

bool valid_vector(const Vector &vector, std::string *detail) {
    const Rational components[3] = {vector.x, vector.y, vector.z};
    const char *names[3] = {"x", "y", "z"};
    const size_t active = vector.rank == 3 ? 3 : 2;
    Rational normalized_value;
    for (size_t axis = 0; axis < active; ++axis) {
        if (!normalize_copy(components[axis], &normalized_value)) {
            *detail = std::string("the ") + names[axis] + " component has an invalid exact value";
            return false;
        }
    }
    if (!normalize_copy(vector.unit.scale, &normalized_value) || normalized_value.num <= 0) {
        *detail = "the unit has an invalid SI conversion scale";
        return false;
    }
    if (vector.precision.kind == NumberKind::Exact && vector.precision.significant_digits != 0) {
        *detail = "an exact vector cannot carry a measured significant-figure count";
        return false;
    }
    if (vector.precision.kind == NumberKind::Measured &&
        (vector.precision.significant_digits == 0 || vector.precision.significant_digits > 18)) {
        *detail = "a measured vector needs between 1 and 18 significant figures";
        return false;
    }
    return true;
}

NodeId vector_node(Arena &arena, const Vector &vector) {
    std::vector<NodeId> components;
    components.push_back(normalized_rational_node(arena, vector.x));
    components.push_back(normalized_rational_node(arena, vector.y));
    if (vector.rank == 3)
        components.push_back(normalized_rational_node(arena, vector.z));
    return arena.call("vector", components);
}

NodeId vector_model_node(Arena &arena, const Vector &vector, MotionStage stage) {
    return arena.call("staged_vector",
                      {vector_node(arena, vector), normalized_rational_node(arena, vector.unit.scale),
                       arena.symbol(vector.frame.name), dimension_node(arena, vector.unit.dimension),
                       arena.integer(integer_text(vector.rank)),
                       arena.symbol(motion_stage_name(stage))});
}

NodeId time_model_node(Arena &arena, const Quantity &time, MotionStage stage) {
    return arena.call("staged_scalar",
                      {normalized_rational_node(arena, time.value), normalized_rational_node(arena, time.unit.scale),
                       dimension_node(arena, time.unit.dimension),
                       arena.symbol(motion_stage_name(stage))});
}

NodeId problem_model(Arena &arena, const PlanarKinematicsProblem &problem) {
    return arena.call(
        "planar_kinematics_problem",
        {arena.symbol(problem.body_name),
         vector_model_node(arena, problem.initial_velocity, problem.initial_velocity_stage),
         vector_model_node(arena, problem.acceleration, problem.acceleration_stage),
         time_model_node(arena, problem.elapsed_time, problem.elapsed_time_stage),
         arena.symbol(planar_axes_name(problem.axes)),
         arena.symbol(problem.projectile ? "projectile" : "general")});
}

NodeId displacement_equation(Arena &arena, NodeId velocity, NodeId acceleration, NodeId time) {
    const NodeId half = arena.binary(Kind::Mul, arena.integer("1"),
                                     arena.binary(Kind::Pow, arena.integer("2"),
                                                  arena.integer("-1")));
    const NodeId drift = arena.binary(Kind::Mul, velocity, time);
    const NodeId curve = arena.binary(
        Kind::Mul, arena.binary(Kind::Mul, half, acceleration),
        arena.binary(Kind::Pow, time, arena.integer("2")));
    return arena.binary(Kind::Equals, arena.symbol("displacement"),
                        arena.binary(Kind::Add, drift, curve));
}

NodeId displacement_equation(Arena &arena, const Vector &velocity, const Vector &acceleration,
                             const Quantity &time) {
    return displacement_equation(arena, vector_node(arena, velocity),
                                 vector_node(arena, acceleration),
                                 normalized_rational_node(arena, time.value));
}

NodeId symbolic_displacement_equation(Arena &arena) {
    return displacement_equation(arena, arena.symbol("v0"), arena.symbol("a"), arena.symbol("t"));
}

std::string describe_motion(const PlanarKinematicsProblem &problem, const Vector &displacement,
                            const Vector &final_velocity) {
    const char *horizontal = displacement.x.num > 0   ? "right"
                             : displacement.x.num < 0 ? "left"
                                                      : "neither left nor right";
    const char *vertical = displacement.y.num > 0   ? "up"
                           : displacement.y.num < 0 ? "down"
                                                    : "neither up nor down";
    return problem.body_name + " moves " + horizontal + " and " + vertical +
           " over the interval in the declared right-up axes, reaching " +
           vector_text(final_velocity) + " at the final event";
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const PlanarKinematicsProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id =
        problem.projectile ? "physics.kinematics.constant-acceleration.projectile.two-dimension"
                           : "physics.kinematics.constant-acceleration.two-dimension";
    inputs.requested_method =
        "validate the declared axes, stages, frames and dimensions, convert to SI, decompose each "
        "axis under the shared time, verify against the average-velocity identity, and interpret";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    if (model != kNoNode) {
        inputs.active_assumptions.push_back("velocity and acceleration use frame " +
                                            problem.initial_velocity.frame.name);
        if (valid_axes(problem.axes))
            inputs.active_assumptions.push_back("positive i is right and positive j is up");
        inputs.active_assumptions.push_back("the acceleration is constant over the whole interval");
        inputs.active_assumptions.push_back("one shared time links both axes");
        if (problem.projectile)
            inputs.active_assumptions.push_back(
                "the projectile specialization leaves the horizontal axis unaccelerated");
    }
    inputs.unit_policy = "exact SI conversion and per-axis exact arithmetic, final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

Quantity si_scalar(const Rational &value, const Dimension &dimension, const Precision &precision) {
    Quantity quantity;
    quantity.value = value;
    quantity.unit.dimension = dimension;
    quantity.unit.scale = Rational{1, 1};
    quantity.unit.text = si_unit_text(dimension);
    quantity.precision = precision;
    return quantity;
}

PlanarKinematicsResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                                  const PlanarKinematicsProblem &problem, Backend *giac,
                                  NodeId *model) {
    if (!valid_axes(problem.axes)) {
        return failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the coordinate convention is invalid");
    }
    if (problem.body_name.empty()) {
        return failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the moving body needs a nonempty name");
    }
    size_t remaining_identity_bytes = arena.limits().max_input_bytes;
    const std::string *identity_fields[3] = {&problem.body_name,
                                             &problem.initial_velocity.frame.name,
                                             &problem.acceleration.frame.name};
    for (const std::string *field : identity_fields) {
        if (field->size() > remaining_identity_bytes) {
            return failed(PlanarKinematicsOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached,
                          "body and frame identifiers exceed the input byte limit");
        }
        remaining_identity_bytes -= field->size();
    }
    std::string invalid_detail;
    if (!valid_vector(problem.initial_velocity, &invalid_detail)) {
        return failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "initial velocity: " + invalid_detail);
    }
    if (!valid_vector(problem.acceleration, &invalid_detail)) {
        return failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "acceleration: " + invalid_detail);
    }
    // Two separate locals, as catch_up.cc keeps them: one shared destination would let the unit
    // scale overwrite the value and leave a negative interval unjudged.
    Rational normalized_time;
    Rational normalized_time_scale;
    if (!normalize_copy(problem.elapsed_time.value, &normalized_time) || normalized_time.num <= 0 ||
        !normalize_copy(problem.elapsed_time.unit.scale, &normalized_time_scale) ||
        normalized_time_scale.num <= 0) {
        return failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the elapsed time has an invalid exact value or unit scale");
    }

    *model = problem_model(arena, problem);
    if (arena.failed()) {
        return failed(PlanarKinematicsOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }

    PlanPayload plan;
    plan.strategy_id = problem.projectile ? "physics.planar-kinematics.projectile-plan"
                                          : "physics.planar-kinematics.plan";
    plan.selected_strategy = "Per-axis constant acceleration under one shared time";
    plan.matched_problem_facts.push_back("body: " + problem.body_name);
    plan.matched_problem_facts.push_back(problem.projectile ? "projectile specialization"
                                                            : "general planar acceleration");
    plan.alternatives_considered.push_back(
        "the one-dimensional family, which cannot carry a second axis");
    plan.alternatives_considered.push_back(
        "relative motion components, which subtract velocities rather than integrating one");
    plan.selection_rationale =
        giac ? "the existing exact vector operations decompose each axis and Giac checks the shared-"
               "time identity"
             : "the existing exact vector operations decompose each axis and check the shared-time "
               "identity locally";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find the displacement and final velocity over the interval";
    // The projectile specialization registers a precondition the general plan has no shape for, and
    // a rule schema is one fixed obligation set, so the two plans are two rules. apex-plan below is
    // the same distinction already drawn once in this file.
    plan_step.rule_id = problem.projectile ? "physics.planar-kinematics.projectile-plan"
                                           : "physics.planar-kinematics.plan";
    plan_step.rule_name = problem.projectile ? "Projectile constant-acceleration plan"
                                             : "Planar constant-acceleration plan";
    plan_step.explanation_short =
        "Check axes, stages, frames and dimensions before decomposing the motion axis by axis";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.rank-two",
                                   "velocity and acceleration are two-dimensional",
                                   "rank comparison", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before decomposing the axes");
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.frames-declared",
                                   "both vectors declare named frames", "frame declaration",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before comparing frame identity");
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.frames-match",
                                   "both vectors use the same frame", "frame identity",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before combining components");
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.axes",
                                   "positive i is right and positive j is up",
                                   "coordinate convention validation",
                                   EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
                                   "the supplied coordinate convention is registered and valid");
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.stages",
                                   "each quantity is used at the stage it belongs to",
                                   "stage identity", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before any quantity is substituted");
    register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.dimensions",
                                   "velocity, acceleration and time carry their own dimensions",
                                   "dimensional analysis", EvidenceStrength::DimensionallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before integrating the acceleration");
    if (problem.projectile) {
        register_strategy_precondition(plan, plan_step, "pre.planar-kinematics.projectile",
                                       "the horizontal axis is unaccelerated and gravity acts down",
                                       "projectile specialization",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::NotAttempted,
                                       "checked before the projectile reading is offered");
    }
    if (!meter.step())
        return PlanarKinematicsResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool rank_ok = problem.initial_velocity.rank == 2 && problem.acceleration.rank == 2;
    const std::string ranks = "ranks " + std::to_string(problem.initial_velocity.rank) + " and " +
                              std::to_string(problem.acceleration.rank);
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-rank",
                   "Planar vector rank", "Check the velocity and acceleration ranks",
                   "This family resolves motion in one Cartesian plane",
                   "obl.planar-kinematics.rank-two", "both vectors have rank two",
                   "rank comparison", ranks, EvidenceStrength::StructurallyValid,
                   rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "velocity and acceleration are two-dimensional", "rank 2 and rank 2", ranks)) {
        return PlanarKinematicsResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.planar-kinematics.rank-two",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, ranks);
    if (!rank_ok) {
        return failed(PlanarKinematicsOutcome::RankMismatch, DerivationStatus::InvalidInput,
                      "planar kinematics requires a two-component velocity and acceleration");
    }

    const bool frames_declared = !problem.initial_velocity.frame.name.empty() &&
                                 !problem.acceleration.frame.name.empty();
    const std::string frames = "frames '" + problem.initial_velocity.frame.name + "' and '" +
                               problem.acceleration.frame.name + "'";
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-frame-declared",
                   "Declared frames", "Check that both vectors declare a frame",
                   "A component vector needs a named Cartesian frame",
                   "obl.planar-kinematics.frames-declared",
                   "both vectors declare a nonempty frame", "frame declaration", frames,
                   EvidenceStrength::StructurallyValid,
                   frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "velocity and acceleration name their frame", "two nonempty frame names",
                   frames)) {
        return PlanarKinematicsResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.planar-kinematics.frames-declared",
        frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_declared) {
        return failed(PlanarKinematicsOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                      "both the velocity and the acceleration must declare a named frame");
    }

    const bool frames_match = problem.initial_velocity.frame == problem.acceleration.frame;
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-frame-match",
                   "Matching frames", "Check that both frames match",
                   "Combining components across frames needs an explicit basis transformation",
                   "obl.planar-kinematics.frames-match", "both vectors use the same frame",
                   "frame identity", frames, EvidenceStrength::StructurallyValid,
                   frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "velocity and acceleration share one frame", "identical frame names", frames)) {
        return PlanarKinematicsResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.planar-kinematics.frames-match",
        frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_match) {
        return failed(PlanarKinematicsOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                      "cannot combine frame " + problem.initial_velocity.frame.name +
                          " with frame " + problem.acceleration.frame.name +
                          " without an explicit basis transformation");
    }

    // PHYS-028. The initial velocity is a state at an event, the acceleration and the elapsed time
    // belong to the interval between the events, and reading one as the other is the substitution
    // this check exists to refuse.
    const bool stages_ok = problem.initial_velocity_stage == MotionStage::State &&
                           problem.acceleration_stage == MotionStage::Interval &&
                           problem.elapsed_time_stage == MotionStage::Interval;
    const std::string stages =
        std::string("velocity ") + motion_stage_name(problem.initial_velocity_stage) +
        ", acceleration " + motion_stage_name(problem.acceleration_stage) + ", time " +
        motion_stage_name(problem.elapsed_time_stage);
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-stages",
                   "Event, state and interval identity", "Check each quantity's stage",
                   "A state at one event cannot stand in for a quantity spanning the interval",
                   "obl.planar-kinematics.stage-identity",
                   "the velocity is a state and the acceleration and time span the interval",
                   "stage identity", stages, EvidenceStrength::StructurallyValid,
                   stages_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "no quantity is substituted across stages",
                   "velocity state, acceleration interval, time interval", stages)) {
        return PlanarKinematicsResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.planar-kinematics.stages",
        stages_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, stages);
    if (!stages_ok) {
        return failed(PlanarKinematicsOutcome::StageMismatch, DerivationStatus::InvalidInput,
                      "event, state and interval quantities are distinct, but received " + stages);
    }

    const bool dimensions_ok =
        problem.initial_velocity.unit.dimension == velocity_dimension() &&
        problem.acceleration.unit.dimension == acceleration_dimension() &&
        problem.elapsed_time.unit.dimension == time_dimension();
    const std::string dimensions =
        "velocity " + dimension_text(problem.initial_velocity.unit.dimension) + ", acceleration " +
        dimension_text(problem.acceleration.unit.dimension) + ", time " +
        dimension_text(problem.elapsed_time.unit.dimension);
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-input-dimensions",
                   "Input dimensions", "Check the three physical quantity roles",
                   "Integrating a constant acceleration needs L T^-1, L T^-2 and T",
                   "obl.planar-kinematics.input-dimensions",
                   "the inputs are a velocity, an acceleration and a time", "dimensional analysis",
                   dimensions, EvidenceStrength::DimensionallyValid,
                   dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "each input carries its own dimension", "L T^-1, L T^-2 and T", dimensions)) {
        return PlanarKinematicsResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.planar-kinematics.dimensions",
        dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, dimensions);
    if (!dimensions_ok) {
        return failed(PlanarKinematicsOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "planar kinematics requires L T^-1, L T^-2 and T, but received " +
                          dimensions);
    }

    if (problem.projectile) {
        const bool projectile_ok = problem.acceleration.x.num == 0 && problem.acceleration.y.num < 0;
        const std::string observed = "acceleration " + vector_text(problem.acceleration);
        if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-projectile",
                       "Projectile specialization", "Check the projectile acceleration",
                       "A projectile has no horizontal acceleration and falls under gravity",
                       "obl.planar-kinematics.projectile",
                       "the horizontal component is zero and the vertical one points down",
                       "projectile specialization", observed, EvidenceStrength::StructurallyValid,
                       projectile_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                       "the acceleration matches free fall in the declared axes",
                       "zero i component and a negative j component", observed)) {
            return PlanarKinematicsResult();
        }
        derivation.complete_plan_precondition(
            plan_id, "pre.planar-kinematics.projectile",
            projectile_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, observed);
        if (!projectile_ok) {
            return failed(PlanarKinematicsOutcome::NotProjectile, DerivationStatus::InvalidInput,
                          "the projectile specialization needs a zero horizontal acceleration and "
                          "a downward vertical one, but received " + observed);
        }
    }

    const NodeId symbolic = symbolic_displacement_equation(arena);
    const NodeId equation = displacement_equation(arena, problem.initial_velocity,
                                                  problem.acceleration, problem.elapsed_time);
    if (arena.failed()) {
        return failed(PlanarKinematicsOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    Step law = transformation_step(
        "physics.planar-kinematics.definition", "Constant-acceleration displacement",
        "Apply the constant-acceleration relations",
        "Use s = v0 t + a t^2 / 2 and v = v0 + a t on each axis under one shared time",
        "Reach for this whenever something moves in a plane with an acceleration that does not "
        "change: a thrown ball, a puck pushed across a table, a boat under a steady current. The "
        "two axes do not interact, so each one is the ordinary one-dimensional motion you already "
        "know. What ties them together is the clock: the same elapsed time appears in both, which "
        "is why a horizontal distance can tell you how long the fall lasted.",
        ClaimType::Definition,
        verification("checked physical-law applicability",
                     "the axes, stages, ranks, frames and dimensions passed",
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    law.proof_obligations.push_back(
        {"obl.planar-kinematics.definition-after-checks",
         "the constant-acceleration relations are applied only after their conditions pass"});
    if (!add_transformation(derivation, meter, plan_id, std::move(law), *model,
                            "Apply s = v0 t + a t^2 / 2 and v = v0 + a t on both axes", symbolic,
                            false)) {
        return PlanarKinematicsResult();
    }

    const std::string substitution = "v0 = " + vector_text(problem.initial_velocity) + ", a = " +
                                     vector_text(problem.acceleration) + ", t = " +
                                     rational_text(problem.elapsed_time.value) + " " +
                                     problem.elapsed_time.unit.text;
    Step substitute = transformation_step(
        "physics.planar-kinematics.substitute", "Substitution",
        "Put the declared velocity, acceleration and time into the law",
        "Replace each symbol by the quantity the problem gave for it",
        "Reach for this once the law is on the page and its conditions have passed. Writing the law "
        "first and the numbers second is what makes the walkthrough readable: the relation is the "
        "part that is true of every problem of this shape, and the numbers are the part that is "
        "true of this one. The units come off the numbers on the next line, not this one.",
        ClaimType::SolutionSetPreserved,
        verification("typed known-quantity lookup", substitution, EvidenceStrength::StructurallyValid,
                     VerificationOutcome::Passed));
    substitute.proof_obligations.push_back(
        {"obl.physics.lookup-preserves-solutions",
         "the value put in place of a symbol is the one the problem declared for it"});
    if (!add_transformation(derivation, meter, plan_id, std::move(substitute), symbolic,
                            "Substitute " + substitution, equation, true)) {
        return PlanarKinematicsResult();
    }

    Vector velocity_si;
    Vector acceleration_si;
    Rational time_si;
    if (!to_si(problem.initial_velocity, &velocity_si) ||
        !to_si(problem.acceleration, &acceleration_si) || !to_si(problem.elapsed_time, &time_si)) {
        return failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "a component does not fit exact arithmetic after SI conversion");
    }
    const Quantity time = si_scalar(time_si, time_dimension(), problem.elapsed_time.precision);
    const NodeId substituted = displacement_equation(arena, velocity_si, acceleration_si, time);
    if (arena.failed()) {
        return failed(PlanarKinematicsOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    Step conversion = transformation_step(
        "physics.planar-kinematics.convert-si", "Exact SI conversion",
        "Convert the velocity, acceleration and time to SI",
        "Apply each declared unit's exact scale before any axis is integrated",
        "Reach for this whenever the inputs are written in different units, or in any unit that is "
        "not the SI one. Multiplying a speed in km/h by a time in minutes would produce a number "
        "belonging to neither, so everything goes onto the same scale first. The scale factors are "
        "exact, so this loses nothing and nothing is rounded yet.",
        ClaimType::EquivalentExpression,
        verification("unit table", "every declared scale was applied exactly",
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    conversion.proof_obligations.push_back(
        {"obl.physics.converts-by-table",
         "the SI form comes from the unit table's exact factor for the entered unit"});
    if (!add_transformation(derivation, meter, plan_id, std::move(conversion), equation,
                            "Convert every input to its SI unit", substituted, true)) {
        return PlanarKinematicsResult();
    }

    // Reuse the vector layer rather than multiplying components here: PHYS-019 asks for the
    // existing component machinery and vector_scale already carries frame, rank and dimension.
    Rational time_squared;
    Rational half_time_squared;
    if (!rational_mul(time_si, time_si, &time_squared) ||
        !rational_mul(time_squared, Rational{1, 2}, &half_time_squared)) {
        return failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "the squared elapsed time does not fit exact arithmetic");
    }
    Vector drift;
    Vector curve;
    Vector displacement;
    Vector velocity_change;
    Vector final_velocity;
    std::string arithmetic_error;
    const Quantity half_square =
        si_scalar(half_time_squared, Dimension{0, 0, 2}, problem.elapsed_time.precision);
    if (!vector_scale(velocity_si, time, &drift, &arithmetic_error) ||
        !vector_scale(acceleration_si, half_square, &curve, &arithmetic_error) ||
        !vector_add(drift, curve, &displacement, &arithmetic_error) ||
        !vector_scale(acceleration_si, time, &velocity_change, &arithmetic_error) ||
        !vector_add(velocity_si, velocity_change, &final_velocity, &arithmetic_error)) {
        return failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached, arithmetic_error);
    }
    displacement.unit.text = si_unit_text(length_dimension());
    final_velocity.unit.text = si_unit_text(velocity_dimension());

    const bool result_dimensions_ok = displacement.unit.dimension == length_dimension() &&
                                      final_velocity.unit.dimension == velocity_dimension();
    const std::string result_dimensions = "displacement " +
                                          dimension_text(displacement.unit.dimension) +
                                          ", final velocity " +
                                          dimension_text(final_velocity.unit.dimension);
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-result-dimension",
                   "Result dimensions", "Check the produced dimensions",
                   "Integrating an acceleration once gives a velocity and twice gives a length",
                   "obl.planar-kinematics.result-dimensions",
                   "the displacement is a length and the final velocity is a velocity",
                   "dimensional analysis", result_dimensions, EvidenceStrength::DimensionallyValid,
                   result_dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "the produced quantities keep their own dimensions", "L and L T^-1",
                   result_dimensions)) {
        return PlanarKinematicsResult();
    }
    if (!result_dimensions_ok) {
        return failed(PlanarKinematicsOutcome::VerificationFailed,
                      DerivationStatus::VerificationFailed,
                      "the vector layer returned " + result_dimensions + " instead of L and L T^-1");
    }

    const Rational velocity_components[2] = {velocity_si.x, velocity_si.y};
    const Rational acceleration_components[2] = {acceleration_si.x, acceleration_si.y};
    const Rational displacement_components[2] = {displacement.x, displacement.y};
    const Rational final_components[2] = {final_velocity.x, final_velocity.y};
    const char *axis_names[2] = {"i", "j"};
    for (size_t axis = 0; axis < 2; ++axis) {
        const NodeId before = arena.binary(
            Kind::Add,
            arena.binary(Kind::Mul, normalized_rational_node(arena, velocity_components[axis]),
                         normalized_rational_node(arena, time_si)),
            arena.binary(Kind::Mul, normalized_rational_node(arena, acceleration_components[axis]),
                         normalized_rational_node(arena, half_time_squared)));
        const NodeId after = normalized_rational_node(arena, displacement_components[axis]);
        if (arena.failed()) {
            return failed(PlanarKinematicsOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached, status_name(arena.status()));
        }
        Step component = transformation_step(
            axis == 0 ? "physics.planar-kinematics.component-i"
                      : "physics.planar-kinematics.component-j",
            "Planar displacement component",
            std::string("Integrate the ") + axis_names[axis] + " axis over the shared time",
            "Combine the initial velocity term and the acceleration term on this axis",
            "Reach for this once the units agree and the clock is fixed. Each axis is integrated on "
            "its own, because a component of the acceleration only ever changes the matching "
            "component of the velocity. The shared elapsed time is the single thing both axes have "
            "in common, and it is what lets one axis answer a question asked about the other.",
            ClaimType::EquivalentExpression,
            verification("nps vector_scale and vector_add",
                         rational_text(displacement_components[axis]),
                         EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        component.proof_obligations.push_back(
            {axis == 0 ? "obl.planar-kinematics.component-i" : "obl.planar-kinematics.component-j",
             std::string("the ") + axis_names[axis] + " displacement equals v0 t + a t^2 / 2"});
        const std::string action = rational_text(velocity_components[axis]) + " * " +
                                   rational_text(time_si) + " + " +
                                   rational_text(acceleration_components[axis]) + " * " +
                                   rational_text(half_time_squared) + " = " +
                                   rational_text(displacement_components[axis]);
        if (!add_transformation(derivation, meter, plan_id, std::move(component), before, action,
                                after, true)) {
            return PlanarKinematicsResult();
        }
    }

    // The final check is the average-velocity identity, which is a different route to the same
    // displacement: it uses the final velocity this solve produced rather than the acceleration, so
    // it fails if either result is wrong.
    bool identity_holds = true;
    std::string identity_detail;
    size_t backend_requests = 0;
    for (size_t axis = 0; axis < 2 && identity_holds; ++axis) {
        Rational sum;
        Rational average;
        Rational predicted;
        if (!rational_add(velocity_components[axis], final_components[axis], &sum) ||
            !rational_mul(sum, Rational{1, 2}, &average) ||
            !rational_mul(average, time_si, &predicted)) {
            return failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "the average-velocity check does not fit exact arithmetic");
        }
        identity_holds = rational_equal(predicted, displacement_components[axis]);
        identity_detail += std::string(axis == 0 ? "" : ", ") + axis_names[axis] + ": " +
                           rational_text(predicted) + " against " +
                           rational_text(displacement_components[axis]);
    }
    if (giac && identity_holds) {
        if (!meter.backend_call())
            return PlanarKinematicsResult();
        const NodeId target = arena.binary(
            Kind::Mul,
            arena.binary(Kind::Mul,
                         arena.binary(Kind::Add, normalized_rational_node(arena, velocity_components[1]),
                                      normalized_rational_node(arena, final_components[1])),
                         normalized_rational_node(arena, Rational{1, 2})),
            normalized_rational_node(arena, time_si));
        if (arena.failed()) {
            return failed(PlanarKinematicsOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached, status_name(arena.status()));
        }
        Request request;
        request.op = Op::Simplify;
        request.target = target;
        Adapter adapter(arena, *giac);
        const Response response = adapter.run(request);
        backend_requests = adapter.call_count();
        const bool usable_exact = response.tag == ResultTag::Exact && response.value != kNoNode;
        bool agrees = false;
        if (usable_exact) {
            const NodeId backend_value = canonicalize(arena, response.value);
            const NodeId local_value =
                canonicalize(arena, normalized_rational_node(arena, displacement_components[1]));
            agrees = !arena.failed() && backend_value == local_value;
        }
        if (arena.failed()) {
            return failed(PlanarKinematicsOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached, status_name(arena.status()));
        }
        if (usable_exact && !agrees) {
            identity_holds = false;
            identity_detail += "; Giac disagrees with the local vertical displacement";
        } else if (!usable_exact) {
            identity_detail += std::string("; Giac returned ") + tag_name(response.tag);
        } else {
            identity_detail += "; Giac agrees with the local vertical displacement";
        }
    }
    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-shared-time",
                   "Shared-time consistency", "Check the displacement against the average velocity",
                   "Both axes advance under one clock, so the average velocity over the interval "
                   "reproduces the same displacement",
                   "obl.planar-kinematics.shared-time",
                   "each axis satisfies s = (v0 + v) t / 2 with the same elapsed time",
                   "average-velocity identity", identity_detail, EvidenceStrength::CandidateChecked,
                   identity_holds ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "the two axes agree on one shared time", "s = (v0 + v) t / 2 on both axes",
                   identity_detail, backend_requests)) {
        return PlanarKinematicsResult();
    }
    if (!identity_holds) {
        return failed(PlanarKinematicsOutcome::VerificationFailed,
                      DerivationStatus::VerificationFailed,
                      "the displacement and the final velocity disagree about the shared time: " +
                          identity_detail);
    }

    // Both reported vectors are judged, since units.h asks a caller not to leave a rounding unread.
    auto report_measured = [&](const Vector &value, const char *what, std::string *text,
                               PlanarKinematicsResult *failure) -> bool {
        HalfPlace rounding = HalfPlace::Within;
        if (!reported_vector_text(value, text, rounding)) {
            *failure = failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                              DerivationStatus::ResourceLimitReached,
                              "reporting the measured precision exceeds exact integer arithmetic");
            return false;
        }
        Vector exact_display = value;
        exact_display.precision = Precision();
        std::string exact_text;
        HalfPlace exact_rounding = HalfPlace::Within;
        if (!reported_vector_text(exact_display, &exact_text, exact_rounding)) {
            *failure = failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                              DerivationStatus::ResourceLimitReached,
                              std::string("the exact ") + what + " cannot be formatted");
            return false;
        }
        if (rounding == HalfPlace::Outside) {
            *failure = failed(PlanarKinematicsOutcome::VerificationFailed,
                              DerivationStatus::VerificationFailed,
                              *text + " is further than half a unit in its last place from " +
                                  exact_text);
            return false;
        }
        if (*text == exact_text && rounding != HalfPlace::Unreadable)
            return true;
        const bool compared = rounding == HalfPlace::Within;
        Step precision = transformation_step(
            "physics.planar-kinematics.significant-figures", "Significant figures",
            std::string("Report the ") + what + " to the measured precision",
            "Round only after exact conversion, decomposition and verification",
            compared ? "Reach for this once, at the very end. A measured value is only as good as "
                       "the figures it was written with, so the answer is reported to the fewest "
                       "among the measurements it came from. Rounding an axis before combining the "
                       "terms would throw away figures the final rounding cannot get back."
                     : "The rounded spelling could not be read back as a decimal, so it was never "
                       "compared against the exact value. The exact value is reported instead.",
            ClaimType::NoClaim,
            verification("exact comparison against the unrounded value",
                         compared ? *text + " is within half a unit in the last place of " +
                                        exact_text
                                  : *text + " could not be read back as a decimal",
                         EvidenceStrength::CandidateChecked,
                         compared ? VerificationOutcome::Passed
                                  : VerificationOutcome::Inconclusive));
        const std::string action = compared ? "Report " + exact_text + " as " + *text
                                            : "Report " + exact_text + " unrounded";
        if (!compared)
            *text = exact_text;
        const NodeId exact_vector = vector_node(arena, value);
        if (arena.failed()) {
            *failure = failed(PlanarKinematicsOutcome::ResourceExceeded,
                              DerivationStatus::ResourceLimitReached, status_name(arena.status()));
            return false;
        }
        if (!add_transformation(derivation, meter, plan_id, std::move(precision), exact_vector,
                                action, exact_vector, false)) {
            *failure = PlanarKinematicsResult();
            return false;
        }
        return true;
    };

    std::string displacement_text;
    std::string final_velocity_text;
    PlanarKinematicsResult reporting_failure;
    if (!report_measured(displacement, "displacement", &displacement_text, &reporting_failure) ||
        !report_measured(final_velocity, "final velocity", &final_velocity_text,
                         &reporting_failure)) {
        return reporting_failure;
    }

    PlanarKinematicsResult solved;
    solved.outcome = PlanarKinematicsOutcome::Solved;
    solved.displacement = displacement;
    solved.final_velocity = final_velocity;
    solved.has_value = true;
    solved.displacement_text = displacement_text;
    solved.final_velocity_text = final_velocity_text;
    solved.interpretation = describe_motion(problem, displacement, final_velocity);
    solved.equation = equation;
    solved.substituted = substituted;
    solved.status = derivation.outcome_from(0);
    return solved;
}

}  // namespace

const char *planar_axes_name(PlanarAxes axes) {
    switch (axes) {
        case PlanarAxes::RightUp: return "right-up";
    }
    return "invalid";
}

const char *motion_stage_name(MotionStage stage) {
    switch (stage) {
        case MotionStage::Event: return "event";
        case MotionStage::State: return "state";
        case MotionStage::Interval: return "interval";
    }
    return "unknown";
}

const char *planar_kinematics_outcome_name(PlanarKinematicsOutcome outcome) {
    switch (outcome) {
        case PlanarKinematicsOutcome::Solved: return "solved";
        case PlanarKinematicsOutcome::InvalidProblem: return "invalid problem";
        case PlanarKinematicsOutcome::RankMismatch: return "rank mismatch";
        case PlanarKinematicsOutcome::FrameUndeclared: return "frame undeclared";
        case PlanarKinematicsOutcome::FrameMismatch: return "frame mismatch";
        case PlanarKinematicsOutcome::DimensionMismatch: return "dimension mismatch";
        case PlanarKinematicsOutcome::StageMismatch: return "stage mismatch";
        case PlanarKinematicsOutcome::NotProjectile: return "not a projectile";
        case PlanarKinematicsOutcome::NoApex: return "no apex above the launch point";
        case PlanarKinematicsOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case PlanarKinematicsOutcome::VerificationFailed: return "verification failed";
        case PlanarKinematicsOutcome::Cancelled: return "cancelled";
        case PlanarKinematicsOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

PlanarKinematicsResult solve_planar_kinematics(Arena &arena, Derivation &derivation,
                                               const PlanarKinematicsProblem &problem,
                                               const Budget &budget, Backend *giac) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    PlanarKinematicsResult result = solve_body(arena, derivation, meter, problem, giac, &model);

    const bool arithmetic_overflow = result.outcome == PlanarKinematicsOutcome::ArithmeticOverflow;
    const bool halted = meter.stopped() || result.outcome == PlanarKinematicsOutcome::Cancelled ||
                        result.outcome == PlanarKinematicsOutcome::ResourceExceeded ||
                        arithmetic_overflow;
    if (halted) {
        const bool cancelled = result.outcome == PlanarKinematicsOutcome::Cancelled ||
                               meter.halt() == Halt::Cancelled;
        bool kept = false;
        if (arithmetic_overflow)
            derivation.rewind_to(mark);
        else
            kept = keep_verified_prefix(derivation, mark, arena);
        PlanarKinematicsResult stopped;
        stopped.outcome = cancelled            ? PlanarKinematicsOutcome::Cancelled
                          : arithmetic_overflow ? PlanarKinematicsOutcome::ArithmeticOverflow
                                                : PlanarKinematicsOutcome::ResourceExceeded;
        stopped.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        stopped.status = !cancelled ? DerivationStatus::ResourceLimitReached
                         : kept     ? DerivationStatus::Cancelled
                                    : DerivationStatus::NotRecorded;
        stopped.cost = meter.cost();
        const NodeId context_model = model < arena.node_count() ? model : kNoNode;
        record_context(derivation, budget, context_model, stopped.status, problem);
        return stopped;
    }

    if (result.outcome == PlanarKinematicsOutcome::Solved)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

namespace {

PlanarApexResult apex_failed(PlanarKinematicsOutcome outcome, DerivationStatus status,
                             const std::string &detail) {
    PlanarApexResult failure;
    failure.outcome = outcome;
    failure.status = status;
    failure.detail = detail;
    return failure;
}

// Keeps a nested solve_kinematics call's Cancelled and ResourceExceeded distinct from an actual
// physics disagreement, per AGENTS.md's contract to preserve.
PlanarKinematicsOutcome apex_outcome_for_kinematics_failure(KinematicsOutcome outcome) {
    switch (outcome) {
        case KinematicsOutcome::Cancelled: return PlanarKinematicsOutcome::Cancelled;
        case KinematicsOutcome::ResourceExceeded: return PlanarKinematicsOutcome::ResourceExceeded;
        default: return PlanarKinematicsOutcome::VerificationFailed;
    }
}

}  // namespace

bool apex_routes_agree(Arena &arena, NodeId route_one_value, NodeId route_two_value) {
    const NodeId route_one_canonical = canonicalize(arena, route_one_value);
    const NodeId route_two_canonical = canonicalize(arena, route_two_value);
    return !arena.failed() && route_one_canonical == route_two_canonical;
}

PlanarApexResult solve_planar_apex(Arena &arena, Derivation &derivation,
                                   const PlanarApexProblem &problem, const Budget &budget,
                                   Backend *giac) {
    Meter meter(budget);
    const size_t mark = derivation.mark();

    if (!valid_axes(problem.axes)) {
        return apex_failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                           "the coordinate convention is invalid");
    }
    if (problem.body_name.empty()) {
        return apex_failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                           "the moving body needs a nonempty name");
    }
    std::string invalid_detail;
    if (!valid_vector(problem.initial_velocity, &invalid_detail)) {
        return apex_failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                           "initial velocity: " + invalid_detail);
    }
    if (!valid_vector(problem.acceleration, &invalid_detail)) {
        return apex_failed(PlanarKinematicsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                           "acceleration: " + invalid_detail);
    }
    if (problem.initial_velocity.rank != 2 || problem.acceleration.rank != 2) {
        return apex_failed(PlanarKinematicsOutcome::RankMismatch, DerivationStatus::InvalidInput,
                           "planar kinematics requires a two-component velocity and acceleration");
    }
    const bool frames_declared = !problem.initial_velocity.frame.name.empty() &&
                                 !problem.acceleration.frame.name.empty();
    if (!frames_declared) {
        return apex_failed(PlanarKinematicsOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                           "both the velocity and the acceleration must declare a named frame");
    }
    if (problem.initial_velocity.frame != problem.acceleration.frame) {
        return apex_failed(PlanarKinematicsOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                           "cannot combine frame " + problem.initial_velocity.frame.name +
                               " with frame " + problem.acceleration.frame.name +
                               " without an explicit basis transformation");
    }
    const bool stages_ok = problem.initial_velocity_stage == MotionStage::State &&
                           problem.acceleration_stage == MotionStage::Interval;
    if (!stages_ok) {
        return apex_failed(PlanarKinematicsOutcome::StageMismatch, DerivationStatus::InvalidInput,
                           "the velocity is a state and the acceleration spans the interval");
    }
    const bool dimensions_ok = problem.initial_velocity.unit.dimension == velocity_dimension() &&
                               problem.acceleration.unit.dimension == acceleration_dimension();
    if (!dimensions_ok) {
        return apex_failed(PlanarKinematicsOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                           "the apex needs a velocity and an acceleration");
    }
    if (problem.acceleration.x.num != 0 || problem.acceleration.y.num >= 0) {
        return apex_failed(PlanarKinematicsOutcome::NotProjectile, DerivationStatus::InvalidInput,
                           "the projectile specialization needs a zero horizontal acceleration and "
                           "a downward vertical one, but received " +
                               vector_text(problem.acceleration));
    }

    Vector velocity_si;
    Vector acceleration_si;
    if (!to_si(problem.initial_velocity, &velocity_si) ||
        !to_si(problem.acceleration, &acceleration_si)) {
        return apex_failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                           DerivationStatus::ResourceLimitReached,
                           "a component does not fit exact arithmetic after SI conversion");
    }
    if (velocity_si.y.num <= 0) {
        return apex_failed(PlanarKinematicsOutcome::NoApex, DerivationStatus::InvalidInput,
                           "the vertical component of the initial velocity must be positive for the "
                           "body to rise above the launch point, but received " +
                               vector_text(problem.initial_velocity));
    }

    using measure::rational_of_node;

    const Quantity v0y = si_scalar(velocity_si.y, velocity_dimension(), velocity_si.precision);
    const Quantity ay = si_scalar(acceleration_si.y, acceleration_dimension(), acceleration_si.precision);
    const Quantity zero_velocity = si_scalar(Rational{0, 1}, velocity_dimension(), Precision());

    // Route one: isolate the time at which the vertical velocity is zero, then substitute it into
    // the displacement equation. Two hops, two calls, because giving v0, v and a together would let
    // the backward chainer reach v^2 = v0^2 + 2 a x directly, which is route two.
    KinematicsProblem time_problem;
    time_problem.unknown = "t";
    time_problem.knowns.push_back({"v0", v0y});
    time_problem.knowns.push_back({"v", zero_velocity});
    time_problem.knowns.push_back({"a", ay});
    const KinematicsResult time_result = solve_kinematics(arena, derivation, time_problem, budget, giac);
    if (time_result.outcome != KinematicsOutcome::Solved) {
        return apex_failed(apex_outcome_for_kinematics_failure(time_result.outcome), time_result.status,
                           "route one could not isolate the time to the apex: " + time_result.detail);
    }
    Rational t_value;
    if (!rational_of_node(arena, time_result.value, &t_value)) {
        return apex_failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                           DerivationStatus::ResourceLimitReached,
                           "the time to the apex does not fit exact arithmetic");
    }
    const Quantity t_quantity = si_scalar(t_value, time_dimension(), time_result.precision);

    KinematicsProblem route_one_height;
    route_one_height.unknown = "x";
    route_one_height.knowns.push_back({"v0", v0y});
    route_one_height.knowns.push_back({"a", ay});
    route_one_height.knowns.push_back({"t", t_quantity});
    const KinematicsResult route_one_result =
        solve_kinematics(arena, derivation, route_one_height, budget, giac);
    if (route_one_result.outcome != KinematicsOutcome::Solved) {
        return apex_failed(apex_outcome_for_kinematics_failure(route_one_result.outcome),
                           route_one_result.status,
                           "route one could not reach the apex height: " + route_one_result.detail);
    }

    // Route two: the same height directly from v^2 = v0^2 + 2 a x with the apex velocity given as
    // zero. It shares the model and the inputs with route one but not the equation.
    KinematicsProblem route_two_height;
    route_two_height.unknown = "x";
    route_two_height.knowns.push_back({"v0", v0y});
    route_two_height.knowns.push_back({"v", zero_velocity});
    route_two_height.knowns.push_back({"a", ay});
    const KinematicsResult route_two_result =
        solve_kinematics(arena, derivation, route_two_height, budget, giac);
    if (route_two_result.outcome != KinematicsOutcome::Solved) {
        return apex_failed(apex_outcome_for_kinematics_failure(route_two_result.outcome),
                           route_two_result.status,
                           "route two could not reach the apex height: " + route_two_result.detail);
    }

    const bool routes_agree = apex_routes_agree(arena, route_one_result.value, route_two_result.value);
    const std::string agreement_detail =
        "route one (time to the apex, then the displacement equation) reached " +
        route_one_result.value_text +
        " m; route two (v^2 = v0^2 + 2 a x with the apex velocity zero) reached " +
        route_two_result.value_text + " m";

    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Confirm the apex height by a second independent route";
    plan_step.rule_id = "physics.planar-kinematics.apex-plan";
    plan_step.rule_name = "Projectile apex verification";
    plan_step.explanation_short =
        "Compare the time-to-apex route against the direct v^2 = v0^2 + 2 a x route";
    plan_step.claim = ClaimType::NoClaim;
    PlanPayload plan;
    plan.strategy_id = "physics.planar-kinematics.apex-plan";
    plan.selected_strategy = "Two independent routes to the apex height";
    plan.matched_problem_facts.push_back("body: " + problem.body_name);
    plan.matched_problem_facts.push_back("apex condition: vertical velocity zero");
    plan.alternatives_considered.push_back(
        "recomputing the same equation twice, which is not an independent check");
    plan.selection_rationale =
        "the one-dimensional kinematics engine already carries both equations, so the apex height "
        "is reached twice by two different equations sharing only the model and the inputs";
    if (!meter.step()) {
        return apex_failed(PlanarKinematicsOutcome::ResourceExceeded,
                           DerivationStatus::ResourceLimitReached, "step budget exhausted");
    }
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    if (!add_check(derivation, meter, plan_id, "physics.planar-kinematics.check-apex-routes",
                  "Independent apex routes", "Check that both routes to the apex height agree",
                  "The apex height comes from two derivations that share the model and the inputs "
                  "but not the equation, so their agreement is the verification",
                  "obl.planar-kinematics.apex-routes-agree",
                  "the time-to-apex route and the direct v^2 = v0^2 + 2 a x route reach the same "
                  "height",
                  "two independent routes", agreement_detail, EvidenceStrength::CandidateChecked,
                  routes_agree ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                  "both routes agree on the apex height", route_one_result.value_text + " m",
                  agreement_detail)) {
        return apex_failed(PlanarKinematicsOutcome::ResourceExceeded,
                           DerivationStatus::ResourceLimitReached, "step budget exhausted");
    }
    if (!routes_agree) {
        return apex_failed(PlanarKinematicsOutcome::VerificationFailed,
                           DerivationStatus::VerificationFailed,
                           "the two routes to the apex height disagree: " + agreement_detail);
    }

    Rational height_value;
    if (!rational_of_node(arena, route_two_result.value, &height_value)) {
        return apex_failed(PlanarKinematicsOutcome::ArithmeticOverflow,
                           DerivationStatus::ResourceLimitReached,
                           "the apex height does not fit exact arithmetic");
    }

    PlanarApexResult solved;
    solved.outcome = PlanarKinematicsOutcome::Solved;
    solved.has_value = true;
    solved.time_to_apex = t_quantity;
    solved.time_to_apex_text = time_result.value_text;
    solved.height = si_scalar(height_value, length_dimension(), route_two_result.precision);
    solved.height_text = route_two_result.value_text;
    solved.status = derivation.outcome_from(mark);
    solved.cost = meter.cost();
    return solved;
}

}  // namespace nps
