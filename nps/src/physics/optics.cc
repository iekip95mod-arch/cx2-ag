#include "nps/physics/optics.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "measurement_support.h"

namespace nps {
namespace {

const size_t kMaxMembers = 4;

struct RelationSchema {
    const char *family_id;
    const char *law_name;
    const char *equation_text;
    const char *convention;
    OpticsVariable members[kMaxMembers];
    size_t member_count;
    bool reciprocal;
};

const RelationSchema &relation_schema(OpticsRelation relation) {
    static const RelationSchema kRefraction = {
        "physics.optics.refraction.snell", "Snell's law", "n1*sin(t1) = n2*sin(t2)",
        "angles are measured from the normal, each sine is a non-negative ratio, and the reflected "
        "ray leaves at the incident angle",
        {OpticsVariable::IndexIncident, OpticsVariable::SineIncident,
         OpticsVariable::IndexTransmitted, OpticsVariable::SineTransmitted},
        4, false};
    static const RelationSchema kThinLens = {
        "physics.optics.thin-lens.image", "Thin lens equation", "1/do + 1/di = 1/f",
        "distances are positive on the real side, so a real object has do > 0, a real image has "
        "di > 0, a converging lens has f > 0, and a virtual image gives di < 0",
        {OpticsVariable::FocalLength, OpticsVariable::ObjectDistance,
         OpticsVariable::ImageDistance},
        3, true};
    static const RelationSchema kMirror = {
        "physics.optics.spherical-mirror.image", "Mirror equation", "1/do + 1/di = 1/f",
        "distances are positive in front of the mirror, so a real object has do > 0, a real image "
        "has di > 0, a concave mirror has f = R/2 > 0, and a virtual image gives di < 0",
        {OpticsVariable::FocalLength, OpticsVariable::ObjectDistance,
         OpticsVariable::ImageDistance},
        3, true};
    static const RelationSchema kDoubleSlit = {
        "physics.optics.double-slit.maxima", "Two-slit interference maxima",
        "d*sin(t) = m*lambda",
        "the order m counts bright fringes outward from the central maximum, so m is 0, 1, 2 and up",
        {OpticsVariable::SlitSpacing, OpticsVariable::SineFringe, OpticsVariable::FringeOrder,
         OpticsVariable::Wavelength},
        4, false};
    static const RelationSchema kSingleSlit = {
        "physics.optics.single-slit.minima", "Single-slit diffraction minima",
        "a*sin(t) = m*lambda",
        "the order m counts diffraction minima outward, so m is 1, 2, 3 and up, and m = 0 is the "
        "central maximum rather than a minimum",
        {OpticsVariable::SlitSpacing, OpticsVariable::SineFringe, OpticsVariable::FringeOrder,
         OpticsVariable::Wavelength},
        4, false};
    switch (relation) {
        case OpticsRelation::Refraction: return kRefraction;
        case OpticsRelation::ThinLens: return kThinLens;
        case OpticsRelation::SphericalMirror: return kMirror;
        case OpticsRelation::DoubleSlit: return kDoubleSlit;
        case OpticsRelation::SingleSlit: return kSingleSlit;
    }
    return kRefraction;
}

const char *variable_symbol(OpticsRelation relation, OpticsVariable variable) {
    switch (variable) {
        case OpticsVariable::IndexIncident: return "n1";
        case OpticsVariable::SineIncident: return "sin(t1)";
        case OpticsVariable::IndexTransmitted: return "n2";
        case OpticsVariable::SineTransmitted: return "sin(t2)";
        case OpticsVariable::FocalLength: return "f";
        case OpticsVariable::ObjectDistance: return "do";
        case OpticsVariable::ImageDistance: return "di";
        case OpticsVariable::SlitSpacing:
            return relation == OpticsRelation::SingleSlit ? "a" : "d";
        case OpticsVariable::SineFringe: return "sin(t)";
        case OpticsVariable::FringeOrder: return "m";
        case OpticsVariable::Wavelength: return "lambda";
    }
    return "?";
}

bool variable_is_length(OpticsVariable variable) {
    switch (variable) {
        case OpticsVariable::FocalLength:
        case OpticsVariable::ObjectDistance:
        case OpticsVariable::ImageDistance:
        case OpticsVariable::SlitSpacing:
        case OpticsVariable::Wavelength:
            return true;
        default:
            return false;
    }
}

Dimension variable_dimension(OpticsVariable variable) {
    Dimension dimension;
    if (variable_is_length(variable))
        dimension.length = 1;
    return dimension;
}

int member_index(const RelationSchema &schema, OpticsVariable variable) {
    for (size_t index = 0; index < schema.member_count; ++index) {
        if (schema.members[index] == variable)
            return static_cast<int>(index);
    }
    return -1;
}

OpticsResult failed(OpticsOutcome outcome, DerivationStatus status, const std::string &detail) {
    OpticsResult result;
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
        if (precision_rounded_text(normalized, quantity.precision, &measured))
            text = measured;
    }
    return text;
}

// A ratio of two givens reported beside the answer, to the fewest figures among the two.
std::string ratio_text(const Rational &ratio, const Precision &a, const Precision &b) {
    Quantity reported;
    reported.value = ratio;
    reported.precision = precision_at_digits(ratio, precision_combine(a, b));
    return value_text(reported);
}

std::string known_text(const OpticsKnown &known) {
    std::string text = std::string(optics_variable_name(known.variable)) + " = " +
                       value_text(known.quantity);
    if (!known.quantity.unit.text.empty())
        text += " " + known.quantity.unit.text;
    return text;
}

Unit si_unit(OpticsVariable variable) {
    Unit unit;
    unit.dimension = variable_dimension(variable);
    unit.text = si_unit_text(unit.dimension);
    unit.scale.num = 1;
    unit.scale.den = 1;
    return unit;
}

using measure::rational_node;
using measure::verification;
using measure::transformation_step;

// The domain rules this family states, checked against every quantity the problem carries and
// against the value it solves for, so a refusal names the same rule either way.
bool domain_holds(OpticsRelation relation, OpticsVariable variable, const Rational &si_value,
                  std::string *detail) {
    Rational value;
    if (!normalize_copy(si_value, &value)) {
        *detail = "the exact value could not be normalised";
        return false;
    }
    const std::string name = optics_variable_name(variable);
    switch (variable) {
        case OpticsVariable::IndexIncident:
        case OpticsVariable::IndexTransmitted: {
            Rational one;
            one.num = 1;
            Rational difference;
            if (!rational_sub(value, one, &difference)) {
                *detail = "comparing " + name + " against one exceeds exact integer arithmetic";
                return false;
            }
            if (difference.num < 0) {
                *detail = name + " is " + rational_text(value) +
                          ", and a refractive index is at least one";
                return false;
            }
            return true;
        }
        case OpticsVariable::SineIncident:
        case OpticsVariable::SineTransmitted:
        case OpticsVariable::SineFringe: {
            if (value.num < 0) {
                *detail = name + " is " + rational_text(value) +
                          ", and an angle measured from the normal has a non-negative sine";
                return false;
            }
            if (value.num > value.den) {
                *detail = name + " is " + rational_text(value) + ", which is above one";
                return false;
            }
            return true;
        }
        case OpticsVariable::SlitSpacing:
        case OpticsVariable::Wavelength: {
            if (value.num <= 0) {
                *detail = name + " is " + rational_text(value) + ", and it has to be positive";
                return false;
            }
            return true;
        }
        case OpticsVariable::FocalLength:
        case OpticsVariable::ObjectDistance:
        case OpticsVariable::ImageDistance: {
            if (value.num == 0) {
                *detail = name + " is zero, and the reciprocal the relation takes is undefined";
                return false;
            }
            return true;
        }
        case OpticsVariable::FringeOrder: {
            if (value.den != 1) {
                *detail = name + " is " + rational_text(value) + ", and a fringe order is an integer";
                return false;
            }
            if (value.num < 0) {
                *detail = name + " is negative, and the order counts fringes outward from the axis";
                return false;
            }
            if (relation == OpticsRelation::SingleSlit && value.num == 0) {
                *detail = "order zero is the central maximum of a single slit rather than a minimum";
                return false;
            }
            return true;
        }
    }
    *detail = "the variable is not one this relation carries";
    return false;
}

void record_context(Derivation &derivation, const Budget &budget, const RelationSchema &schema,
                    NodeId model, DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = schema.family_id;
    inputs.requested_method =
        std::string(schema.law_name) + ", exact SI substitution, exact rational isolation";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.branch_convention = "real domain";
    inputs.active_assumptions.push_back("the medium is homogeneous and the light is monochromatic");
    inputs.active_assumptions.push_back(schema.convention);
    inputs.unit_policy = "validate dimensions before substitution, convert exactly to SI, and round "
                         "only the reported answer";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

OpticsResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                        const OpticsProblem &problem, const Budget &budget, NodeId *model) {
    const RelationSchema &schema = relation_schema(problem.relation);
    const int unknown_index = member_index(schema, problem.unknown);
    if (unknown_index < 0)
        return failed(OpticsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      std::string(optics_variable_name(problem.unknown)) + " is not a variable of " +
                          schema.equation_text);

    const OpticsKnown *knowns[kMaxMembers] = {nullptr, nullptr, nullptr, nullptr};
    for (const OpticsKnown &known : problem.knowns) {
        const int index = member_index(schema, known.variable);
        if (index < 0)
            return failed(OpticsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(optics_variable_name(known.variable)) +
                              " is not a variable of " + schema.equation_text);
        if (index == unknown_index)
            return failed(OpticsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(optics_variable_name(known.variable)) +
                              " is both known and unknown");
        if (knowns[index])
            return failed(OpticsOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                          std::string(optics_variable_name(known.variable)) + " is given twice");
        std::string invalid_detail;
        if (!valid_quantity(known.quantity, &invalid_detail))
            return failed(OpticsOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(optics_variable_name(known.variable)) + ": " +
                              invalid_detail);
        const Dimension expected = variable_dimension(known.variable);
        if (known.quantity.unit.dimension != expected) {
            return failed(OpticsOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " has dimension " +
                              dimension_text(known.quantity.unit.dimension) + ", but " +
                              optics_variable_name(known.variable) + " requires " +
                              dimension_text(expected));
        }
        knowns[index] = &known;
    }
    for (size_t index = 0; index < schema.member_count; ++index) {
        if (static_cast<int>(index) != unknown_index && !knowns[index]) {
            return failed(OpticsOutcome::MissingKnown, DerivationStatus::InvalidInput,
                          std::string("missing known ") +
                              optics_variable_name(schema.members[index]));
        }
    }

    NodeId symbols[kMaxMembers] = {kNoNode, kNoNode, kNoNode, kNoNode};
    for (size_t index = 0; index < schema.member_count; ++index)
        symbols[index] = arena.symbol(variable_symbol(problem.relation, schema.members[index]));
    NodeId equation = kNoNode;
    if (schema.reciprocal) {
        NodeId minus_one = arena.integer("-1");
        NodeId object_term = arena.binary(Kind::Pow, symbols[1], minus_one);
        NodeId image_term = arena.binary(Kind::Pow, symbols[2], minus_one);
        NodeId focal_term = arena.binary(Kind::Pow, symbols[0], minus_one);
        equation = arena.binary(Kind::Equals, arena.binary(Kind::Add, object_term, image_term),
                                focal_term);
    } else {
        equation = arena.binary(Kind::Equals, arena.binary(Kind::Mul, symbols[0], symbols[1]),
                                arena.binary(Kind::Mul, symbols[2], symbols[3]));
    }
    *model = equation;
    if (arena.failed())
        return failed(OpticsOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    PlanPayload plan;
    plan.strategy_id = schema.family_id;
    plan.selected_strategy = std::string("Apply ") + schema.law_name + " as " +
                             schema.equation_text;
    for (const OpticsKnown &known : problem.knowns)
        plan.matched_problem_facts.push_back(known_text(known));
    plan.matched_problem_facts.push_back(std::string("find ") +
                                         optics_variable_name(problem.unknown));
    plan.alternatives_considered.push_back("general symbolic backend isolation");
    plan.selection_rationale =
        "every unknown of this relation is reached by exact rational arithmetic on the remaining "
        "quantities, so the local solver is sufficient and deterministic";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Find ") + optics_variable_name(problem.unknown);
    plan_step.rule_id = schema.family_id;
    plan_step.rule_name = schema.law_name;
    plan_step.explanation_short =
        std::string("Use ") + schema.equation_text + " and solve for the requested quantity";
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.optics.compatible-dimensions",
                                   "both sides of the relation have the same dimension",
                                   "dimensional analysis", EvidenceStrength::DimensionallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked against the registered relation");
    register_strategy_precondition(plan, plan_step, "pre.optics.declared-convention",
                                   "the sign and order convention is declared before it is used",
                                   "registered relation convention",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted, "recorded with the relation");
    register_strategy_precondition(plan, plan_step, "pre.optics.physical-givens",
                                   "every given quantity is inside this family's stated domain",
                                   "registered optical domain rules",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked after conversion to SI");
    if (!meter.step())
        return OpticsResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    bool dimensions_fit = true;
    bool dimensions_match = false;
    std::string dimension_observed;
    if (schema.reciprocal) {
        Dimension object_term;
        Dimension image_term;
        Dimension focal_term;
        dimensions_fit = dimension_power(variable_dimension(schema.members[1]), -1, &object_term) &&
                         dimension_power(variable_dimension(schema.members[2]), -1, &image_term) &&
                         dimension_power(variable_dimension(schema.members[0]), -1, &focal_term);
        dimensions_match =
            dimensions_fit && object_term == image_term && object_term == focal_term;
        dimension_observed = dimensions_fit ? dimension_text(object_term) + " against " +
                                                  dimension_text(focal_term)
                                            : "a reciprocal dimension does not fit";
    } else {
        Dimension left;
        Dimension right;
        dimensions_fit = dimension_multiply(variable_dimension(schema.members[0]),
                                            variable_dimension(schema.members[1]), &left) &&
                         dimension_multiply(variable_dimension(schema.members[2]),
                                            variable_dimension(schema.members[3]), &right);
        dimensions_match = dimensions_fit && left == right;
        dimension_observed =
            dimensions_fit ? dimension_text(left) + " against " + dimension_text(right)
                           : "a product dimension does not fit";
    }
    if (!meter.step())
        return OpticsResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the dimensions of ") + schema.equation_text;
        step.rule_id = "physics.optics.check-dimensions";
        step.rule_name = "Dimensional analysis";
        step.explanation_short = "Both sides of the optical relation must have the same dimension";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.optics.dimensions-agree", "both sides of the optical relation have one dimension"});
        step.verifications.push_back(verification(
            "dimensional analysis", dimension_observed, EvidenceStrength::DimensionallyValid,
            dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = std::string(schema.equation_text) + " is dimensionally consistent";
        check.check_method = "combine the dimensions on each side";
        check.expected_relation = "equal dimensions";
        check.observed_result = dimension_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.optics.compatible-dimensions",
        dimensions_match ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        dimension_observed);
    if (!dimensions_fit)
        return failed(OpticsOutcome::ArithmeticOverflow, DerivationStatus::VerificationFailed,
                      "the relation dimension does not fit");
    if (!dimensions_match)
        return failed(OpticsOutcome::DimensionMismatch, DerivationStatus::VerificationFailed,
                      std::string(schema.equation_text) + " is dimensionally inconsistent");

    Quantity si_quantities[kMaxMembers];
    std::string conversions;
    bool converted_units = false;
    bool conversions_agree = true;
    std::string conversion_observed;
    size_t conversions_checked = 0;
    for (size_t index = 0; index < schema.member_count; ++index) {
        if (static_cast<int>(index) == unknown_index)
            continue;
        const OpticsKnown &known = *knowns[index];
        Rational value;
        if (!to_si(known.quantity, &value)) {
            return failed(OpticsOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
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
            OpticsKnown converted_known;
            converted_known.variable = known.variable;
            converted_known.quantity = converted;
            conversions += known_text(known) + " becomes " + known_text(converted_known);
        }
        // Dividing back is the check. Multiplying again repeats the call to_si made, so it agrees
        // with itself whatever either operand is and the failed arm cannot be reached.
        Rational recovered;
        const bool inverted =
            rational_div(si_quantities[index].value, known.quantity.unit.scale, &recovered);
        ++conversions_checked;
        if (inverted && rational_equal(recovered, known.quantity.value))
            continue;
        conversions_agree = false;
        if (!conversion_observed.empty())
            conversion_observed += ", ";
        conversion_observed += std::string(variable_symbol(problem.relation, known.variable)) +
                               " was stored as " + rational_text(si_quantities[index].value) +
                               ", which does not divide back to " +
                               rational_text(known.quantity.value);
    }
    if (conversion_observed.empty()) {
        conversion_observed = std::to_string(conversions_checked) +
                              " stored values divide back to the given by their table scale";
    }
    if (converted_units) {
        if (!meter.step())
            return OpticsResult();
        Step step = transformation_step(
            "Convert the known quantities to SI", "physics.optics.convert-units", "Unit conversion",
            "Apply each unit's exact scale to SI",
            std::string("Use metres for every length in ") + schema.equation_text +
                " so the two sides use consistent units. A wavelength in nanometres and a slit "
                "spacing in millimetres cancel only after both reach metres.");
        step.verifications.push_back(
            verification("divide each stored value by its table scale and compare with the given",
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
        return failed(OpticsOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      "a converted quantity does not divide back to the given by its table scale");

    bool givens_physical = true;
    std::string domain_observed;
    for (size_t index = 0; index < schema.member_count; ++index) {
        if (static_cast<int>(index) == unknown_index)
            continue;
        std::string detail;
        if (domain_holds(problem.relation, schema.members[index], si_quantities[index].value,
                         &detail))
            continue;
        givens_physical = false;
        if (!domain_observed.empty())
            domain_observed += "; ";
        domain_observed += detail;
    }
    if (domain_observed.empty())
        domain_observed = "every given quantity is inside this family's stated domain";
    if (!meter.step())
        return OpticsResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check the givens against the optical domain";
        step.rule_id = "physics.optics.check-domain";
        step.rule_name = "Optical domain rules";
        step.explanation_short =
            "An index is at least one, a sine lies in [0, 1], a length is positive and an order "
            "is a whole number";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.optics.domain-holds",
             "every quantity the relation uses lies inside this family's stated domain"});
        step.verifications.push_back(verification(
            "registered optical domain rules", domain_observed, EvidenceStrength::StructurallyValid,
            givens_physical ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "the givens are physically admissible for " +
                             std::string(schema.law_name);
        check.check_method = "apply each registered domain rule to the converted SI value";
        check.expected_relation = "every rule holds";
        check.observed_result = domain_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.optics.physical-givens",
        givens_physical ? VerificationOutcome::Passed : VerificationOutcome::Failed,
        domain_observed);
    if (!givens_physical)
        return failed(OpticsOutcome::UnphysicalValue, DerivationStatus::VerificationFailed,
                      domain_observed);

    if (!meter.step())
        return OpticsResult();
    {
        Step step;
        step.phase = "solve";
        step.goal = "State the convention the answer is read under";
        step.rule_id = "physics.optics.sign-convention";
        step.rule_name = "Declared convention";
        step.explanation_short = schema.convention;
        step.explanation_detailed =
            std::string("Nothing below this line decides a sign on its own. ") + schema.convention +
            ". Read every answer of this family against that sentence, because the same arithmetic "
            "under the opposite convention names a different physical situation.";
        step.claim = ClaimType::Definition;
        step.assumptions_before.push_back(schema.convention);
        step.proof_obligations.push_back(
            {"obl.optics.convention-declared",
             "the sign and order convention is stated before any signed answer is read"});
        step.verifications.push_back(verification("registered relation convention",
                                                  schema.convention,
                                                  EvidenceStrength::StructurallyValid,
                                                  VerificationOutcome::Passed));
        CheckPayload check;
        check.target_claim = "the convention is visible rather than baked into the arithmetic";
        check.check_method = "read the convention registered with the relation";
        check.expected_relation = "a nonempty declared convention";
        check.observed_result = schema.convention;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(plan_id, "pre.optics.declared-convention",
                                          VerificationOutcome::Passed, schema.convention);

    NodeId expressions[kMaxMembers] = {kNoNode, kNoNode, kNoNode, kNoNode};
    for (size_t index = 0; index < schema.member_count; ++index) {
        expressions[index] = static_cast<int>(index) == unknown_index
                                 ? symbols[index]
                                 : rational_node(arena, si_quantities[index].value);
    }
    NodeId substituted = kNoNode;
    if (schema.reciprocal) {
        NodeId minus_one = arena.integer("-1");
        substituted = arena.binary(
            Kind::Equals,
            arena.binary(Kind::Add, arena.binary(Kind::Pow, expressions[1], minus_one),
                         arena.binary(Kind::Pow, expressions[2], minus_one)),
            arena.binary(Kind::Pow, expressions[0], minus_one));
    } else {
        substituted =
            arena.binary(Kind::Equals, arena.binary(Kind::Mul, expressions[0], expressions[1]),
                         arena.binary(Kind::Mul, expressions[2], expressions[3]));
    }
    if (arena.failed())
        return failed(OpticsOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    if (!meter.step())
        return OpticsResult();
    {
        Step step = transformation_step(
            "Substitute the known SI values", "physics.optics.substitute", "Substitution",
            "Replace each known symbol by its exact SI value",
            std::string("Put the converted quantities into ") + schema.equation_text +
                ", leave the requested quantity as a symbol, and isolate it with exact rational "
                "arithmetic.");
        step.verifications.push_back(verification(
            "typed known-quantity lookup",
            std::to_string(schema.member_count - 1) + " known quantities were substituted",
            EvidenceStrength::StructurallyValid, VerificationOutcome::Passed));
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        TransformationPayload payload;
        payload.before = equation;
        payload.after = substituted;
        payload.concrete_action = "Substitute the known quantities after exact SI conversion";
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    OpticsResult result;
    result.unknown = symbols[unknown_index];
    result.equation = equation;
    result.substituted = substituted;
    result.convention = schema.convention;

    Rational candidate;
    bool computed = false;
    bool determined = true;
    if (schema.reciprocal) {
        Rational one;
        one.num = 1;
        Rational reciprocals[kMaxMembers];
        bool fits = true;
        for (size_t index = 0; index < schema.member_count; ++index) {
            if (static_cast<int>(index) == unknown_index)
                continue;
            fits = fits && rational_div(one, si_quantities[index].value, &reciprocals[index]);
        }
        Rational target;
        if (fits && unknown_index == 0) {
            fits = rational_add(reciprocals[1], reciprocals[2], &target);
        } else if (fits) {
            const size_t other = unknown_index == 1 ? 2 : 1;
            fits = rational_sub(reciprocals[0], reciprocals[other], &target);
        }
        if (!fits) {
            computed = false;
        } else if (target.num == 0) {
            determined = false;
        } else {
            computed = rational_div(one, target, &candidate);
        }
    } else {
        const bool unknown_left = unknown_index < 2;
        const size_t partner = unknown_left ? (unknown_index == 0 ? 1u : 0u)
                                            : (unknown_index == 2 ? 3u : 2u);
        const size_t first = unknown_left ? 2u : 0u;
        const size_t second = unknown_left ? 3u : 1u;
        Rational product;
        if (!rational_mul(si_quantities[first].value, si_quantities[second].value, &product)) {
            computed = false;
        } else if (si_quantities[partner].value.num == 0) {
            determined = false;
        } else {
            computed = rational_div(product, si_quantities[partner].value, &candidate);
        }
    }
    if (!determined) {
        result.outcome = OpticsOutcome::Indeterminate;
        result.status = DerivationStatus::InvalidInput;
        result.detail = "the given values do not determine one " +
                        std::string(optics_variable_name(problem.unknown)) + " in " +
                        schema.equation_text;
        return result;
    }
    if (!computed) {
        result.outcome = OpticsOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "isolating " + std::string(optics_variable_name(problem.unknown)) +
                        " exceeds exact integer arithmetic";
        return result;
    }

    if (problem.relation == OpticsRelation::Refraction) {
        const int incident_index = member_index(schema, OpticsVariable::IndexIncident);
        const int transmitted_index = member_index(schema, OpticsVariable::IndexTransmitted);
        if (unknown_index != incident_index && unknown_index != transmitted_index) {
            Rational critical;
            // Total internal reflection only exists entering the rarer medium, so a ratio above
            // one is not a sine any angle has and is left unreported.
            if (rational_div(si_quantities[transmitted_index].value,
                             si_quantities[incident_index].value, &critical) &&
                critical.num <= critical.den) {
                result.has_critical_sine = true;
                result.critical_sine_text =
                    ratio_text(critical, si_quantities[transmitted_index].precision,
                               si_quantities[incident_index].precision);
            }
        }
        if (problem.unknown == OpticsVariable::SineTransmitted && candidate.num > candidate.den) {
            if (!meter.step())
                return OpticsResult();
            Step step;
            step.phase = "check";
            step.goal = "Check for total internal reflection";
            step.rule_id = "physics.optics.total-internal-reflection";
            step.rule_name = "Total internal reflection";
            step.explanation_short =
                "No transmitted ray exists once the incident sine passes the critical sine n2/n1";
            step.explanation_detailed =
                "Snell's law would need a transmitted sine above one, which no angle has. The "
                "light is entirely reflected, so the answer is the refusal together with the "
                "critical sine the incident ray passed.";
            step.claim = ClaimType::Definition;
            step.proof_obligations.push_back(
                {"obl.optics.critical-angle",
                 "a transmitted sine above one is reported as total internal reflection"});
            const std::string observed =
                "the transmitted sine would be " + rational_text(candidate) +
                ", above the critical sine " + result.critical_sine_text;
            step.verifications.push_back(verification("exact comparison against one", observed,
                                                      EvidenceStrength::CandidateChecked,
                                                      VerificationOutcome::Passed));
            CheckPayload check;
            check.target_claim = "Snell's law has no transmitted ray for this incident angle";
            check.check_method = "compare the isolated transmitted sine against one";
            check.expected_relation = "a transmitted sine at most one";
            check.observed_result = observed;
            derivation.add_check(plan_id, std::move(step), std::move(check));
            result.outcome = OpticsOutcome::TotalInternalReflection;
            result.detail = observed;
            return result;
        }
    }

    std::string candidate_domain;
    if (!domain_holds(problem.relation, problem.unknown, candidate, &candidate_domain)) {
        result.outcome = OpticsOutcome::UnphysicalValue;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = candidate_domain;
        return result;
    }

    si_quantities[unknown_index].value = candidate;
    si_quantities[unknown_index].unit = si_unit(problem.unknown);
    Precision answer_precision;
    for (size_t index = 0; index < schema.member_count; ++index) {
        if (static_cast<int>(index) != unknown_index)
            answer_precision = precision_combine(answer_precision, si_quantities[index].precision);
    }
    si_quantities[unknown_index].precision = answer_precision;

    bool check_computed = false;
    bool candidate_passes = false;
    if (schema.reciprocal) {
        Rational one;
        one.num = 1;
        Rational object_term;
        Rational image_term;
        Rational focal_term;
        Rational sum;
        check_computed = rational_div(one, si_quantities[1].value, &object_term) &&
                         rational_div(one, si_quantities[2].value, &image_term) &&
                         rational_div(one, si_quantities[0].value, &focal_term) &&
                         rational_add(object_term, image_term, &sum);
        candidate_passes = check_computed && rational_equal(sum, focal_term);
    } else {
        Rational left;
        Rational right;
        check_computed = rational_mul(si_quantities[0].value, si_quantities[1].value, &left) &&
                         rational_mul(si_quantities[2].value, si_quantities[3].value, &right);
        candidate_passes = check_computed && rational_equal(left, right);
    }
    if (!meter.step())
        return OpticsResult();
    {
        Step step;
        step.phase = "check";
        step.goal = std::string("Check the candidate in ") + schema.equation_text;
        step.rule_id = "physics.optics.check-candidate";
        step.rule_name = "Substitution check";
        step.explanation_short = "Put the candidate back into the optical relation";
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.optics.candidate-satisfies",
             "the candidate satisfies the original optical relation"});
        const VerificationOutcome outcome = !check_computed ? VerificationOutcome::Inconclusive
                                            : candidate_passes ? VerificationOutcome::Passed
                                                               : VerificationOutcome::Failed;
        const std::string observed = !check_computed
                                         ? "the exact evaluation exceeds integer arithmetic"
                                     : candidate_passes ? "both sides are exactly equal"
                                                        : "the two sides are not equal";
        step.verifications.push_back(
            verification("exact substitution into the original relation", observed,
                         EvidenceStrength::CandidateChecked, outcome));
        CheckPayload check;
        check.target_claim = std::string(variable_symbol(problem.relation, problem.unknown)) +
                             " = " + rational_text(candidate) + " satisfies " +
                             schema.equation_text;
        check.check_method = "evaluate both sides from the exact SI values";
        check.expected_relation = "the two sides are equal";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!check_computed) {
        result.outcome = OpticsOutcome::ArithmeticOverflow;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "checking the candidate exceeds exact integer arithmetic";
        return result;
    }
    if (!candidate_passes) {
        result.outcome = OpticsOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = std::string("the candidate failed substitution into ") +
                        schema.equation_text;
        return result;
    }

    if (schema.reciprocal) {
        Rational negated;
        Rational zero;
        Rational magnification;
        if (rational_sub(zero, si_quantities[2].value, &negated) &&
            rational_div(negated, si_quantities[1].value, &magnification)) {
            result.has_magnification = true;
            result.magnification_text = ratio_text(magnification, si_quantities[2].precision,
                                                   si_quantities[1].precision);
        }
    }

    result.outcome = OpticsOutcome::Solved;
    result.value = rational_node(arena, candidate);
    if (arena.failed())
        return failed(OpticsOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    result.quantity = si_quantities[unknown_index];
    result.quantity.precision = precision_at_digits(candidate, result.quantity.precision);
    result.value_text = rational_text(candidate);
    result.unit_text = result.quantity.unit.text;
    if (result.quantity.precision.kind != NumberKind::Measured)
        return result;

    std::string reported;
    if (!precision_rounded_text(candidate, result.quantity.precision, &reported)) {
        result.outcome = OpticsOutcome::ArithmeticOverflow;
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
        result.outcome = OpticsOutcome::VerificationFailed;
        result.status = DerivationStatus::VerificationFailed;
        result.detail = reported + " is further than half a unit in its last place from " +
                        result.value_text;
        result.value = kNoNode;
        result.value_text.clear();
        result.unit_text.clear();
        return result;
    }
    if (!meter.step())
        return OpticsResult();
    if (checked == HalfPlace::Unreadable) {
        result.status = DerivationStatus::SolvedButUnchecked;
        result.detail = reported +
                        " could not be read back as a decimal, so it was never compared against " +
                        result.value_text;
        Step step;
        step.phase = "report";
        step.goal = "Report the answer to the measured precision";
        step.rule_id = "physics.optics.significant-figures";
        step.rule_name = "Significant figures";
        step.claim = ClaimType::NoClaim;
        step.explanation_short = "Use the fewest significant figures among the measured givens";
        step.explanation_detailed =
            "The rounded spelling could not be read back as a decimal, so it was never compared "
            "against the exact value. The exact value is reported instead.";
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
    step.rule_id = "physics.optics.significant-figures";
    step.rule_name = "Significant figures";
    step.explanation_short = "Use the fewest significant figures among the measured givens";
    step.explanation_detailed =
        "A wavelength written to three figures cannot buy a fringe spacing written to six, so the "
        "answer is reported to the fewest significant figures among the measurements it came from. "
        "Every step above this one keeps the exact value, because rounding partway through throws "
        "away figures the final rounding cannot get back.";
    step.claim = ClaimType::NoClaim;
    step.verifications.push_back(verification(
        "exact comparison against the unrounded value",
        reported + " is within half a unit in the last place of " + result.value_text,
        EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
    step.proof_obligations.push_back(
        {"obl.physics.reported-within-half-place",
         "the reported value is within half a unit in the last place of the exact one"});
    TransformationPayload payload;
    payload.before = result.value;
    payload.after = reported.find('.') == std::string::npos ? arena.integer(reported)
                                                            : arena.decimal(reported);
    payload.concrete_action = "Report " + result.value_text + " as " + reported;
    payload.reversible = false;
    if (arena.failed())
        return failed(OpticsOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
    result.value_text = reported;
    return result;
}

}  // namespace

const char *optics_relation_name(OpticsRelation relation) {
    switch (relation) {
        case OpticsRelation::Refraction: return "refraction";
        case OpticsRelation::ThinLens: return "thin lens";
        case OpticsRelation::SphericalMirror: return "spherical mirror";
        case OpticsRelation::DoubleSlit: return "two-slit interference";
        case OpticsRelation::SingleSlit: return "single-slit diffraction";
    }
    return "invalid relation";
}

const char *optics_variable_name(OpticsVariable variable) {
    switch (variable) {
        case OpticsVariable::IndexIncident: return "incident index";
        case OpticsVariable::SineIncident: return "incident sine";
        case OpticsVariable::IndexTransmitted: return "transmitted index";
        case OpticsVariable::SineTransmitted: return "transmitted sine";
        case OpticsVariable::FocalLength: return "focal length";
        case OpticsVariable::ObjectDistance: return "object distance";
        case OpticsVariable::ImageDistance: return "image distance";
        case OpticsVariable::SlitSpacing: return "slit spacing";
        case OpticsVariable::SineFringe: return "fringe sine";
        case OpticsVariable::FringeOrder: return "fringe order";
        case OpticsVariable::Wavelength: return "wavelength";
    }
    return "invalid variable";
}

const char *optics_outcome_name(OpticsOutcome outcome) {
    switch (outcome) {
        case OpticsOutcome::Solved: return "solved";
        case OpticsOutcome::TotalInternalReflection: return "total internal reflection";
        case OpticsOutcome::InvalidProblem: return "invalid problem";
        case OpticsOutcome::MissingKnown: return "missing known";
        case OpticsOutcome::DuplicateKnown: return "duplicate known";
        case OpticsOutcome::DimensionMismatch: return "dimension mismatch";
        case OpticsOutcome::UnphysicalValue: return "unphysical value";
        case OpticsOutcome::Indeterminate: return "indeterminate";
        case OpticsOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case OpticsOutcome::VerificationFailed: return "verification failed";
        case OpticsOutcome::Cancelled: return "cancelled";
        case OpticsOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

OpticsResult solve_optics(Arena &arena, Derivation &derivation, const OpticsProblem &problem,
                          const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    OpticsResult result = solve_body(arena, derivation, meter, problem, budget, &model);

    const bool nested_halt = result.outcome == OpticsOutcome::Cancelled ||
                             result.outcome == OpticsOutcome::ResourceExceeded;
    if (meter.stopped() || nested_halt) {
        const bool cancelled =
            result.outcome == OpticsOutcome::Cancelled || meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        OpticsResult halted;
        halted.outcome = cancelled ? OpticsOutcome::Cancelled : OpticsOutcome::ResourceExceeded;
        halted.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        halted.cost = meter.cost();
        record_context(derivation, budget, relation_schema(problem.relation), model, halted.status);
        return halted;
    }

    // The refusal is a conclusion this family proved rather than a failure, so it carries the
    // derivation's own verified status the way an answer does.
    if (result.outcome == OpticsOutcome::Solved ||
        result.outcome == OpticsOutcome::TotalInternalReflection)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, relation_schema(problem.relation), model, result.status);
    return result;
}

}  // namespace nps
