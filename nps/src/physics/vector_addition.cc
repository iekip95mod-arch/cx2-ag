#include "nps/physics/vector_addition.h"

#include <utility>
#include <vector>

#include "nps/core/context.h"

namespace nps {
namespace {

NodeId rational_node(Arena &arena, const Rational &value) {
    if (value.den == 1)
        return arena.integer(integer_text(value.num));
    return arena.binary(Kind::Mul, arena.integer(integer_text(value.num)),
                        arena.binary(Kind::Pow, arena.integer(integer_text(value.den)),
                                     arena.integer("-1")));
}

NodeId vector_node(Arena &arena, const Vector &value) {
    std::vector<NodeId> components;
    components.push_back(rational_node(arena, value.x));
    components.push_back(rational_node(arena, value.y));
    if (value.rank == 3)
        components.push_back(rational_node(arena, value.z));
    return arena.call("vector", components);
}

NodeId vector_model_node(Arena &arena, const Vector &value) {
    NodeId dimension = arena.call("dimension", {arena.integer(integer_text(value.unit.dimension.length)),
                                                arena.integer(integer_text(value.unit.dimension.mass)),
                                                arena.integer(integer_text(value.unit.dimension.time))});
    return arena.call("framed_vector", {vector_node(arena, value), rational_node(arena, value.unit.scale),
                                         arena.symbol(value.frame.name), dimension});
}

VerificationRecord verification(const std::string &method, const std::string &detail,
                                EvidenceStrength passing, VerificationOutcome outcome) {
    VerificationRecord record;
    record.method = method;
    record.outcome = outcome;
    record.strength = strength_for(record.outcome, passing);
    record.detail = detail;
    return record;
}

VerificationRecord verification(const std::string &method, const std::string &detail,
                                EvidenceStrength passing, bool passed) {
    return verification(method, detail, passing,
                        passed ? VerificationOutcome::Passed : VerificationOutcome::Failed);
}

Step check_step(const char *rule_id, const std::string &goal, const std::string &explanation,
                const char *obligation_id, const std::string &obligation,
                const std::string &method, const std::string &detail, EvidenceStrength passing,
                bool passed) {
    Step step;
    step.phase = "check";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = "Vector addition precondition";
    step.explanation_short = explanation;
    step.claim = ClaimType::Definition;
    step.proof_obligations.push_back({obligation_id, obligation});
    step.verifications.push_back(verification(method, detail, passing, passed));
    return step;
}

bool add_check(Derivation &derivation, Meter &meter, StepId parent, Step step,
               const std::string &target, const std::string &expected,
               const std::string &observed) {
    if (!meter.step())
        return false;
    CheckPayload payload;
    payload.target_claim = target;
    payload.check_method = step.verifications[0].method;
    payload.expected_relation = expected;
    payload.observed_result = observed;
    derivation.add_check(parent, std::move(step), std::move(payload));
    return true;
}

bool add_transformation(Derivation &derivation, Meter &meter, StepId parent, Step step,
                        NodeId before, NodeId after, const std::string &action,
                        bool reversible = true) {
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

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const VectorAdditionProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.vectors.cartesian-addition.two-dimension";
    inputs.requested_method = "convert components to SI, add matching components, report once";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions.push_back("first vector frame is " + problem.first.frame.name);
    inputs.active_assumptions.push_back("second vector frame is " + problem.second.frame.name);
    inputs.unit_policy = "exact SI conversion and component arithmetic, final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

VectorAdditionResult halted_result(const Arena &arena, Derivation &derivation, size_t mark,
                                   Meter &meter, const Budget &budget, NodeId model,
                                   const VectorAdditionProblem &problem) {
    // STEP-025: keep the run of records that were checked. A precondition still waiting for its
    // check point leaves the plan unverified, so a halt before that point keeps nothing, which is
    // what actually happened.
    const bool cancelled = meter.halt() == Halt::Cancelled;
    const bool kept = keep_verified_prefix(derivation, mark, arena);
    VectorAdditionResult result;
    result.outcome = cancelled ? VectorAdditionOutcome::Cancelled
                               : VectorAdditionOutcome::ResourceExceeded;
    result.detail = halt_name(meter.halt());
    result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                    : kept     ? DerivationStatus::Cancelled
                               : DerivationStatus::NotRecorded;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

VectorAdditionResult arena_result(Arena &arena, Derivation &derivation, size_t mark, Meter &meter,
                                  const Budget &budget, NodeId model,
                                  const VectorAdditionProblem &problem) {
    derivation.rewind_to(mark);
    VectorAdditionResult result;
    result.outcome = VectorAdditionOutcome::ResourceExceeded;
    result.detail = status_name(arena.status());
    result.status = DerivationStatus::ResourceLimitReached;
    result.cost = meter.cost();
    const NodeId context_model = model < arena.node_count() ? model : kNoNode;
    record_context(derivation, budget, context_model, result.status, problem);
    return result;
}

}  // namespace

const char *vector_addition_outcome_name(VectorAdditionOutcome outcome) {
    switch (outcome) {
        case VectorAdditionOutcome::Solved: return "solved";
        case VectorAdditionOutcome::RankMismatch: return "rank mismatch";
        case VectorAdditionOutcome::FrameMismatch: return "frame mismatch";
        case VectorAdditionOutcome::DimensionMismatch: return "dimension mismatch";
        case VectorAdditionOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case VectorAdditionOutcome::VerificationFailed: return "verification failed";
        case VectorAdditionOutcome::Cancelled: return "cancelled";
        case VectorAdditionOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

VectorAdditionResult solve_vector_addition(Arena &arena, Derivation &derivation,
                                           const VectorAdditionProblem &problem,
                                           const Budget &budget) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    NodeId model = arena.call("vector_add", {vector_model_node(arena, problem.first),
                                             vector_model_node(arena, problem.second)});
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    if (meter.stopped())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    VectorAdditionResult result;
    PlanPayload plan;
    plan.strategy_id = "vec.add.plan";
    plan.selected_strategy = "Cartesian vector addition";
    plan.matched_problem_facts.push_back("first vector: " + vector_text(problem.first));
    plan.matched_problem_facts.push_back("second vector: " + vector_text(problem.second));
    plan.selection_rationale = "matching Cartesian components add after exact SI conversion";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Add the two Cartesian vectors";
    plan_step.rule_id = "vec.add.plan";
    plan_step.rule_name = "Cartesian vector addition";
    plan_step.explanation_short = "Check compatibility, convert to SI, then add i and j components";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.vector-add.rank-two", "both vectors have two components",
        "rank comparison", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before adding components");
    register_strategy_precondition(
        plan, plan_step, "pre.vector-add.frames-match", "both vectors use the same frame",
        "frame identity", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before adding components");
    register_strategy_precondition(
        plan, plan_step, "pre.vector-add.dimensions-match",
        "both vectors have the same physical dimension", "dimension comparison",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked before adding components");
    if (!meter.step())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool rank_ok = problem.first.rank == 2 && problem.second.rank == 2;
    const std::string rank_observed = "ranks " + std::to_string(problem.first.rank) + " and " +
                                      std::to_string(problem.second.rank);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.add.check-rank", "Check vector ranks",
                              "This archetype adds two-dimensional vectors", "obl.vector-add.rank-two",
                              "both input vectors have rank two", "rank comparison", rank_observed,
                              EvidenceStrength::StructurallyValid, rank_ok),
                   "both vectors are two-dimensional", "rank 2 and rank 2", rank_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-add.rank-two",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, rank_observed);
    if (!rank_ok) {
        result.outcome = VectorAdditionOutcome::RankMismatch;
        result.detail = "vector addition requires two vectors with two components";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const bool frame_ok = problem.first.frame == problem.second.frame;
    const std::string frame_observed = "frame " + problem.first.frame.name + " and frame " +
                                       problem.second.frame.name;
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.add.check-frame", "Check vector frames",
                              "Frames must match unless a basis transformation is explicit",
                              "obl.vector-add.frames-match", "both input vectors use the same frame",
                              "frame identity", frame_observed,
                              EvidenceStrength::StructurallyValid, frame_ok),
                   "both vectors use the same frame", "identical named frames", frame_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-add.frames-match",
        frame_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, frame_observed);
    if (!frame_ok) {
        result.outcome = VectorAdditionOutcome::FrameMismatch;
        result.detail = "cannot add vectors in frame " + problem.first.frame.name + " and frame " +
                        problem.second.frame.name + " without an explicit basis transformation";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const bool dimension_ok = problem.first.unit.dimension == problem.second.unit.dimension;
    const std::string dimension_observed = dimension_text(problem.first.unit.dimension) + " and " +
                                           dimension_text(problem.second.unit.dimension);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.add.check-dimension", "Check vector dimensions",
                              "Only quantities of the same dimension can be added",
                              "obl.vector-add.dimensions-match",
                              "both input vectors have the same physical dimension",
                              "dimension comparison", dimension_observed,
                              EvidenceStrength::DimensionallyValid, dimension_ok),
                   "both vector units have the same dimension", "equal dimensions",
                   dimension_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-add.dimensions-match",
        dimension_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimension_observed);
    if (!dimension_ok) {
        result.outcome = VectorAdditionOutcome::DimensionMismatch;
        result.detail = "cannot add " + dimension_text(problem.first.unit.dimension) + " and " +
                        dimension_text(problem.second.unit.dimension);
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    Vector first_si, second_si;
    if (!to_si(problem.first, &first_si) || !to_si(problem.second, &second_si)) {
        result.outcome = VectorAdditionOutcome::ArithmeticOverflow;
        result.detail = "a component does not fit exact arithmetic after SI conversion";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const NodeId si_inputs = arena.call("vector_add", {vector_model_node(arena, first_si),
                                                       vector_model_node(arena, second_si)});
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    const bool converted = problem.first.unit.scale.num != 1 || problem.first.unit.scale.den != 1 ||
                           problem.second.unit.scale.num != 1 ||
                           problem.second.unit.scale.den != 1;
    if (converted) {
        Step step;
        step.phase = "solve";
        step.goal = "Convert both vectors to SI";
        step.rule_id = "vec.add.convert-si";
        step.rule_name = "Exact SI conversion";
        step.explanation_short = "Apply each unit's exact scale to every component";
        step.explanation_detailed =
            "Reach for this whenever the two vectors are written in different units, or in any unit "
            "that is not the SI one. Adding a component in kilometres to one in metres would be "
            "adding two different things, so every component is put on the same scale first. The "
            "scale factors are exact, so nothing is lost here and nothing needs rounding yet.";
        step.claim = ClaimType::EquivalentExpression;
        step.verifications.push_back(
            verification("unit table", "both unit scales were applied exactly",
                         EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.physics.converts-by-table",
             "the SI form comes from the unit table's exact factor for the entered unit"});
        const std::string action = vector_text(problem.first) + " becomes " + vector_text(first_si) +
                                   ", and " + vector_text(problem.second) + " becomes " +
                                   vector_text(second_si);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), model, si_inputs, action))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    Vector sum;
    std::string why;
    if (!vector_add(problem.first, problem.second, &sum, &why)) {
        result.outcome = VectorAdditionOutcome::ArithmeticOverflow;
        result.detail = why;
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const Rational left[2] = {first_si.x, first_si.y};
    const Rational right[2] = {second_si.x, second_si.y};
    const Rational answers[2] = {sum.x, sum.y};
    const char *axes[2] = {"i", "j"};
    StepId component_parent = plan_id;
    for (size_t axis = 0; axis < 2; ++axis) {
        Step step;
        step.phase = "solve";
        step.goal = std::string("Add the ") + axes[axis] + " components";
        step.rule_id = axis == 0 ? "vec.add.component-i" : "vec.add.component-j";
        step.rule_name = "Component addition";
        step.explanation_short = "Components on the same Cartesian axis add as exact scalars";
        step.explanation_detailed =
            "Reach for this once both vectors are in the same units and you want their sum. Vectors "
            "add one axis at a time: the two i components make the answer's i component, and the "
            "two j components make its j. Each of those is ordinary addition of numbers, which is "
            "the whole reason breaking a vector into components is worth doing.";
        step.claim = ClaimType::EquivalentExpression;
        step.verifications.push_back(
            verification("exact rational addition", rational_text(answers[axis]),
                         EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.vector-add.component-sum",
             "the component written down is the exact sum of the two input components"});
        const NodeId before = arena.binary(Kind::Add, rational_node(arena, left[axis]),
                                           rational_node(arena, right[axis]));
        const NodeId after = rational_node(arena, answers[axis]);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        const std::string action = rational_text(left[axis]) + " + " + rational_text(right[axis]) +
                                   " = " + rational_text(answers[axis]);
        if (!add_transformation(derivation, meter, component_parent, std::move(step), before, after,
                                action))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    HalfPlace rounding = HalfPlace::Within;
    if (!reported_vector_text(sum, &result.value_text, rounding)) {
        result.outcome = VectorAdditionOutcome::ArithmeticOverflow;
        result.detail = "reporting the measured precision exceeds exact integer arithmetic";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    Vector exact_display = sum;
    exact_display.precision = Precision();
    std::string exact_text;
    HalfPlace exact_rounding = HalfPlace::Within;
    if (!reported_vector_text(exact_display, &exact_text, exact_rounding)) {
        result.outcome = VectorAdditionOutcome::ArithmeticOverflow;
        result.detail = "the exact vector sum cannot be formatted";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    if (rounding == HalfPlace::Outside) {
        // Refused before the step is built, so a report the comparison rejects leaves no
        // transformation to the value it rejected, and no answer the student can read as checked.
        result.outcome = VectorAdditionOutcome::VerificationFailed;
        result.detail = result.value_text +
                        " is further than half a unit in its last place from " + exact_text;
        result.value_text.clear();
        result.status = DerivationStatus::VerificationFailed;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    if (sum.precision.kind == NumberKind::Measured) {
        const bool compared = rounding == HalfPlace::Within;
        const std::string rounded_text = result.value_text;
        Step step;
        step.phase = "report";
        step.goal = "Apply final reporting precision";
        step.rule_id = "vec.add.report-precision";
        step.rule_name = "Final-only precision";
        step.explanation_short = "Round only the reported vector, after exact component addition";
        step.explanation_detailed =
            compared ? "Reach for this once, at the very end, and never partway through. A measured "
                       "value is only as good as the figures it was written with, so the answer is "
                       "reported to the fewest significant figures among the measurements it came "
                       "from. Rounding a component before adding would throw away figures the final "
                       "rounding cannot get back, which is why every step above this one keeps the "
                       "exact value."
                     : "The rounded spelling could not be read back as a decimal, so it was never "
                       "compared against the exact value. The exact value is reported instead, "
                       "since showing a rounding nothing checked would be showing an answer with "
                       "no evidence behind it.";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.vector-add.rounding-final", "precision is applied once after exact component addition"});
        step.verifications.push_back(
            verification("significant figures", std::to_string(sum.precision.significant_digits),
                         EvidenceStrength::StructurallyValid, true));
        // Both obligations whichever way the comparison went, since the schema belongs to the rule
        // rather than to the branch, and only the half-place one goes unanswered on a degrade.
        step.proof_obligations.push_back(
            {"obl.vector-add.rounding-within-half-place",
             "the reported vector is within half a unit in the last place of the exact one"});
        step.verifications.push_back(verification(
            "exact half-place comparison",
            compared ? rounded_text + " is within half a unit in the last place of " + exact_text
                     : rounded_text + " could not be read back as a decimal, so it was never "
                                      "compared against " +
                           exact_text,
            EvidenceStrength::CandidateChecked,
            compared ? VerificationOutcome::Passed : VerificationOutcome::Inconclusive));
        if (!compared)
            result.value_text = exact_text;
        const NodeId exact_vector = vector_node(arena, sum);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), exact_vector,
                                exact_vector,
                                compared ? "report " + rounded_text
                                         : "report " + exact_text + " unrounded",
                                false))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    result.value = sum;
    result.has_value = true;
    result.outcome = VectorAdditionOutcome::Solved;
    result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

}  // namespace nps
