#include "nps/physics/unit_conversion.h"

#include <utility>
#include <vector>

#include "nps/core/context.h"
#include "measurement_support.h"

namespace nps {
namespace {

using measure::dimension_node;

NodeId rational_node(Arena &arena, const Rational &value) {
    if (value.den == 1)
        return arena.integer(integer_text(value.num));
    return arena.binary(Kind::Mul, arena.integer(integer_text(value.num)),
                        arena.binary(Kind::Pow, arena.integer(integer_text(value.den)),
                                     arena.integer("-1")));
}

std::string unit_name(const Unit &unit) {
    return unit.text.empty() ? si_unit_text(unit.dimension) : unit.text;
}

NodeId unit_node(Arena &arena, const Unit &unit) {
    return arena.call("unit", {arena.symbol(unit_name(unit)), rational_node(arena, unit.scale),
                               dimension_node(arena, unit.dimension)});
}

NodeId quantity_node(Arena &arena, NodeId value, const Unit &unit, const Precision &precision) {
    return arena.call("quantity", {value, unit_node(arena, unit),
                                   arena.symbol(precision.kind == NumberKind::Exact ? "exact" : "measured"),
                                   arena.integer(integer_text(precision.significant_digits))});
}

NodeId quantity_node(Arena &arena, const Quantity &quantity) {
    return quantity_node(arena, rational_node(arena, quantity.value), quantity.unit,
                         quantity.precision);
}

// Takes the outcome rather than a bool, because a check that could not run is not a check that
// failed and a bool cannot hold the difference.
VerificationRecord verification(const std::string &method, const std::string &detail,
                                EvidenceStrength passing, VerificationOutcome outcome) {
    VerificationRecord record;
    record.method = method;
    record.outcome = outcome;
    record.strength = strength_for(record.outcome, passing);
    record.detail = detail;
    return record;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "units.chain-link-conversion";
    inputs.requested_method =
        "check dimensions, apply exact source-to-SI and SI-to-target factors, report once";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.unit_policy = "exact rational conversion factors with final-only precision";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

UnitConversionResult halted_result(Derivation &derivation, size_t mark, Meter &meter,
                                   const Budget &budget, NodeId model) {
    derivation.rewind_to(mark);
    UnitConversionResult conversion;
    const bool cancelled = meter.halt() == Halt::Cancelled;
    conversion.outcome = cancelled ? UnitConversionOutcome::Cancelled
                                   : UnitConversionOutcome::ResourceExceeded;
    conversion.detail = halt_name(meter.halt());
    conversion.status = cancelled ? DerivationStatus::NotRecorded
                                  : DerivationStatus::ResourceLimitReached;
    conversion.cost = meter.cost();
    record_context(derivation, budget, model, conversion.status);
    return conversion;
}

UnitConversionResult arena_result(Derivation &derivation, size_t mark, Meter &meter,
                                  const Budget &budget, NodeId model) {
    derivation.rewind_to(mark);
    UnitConversionResult conversion;
    conversion.outcome = UnitConversionOutcome::ResourceExceeded;
    conversion.detail = "the expression arena could not record the conversion";
    conversion.status = DerivationStatus::ResourceLimitReached;
    conversion.cost = meter.cost();
    record_context(derivation, budget, model, conversion.status);
    return conversion;
}

UnitConversionResult overflow_result(Derivation &derivation, Meter &meter, const Budget &budget,
                                     NodeId model, const std::string &detail) {
    UnitConversionResult conversion;
    conversion.outcome = UnitConversionOutcome::ArithmeticOverflow;
    conversion.detail = detail;
    conversion.status = DerivationStatus::ResourceLimitReached;
    conversion.cost = meter.cost();
    record_context(derivation, budget, model, conversion.status);
    return conversion;
}

// A check that ran and failed, which is not the same answer as running out of room. Told apart here
// because the student is owed the difference, and the two used to arrive as one.
UnitConversionResult verification_failed_result(Derivation &derivation, Meter &meter,
                                                const Budget &budget, NodeId model,
                                                const std::string &detail) {
    UnitConversionResult conversion;
    conversion.outcome = UnitConversionOutcome::VerificationFailed;
    conversion.detail = detail;
    conversion.status = DerivationStatus::VerificationFailed;
    conversion.cost = meter.cost();
    record_context(derivation, budget, model, conversion.status);
    return conversion;
}

StepId add_transformation(Derivation &derivation, Meter &meter, StepId parent, Step step,
                          NodeId before, NodeId after, const std::string &action,
                          bool reversible = true) {
    if (!meter.step())
        return kNoStep;
    TransformationPayload payload;
    payload.before = before;
    payload.after = after;
    payload.concrete_action = action;
    payload.reversible = reversible;
    return derivation.add_transformation(parent, std::move(step), std::move(payload));
}

std::string reported_quantity_text(const Quantity &quantity, const std::string &number) {
    const std::string unit = unit_name(quantity.unit);
    return unit.empty() || unit == "1" ? number : number + " " + unit;
}

}

const char *unit_conversion_parse_outcome_name(UnitConversionParseOutcome outcome) {
    switch (outcome) {
        case UnitConversionParseOutcome::Parsed: return "parsed";
        case UnitConversionParseOutcome::InvalidQuantity: return "invalid quantity";
        case UnitConversionParseOutcome::InvalidTargetUnit: return "invalid target unit";
    }
    return "unknown";
}

UnitConversionParseResult parse_unit_conversion_problem(const std::string &source,
                                                         const std::string &target_unit,
                                                         UnitConversionProblem *problem) {
    UnitConversionParseResult parsed;
    UnitConversionProblem candidate;
    if (!parse_quantity(source, &candidate.source, &parsed.detail)) {
        parsed.outcome = UnitConversionParseOutcome::InvalidQuantity;
        return parsed;
    }
    if (!parse_unit(target_unit, &candidate.target, &parsed.detail)) {
        parsed.outcome = UnitConversionParseOutcome::InvalidTargetUnit;
        return parsed;
    }
    *problem = std::move(candidate);
    parsed.outcome = UnitConversionParseOutcome::Parsed;
    return parsed;
}

const char *unit_conversion_outcome_name(UnitConversionOutcome outcome) {
    switch (outcome) {
        case UnitConversionOutcome::Converted: return "converted";
        case UnitConversionOutcome::DimensionMismatch: return "dimension mismatch";
        case UnitConversionOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case UnitConversionOutcome::VerificationFailed: return "verification failed";
        case UnitConversionOutcome::Cancelled: return "cancelled";
        case UnitConversionOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

UnitConversionResult solve_unit_conversion(Arena &arena, Derivation &derivation,
                                            const UnitConversionProblem &problem,
                                            const Budget &budget) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    if (meter.stopped())
        return halted_result(derivation, mark, meter, budget, kNoNode);
    const NodeId source_node = quantity_node(arena, problem.source);
    const NodeId model = arena.call("convert_unit", {source_node, unit_node(arena, problem.target)});
    if (arena.failed())
        return arena_result(derivation, mark, meter, budget, kNoNode);

    UnitConversionResult conversion;
    PlanPayload plan;
    plan.strategy_id = "unit.convert.plan";
    plan.selected_strategy = "chain-link unit conversion";
    plan.matched_problem_facts.push_back("source: " +
                                         reported_quantity_text(problem.source,
                                                                rational_text(problem.source.value)));
    plan.matched_problem_facts.push_back("target unit: " + unit_name(problem.target));
    plan.selection_rationale =
        "link the source unit to SI and SI to the target before applying reporting precision";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Convert the quantity through exact unit links";
    plan_step.rule_id = "unit.convert.plan";
    plan_step.rule_name = "Chain-link unit conversion";
    plan_step.explanation_short = "Check dimensions, compose exact factors, then report once";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.unit-convert.dimensions-match",
        "source and target units have equal dimensions", "dimension comparison",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked before composing unit factors");
    register_strategy_precondition(
        plan, plan_step, "pre.unit-convert.exact-factors",
        "every conversion factor fits exact rational arithmetic", "exact rational operations",
        EvidenceStrength::StructurallyValid, VerificationOutcome::NotAttempted,
        "checked while composing the conversion chain");
    if (!meter.step())
        return halted_result(derivation, mark, meter, budget, model);
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const bool dimensions_match = problem.source.unit.dimension == problem.target.dimension;
    const std::string dimensions_observed = dimension_text(problem.source.unit.dimension) + " and " +
                                            dimension_text(problem.target.dimension);
    Step dimension_step;
    dimension_step.phase = "check";
    dimension_step.goal = "Check unit dimensions";
    dimension_step.rule_id = "unit.convert.check-dimension";
    dimension_step.rule_name = "Unit conversion precondition";
    dimension_step.explanation_short = "Only units of the same dimension are convertible";
    dimension_step.claim = ClaimType::Definition;
    dimension_step.proof_obligations.push_back(
        {"obl.unit-conversion.dimensions-match", "source and target dimensions are equal"});
    dimension_step.verifications.push_back(
        verification("dimension comparison", dimensions_observed,
                     EvidenceStrength::DimensionallyValid,
                     dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
    if (!meter.step())
        return halted_result(derivation, mark, meter, budget, model);
    CheckPayload dimension_check;
    dimension_check.target_claim = "source and target units are compatible";
    dimension_check.check_method = "dimension comparison";
    dimension_check.expected_relation = "equal dimensions";
    dimension_check.observed_result = dimensions_observed;
    derivation.add_check(plan_id, std::move(dimension_step), std::move(dimension_check));
    derivation.complete_plan_precondition(
        plan_id, "pre.unit-convert.dimensions-match",
        dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimensions_observed);
    if (!dimensions_match) {
        conversion.outcome = UnitConversionOutcome::DimensionMismatch;
        conversion.detail = "cannot convert " + dimension_text(problem.source.unit.dimension) +
                            " to " + dimension_text(problem.target.dimension);
        conversion.status = DerivationStatus::InvalidInput;
        conversion.cost = meter.cost();
        record_context(derivation, budget, model, conversion.status);
        return conversion;
    }

    conversion.source_to_si_factor = problem.source.unit.scale;
    Rational one;
    one.num = 1;
    if (!meter.rewrite())
        return halted_result(derivation, mark, meter, budget, model);
    if (!rational_div(one, problem.target.scale, &conversion.si_to_target_factor))
        return overflow_result(derivation, meter, budget, model,
                               "the target-to-SI factor cannot be inverted exactly");
    if (!meter.rewrite())
        return halted_result(derivation, mark, meter, budget, model);
    if (!rational_mul(conversion.source_to_si_factor, conversion.si_to_target_factor,
                      &conversion.combined_factor))
        return overflow_result(derivation, meter, budget, model,
                               "the chained conversion factor does not fit exact arithmetic");
    Rational converted_value;
    if (!meter.rewrite())
        return halted_result(derivation, mark, meter, budget, model);
    if (!rational_mul(problem.source.value, conversion.combined_factor, &converted_value))
        return overflow_result(derivation, meter, budget, model,
                               "the converted value does not fit exact arithmetic");
    derivation.complete_plan_precondition(
        plan_id, "pre.unit-convert.exact-factors", VerificationOutcome::Passed,
        "the source, target, combined and value factors fit exact rational arithmetic");

    Quantity converted;
    converted.value = converted_value;
    converted.unit = problem.target;
    // An exact factor keeps the figure count, so that much copies, but the place moves with the
    // scale: 9.96 km is good to 10 m, not to the hundredth of a metre the source place named.
    converted.precision = precision_at_digits(converted_value, problem.source.precision);

    Unit si_unit;
    si_unit.text = si_unit_text(problem.source.unit.dimension);
    si_unit.dimension = problem.source.unit.dimension;
    si_unit.scale.num = 1;
    si_unit.scale.den = 1;
    const NodeId si_value = arena.binary(Kind::Mul, rational_node(arena, problem.source.value),
                                         rational_node(arena, conversion.source_to_si_factor));
    const NodeId si_quantity = quantity_node(arena, si_value, si_unit, problem.source.precision);
    const NodeId converted_node = quantity_node(arena, converted);
    if (arena.failed())
        return arena_result(derivation, mark, meter, budget, model);

    Step source_step;
    source_step.phase = "solve";
    source_step.goal = "Link the source unit to SI";
    source_step.rule_id = "unit.convert.source-to-si";
    source_step.rule_name = "Exact source-to-SI conversion";
    source_step.explanation_short = "Apply the source unit's exact SI scale";
    source_step.explanation_detailed =
        "Reach for this as the first half of any unit conversion, whatever the two units are. "
        "Rather than learn a factor for every pair, the value is taken to the SI unit first and "
        "then out to the target, so a table of one factor per unit covers every pair. This step is "
        "the way in: multiply by what one source unit is worth in SI.";
    source_step.claim = ClaimType::EquivalentExpression;
    source_step.proof_obligations.push_back(
        {"obl.unit-conversion.source-factor-exact", "the source-to-SI factor is exact"});
    source_step.verifications.push_back(
        verification("unit table", rational_text(conversion.source_to_si_factor),
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    const std::string source_action = "multiply by " +
                                      rational_text(conversion.source_to_si_factor) + " to change " +
                                      unit_name(problem.source.unit) + " to " + unit_name(si_unit);
    const StepId source_step_id = add_transformation(
        derivation, meter, plan_id, std::move(source_step), source_node, si_quantity, source_action);
    if (source_step_id == kNoStep)
        return halted_result(derivation, mark, meter, budget, model);

    Step target_step;
    target_step.phase = "solve";
    target_step.goal = "Link SI to the target unit";
    target_step.rule_id = "unit.convert.si-to-target";
    target_step.rule_name = "Exact SI-to-target conversion";
    target_step.explanation_short = "Apply the inverse of the target unit's exact SI scale";
    target_step.explanation_detailed =
        "Reach for this as the second half, once the value is in SI and has to come back out. It "
        "divides by what one target unit is worth in SI, which is the same table entry as the step "
        "above used, read the other way. Dividing rather than multiplying is the part worth "
        "watching: going in multiplies, coming out divides, and swapping the two is the usual way "
        "a conversion lands out by the square of the factor.";
    target_step.claim = ClaimType::EquivalentExpression;
    target_step.proof_obligations.push_back(
        {"obl.unit-conversion.target-factor-exact", "the SI-to-target factor is exact"});
    target_step.verifications.push_back(
        verification("unit table", rational_text(conversion.si_to_target_factor),
                     EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
    const std::string target_action = "multiply by " +
                                      rational_text(conversion.si_to_target_factor) + " to change " +
                                      unit_name(si_unit) + " to " + unit_name(problem.target);
    const StepId target_step_id =
        add_transformation(derivation, meter, source_step_id, std::move(target_step), si_quantity,
                           converted_node, target_action);
    if (target_step_id == kNoStep)
        return halted_result(derivation, mark, meter, budget, model);

    std::string reported_number = rational_text(converted.value);
    if (converted.precision.kind == NumberKind::Measured) {
        const std::string exact_number = reported_number;
        if (!meter.rewrite())
            return halted_result(derivation, mark, meter, budget, model);
        if (!rounded_text(converted.value, converted.precision.significant_digits, &reported_number))
            return overflow_result(derivation, meter, budget, model,
                                   "the final reporting precision could not be represented exactly");
        // Before the step is built, so a report the check rejects leaves no transformation to the
        // value it rejected. A wrong rounding used to reach the student as an arithmetic overflow,
        // which told them the calculator ran out of room when their answer had been checked.
        const HalfPlace checked =
            precision_rounding_valid(converted.value, reported_number, converted.precision);
        if (checked == HalfPlace::Outside) {
            return verification_failed_result(
                derivation, meter, budget, model,
                reported_number + " is further than half a unit in its last place from " +
                    exact_number);
        }
        if (checked == HalfPlace::Unreadable) {
            // Nothing was compared, so there is no failed check to refuse on. The conversion is
            // exact and stands, and the rounded spelling is what gets withheld. The record carries
            // the attempt, and outcome_from turns the inconclusive check into the status.
            //
            // Metered like the reporting transformation it replaces, since the other branch spends
            // its step inside add_transformation and this one records a step of its own.
            if (!meter.step())
                return halted_result(derivation, mark, meter, budget, model);
            Step unchecked;
            unchecked.phase = "report";
            unchecked.goal = "Apply final reporting precision";
            unchecked.rule_id = "unit.convert.report-precision";
            unchecked.rule_name = "Final-only precision";
            unchecked.claim = ClaimType::Definition;
            unchecked.explanation_short = "Round only after the exact conversion is complete";
            unchecked.explanation_detailed =
                "The rounded spelling could not be read back as a decimal, so it was never compared "
                "against the exact value. The exact value is reported instead, since showing a "
                "rounding nothing checked would be showing an answer with no evidence behind it.";
            // Both of the rule's obligations, because the degrade reuses the rule and the schema is
            // the rule's, not the branch's. Rounding once at the end is what this step did whichever
            // way the comparison went, so only the half-place obligation goes unanswered.
            unchecked.proof_obligations.push_back(
                {"obl.unit-conversion.rounding-final",
                 "precision is applied once to the final value"});
            unchecked.verifications.push_back(verification(
                "significant figures", std::to_string(converted.precision.significant_digits),
                EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
            unchecked.proof_obligations.push_back(
                {"obl.unit-conversion.rounding-within-half-place",
                 "the reported value is within half a unit in the last place of the exact one"});
            const std::string observed =
                reported_number + " could not be read back as a decimal, so it was never compared "
                                  "against " +
                exact_number;
            unchecked.verifications.push_back(verification("exact half-place comparison", observed,
                                                           EvidenceStrength::CandidateChecked,
                                                           VerificationOutcome::Inconclusive));
            CheckPayload check;
            check.target_claim =
                "the reported value is within half a unit in the last place of the exact one";
            check.check_method =
                "read the rounded text back and compare it against the exact value";
            check.expected_relation = "the difference is at most half a unit in the last place";
            check.observed_result = observed;
            derivation.add_check(target_step_id, std::move(unchecked), std::move(check));
            reported_number = exact_number;
        } else {
        Step report_step;
        report_step.phase = "report";
        report_step.goal = "Apply final reporting precision";
        report_step.rule_id = "unit.convert.report-precision";
        report_step.rule_name = "Final-only precision";
        report_step.explanation_short = "Round only after the exact conversion is complete";
        report_step.explanation_detailed =
            "Reach for this once, at the very end, and never partway through. A measured value is "
            "only as good as the figures it was written with, so the answer is reported to the "
            "fewest significant figures among the measurements it came from. Rounding earlier "
            "would throw away figures the final rounding cannot get back, which is why every step "
            "above this one keeps the exact value.";
        report_step.claim = ClaimType::Definition;
        report_step.proof_obligations.push_back(
            {"obl.unit-conversion.rounding-final", "precision is applied once to the final value"});
        report_step.verifications.push_back(verification(
            "significant figures", std::to_string(converted.precision.significant_digits),
            EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        report_step.proof_obligations.push_back(
            {"obl.unit-conversion.rounding-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        report_step.verifications.push_back(verification(
            "exact half-place comparison",
            reported_number + " is within half a unit in the last place of " + exact_number,
            EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
        const NodeId reported_value = reported_number.find('.') == std::string::npos
                                          ? arena.integer(reported_number)
                                          : arena.decimal(reported_number);
        const NodeId reported_quantity =
            quantity_node(arena, reported_value, converted.unit, converted.precision);
        if (arena.failed())
            return arena_result(derivation, mark, meter, budget, model);
        if (add_transformation(derivation, meter, target_step_id, std::move(report_step),
                               converted_node, reported_quantity, "report " + reported_number,
                               false) == kNoStep)
            return halted_result(derivation, mark, meter, budget, model);
        }
    }

    conversion.value = converted;
    conversion.has_value = true;
    conversion.value_text = reported_quantity_text(converted, reported_number);
    conversion.outcome = UnitConversionOutcome::Converted;
    conversion.detail = "converted with exact unit factors";
    conversion.status = derivation.outcome_from(mark);
    conversion.cost = meter.cost();
    record_context(derivation, budget, model, conversion.status);
    return conversion;
}

}
