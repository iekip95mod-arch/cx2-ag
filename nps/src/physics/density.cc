#include "nps/physics/density.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {
namespace {

const size_t kVariableCount = 3;

int variable_index(DensityVariable variable) {
    switch (variable) {
        case DensityVariable::Mass: return 0;
        case DensityVariable::Volume: return 1;
        case DensityVariable::Density: return 2;
    }
    return -1;
}

const char *variable_symbol(DensityVariable variable) {
    switch (variable) {
        case DensityVariable::Mass: return "m";
        case DensityVariable::Volume: return "V";
        case DensityVariable::Density: return "rho";
    }
    return "?";
}

Dimension variable_dimension(DensityVariable variable) {
    Dimension dimension;
    switch (variable) {
        case DensityVariable::Mass: dimension.mass = 1; break;
        case DensityVariable::Volume: dimension.length = 3; break;
        case DensityVariable::Density:
            dimension.length = -3;
            dimension.mass = 1;
            break;
    }
    return dimension;
}

DensityResult failed(DensityOutcome outcome, DerivationStatus status, const std::string &detail) {
    DensityResult result;
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

std::string known_text(const DensityKnown &known) {
    std::string text = std::string(density_variable_name(known.variable)) + " = " +
                       value_text(known.quantity);
    if (!known.quantity.unit.text.empty())
        text += " " + known.quantity.unit.text;
    return text;
}

Unit si_unit(DensityVariable variable) {
    Unit unit;
    unit.dimension = variable_dimension(variable);
    unit.text = si_unit_text(unit.dimension);
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

// detailed has no default on purpose. STEP-021 wants every transformation to say how to recognise
// its rule again, and a builder that lets the field be left out is why it was empty here.
Step transformation_step(const char *goal, const char *rule_id, const char *rule_name,
                         const char *explanation, const char *detailed) {
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

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.density.mass-volume";
    inputs.requested_method = "density definition, exact SI substitution, linear isolation";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.branch_convention = "real domain";
    // The one modelling premise this family has, which the catalog claimed and nothing recorded.
    inputs.active_assumptions.push_back("density is uniform across the sample");
    inputs.unit_policy = "validate dimensions before substitution, convert exactly to SI, and round "
                         "only the reported answer";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

DensityResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                         const DensityProblem &problem, const Budget &budget, NodeId *model) {
    const int unknown_index = variable_index(problem.unknown);
    if (unknown_index < 0)
        return failed(DensityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the requested density variable is invalid");

    const DensityKnown *knowns[kVariableCount] = {nullptr, nullptr, nullptr};
    for (const DensityKnown &known : problem.knowns) {
        const int index = variable_index(known.variable);
        if (index < 0)
            return failed(DensityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "a known density variable is invalid");
        if (index == unknown_index)
            return failed(DensityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(density_variable_name(known.variable)) +
                              " is both known and unknown");
        if (knowns[index])
            return failed(DensityOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                          std::string(density_variable_name(known.variable)) + " is given twice");
        std::string invalid_detail;
        if (!valid_quantity(known.quantity, &invalid_detail))
            return failed(DensityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(density_variable_name(known.variable)) + ": " +
                              invalid_detail);
        const Dimension expected = variable_dimension(known.variable);
        if (known.quantity.unit.dimension != expected) {
            return failed(DensityOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " has dimension " +
                              dimension_text(known.quantity.unit.dimension) + ", but " +
                              density_variable_name(known.variable) + " requires " +
                              dimension_text(expected));
        }
        knowns[index] = &known;
    }

    for (size_t index = 0; index < kVariableCount; ++index) {
        if (static_cast<int>(index) != unknown_index && !knowns[index]) {
            const DensityVariable missing = static_cast<DensityVariable>(index);
            return failed(DensityOutcome::MissingKnown, DerivationStatus::InvalidInput,
                          std::string("missing known ") + density_variable_name(missing));
        }
    }

    NodeId mass_symbol = arena.symbol(variable_symbol(DensityVariable::Mass));
    NodeId volume_symbol = arena.symbol(variable_symbol(DensityVariable::Volume));
    NodeId density_symbol = arena.symbol(variable_symbol(DensityVariable::Density));
    NodeId equation = arena.binary(Kind::Equals, mass_symbol,
                                   arena.binary(Kind::Mul, density_symbol, volume_symbol));
    *model = equation;
    if (arena.failed())
        return failed(DensityOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    PlanPayload plan;
    plan.strategy_id = "physics.density.definition";
    plan.selected_strategy = "Apply the density definition m = rho*V";
    for (const DensityKnown &known : problem.knowns)
        plan.matched_problem_facts.push_back(known_text(known));
    plan.matched_problem_facts.push_back(
        std::string("find ") + density_variable_name(problem.unknown));
    plan.alternatives_considered.push_back("general symbolic backend isolation");
    plan.selection_rationale =
        "the density definition is linear in each possible unknown, so the local exact linear "
        "solver is sufficient and deterministic";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Find ") + density_variable_name(problem.unknown);
    plan_step.rule_id = "physics.density.definition";
    plan_step.rule_name = "Density definition";
    plan_step.explanation_short = "Use m = rho*V and solve for the requested quantity";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(
        plan, plan_step, "pre.density.compatible-dimensions",
        "mass, volume and density use compatible dimensions", "dimensional analysis",
        EvidenceStrength::DimensionallyValid, VerificationOutcome::NotAttempted,
        "checked against the density definition");
    register_strategy_precondition(
        plan, plan_step, "pre.density.linear-unknown",
        "the density definition is linear in the requested unknown",
        "registered density-variable model", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed,
        "the requested mass, volume or density symbol occurs linearly in m = rho*V");
    if (!meter.step())
        return DensityResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const Dimension mass_dimension = variable_dimension(DensityVariable::Mass);
    Dimension product_dimension;
    const bool dimension_product_fits =
        dimension_multiply(variable_dimension(DensityVariable::Density),
                           variable_dimension(DensityVariable::Volume), &product_dimension);
    const bool dimensions_match = dimension_product_fits && mass_dimension == product_dimension;
    if (!meter.step())
        return DensityResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check the density definition dimensions";
        step.rule_id = "physics.density.check-dimensions";
        step.rule_name = "Dimensional analysis";
        step.explanation_short = "Mass and density times volume must have the same dimension";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.density.dimensions-agree", "dimension of mass equals dimension of density times volume"});
        const std::string observed =
            dimension_product_fits
                ? dimension_text(mass_dimension) + " against " + dimension_text(product_dimension)
                : "density times volume dimension does not fit";
        step.verifications.push_back(verification(
            "dimensional analysis", observed, EvidenceStrength::DimensionallyValid,
            dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "m = rho*V is dimensionally consistent";
        check.check_method = "multiply the density and volume dimensions";
        check.expected_relation = "equal dimensions";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.density.compatible-dimensions",
        dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimension_product_fits ? dimension_text(mass_dimension) + " against " +
                                     dimension_text(product_dimension)
                               : "density times volume dimension does not fit");
    if (!dimension_product_fits)
        return failed(DensityOutcome::ArithmeticOverflow, DerivationStatus::VerificationFailed,
                      "the density definition dimension does not fit");
    if (!dimensions_match)
        return failed(DensityOutcome::DimensionMismatch, DerivationStatus::VerificationFailed,
                      "the density definition is dimensionally inconsistent");

    Quantity si_quantities[kVariableCount];
    std::string conversions;
    bool converted_units = false;
    for (size_t index = 0; index < kVariableCount; ++index) {
        if (static_cast<int>(index) == unknown_index)
            continue;
        const DensityKnown &known = *knowns[index];
        Rational value;
        if (!to_si(known.quantity, &value)) {
            return failed(DensityOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "converting " + known_text(known) +
                              " to SI exceeds exact integer arithmetic");
        }
        Quantity converted;
        converted.value = value;
        converted.unit = si_unit(known.variable);
        converted.precision = known.quantity.precision;
        si_quantities[index] = converted;
        Rational scale;
        normalize_copy(known.quantity.unit.scale, &scale);
        if (scale.num != scale.den) {
            converted_units = true;
            if (!conversions.empty())
                conversions += ", ";
            DensityKnown converted_known;
            converted_known.variable = known.variable;
            converted_known.quantity = converted;
            conversions += known_text(known) + " becomes " + known_text(converted_known);
        }
    }

    // Reads the stored array rather than the loop's local, so a miscopy is caught. A wrong table
    // scale is not, and cannot be from here: units_tests pins the scales against literals.
    bool conversions_agree = true;
    size_t conversions_checked = 0;
    std::string conversion_observed;
    for (size_t index = 0; index < kVariableCount; ++index) {
        if (static_cast<int>(index) == unknown_index)
            continue;
        const DensityKnown &known = *knowns[index];
        // to_si is this one multiplication and it already succeeded above, so there is no
        // arithmetic outcome left to fail here and no Inconclusive arm to record.
        Rational expected;
        static_cast<void>(rational_mul(known.quantity.value, known.quantity.unit.scale, &expected));
        ++conversions_checked;
        if (rational_equal(si_quantities[index].value, expected))
            continue;
        conversions_agree = false;
        if (!conversion_observed.empty())
            conversion_observed += ", ";
        conversion_observed += std::string(variable_symbol(known.variable)) + " was stored as " +
                               rational_text(si_quantities[index].value) + " rather than " +
                               rational_text(expected);
    }
    const VerificationOutcome conversion_outcome =
        conversions_agree ? VerificationOutcome::Passed : VerificationOutcome::Failed;
    if (conversion_observed.empty()) {
        conversion_observed = std::to_string(conversions_checked) +
                              " stored values equal the given times its table scale";
    }

    if (converted_units) {
        if (!meter.step())
            return DensityResult();
        Step step = transformation_step("Convert the known quantities to SI",
                                        "physics.density.convert-units", "Unit conversion",
                                        "Apply each unit's exact scale to SI",
                                        "Use kg for mass, m^3 for volume and kg/m^3 for density "
                                        "so m = rho*V uses consistent units. Convert each known "
                                        "quantity with its exact unit scale before substitution.");
        step.verifications.push_back(
            verification("recompute each stored value as the given times its table scale",
                         conversion_observed, EvidenceStrength::CandidateChecked,
                         conversion_outcome));
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
        return failed(DensityOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      "a converted quantity does not equal the given times its table scale");

    NodeId expressions[kVariableCount] = {mass_symbol, volume_symbol, density_symbol};
    for (size_t index = 0; index < kVariableCount; ++index) {
        if (static_cast<int>(index) != unknown_index)
            expressions[index] = rational_node(arena, si_quantities[index].value);
    }
    NodeId substituted = arena.binary(
        Kind::Equals, expressions[variable_index(DensityVariable::Mass)],
        arena.binary(Kind::Mul, expressions[variable_index(DensityVariable::Density)],
                     expressions[variable_index(DensityVariable::Volume)]));
    if (arena.failed())
        return failed(DensityOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    if (!meter.step())
        return DensityResult();
    {
        Step step = transformation_step("Substitute the known SI values",
                                        "physics.density.substitute", "Substitution",
                                        "Replace each known symbol by its exact SI value",
                                        "After converting the known quantities to SI, put their "
                                        "values into m = rho*V. Leave the requested quantity as a "
                                        "symbol, then solve the resulting equation for it.");
        step.verifications.push_back(verification(
            "typed known-quantity lookup", "both known quantities were substituted",
            EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        TransformationPayload payload;
        payload.before = equation;
        payload.after = substituted;
        payload.concrete_action = "Substitute the two known quantities after exact SI conversion";
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    DensityResult result;
    result.unknown = expressions[unknown_index];
    result.equation = equation;
    result.substituted = substituted;
    const Budget linear_budget = remaining_budget(budget, meter);
    const SolveResult solved =
        solve_linear(arena, derivation, substituted, expressions[unknown_index], linear_budget);
    if (!charge(meter, solved.cost))
        return DensityResult();
    if (solved.outcome == SolveOutcome::Cancelled) {
        result.outcome = DensityOutcome::Cancelled;
        result.status = DerivationStatus::NotRecorded;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::ResourceExceeded) {
        result.outcome = DensityOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::NoSolution || solved.outcome == SolveOutcome::AllValues) {
        result.outcome = DensityOutcome::InvalidProblem;
        result.status = DerivationStatus::InvalidInput;
        result.detail = "the given values do not determine one " +
                        std::string(density_variable_name(problem.unknown)) + ": " + solved.detail;
        return result;
    }
    if (solved.outcome != SolveOutcome::Solved) {
        const bool overflow = solved.status == DerivationStatus::ResourceLimitReached;
        result.outcome =
            overflow ? DensityOutcome::ArithmeticOverflow : DensityOutcome::VerificationFailed;
        result.status = overflow ? DerivationStatus::ResourceLimitReached : solved.status;
        result.detail = solved.detail;
        return result;
    }

    Rational candidate;
    if (!rational_of_node(arena, solved.solution, &candidate)) {
        result.outcome = DensityOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the linear solver returned a value that is not an exact rational";
        return result;
    }
    si_quantities[unknown_index].value = candidate;
    si_quantities[unknown_index].unit = si_unit(problem.unknown);
    Precision answer_precision;
    for (size_t index = 0; index < kVariableCount; ++index) {
        if (static_cast<int>(index) != unknown_index)
            answer_precision = precision_combine(answer_precision, si_quantities[index].precision);
    }
    si_quantities[unknown_index].precision = answer_precision;

    Rational density_times_volume;
    const bool check_computed =
        rational_mul(si_quantities[variable_index(DensityVariable::Density)].value,
                     si_quantities[variable_index(DensityVariable::Volume)].value,
                     &density_times_volume);
    const bool candidate_passes =
        check_computed && rational_equal(si_quantities[variable_index(DensityVariable::Mass)].value,
                                         density_times_volume);
    if (!meter.step())
        return DensityResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check the candidate in the density definition";
        step.rule_id = "physics.density.check-candidate";
        step.rule_name = "Substitution check";
        step.explanation_short = "Put the candidate back into m = rho*V";
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.density.candidate-satisfies", "the candidate satisfies the original density definition"});
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
        check.target_claim = std::string(variable_symbol(problem.unknown)) + " = " +
                             rational_text(candidate) + " satisfies m = rho*V";
        check.check_method = "substitute all three exact SI values";
        check.expected_relation = "mass equals density times volume";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!check_computed) {
        result.outcome = DensityOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "checking the candidate exceeds exact integer arithmetic";
        return result;
    }
    if (!candidate_passes) {
        result.outcome = DensityOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the candidate failed substitution into m = rho*V";
        return result;
    }

    result.outcome = DensityOutcome::Solved;
    result.value = solved.solution;
    result.quantity = si_quantities[unknown_index];
    // The combined figure count is the given here, so the place follows the solved value rather
    // than the operands the combine took it from.
    result.quantity.precision = precision_at_digits(candidate, result.quantity.precision);
    result.value_text = rational_text(candidate);
    result.unit_text = result.quantity.unit.text;
    if (result.quantity.precision.kind == NumberKind::Measured) {
        std::string reported;
        const unsigned digits = result.quantity.precision.significant_digits;
        if (!rounded_text(candidate, digits, &reported)) {
            result.outcome = DensityOutcome::ArithmeticOverflow;
            result.status = DerivationStatus::ResourceLimitReached;
            result.detail = "reporting the measured precision exceeds exact integer arithmetic";
            result.value = kNoNode;
            result.value_text.clear();
            result.unit_text.clear();
            return result;
        }
        if (reported != result.value_text) {
            // Checked against the string this step records. The predicate parses the reported text
            // back and measures the error, so it shares no path with rounded_text. A wrong
            // predicate it cannot catch, and units_tests pins that with negative cases.
            const HalfPlace checked =
                precision_rounding_valid(candidate, reported, result.quantity.precision);
            if (checked == HalfPlace::Outside) {
                result.outcome = DensityOutcome::VerificationFailed;
                result.status = DerivationStatus::VerificationFailed;
                result.detail = reported + " is further than half a unit in its last place from " +
                                result.value_text;
                result.value = kNoNode;
                result.value_text.clear();
                result.unit_text.clear();
                return result;
            }
            if (checked == HalfPlace::Unreadable) {
                // Never compared, so there is no verdict to refuse on. The exact value above is the
                // answer and it stands; what is withheld is the rounded spelling.
                if (!meter.step())
                    return DensityResult();
                result.status = DerivationStatus::SolvedButUnchecked;
                result.detail = reported +
                                " could not be read back as a decimal, so it was never compared "
                                "against " +
                                result.value_text;
                Step s;
                s.phase = "report";
                s.goal = "Report the answer to the measured precision";
                s.rule_id = "physics.density.significant-figures";
                s.rule_name = "Significant figures";
                s.claim = ClaimType::NoClaim;
                s.explanation_short =
                    "Use the fewest significant figures among the measured givens";
                s.explanation_detailed =
                    "The rounded spelling could not be read back as a decimal, so it was never "
                    "compared against the exact value. The exact value is reported instead, since "
                    "showing a rounding nothing checked would be showing an answer with no "
                    "evidence behind it.";
                s.proof_obligations.push_back(
                    {"obl.physics.reported-within-half-place",
                     "the reported value is within half a unit in the last place of the exact one"});
                s.verifications.push_back(verification(
                    "exact comparison against the unrounded value", result.detail,
                    EvidenceStrength::CandidateChecked, VerificationOutcome::Inconclusive));
                CheckPayload check;
                check.target_claim =
                    "the reported value is within half a unit in the last place of the exact one";
                check.check_method =
                    "read the rounded text back and compare it against the exact value";
                check.expected_relation = "the difference is at most half a unit in the last place";
                check.observed_result = result.detail;
                derivation.add_check(kNoStep, std::move(s), std::move(check));
                return result;
            }
            if (!meter.step())
                return DensityResult();
            Step step;
            step.phase = "report";
            step.goal = "Report the answer to the measured precision";
            step.rule_id = "physics.density.significant-figures";
            step.rule_name = "Significant figures";
            step.explanation_short = "Use the fewest significant figures among the measured givens";
            step.explanation_detailed =
                "Reach for this once, at the very end, and never partway through. A measured value "
                "is only as good as the figures it was written with, so the answer is reported to "
                "the fewest significant figures among the measurements it came from. Rounding "
                "before dividing would throw away figures the final rounding cannot get back, "
                "which is why every step above this one keeps the exact value.";
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
                return failed(DensityOutcome::ResourceExceeded,
                              DerivationStatus::ResourceLimitReached,
                              status_name(arena.status()));
            derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
            result.value_text = reported;
        }
    }
    return result;
}

}  // namespace

const char *density_variable_name(DensityVariable variable) {
    switch (variable) {
        case DensityVariable::Mass: return "mass";
        case DensityVariable::Volume: return "volume";
        case DensityVariable::Density: return "density";
    }
    return "invalid variable";
}

const char *density_outcome_name(DensityOutcome outcome) {
    switch (outcome) {
        case DensityOutcome::Solved: return "solved";
        case DensityOutcome::InvalidProblem: return "invalid problem";
        case DensityOutcome::MissingKnown: return "missing known";
        case DensityOutcome::DuplicateKnown: return "duplicate known";
        case DensityOutcome::DimensionMismatch: return "dimension mismatch";
        case DensityOutcome::UnphysicalValue: return "unphysical value";
        case DensityOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case DensityOutcome::VerificationFailed: return "verification failed";
        case DensityOutcome::Cancelled: return "cancelled";
        case DensityOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

DensityResult solve_density(Arena &arena, Derivation &derivation, const DensityProblem &problem,
                            const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    DensityResult result = solve_body(arena, derivation, meter, problem, budget, &model);

    const bool nested_halt = result.outcome == DensityOutcome::Cancelled ||
                             result.outcome == DensityOutcome::ResourceExceeded;
    // STEP-025: keep the run of records that were checked. A precondition still waiting for its
    // check point leaves the plan unverified, so a halt before that point keeps nothing, which is
    // what actually happened.
    if (meter.stopped() || nested_halt) {
        const bool cancelled = result.outcome == DensityOutcome::Cancelled ||
                               meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        DensityResult halted;
        halted.outcome = cancelled ? DensityOutcome::Cancelled : DensityOutcome::ResourceExceeded;
        halted.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        const Cost local_cost = meter.cost();
        halted.cost = local_cost;
        record_context(derivation, budget, model, halted.status);
        return halted;
    }

    if (result.outcome == DensityOutcome::Solved) {
        result.status = derivation.outcome_from(mark);
    }
    const Cost local_cost = meter.cost();
    result.cost = local_cost;
    record_context(derivation, budget, model, result.status);
    return result;
}

}  // namespace nps
