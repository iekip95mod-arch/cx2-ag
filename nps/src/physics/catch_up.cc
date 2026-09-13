#include "nps/physics/catch_up.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {
namespace {

struct BodySI {
    Rational position;
    Rational velocity;
    Rational start;
    Precision position_precision;
    Precision velocity_precision;
    Precision start_precision;
};

Dimension length_dimension() {
    Dimension dimension;
    dimension.length = 1;
    return dimension;
}

Dimension time_dimension() {
    Dimension dimension;
    dimension.time = 1;
    return dimension;
}

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

CatchUpResult failed(CatchUpOutcome outcome, DerivationStatus status,
                     const std::string &detail) {
    CatchUpResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

bool valid_motion(CatchUpMotionModel model) {
    switch (model) {
        case CatchUpMotionModel::ConstantVelocity:
        case CatchUpMotionModel::ConstantAcceleration: return true;
    }
    return false;
}

bool normalized(const Rational &source, Rational *value) {
    *value = source;
    return normalise(&value->num, &value->den);
}

bool valid_quantity(const Quantity &quantity, std::string *detail) {
    Rational value;
    if (!normalized(quantity.value, &value)) {
        *detail = "the quantity has an invalid exact value";
        return false;
    }
    if (!normalized(quantity.unit.scale, &value) || value.num <= 0) {
        *detail = "the unit has an invalid SI conversion scale";
        return false;
    }
    switch (quantity.precision.kind) {
        case NumberKind::Exact:
            if (quantity.precision.significant_digits != 0) {
                *detail = "an exact value cannot carry a measured significant-figure count";
                return false;
            }
            return true;
        case NumberKind::Measured:
            if (quantity.precision.significant_digits == 0 ||
                quantity.precision.significant_digits > 18) {
                *detail = "a measured value needs between 1 and 18 significant figures";
                return false;
            }
            return true;
    }
    *detail = "the quantity has an invalid precision kind";
    return false;
}

bool validate_field(const CatchUpBody &body, const char *field,
                    const Quantity &quantity, const Dimension &expected,
                    CatchUpResult *failure) {
    std::string detail;
    if (!valid_quantity(quantity, &detail)) {
        *failure = failed(CatchUpOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          body.name + " " + field + ": " + detail);
        return false;
    }
    if (quantity.unit.dimension != expected) {
        *failure = failed(CatchUpOutcome::DimensionMismatch,
                          DerivationStatus::InvalidInput,
                          body.name + " " + field + " has dimension " +
                              dimension_text(quantity.unit.dimension) + ", but requires " +
                              dimension_text(expected));
        return false;
    }
    return true;
}

bool validate_body(const CatchUpBody &body, CatchUpResult *failure) {
    if (!validate_field(body, "position at start", body.position_at_start,
                        length_dimension(), failure) ||
        !validate_field(body, "velocity at start", body.velocity_at_start,
                        velocity_dimension(), failure) ||
        !validate_field(body, "start time", body.start_time, time_dimension(), failure)) {
        return false;
    }
    if (body.motion == CatchUpMotionModel::ConstantAcceleration &&
        !validate_field(body, "acceleration", body.acceleration,
                        acceleration_dimension(), failure)) {
        return false;
    }
    return true;
}

bool has_zero_acceleration(const CatchUpBody &body) {
    if (body.motion != CatchUpMotionModel::ConstantAcceleration ||
        body.acceleration.unit.dimension != acceleration_dimension()) {
        return false;
    }
    std::string detail;
    Rational acceleration;
    return valid_quantity(body.acceleration, &detail) &&
           to_si(body.acceleration, &acceleration) && acceleration.num == 0;
}

Unit si_unit(const Dimension &dimension) {
    Unit unit;
    unit.dimension = dimension;
    unit.scale.num = 1;
    unit.scale.den = 1;
    unit.text = si_unit_text(dimension);
    return unit;
}

Precision si_precision(const Quantity &quantity, const Rational &converted) {
    return precision_product(converted, quantity.value, quantity.precision, quantity.unit.scale,
                             Precision());
}

bool convert_body(const CatchUpBody &body, BodySI *converted,
                  CatchUpResult *failure) {
    if (!to_si(body.position_at_start, &converted->position)) {
        *failure = failed(CatchUpOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "converting " + body.name + " position to SI exceeds exact arithmetic");
        return false;
    }
    if (!to_si(body.velocity_at_start, &converted->velocity)) {
        *failure = failed(CatchUpOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "converting " + body.name + " velocity to SI exceeds exact arithmetic");
        return false;
    }
    if (!to_si(body.start_time, &converted->start)) {
        *failure = failed(CatchUpOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "converting " + body.name + " start time to SI exceeds exact arithmetic");
        return false;
    }
    converted->position_precision = si_precision(body.position_at_start, converted->position);
    converted->velocity_precision = si_precision(body.velocity_at_start, converted->velocity);
    converted->start_precision = si_precision(body.start_time, converted->start);
    if (body.motion == CatchUpMotionModel::ConstantAcceleration) {
        Rational acceleration;
        if (!to_si(body.acceleration, &acceleration)) {
            *failure = failed(CatchUpOutcome::ArithmeticOverflow,
                              DerivationStatus::ResourceLimitReached,
                              "converting " + body.name +
                                  " acceleration to SI exceeds exact arithmetic");
            return false;
        }
        if (acceleration.num != 0) {
            *failure = failed(
                CatchUpOutcome::NonlinearMotionUnsupported,
                DerivationStatus::Unsupported,
                body.name +
                    " uses constant-acceleration motion, whose position law is nonlinear in time");
            return false;
        }
    }
    return true;
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

bool rational_of_node(const Arena &arena, NodeId id, Rational *value) {
    if (id == kNoNode)
        return false;
    int64_t integer;
    const Node &node = arena.at(id);
    if (small_integer(arena, id, &integer)) {
        value->num = integer;
        value->den = 1;
        return true;
    }
    const ChildView children = arena.children(node);
    if (node.kind != Kind::Mul || children.size() != 2)
        return false;
    Rational numerator;
    if (!rational_of_node(arena, children[0], &numerator) || numerator.den != 1)
        return false;
    const Node &power = arena.at(children[1]);
    const ChildView power_children = arena.children(power);
    int64_t denominator;
    int64_t exponent;
    if (power.kind != Kind::Pow || power_children.size() != 2 ||
        !small_integer(arena, power_children[0], &denominator) ||
        !small_integer(arena, power_children[1], &exponent) || exponent != -1 ||
        denominator == 0) {
        return false;
    }
    value->num = numerator.num;
    value->den = denominator;
    return normalise(&value->num, &value->den);
}

NodeId position_expression(Arena &arena, NodeId time, const BodySI &body) {
    const NodeId elapsed = arena.binary(
        Kind::Add, time, arena.unary(Kind::Neg, rational_node(arena, body.start)));
    const NodeId displacement =
        arena.binary(Kind::Mul, rational_node(arena, body.velocity), elapsed);
    return arena.binary(Kind::Add, rational_node(arena, body.position), displacement);
}

bool position_at(const BodySI &body, const Rational &time, const Precision &time_precision,
                 Rational *position, Precision *precision) {
    Rational elapsed;
    Rational displacement;
    if (!rational_sub(time, body.start, &elapsed) ||
        !rational_mul(body.velocity, elapsed, &displacement) ||
        !rational_add(body.position, displacement, position)) {
        return false;
    }
    const Precision elapsed_precision =
        precision_sum(elapsed, time_precision, body.start_precision);
    const Precision displacement_precision = precision_product(
        displacement, body.velocity, body.velocity_precision, elapsed, elapsed_precision);
    *precision = precision_sum(*position, body.position_precision, displacement_precision);
    return true;
}

Precision finer_precision(const Precision &a, const Precision &b) {
    if (a.kind == NumberKind::Exact || b.kind == NumberKind::Exact)
        return Precision();
    return a.last_significant_decimal_place <= b.last_significant_decimal_place ? a : b;
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

Precision answer_precision(const CatchUpProblem &problem) {
    Precision precision;
    const CatchUpBody *bodies[2] = {&problem.first, &problem.second};
    for (const CatchUpBody *body : bodies) {
        precision = precision_combine(precision, body->position_at_start.precision);
        precision = precision_combine(precision, body->velocity_at_start.precision);
        precision = precision_combine(precision, body->start_time.precision);
    }
    return precision;
}

bool report_text(const Rational &value, const Precision &precision, std::string *text) {
    *text = rational_text(value);
    if (precision.kind != NumberKind::Measured)
        return true;
    return rounded_text(value, precision.significant_digits, text);
}

NodeId reported_node(Arena &arena, const std::string &text) {
    return text.find('.') == std::string::npos ? arena.integer(text) : arena.decimal(text);
}

// Unreadable is kept apart from OutsideHalfPlace because they deserve opposite answers. A rounding
// measured and rejected is an engine fault and the solve refuses. A rounding whose text could not be
// read back was never judged, so the exact value stands and the record says the check did not run.
enum class ReportStep { Recorded, OutsideHalfPlace, Unreadable, Halted };

// The two answers are rounded by different rules, so the step that records each one names its own.
struct ReportRule {
    const char *quantity_name;
    const char *rule_name;
    const char *explanation_short;
    const char *explanation_detailed;
};

constexpr ReportRule kEventTimeRule = {
    "event time", "Significant figures",
    "Use the fewest significant figures among the measured givens",
    "Reach for this once, at the very end, and never partway through. A measured value is only as "
    "good as the figures it was written with, so the event time is reported to the fewest "
    "significant figures among the measurements it came from. Rounding a speed or a head start "
    "before solving would throw away figures the final rounding cannot get back, which is why "
    "every step above this one keeps the exact value."};

constexpr ReportRule kEventPositionRule = {
    "event position", "Decimal places",
    "Keep every decimal place both terms of the position law reach",
    "Reach for this once, at the very end, and never partway through. The meeting position is a "
    "sum, a starting position plus a displacement, and a sum is limited by decimal places rather "
    "than by figure counts: it is only as good as the coarsest place among its terms. That is "
    "often finer than the fewest significant figures among the givens, because a starting position "
    "written to a tenth still pins the tenth after a coarse displacement is added to it."};

ReportStep add_report_step(Arena &arena, Derivation &derivation, Meter &meter,
                           const ReportRule &rule, NodeId exact, const Rational &exact_value,
                           const Precision &precision, const std::string &exact_text,
                           const std::string &reported, std::string *observed) {
    if (exact_text == reported)
        return ReportStep::Recorded;
    // Checked against the string this step is about to record rather than upstream against the
    // local that produced it. The predicate parses the reported text back and measures the error,
    // so it shares no path with the rounder. The last place is the declared one, because a
    // position that correctly rounds to "0" has no characters to count a place off.
    const HalfPlace checked = precision_rounding_valid(exact_value, reported, precision);
    if (checked == HalfPlace::Outside) {
        *observed =
            reported + " is further than half a unit in its last place from " + exact_text;
        return ReportStep::OutsideHalfPlace;
    }
    if (checked == HalfPlace::Unreadable) {
        *observed = reported + " could not be read back as a decimal, so it was never compared "
                               "against " +
                    exact_text;
        if (!meter.step())
            return ReportStep::Halted;
        // The exact value is what gets reported, so the record carries the attempt rather than
        // leaving a reader to wonder why the rounding they expected is missing.
        Step s;
        s.phase = "report";
        s.goal = std::string("Report the ") + rule.quantity_name + " to the measured precision";
        s.rule_id = "physics.catch-up.significant-figures";
        s.rule_name = rule.rule_name;
        s.claim = ClaimType::NoClaim;
        s.explanation_short = rule.explanation_short;
        s.explanation_detailed =
            "The rounded spelling could not be read back as a decimal, so it was never compared "
            "against the exact value. The exact value is reported instead, since showing a rounding "
            "nothing checked would be showing an answer with no evidence behind it.";
        s.proof_obligations.push_back(
            {"obl.physics.reported-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        s.verifications.push_back(verification("exact comparison against the unrounded value",
                                               *observed, EvidenceStrength::CandidateChecked,
                                               VerificationOutcome::Inconclusive));
        CheckPayload check;
        check.target_claim =
            "the reported value is within half a unit in the last place of the exact one";
        check.check_method = "read the rounded text back and compare it against the exact value";
        check.expected_relation = "the difference is at most half a unit in the last place";
        check.observed_result = *observed;
        derivation.add_check(kNoStep, std::move(s), std::move(check));
        return ReportStep::Unreadable;
    }
    if (!meter.step())
        return ReportStep::Halted;
    Step step;
    step.phase = "report";
    step.goal = std::string("Report the ") + rule.quantity_name + " to the measured precision";
    step.rule_id = "physics.catch-up.significant-figures";
    step.rule_name = rule.rule_name;
    step.explanation_short = rule.explanation_short;
    step.explanation_detailed = rule.explanation_detailed;
    step.claim = ClaimType::NoClaim;
    step.verifications.push_back(verification(
        "exact comparison against the unrounded value",
        reported + " is within half a unit in the last place of " + exact_text,
        EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
    step.proof_obligations.push_back(
        {"obl.physics.reported-within-half-place",
         "the reported value is within half a unit in the last place of the exact one"});
    TransformationPayload payload;
    payload.before = exact;
    payload.after = reported_node(arena, reported);
    payload.concrete_action = "Report " + exact_text + " as " + reported;
    payload.reversible = false;
    if (arena.failed())
        return ReportStep::Halted;
    derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
    return ReportStep::Recorded;
}

// The modelling assumptions the position laws rest on, in one place because two consumers need
// them: the plan step carries the link, and a refusal that never built a step still has to say
// what was assumed. Written twice, the two would drift.
std::vector<std::string> motion_assumptions(const CatchUpProblem &problem) {
    std::vector<std::string> assumptions;
    assumptions.push_back(std::string("first body motion model: ") +
                          catch_up_motion_model_name(problem.first.motion));
    assumptions.push_back(std::string("second body motion model: ") +
                          catch_up_motion_model_name(problem.second.motion));
    if (has_zero_acceleration(problem.first))
        assumptions.push_back("first body acceleration is zero on its active interval");
    if (has_zero_acceleration(problem.second))
        assumptions.push_back("second body acceleration is zero on its active interval");
    return assumptions;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const CatchUpProblem &problem) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.kinematics.catch-up.equal-position";
    inputs.requested_method =
        "build active-interval position laws, set positions equal, solve exactly, substitute back";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    for (const std::string &assumption : motion_assumptions(problem))
        inputs.active_assumptions.push_back(assumption);
    inputs.active_assumptions.push_back(
        "motion is one-dimensional with positive position along the declared axis");
    inputs.branch_convention = "event time is in both bodies' active intervals";
    inputs.unit_policy =
        "validate dimensions, convert exactly to SI, and round only reported event values";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

CatchUpResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                         const CatchUpProblem &problem, const Budget &budget,
                         NodeId *model) {
    if (problem.first.name.empty() || problem.second.name.empty()) {
        return failed(CatchUpOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "both bodies need names");
    }
    const size_t input_limit = arena.limits().max_input_bytes;
    size_t remaining_identity_bytes = input_limit;
    const std::string *identity_fields[4] = {
        &problem.first.name, &problem.first.frame.name,
        &problem.second.name, &problem.second.frame.name};
    for (const std::string *field : identity_fields) {
        if (field->size() > remaining_identity_bytes) {
            return failed(CatchUpOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached,
                          "body and frame identifiers exceed the input byte limit");
        }
        remaining_identity_bytes -= field->size();
    }
    if (problem.first.name == problem.second.name) {
        return failed(CatchUpOutcome::DuplicateBody, DerivationStatus::InvalidInput,
                      "the two motion records name the same body");
    }
    if (problem.first.frame.name.empty() || problem.second.frame.name.empty()) {
        return failed(CatchUpOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                      "both bodies need a declared one-dimensional coordinate frame");
    }
    if (problem.first.frame != problem.second.frame) {
        return failed(CatchUpOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                      "the two bodies use different coordinate frames");
    }
    if (!valid_motion(problem.first.motion) || !valid_motion(problem.second.motion)) {
        return failed(CatchUpOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "a body has an invalid motion model");
    }

    CatchUpResult failure;
    if (!validate_body(problem.first, &failure) ||
        !validate_body(problem.second, &failure)) {
        return failure;
    }

    BodySI first;
    BodySI second;
    if (!convert_body(problem.first, &first, &failure) ||
        !convert_body(problem.second, &second, &failure)) {
        return failure;
    }

    Rational start_difference;
    if (!rational_sub(first.start, second.start, &start_difference)) {
        return failed(CatchUpOutcome::ArithmeticOverflow,
                      DerivationStatus::ResourceLimitReached,
                      "comparing the two active-interval boundaries exceeds exact arithmetic");
    }
    const Rational shared_start = start_difference.num < 0 ? second.start : first.start;

    CatchUpResult result;
    result.shared_active_start.value = shared_start;
    result.shared_active_start.unit = si_unit(time_dimension());
    result.shared_active_start.precision = precision_combine(
        problem.first.start_time.precision, problem.second.start_time.precision);

    const NodeId time = arena.symbol("t_event");
    const NodeId first_position = position_expression(arena, time, first);
    const NodeId second_position = position_expression(arena, time, second);
    const NodeId equation = arena.binary(Kind::Equals, first_position, second_position);
    const NodeId shared_start_node = rational_node(arena, shared_start);
    const NodeId active_domain =
        arena.binary(Kind::GreaterEqual, time, shared_start_node);
    *model = equation;
    if (arena.failed()) {
        return failed(CatchUpOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    result.time = time;
    result.first_position = first_position;
    result.second_position = second_position;
    result.equation = equation;
    result.active_domain = active_domain;

    PlanPayload plan;
    plan.strategy_id = "physics.catch-up.constant-velocity";
    plan.selected_strategy =
        "Use each constant-velocity position law on its active interval and solve equal positions";
    plan.matched_problem_facts.push_back(problem.first.name + " starts at " +
                                         rational_text(first.start) + " s");
    plan.matched_problem_facts.push_back(problem.second.name + " starts at " +
                                         rational_text(second.start) + " s");
    plan.matched_problem_facts.push_back("shared coordinate frame: " +
                                         problem.first.frame.name);
    if (problem.first.motion == CatchUpMotionModel::ConstantAcceleration) {
        plan.matched_problem_facts.push_back(
            problem.first.name + " has zero acceleration");
    }
    if (problem.second.motion == CatchUpMotionModel::ConstantAcceleration) {
        plan.matched_problem_facts.push_back(
            problem.second.name + " has zero acceleration");
    }
    plan.alternatives_considered.push_back(
        "a constant-acceleration position law, which would be nonlinear in event time");
    plan.selection_rationale =
        "both active-interval laws have constant velocity directly or by zero acceleration, "
        "so the existing exact linear solver applies";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find when " + problem.first.name + " and " + problem.second.name + " meet";
    plan_step.rule_id = "physics.catch-up.constant-velocity";
    plan_step.rule_name = "Constant-velocity active-interval model";
    plan_step.explanation_short =
        "Write one position law per body and solve their equal-position event";
    plan_step.assumptions_before = motion_assumptions(problem);
    plan_step.assumptions_before.push_back("t_event must be at or after both stated start times");
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.catch-up.constant-velocity",
        "each body has constant velocity after its stated start time",
        "validated motion models", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed,
        "both bodies are constant velocity directly or have zero acceleration");
    register_strategy_precondition(
        plan, plan_step, "pre.catch-up.shared-axis",
        "both positions use the same one-dimensional coordinate axis", "frame identity",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        "both bodies use the same declared coordinate frame");
    register_strategy_precondition(
        plan, plan_step, "pre.catch-up.shared-active-domain",
        "an event is admissible only when both bodies are active",
        "exact rational boundary comparison", EvidenceStrength::CandidateChecked,
        VerificationOutcome::NotAttempted, "checked after solving the equal-position equation");
    if (!meter.step())
        return CatchUpResult();
    const StepId plan_id =
        derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    Dimension displacement_dimension;
    const bool dimension_product_fits = dimension_multiply(
        velocity_dimension(), time_dimension(), &displacement_dimension);
    const bool dimensions_match =
        dimension_product_fits && displacement_dimension == length_dimension();
    if (!meter.step())
        return CatchUpResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check both position-law dimensions";
        step.rule_id = "physics.catch-up.check-dimensions";
        step.rule_name = "Dimensional analysis";
        step.explanation_short =
            "Position at start and velocity times elapsed time must both be lengths";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.catch-up.position-law-dimensions",
             "both active-interval position laws have length dimension"});
        const std::string observed =
            dimensions_match ? "both position laws reduce to length"
                             : "a position-law dimension does not reduce to length";
        step.verifications.push_back(verification(
            "typed dimensional analysis", observed, EvidenceStrength::DimensionallyValid,
            dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "both sides of the equal-position equation are lengths";
        check.check_method = "multiply velocity dimension by elapsed-time dimension";
        check.expected_relation = "L + (L T^-1)(T) has dimension L";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!dimension_product_fits) {
        return failed(CatchUpOutcome::ArithmeticOverflow,
                      DerivationStatus::VerificationFailed,
                      "the position-law dimension does not fit");
    }
    if (!dimensions_match) {
        return failed(CatchUpOutcome::DimensionMismatch,
                      DerivationStatus::VerificationFailed,
                      "the position laws are not dimensionally consistent");
    }

    if (!meter.step())
        return CatchUpResult();
    {
        Step step;
        step.phase = "model";
        step.goal = "Create the equal-position event equation";
        step.rule_id = "physics.catch-up.equal-position";
        step.rule_name = "Equal-position event";
        step.explanation_short =
            "Bodies meet when their positions on the same axis are equal";
        step.explanation_detailed =
            "Reach for this whenever a question asks when or where one body catches, meets or "
            "overtakes another. Catching up is not a separate formula: write each body's position "
            "as a function of time, then set the two equal, because being in the same place at the "
            "same time is the whole of what catching up means. Solving that equation gives the "
            "time, and putting the time back into either position gives the place.";
        step.claim = ClaimType::Definition;
        step.assumptions_before.push_back(
            "the equation is valid only in the shared active-time domain");
        step.verifications.push_back(verification(
            "typed body and interval identity",
            "the equation uses one original position expression from each named body",
            EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        step.proof_obligations.push_back(
            {"obl.catch-up.equal-position-is-the-event",
             "the event the two bodies share is one position at one time on one axis"});
        TransformationPayload payload;
        payload.before = first_position;
        payload.after = equation;
        payload.concrete_action = "Set " + problem.first.name + " position equal to " +
                                  problem.second.name + " position";
        payload.reversible = false;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    const Budget linear_budget = remaining_budget(budget, meter);
    const size_t solve_start = derivation.mark();
    const SolveResult solved =
        solve_linear(arena, derivation, equation, time, linear_budget);
    derivation.adopt_roots_since(solve_start, plan_id);
    if (!charge(meter, solved.cost))
        return CatchUpResult();
    if (solved.outcome == SolveOutcome::Cancelled) {
        result.outcome = CatchUpOutcome::Cancelled;
        result.status = DerivationStatus::NotRecorded;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::ResourceExceeded) {
        result.outcome = CatchUpOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::NoSolution) {
        derivation.complete_plan_precondition(
            plan_id, "pre.catch-up.shared-active-domain", VerificationOutcome::Passed,
            "no algebraic candidate exists outside the shared active domain");
        result.outcome = CatchUpOutcome::NoMeeting;
        result.detail =
            "the active-interval position laws never have the same position: " + solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::AllValues) {
        derivation.complete_plan_precondition(
            plan_id, "pre.catch-up.shared-active-domain", VerificationOutcome::Passed,
            "the reported solution set is restricted to the shared active domain");
        result.outcome = CatchUpOutcome::AllTimesMeeting;
        result.detail = "the two position laws coincide for every time in the shared active domain";
        return result;
    }
    if (solved.outcome != SolveOutcome::Solved) {
        const bool overflow = solved.status == DerivationStatus::ResourceLimitReached;
        result.outcome = overflow ? CatchUpOutcome::ArithmeticOverflow
                                  : CatchUpOutcome::VerificationFailed;
        result.status = solved.status;
        result.detail = solved.detail;
        return result;
    }

    Rational candidate;
    if (!rational_of_node(arena, solved.solution, &candidate)) {
        result.outcome = CatchUpOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the linear solver returned an event time that is not an exact rational";
        return result;
    }

    Rational domain_offset;
    const bool domain_computed = rational_sub(candidate, shared_start, &domain_offset);
    const bool in_domain = domain_computed && domain_offset.num >= 0;
    if (!meter.step())
        return CatchUpResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check the shared active-time domain";
        step.rule_id = "physics.catch-up.shared-domain";
        step.rule_name = "Shared active interval";
        step.explanation_short =
            "The meeting time must not precede either body's start time";
        step.claim = ClaimType::Implication;
        step.assumptions_before.push_back("t_event must be at or after both stated start times");
        step.proof_obligations.push_back(
            {"obl.catch-up.candidate-in-domain",
             "the candidate time belongs to both bodies' active intervals"});
        const std::string observed =
            !domain_computed
                ? "the exact boundary comparison overflowed"
                : in_domain ? "the candidate is in both active intervals"
                            : "the algebraic root precedes the shared active interval";
        step.verifications.push_back(verification(
            "exact rational boundary comparison", observed, EvidenceStrength::CandidateChecked,
            !domain_computed ? VerificationOutcome::Inconclusive
                             : in_domain ? VerificationOutcome::Passed
                                         : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "the candidate satisfies the shared active-time domain";
        check.check_method = "subtract the later start time from the exact candidate";
        check.expected_relation = "t_event - max(start times) is nonnegative";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.catch-up.shared-active-domain",
        !domain_computed ? VerificationOutcome::Inconclusive
                         : in_domain ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        !domain_computed ? "the exact boundary comparison overflowed"
                         : in_domain ? "the candidate is in both active intervals"
                                     : "the candidate precedes the shared active interval");
    if (!domain_computed) {
        result.outcome = CatchUpOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "checking the shared active-time domain exceeds exact arithmetic";
        return result;
    }
    if (!in_domain) {
        result.outcome = CatchUpOutcome::BeforeSharedDomain;
        result.status = DerivationStatus::Unsupported;
        result.detail = "the algebraic root " + rational_text(candidate) +
                        " s is before the shared active interval at " +
                        rational_text(shared_start) + " s";
        return result;
    }

    const Precision precision = answer_precision(problem);
    const Precision time_precision = precision_at_digits(candidate, precision);
    Rational first_event_position;
    Rational second_event_position;
    Precision first_position_precision;
    Precision second_position_precision;
    const bool first_computed = position_at(first, candidate, time_precision,
                                            &first_event_position, &first_position_precision);
    const bool second_computed = position_at(second, candidate, time_precision,
                                             &second_event_position, &second_position_precision);
    if (!first_computed || !second_computed) {
        result.outcome = CatchUpOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "substituting the event time into an original position law exceeds exact arithmetic";
        return result;
    }
    const bool positions_match = rational_equal(first_event_position, second_event_position);
    const NodeId candidate_node = rational_node(arena, candidate);
    const NodeId first_event_node = rational_node(arena, first_event_position);
    const NodeId second_event_node = rational_node(arena, second_event_position);
    const NodeId substituted = arena.binary(Kind::Equals, first_event_node, second_event_node);
    if (arena.failed()) {
        return failed(CatchUpOutcome::ResourceExceeded,
                      DerivationStatus::ResourceLimitReached, status_name(arena.status()));
    }
    result.substituted = substituted;
    result.first_at_event = first_event_node;
    result.second_at_event = second_event_node;

    if (!meter.step())
        return CatchUpResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Evaluate " + problem.first.name + " at the candidate time";
        step.rule_id = "physics.catch-up.verify-first-position";
        step.rule_name = "Original position-law substitution";
        step.explanation_short =
            "Put the exact event time into the first body's original position expression";
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.catch-up.first-position",
             "the candidate can be substituted into the first original position law"});
        step.verifications.push_back(verification(
            "exact substitution into the original position law",
            problem.first.name + " position is " + rational_text(first_event_position) + " m",
            EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
        CheckPayload check;
        check.target_claim = "the candidate determines " + problem.first.name + " position";
        check.check_method = "substitute the exact candidate into x_start + v(t - t_start)";
        check.expected_relation = "an exact position in metres";
        check.observed_result = rational_text(first_event_position) + " m";
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }

    if (!meter.step())
        return CatchUpResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Evaluate " + problem.second.name + " at the candidate time";
        step.rule_id = "physics.catch-up.verify-second-position";
        step.rule_name = "Original position-law substitution";
        step.explanation_short =
            "Put the exact event time into the second body's original position expression";
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.catch-up.second-position",
             "the candidate gives the same position in the second original position law"});
        step.verifications.push_back(verification(
            "exact substitution into both original position laws",
            positions_match ? "both named bodies have exactly the same position"
                            : "the named bodies have different positions",
            EvidenceStrength::CandidateChecked,
            positions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "the candidate makes the two original positions equal";
        check.check_method = "substitute into the second law and compare exact rational positions";
        check.expected_relation = "both positions are exactly equal";
        check.observed_result =
            rational_text(first_event_position) + " m against " +
            rational_text(second_event_position) + " m";
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!positions_match) {
        result.outcome = CatchUpOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the candidate failed substitution into the two original position laws";
        return result;
    }

    // The finer of the two routes, not the first, since positions_match just checked they agree.
    const Precision position_precision =
        finer_precision(first_position_precision, second_position_precision);
    std::string time_text;
    std::string position_text;
    if (!report_text(candidate, precision, &time_text) ||
        !precision_rounded_text(first_event_position, position_precision, &position_text)) {
        result.outcome = CatchUpOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "reporting the measured precision exceeds exact arithmetic";
        return result;
    }
    std::string outside_half_place;
    // time_precision, not the combine it came from, since only it has a place settled at this value.
    ReportStep report = add_report_step(arena, derivation, meter, kEventTimeRule, candidate_node,
                                        candidate, time_precision, rational_text(candidate),
                                        time_text, &outside_half_place);
    if (report == ReportStep::Recorded) {
        report = add_report_step(arena, derivation, meter, kEventPositionRule, first_event_node,
                                 first_event_position, position_precision,
                                 rational_text(first_event_position), position_text,
                                 &outside_half_place);
    }
    if (report == ReportStep::OutsideHalfPlace) {
        result.outcome = CatchUpOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = outside_half_place;
        return result;
    }
    if (report == ReportStep::Unreadable) {
        // Solved, and correctly: the exact values above are the answer. What is missing is the
        // rounded spelling, so the answer stands and the status says the check did not run.
        result.status = DerivationStatus::SolvedButUnchecked;
        result.detail = outside_half_place;
        time_text = rational_text(candidate);
        position_text = rational_text(first_event_position);
    }
    if (report == ReportStep::Halted) {
        if (arena.failed()) {
            return failed(CatchUpOutcome::ResourceExceeded,
                          DerivationStatus::ResourceLimitReached,
                          status_name(arena.status()));
        }
        return CatchUpResult();
    }

    result.outcome = CatchUpOutcome::Solved;
    result.event_time.value = candidate;
    result.event_time.unit = si_unit(time_dimension());
    // The time is a quotient, so the combined figure count is its given and its place follows the
    // value. The position is a sum and carries the place its own terms reached instead.
    result.event_time.precision = time_precision;
    result.event_position.value = first_event_position;
    result.event_position.unit = si_unit(length_dimension());
    result.event_position.precision = position_precision;
    result.event_time_text = time_text;
    result.event_position_text = position_text;
    result.time_unit_text = result.event_time.unit.text;
    result.position_unit_text = result.event_position.unit.text;
    result.detail = problem.first.name + " and " + problem.second.name + " meet at " +
                    time_text + " s and position " + position_text + " m";
    return result;
}

}  // namespace

const char *catch_up_motion_model_name(CatchUpMotionModel model) {
    switch (model) {
        case CatchUpMotionModel::ConstantVelocity: return "constant velocity";
        case CatchUpMotionModel::ConstantAcceleration: return "constant acceleration";
    }
    return "invalid motion model";
}

const char *catch_up_outcome_name(CatchUpOutcome outcome) {
    switch (outcome) {
        case CatchUpOutcome::Solved: return "solved";
        case CatchUpOutcome::InvalidProblem: return "invalid problem";
        case CatchUpOutcome::DuplicateBody: return "duplicate body";
        case CatchUpOutcome::FrameUndeclared: return "frame undeclared";
        case CatchUpOutcome::FrameMismatch: return "frame mismatch";
        case CatchUpOutcome::DimensionMismatch: return "dimension mismatch";
        case CatchUpOutcome::NonlinearMotionUnsupported: return "nonlinear motion unsupported";
        case CatchUpOutcome::NoMeeting: return "no meeting";
        case CatchUpOutcome::AllTimesMeeting: return "meeting at every active time";
        case CatchUpOutcome::BeforeSharedDomain: return "meeting before shared active time";
        case CatchUpOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case CatchUpOutcome::VerificationFailed: return "verification failed";
        case CatchUpOutcome::Cancelled: return "cancelled";
        case CatchUpOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

CatchUpResult solve_catch_up(Arena &arena, Derivation &derivation,
                             const CatchUpProblem &problem, const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    CatchUpResult result =
        solve_body(arena, derivation, meter, problem, budget, &model);

    const bool halted = meter.stopped() || result.outcome == CatchUpOutcome::Cancelled ||
                        result.outcome == CatchUpOutcome::ResourceExceeded;
    // STEP-025: keep the run of records that were checked. A precondition still waiting for its
    // check point leaves the plan unverified, so a halt before that point keeps nothing, which is
    // what actually happened.
    if (halted) {
        const bool cancelled = result.outcome == CatchUpOutcome::Cancelled ||
                               meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        CatchUpResult stopped;
        stopped.outcome =
            cancelled ? CatchUpOutcome::Cancelled : CatchUpOutcome::ResourceExceeded;
        stopped.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        stopped.status = !cancelled ? DerivationStatus::ResourceLimitReached
                         : kept     ? DerivationStatus::Cancelled
                                    : DerivationStatus::NotRecorded;
        stopped.cost = meter.cost();
        record_context(derivation, budget, model, stopped.status, problem);
        return stopped;
    }

    if (result.outcome == CatchUpOutcome::Solved ||
        result.outcome == CatchUpOutcome::NoMeeting ||
        result.outcome == CatchUpOutcome::AllTimesMeeting) {
        result.status = derivation.outcome_from(mark);
    }
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem);
    return result;
}

}  // namespace nps
