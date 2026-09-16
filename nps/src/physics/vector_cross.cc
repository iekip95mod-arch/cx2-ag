#include "nps/physics/vector_cross.h"

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
    int powers[kDimensionCount];
    dimension_powers(value.unit.dimension, powers);
    std::vector<NodeId> exponents;
    for (int power : powers)
        exponents.push_back(arena.integer(integer_text(power)));
    NodeId dimension = arena.call("dimension", exponents);
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
    step.rule_name = "Vector cross product precondition";
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
                    DerivationStatus status, const VectorCrossProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.vectors.cartesian-cross-product.three-dimension";
    inputs.requested_method =
        "convert components to SI, form the determinant cross product, report once";
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

VectorCrossResult halted_result(const Arena &arena, Derivation &derivation, size_t mark,
                                Meter &meter, const Budget &budget, NodeId model,
                                const VectorCrossProblem &problem) {
    const bool cancelled = meter.halt() == Halt::Cancelled;
    const bool kept = keep_verified_prefix(derivation, mark, arena);
    VectorCrossResult result;
    result.outcome = cancelled ? VectorCrossOutcome::Cancelled : VectorCrossOutcome::ResourceExceeded;
    result.detail = halt_name(meter.halt());
    result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                    : kept     ? DerivationStatus::Cancelled
                               : DerivationStatus::NotRecorded;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

VectorCrossResult arena_result(Arena &arena, Derivation &derivation, size_t mark, Meter &meter,
                               const Budget &budget, NodeId model,
                               const VectorCrossProblem &problem) {
    derivation.rewind_to(mark);
    VectorCrossResult result;
    result.outcome = VectorCrossOutcome::ResourceExceeded;
    result.detail = status_name(arena.status());
    result.status = DerivationStatus::ResourceLimitReached;
    result.cost = meter.cost();
    const NodeId context_model = model < arena.node_count() ? model : kNoNode;
    record_context(derivation, budget, context_model, result.status, problem);
    return result;
}

}  // namespace

const char *vector_cross_outcome_name(VectorCrossOutcome outcome) {
    switch (outcome) {
        case VectorCrossOutcome::Solved: return "solved";
        case VectorCrossOutcome::RankMismatch: return "rank mismatch";
        case VectorCrossOutcome::FrameMismatch: return "frame mismatch";
        case VectorCrossOutcome::DimensionMismatch: return "dimension mismatch";
        case VectorCrossOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case VectorCrossOutcome::VerificationFailed: return "verification failed";
        case VectorCrossOutcome::Cancelled: return "cancelled";
        case VectorCrossOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

VectorCrossResult solve_vector_cross(Arena &arena, Derivation &derivation,
                                     const VectorCrossProblem &problem, const Budget &budget) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    NodeId model = arena.call("vector_cross", {vector_model_node(arena, problem.first),
                                               vector_model_node(arena, problem.second)});
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    if (meter.stopped())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    VectorCrossResult result;
    PlanPayload plan;
    plan.strategy_id = "vec.cross.plan";
    plan.selected_strategy = "Cartesian vector cross product";
    plan.matched_problem_facts.push_back("first vector: " + vector_text(problem.first));
    plan.matched_problem_facts.push_back("second vector: " + vector_text(problem.second));
    plan.selection_rationale =
        "the determinant expansion after exact SI conversion gives an axis vector orthogonal to both "
        "operands";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Cross the two Cartesian vectors";
    plan_step.rule_id = "vec.cross.plan";
    plan_step.rule_name = "Cartesian vector cross product";
    plan_step.explanation_short =
        "Check compatibility, convert to SI, then expand the determinant into three components";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.vector-cross.rank-three", "both vectors have exactly rank three",
        "rank comparison", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before crossing components");
    register_strategy_precondition(
        plan, plan_step, "pre.vector-cross.frames-match", "both vectors use the same frame",
        "frame identity", EvidenceStrength::StructurallyValid,
        VerificationOutcome::NotAttempted, "checked before crossing components");
    register_strategy_precondition(
        plan, plan_step, "pre.vector-cross.dimension-product",
        "the product of the two vectors' dimensions fits the dimension table", "dimension product",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked before crossing components");
    if (!meter.step())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool rank_ok = problem.first.rank == 3 && problem.second.rank == 3;
    const std::string rank_observed = "ranks " + std::to_string(problem.first.rank) + " and " +
                                      std::to_string(problem.second.rank);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.cross.check-rank", "Check vector ranks",
                              "The cross product is defined on two three-dimensional vectors, so both "
                              "operands need all three axes",
                              "obl.vector-cross.rank-three", "both input vectors have exactly rank three",
                              "rank comparison", rank_observed,
                              EvidenceStrength::StructurallyValid, rank_ok),
                   "both vectors are rank three", "rank 3", rank_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-cross.rank-three",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, rank_observed);
    if (!rank_ok) {
        result.outcome = VectorCrossOutcome::RankMismatch;
        result.detail = "the cross product is defined on three components and one of these has two";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const bool frame_ok = problem.first.frame == problem.second.frame;
    const std::string frame_observed = "frame " + problem.first.frame.name + " and frame " +
                                       problem.second.frame.name;
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.cross.check-frame", "Check vector frames",
                              "Frames must match unless a basis transformation is explicit",
                              "obl.vector-cross.frames-match", "both input vectors use the same frame",
                              "frame identity", frame_observed,
                              EvidenceStrength::StructurallyValid, frame_ok),
                   "both vectors use the same frame", "identical named frames", frame_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-cross.frames-match",
        frame_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, frame_observed);
    if (!frame_ok) {
        result.outcome = VectorCrossOutcome::FrameMismatch;
        result.detail = "cannot cross vectors in frame " + problem.first.frame.name + " and frame " +
                        problem.second.frame.name + " without an explicit basis transformation";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    Dimension product_dimension;
    const bool dimension_ok =
        dimension_multiply(problem.first.unit.dimension, problem.second.unit.dimension, &product_dimension);
    const std::string dimension_observed = dimension_text(problem.first.unit.dimension) + " and " +
                                           dimension_text(problem.second.unit.dimension);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.cross.check-dimension", "Check the product dimension",
                              "Unlike a sum, the two operand dimensions need not match: the product "
                              "dimension is their product, such as a force crossed with a distance "
                              "giving a torque",
                              "obl.vector-cross.dimension-product",
                              "the product of the two operand dimensions fits the dimension table",
                              "dimension product", dimension_observed,
                              EvidenceStrength::DimensionallyValid, dimension_ok),
                   "the product dimension fits", "a representable product dimension",
                   dimension_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.vector-cross.dimension-product",
        dimension_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, dimension_observed);
    if (!dimension_ok) {
        result.outcome = VectorCrossOutcome::DimensionMismatch;
        result.detail = "the product dimension does not fit";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    Vector first_si, second_si;
    if (!to_si(problem.first, &first_si) || !to_si(problem.second, &second_si)) {
        result.outcome = VectorCrossOutcome::ArithmeticOverflow;
        result.detail = "a component does not fit exact arithmetic after SI conversion";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const NodeId si_inputs = arena.call("vector_cross", {vector_model_node(arena, first_si),
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
        step.rule_id = "vec.cross.convert-si";
        step.rule_name = "Exact SI conversion";
        step.explanation_short = "Apply each unit's exact scale to every component";
        step.explanation_detailed =
            "Reach for this whenever the two vectors are written in different units, or in any unit "
            "that is not the SI one. The determinant expansion below assumes both operands are already "
            "on the same scale, so every component is put in SI first. The scale factors are exact, so "
            "nothing is lost here and nothing needs rounding yet.";
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

    Vector cross;
    std::string why;
    if (!vector_cross(problem.first, problem.second, &cross, &why)) {
        result.outcome = VectorCrossOutcome::ArithmeticOverflow;
        result.detail = why;
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const char *axes[3] = {"i", "j", "k"};
    const char *component_rules[3] = {"vec.cross.component-i", "vec.cross.component-j",
                                      "vec.cross.component-k"};
    const char *left_labels[3] = {"ay", "az", "ax"};
    const char *right_labels[3] = {"bz", "bx", "by"};
    const char *neg_left_labels[3] = {"az", "ax", "ay"};
    const char *neg_right_labels[3] = {"by", "bz", "bx"};
    const Rational first_left[3] = {first_si.y, first_si.z, first_si.x};
    const Rational first_right[3] = {second_si.z, second_si.x, second_si.y};
    const Rational second_left[3] = {first_si.z, first_si.x, first_si.y};
    const Rational second_right[3] = {second_si.y, second_si.z, second_si.x};
    const Rational answers[3] = {cross.x, cross.y, cross.z};
    StepId component_parent = plan_id;
    for (size_t axis = 0; axis < 3; ++axis) {
        Step step;
        step.phase = "solve";
        step.goal = std::string("Expand the ") + axes[axis] + " determinant term";
        step.rule_id = component_rules[axis];
        step.rule_name = "Determinant component expansion";
        step.explanation_short = "Each axis is the difference of the two off-axis products";
        step.explanation_detailed =
            "Reach for this once both vectors are in SI and you want their cross product. Every axis "
            "of a cross product is the difference of two products taken from the other two axes: the "
            "i component is ay*bz minus az*by, and the j and k components cycle the same pattern "
            "around the other axes. Each of those is ordinary exact multiplication and subtraction, "
            "which is the whole reason the determinant form is worth writing down.";
        step.claim = ClaimType::EquivalentExpression;
        step.verifications.push_back(
            verification("exact rational arithmetic", rational_text(answers[axis]),
                         EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.vector-cross.component-cross",
             "the component written down is the exact determinant expansion of the two input "
             "components"});
        const NodeId term_a =
            arena.binary(Kind::Mul, rational_node(arena, first_left[axis]),
                        rational_node(arena, first_right[axis]));
        const NodeId term_b =
            arena.binary(Kind::Mul, rational_node(arena, second_left[axis]),
                        rational_node(arena, second_right[axis]));
        const NodeId before = arena.binary(Kind::Add, term_a, arena.unary(Kind::Neg, term_b));
        const NodeId after = rational_node(arena, answers[axis]);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        const std::string action = std::string(left_labels[axis]) + "*" + right_labels[axis] + " - " +
                                   neg_left_labels[axis] + "*" + neg_right_labels[axis] + " = " +
                                   rational_text(answers[axis]);
        if (!add_transformation(derivation, meter, component_parent, std::move(step), before, after,
                                action))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    Quantity dot_a, dot_b;
    std::string dot_error;
    const bool dot_a_ok = vector_dot(cross, first_si, &dot_a, &dot_error);
    const bool dot_b_ok = vector_dot(cross, second_si, &dot_b, &dot_error);
    const bool orthogonal_first = dot_a_ok && dot_a.value.num == 0;
    const bool orthogonal_second = dot_b_ok && dot_b.value.num == 0;
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.cross.check-orthogonal-first", "Check orthogonality to the first operand",
                              "The cross product is defined to be perpendicular to both inputs, so its "
                              "exact dot product with the first operand must be exactly zero",
                              "obl.vector-cross.orthogonal-first",
                              "the result vector is orthogonal to the first operand", "exact dot product",
                              orthogonal_first ? "0" : rational_text(dot_a.value),
                              EvidenceStrength::CandidateChecked, orthogonal_first),
                   "the result is orthogonal to the first operand", "exact dot product equal to zero",
                   orthogonal_first ? "0" : rational_text(dot_a.value)))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.cross.check-orthogonal-second", "Check orthogonality to the second operand",
                              "The same perpendicularity holds against the second operand",
                              "obl.vector-cross.orthogonal-second",
                              "the result vector is orthogonal to the second operand", "exact dot product",
                              orthogonal_second ? "0" : rational_text(dot_b.value),
                              EvidenceStrength::CandidateChecked, orthogonal_second),
                   "the result is orthogonal to the second operand", "exact dot product equal to zero",
                   orthogonal_second ? "0" : rational_text(dot_b.value)))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    Vector reversed;
    std::string reversed_error;
    const bool reversed_ok = vector_cross(problem.second, problem.first, &reversed, &reversed_error);
    const bool anticommutative = reversed_ok && reversed.x.num == -cross.x.num &&
                                 reversed.x.den == cross.x.den && reversed.y.num == -cross.y.num &&
                                 reversed.y.den == cross.y.den && reversed.z.num == -cross.z.num &&
                                 reversed.z.den == cross.z.den;
    const std::string anticommutative_observed =
        anticommutative ? "b cross a equals the negation of a cross b"
                        : "b cross a did not negate a cross b exactly";
    if (!add_check(
            derivation, meter, plan_id,
            check_step("vec.cross.check-anticommutative", "Check anticommutativity",
                      "Swapping the operand order must negate every component exactly",
                      "obl.vector-cross.anticommutative",
                      "crossing the operands in the opposite order negates the result exactly",
                      "exact reversed cross product", anticommutative_observed,
                      EvidenceStrength::CandidateChecked, anticommutative),
            "the reversed cross product negates this result", "exact componentwise negation",
            anticommutative_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    HalfPlace rounding = HalfPlace::Within;
    if (!reported_vector_text(cross, &result.value_text, rounding)) {
        result.outcome = VectorCrossOutcome::ArithmeticOverflow;
        result.detail = "reporting the measured precision exceeds exact integer arithmetic";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    Vector exact_display = cross;
    exact_display.precision = Precision();
    std::string exact_text;
    HalfPlace exact_rounding = HalfPlace::Within;
    if (!reported_vector_text(exact_display, &exact_text, exact_rounding)) {
        result.outcome = VectorCrossOutcome::ArithmeticOverflow;
        result.detail = "the exact cross product cannot be formatted";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    if (rounding == HalfPlace::Outside) {
        result.outcome = VectorCrossOutcome::VerificationFailed;
        result.detail = result.value_text +
                        " is further than half a unit in its last place from " + exact_text;
        result.value_text.clear();
        result.status = DerivationStatus::VerificationFailed;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    if (cross.precision.kind == NumberKind::Measured) {
        const bool compared = rounding == HalfPlace::Within;
        const std::string rounded_text = result.value_text;
        Step step;
        step.phase = "report";
        step.goal = "Apply final reporting precision";
        step.rule_id = "vec.cross.report-precision";
        step.rule_name = "Final-only precision";
        step.explanation_short = "Round only the reported vector, after exact determinant expansion";
        step.explanation_detailed =
            compared ? "Reach for this once, at the very end, and never partway through. A measured "
                       "value is only as good as the figures it was written with, so the answer is "
                       "reported to the fewest significant figures among the measurements it came "
                       "from. Rounding a component before crossing would throw away figures the final "
                       "rounding cannot get back, which is why every step above this one keeps the "
                       "exact value."
                     : "The rounded spelling could not be read back as a decimal, so it was never "
                       "compared against the exact value. The exact value is reported instead, "
                       "since showing a rounding nothing checked would be showing an answer with "
                       "no evidence behind it.";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.vector-cross.rounding-final", "precision is applied once after exact component cross"});
        step.verifications.push_back(
            verification("significant figures", std::to_string(cross.precision.significant_digits),
                         EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.vector-cross.rounding-within-half-place",
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
        const NodeId exact_vector = vector_node(arena, cross);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), exact_vector, exact_vector,
                                compared ? "report " + rounded_text
                                         : "report " + exact_text + " unrounded",
                                false))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    result.value = cross;
    result.has_value = true;
    result.outcome = VectorCrossOutcome::Solved;
    result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

}  // namespace nps
