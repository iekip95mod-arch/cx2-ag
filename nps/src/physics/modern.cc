#include "nps/physics/modern.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {
namespace {

const size_t kMaxVariables = 3;

struct RelationShape {
    const char *family_id;
    const char *strategy_id;
    const char *written;
    ModernVariable variables[kMaxVariables];
    size_t count;
    const char *assumption;
};

Dimension energy_dimension_local() {
    Dimension d;
    d.length = 2;
    d.mass = 1;
    d.time = -2;
    return d;
}

Dimension length_dimension_local() {
    Dimension d;
    d.length = 1;
    return d;
}

Dimension mass_dimension_local() {
    Dimension d;
    d.mass = 1;
    return d;
}

const RelationShape &shape_of(ModernRelation relation) {
    static const RelationShape photon = {
        "physics.modern.photon-wavelength", "physics.modern.planck-relation",
        "E * lambda = hc",
        {ModernVariable::PhotonEnergy, ModernVariable::Wavelength, ModernVariable::PhotonEnergy},
        2,
        "the quantum travels in free space, where hc is the tabulated 1239.8 eV nm"};
    static const RelationShape photoelectric = {
        "physics.modern.photoelectric", "physics.modern.einstein-photoelectric",
        "Kmax = E - phi",
        {ModernVariable::KineticEnergy, ModernVariable::PhotonEnergy, ModernVariable::WorkFunction},
        3,
        "one photon ejects one electron from a clean surface with no collision losses"};
    static const RelationShape mass_energy = {
        "physics.modern.mass-energy", "physics.modern.mass-energy-equivalence",
        "E = dm * c^2",
        {ModernVariable::RestEnergy, ModernVariable::MassDefect, ModernVariable::RestEnergy},
        2,
        "the mass defect is the whole energy release, with 931.49 MeV per atomic mass unit"};
    switch (relation) {
        case ModernRelation::PhotonWavelength: return photon;
        case ModernRelation::Photoelectric: return photoelectric;
        case ModernRelation::MassEnergy: return mass_energy;
    }
    return photon;
}

const char *variable_symbol(ModernVariable variable) {
    switch (variable) {
        case ModernVariable::PhotonEnergy: return "E";
        case ModernVariable::Wavelength: return "lambda";
        case ModernVariable::KineticEnergy: return "Kmax";
        case ModernVariable::WorkFunction: return "phi";
        case ModernVariable::MassDefect: return "dm";
        case ModernVariable::RestEnergy: return "Erest";
    }
    return "?";
}

Dimension variable_dimension(ModernVariable variable) {
    switch (variable) {
        case ModernVariable::Wavelength: return length_dimension_local();
        case ModernVariable::MassDefect: return mass_dimension_local();
        default: break;
    }
    return energy_dimension_local();
}

int index_in(const RelationShape &shape, ModernVariable variable) {
    for (size_t i = 0; i < shape.count; ++i) {
        if (shape.variables[i] == variable)
            return static_cast<int>(i);
    }
    return -1;
}

// The dimension the tabulated constant carries, built from the relation rather than from the
// variables it is compared against.
Dimension constant_dimension(ModernRelation relation) {
    Dimension d;
    switch (relation) {
        case ModernRelation::PhotonWavelength:
            d.length = 3;
            d.mass = 1;
            d.time = -2;
            return d;
        case ModernRelation::MassEnergy:
            d.length = 2;
            d.time = -2;
            return d;
        case ModernRelation::Photoelectric: return d;
    }
    return d;
}

// The tabulated product each relation carries, with the figure count it is quoted to.
Rational relation_constant(ModernRelation relation, unsigned *digits, const char **text) {
    Rational value;
    switch (relation) {
        case ModernRelation::PhotonWavelength:
            value.num = 12398;
            value.den = 10;
            *digits = 5;
            *text = "hc = 1239.8 eV nm";
            return value;
        case ModernRelation::MassEnergy:
            value.num = 93149;
            value.den = 100;
            *digits = 5;
            *text = "c^2 = 931.49 MeV/u";
            return value;
        case ModernRelation::Photoelectric:
            value.num = 1;
            value.den = 1;
            *digits = 0;
            *text = "no tabulated constant";
            return value;
    }
    *digits = 0;
    *text = "no tabulated constant";
    return value;
}

ModernResult failed(ModernOutcome outcome, DerivationStatus status, const std::string &detail) {
    ModernResult result;
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

std::string known_text(const ModernKnown &known) {
    return std::string(modern_variable_name(known.variable)) + " = " + value_text(known.quantity) +
           " " + modern_variable_unit(known.variable);
}

Unit family_unit(ModernVariable variable) {
    Unit unit;
    unit.dimension = variable_dimension(variable);
    unit.text = modern_variable_unit(variable);
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

Step transformation_step(const char *goal, const char *rule_id, const char *rule_name,
                         const char *explanation, const std::string &detailed) {
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

void record_context(Derivation &derivation, const Budget &budget, const RelationShape &shape,
                    NodeId model, DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = shape.family_id;
    inputs.requested_method =
        std::string(shape.written) + ", exact substitution in eV and nm, linear isolation";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.branch_convention = "real domain";
    inputs.active_assumptions.push_back(shape.assumption);
    // Said out loud because it is the reason this family has no SI conversion step at all.
    inputs.active_assumptions.push_back(
        "quantities stay in the declared eV, nm, u and MeV working units, since their SI values do "
        "not fit the exact integer rationals every step here checks with");
    inputs.unit_policy = "require each given in the unit its variable declares, check the relation "
                         "dimensions, and round only the reported answer";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

// The relation written with one side per shape, so the model and the substituted copy share it.
NodeId build_relation(Arena &arena, ModernRelation relation, const NodeId *expressions,
                      NodeId constant) {
    switch (relation) {
        case ModernRelation::PhotonWavelength:
            return arena.binary(Kind::Equals, arena.binary(Kind::Mul, expressions[0],
                                                           expressions[1]),
                                constant);
        case ModernRelation::Photoelectric:
            return arena.binary(Kind::Equals, expressions[0],
                                arena.binary(Kind::Add, expressions[1],
                                             arena.unary(Kind::Neg, expressions[2])));
        case ModernRelation::MassEnergy:
            return arena.binary(Kind::Equals, expressions[0],
                                arena.binary(Kind::Mul, constant, expressions[1]));
    }
    return kNoNode;
}

// The relation evaluated on exact values, which is what the final check compares.
bool relation_holds(ModernRelation relation, const Rational *values, const Rational &constant,
                    bool *computed) {
    Rational left;
    Rational right;
    *computed = true;
    switch (relation) {
        case ModernRelation::PhotonWavelength:
            if (!rational_mul(values[0], values[1], &left)) {
                *computed = false;
                return false;
            }
            right = constant;
            break;
        case ModernRelation::Photoelectric:
            left = values[0];
            if (!rational_sub(values[1], values[2], &right)) {
                *computed = false;
                return false;
            }
            break;
        case ModernRelation::MassEnergy:
            left = values[0];
            if (!rational_mul(constant, values[1], &right)) {
                *computed = false;
                return false;
            }
            break;
    }
    return rational_equal(left, right);
}

bool positive(const Rational &value) {
    Rational normalized;
    if (!normalize_copy(value, &normalized))
        return false;
    return normalized.num > 0;
}

ModernResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                        const ModernProblem &problem, const Budget &budget,
                        const RelationShape &shape, NodeId *model) {
    const int unknown_index = index_in(shape, problem.unknown);
    if (unknown_index < 0)
        return failed(ModernOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      std::string(modern_variable_name(problem.unknown)) + " is not a variable of " +
                          shape.written);

    const ModernKnown *knowns[kMaxVariables] = {nullptr, nullptr, nullptr};
    for (const ModernKnown &known : problem.knowns) {
        const int index = index_in(shape, known.variable);
        if (index < 0)
            return failed(ModernOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(modern_variable_name(known.variable)) +
                              " is not a variable of " + shape.written);
        if (index == unknown_index)
            return failed(ModernOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(modern_variable_name(known.variable)) +
                              " is both known and unknown");
        if (knowns[index])
            return failed(ModernOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                          std::string(modern_variable_name(known.variable)) + " is given twice");
        std::string invalid_detail;
        if (!valid_quantity(known.quantity, &invalid_detail))
            return failed(ModernOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(modern_variable_name(known.variable)) + ": " +
                              invalid_detail);
        const Dimension expected = variable_dimension(known.variable);
        if (known.quantity.unit.dimension != expected) {
            return failed(ModernOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " has dimension " +
                              dimension_text(known.quantity.unit.dimension) + ", but " +
                              modern_variable_name(known.variable) + " requires " +
                              dimension_text(expected));
        }
        // No conversion happens in this family, so a given written in another unit of the same
        // dimension would be substituted unchanged and silently wrong.
        if (known.quantity.unit.text != modern_variable_unit(known.variable)) {
            return failed(ModernOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " was given in " + known.quantity.unit.text +
                              ", but this family reads " + modern_variable_name(known.variable) +
                              " in " + modern_variable_unit(known.variable));
        }
        if (!positive(known.quantity.value)) {
            return failed(ModernOutcome::UnphysicalValue, DerivationStatus::InvalidInput,
                          known_text(known) + " is not positive");
        }
        knowns[index] = &known;
    }

    for (size_t index = 0; index < shape.count; ++index) {
        if (static_cast<int>(index) != unknown_index && !knowns[index]) {
            return failed(ModernOutcome::MissingKnown, DerivationStatus::InvalidInput,
                          std::string("missing known ") +
                              modern_variable_name(shape.variables[index]));
        }
    }

    unsigned constant_digits = 0;
    const char *constant_text = nullptr;
    const Rational constant = relation_constant(problem.relation, &constant_digits, &constant_text);
    NodeId constant_node = constant_digits == 0 ? kNoNode : rational_node(arena, constant);

    NodeId symbols[kMaxVariables] = {kNoNode, kNoNode, kNoNode};
    for (size_t index = 0; index < shape.count; ++index)
        symbols[index] = arena.symbol(variable_symbol(shape.variables[index]));
    NodeId equation = build_relation(arena, problem.relation, symbols, constant_node);
    *model = equation;
    if (arena.failed())
        return failed(ModernOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    PlanPayload plan;
    plan.strategy_id = shape.strategy_id;
    plan.selected_strategy = std::string("Apply ") + shape.written;
    for (const ModernKnown &known : problem.knowns)
        plan.matched_problem_facts.push_back(known_text(known));
    plan.matched_problem_facts.push_back(std::string("find ") +
                                         modern_variable_name(problem.unknown));
    plan.alternatives_considered.push_back("general symbolic backend isolation");
    plan.selection_rationale =
        std::string(shape.written) +
        " is linear in each possible unknown, so the local exact linear solver is sufficient and "
        "deterministic";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Find ") + modern_variable_name(problem.unknown);
    plan_step.rule_id = shape.strategy_id;
    plan_step.rule_name = modern_relation_name(problem.relation);
    plan_step.explanation_short =
        std::string("Use ") + shape.written + " and solve for the requested quantity";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.modern.compatible-dimensions",
                                   "both sides of the relation carry the same dimension",
                                   "dimensional analysis", EvidenceStrength::DimensionallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked against the written relation");
    register_strategy_precondition(
        plan, plan_step, "pre.modern.linear-unknown",
        "the relation is linear in the requested unknown", "registered relation model",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        std::string("the requested symbol occurs linearly in ") + shape.written);
    if (!meter.step())
        return ModernResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    // Both sides of the relation as dimensions, with the tabulated constant carrying the one
    // that closes it, so the comparison is between two independently built dimensions.
    Dimension left;
    Dimension right;
    bool dimension_fits = true;
    switch (problem.relation) {
        case ModernRelation::PhotonWavelength:
            dimension_fits = dimension_multiply(variable_dimension(ModernVariable::PhotonEnergy),
                                                variable_dimension(ModernVariable::Wavelength),
                                                &left);
            right = constant_dimension(problem.relation);
            break;
        case ModernRelation::MassEnergy:
            left = variable_dimension(ModernVariable::RestEnergy);
            dimension_fits = dimension_multiply(constant_dimension(problem.relation),
                                                variable_dimension(ModernVariable::MassDefect),
                                                &right);
            break;
        case ModernRelation::Photoelectric:
            left = variable_dimension(ModernVariable::KineticEnergy);
            right = variable_dimension(ModernVariable::PhotonEnergy);
            dimension_fits = right == variable_dimension(ModernVariable::WorkFunction);
            break;
    }
    const bool dimensions_match = dimension_fits && left == right;
    const std::string observed =
        !dimension_fits ? "the relation dimension does not fit"
                        : dimension_text(left) + " against " + dimension_text(right);
    if (!meter.step())
        return ModernResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the ") + modern_relation_name(problem.relation) +
                    " dimensions";
        step.rule_id = "physics.modern.check-dimensions";
        step.rule_name = "Dimensional analysis";
        step.explanation_short = "Every term added or equated must carry the same dimension";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.modern.dimensions-agree", "the two sides of the relation carry the same dimension"});
        step.verifications.push_back(
            verification("dimensional analysis", observed, EvidenceStrength::DimensionallyValid,
                         dimensions_match ? VerificationOutcome::Passed
                                          : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = std::string(shape.written) + " is dimensionally consistent";
        check.check_method = "compare the dimensions the relation equates";
        check.expected_relation = "equal dimensions";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.modern.compatible-dimensions",
        dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed, observed);
    if (!dimension_fits)
        return failed(ModernOutcome::ArithmeticOverflow, DerivationStatus::VerificationFailed,
                      "the relation dimension does not fit");
    if (!dimensions_match)
        return failed(ModernOutcome::DimensionMismatch, DerivationStatus::VerificationFailed,
                      std::string(shape.written) + " is dimensionally inconsistent");

    // The photoelectric threshold is a domain rule rather than an arithmetic one: below it the
    // surface emits nothing, and a negative Kmax would be a number with no physical reading.
    if (problem.relation == ModernRelation::Photoelectric &&
        problem.unknown == ModernVariable::KineticEnergy) {
        const int energy_index = index_in(shape, ModernVariable::PhotonEnergy);
        const int work_index = index_in(shape, ModernVariable::WorkFunction);
        Rational difference;
        if (!rational_sub(knowns[energy_index]->quantity.value,
                          knowns[work_index]->quantity.value, &difference)) {
            return failed(ModernOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                          "comparing the photon energy against the work function exceeds exact "
                          "integer arithmetic");
        }
        if (!positive(difference)) {
            return failed(ModernOutcome::UnphysicalValue, DerivationStatus::InvalidInput,
                          "the photon energy is at or below the work function, so no electron is "
                          "emitted and there is no maximum kinetic energy to report");
        }
    }

    Quantity quantities[kMaxVariables];
    NodeId expressions[kMaxVariables] = {symbols[0], symbols[1], symbols[2]};
    for (size_t index = 0; index < shape.count; ++index) {
        if (static_cast<int>(index) == unknown_index)
            continue;
        quantities[index] = knowns[index]->quantity;
        quantities[index].unit = family_unit(shape.variables[index]);
        expressions[index] = rational_node(arena, quantities[index].value);
    }
    NodeId substituted = build_relation(arena, problem.relation, expressions, constant_node);
    if (arena.failed())
        return failed(ModernOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    if (!meter.step())
        return ModernResult();
    {
        std::string detailed =
            std::string("Every quantity is already in the unit this family reads it in, so ") +
            shape.written +
            " takes the given values unchanged. Leave the requested quantity as a symbol, then "
            "solve the resulting equation for it.";
        if (constant_digits != 0)
            detailed += std::string(" The tabulated ") + constant_text + " closes the relation.";
        Step step = transformation_step("Substitute the known values", "physics.modern.substitute",
                                        "Substitution",
                                        "Replace each known symbol by its exact declared value",
                                        detailed);
        step.verifications.push_back(verification("typed known-quantity lookup",
                                                  "every known quantity was substituted",
                                                  EvidenceStrength::StructurallyValid,
                                                  VerificationOutcome::Passed));
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        TransformationPayload payload;
        payload.before = equation;
        payload.after = substituted;
        payload.concrete_action = std::string("Substitute the known quantities into ") +
                                  shape.written;
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    ModernResult result;
    result.unknown = expressions[unknown_index];
    result.equation = equation;
    result.substituted = substituted;
    const Budget linear_budget = remaining_budget(budget, meter);
    const SolveResult solved =
        solve_linear(arena, derivation, substituted, expressions[unknown_index], linear_budget);
    if (!charge(meter, solved.cost))
        return ModernResult();
    if (solved.outcome == SolveOutcome::Cancelled) {
        result.outcome = ModernOutcome::Cancelled;
        result.status = DerivationStatus::NotRecorded;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::ResourceExceeded) {
        result.outcome = ModernOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = solved.detail;
        return result;
    }
    if (solved.outcome == SolveOutcome::NoSolution || solved.outcome == SolveOutcome::AllValues) {
        result.outcome = ModernOutcome::InvalidProblem;
        result.status = DerivationStatus::InvalidInput;
        result.detail = "the given values do not determine one " +
                        std::string(modern_variable_name(problem.unknown)) + ": " + solved.detail;
        return result;
    }
    if (solved.outcome != SolveOutcome::Solved) {
        const bool overflow = solved.status == DerivationStatus::ResourceLimitReached;
        result.outcome =
            overflow ? ModernOutcome::ArithmeticOverflow : ModernOutcome::VerificationFailed;
        result.status = overflow ? DerivationStatus::ResourceLimitReached : solved.status;
        result.detail = solved.detail;
        return result;
    }

    Rational candidate;
    if (!rational_of_node(arena, solved.solution, &candidate)) {
        result.outcome = ModernOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = "the linear solver returned a value that is not an exact rational";
        return result;
    }
    if (!positive(candidate)) {
        result.outcome = ModernOutcome::UnphysicalValue;
        result.status = DerivationStatus::InvalidInput;
        result.detail = std::string(modern_variable_name(problem.unknown)) + " came out as " +
                        rational_text(candidate) + ", which is not a positive " +
                        modern_variable_unit(problem.unknown) + " value";
        return result;
    }
    quantities[unknown_index].value = candidate;
    quantities[unknown_index].unit = family_unit(problem.unknown);
    Precision answer_precision;
    if (constant_digits != 0) {
        Precision tabulated;
        tabulated.kind = NumberKind::Measured;
        tabulated.significant_digits = static_cast<uint16_t>(constant_digits);
        answer_precision = precision_combine(answer_precision, tabulated);
    }
    for (size_t index = 0; index < shape.count; ++index) {
        if (static_cast<int>(index) != unknown_index)
            answer_precision = precision_combine(answer_precision, quantities[index].precision);
    }
    quantities[unknown_index].precision = answer_precision;

    Rational values[kMaxVariables];
    for (size_t index = 0; index < shape.count; ++index)
        values[index] = quantities[index].value;
    bool check_computed = false;
    const bool candidate_passes =
        relation_holds(problem.relation, values, constant, &check_computed);
    if (!meter.step())
        return ModernResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the candidate in ") + shape.written;
        step.rule_id = "physics.modern.check-candidate";
        step.rule_name = "Substitution check";
        step.explanation_short = std::string("Put the candidate back into ") + shape.written;
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.modern.candidate-satisfies", "the candidate satisfies the original relation"});
        const VerificationOutcome verification_outcome =
            !check_computed ? VerificationOutcome::Inconclusive
                            : candidate_passes ? VerificationOutcome::Passed
                                               : VerificationOutcome::Failed;
        const std::string check_observed =
            !check_computed ? "the exact comparison exceeds integer arithmetic"
                            : candidate_passes ? "both sides are exactly equal"
                                               : "the two sides are not equal";
        step.verifications.push_back(
            verification("exact substitution into the original relation", check_observed,
                         EvidenceStrength::CandidateChecked, verification_outcome));
        CheckPayload check;
        check.target_claim = std::string(variable_symbol(problem.unknown)) + " = " +
                             rational_text(candidate) + " satisfies " + shape.written;
        check.check_method = "substitute every exact declared value";
        check.expected_relation = "the two sides of the relation are equal";
        check.observed_result = check_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!check_computed) {
        result.outcome = ModernOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "checking the candidate exceeds exact integer arithmetic";
        return result;
    }
    if (!candidate_passes) {
        result.outcome = ModernOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = std::string("the candidate failed substitution into ") + shape.written;
        return result;
    }

    result.outcome = ModernOutcome::Solved;
    result.value = solved.solution;
    result.quantity = quantities[unknown_index];
    result.quantity.precision = precision_at_digits(candidate, result.quantity.precision);
    result.value_text = rational_text(candidate);
    result.unit_text = result.quantity.unit.text;
    if (result.quantity.precision.kind == NumberKind::Measured) {
        std::string reported;
        const unsigned digits = result.quantity.precision.significant_digits;
        if (!rounded_text(candidate, digits, &reported)) {
            result.outcome = ModernOutcome::ArithmeticOverflow;
            result.status = DerivationStatus::ResourceLimitReached;
            result.detail = "reporting the measured precision exceeds exact integer arithmetic";
            result.value = kNoNode;
            result.value_text.clear();
            result.unit_text.clear();
            return result;
        }
        if (reported != result.value_text) {
            const HalfPlace checked =
                precision_rounding_valid(candidate, reported, result.quantity.precision);
            if (checked == HalfPlace::Outside) {
                result.outcome = ModernOutcome::VerificationFailed;
                result.status = DerivationStatus::VerificationFailed;
                result.detail = reported + " is further than half a unit in its last place from " +
                                result.value_text;
                result.value = kNoNode;
                result.value_text.clear();
                result.unit_text.clear();
                return result;
            }
            if (!meter.step())
                return ModernResult();
            Step step;
            step.phase = "report";
            step.goal = "Report the answer to the measured precision";
            step.rule_id = "physics.modern.significant-figures";
            step.rule_name = "Significant figures";
            step.explanation_short =
                "Use the fewest significant figures among the measured givens and the tabulated "
                "constant";
            step.explanation_detailed =
                "The tabulated constant is a measurement too, so it enters the figure count "
                "alongside the givens. Round once, at the end, since rounding partway through "
                "throws away figures the final rounding cannot get back.";
            step.claim = ClaimType::NoClaim;
            const VerificationOutcome report_outcome = checked == HalfPlace::Unreadable
                                                           ? VerificationOutcome::Inconclusive
                                                           : VerificationOutcome::Passed;
            const std::string report_observed =
                checked == HalfPlace::Unreadable
                    ? reported + " could not be read back as a decimal, so it was never compared "
                                 "against " +
                          result.value_text
                    : reported + " is within half a unit in the last place of " + result.value_text;
            step.verifications.push_back(verification("exact comparison against the unrounded value",
                                                      report_observed,
                                                      EvidenceStrength::CandidateChecked,
                                                      report_outcome));
            step.proof_obligations.push_back(
                {"obl.physics.reported-within-half-place",
                 "the reported value is within half a unit in the last place of the exact one"});
            if (checked == HalfPlace::Unreadable) {
                // Never compared, so the exact value stands and only the rounded spelling is
                // withheld. Showing a rounding nothing checked would be evidence-free.
                result.status = DerivationStatus::SolvedButUnchecked;
                result.detail = report_observed;
                CheckPayload check;
                check.target_claim =
                    "the reported value is within half a unit in the last place of the exact one";
                check.check_method =
                    "read the rounded text back and compare it against the exact value";
                check.expected_relation = "the difference is at most half a unit in the last place";
                check.observed_result = report_observed;
                derivation.add_check(kNoStep, std::move(step), std::move(check));
                return result;
            }
            TransformationPayload payload;
            payload.before = solved.solution;
            payload.after = reported.find('.') == std::string::npos ? arena.integer(reported)
                                                                    : arena.decimal(reported);
            payload.concrete_action = "Report " + result.value_text + " as " + reported;
            payload.reversible = false;
            if (arena.failed())
                return failed(ModernOutcome::ResourceExceeded,
                              DerivationStatus::ResourceLimitReached, status_name(arena.status()));
            derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
            result.value_text = reported;
        }
    }
    return result;
}

}  // namespace

const char *modern_relation_name(ModernRelation relation) {
    switch (relation) {
        case ModernRelation::PhotonWavelength: return "Planck photon relation";
        case ModernRelation::Photoelectric: return "Einstein photoelectric equation";
        case ModernRelation::MassEnergy: return "Mass-energy equivalence";
    }
    return "invalid relation";
}

const char *modern_variable_name(ModernVariable variable) {
    switch (variable) {
        case ModernVariable::PhotonEnergy: return "photon energy";
        case ModernVariable::Wavelength: return "wavelength";
        case ModernVariable::KineticEnergy: return "maximum kinetic energy";
        case ModernVariable::WorkFunction: return "work function";
        case ModernVariable::MassDefect: return "mass defect";
        case ModernVariable::RestEnergy: return "rest energy";
    }
    return "invalid variable";
}

const char *modern_variable_unit(ModernVariable variable) {
    switch (variable) {
        case ModernVariable::Wavelength: return "nm";
        case ModernVariable::MassDefect: return "u";
        case ModernVariable::RestEnergy: return "MeV";
        default: break;
    }
    return "eV";
}

bool modern_relation_has(ModernRelation relation, ModernVariable variable) {
    return index_in(shape_of(relation), variable) >= 0;
}

const char *modern_outcome_name(ModernOutcome outcome) {
    switch (outcome) {
        case ModernOutcome::Solved: return "solved";
        case ModernOutcome::InvalidProblem: return "invalid problem";
        case ModernOutcome::MissingKnown: return "missing known";
        case ModernOutcome::DuplicateKnown: return "duplicate known";
        case ModernOutcome::DimensionMismatch: return "dimension mismatch";
        case ModernOutcome::UnphysicalValue: return "unphysical value";
        case ModernOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case ModernOutcome::VerificationFailed: return "verification failed";
        case ModernOutcome::Cancelled: return "cancelled";
        case ModernOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

ModernResult solve_modern(Arena &arena, Derivation &derivation, const ModernProblem &problem,
                          const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    const RelationShape &shape = shape_of(problem.relation);
    NodeId model = kNoNode;
    ModernResult result = solve_body(arena, derivation, meter, problem, budget, shape, &model);

    const bool nested_halt = result.outcome == ModernOutcome::Cancelled ||
                             result.outcome == ModernOutcome::ResourceExceeded;
    if (meter.stopped() || nested_halt) {
        const bool cancelled =
            result.outcome == ModernOutcome::Cancelled || meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        ModernResult halted;
        halted.outcome = cancelled ? ModernOutcome::Cancelled : ModernOutcome::ResourceExceeded;
        halted.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        halted.cost = meter.cost();
        record_context(derivation, budget, shape, model, halted.status);
        return halted;
    }

    if (result.outcome == ModernOutcome::Solved)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, shape, model, result.status);
    return result;
}

}  // namespace nps
