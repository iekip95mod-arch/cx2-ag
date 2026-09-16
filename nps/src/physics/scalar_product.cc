#include "nps/physics/scalar_product.h"

#include <utility>
#include <vector>

#include "nps/core/context.h"
#include "nps/core/rational.h"

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
    return arena.call("framed_vector",
                      {vector_node(arena, value), rational_node(arena, value.unit.scale),
                       arena.symbol(value.frame.name), arena.call("dimension", exponents)});
}

// The sum of products, built from whatever stands for each component. One shape for the symbolic
// form and the substituted one, so the two cannot drift apart.
NodeId component_sum(Arena &arena, const NodeId *left, const NodeId *right, uint8_t rank) {
    NodeId sum = arena.binary(Kind::Mul, left[0], right[0]);
    for (uint8_t axis = 1; axis < rank; ++axis)
        sum = arena.binary(Kind::Add, sum, arena.binary(Kind::Mul, left[axis], right[axis]));
    return sum;
}

NodeId symbolic_equation(Arena &arena, uint8_t rank) {
    const char *left_names[3] = {"ax", "ay", "az"};
    const char *right_names[3] = {"bx", "by", "bz"};
    NodeId left[3];
    NodeId right[3];
    for (uint8_t axis = 0; axis < rank; ++axis) {
        left[axis] = arena.symbol(left_names[axis]);
        right[axis] = arena.symbol(right_names[axis]);
    }
    return arena.binary(Kind::Equals, arena.call("dot", {arena.symbol("a"), arena.symbol("b")}),
                        component_sum(arena, left, right, rank));
}

NodeId substituted_equation(Arena &arena, const Vector &first, const Vector &second) {
    const Rational left_values[3] = {first.x, first.y, first.z};
    const Rational right_values[3] = {second.x, second.y, second.z};
    NodeId left[3];
    NodeId right[3];
    for (uint8_t axis = 0; axis < first.rank; ++axis) {
        left[axis] = rational_node(arena, left_values[axis]);
        right[axis] = rational_node(arena, right_values[axis]);
    }
    return arena.binary(Kind::Equals, arena.call("dot", {arena.symbol("a"), arena.symbol("b")}),
                        component_sum(arena, left, right, first.rank));
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

Step check_step(const char *rule_id, const char *rule_name, const std::string &goal,
                const std::string &explanation, const char *obligation_id,
                const std::string &obligation, const std::string &method,
                const std::string &detail, EvidenceStrength passing, bool passed) {
    Step step;
    step.phase = "check";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
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
                    DerivationStatus status, const ScalarProductProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.vectors.cartesian-scalar-product";
    inputs.requested_method =
        problem.angle
            ? "convert components to SI, sum the component products, and read the angle from its sign"
            : "convert components to SI, sum the component products, report once";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions.push_back("first vector frame is " + problem.first.frame.name);
    inputs.active_assumptions.push_back("second vector frame is " + problem.second.frame.name);
    inputs.active_assumptions.push_back(
        "the two readings of the scalar product name one quantity");
    inputs.unit_policy = "exact SI conversion and component arithmetic, final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

ScalarProductResult halted_result(const Arena &arena, Derivation &derivation, size_t mark,
                                  Meter &meter, const Budget &budget, NodeId model,
                                  const ScalarProductProblem &problem) {
    const bool cancelled = meter.halt() == Halt::Cancelled;
    const bool kept = keep_verified_prefix(derivation, mark, arena);
    ScalarProductResult result;
    result.outcome =
        cancelled ? ScalarProductOutcome::Cancelled : ScalarProductOutcome::ResourceExceeded;
    result.detail = halt_name(meter.halt());
    result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                    : kept     ? DerivationStatus::Cancelled
                               : DerivationStatus::NotRecorded;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

ScalarProductResult arena_result(Arena &arena, Derivation &derivation, size_t mark, Meter &meter,
                                 const Budget &budget, NodeId model,
                                 const ScalarProductProblem &problem) {
    derivation.rewind_to(mark);
    ScalarProductResult result;
    result.outcome = ScalarProductOutcome::ResourceExceeded;
    result.detail = status_name(arena.status());
    result.status = DerivationStatus::ResourceLimitReached;
    result.cost = meter.cost();
    const NodeId context_model = model < arena.node_count() ? model : kNoNode;
    record_context(derivation, budget, context_model, result.status, problem);
    return result;
}

bool is_zero(const Vector &v) {
    return v.x.num == 0 && v.y.num == 0 && (v.rank != 3 || v.z.num == 0);
}

std::string component_action(const Vector &first, const Vector &second) {
    const Rational left[3] = {first.x, first.y, first.z};
    const Rational right[3] = {second.x, second.y, second.z};
    std::string action;
    for (uint8_t axis = 0; axis < first.rank; ++axis) {
        if (axis != 0)
            action += " + ";
        action += rational_text(left[axis]) + "*" + rational_text(right[axis]);
    }
    return action;
}

}  // namespace

const char *scalar_product_outcome_name(ScalarProductOutcome outcome) {
    switch (outcome) {
        case ScalarProductOutcome::Solved: return "solved";
        case ScalarProductOutcome::RankMismatch: return "rank mismatch";
        case ScalarProductOutcome::FrameMismatch: return "frame mismatch";
        case ScalarProductOutcome::DimensionMismatch: return "dimension mismatch";
        case ScalarProductOutcome::ZeroVector: return "zero vector";
        case ScalarProductOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case ScalarProductOutcome::VerificationFailed: return "verification failed";
        case ScalarProductOutcome::Cancelled: return "cancelled";
        case ScalarProductOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

const char *scalar_angle_name(ScalarAngle angle) {
    switch (angle) {
        case ScalarAngle::NotAsked: return "not asked";
        case ScalarAngle::Acute: return "acute";
        case ScalarAngle::Perpendicular: return "perpendicular";
        case ScalarAngle::Obtuse: return "obtuse";
    }
    return "unknown";
}

ScalarProductResult solve_scalar_product(Arena &arena, Derivation &derivation,
                                         const ScalarProductProblem &problem,
                                         const Budget &budget) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    const NodeId model = arena.call("dot", {vector_model_node(arena, problem.first),
                                            vector_model_node(arena, problem.second)});
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    if (meter.stopped())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    ScalarProductResult result;
    PlanPayload plan;
    plan.strategy_id = problem.angle ? "vec.dot.angle-plan" : "vec.dot.plan";
    plan.selected_strategy = "Cartesian scalar product";
    plan.matched_problem_facts.push_back("first vector: " + vector_text(problem.first));
    plan.matched_problem_facts.push_back("second vector: " + vector_text(problem.second));
    plan.selection_rationale =
        "the sum of the matching component products is the same quantity as the magnitude product "
        "times the cosine of the angle between them, and the component route is the one the given "
        "data supports exactly";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = problem.angle ? "Dot the two vectors and place the angle between them"
                                   : "Dot the two Cartesian vectors";
    // The angle asks for one precondition more than the product, and a rule schema is one fixed
    // obligation set, so the two plans are two rules.
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Cartesian scalar product";
    plan_step.explanation_short =
        "Check compatibility, convert to SI, then sum the matching component products";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.scalar-product.ranks-match",
                                   "both vectors have the same number of components",
                                   "rank comparison", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before any component is multiplied");
    register_strategy_precondition(plan, plan_step, "pre.scalar-product.frames-match",
                                   "both vectors use the same frame", "frame identity",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before any component is multiplied");
    register_strategy_precondition(plan, plan_step, "pre.scalar-product.dimension-product",
                                   "the product of the two vectors' dimensions fits the dimension "
                                   "table",
                                   "dimensional multiplication", EvidenceStrength::DimensionallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked before any component is multiplied");
    if (problem.angle) {
        register_strategy_precondition(plan, plan_step, "pre.scalar-product.nonzero",
                                       "neither vector is the zero vector", "component comparison",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::NotAttempted,
                                       "checked before the angle is placed");
    }
    if (!meter.step())
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool rank_ok = problem.first.rank == problem.second.rank;
    const std::string rank_observed = "ranks " + std::to_string(problem.first.rank) + " and " +
                                      std::to_string(problem.second.rank);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.dot.check-rank", "Scalar product precondition",
                              "Check vector ranks",
                              "Every component of one vector needs a matching component in the "
                              "other, so the two ranks have to agree",
                              "obl.scalar-product.ranks-match",
                              "both input vectors have the same number of components",
                              "rank comparison", rank_observed, EvidenceStrength::StructurallyValid,
                              rank_ok),
                   "both vectors have the same rank", "equal ranks", rank_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.scalar-product.ranks-match",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, rank_observed);
    if (!rank_ok) {
        result.outcome = ScalarProductOutcome::RankMismatch;
        result.detail =
            "cannot take the dot product of vectors with a different number of components";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    const bool frame_ok = problem.first.frame == problem.second.frame;
    const std::string frame_observed = "frame " + problem.first.frame.name + " and frame " +
                                       problem.second.frame.name;
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.dot.check-frame", "Scalar product precondition",
                              "Check vector frames",
                              "Frames must match unless a basis transformation is explicit",
                              "obl.scalar-product.frames-match",
                              "both input vectors use the same frame", "frame identity",
                              frame_observed, EvidenceStrength::StructurallyValid, frame_ok),
                   "both vectors use the same frame", "identical named frames", frame_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.scalar-product.frames-match",
        frame_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, frame_observed);
    if (!frame_ok) {
        result.outcome = ScalarProductOutcome::FrameMismatch;
        result.detail = "cannot take the dot product of vectors in frame " +
                        problem.first.frame.name + " and frame " + problem.second.frame.name +
                        " without an explicit basis transformation";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    Dimension product_dimension;
    const bool dimension_ok = dimension_multiply(problem.first.unit.dimension,
                                                 problem.second.unit.dimension, &product_dimension);
    const std::string dimension_observed = dimension_text(problem.first.unit.dimension) + " and " +
                                           dimension_text(problem.second.unit.dimension);
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.dot.check-dimension", "Scalar product precondition",
                              "Check the product dimension",
                              "Unlike a sum, the two operand dimensions need not match: the product "
                              "dimension is their product, such as a force dotted with a "
                              "displacement giving an energy",
                              "obl.scalar-product.dimension-product",
                              "the product of the two operand dimensions fits the dimension table",
                              "dimensional multiplication", dimension_observed,
                              EvidenceStrength::DimensionallyValid, dimension_ok),
                   "the product dimension fits", "a representable product dimension",
                   dimension_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    derivation.complete_plan_precondition(
        plan_id, "pre.scalar-product.dimension-product",
        dimension_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimension_observed);
    if (!dimension_ok) {
        result.outcome = ScalarProductOutcome::DimensionMismatch;
        result.detail = "the product dimension does not fit";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    if (problem.angle) {
        const bool nonzero_ok = !is_zero(problem.first) && !is_zero(problem.second);
        const std::string nonzero_observed =
            nonzero_ok ? "neither operand is the zero vector"
                       : std::string("the ") +
                             (is_zero(problem.first) ? "first" : "second") +
                             " operand is the zero vector";
        if (!add_check(derivation, meter, plan_id,
                       check_step("vec.dot.check-nonzero", "Scalar product precondition",
                                  "Check both vectors point somewhere",
                                  "The zero vector has no direction, so there is no angle between "
                                  "it and anything else",
                                  "obl.scalar-product.nonzero",
                                  "neither operand is the zero vector", "component comparison",
                                  nonzero_observed, EvidenceStrength::StructurallyValid,
                                  nonzero_ok),
                       "both operands have a direction", "a non-zero component on each side",
                       nonzero_observed))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
        derivation.complete_plan_precondition(
            plan_id, "pre.scalar-product.nonzero",
            nonzero_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed,
            nonzero_observed);
        if (!nonzero_ok) {
            result.outcome = ScalarProductOutcome::ZeroVector;
            result.detail =
                "the angle between two vectors needs both of them to point somewhere, and " +
                nonzero_observed;
            result.status = DerivationStatus::InvalidInput;
            result.cost = meter.cost();
            record_context(derivation, budget, model, result.status, problem);
            return result;
        }
    }

    const NodeId law = symbolic_equation(arena, problem.first.rank);
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    {
        Step step;
        step.phase = "solve";
        step.goal = "Write the scalar product both ways";
        step.rule_id = "vec.dot.definition";
        step.rule_name = "Scalar product definition";
        step.explanation_short =
            "a . b is a b cos(phi) and it is also the sum of the matching component products";
        step.explanation_detailed =
            "Reach for this whenever a question multiplies two vectors and wants a number rather "
            "than a vector back: work from a force and a displacement, or the angle between two "
            "directions. Chapter 3 gives the same quantity two readings. The geometric one, a b "
            "cos(phi), is the one to picture, because it says the scalar product measures how much "
            "of one vector lies along the other. The component one, ax bx + ay by + az bz, is the "
            "one to compute with, because it needs no angle and no square root. Which reading you "
            "use is decided by which data the question gave you, not by which is more correct.";
        step.claim = ClaimType::Definition;
        step.assumptions_before.push_back(
            "the two readings of the scalar product name one quantity");
        step.verifications.push_back(
            verification("checked physical-law applicability",
                         "the ranks, frames and dimensions passed",
                         EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.scalar-product.definition-after-checks",
             "the scalar product definition is applied only after its conditions pass"});
        if (!add_transformation(derivation, meter, plan_id, std::move(step), model, law,
                                "Apply a . b = a b cos(phi) = ax bx + ay by + az bz", false))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    const NodeId substituted = substituted_equation(arena, problem.first, problem.second);
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    {
        const std::string substitution =
            "a = " + vector_text(problem.first) + ", b = " + vector_text(problem.second);
        Step step;
        step.phase = "solve";
        step.goal = "Put the two declared vectors into the component form";
        step.rule_id = "vec.dot.substitute";
        step.rule_name = "Substitution";
        step.explanation_short = "Replace each component symbol by the number the problem gave";
        step.explanation_detailed =
            "The component form is the one the given data supports, so it is the one the numbers go "
            "into. Pairing the components up while they are still named is what stops an x being "
            "multiplied by a y, which is the one mistake this arithmetic invites.";
        step.claim = ClaimType::SolutionSetPreserved;
        step.verifications.push_back(verification("typed known-quantity lookup", substitution,
                                                  EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        if (!add_transformation(derivation, meter, plan_id, std::move(step), law, substituted,
                                "Substitute " + substitution))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    Vector first_si;
    Vector second_si;
    if (!to_si(problem.first, &first_si) || !to_si(problem.second, &second_si)) {
        result.outcome = ScalarProductOutcome::ArithmeticOverflow;
        result.detail = "a component does not fit exact arithmetic after SI conversion";
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    const NodeId si_equation = substituted_equation(arena, first_si, second_si);
    if (arena.failed())
        return arena_result(arena, derivation, mark, meter, budget, model, problem);
    const bool converted = problem.first.unit.scale.num != 1 || problem.first.unit.scale.den != 1 ||
                           problem.second.unit.scale.num != 1 ||
                           problem.second.unit.scale.den != 1;
    if (converted) {
        Step step;
        step.phase = "solve";
        step.goal = "Convert both vectors to SI";
        step.rule_id = "vec.dot.convert-si";
        step.rule_name = "Exact SI conversion";
        step.explanation_short = "Apply each unit's exact scale to every component";
        step.explanation_detailed =
            "Reach for this whenever the two vectors are written in different units, or in any unit "
            "that is not the SI one. Multiplying a length in centimetres by a force in newtons "
            "would give a number belonging to neither, so every component goes onto the same scale "
            "first. The scale factors are exact, so nothing is lost here and nothing is rounded "
            "yet.";
        step.claim = ClaimType::EquivalentExpression;
        step.verifications.push_back(verification("unit table",
                                                  "both unit scales were applied exactly",
                                                  EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.physics.converts-by-table",
             "the SI form comes from the unit table's exact factor for the entered unit"});
        const std::string action = vector_text(problem.first) + " becomes " +
                                   vector_text(first_si) + ", and " +
                                   vector_text(problem.second) + " becomes " +
                                   vector_text(second_si);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), substituted,
                                si_equation, action))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    Quantity product;
    std::string why;
    if (!vector_dot(problem.first, problem.second, &product, &why)) {
        result.outcome = ScalarProductOutcome::ArithmeticOverflow;
        result.detail = why;
        result.status = DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }
    product.unit.text = si_unit_text(product.unit.dimension);
    {
        Step step;
        step.phase = "solve";
        step.goal = "Add the component products up";
        step.rule_id = "vec.dot.component-sum";
        step.rule_name = "Component sum";
        step.explanation_short = "Multiply each pair of matching components and add the results";
        step.explanation_detailed =
            "Each pair of matching components is multiplied and the products are added. The axes "
            "are perpendicular, so no pair of different axes contributes anything, which is exactly "
            "why the sum is this short. What comes out is one number rather than a vector, and that "
            "is what makes this the scalar product.";
        step.claim = ClaimType::EquivalentExpression;
        step.verifications.push_back(verification("nps vector_dot", rational_text(product.value),
                                                  EvidenceStrength::StructurallyValid, true));
        step.proof_obligations.push_back(
            {"obl.scalar-product.sum-is-the-definition",
             "the value written down is the component sum the definition names"});
        const NodeId value_node = rational_node(arena, product.value);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), si_equation,
                                arena.binary(Kind::Equals,
                                             arena.call("dot", {arena.symbol("a"),
                                                                arena.symbol("b")}),
                                             value_node),
                                component_action(first_si, second_si) + " = " +
                                    rational_text(product.value)))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    Quantity reversed;
    std::string reversed_error;
    const bool reversed_ok = vector_dot(problem.second, problem.first, &reversed, &reversed_error);
    const bool commutative = reversed_ok && rational_equal(reversed.value, product.value);
    const std::string commutative_observed =
        commutative ? "b . a equals a . b exactly"
                    : "b . a did not equal a . b exactly";
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.dot.check-commutative", "Scalar product check",
                              "Check that the order does not matter",
                              "Each term is a product of two numbers, so swapping the operands "
                              "cannot change the sum",
                              "obl.scalar-product.commutative",
                              "dotting the operands in the opposite order gives the same value",
                              "exact reversed dot product", commutative_observed,
                              EvidenceStrength::CandidateChecked, commutative),
                   "the reversed dot product equals this one", "exact equality",
                   commutative_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);

    // The geometric reading made checkable without a square root. a b cos(phi) has |cos(phi)| at
    // most one, which is exactly (a . b)^2 <= (a . a)(b . b), and every term of that is exact.
    Quantity first_self;
    Quantity second_self;
    Rational product_square;
    Rational self_product;
    Rational slack;
    const bool bound_computed =
        vector_dot(problem.first, problem.first, &first_self, &why) &&
        vector_dot(problem.second, problem.second, &second_self, &why) &&
        rational_mul(product.value, product.value, &product_square) &&
        rational_mul(first_self.value, second_self.value, &self_product) &&
        rational_sub(self_product, product_square, &slack);
    const bool bound_ok = bound_computed && slack.num >= 0;
    const std::string bound_observed =
        bound_computed ? rational_text(product_square) + " against " + rational_text(self_product)
                       : "the comparison exceeds exact integer arithmetic";
    if (!add_check(derivation, meter, plan_id,
                   check_step("vec.dot.check-magnitude-bound", "Scalar product check",
                              "Check the value against the geometric reading",
                              "a b cos(phi) can never exceed a b in size, so the square of the "
                              "product can never exceed the product of the two self-products",
                              "obl.scalar-product.within-magnitude-bound",
                              "the square of the scalar product is at most the product of the two "
                              "self-products",
                              "exact Cauchy-Schwarz comparison", bound_observed,
                              EvidenceStrength::CandidateChecked, bound_ok),
                   "the component value agrees with the geometric reading",
                   "(a . b)^2 at most (a . a)(b . b)", bound_observed))
        return halted_result(arena, derivation, mark, meter, budget, model, problem);
    if (!bound_ok) {
        result.outcome = ScalarProductOutcome::VerificationFailed;
        result.detail = "the component sum exceeds the bound its geometric reading puts on it: " +
                        bound_observed;
        result.status = DerivationStatus::VerificationFailed;
        result.cost = meter.cost();
        record_context(derivation, budget, model, result.status, problem);
        return result;
    }

    if (problem.angle) {
        result.angle = product.value.num > 0   ? ScalarAngle::Acute
                       : product.value.num < 0 ? ScalarAngle::Obtuse
                                               : ScalarAngle::Perpendicular;
        result.angle_text = result.angle == ScalarAngle::Acute
                                ? "the angle between them is less than a right angle"
                            : result.angle == ScalarAngle::Obtuse
                                ? "the angle between them is more than a right angle"
                                : "the two vectors are perpendicular";
        if (!add_check(derivation, meter, plan_id,
                       check_step("vec.dot.interpret-angle", "Scalar product reading",
                                  "Place the angle between the two vectors",
                                  "The magnitudes are both positive, so the sign of the scalar "
                                  "product is the sign of cos(phi) and nothing else",
                                  "obl.scalar-product.angle-from-sign",
                                  "the sign of the scalar product places the angle against a right "
                                  "angle",
                                  "exact sign comparison",
                                  rational_text(product.value) + ", so " + result.angle_text,
                                  EvidenceStrength::CandidateChecked, true),
                       "the angle is placed against a right angle",
                       "positive is acute, zero is perpendicular, negative is obtuse",
                       scalar_angle_name(result.angle)))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    result.value_text = rational_text(product.value) + " " + product.unit.text;
    if (product.precision.kind == NumberKind::Measured) {
        std::string rounded;
        if (!precision_rounded_text(product.value, product.precision, &rounded)) {
            result.outcome = ScalarProductOutcome::ArithmeticOverflow;
            result.detail = "reporting the measured precision exceeds exact integer arithmetic";
            result.status = DerivationStatus::ResourceLimitReached;
            result.cost = meter.cost();
            record_context(derivation, budget, model, result.status, problem);
            return result;
        }
        const HalfPlace checked =
            precision_rounding_valid(product.value, rounded, product.precision);
        if (checked == HalfPlace::Outside) {
            result.outcome = ScalarProductOutcome::VerificationFailed;
            result.detail = rounded + " is further than half a unit in its last place from " +
                            rational_text(product.value);
            result.status = DerivationStatus::VerificationFailed;
            result.cost = meter.cost();
            record_context(derivation, budget, model, result.status, problem);
            return result;
        }
        const bool compared = checked == HalfPlace::Within;
        Step step;
        step.phase = "report";
        step.goal = "Apply final reporting precision";
        step.rule_id = "vec.dot.report-precision";
        step.rule_name = "Final-only precision";
        step.explanation_short = "Round only the reported value, after the exact component sum";
        step.explanation_detailed =
            compared ? "Reach for this once, at the very end, and never partway through. A measured "
                       "value is only as good as the figures it was written with, so the answer is "
                       "reported to the precision the measurements it came from support. Rounding a "
                       "component before multiplying would throw away figures the final rounding "
                       "cannot get back."
                     : "The rounded spelling could not be read back as a decimal, so it was never "
                       "compared against the exact value. The exact value is reported instead, "
                       "since showing a rounding nothing checked would be showing an answer with no "
                       "evidence behind it.";
        step.claim = ClaimType::NoClaim;
        step.proof_obligations.push_back(
            {"obl.physics.reported-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        step.verifications.push_back(verification(
            "exact comparison against the unrounded value",
            compared ? rounded + " is within half a unit in the last place of " +
                           rational_text(product.value)
                     : rounded + " could not be read back as a decimal, so it was never compared "
                                 "against " +
                           rational_text(product.value),
            EvidenceStrength::CandidateChecked,
            compared ? VerificationOutcome::Passed : VerificationOutcome::Inconclusive));
        if (compared)
            result.value_text = rounded + " " + product.unit.text;
        const NodeId exact_value = rational_node(arena, product.value);
        if (arena.failed())
            return arena_result(arena, derivation, mark, meter, budget, model, problem);
        if (!add_transformation(derivation, meter, plan_id, std::move(step), exact_value,
                                exact_value, "report " + result.value_text, false))
            return halted_result(arena, derivation, mark, meter, budget, model, problem);
    }

    result.value = product;
    result.has_value = true;
    result.outcome = ScalarProductOutcome::Solved;
    result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

}  // namespace nps
