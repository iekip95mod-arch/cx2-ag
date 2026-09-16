#include "nps/physics/work.h"

#include <utility>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "measurement_support.h"

namespace nps {
namespace {

using measure::dimension_node;

Dimension force_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

Dimension displacement_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

Dimension work_dimension() {
    Dimension dimension;
    dimension.length = 2;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

WorkResult failed(WorkOutcome outcome, DerivationStatus status, const std::string &detail) {
    WorkResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

bool valid_profile(WorkForceProfile profile) {
    switch (profile) {
        case WorkForceProfile::Unspecified:
        case WorkForceProfile::Constant:
        case WorkForceProfile::Variable: return true;
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
    Rational value;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!normalized(components[axis], &value)) {
            *detail = std::string("the ") + names[axis] + " component has an invalid exact value";
            return false;
        }
    }
    if (!normalized(vector.unit.scale, &value) || value.num <= 0) {
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
    const NodeId reciprocal =
        arena.binary(Kind::Pow, denominator, arena.integer("-1"));
    return arena.binary(Kind::Mul, numerator, reciprocal);
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

NodeId work_equation(Arena &arena, NodeId work_symbol, const Vector &force,
                     const Vector &displacement) {
    const NodeId dot = arena.call(
        "dot", {vector_model_node(arena, force), vector_model_node(arena, displacement)});
    return arena.binary(Kind::Equals, work_symbol, dot);
}

bool needs_conversion(const Vector &vector) {
    Rational scale;
    return !normalized(vector.unit.scale, &scale) || scale.num != 1 || scale.den != 1;
}

std::string safe_vector_text(const Vector &vector) {
    if (vector.rank == 2 || vector.rank == 3)
        return vector_text(vector);
    return "rank " + std::to_string(vector.rank) + " vector";
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

Step transformation_step(const char *rule_id, const char *rule_name, const char *goal,
                         const char *explanation, const char *detailed, ClaimType claim,
                         const VerificationRecord &record) {
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = claim;
    step.verifications.push_back(record);
    return step;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const WorkProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.work.constant-force-dot-product";
    inputs.requested_method = "validate constant-force applicability, convert component vectors to "
                              "SI, evaluate the existing exact dot product, and report once";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions.push_back(std::string("force profile is ") +
                                        work_force_profile_name(problem.force_profile));
    inputs.active_assumptions.push_back("force frame is " + problem.force.frame.name);
    inputs.active_assumptions.push_back("displacement frame is " +
                                        problem.displacement.frame.name);
    inputs.unit_policy = "exact SI conversion and dot-product arithmetic, final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

WorkSign sign_of(const Rational &value) {
    if (value.num < 0)
        return WorkSign::Negative;
    return value.num > 0 ? WorkSign::Positive : WorkSign::Zero;
}

const char *sign_interpretation(WorkSign sign) {
    switch (sign) {
        case WorkSign::Negative:
            return "the force has a component opposite the displacement direction";
        case WorkSign::Zero:
            return "the force has no net component along the displacement";
        case WorkSign::Positive:
            return "the force has a component in the displacement direction";
    }
    return "the work sign is invalid";
}

void component_nodes(Arena &arena, const Vector &vector, std::vector<NodeId> *components) {
    components->clear();
    components->push_back(rational_node(arena, vector.x));
    components->push_back(rational_node(arena, vector.y));
    if (vector.rank == 3)
        components->push_back(rational_node(arena, vector.z));
}

WorkResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                      const WorkProblem &problem, Backend *giac, NodeId *model) {
    if (!valid_profile(problem.force_profile)) {
        return failed(WorkOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the force profile is invalid");
    }
    std::string invalid_detail;
    if (!valid_vector(problem.force, &invalid_detail)) {
        return failed(WorkOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "force: " + invalid_detail);
    }
    if (!valid_vector(problem.displacement, &invalid_detail)) {
        return failed(WorkOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "displacement: " + invalid_detail);
    }

    *model = arena.call("work_problem",
                        {vector_model_node(arena, problem.force),
                         vector_model_node(arena, problem.displacement),
                         arena.symbol(work_force_profile_name(problem.force_profile))});
    if (arena.failed()) {
        return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }

    PlanPayload plan;
    plan.strategy_id = "physics.work.plan";
    plan.selected_strategy = "Constant-force work from Cartesian components";
    plan.matched_problem_facts.push_back("force: " + safe_vector_text(problem.force));
    plan.matched_problem_facts.push_back("displacement: " +
                                         safe_vector_text(problem.displacement));
    plan.matched_problem_facts.push_back(std::string("force profile: ") +
                                         work_force_profile_name(problem.force_profile));
    plan.alternatives_considered.push_back("variable-force line integral");
    plan.selection_rationale =
        giac ? "the local exact vector operation produces the result and Giac independently checks it"
             : "the existing local exact vector operation produces and checks the component dot product";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find the work done by the supplied force";
    plan_step.rule_id = "physics.work.plan";
    plan_step.rule_name = "Constant-force work plan";
    plan_step.explanation_short = "Check applicability, dimensions and frames before using W = F dot d";
    plan_step.assumptions_before.push_back(std::string("force profile is ") +
                                           work_force_profile_name(problem.force_profile));
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.work.constant-force",
        "the supplied force is constant over the displacement", "declared force profile",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before applying the work definition");
    register_strategy_precondition(
        plan, plan_step, "pre.work.matching-rank",
        "both vectors have matching rank two or three", "rank comparison",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before evaluating the dot product");
    register_strategy_precondition(
        plan, plan_step, "pre.work.frames-declared", "both vectors declare a named frame",
        "frame declaration", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before comparing frame identity");
    register_strategy_precondition(
        plan, plan_step, "pre.work.frames-match", "both vectors use the same frame",
        "frame identity", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before evaluating matching components");
    register_strategy_precondition(
        plan, plan_step, "pre.work.input-dimensions",
        "force and displacement have their required dimensions", "dimensional analysis",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked before applying the work definition");
    if (!meter.step())
        return WorkResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool applicable = problem.force_profile == WorkForceProfile::Constant;
    const VerificationOutcome applicability_outcome =
        applicable ? VerificationOutcome::Passed
                   : problem.force_profile == WorkForceProfile::Unspecified
                         ? VerificationOutcome::Inconclusive
                         : VerificationOutcome::Failed;
    const std::string profile_text = work_force_profile_name(problem.force_profile);
    if (!add_check(derivation, meter, plan_id, "physics.work.check-applicability",
                   "Constant-force applicability", "Check whether the work definition applies",
                   "W = F dot d requires a force constant over the displacement",
                   "obl.work.constant-force", "the force is constant over the displacement",
                   "declared force profile", profile_text, EvidenceStrength::StructurallyValid,
                   applicability_outcome,
                   "the constant-force work definition applies", "constant force", profile_text)) {
        return WorkResult();
    }
    derivation.complete_plan_precondition(plan_id, "pre.work.constant-force",
                                          applicability_outcome, profile_text);
    if (!applicable) {
        if (problem.force_profile == WorkForceProfile::Unspecified) {
            return failed(WorkOutcome::ClarificationRequired,
                          DerivationStatus::ClarificationRequired,
                          "state whether the force is constant over the displacement");
        }
        return failed(WorkOutcome::LawNotApplicable, DerivationStatus::Unsupported,
                      "a variable force requires a path integral, not one component-vector dot product");
    }

    const bool ranks_supported =
        (problem.force.rank == 2 || problem.force.rank == 3) &&
        (problem.displacement.rank == 2 || problem.displacement.rank == 3);
    const bool ranks_match = problem.force.rank == problem.displacement.rank;
    const bool rank_ok = ranks_supported && ranks_match;
    const std::string ranks = "ranks " + std::to_string(problem.force.rank) + " and " +
                              std::to_string(problem.displacement.rank);
    if (!add_check(derivation, meter, plan_id, "physics.work.check-rank", "Vector rank",
                   "Check vector ranks", "A Cartesian dot product needs matching rank two or three",
                   "obl.work.ranks-match", "both vectors have matching rank two or three",
                   "rank comparison", ranks, EvidenceStrength::StructurallyValid,
                   rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "force and displacement ranks are compatible", "matching rank 2 or rank 3", ranks)) {
        return WorkResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.work.matching-rank",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, ranks);
    if (!rank_ok) {
        return failed(WorkOutcome::RankMismatch, DerivationStatus::InvalidInput,
                      "work requires force and displacement vectors of matching rank two or three");
    }

    const bool frames_declared = !problem.force.frame.name.empty() &&
                                 !problem.displacement.frame.name.empty();
    const std::string frames = "frames '" + problem.force.frame.name + "' and '" +
                               problem.displacement.frame.name + "'";
    if (!add_check(derivation, meter, plan_id, "physics.work.check-frame-declared",
                   "Declared vector frames", "Check that both vector frames are declared",
                   "A vector component list needs a named frame", "obl.work.frames-declared",
                   "both vectors declare a nonempty frame", "frame declaration", frames,
                   EvidenceStrength::StructurallyValid,
                   frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "force and displacement frames are declared", "two nonempty frame names", frames)) {
        return WorkResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.work.frames-declared",
        frames_declared ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_declared) {
        return failed(WorkOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                      "both force and displacement must declare a named frame");
    }

    const bool frames_match = problem.force.frame == problem.displacement.frame;
    if (!add_check(derivation, meter, plan_id, "physics.work.check-frame-match",
                   "Matching vector frames", "Check that the vector frames match",
                   "A dot product across frames needs an explicit basis transformation",
                   "obl.work.frames-match", "force and displacement use the same frame",
                   "frame identity", frames, EvidenceStrength::StructurallyValid,
                   frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "force and displacement are expressed in one frame", "identical frame names",
                   frames)) {
        return WorkResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.work.frames-match",
        frames_match ? VerificationOutcome::Passed : VerificationOutcome::Failed, frames);
    if (!frames_match) {
        return failed(WorkOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                      "cannot compute work across frames " + problem.force.frame.name + " and " +
                          problem.displacement.frame.name +
                          " without an explicit basis transformation");
    }

    const Dimension required_force = force_dimension();
    const Dimension required_displacement = displacement_dimension();
    const bool force_dimension_ok = problem.force.unit.dimension == required_force;
    const bool displacement_dimension_ok =
        problem.displacement.unit.dimension == required_displacement;
    const bool input_dimensions_ok = force_dimension_ok && displacement_dimension_ok;
    const std::string observed_dimensions =
        "force " + dimension_text(problem.force.unit.dimension) + ", displacement " +
        dimension_text(problem.displacement.unit.dimension);
    const std::string expected_dimensions = "force " + dimension_text(required_force) +
                                             ", displacement " +
                                             dimension_text(required_displacement);
    if (!add_check(derivation, meter, plan_id, "physics.work.check-input-dimensions",
                   "Work quantity dimensions", "Check force and displacement dimensions",
                   "Force and displacement are distinct physical roles",
                   "obl.work.input-dimensions", "force is M L T^-2 and displacement is L",
                   "dimensional analysis", observed_dimensions,
                   EvidenceStrength::DimensionallyValid,
                   input_dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "the supplied vectors have the dimensions required by the work definition",
                   expected_dimensions, observed_dimensions)) {
        return WorkResult();
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.work.input-dimensions",
        input_dimensions_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        observed_dimensions);
    if (!input_dimensions_ok) {
        return failed(WorkOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "work requires " + expected_dimensions + ", but received " +
                          observed_dimensions);
    }

    const NodeId work_symbol = arena.symbol("W");
    const NodeId equation = work_equation(arena, work_symbol, problem.force, problem.displacement);
    if (arena.failed()) {
        return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }
    Step law = transformation_step(
        "physics.work.constant-force-definition", "Constant-force work definition",
        "Apply W = F dot d", "Use the component-vector dot product after its applicability checks",
        "Reach for this when a constant force acts over a straight displacement and the question "
        "asks for work or energy transferred. The dot product is what makes only the part of the "
        "force along the displacement count: a force at right angles to the motion does no work at "
        "all, which is why pushing sideways on a sliding box changes nothing. It needs the force to "
        "be constant, so a spring or any varying force is a different problem.",
        ClaimType::Definition,
        verification("checked physical-law applicability",
                     "the force profile, vector ranks, frames and quantity roles all passed",
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    law.proof_obligations.push_back(
        {"obl.work.law-applied-after-checks", "all applicability checks precede the work definition"});
    if (!add_transformation(derivation, meter, plan_id, std::move(law), *model,
                            "Apply the constant-force definition W = F dot d", equation, false)) {
        return WorkResult();
    }

    Vector force_si;
    Vector displacement_si;
    if (!to_si(problem.force, &force_si)) {
        return failed(WorkOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "the force components do not fit exact arithmetic after SI conversion");
    }
    if (!to_si(problem.displacement, &displacement_si)) {
        return failed(WorkOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "the displacement components do not fit exact arithmetic after SI conversion");
    }
    const NodeId substituted = work_equation(arena, work_symbol, force_si, displacement_si);
    if (arena.failed()) {
        return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }
    if (needs_conversion(problem.force) || needs_conversion(problem.displacement)) {
        Step conversion = transformation_step(
            "physics.work.convert-si", "Exact SI conversion", "Convert both vectors to SI",
            "Apply each unit's exact scale to every active component",
            "Reach for this whenever the force and the displacement are written in units that are "
            "not newtons and metres. Multiplying a force in kilonewtons by a distance in "
            "centimetres gives a number in neither joules nor anything else, so both go onto the "
            "SI scale first and the answer comes out in joules. The scale factors are exact, so "
            "nothing is lost here and nothing is rounded yet.",
            ClaimType::EquivalentExpression,
            verification("unit table", "both vector scales were applied exactly",
                         EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        conversion.proof_obligations.push_back(
            {"obl.physics.converts-by-table",
             "the SI form comes from the unit table's exact factor for the entered unit"});
        if (!add_transformation(derivation, meter, plan_id, std::move(conversion), equation,
                                safe_vector_text(problem.force) + " and " +
                                    safe_vector_text(problem.displacement) + " become " +
                                    safe_vector_text(force_si) + " and " +
                                    safe_vector_text(displacement_si),
                                substituted,
                                true)) {
            return WorkResult();
        }
    }

    Quantity exact_work;
    std::string dot_error;
    if (!vector_dot(force_si, displacement_si, &exact_work, &dot_error)) {
        return failed(WorkOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      dot_error);
    }
    exact_work.unit.text = si_unit_text(exact_work.unit.dimension);
    const Dimension expected_work = work_dimension();
    // True on every reachable path, because the input check above pins force and displacement and
    // vector_dot adds their exponents. Kept so a units-layer fault refuses rather than answering,
    // which is what mutating the dot product dimension shows.
    const bool output_dimension_ok = exact_work.unit.dimension == expected_work;
    const std::string output_dimension = dimension_text(exact_work.unit.dimension);
    if (!add_check(derivation, meter, plan_id, "physics.work.check-result-dimension",
                   "Derived work dimension", "Derive the work dimension",
                   "The dot product multiplies force and displacement dimensions",
                   "obl.work.result-dimension", "work has dimension M L^2 T^-2",
                   "dimensional multiplication", output_dimension,
                   EvidenceStrength::DimensionallyValid,
                   output_dimension_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "dimension of force times displacement", dimension_text(expected_work),
                   output_dimension)) {
        return WorkResult();
    }
    if (!output_dimension_ok) {
        return failed(WorkOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      "the vector dot operation returned dimension " + output_dimension +
                          " instead of " + dimension_text(expected_work));
    }

    const NodeId value = rational_node(arena, exact_work.value);
    const NodeId evaluated = arena.binary(Kind::Equals, work_symbol, value);
    if (arena.failed()) {
        return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }
    Step evaluation = transformation_step(
        "physics.work.evaluate-dot", "Exact vector dot product", "Evaluate the component dot product",
        "Use the existing frame-aware exact vector operation",
        "Reach for this once the force and displacement are both in SI and the dot product is ready "
        "to be worked out. Multiply the two i components, multiply the two j components, and add "
        "the results: one number out of two vectors, which is what makes work a scalar with no "
        "direction of its own. A negative answer is meaningful rather than an error, and means the "
        "force opposed the motion, the way friction does.",
        ClaimType::EquivalentExpression,
        verification("nps vector_dot", rational_text(exact_work.value),
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    evaluation.proof_obligations.push_back(
        {"obl.work.dot-is-the-definition",
         "the value written down is the component dot product the definition names"});
    if (!add_transformation(derivation, meter, plan_id, std::move(evaluation), substituted,
                            "vector_dot gives " + rational_text(exact_work.value), evaluated, true)) {
        return WorkResult();
    }

    if (!add_check(derivation, meter, plan_id, "physics.work.check-candidate",
                   "Local work candidate", "Check the exact work candidate",
                   "The reported candidate must be the checked dot-operation result",
                   "obl.work.candidate-satisfies",
                   "the candidate is the exact value of the original component dot product",
                   "exact construction through vector_dot", rational_text(exact_work.value),
                   EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
                   "W satisfies W = F dot d",
                   rational_text(exact_work.value), rational_text(exact_work.value))) {
        return WorkResult();
    }

    NodeId backend_value = kNoNode;
    if (giac) {
        Request request;
        request.op = Op::Dot;
        component_nodes(arena, force_si, &request.target_components);
        component_nodes(arena, displacement_si, &request.argument_components);
        if (arena.failed()) {
            return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                          status_name(arena.status()));
        }
        if (!meter.backend_call())
            return WorkResult();
        Adapter adapter(arena, *giac);
        const Response response = adapter.run(request);
        backend_value = response.value;
        const bool usable_exact = response.tag == ResultTag::Exact && response.value != kNoNode;
        bool agrees = false;
        if (usable_exact) {
            const NodeId canonical_backend = canonicalize(arena, response.value);
            const NodeId canonical_local = canonicalize(arena, value);
            agrees = !arena.failed() && canonical_backend == canonical_local;
        }
        // Outside the branch above on purpose: reading the reply exhausts the arena too, and a
        // resource failure is sticky, so neither a verdict nor an answer may come out of one.
        if (arena.failed()) {
            return failed(WorkOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                          status_name(arena.status()));
        }
        const VerificationOutcome backend_outcome =
            !usable_exact ? VerificationOutcome::Inconclusive
                          : agrees ? VerificationOutcome::Passed : VerificationOutcome::Failed;
        const std::string backend_observed =
            !usable_exact ? std::string("Giac returned ") + tag_name(response.tag) +
                                (response.detail.empty() ? "" : ": " + response.detail)
                          : agrees ? "Giac's exact dot value matches the local exact value"
                                   : "Giac's exact dot value differs from the local exact value";
        if (!add_check(derivation, meter, plan_id, "physics.work.giac-cross-check",
                       "Independent dot-product check", "Cross-check the local exact work value",
                       "A supplied backend must return the same exact dot value",
                       "obl.work.backend-agrees", "Giac's Op::Dot result equals the local result",
                       "Giac Adapter Op::Dot and local canonical comparison", backend_observed,
                       EvidenceStrength::SymbolicallyEquivalentUnderAssumptions, backend_outcome,
                       "the independent dot result agrees",
                       "equal exact canonical values", backend_observed, adapter.call_count())) {
            return WorkResult();
        }
        // A backend that would not certify the value never checked it, so there is nothing to
        // withdraw. The exact dot product stands on physics.work.check-candidate above, and the
        // inconclusive record reaches outcome_from as solved but unchecked.
        if (usable_exact && !agrees) {
            return failed(WorkOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                          "Giac and the local exact vector operation disagree, so no value is offered");
        }
    }

    const WorkSign sign = sign_of(exact_work.value);
    const std::string interpretation = sign_interpretation(sign);
    if (!add_check(derivation, meter, plan_id, "physics.work.interpret-sign",
                   "Work sign interpretation", "Interpret the sign of the work",
                   "The exact dot-product sign states the force component along displacement",
                   "obl.work.sign-interpreted", "the work sign is interpreted from the exact value",
                   "exact rational sign", work_sign_name(sign), EvidenceStrength::StructurallyValid,
                   VerificationOutcome::Passed,
                   "the sign interpretation follows the exact work value", work_sign_name(sign),
                   interpretation)) {
        return WorkResult();
    }

    std::string reported = rational_text(exact_work.value);
    if (exact_work.precision.kind == NumberKind::Measured) {
        // Work is a sum of products, so the dot product's precision is a sum's and its last decimal
        // place is the given. Rounding to the figure count instead lost that place when the rounding
        // carried, as 5.0 plus 4.96 does: 9.96 at the tenths is 10.0, and a two-figure rounder wrote 10.
        std::string rounded;
        if (!precision_rounded_text(exact_work.value, exact_work.precision, &rounded)) {
            return failed(WorkOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                          "reporting the measured precision exceeds exact integer arithmetic");
        }
        if (rounded != reported) {
            // Checked against the string this step records. The predicate parses the reported text
            // back and measures the error, so it shares no path with the rounder. A wrong
            // predicate it cannot catch, and units_tests pins that with negative cases.
            const HalfPlace checked =
                precision_rounding_valid(exact_work.value, rounded, exact_work.precision);
            if (checked == HalfPlace::Outside) {
                return failed(WorkOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                              rounded + " is further than half a unit in its last place from " +
                                  reported);
            }
            if (checked == HalfPlace::Unreadable) {
                // Never compared, so nothing failed. The exact work value stands and the rounded
                // spelling is withheld, with the attempt recorded so the record says why.
                if (!meter.step())
                    return WorkResult();
                const std::string observed =
                    rounded + " could not be read back as a decimal, so it was never compared "
                              "against " +
                    reported;
                Step unchecked;
                unchecked.phase = "report";
                unchecked.goal = "Report the work to the measured precision";
                unchecked.rule_id = "physics.work.significant-figures";
                unchecked.rule_name = "Significant figures";
                unchecked.claim = ClaimType::NoClaim;
                unchecked.explanation_short =
                    "Round only after exact conversion, evaluation and verification";
                unchecked.explanation_detailed =
                    "The rounded spelling could not be read back as a decimal, so it was never "
                    "compared against the exact value. The exact value is reported instead, since "
                    "showing a rounding nothing checked would be showing an answer with no "
                    "evidence behind it.";
                unchecked.proof_obligations.push_back(
                    {"obl.work.rounding-within-half-place",
                     "the reported value is within half a unit in the last place of the exact one"});
                unchecked.verifications.push_back(
                    verification("exact half-place comparison", observed,
                                 EvidenceStrength::CandidateChecked,
                                 VerificationOutcome::Inconclusive));
                CheckPayload check;
                check.target_claim =
                    "the reported value is within half a unit in the last place of the exact one";
                check.check_method =
                    "read the rounded text back and compare it against the exact value";
                check.expected_relation =
                    "the difference is at most half a unit in the last place";
                check.observed_result = observed;
                derivation.add_check(plan_id, std::move(unchecked), std::move(check));
                // reported keeps the exact spelling, and outcome_from below reads the inconclusive
                // check as solved but unchecked.
            } else {
            const NodeId rounded_node = rounded.find('.') == std::string::npos
                                            ? arena.integer(rounded)
                                            : arena.decimal(rounded);
            if (arena.failed()) {
                return failed(WorkOutcome::ResourceExceeded,
                              DerivationStatus::ResourceLimitReached,
                              status_name(arena.status()));
            }
            Step precision = transformation_step(
                "physics.work.significant-figures", "Significant figures",
                "Report the work to the measured precision",
                "Round only after exact conversion, evaluation and verification",
                "Reach for this once, at the very end, and never partway through. A measured value "
                "is only as good as the figures it was written with, so the answer is reported to "
                "the fewest significant figures among the measurements it came from. Rounding a "
                "component before the dot product would throw away figures the final rounding "
                "cannot get back, which is why every step above this one keeps the exact value.",
                ClaimType::NoClaim,
                verification("exact half-place comparison",
                             rounded + " is within half a unit in the last place of " + reported,
                             EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
            precision.proof_obligations.push_back(
                {"obl.work.rounding-within-half-place",
                 "the reported value is within half a unit in the last place of the exact one"});
            if (!add_transformation(derivation, meter, plan_id, std::move(precision), value,
                                    "Report " + reported + " as " + rounded, rounded_node, false)) {
                return WorkResult();
            }
            reported = rounded;
            }
        }
    }

    WorkResult result;
    result.outcome = WorkOutcome::Solved;
    result.quantity = exact_work;
    result.has_value = true;
    result.sign = sign;
    result.value_text = reported;
    result.unit_text = exact_work.unit.text;
    result.interpretation = interpretation;
    result.value = value;
    result.equation = equation;
    result.substituted = substituted;
    result.backend_value = backend_value;
    result.status = derivation.outcome_from(0);
    return result;
}

}

const char *work_force_profile_name(WorkForceProfile profile) {
    switch (profile) {
        case WorkForceProfile::Unspecified: return "unspecified";
        case WorkForceProfile::Constant: return "constant";
        case WorkForceProfile::Variable: return "variable";
    }
    return "invalid";
}

const char *work_outcome_name(WorkOutcome outcome) {
    switch (outcome) {
        case WorkOutcome::Solved: return "solved";
        case WorkOutcome::InvalidProblem: return "invalid problem";
        case WorkOutcome::ClarificationRequired: return "clarification required";
        case WorkOutcome::LawNotApplicable: return "law not applicable";
        case WorkOutcome::RankMismatch: return "rank mismatch";
        case WorkOutcome::FrameUndeclared: return "frame undeclared";
        case WorkOutcome::FrameMismatch: return "frame mismatch";
        case WorkOutcome::DimensionMismatch: return "dimension mismatch";
        case WorkOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case WorkOutcome::VerificationFailed: return "verification failed";
        case WorkOutcome::Cancelled: return "cancelled";
        case WorkOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

const char *work_sign_name(WorkSign sign) {
    switch (sign) {
        case WorkSign::Negative: return "negative";
        case WorkSign::Zero: return "zero";
        case WorkSign::Positive: return "positive";
    }
    return "unknown";
}

WorkResult solve_work(Arena &arena, Derivation &derivation, const WorkProblem &problem,
                      const Budget &budget, Backend *giac) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    WorkResult result = solve_body(arena, derivation, meter, problem, giac, &model);

    const bool halted = meter.stopped() || result.outcome == WorkOutcome::Cancelled ||
                        result.outcome == WorkOutcome::ResourceExceeded;
    if (halted) {
        derivation.rewind_to(mark);
        const bool cancelled = result.outcome == WorkOutcome::Cancelled ||
                               meter.halt() == Halt::Cancelled;
        WorkResult stopped;
        stopped.outcome = cancelled ? WorkOutcome::Cancelled : WorkOutcome::ResourceExceeded;
        stopped.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        stopped.status = cancelled ? DerivationStatus::NotRecorded
                                   : DerivationStatus::ResourceLimitReached;
        stopped.cost = meter.cost();
        record_context(derivation, budget, model, stopped.status, problem);
        return stopped;
    }

    if (result.outcome == WorkOutcome::Solved) {
        result.status = derivation.outcome_from(mark);
    }
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

}
