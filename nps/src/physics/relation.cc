#include "nps/physics/relation.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {
namespace {

RelationResult failed(RelationOutcome outcome, DerivationStatus status, const std::string &detail) {
    RelationResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

bool normalize_copy(const Rational &source, Rational *normalized) {
    *normalized = source;
    return normalise(&normalized->num, &normalized->den);
}

bool valid_quantity(const Quantity &quantity, std::string *detail) {
    Rational normalized;
    if (!normalize_copy(quantity.value, &normalized)) {
        *detail = "the quantity has an invalid exact value";
        return false;
    }
    if (!normalize_copy(quantity.unit.scale, &normalized) || normalized.num <= 0) {
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

std::string value_text(const Quantity &quantity) {
    Rational normalized;
    if (!normalize_copy(quantity.value, &normalized))
        return "invalid exact value";
    std::string text = rational_text(normalized);
    if (quantity.precision.kind == NumberKind::Measured) {
        std::string measured;
        if (rounded_text(normalized, quantity.precision.significant_digits, &measured))
            text = measured;
    }
    return text;
}

std::string known_text(const RelationModel &model, const RelationKnown &known) {
    std::string text =
        std::string(relation_term(model, known.index).name) + " = " + value_text(known.quantity);
    if (!known.quantity.unit.text.empty())
        text += " " + known.quantity.unit.text;
    return text;
}

Unit si_unit(const Dimension &dimension) {
    Unit unit;
    unit.dimension = dimension;
    unit.text = si_unit_text(dimension);
    unit.scale.num = 1;
    unit.scale.den = 1;
    return unit;
}

NodeId rational_node(Arena &arena, const Rational &rational) {
    if (rational.den == 1)
        return arena.integer(integer_text(rational.num));
    NodeId numerator = arena.integer(integer_text(rational.num));
    NodeId denominator = arena.integer(integer_text(rational.den));
    NodeId reciprocal = arena.binary(Kind::Pow, denominator, arena.integer("-1"));
    return arena.binary(Kind::Mul, numerator, reciprocal);
}

bool rational_of_node(const Arena &arena, NodeId id, Rational *value) {
    if (id == kNoNode)
        return false;
    const Node &node = arena.at(id);
    int64_t integer;
    if (node.kind == Kind::Integer || node.kind == Kind::Neg) {
        if (small_integer(arena, id, &integer)) {
            value->num = integer;
            value->den = 1;
            return true;
        }
        if (node.kind != Kind::Neg)
            return false;
        Rational inner;
        Rational zero;
        return rational_of_node(arena, arena.children(node)[0], &inner) &&
               rational_sub(zero, inner, value);
    }
    const ChildView children = arena.children(node);
    if (node.kind != Kind::Mul || children.size() != 2)
        return false;
    Rational numerator;
    if (!rational_of_node(arena, children[0], &numerator) || numerator.den != 1)
        return false;
    const Node &reciprocal = arena.at(children[1]);
    const ChildView reciprocal_children = arena.children(reciprocal);
    int64_t denominator;
    int64_t exponent;
    if (reciprocal.kind != Kind::Pow || reciprocal_children.size() != 2 ||
        !small_integer(arena, reciprocal_children[0], &denominator) ||
        !small_integer(arena, reciprocal_children[1], &exponent) || exponent != -1 ||
        denominator == 0) {
        return false;
    }
    value->num = numerator.num;
    value->den = denominator;
    return normalise(&value->num, &value->den);
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

Step transformation_step(const std::string &goal, const std::string &rule_id,
                         const char *rule_name, const std::string &explanation,
                         const std::string &detailed) {
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = ClaimType::SolutionSetPreserved;
    return step;
}

// The right-hand side dimension the model claims, so a mistyped power is caught before substitution.
bool product_dimension(const RelationModel &model, Dimension *out) {
    Dimension product = model.constant_dimension;
    for (size_t index = 0; index < model.factor_count; ++index) {
        Dimension raised;
        if (!dimension_power(model.factors[index].dimension, model.factors[index].power, &raised))
            return false;
        Dimension combined;
        if (!dimension_multiply(product, raised, &combined))
            return false;
        product = combined;
    }
    *out = product;
    return true;
}

// The exact right-hand side value, used both to build the model and to check the candidate.
bool product_value(const RelationModel &model, const Quantity *quantities, Rational *out) {
    Rational product = model.constant;
    for (size_t index = 0; index < model.factor_count; ++index) {
        Rational raised;
        if (!rational_power(quantities[index + 1].value, model.factors[index].power, &raised))
            return false;
        Rational combined;
        if (!rational_mul(product, raised, &combined))
            return false;
        product = combined;
    }
    *out = product;
    return true;
}

NodeId factor_expression(Arena &arena, NodeId base, int power) {
    if (power == 1)
        return base;
    return arena.binary(Kind::Pow, base, arena.integer(std::to_string(power)));
}

NodeId right_hand_side(Arena &arena, const RelationModel &model, const NodeId *expressions) {
    NodeId product = kNoNode;
    if (model.constant_symbol != nullptr || model.constant.num != model.constant.den)
        product = expressions[0];
    for (size_t index = 0; index < model.factor_count; ++index) {
        NodeId factor =
            factor_expression(arena, expressions[index + 2], model.factors[index].power);
        product = product == kNoNode ? factor : arena.binary(Kind::Mul, product, factor);
    }
    return product;
}

void record_context(Derivation &derivation, const RelationModel &model, const Budget &budget,
                    NodeId equation, DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = model.family_id;
    inputs.requested_method = model.method_text;
    inputs.normalized_problem_model = equation;
    inputs.original_expression = derivation.request.original_expression;
    inputs.branch_convention = "real domain";
    for (const char *condition : model.conditions) {
        if (condition != nullptr)
            inputs.active_assumptions.push_back(condition);
    }
    if (model.constant_note != nullptr)
        inputs.active_assumptions.push_back(model.constant_note);
    inputs.unit_policy = "validate dimensions before substitution, convert exactly to SI, and round "
                         "only the reported answer";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

RelationResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                          const RelationModel &model, const RelationProblem &problem,
                          const Budget &budget, NodeId *equation_out) {
    const size_t term_count = relation_term_count(model);
    if (model.factor_count == 0 || model.factor_count > kRelationMaxFactors)
        return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the relation model declares no usable factors");
    if (problem.unknown >= term_count)
        return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the requested unknown is not a position in " +
                          std::string(model.equation_text));
    const RelationTerm &unknown_term = relation_term(model, problem.unknown);
    if (unknown_term.power != 1) {
        return failed(RelationOutcome::UnsupportedUnknown, DerivationStatus::Unsupported,
                      std::string(unknown_term.name) + " occurs at power " +
                          std::to_string(unknown_term.power) + " in " + model.equation_text +
                          ", which this exact linear path does not isolate");
    }

    const RelationKnown *knowns[kRelationMaxFactors + 1] = {nullptr};
    for (const RelationKnown &known : problem.knowns) {
        if (known.index >= term_count)
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "a known position is not a position in " +
                              std::string(model.equation_text));
        if (known.index == problem.unknown)
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(relation_term(model, known.index).name) +
                              " is both known and unknown");
        if (knowns[known.index])
            return failed(RelationOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                          std::string(relation_term(model, known.index).name) + " is given twice");
        std::string invalid_detail;
        if (!valid_quantity(known.quantity, &invalid_detail))
            return failed(RelationOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(relation_term(model, known.index).name) + ": " +
                              invalid_detail);
        const Dimension expected = relation_term(model, known.index).dimension;
        if (known.quantity.unit.dimension != expected) {
            return failed(RelationOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(model, known) + " has dimension " +
                              dimension_text(known.quantity.unit.dimension) + ", but " +
                              relation_term(model, known.index).name + " requires " +
                              dimension_text(expected));
        }
        knowns[known.index] = &known;
    }
    for (size_t index = 0; index < term_count; ++index) {
        if (index != problem.unknown && !knowns[index])
            return failed(RelationOutcome::MissingKnown, DerivationStatus::InvalidInput,
                          std::string("missing known ") + relation_term(model, index).name);
    }

    NodeId symbols[kRelationMaxFactors + 2] = {kNoNode};
    symbols[0] = model.constant_symbol != nullptr ? arena.symbol(model.constant_symbol)
                                                  : rational_node(arena, model.constant);
    symbols[1] = arena.symbol(model.target.symbol);
    for (size_t index = 0; index < model.factor_count; ++index)
        symbols[index + 2] = arena.symbol(model.factors[index].symbol);
    NodeId equation = arena.binary(Kind::Equals, symbols[1], right_hand_side(arena, model, symbols));
    *equation_out = equation;
    if (arena.failed())
        return failed(RelationOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    PlanPayload plan;
    const std::string definition_rule = std::string(model.rule_prefix) + ".definition";
    plan.strategy_id = definition_rule;
    plan.selected_strategy = model.strategy_text;
    for (const RelationKnown &known : problem.knowns)
        plan.matched_problem_facts.push_back(known_text(model, known));
    plan.matched_problem_facts.push_back(std::string("find ") + unknown_term.name);
    plan.alternatives_considered.push_back("general symbolic backend isolation");
    plan.selection_rationale =
        std::string(model.equation_text) +
        " is linear in the requested unknown after exact SI substitution, so the local exact linear "
        "solver is sufficient and deterministic";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Find ") + unknown_term.name;
    plan_step.rule_id = definition_rule;
    plan_step.rule_name = model.rule_name;
    plan_step.explanation_short =
        std::string("Use ") + model.equation_text + " and solve for the requested quantity";
    plan_step.claim = ClaimType::NoClaim;
    for (const char *condition : model.conditions) {
        if (condition != nullptr)
            plan_step.assumptions_before.push_back(condition);
    }
    if (model.constant_note != nullptr)
        plan_step.assumptions_before.push_back(model.constant_note);
    register_strategy_precondition(
        plan, plan_step, std::string(model.rule_prefix) + ".compatible-dimensions",
        std::string("every quantity in ") + model.equation_text + " uses compatible dimensions",
        "dimensional analysis", EvidenceStrength::DimensionallyValid,
        VerificationOutcome::NotAttempted, "checked against the registered relation");
    register_strategy_precondition(
        plan, plan_step, std::string(model.rule_prefix) + ".linear-unknown",
        std::string("the relation is linear in ") + unknown_term.name,
        "registered relation-position model", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed,
        std::string(unknown_term.symbol) + " occurs at power one in " + model.equation_text);
    if (!meter.step())
        return RelationResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    Dimension right_dimension;
    const bool dimension_fits = product_dimension(model, &right_dimension);
    const bool dimensions_match = dimension_fits && model.target.dimension == right_dimension;
    const std::string dimension_observed =
        dimension_fits ? dimension_text(model.target.dimension) + " against " +
                             dimension_text(right_dimension)
                       : "the right-hand side dimension does not fit";
    if (!meter.step())
        return RelationResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the dimensions of ") + model.equation_text;
        step.rule_id = std::string(model.rule_prefix) + ".check-dimensions";
        step.rule_name = "Dimensional analysis";
        step.explanation_short =
            std::string("Both sides of ") + model.equation_text + " must have the same dimension";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back({std::string(model.rule_prefix) + ".dimensions-agree",
                                         std::string("both sides of ") + model.equation_text +
                                             " have the same dimension"});
        step.verifications.push_back(verification(
            "dimensional analysis", dimension_observed, EvidenceStrength::DimensionallyValid,
            dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = std::string(model.equation_text) + " is dimensionally consistent";
        check.check_method = "raise each factor dimension to its power and multiply";
        check.expected_relation = "equal dimensions";
        check.observed_result = dimension_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, std::string(model.rule_prefix) + ".compatible-dimensions",
        dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimension_observed);
    if (!dimension_fits)
        return failed(RelationOutcome::ArithmeticOverflow, DerivationStatus::VerificationFailed,
                      "the relation dimension does not fit");
    if (!dimensions_match)
        return failed(RelationOutcome::DimensionMismatch, DerivationStatus::VerificationFailed,
                      std::string(model.equation_text) + " is dimensionally inconsistent");

    Quantity si_quantities[kRelationMaxFactors + 1];
    std::string conversions;
    bool converted_units = false;
    for (size_t index = 0; index < term_count; ++index) {
        if (index == problem.unknown)
            continue;
        const RelationKnown &known = *knowns[index];
        Rational value;
        if (!to_si(known.quantity, &value))
            return failed(RelationOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "converting " + known_text(model, known) +
                              " to SI exceeds exact integer arithmetic");
        Quantity converted;
        converted.value = value;
        converted.unit = si_unit(relation_term(model, index).dimension);
        converted.precision = known.quantity.precision;
        si_quantities[index] = converted;
        Rational scale;
        normalize_copy(known.quantity.unit.scale, &scale);
        if (scale.num != scale.den) {
            converted_units = true;
            if (!conversions.empty())
                conversions += ", ";
            RelationKnown converted_known;
            converted_known.index = index;
            converted_known.quantity = converted;
            conversions += known_text(model, known) + " becomes " +
                           known_text(model, converted_known);
        }
    }

    bool conversions_agree = true;
    size_t conversions_checked = 0;
    std::string conversion_observed;
    for (size_t index = 0; index < term_count; ++index) {
        if (index == problem.unknown)
            continue;
        const RelationKnown &known = *knowns[index];
        Rational expected;
        static_cast<void>(rational_mul(known.quantity.value, known.quantity.unit.scale, &expected));
        ++conversions_checked;
        if (rational_equal(si_quantities[index].value, expected))
            continue;
        conversions_agree = false;
        if (!conversion_observed.empty())
            conversion_observed += ", ";
        conversion_observed += std::string(relation_term(model, index).symbol) +
                               " was stored as " + rational_text(si_quantities[index].value) +
                               " rather than " + rational_text(expected);
    }
    if (conversion_observed.empty())
        conversion_observed = std::to_string(conversions_checked) +
                              " stored values equal the given times its table scale";
    if (converted_units) {
        if (!meter.step())
            return RelationResult();
        Step step = transformation_step(
            "Convert the known quantities to SI",
            std::string(model.rule_prefix) + ".convert-units", "Unit conversion",
            "Apply each unit's exact scale to SI",
            std::string("Convert every known quantity with its exact unit scale before "
                        "substituting into ") +
                model.equation_text + ", so the relation uses consistent SI units throughout.");
        step.verifications.push_back(
            verification("recompute each stored value as the given times its table scale",
                         conversion_observed, EvidenceStrength::CandidateChecked,
                         conversions_agree ? VerificationOutcome::Passed
                                           : VerificationOutcome::Failed));
        step.proof_obligations.push_back(
            {"obl.physics.scale-preserves-solutions",
             "each quantity's stored SI value is its given value times the table factor"});
        TransformationPayload payload;
        payload.before = equation;
        payload.after = equation;
        payload.concrete_action = conversions;
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }
    if (!conversions_agree)
        return failed(RelationOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      "a converted quantity does not equal the given times its table scale");

    NodeId expressions[kRelationMaxFactors + 2] = {kNoNode};
    expressions[0] = model.constant_symbol != nullptr ? rational_node(arena, model.constant)
                                                      : symbols[0];
    for (size_t index = 0; index < term_count; ++index) {
        expressions[index + 1] = index == problem.unknown
                                     ? symbols[index + 1]
                                     : rational_node(arena, si_quantities[index].value);
    }
    NodeId substituted =
        arena.binary(Kind::Equals, expressions[1], right_hand_side(arena, model, expressions));
    if (arena.failed())
        return failed(RelationOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    if (!meter.step())
        return RelationResult();
    {
        Step step = transformation_step(
            "Substitute the known SI values", std::string(model.rule_prefix) + ".substitute",
            "Substitution", "Replace each known symbol by its exact SI value",
            model.substitution_detail);
        step.verifications.push_back(verification(
            "typed known-quantity lookup",
            std::to_string(term_count - 1) + " known quantities were substituted",
            EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        TransformationPayload payload;
        payload.before = equation;
        payload.after = substituted;
        payload.concrete_action =
            std::string("Substitute the known quantities into ") + model.equation_text +
            " after exact SI conversion";
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    RelationResult result;
    result.unknown = expressions[problem.unknown + 1];
    result.equation = equation;
    result.substituted = substituted;
    const Budget linear_budget = remaining_budget(budget, meter);
    const SolveResult solved =
        solve_linear(arena, derivation, substituted, result.unknown, linear_budget);
    if (!charge(meter, solved.cost))
        return RelationResult();
    if (solved.outcome == SolveOutcome::Cancelled) {
        result.outcome = RelationOutcome::Cancelled;
        result.status = DerivationStatus::NotRecorded;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::ResourceExceeded) {
        result.outcome = RelationOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::NoSolution || solved.outcome == SolveOutcome::AllValues) {
        result.outcome = RelationOutcome::InvalidProblem;
        result.status = DerivationStatus::InvalidInput;
        result.detail = "the given values do not determine one " + std::string(unknown_term.name) +
                        ": " + solved.detail;
        return result;
    }
    if (solved.outcome != SolveOutcome::Solved) {
        const bool overflow = solved.status == DerivationStatus::ResourceLimitReached;
        result.outcome =
            overflow ? RelationOutcome::ArithmeticOverflow : RelationOutcome::VerificationFailed;
        result.status = overflow ? DerivationStatus::ResourceLimitReached : solved.status;
        result.detail = solved.detail;
        return result;
    }

    Rational candidate;
    if (!rational_of_node(arena, solved.solution, &candidate)) {
        result.outcome = RelationOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the linear solver returned a value that is not an exact rational";
        return result;
    }
    si_quantities[problem.unknown].value = candidate;
    si_quantities[problem.unknown].unit = si_unit(unknown_term.dimension);
    Precision answer_precision;
    for (size_t index = 0; index < term_count; ++index) {
        if (index != problem.unknown)
            answer_precision = precision_combine(answer_precision, si_quantities[index].precision);
    }
    si_quantities[problem.unknown].precision = answer_precision;

    Rational right_value;
    const bool check_computed = product_value(model, si_quantities, &right_value);
    const bool candidate_passes =
        check_computed && rational_equal(si_quantities[kRelationTarget].value, right_value);
    if (!meter.step())
        return RelationResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the candidate in ") + model.equation_text;
        step.rule_id = std::string(model.rule_prefix) + ".check-candidate";
        step.rule_name = "Substitution check";
        step.explanation_short = std::string("Put the candidate back into ") + model.equation_text;
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back({std::string(model.rule_prefix) + ".candidate-satisfies",
                                          std::string("the candidate satisfies ") +
                                              model.equation_text});
        const VerificationOutcome verification_outcome =
            !check_computed ? VerificationOutcome::Inconclusive
                            : candidate_passes ? VerificationOutcome::Passed
                                               : VerificationOutcome::Failed;
        const std::string observed =
            !check_computed ? "the exact product exceeds integer arithmetic"
                            : candidate_passes ? "both sides are exactly equal"
                                               : "the two sides are not equal";
        step.verifications.push_back(
            verification("exact substitution into the original relation", observed,
                         EvidenceStrength::CandidateChecked, verification_outcome));
        CheckPayload check;
        check.target_claim = std::string(unknown_term.symbol) + " = " + rational_text(candidate) +
                             " satisfies " + model.equation_text;
        check.check_method = "substitute every exact SI value into the relation";
        check.expected_relation = std::string("both sides of ") + model.equation_text + " agree";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!check_computed) {
        result.outcome = RelationOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "checking the candidate exceeds exact integer arithmetic";
        return result;
    }
    if (!candidate_passes) {
        result.outcome = RelationOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = std::string("the candidate failed substitution into ") +
                        model.equation_text;
        return result;
    }

    result.outcome = RelationOutcome::Solved;
    result.value = solved.solution;
    result.quantity = si_quantities[problem.unknown];
    result.quantity.precision = precision_at_digits(candidate, result.quantity.precision);
    result.value_text = rational_text(candidate);
    result.unit_text = result.quantity.unit.text;
    if (result.quantity.precision.kind != NumberKind::Measured)
        return result;

    std::string reported;
    const unsigned digits = result.quantity.precision.significant_digits;
    if (!rounded_text(candidate, digits, &reported)) {
        result.outcome = RelationOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "reporting the measured precision exceeds exact integer arithmetic";
        result.value = kNoNode;
        result.value_text.clear();
        result.unit_text.clear();
        return result;
    }
    if (reported == result.value_text)
        return result;
    const HalfPlace checked =
        precision_rounding_valid(candidate, reported, result.quantity.precision);
    if (checked == HalfPlace::Outside) {
        result.outcome = RelationOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = reported + " is further than half a unit in its last place from " +
                        result.value_text;
        result.value = kNoNode;
        result.value_text.clear();
        result.unit_text.clear();
        return result;
    }
    if (!meter.step())
        return RelationResult();
    if (checked == HalfPlace::Unreadable) {
        // Never compared, so the exact value stands and only the rounded spelling is withheld.
        result.status = DerivationStatus::SolvedButUnchecked;
        result.detail = reported +
                        " could not be read back as a decimal, so it was never compared against " +
                        result.value_text;
        Step step;
        step.phase = "report";
        step.goal = "Report the answer to the measured precision";
        step.rule_id = std::string(model.rule_prefix) + ".significant-figures";
        step.rule_name = "Significant figures";
        step.claim = ClaimType::NoClaim;
        step.explanation_short = "Use the fewest significant figures among the measured givens";
        step.explanation_detailed =
            "The rounded spelling could not be read back as a decimal, so it was never compared "
            "against the exact value. The exact value is reported instead, since showing a "
            "rounding nothing checked would be showing an answer with no evidence behind it.";
        step.proof_obligations.push_back(
            {"obl.physics.reported-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        step.verifications.push_back(verification("exact comparison against the unrounded value",
                                                  result.detail, EvidenceStrength::CandidateChecked,
                                                  VerificationOutcome::Inconclusive));
        CheckPayload check;
        check.target_claim =
            "the reported value is within half a unit in the last place of the exact one";
        check.check_method = "read the rounded text back and compare it against the exact value";
        check.expected_relation = "the difference is at most half a unit in the last place";
        check.observed_result = result.detail;
        derivation.add_check(kNoStep, std::move(step), std::move(check));
        return result;
    }
    Step step;
    step.phase = "report";
    step.goal = "Report the answer to the measured precision";
    step.rule_id = std::string(model.rule_prefix) + ".significant-figures";
    step.rule_name = "Significant figures";
    step.explanation_short = "Use the fewest significant figures among the measured givens";
    step.explanation_detailed =
        "A measured value is only as good as the figures it was written with, so the answer is "
        "reported to the fewest significant figures among the measurements it came from. Every "
        "step above this one keeps the exact value, because rounding partway through throws away "
        "figures the final rounding cannot get back.";
    step.claim = ClaimType::NoClaim;
    step.verifications.push_back(verification(
        "exact comparison against the unrounded value",
        reported + " is within half a unit in the last place of " + result.value_text,
        EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
    step.proof_obligations.push_back(
        {"obl.physics.reported-within-half-place",
         "the reported value is within half a unit in the last place of the exact one"});
    TransformationPayload payload;
    payload.before = solved.solution;
    payload.after = reported.find('.') == std::string::npos ? arena.integer(reported)
                                                            : arena.decimal(reported);
    payload.concrete_action = "Report " + result.value_text + " as " + reported;
    payload.reversible = false;
    if (arena.failed())
        return failed(RelationOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
    result.value_text = reported;
    return result;
}

}  // namespace

size_t relation_term_count(const RelationModel &model) { return model.factor_count + 1; }

const RelationTerm &relation_term(const RelationModel &model, size_t index) {
    if (index == kRelationTarget || index > model.factor_count)
        return model.target;
    return model.factors[index - 1];
}

const char *relation_outcome_name(RelationOutcome outcome) {
    switch (outcome) {
        case RelationOutcome::Solved: return "solved";
        case RelationOutcome::InvalidProblem: return "invalid problem";
        case RelationOutcome::MissingKnown: return "missing known";
        case RelationOutcome::DuplicateKnown: return "duplicate known";
        case RelationOutcome::DimensionMismatch: return "dimension mismatch";
        case RelationOutcome::UnsupportedUnknown: return "unsupported unknown";
        case RelationOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case RelationOutcome::VerificationFailed: return "verification failed";
        case RelationOutcome::Cancelled: return "cancelled";
        case RelationOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

RelationResult solve_relation(Arena &arena, Derivation &derivation, const RelationModel &model,
                              const RelationProblem &problem, const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId equation = kNoNode;
    RelationResult result =
        solve_body(arena, derivation, meter, model, problem, budget, &equation);

    const bool nested_halt = result.outcome == RelationOutcome::Cancelled ||
                             result.outcome == RelationOutcome::ResourceExceeded;
    if (meter.stopped() || nested_halt) {
        const bool cancelled =
            result.outcome == RelationOutcome::Cancelled || meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        RelationResult halted;
        halted.outcome =
            cancelled ? RelationOutcome::Cancelled : RelationOutcome::ResourceExceeded;
        halted.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        halted.cost = meter.cost();
        record_context(derivation, model, budget, equation, halted.status);
        return halted;
    }

    if (result.outcome == RelationOutcome::Solved)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, model, budget, equation, result.status);
    return result;
}

}  // namespace nps
