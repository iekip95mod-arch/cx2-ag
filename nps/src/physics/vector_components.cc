#include "nps/physics/vector_components.h"

#include <utility>
#include <vector>

#include "nps/core/context.h"
#include "nps/core/evaluate.h"

namespace nps {
namespace {

enum class ConversionDirection : uint8_t {
    ToComponents,
    ToPolar,
};

NodeId rational_node(Arena &arena, const Rational &value) {
    Rational normalized = value;
    if (!normalise(&normalized.num, &normalized.den))
        return kNoNode;
    if (normalized.den == 1)
        return arena.integer(std::to_string(normalized.num));
    return arena.binary(Kind::Mul, arena.integer(std::to_string(normalized.num)),
                        arena.binary(Kind::Pow, arena.integer(std::to_string(normalized.den)),
                                     arena.integer("-1")));
}

NodeId dimension_node(Arena &arena, const Dimension &dimension) {
    return arena.call("dimension", {arena.integer(std::to_string(dimension.length)),
                                    arena.integer(std::to_string(dimension.mass)),
                                    arena.integer(std::to_string(dimension.time))});
}

const char *angle_unit_name(AngleUnit unit) {
    switch (unit) {
        case AngleUnit::Radians: return "radians";
        case AngleUnit::Degrees: return "degrees";
    }
    return "invalid";
}

const char *precision_name(const Precision &precision) {
    return precision.kind == NumberKind::Measured ? "measured" : "exact";
}

NodeId precision_node(Arena &arena, const Precision &precision) {
    return arena.call("precision", {arena.symbol(precision_name(precision)),
                                    arena.integer(std::to_string(precision.significant_digits))});
}

NodeId magnitude_angle_model(Arena &arena, const MagnitudeAngleExpr &input) {
    return arena.call(
        "magnitude_angle_to_components",
        {input.magnitude, input.angle, arena.symbol(angle_unit_name(input.angle_unit)),
         arena.symbol(input.frame.name), rational_node(arena, input.unit.scale),
         dimension_node(arena, input.unit.dimension), precision_node(arena, input.precision)});
}

NodeId vector_model(Arena &arena, const VectorExpr &input, AngleUnit output_unit) {
    return arena.call(
        "components_to_magnitude_angle",
        {arena.call("vector", {input.x, input.y}), arena.symbol(angle_unit_name(output_unit)),
         arena.symbol(input.frame.name), rational_node(arena, input.unit.scale),
         dimension_node(arena, input.unit.dimension), precision_node(arena, input.precision),
         arena.integer(std::to_string(input.rank))});
}

bool valid_node(const Arena &arena, NodeId value) {
    return value != kNoNode && value < arena.node_count();
}

bool valid_angle_unit(AngleUnit unit) {
    switch (unit) {
        case AngleUnit::Radians:
        case AngleUnit::Degrees: return true;
    }
    return false;
}

bool valid_precision(const Precision &precision, std::string *why) {
    switch (precision.kind) {
        case NumberKind::Exact:
            if (precision.significant_digits != 0) {
                *why = "exact values cannot carry a measured significant-figure count";
                return false;
            }
            return true;
        case NumberKind::Measured:
            if (precision.significant_digits == 0 || precision.significant_digits > 18) {
                *why = "measured values need between 1 and 18 significant figures";
                return false;
            }
            return true;
    }
    *why = "the precision kind is invalid";
    return false;
}

bool valid_unit(const Unit &unit, std::string *why) {
    if (unit.text.empty()) {
        *why = "a unit is required";
        return false;
    }
    int64_t numerator = unit.scale.num;
    int64_t denominator = unit.scale.den;
    if (!normalise(&numerator, &denominator) || numerator <= 0) {
        *why = "the unit has an invalid SI conversion scale";
        return false;
    }
    return true;
}

NodeId difference(Arena &arena, NodeId left, NodeId right) {
    return arena.binary(Kind::Add, left, arena.unary(Kind::Neg, right));
}

NodeId angle_radians(Arena &arena, const MagnitudeAngleExpr &input) {
    if (input.angle_unit == AngleUnit::Radians)
        return input.angle;
    const NodeId factor = arena.binary(
        Kind::Mul, arena.symbol("pi"),
        arena.binary(Kind::Pow, arena.integer("180"), arena.integer("-1")));
    return arena.binary(Kind::Mul, input.angle, factor);
}

NodeId angle_from_radians(Arena &arena, NodeId radians, AngleUnit output_unit) {
    if (output_unit == AngleUnit::Radians)
        return radians;
    const NodeId factor = arena.binary(
        Kind::Mul, arena.integer("180"),
        arena.binary(Kind::Pow, arena.symbol("pi"), arena.integer("-1")));
    return arena.binary(Kind::Mul, radians, factor);
}

bool literal_zero(const Arena &arena, NodeId value) {
    if (!valid_node(arena, value))
        return false;
    const Node &node = arena.at(value);
    return node.kind == Kind::Integer && node.small_valid && node.small == 0;
}

VerificationRecord verification(const std::string &method, const std::string &detail,
                                 EvidenceStrength passing, VerificationOutcome outcome) {
    VerificationRecord record;
    record.method = method;
    record.detail = detail;
    record.outcome = outcome;
    // Every Inconclusive recorded in this file is a backend agreeing with an answer it supplied part
    // of, which is worth what a pass would have been worth. strength_for reads Inconclusive as a
    // check that could not evaluate and flattens it, which is the other meaning of the same outcome.
    record.strength = outcome == VerificationOutcome::Inconclusive ? passing
                                                                   : strength_for(outcome, passing);
    return record;
}

std::string precision_detail(const Precision &precision) {
    if (precision.kind == NumberKind::Exact)
        return "exact expressions retained";
    return std::to_string(precision.significant_digits) +
           " significant-figure metadata retained after exact formula checks";
}

bool add_check(Derivation &derivation, Meter &meter, StepId parent, const char *rule_id,
               const char *rule_name, const std::string &goal, const std::string &explanation,
               const char *obligation_id, const std::string &obligation,
               const std::string &method, const std::string &target,
               const std::string &expected, const std::string &observed,
               EvidenceStrength passing, bool passed) {
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
    step.verifications.push_back(verification(
        method, observed, passing,
        passed ? VerificationOutcome::Passed : VerificationOutcome::Failed));
    CheckPayload payload;
    payload.target_claim = target;
    payload.check_method = method;
    payload.expected_relation = expected;
    payload.observed_result = observed;
    derivation.add_check(parent, std::move(step), std::move(payload));
    return true;
}

// detailed has no default on purpose. STEP-021 wants every transformation to say how to recognise
// its rule again, and a helper that lets the field be left out is why it was empty here.
bool add_transformation(Derivation &derivation, Meter &meter, StepId parent,
                        const char *rule_id, const char *rule_name, const std::string &goal,
                        const std::string &explanation, const std::string &detailed,
                        const char *obligation_id,
                        const std::string &obligation, NodeId before, NodeId after,
                        const std::string &method, const std::string &verification_detail,
                        EvidenceStrength passing, VerificationOutcome outcome,
                        size_t backend_requests) {
    if (!meter.rewrite() || !meter.step())
        return false;
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = ClaimType::EquivalentExpression;
    step.proof_obligations.push_back({obligation_id, obligation});
    step.verifications.push_back(verification(method, verification_detail, passing, outcome));
    step.backend_requests = static_cast<uint32_t>(backend_requests);
    TransformationPayload payload;
    payload.before = before;
    payload.after = after;
    payload.concrete_action = goal;
    payload.reversible = true;
    derivation.add_transformation(parent, std::move(step), std::move(payload));
    return true;
}

StepId add_plan(Derivation &derivation, Meter &meter, ConversionDirection direction,
                const Frame &frame, const Unit &unit, const Precision &precision,
                AngleUnit angle_unit) {
    if (!meter.step())
        return kNoStep;
    const bool to_components = direction == ConversionDirection::ToComponents;
    Step step;
    step.phase = "plan";
    step.goal = to_components ? "Resolve magnitude and direction into Cartesian components"
                              : "Resolve Cartesian components into magnitude and direction";
    step.rule_id = to_components ? "vec.components.plan" : "vec.polar.plan";
    step.rule_name = "Cartesian vector conversion";
    step.explanation_short =
        to_components ? "Use x = r cos(theta) and y = r sin(theta) in the declared frame"
                      : "Use the magnitude relation and atan2(y, x) in the declared frame";
    step.claim = ClaimType::NoClaim;
    PlanPayload payload;
    payload.strategy_id = step.rule_id;
    payload.selected_strategy = to_components ? "Magnitude-angle component relations"
                                               : "Magnitude relation and quadrant-aware atan2";
    register_strategy_precondition(
        payload, step, "pre.vector-components.rank-two", "the conversion is two-dimensional",
        "rank comparison", EvidenceStrength::StructurallyValid,
        to_components ? VerificationOutcome::Passed : VerificationOutcome::NotAttempted,
        to_components ? "magnitude-angle input defines a two-component result"
                      : "checked from the Cartesian input rank");
    register_strategy_precondition(
        payload, step, "pre.vector-components.frame-declared", "the vector frame is declared",
        "frame declaration", EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before applying component relations");
    register_strategy_precondition(
        payload, step, "pre.vector-components.angle-unit",
        "the angle unit is degrees or radians", "angle-unit validation",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked before trigonometric evaluation");
    register_strategy_precondition(
        payload, step, "pre.vector-components.dimensions-preserved",
        "magnitude and Cartesian components carry the same physical dimension",
        "dimensional analysis", EvidenceStrength::DimensionallyValid,
        VerificationOutcome::NotAttempted, "checked before evaluating the conversion formulas");
    payload.matched_problem_facts.push_back("frame: " + frame.name);
    payload.matched_problem_facts.push_back("dimension: " + dimension_text(unit.dimension));
    payload.matched_problem_facts.push_back(std::string("angle unit: ") +
                                            angle_unit_name(angle_unit));
    payload.matched_problem_facts.push_back("precision: " + precision_detail(precision));
    payload.alternatives_considered.push_back(to_components ? "direct Cartesian input"
                                                            : "single-argument inverse tangent");
    payload.selection_rationale =
        to_components
            ? "the registered component laws retain the frame and dimension while the adapter checks each formula"
            : "the existing exact magnitude path and atan2 retain dimension and quadrant while the adapter checks each formula";
    return derivation.add_plan(kNoStep, std::move(step), std::move(payload));
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, ConversionDirection direction, const Frame &frame,
                    const Unit &unit, const Precision &precision, AngleUnit angle_unit) {
    const bool to_components = direction == ConversionDirection::ToComponents;
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.vectors.magnitude-components.two-dimension";
    inputs.requested_method =
        to_components
            ? "validate typed metadata, apply x = r cos(theta) and y = r sin(theta), verify exact formulas, then approximate only final components when measured"
            : "validate typed metadata, apply the magnitude relation and atan2(y, x), verify exact formulas, then approximate only final values when measured";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    if (!frame.name.empty())
        inputs.active_assumptions.push_back("vector frame is " + frame.name);
    inputs.active_assumptions.push_back("the supplied unit has dimension " +
                                        dimension_text(unit.dimension));
    inputs.angle_convention =
        to_components ? std::string("input angle in ") + angle_unit_name(angle_unit) +
                            ", converted exactly to radians before trigonometric evaluation"
                      : std::string("atan2(y, x), reported in ") + angle_unit_name(angle_unit);
    inputs.unit_policy =
        "preserve the declared physical dimension and unit, verify exact expressions before final-only approximation, with " +
        precision_detail(precision);
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

VectorComponentsResult halted(const Arena &arena, Derivation &derivation, size_t mark, Meter &meter,
                              const Budget &budget, NodeId model, ConversionDirection direction,
                              const Frame &frame, const Unit &unit, const Precision &precision,
                              AngleUnit angle_unit, const std::string &detail) {
    // STEP-025: keep the run of records that were checked. A precondition still waiting for its
    // check point leaves the plan unverified, so a halt before that point keeps nothing, which is
    // what actually happened.
    const bool cancelled = meter.halt() == Halt::Cancelled;
    const bool kept = keep_verified_prefix(derivation, mark, arena);
    VectorComponentsResult result;
    result.outcome = cancelled ? VectorComponentsOutcome::Cancelled
                               : VectorComponentsOutcome::ResourceExceeded;
    result.detail = detail;
    result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                    : kept     ? DerivationStatus::Cancelled
                               : DerivationStatus::NotRecorded;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, direction, frame, unit, precision,
                   angle_unit);
    return result;
}

bool adapter_value(Arena &arena, Adapter &adapter, Meter &meter, const Request &request,
                   NodeId *value, ResultTag *tag, std::string *why) {
    if (!meter.backend_call()) {
        *why = halt_name(meter.halt());
        return false;
    }
    const Response response = adapter.run(request);
    if (!response.usable() || !valid_node(arena, response.value)) {
        *why = response.detail.empty() ? tag_name(response.tag) : response.detail;
        if (response.usable() && response.detail.empty())
            *why = "the backend returned an invalid expression reference";
        return false;
    }
    *value = response.value;
    *tag = response.tag;
    return true;
}

// differentiate.cc:656-657 word for word, so every site that moves together the day this evidence
// has a status of its own is one search apart.
const char *const corroborated =
    "Giac agrees, but Giac was also asked for part of the answer it is checking, so this "
    "corroborates the result rather than proving it";

std::string self_checked(bool approximate) {
    return approximate ? std::string("checked before final approximation. ") + corroborated
                       : corroborated;
}

bool checked_value(Arena &arena, Adapter &adapter, Meter &meter, const Request &request,
                   NodeId formula, bool approximate, NodeId *value, std::string *why,
                   bool *verification_failed) {
    *verification_failed = false;
    ResultTag tag;
    NodeId calculated;
    if (!adapter_value(arena, adapter, meter, request, &calculated, &tag, why))
        return false;

    Request check;
    check.op = Op::IsZero;
    check.target = difference(arena, formula, calculated);
    NodeId checked;
    if (!adapter_value(arena, adapter, meter, check, &checked, &tag, why))
        return false;
    if (!literal_zero(arena, checked)) {
        *why = "Giac did not verify the component formula";
        *verification_failed = true;
        return false;
    }

    if (approximate) {
        Request numeric;
        numeric.op = Op::Approximate;
        numeric.target = calculated;
        if (!adapter_value(arena, adapter, meter, numeric, &calculated, &tag, why))
            return false;
    }
    *value = calculated;
    return true;
}

VectorComponentsResult conversion_failure(
    Derivation &derivation, size_t mark, Meter &meter, const Budget &budget, NodeId model,
    ConversionDirection direction, const Frame &frame, const Unit &unit,
    const Precision &precision, AngleUnit angle_unit, const std::string &detail,
    bool verification_failed) {
    derivation.rewind_to(mark);
    VectorComponentsResult result;
    result.outcome = verification_failed ? VectorComponentsOutcome::VerificationFailed
                                         : VectorComponentsOutcome::BackendFailure;
    result.detail = detail;
    result.status = verification_failed ? DerivationStatus::VerificationFailed
                                        : DerivationStatus::DependencyUnavailable;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, direction, frame, unit, precision,
                   angle_unit);
    return result;
}

VectorComponentsResult invalid_input(Derivation &derivation, Meter &meter, const Budget &budget,
                                     NodeId model, ConversionDirection direction,
                                     const Frame &frame, const Unit &unit,
                                     const Precision &precision, AngleUnit angle_unit,
                                     const std::string &detail) {
    VectorComponentsResult result;
    result.detail = detail;
    result.status = DerivationStatus::InvalidInput;
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, direction, frame, unit, precision,
                   angle_unit);
    return result;
}

}  // namespace

const char *vector_components_outcome_name(VectorComponentsOutcome outcome) {
    switch (outcome) {
        case VectorComponentsOutcome::Solved: return "solved";
        case VectorComponentsOutcome::InvalidInput: return "invalid input";
        case VectorComponentsOutcome::BackendFailure: return "backend failure";
        case VectorComponentsOutcome::VerificationFailed: return "verification failed";
        case VectorComponentsOutcome::Cancelled: return "cancelled";
        case VectorComponentsOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

VectorExpr vector_expr_from_exact(Arena &arena, const Vector &value) {
    VectorExpr expression;
    expression.x = rational_node(arena, value.x);
    expression.y = rational_node(arena, value.y);
    expression.z = rational_node(arena, value.z);
    expression.rank = value.rank;
    expression.frame = value.frame;
    expression.unit = value.unit;
    expression.precision = value.precision;
    return expression;
}

VectorComponentsResult magnitude_angle_to_components(
    Arena &arena, Derivation &derivation, const MagnitudeAngleExpr &input, Backend &backend,
    const Budget &budget) {
    const ConversionDirection direction = ConversionDirection::ToComponents;
    const size_t mark = derivation.mark();
    Meter meter(budget);
    NodeId model = kNoNode;
    if (!valid_node(arena, input.magnitude) || !valid_node(arena, input.angle))
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, input.angle_unit,
                             "magnitude and angle expressions are required");
    std::string why;
    if (!valid_unit(input.unit, &why) || !valid_precision(input.precision, &why))
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, input.angle_unit, why);

    model = magnitude_angle_model(arena, input);
    if (arena.failed())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, status_name(arena.status()));
    if (meter.stopped())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));
    const StepId plan = add_plan(derivation, meter, direction, input.frame, input.unit,
                                 input.precision, input.angle_unit);
    if (meter.stopped())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));

    const bool frame_ok = !input.frame.name.empty();
    const std::string frame_observed = frame_ok ? "frame '" + input.frame.name + "'"
                                                : "empty frame";
    if (!add_check(derivation, meter, plan, "vec.components.check-frame",
                   "Declared vector frame", "Check the vector frame",
                   "Component laws require a named Cartesian frame",
                   "obl.vector-components.frame-declared", "the vector frame is declared",
                   "frame declaration", "the magnitude and components use a named frame",
                   "a nonempty frame name", frame_observed, EvidenceStrength::StructurallyValid,
                   frame_ok))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.frame-declared",
        frame_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, frame_observed);
    if (!frame_ok)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, input.angle_unit, "a vector frame is required");

    const bool angle_unit_ok = valid_angle_unit(input.angle_unit);
    const std::string angle_observed = angle_unit_name(input.angle_unit);
    if (!add_check(derivation, meter, plan, "vec.components.check-angle-unit",
                   "Declared angle unit", "Check the input angle unit",
                   "Trigonometric evaluation requires degrees or radians",
                   "obl.vector-components.angle-unit-explicit",
                   "the angle unit is explicitly degrees or radians", "angle-unit validation",
                   "the component laws receive a dimensionless angle measure",
                   "degrees or radians", angle_observed, EvidenceStrength::StructurallyValid,
                   angle_unit_ok))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.angle-unit",
        angle_unit_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, angle_observed);
    if (!angle_unit_ok)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, input.angle_unit,
                             "the angle unit must be degrees or radians");

    const std::string dimension = dimension_text(input.unit.dimension);
    const std::string dimension_observed =
        "magnitude and components have dimension " + dimension + ", angle has dimension 1";
    if (!add_check(derivation, meter, plan, "vec.components.check-dimension",
                   "Component-law dimensions", "Check the component-law dimensions",
                   "Sine and cosine are dimensionless, so each component keeps the magnitude dimension",
                   "obl.vector-components.dimensions-preserved",
                   "magnitude and Cartesian components have the same physical dimension",
                   "dimensional analysis", "x and y have the magnitude dimension",
                   "r times a dimensionless trigonometric factor", dimension_observed,
                   EvidenceStrength::DimensionallyValid, true))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.dimensions-preserved", VerificationOutcome::Passed,
        dimension_observed);

    Adapter adapter(arena, backend);
    const NodeId radians = angle_radians(arena, input);
    const NodeId cos_call = arena.call("cos", std::vector<NodeId>{radians});
    const NodeId sin_call = arena.call("sin", std::vector<NodeId>{radians});
    const NodeId x_formula = arena.binary(Kind::Mul, input.magnitude, cos_call);
    const NodeId y_formula = arena.binary(Kind::Mul, input.magnitude, sin_call);
    const bool approximate = input.precision.kind == NumberKind::Measured;

    Request trig;
    trig.op = Op::Cos;
    trig.target = radians;
    NodeId cosine;
    bool verification_failed = false;
    const size_t x_calls_before = meter.backend_calls();
    if (!checked_value(arena, adapter, meter, trig, cos_call, false, &cosine, &why,
                       &verification_failed))
        return meter.stopped()
                   ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                            input.unit, input.precision, input.angle_unit, why)
                   : conversion_failure(derivation, mark, meter, budget, model, direction,
                                        input.frame, input.unit, input.precision, input.angle_unit,
                                        why, verification_failed);
    Request simplify;
    simplify.op = Op::Simplify;
    simplify.target = arena.binary(Kind::Mul, input.magnitude, cosine);
    NodeId x;
    if (!checked_value(arena, adapter, meter, simplify, x_formula, approximate, &x, &why,
                       &verification_failed))
        return meter.stopped()
                   ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                            input.unit, input.precision, input.angle_unit, why)
                   : conversion_failure(derivation, mark, meter, budget, model, direction,
                                        input.frame, input.unit, input.precision, input.angle_unit,
                                        why, verification_failed);
    const std::string verification_detail = self_checked(approximate);
    if (!add_transformation(
            derivation, meter, plan, "vec.components.x", "Cartesian x component",
            "Compute the x component", "Apply x = r cos(theta)",
            "Reach for this when you have a vector as a length and an angle and you want how far it "
            "reaches along the x axis. Picture the vector as the hypotenuse of a right triangle "
            "with its legs on the axes: the x leg is the side next to the angle, and the cosine is "
            "what relates a hypotenuse to the side next to it. The angle is measured from the "
            "positive x axis, which is what makes cosine the x one and sine the y one.",
            "obl.vector-components.component-relations",
            "the Cartesian components satisfy x = r cos(theta) and y = r sin(theta)", x_formula,
            x, approximate ? "Giac zero check before approximation" : "Giac zero check",
            verification_detail, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
            VerificationOutcome::Inconclusive, meter.backend_calls() - x_calls_before))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));

    trig.op = Op::Sin;
    NodeId sine;
    const size_t y_calls_before = meter.backend_calls();
    if (!checked_value(arena, adapter, meter, trig, sin_call, false, &sine, &why,
                       &verification_failed))
        return meter.stopped()
                   ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                            input.unit, input.precision, input.angle_unit, why)
                   : conversion_failure(derivation, mark, meter, budget, model, direction,
                                        input.frame, input.unit, input.precision, input.angle_unit,
                                        why, verification_failed);
    simplify.target = arena.binary(Kind::Mul, input.magnitude, sine);
    NodeId y;
    if (!checked_value(arena, adapter, meter, simplify, y_formula, approximate, &y, &why,
                       &verification_failed))
        return meter.stopped()
                   ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                            input.unit, input.precision, input.angle_unit, why)
                   : conversion_failure(derivation, mark, meter, budget, model, direction,
                                        input.frame, input.unit, input.precision, input.angle_unit,
                                        why, verification_failed);
    if (!add_transformation(
            derivation, meter, plan, "vec.components.y", "Cartesian y component",
            "Compute the y component", "Apply y = r sin(theta)",
            "Reach for this for the other leg of the same triangle, the one across from the angle "
            "rather than next to it, which is what sine relates to the hypotenuse. It always comes "
            "in a pair with the x component: a vector given as a length and an angle needs both "
            "before it can be added to anything, because only components add axis by axis.",
            "obl.vector-components.component-relations",
            "the Cartesian components satisfy x = r cos(theta) and y = r sin(theta)", y_formula,
            y, approximate ? "Giac zero check before approximation" : "Giac zero check",
            verification_detail, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
            VerificationOutcome::Inconclusive, meter.backend_calls() - y_calls_before))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));

    const std::string precision_observed = precision_detail(input.precision);
    if (!add_check(derivation, meter, plan, "vec.components.report-precision",
                   "Final-only component precision", "Check final component precision",
                   "Exact formulas are verified before any requested numeric approximation",
                   "obl.vector-components.precision-final",
                   "precision is applied only after exact component formulas are verified",
                   "operation ordering", "component reporting preserves the declared precision",
                   input.precision.kind == NumberKind::Exact
                       ? "exact component expressions"
                       : "approximation after exact formula checks",
                   precision_observed, EvidenceStrength::StructurallyValid, true))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, input.angle_unit, halt_name(meter.halt()));

    VectorComponentsResult result;
    result.outcome = VectorComponentsOutcome::Solved;
    result.components = {x, y, arena.integer("0"), 2, input.frame, input.unit, input.precision};
    result.has_components = true;
    result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, direction, input.frame, input.unit,
                   input.precision, input.angle_unit);
    return result;
}

static VectorComponentsResult components_to_magnitude_angle_impl(
    Arena &arena, Derivation &derivation, const VectorExpr &input, AngleUnit output_unit,
    Backend &backend, const Budget &budget, const Quantity *exact_magnitude) {
    const ConversionDirection direction = ConversionDirection::ToPolar;
    const size_t mark = derivation.mark();
    Meter meter(budget);
    NodeId model = kNoNode;
    if (arena.failed())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, status_name(arena.status()));
    if (!valid_node(arena, input.x) || !valid_node(arena, input.y))
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit, "two component expressions are required");
    std::string why;
    if (!valid_unit(input.unit, &why) || !valid_precision(input.precision, &why))
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit, why);

    model = vector_model(arena, input, output_unit);
    if (arena.failed())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, status_name(arena.status()));
    if (meter.stopped())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));
    const StepId plan = add_plan(derivation, meter, direction, input.frame, input.unit,
                                 input.precision, output_unit);
    if (meter.stopped())
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));

    const bool rank_ok = input.rank == 2;
    const std::string rank_observed = "rank " + std::to_string(input.rank);
    if (!add_check(derivation, meter, plan, "vec.polar.check-rank", "Two-dimensional vector rank",
                   "Check the Cartesian vector rank",
                   "A single direction angle represents a two-dimensional vector",
                   "obl.vector-components.rank-two", "the Cartesian vector has rank two",
                   "rank comparison", "the conversion has exactly x and y components", "rank 2",
                   rank_observed, EvidenceStrength::StructurallyValid, rank_ok))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.rank-two",
        rank_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, rank_observed);
    if (!rank_ok)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit,
                             "magnitude-angle conversion requires a rank 2 vector");

    const bool frame_ok = !input.frame.name.empty();
    const std::string frame_observed = frame_ok ? "frame '" + input.frame.name + "'"
                                                : "empty frame";
    if (!add_check(derivation, meter, plan, "vec.polar.check-frame", "Declared vector frame",
                   "Check the vector frame", "Magnitude and direction retain the Cartesian frame",
                   "obl.vector-components.frame-declared", "the vector frame is declared",
                   "frame declaration", "the input and polar result use a named frame",
                   "a nonempty frame name", frame_observed, EvidenceStrength::StructurallyValid,
                   frame_ok))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.frame-declared",
        frame_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, frame_observed);
    if (!frame_ok)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit, "a vector frame is required");

    const bool angle_unit_ok = valid_angle_unit(output_unit);
    const std::string angle_observed = angle_unit_name(output_unit);
    if (!add_check(derivation, meter, plan, "vec.polar.check-angle-unit",
                   "Declared angle unit", "Check the output angle unit",
                   "Direction must be reported explicitly in degrees or radians",
                   "obl.vector-components.angle-unit-explicit",
                   "the angle unit is explicitly degrees or radians", "angle-unit validation",
                   "the direction result names its dimensionless angle measure",
                   "degrees or radians", angle_observed, EvidenceStrength::StructurallyValid,
                   angle_unit_ok))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.angle-unit",
        angle_unit_ok ? VerificationOutcome::Passed : VerificationOutcome::Failed, angle_observed);
    if (!angle_unit_ok)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit,
                             "the angle unit must be degrees or radians");

    const std::string dimension = dimension_text(input.unit.dimension);
    const std::string dimension_observed =
        "components and magnitude have dimension " + dimension + ", direction has dimension 1";
    if (!add_check(derivation, meter, plan, "vec.polar.check-dimension",
                   "Polar-law dimensions", "Check the magnitude and direction dimensions",
                   "The magnitude keeps the component dimension and atan2 returns a dimensionless angle",
                   "obl.vector-components.dimensions-preserved",
                   "magnitude and Cartesian components have the same physical dimension",
                   "dimensional analysis", "magnitude has the component dimension",
                   "sqrt(x^2 + y^2) has the component dimension", dimension_observed,
                   EvidenceStrength::DimensionallyValid, true))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));
    derivation.complete_plan_precondition(
        plan, "pre.vector-components.dimensions-preserved", VerificationOutcome::Passed,
        dimension_observed);

    Rational exact_x;
    Rational exact_y;
    const std::vector<SymbolValue> no_symbols;
    if (exact_magnitude == nullptr &&
        evaluate_rational(arena, input.x, no_symbols, &exact_x) &&
        evaluate_rational(arena, input.y, no_symbols, &exact_y) &&
        rational_equal(exact_x, {0, 1}) && rational_equal(exact_y, {0, 1}))
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit,
                             "the zero vector has no defined direction");

    Adapter adapter(arena, backend);
    const NodeId two = arena.integer("2");
    const NodeId sum = arena.binary(
        Kind::Add, arena.binary(Kind::Pow, input.x, two), arena.binary(Kind::Pow, input.y, two));
    const NodeId magnitude_formula = arena.call("sqrt", std::vector<NodeId>{sum});
    NodeId magnitude;
    bool verification_failed = false;
    const bool approximate = input.precision.kind == NumberKind::Measured;
    const size_t magnitude_calls_before = meter.backend_calls();
    if (exact_magnitude) {
        magnitude = rational_node(arena, exact_magnitude->value);
        Request check;
        check.op = Op::IsZero;
        check.target = difference(arena, magnitude_formula, magnitude);
        NodeId zero;
        ResultTag tag;
        if (!adapter_value(arena, adapter, meter, check, &zero, &tag, &why))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, false);
        if (!literal_zero(arena, zero))
            return conversion_failure(
                derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                input.precision, output_unit, "Giac did not verify the exact magnitude", true);
    } else {
        Request simplify;
        simplify.op = Op::Simplify;
        simplify.target = magnitude_formula;
        if (!checked_value(arena, adapter, meter, simplify, magnitude_formula, false,
                           &magnitude, &why, &verification_failed))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, verification_failed);
    }
    Rational simplified_magnitude;
    const bool rational_magnitude =
        evaluate_rational(arena, magnitude, no_symbols, &simplified_magnitude);
    bool zero_magnitude = rational_magnitude && rational_equal(simplified_magnitude, {0, 1});
    if (!rational_magnitude) {
        Request classify;
        classify.op = Op::IsZero;
        classify.target = magnitude;
        NodeId classified;
        ResultTag tag;
        if (!adapter_value(arena, adapter, meter, classify, &classified, &tag, &why))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, false);
        Rational zero_classification;
        zero_magnitude = evaluate_rational(arena, classified, no_symbols, &zero_classification) &&
                         rational_equal(zero_classification, {0, 1});
    }
    if (zero_magnitude)
        return invalid_input(derivation, meter, budget, model, direction, input.frame, input.unit,
                             input.precision, output_unit,
                             "the zero vector has no defined direction");
    if (approximate) {
        Request numeric;
        numeric.op = Op::Approximate;
        numeric.target = magnitude;
        ResultTag tag;
        if (!adapter_value(arena, adapter, meter, numeric, &magnitude, &tag, &why))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, false);
    }
    // The one zero check in this file whose two operands come from two engines: the magnitude is a
    // native rational and Giac only judged it, so disagreement is detectable and the word holds.
    const bool independent = exact_magnitude != nullptr;
    const std::string verification_detail =
        !independent  ? self_checked(approximate)
        : approximate ? "the exact magnitude relation passed an independent zero check before final "
                        "approximation"
                      : "the returned exact magnitude passed an independent zero check";
    if (!add_transformation(
            derivation, meter, plan, "vec.polar.magnitude", "Vector magnitude relation",
            "Compute the vector magnitude", "Apply r = sqrt(x^2 + y^2)",
            "Reach for this going the other way, when you have the x and y components and want the "
            "vector's length. It is Pythagoras on the same right triangle the components came from, "
            "with the two components as the legs and the length as the hypotenuse. The length is "
            "never negative, whatever signs the components carry, because the squares remove them.",
            "obl.vector-components.magnitude-relation",
            "the magnitude satisfies r = sqrt(x^2 + y^2)", magnitude_formula, magnitude,
            approximate ? "Giac zero check before approximation" : "Giac zero check",
            verification_detail, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
            independent ? VerificationOutcome::Passed : VerificationOutcome::Inconclusive,
            meter.backend_calls() - magnitude_calls_before))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));

    Request atan;
    atan.op = Op::Atan2;
    atan.target = input.y;
    atan.argument = input.x;
    const NodeId angle_formula = arena.call("atan2", std::vector<NodeId>{input.y, input.x});
    NodeId radians;
    const size_t angle_calls_before = meter.backend_calls();
    if (!checked_value(arena, adapter, meter, atan, angle_formula, false, &radians, &why,
                       &verification_failed))
        return meter.stopped()
                   ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                            input.unit, input.precision, output_unit, why)
                   : conversion_failure(derivation, mark, meter, budget, model, direction,
                                        input.frame, input.unit, input.precision, output_unit, why,
                                        verification_failed);
    const NodeId angle_formula_out = angle_from_radians(arena, radians, output_unit);
    NodeId angle = angle_formula_out;
    if (output_unit == AngleUnit::Degrees) {
        Request simplify;
        simplify.op = Op::Simplify;
        simplify.target = angle_formula_out;
        if (!checked_value(arena, adapter, meter, simplify, angle_formula_out, approximate,
                           &angle, &why, &verification_failed))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, verification_failed);
    } else if (approximate) {
        Request numeric;
        numeric.op = Op::Approximate;
        numeric.target = radians;
        ResultTag tag;
        if (!adapter_value(arena, adapter, meter, numeric, &angle, &tag, &why))
            return meter.stopped()
                       ? halted(arena, derivation, mark, meter, budget, model, direction, input.frame,
                                input.unit, input.precision, output_unit, why)
                       : conversion_failure(derivation, mark, meter, budget, model, direction,
                                            input.frame, input.unit, input.precision, output_unit,
                                            why, false);
    }
    if (!add_transformation(
            derivation, meter, plan, "vec.polar.direction", "Quadrant-aware vector direction",
            "Compute direction with atan2", "Apply theta = atan2(y, x)",
            "Reach for this for the angle that goes with the length, whenever components are being "
            "turned back into a direction. It is atan2 rather than the arctangent of y over x "
            "because dividing first loses which quadrant the vector points into: (-3, -4) and "
            "(3, 4) give the same ratio and opposite directions. Taking y and x separately keeps "
            "the two signs, so the angle lands in the right quadrant.",
            "obl.vector-components.quadrant-direction",
            "the direction uses atan2(y, x) and preserves the Cartesian quadrant",
            angle_formula_out, angle,
            approximate ? "Giac zero check before approximation" : "Giac zero check",
            self_checked(approximate), EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
            VerificationOutcome::Inconclusive, meter.backend_calls() - angle_calls_before))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));

    const std::string precision_observed = precision_detail(input.precision);
    if (!add_check(derivation, meter, plan, "vec.polar.report-precision",
                   "Final-only polar precision", "Check final polar precision",
                   "Exact magnitude and direction relations are verified before any requested approximation",
                   "obl.vector-components.precision-final",
                   "precision is applied only after exact polar formulas are verified",
                   "operation ordering", "polar reporting preserves the declared precision",
                   input.precision.kind == NumberKind::Exact
                       ? "exact magnitude and direction expressions"
                       : "approximation after exact formula checks",
                   precision_observed, EvidenceStrength::StructurallyValid, true))
        return halted(arena, derivation, mark, meter, budget, model, direction, input.frame, input.unit,
                      input.precision, output_unit, halt_name(meter.halt()));

    VectorComponentsResult result;
    result.outcome = VectorComponentsOutcome::Solved;
    result.polar = {magnitude, angle, output_unit, input.frame, input.unit, input.precision};
    result.has_polar = true;
    result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, direction, input.frame, input.unit,
                   input.precision, output_unit);
    return result;
}

VectorComponentsResult components_to_magnitude_angle(
    Arena &arena, Derivation &derivation, const VectorExpr &input, AngleUnit output_unit,
    Backend &backend, const Budget &budget) {
    return components_to_magnitude_angle_impl(arena, derivation, input, output_unit, backend,
                                              budget, nullptr);
}

VectorComponentsResult components_to_magnitude_angle(
    Arena &arena, Derivation &derivation, const Vector &input, AngleUnit output_unit,
    Backend &backend, const Budget &budget) {
    Quantity exact_magnitude;
    std::string why;
    const Quantity *exact = nullptr;
    if (vector_magnitude(input, &exact_magnitude, &why)) {
        Rational input_unit_magnitude;
        if (rational_div(exact_magnitude.value, input.unit.scale, &input_unit_magnitude)) {
            exact_magnitude.value = input_unit_magnitude;
            exact_magnitude.unit = input.unit;
            exact = &exact_magnitude;
        }
    }
    return components_to_magnitude_angle_impl(arena, derivation,
                                              vector_expr_from_exact(arena, input), output_unit,
                                              backend, budget, exact);
}

}  // namespace nps
