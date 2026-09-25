#include "nps/physics/forces.h"

#include <utility>
#include <vector>

#include "nps/core/context.h"
#include "nps/core/rational.h"

namespace nps {
namespace {

Dimension mass_dimension() {
    Dimension dimension;
    dimension.mass = 1;
    return dimension;
}

Dimension acceleration_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.time = -2;
    return dimension;
}

Dimension force_dimension() {
    Dimension dimension;
    dimension.length = 1;
    dimension.mass = 1;
    dimension.time = -2;
    return dimension;
}

ForcesResult failed(ForcesOutcome outcome, DerivationStatus status, const std::string &detail) {
    ForcesResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
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

bool add_check(Derivation &derivation, Meter &meter, StepId parent, const char *rule_id,
               const char *rule_name, const std::string &goal, const std::string &explanation,
               const char *obligation_id, const std::string &obligation, const char *method,
               const std::string &verification_detail, EvidenceStrength passing,
               VerificationOutcome outcome, const std::string &target, const std::string &expected,
               const std::string &observed) {
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
    step.verifications.push_back(verification(method, verification_detail, passing, outcome));
    CheckPayload payload;
    payload.target_claim = target;
    payload.check_method = method;
    payload.expected_relation = expected;
    payload.observed_result = observed;
    derivation.add_check(parent, std::move(step), std::move(payload));
    return true;
}

bool add_transformation(Derivation &derivation, Meter &meter, StepId parent, const char *rule_id,
                        const char *rule_name, const std::string &goal,
                        const std::string &explanation, const std::string &detailed,
                        const char *obligation_id, const std::string &obligation,
                        VerificationRecord record, ClaimType claim, NodeId before,
                        const std::string &action, NodeId after) {
    if (!meter.rewrite() || !meter.step())
        return false;
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = claim;
    step.proof_obligations.push_back({obligation_id, obligation});
    record.evidence_id = obligation_id;
    step.verifications.push_back(std::move(record));
    TransformationPayload payload;
    payload.before = before;
    payload.after = after;
    payload.concrete_action = action;
    payload.reversible = true;
    derivation.add_transformation(parent, std::move(step), std::move(payload));
    return true;
}

bool normalized(const Rational &source, Rational *value) {
    *value = source;
    return normalise(&value->num, &value->den);
}

bool quantity_si(const Quantity &quantity, const Dimension &expected, Rational *value,
                 std::string *detail) {
    if (quantity.unit.dimension != expected) {
        *detail = "the unit " + quantity.unit.text + " has dimension " +
                  dimension_text(quantity.unit.dimension) + " where " + dimension_text(expected) +
                  " is required";
        return false;
    }
    Rational scale;
    if (!normalized(quantity.unit.scale, &scale) || scale.num <= 0) {
        *detail = "the unit " + quantity.unit.text + " has an invalid SI conversion scale";
        return false;
    }
    if (!to_si(quantity, value)) {
        *detail = "converting " + quantity.unit.text + " to SI exceeds exact arithmetic";
        return false;
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
    return arena.binary(Kind::Mul, numerator,
                        arena.binary(Kind::Pow, denominator, arena.integer("-1")));
}

std::string newtons(const Rational &value) { return rational_text(value) + " N"; }

// The whole family is a chain of exact rational steps, and any one of them can overflow. Carrying
// that as a flag rather than an exception keeps the device build, which has neither exceptions nor
// RTTI, on the same path as the host.
struct Exact {
    Rational value;
    bool ok = true;
};

Exact mul(const Exact &a, const Exact &b) {
    Exact out;
    out.ok = a.ok && b.ok && rational_mul(a.value, b.value, &out.value);
    return out;
}

Exact add(const Exact &a, const Exact &b) {
    Exact out;
    out.ok = a.ok && b.ok && rational_add(a.value, b.value, &out.value);
    return out;
}

Exact sub(const Exact &a, const Exact &b) {
    Exact out;
    out.ok = a.ok && b.ok && rational_sub(a.value, b.value, &out.value);
    return out;
}

Exact exact(const Rational &value) {
    Exact out;
    out.value = value;
    return out;
}

Exact exact(int64_t value) {
    Exact out;
    out.value.num = value;
    out.value.den = 1;
    return out;
}

Rational negated(const Rational &value) {
    Rational out = value;
    out.num = -out.num;
    return out;
}

Rational absolute(const Rational &value) {
    Rational out = value;
    if (out.num < 0)
        out.num = -out.num;
    return out;
}

bool compare(const Rational &a, const Rational &b, int *out) {
    Rational difference;
    if (!rational_sub(a, b, &difference))
        return false;
    *out = difference.num < 0 ? -1 : (difference.num > 0 ? 1 : 0);
    return true;
}

// The force the request asks for is the one the problem does not supply, so the inventory marks it
// unknown instead of publishing it as a given.
bool requested(const ForcesProblem &problem, ForceKind kind) {
    switch (problem.unknown) {
        case ForcesUnknown::Acceleration: return false;
        case ForcesUnknown::AppliedForce: return kind == ForceKind::Applied;
        case ForcesUnknown::NormalForce: return kind == ForceKind::Normal;
        case ForcesUnknown::FrictionForce: return kind == ForceKind::Friction;
    }
    return false;
}

void add_entry(ForcesResult *result, ForceKind kind, const std::string &label,
               const std::string &agent, const Rational &along, const Rational &across,
               const Rational &magnitude, bool known) {
    ForceEntry entry;
    entry.kind = kind;
    entry.label = label;
    entry.agent = agent;
    entry.along = along;
    entry.across = across;
    entry.magnitude = magnitude;
    entry.magnitude_text = newtons(magnitude);
    entry.along_text = newtons(along);
    entry.across_text = newtons(across);
    entry.known = known;
    result->inventory.push_back(std::move(entry));
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, const ForcesProblem &problem,
                    const std::vector<std::string> &assumptions) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.forces.newton-second-law";
    inputs.requested_method = "inventory the forces on one body, resolve them onto the surface "
                              "axes, sum each axis under Newton's second law and solve for the "
                              "requested unknown";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.active_assumptions = assumptions;
    inputs.active_assumptions.push_back(std::string("surface is ") +
                                        surface_kind_name(problem.surface));
    inputs.active_assumptions.push_back(std::string("friction model is ") +
                                        friction_model_name(problem.friction));
    inputs.unit_policy = "exact SI conversion throughout, newtons reported exactly";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

ForcesResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                        const ForcesProblem &problem, NodeId *model,
                        std::vector<std::string> *assumptions) {
    if (problem.body.empty() || problem.support.empty()) {
        return failed(ForcesOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the body and its support both need a name");
    }
    std::string detail;
    Rational mass;
    if (!quantity_si(problem.mass, mass_dimension(), &mass, &detail))
        return failed(ForcesOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "mass: " + detail);
    if (mass.num <= 0) {
        return failed(ForcesOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the body needs a positive mass");
    }
    Rational gravity;
    if (!quantity_si(problem.gravity, acceleration_dimension(), &gravity, &detail))
        return failed(ForcesOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "gravity: " + detail);
    if (gravity.num <= 0) {
        return failed(ForcesOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the gravitational field strength must be positive");
    }

    Rational sine;
    Rational cosine;
    if (problem.surface == SurfaceKind::Horizontal) {
        sine.num = 0;
        sine.den = 1;
        cosine.num = 1;
        cosine.den = 1;
    } else {
        if (!normalized(problem.incline_sin, &sine) || !normalized(problem.incline_cos, &cosine)) {
            return failed(ForcesOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "the incline sine and cosine are not valid exact values");
        }
        Rational sine_squared;
        Rational cosine_squared;
        Rational identity;
        if (!rational_mul(sine, sine, &sine_squared) ||
            !rational_mul(cosine, cosine, &cosine_squared) ||
            !rational_add(sine_squared, cosine_squared, &identity)) {
            return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                          "checking the incline trigonometric identity exceeds exact arithmetic");
        }
        if (identity.num != identity.den || sine.num < 0 || cosine.num <= 0) {
            // An angle whose sine and cosine are not an exact rational pair is outside the
            // envelope. Refusing it is the honest answer, because a decimal approximation here
            // would put an unstated error into every force below it.
            return failed(ForcesOutcome::InclineAngleNotExact, DerivationStatus::InvalidInput,
                          "the incline needs an exact sine and cosine with a non-negative sine, a "
                          "positive cosine and sin^2 + cos^2 = 1");
        }
    }

    Rational coefficient;
    if (problem.friction == FrictionModel::None) {
        coefficient.num = 0;
        coefficient.den = 1;
    } else {
        if (!normalized(problem.friction_coefficient, &coefficient) || coefficient.num < 0) {
            return failed(ForcesOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          "the friction coefficient must be an exact non-negative value");
        }
    }
    if (problem.friction == FrictionModel::Kinetic && problem.motion == MotionSense::Undeclared) {
        // Kinetic friction opposes the motion, so without a declared direction there is no sign to
        // give it. Assuming one would be inventing the answer.
        return failed(ForcesOutcome::MotionSenseUndeclared, DerivationStatus::InvalidInput,
                      "kinetic friction needs a declared direction of motion along the axis");
    }
    if (problem.friction == FrictionModel::Static && !problem.assume_equilibrium) {
        return failed(ForcesOutcome::UnsupportedArrangement, DerivationStatus::InvalidInput,
                      "static friction applies while the body is not sliding, so it cannot be "
                      "combined with a declared acceleration along the surface");
    }
    if (problem.friction == FrictionModel::Static &&
        problem.unknown == ForcesUnknown::AppliedForce) {
        // Static friction takes whatever value the balance needs, so it absorbs any applied force
        // inside its limit and one equation leaves both of them unfixed.
        return failed(ForcesOutcome::Underdetermined, DerivationStatus::InvalidInput,
                      "static friction adapts to the applied force, so an equilibrium with both "
                      "unknown does not determine the applied force");
    }

    Rational applied;
    applied.num = 0;
    applied.den = 1;
    if (problem.has_applied && !quantity_si(problem.applied, force_dimension(), &applied, &detail))
        return failed(ForcesOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "applied force: " + detail);
    Rational tension;
    tension.num = 0;
    tension.den = 1;
    if (problem.has_tension && !quantity_si(problem.tension, force_dimension(), &tension, &detail))
        return failed(ForcesOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "tension: " + detail);
    Rational acceleration;
    acceleration.num = 0;
    acceleration.den = 1;
    if (problem.has_acceleration &&
        !quantity_si(problem.acceleration, acceleration_dimension(), &acceleration, &detail))
        return failed(ForcesOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "acceleration: " + detail);
    if (problem.assume_equilibrium && problem.has_acceleration && acceleration.num != 0) {
        return failed(ForcesOutcome::InconsistentInput, DerivationStatus::InvalidInput,
                      "an equilibrium problem cannot also declare a non-zero acceleration");
    }
    if (!problem.assume_equilibrium && !problem.has_acceleration &&
        problem.unknown != ForcesUnknown::Acceleration) {
        return failed(ForcesOutcome::Underdetermined, DerivationStatus::InvalidInput,
                      "solving for " + std::string(forces_unknown_name(problem.unknown)) +
                          " under acceleration needs the acceleration to be supplied");
    }
    if (problem.unknown == ForcesUnknown::Acceleration && problem.assume_equilibrium) {
        return failed(ForcesOutcome::InconsistentInput, DerivationStatus::InvalidInput,
                      "the acceleration is already fixed at zero by the equilibrium assumption");
    }
    if (problem.unknown == ForcesUnknown::AppliedForce && problem.has_applied) {
        return failed(ForcesOutcome::InconsistentInput, DerivationStatus::InvalidInput,
                      "the applied force cannot be both supplied and requested");
    }

    *model = arena.call(
        "forces_problem",
        {arena.symbol(problem.body), rational_node(arena, mass), rational_node(arena, gravity),
         arena.symbol(surface_kind_name(problem.surface)), rational_node(arena, sine),
         rational_node(arena, cosine), arena.symbol(friction_model_name(problem.friction)),
         rational_node(arena, coefficient), arena.symbol(motion_sense_name(problem.motion)),
         arena.symbol(forces_unknown_name(problem.unknown))});
    if (arena.failed()) {
        return failed(ForcesOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }

    assumptions->push_back("the body is a particle, so every force acts at one point");
    assumptions->push_back("the axes run along the supporting surface and across it");
    if (problem.has_tension) {
        assumptions->push_back("the string is massless and inextensible, so it transmits its "
                               "tension unchanged");
    }
    assumptions->push_back("the surface stays in contact, so the across-axis acceleration is zero");

    PlanPayload plan;
    plan.strategy_id = "physics.forces.plan";
    plan.selected_strategy = "Newton's second law on axes along and across the surface";
    plan.matched_problem_facts.push_back("body: " + problem.body);
    plan.matched_problem_facts.push_back("mass: " + rational_text(mass) + " kg");
    plan.matched_problem_facts.push_back(std::string("surface: ") +
                                         surface_kind_name(problem.surface));
    plan.matched_problem_facts.push_back(std::string("friction: ") +
                                         friction_model_name(problem.friction));
    plan.matched_problem_facts.push_back(std::string("unknown: ") +
                                         forces_unknown_name(problem.unknown));
    plan.alternatives_considered.push_back("energy methods, which do not give the normal force");
    plan.alternatives_considered.push_back("a connected-body system, which is outside the envelope");
    plan.selection_rationale = "the force inventory on a single body determines both axis sums, and "
                               "the requested unknown appears linearly in one of them";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find the " + std::string(forces_unknown_name(problem.unknown)) + " on " +
                     problem.body;
    plan_step.rule_id = "physics.forces.plan";
    plan_step.rule_name = "Free-body plan";
    plan_step.explanation_short =
        "Inventory the forces, resolve them onto the axes and sum each axis";
    plan_step.assumptions_before = *assumptions;
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.forces.single-body",
                                   "exactly one body carries the force inventory",
                                   "arrangement check", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed,
                                   "checked before the plan was recorded");
    register_strategy_precondition(plan, plan_step, "pre.forces.exact-angle",
                                   "the incline angle has an exact sine and cosine",
                                   "trigonometric identity", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed,
                                   "checked before the plan was recorded");
    register_strategy_precondition(plan, plan_step, "pre.forces.input-dimensions",
                                   "every supplied quantity has its required dimension",
                                   "dimensional analysis", EvidenceStrength::DimensionallyValid,
                                   VerificationOutcome::Passed,
                                   "checked before the plan was recorded");
    register_strategy_precondition(plan, plan_step, "pre.forces.friction-declared",
                                   "the friction model and, for kinetic friction, the direction of "
                                   "motion are declared",
                                   "declaration check", EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed,
                                   "checked before the plan was recorded");
    if (!meter.step())
        return ForcesResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    if (!add_check(derivation, meter, plan_id, "physics.forces.check-input-dimensions",
                   "Input dimensions", "Check every supplied quantity's dimension",
                   "A mass, a field strength and a force each have one dimension", 
                   "obl.forces.input-dimensions",
                   "every supplied quantity carries its required dimension", "dimensional analysis",
                   "mass in M, gravity in L T^-2 and every force in L M T^-2",
                   EvidenceStrength::DimensionallyValid, VerificationOutcome::Passed,
                   "the supplied quantities have their required dimensions",
                   "each quantity matches its dimension",
                   "mass, field strength and forces all matched")) {
        return ForcesResult();
    }

    const std::string identity_detail =
        problem.surface == SurfaceKind::Horizontal
            ? std::string("a horizontal surface uses sin = 0 and cos = 1")
            : "sin = " + rational_text(sine) + ", cos = " + rational_text(cosine) +
                  " satisfy sin^2 + cos^2 = 1 exactly";
    if (!add_check(derivation, meter, plan_id, "physics.forces.check-angle",
                   "Exact incline angle", "Check the incline angle is exactly representable",
                   "Resolving the weight needs exact trigonometric values",
                   "obl.forces.exact-angle", "the angle's sine and cosine are exact rationals",
                   "trigonometric identity", identity_detail, EvidenceStrength::StructurallyValid,
                   VerificationOutcome::Passed, "the incline angle is exactly representable",
                   "sin^2 + cos^2 = 1", identity_detail)) {
        return ForcesResult();
    }

    ForcesResult result;
    result.assumptions = *assumptions;

    // The weight, resolved onto the axes. Across the surface it presses in, so its across component
    // is negative; along the surface it pulls down the slope.
    const Exact weight = mul(exact(mass), exact(gravity));
    const Exact weight_along = mul(weight, exact(negated(sine)));
    const Exact weight_across = mul(weight, exact(negated(cosine)));
    if (!weight.ok || !weight_along.ok || !weight_across.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "resolving the weight exceeds exact arithmetic");
    }
    add_entry(&result, ForceKind::Weight, "W", "the Earth", weight_along.value,
              weight_across.value, weight.value, true);

    const NodeId weight_expression =
        arena.binary(Kind::Mul, rational_node(arena, mass), rational_node(arena, gravity));
    if (!add_transformation(derivation, meter, plan_id, "physics.forces.weight", "Weight",
                            "Resolve the weight onto the axes",
                            "The weight is m g downward, resolved by the incline angle",
                            "Every force on the body goes onto the inventory before any axis is "
                            "summed, and the weight is the one force that is always present. It "
                            "acts vertically, so on an incline it splits into a component down the "
                            "slope and a component pressing into the surface.",
                            "obl.forces.weight-components",
                            "weight is mass times gravity and its components are its exact incline projections",
                            verification("exact rational product",
                                         "W = " + newtons(weight.value) + " with components " +
                                             newtons(weight_along.value) + " along and " +
                                             newtons(weight_across.value) + " across",
                                         EvidenceStrength::DimensionallyValid,
                                         VerificationOutcome::Passed),
                            ClaimType::EquivalentExpression, weight_expression,
                            "W = " + rational_text(mass) + " * " + rational_text(gravity) + " = " +
                                newtons(weight.value),
                            rational_node(arena, weight.value))) {
        return ForcesResult();
    }

    // Across the surface nothing accelerates, so the normal force is whatever balances the other
    // across components. Its own equation comes first because friction is measured from it.
    Exact across_others = weight_across;
    const Exact normal = sub(exact(0), across_others);
    if (!normal.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "summing the across axis exceeds exact arithmetic");
    }
    if (normal.value.num < 0) {
        return failed(ForcesOutcome::UnsupportedArrangement, DerivationStatus::InvalidInput,
                      "the across-axis sum needs a negative normal force, so the body would leave "
                      "the surface and this arrangement is outside the envelope");
    }
    Rational zero;
    zero.num = 0;
    zero.den = 1;
    add_entry(&result, ForceKind::Normal, "N", problem.support, zero, normal.value, normal.value,
              !requested(problem, ForceKind::Normal));
    result.across_equation_text = "N + (" + newtons(weight_across.value) + ") = 0";
    result.across_equation = arena.binary(
        Kind::Equals,
        arena.binary(Kind::Add, arena.symbol("N"), rational_node(arena, weight_across.value)),
        arena.integer("0"));
    const NodeId isolated_normal =
        arena.binary(Kind::Equals, arena.symbol("N"), rational_node(arena, normal.value));
    if (arena.failed()) {
        return failed(ForcesOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }
    if (!add_transformation(derivation, meter, plan_id, "physics.forces.normal-force",
                            "Normal force from the across-axis sum",
                            "Sum the across axis and solve for the normal force",
                            "The body stays on the surface, so the across components sum to zero",
                            "The across axis is the one with no acceleration on it while the body "
                            "keeps contact. That makes its sum an equation with one unknown, and "
                            "the normal force falls out of it before friction needs it.",
                            "obl.forces.normal-from-balance",
                            "the normal force makes the exact across-axis sum zero",
                            verification("exact across-axis sum",
                                         "N = " + newtons(normal.value),
                                         EvidenceStrength::DimensionallyValid,
                                         VerificationOutcome::Passed),
                            ClaimType::SolutionSetPreserved, result.across_equation,
                            "N = " + newtons(normal.value) + " from " + result.across_equation_text,
                            isolated_normal)) {
        return ForcesResult();
    }

    // Newton's third law names a partner for each contact force, and that partner acts on the other
    // body. It never joins this inventory, which is exactly the distinction the pairs record keeps.
    InteractionPair contact;
    contact.kind = ForceKind::Normal;
    contact.on_body = problem.body;
    contact.by_body = problem.support;
    contact.reaction_on = problem.support;
    contact.reaction_by = problem.body;
    contact.magnitude = normal.value;
    contact.magnitude_text = newtons(normal.value);
    result.pairs.push_back(contact);
    InteractionPair gravitational;
    gravitational.kind = ForceKind::Weight;
    gravitational.on_body = problem.body;
    gravitational.by_body = "the Earth";
    gravitational.reaction_on = "the Earth";
    gravitational.reaction_by = problem.body;
    gravitational.magnitude = weight.value;
    gravitational.magnitude_text = newtons(weight.value);
    result.pairs.push_back(gravitational);
    if (!add_check(derivation, meter, plan_id, "physics.forces.third-law-pairs",
                   "Third-law interaction pairs", "Name each contact force's third-law partner",
                   "A third-law partner acts on the other body, so it never joins this sum",
                   "obl.forces.pairs-separate",
                   "no third-law reaction appears in the inventory of this body",
                   "inventory and pair comparison",
                   problem.body + " feels N and W while " + problem.support + " and the Earth feel "
                       "their equal and opposite partners",
                   EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
                   "the third-law partners are kept off this body's force sum",
                   "the pair list and the inventory are disjoint",
                   "two pairs recorded, neither summed on " + problem.body)) {
        return ForcesResult();
    }

    if (problem.has_applied)
        add_entry(&result, ForceKind::Applied, "F", "the applied push or pull", applied, zero,
                  absolute(applied), true);
    if (problem.has_tension)
        add_entry(&result, ForceKind::Tension, "T", "the string", tension, zero,
                  absolute(tension), true);

    // Everything on the along axis that is known before friction and before the unknown.
    Exact along_known = weight_along;
    if (problem.has_applied)
        along_known = add(along_known, exact(applied));
    if (problem.has_tension)
        along_known = add(along_known, exact(tension));
    const Exact required = mul(exact(mass), exact(acceleration));
    if (!along_known.ok || !required.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "summing the along axis exceeds exact arithmetic");
    }

    // Friction from the normal force. Kinetic friction opposes the declared motion and has a fixed
    // magnitude; static friction takes whatever value the balance needs, up to its maximum.
    const Exact maximum_static = mul(exact(coefficient), exact(normal.value));
    if (!maximum_static.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "evaluating the friction limit exceeds exact arithmetic");
    }
    Rational friction;
    friction.num = 0;
    friction.den = 1;
    if (problem.friction == FrictionModel::Kinetic) {
        friction = problem.motion == MotionSense::UpTheAxis ? negated(maximum_static.value)
                                                            : maximum_static.value;
        NodeId friction_expression = arena.binary(Kind::Mul, rational_node(arena, coefficient),
                                                  rational_node(arena, normal.value));
        if (problem.motion == MotionSense::UpTheAxis)
            friction_expression = arena.unary(Kind::Neg, friction_expression);
        if (arena.failed()) {
            return failed(ForcesOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                          status_name(arena.status()));
        }
        if (!add_transformation(
                derivation, meter, plan_id, "physics.forces.kinetic-friction", "Kinetic friction",
                "Evaluate the kinetic friction from the normal force",
                "Sliding friction is mu_k N against the motion",
                "Kinetic friction has a value the surfaces fix rather than one the balance "
                "chooses, and it points against the sliding. That is the whole difference from "
                "the static case below, where the friction is whatever the equilibrium needs.",
                "obl.forces.kinetic-friction",
                "kinetic friction has magnitude mu_k N and points opposite the declared motion",
                verification("exact rational product",
                             "f = " + newtons(friction) + " opposing motion " +
                                 motion_sense_name(problem.motion),
                             EvidenceStrength::DimensionallyValid, VerificationOutcome::Passed),
                ClaimType::EquivalentExpression, friction_expression,
                "f = mu_k N = " + newtons(friction), rational_node(arena, friction))) {
            return ForcesResult();
        }
        add_entry(&result, ForceKind::Friction, "f", problem.support, friction, zero,
                  absolute(friction), !requested(problem, ForceKind::Friction));
    } else if (problem.friction == FrictionModel::Static) {
        // Equilibrium along the axis needs the friction to cancel everything else on it.
        const Exact needed = sub(exact(0), along_known);
        if (!needed.ok) {
            return failed(ForcesOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "evaluating the required friction exceeds exact arithmetic");
        }
        friction = needed.value;
        result.static_checked = true;
        result.required_friction = friction;
        result.maximum_static_friction = maximum_static.value;
        int ordering = 0;
        if (!compare(absolute(friction), maximum_static.value, &ordering)) {
            return failed(ForcesOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "comparing the required friction against its limit exceeds exact "
                          "arithmetic");
        }
        const bool holds = ordering <= 0;
        const std::string observed = "the balance needs " + newtons(absolute(friction)) +
                                     " against a maximum of " + newtons(maximum_static.value);
        if (!add_check(derivation, meter, plan_id, "physics.forces.static-friction-limit",
                       "Static friction against its limit",
                       "Compare the friction equilibrium needs with the most static friction "
                       "available",
                       "Static friction takes the value the balance needs, up to mu_s N",
                       "obl.forces.static-within-limit",
                       "the friction equilibrium requires does not exceed mu_s N",
                       "exact comparison of required friction against mu_s N", observed,
                       EvidenceStrength::DimensionallyValid,
                       holds ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                       "the assumed equilibrium is consistent with static friction",
                       "the required friction is at most mu_s N", observed)) {
            return ForcesResult();
        }
        if (!holds) {
            // The assumption was tested rather than taken on trust, and it failed, so no value is
            // offered. Reporting the required friction anyway would be reporting a force the
            // surface cannot supply.
            ForcesResult impossible =
                failed(ForcesOutcome::EquilibriumImpossible, DerivationStatus::VerificationFailed,
                       observed + ", so the body cannot stay in equilibrium and slides instead");
            impossible.inventory = result.inventory;
            impossible.pairs = result.pairs;
            impossible.assumptions = result.assumptions;
            impossible.static_checked = true;
            impossible.required_friction = friction;
            impossible.maximum_static_friction = maximum_static.value;
            impossible.consistency = "the assumed equilibrium is inconsistent with mu_s N";
            return impossible;
        }
        result.consistency = "the assumed equilibrium is consistent with mu_s N";
        add_entry(&result, ForceKind::Friction, "f", problem.support, friction, zero,
                  absolute(friction), !requested(problem, ForceKind::Friction));
    }

    Exact along_total = along_known;
    if (problem.friction != FrictionModel::None)
        along_total = add(along_total, exact(friction));
    if (!along_total.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "summing the along axis exceeds exact arithmetic");
    }

    result.along_equation_text =
        rational_text(along_total.value) + " N = " + rational_text(mass) + " kg * a";
    result.along_equation = arena.binary(
        Kind::Equals, rational_node(arena, along_total.value),
        arena.binary(Kind::Mul, rational_node(arena, mass), arena.symbol("a")));
    if (arena.failed()) {
        return failed(ForcesOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }

    // The requested unknown is the one symbol left in a linear equation, so solving is one
    // rearrangement rather than a search.
    Exact answer;
    Exact isolated_answer;
    std::string unit_text = "N";
    NodeId solve_equation = kNoNode;
    const char *solve_symbol = "";
    switch (problem.unknown) {
        case ForcesUnknown::Acceleration: {
            Exact quotient;
            quotient.ok = rational_div(along_total.value, mass, &quotient.value);
            answer = quotient;
            isolated_answer = answer;
            unit_text = "m/s^2";
            solve_equation = result.along_equation;
            solve_symbol = "a";
            break;
        }
        case ForcesUnknown::AppliedForce:
            answer = sub(required, along_total);
            isolated_answer = answer;
            solve_equation = arena.binary(
                Kind::Equals,
                arena.binary(Kind::Add, rational_node(arena, along_total.value), arena.symbol("F")),
                rational_node(arena, required.value));
            solve_symbol = "F";
            break;
        case ForcesUnknown::NormalForce:
            answer = normal;
            isolated_answer = answer;
            solve_equation = result.across_equation;
            solve_symbol = "N";
            break;
        case ForcesUnknown::FrictionForce:
            if (problem.friction == FrictionModel::None) {
                return failed(ForcesOutcome::InconsistentInput, DerivationStatus::InvalidInput,
                              "a frictionless surface has no friction force to solve for");
            }
            answer = exact(friction);
            isolated_answer = sub(required, along_known);
            solve_equation = arena.binary(
                Kind::Equals,
                arena.binary(Kind::Add, rational_node(arena, along_known.value), arena.symbol("f")),
                rational_node(arena, required.value));
            solve_symbol = "f";
            break;
    }
    if (!answer.ok || !isolated_answer.ok) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "solving for the requested unknown exceeds exact arithmetic");
    }
    const NodeId isolated_unknown =
        arena.binary(Kind::Equals, arena.symbol(solve_symbol),
                     rational_node(arena, isolated_answer.value));
    if (arena.failed()) {
        return failed(ForcesOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));
    }

    if (!add_transformation(
            derivation, meter, plan_id, "physics.forces.solve-unknown",
            "Solve the equation of motion",
            "Solve the force-balance equation for the " +
                std::string(forces_unknown_name(problem.unknown)),
            "The unknown appears linearly, so one rearrangement isolates it",
            "The force balance has one unknown. Isolating it is ordinary rearrangement, and the "
            "residual check below puts the answer back into the same sum.",
            "obl.forces.unknown-isolated",
            "exact rearrangement isolates the requested unknown from its force-balance equation",
            verification("exact rearrangement",
                         std::string(forces_unknown_name(problem.unknown)) + " = " +
                             rational_text(isolated_answer.value) + " " + unit_text,
                         EvidenceStrength::DimensionallyValid, VerificationOutcome::Passed),
            ClaimType::SolutionSetPreserved, solve_equation,
            "Isolate the " + std::string(forces_unknown_name(problem.unknown)) + " to get " +
                rational_text(isolated_answer.value) + " " + unit_text,
            isolated_unknown)) {
        return ForcesResult();
    }

    // The force the request asked for is missing from the inventory until it has a value, so it
    // joins the given ones here and the residual below is then rebuilt from a complete record.
    if (problem.unknown == ForcesUnknown::AppliedForce) {
        add_entry(&result, ForceKind::Applied, "F", "the applied push or pull", answer.value, zero,
                  absolute(answer.value), false);
    }

    // Put the answer back into the axis sum it came from. A residual that is not exactly zero means
    // the inventory and the answer disagree, and then no value is offered.
    // Rebuilt from the published inventory rather than from the running total the answer came out
    // of, so an entry that disagrees with the sum shows up here instead of cancelling itself.
    // Neither branch can overflow: required was checked and the product rebuilds the along total.
    Exact target;
    if (problem.unknown == ForcesUnknown::Acceleration) {
        target = mul(exact(mass), answer);
    } else {
        target = required;
    }
    const ForceBalance balance = forces_along_balance(result.inventory, target.value);
    if (!balance.exact) {
        return failed(ForcesOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "evaluating the force-balance residual exceeds exact arithmetic");
    }
    const bool balanced = balance.balanced;
    const std::string residual_detail =
        "the along-axis residual is " + rational_text(balance.residual) + " N";
    if (!add_check(derivation, meter, plan_id, "physics.forces.check-residual",
                   "Force-balance residual", "Substitute the answer back into the axis sum",
                   "A correct answer leaves the along-axis sum exactly equal to m a",
                   "obl.forces.residual-zero",
                   "the along-axis force sum minus m a is exactly zero",
                   "exact substitution into the along-axis sum", residual_detail,
                   EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
                   balanced ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                   "the reported answer satisfies the equation it came from", "a zero residual",
                   residual_detail)) {
        return ForcesResult();
    }
    if (!balanced) {
        return failed(ForcesOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      residual_detail + ", so no value is offered");
    }

    const Dimension answer_dimension = problem.unknown == ForcesUnknown::Acceleration
                                           ? acceleration_dimension()
                                           : force_dimension();
    if (!add_check(derivation, meter, plan_id, "physics.forces.check-result-dimension",
                   "Result dimension", "Check the answer's dimension",
                   "A force sum divided by a mass is an acceleration",
                   "obl.forces.result-dimension",
                   "the reported answer has the dimension its unit claims",
                   "dimensional analysis",
                   std::string(forces_unknown_name(problem.unknown)) + " has dimension " +
                       dimension_text(answer_dimension),
                   EvidenceStrength::DimensionallyValid, VerificationOutcome::Passed,
                   "the answer's unit matches its dimension",
                   unit_text + " is the SI unit of " + dimension_text(answer_dimension),
                   si_unit_text(answer_dimension))) {
        return ForcesResult();
    }

    result.outcome = ForcesOutcome::Solved;
    result.has_value = true;
    result.value = answer.value;
    result.value_text = rational_text(answer.value);
    result.unit_text = unit_text;
    result.model = *model;
    return result;
}

}

ForceBalance forces_along_balance(const std::vector<ForceEntry> &inventory, const Rational &target) {
    Exact along = exact(0);
    for (const ForceEntry &entry : inventory)
        along = add(along, exact(entry.along));
    const Exact residual = sub(along, exact(target));
    ForceBalance balance;
    balance.exact = residual.ok;
    if (!balance.exact)
        return balance;
    balance.residual = residual.value;
    balance.balanced = residual.value.num == 0;
    return balance;
}

const char *surface_kind_name(SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::Horizontal: return "horizontal";
        case SurfaceKind::Incline: return "incline";
    }
    return "invalid";
}

const char *friction_model_name(FrictionModel model) {
    switch (model) {
        case FrictionModel::None: return "frictionless";
        case FrictionModel::Static: return "static";
        case FrictionModel::Kinetic: return "kinetic";
    }
    return "invalid";
}

const char *motion_sense_name(MotionSense sense) {
    switch (sense) {
        case MotionSense::Undeclared: return "undeclared";
        case MotionSense::UpTheAxis: return "up the axis";
        case MotionSense::DownTheAxis: return "down the axis";
    }
    return "invalid";
}

const char *force_kind_name(ForceKind kind) {
    switch (kind) {
        case ForceKind::Weight: return "weight";
        case ForceKind::Normal: return "normal";
        case ForceKind::Applied: return "applied";
        case ForceKind::Tension: return "tension";
        case ForceKind::Friction: return "friction";
    }
    return "invalid";
}

const char *forces_unknown_name(ForcesUnknown unknown) {
    switch (unknown) {
        case ForcesUnknown::Acceleration: return "acceleration";
        case ForcesUnknown::AppliedForce: return "applied force";
        case ForcesUnknown::NormalForce: return "normal force";
        case ForcesUnknown::FrictionForce: return "friction force";
    }
    return "invalid";
}

const char *forces_outcome_name(ForcesOutcome outcome) {
    switch (outcome) {
        case ForcesOutcome::Solved: return "solved";
        case ForcesOutcome::InvalidProblem: return "invalid problem";
        case ForcesOutcome::UnsupportedArrangement: return "unsupported arrangement";
        case ForcesOutcome::InclineAngleNotExact: return "incline angle not exact";
        case ForcesOutcome::DimensionMismatch: return "dimension mismatch";
        case ForcesOutcome::MotionSenseUndeclared: return "motion sense undeclared";
        case ForcesOutcome::EquilibriumImpossible: return "equilibrium impossible";
        case ForcesOutcome::Underdetermined: return "underdetermined";
        case ForcesOutcome::InconsistentInput: return "inconsistent input";
        case ForcesOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case ForcesOutcome::VerificationFailed: return "verification failed";
        case ForcesOutcome::Cancelled: return "cancelled";
        case ForcesOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

ForcesResult solve_forces(Arena &arena, Derivation &derivation, const ForcesProblem &problem,
                          const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    std::vector<std::string> assumptions;
    ForcesResult result = solve_body(arena, derivation, meter, problem, &model, &assumptions);

    const bool halted = meter.stopped() || result.outcome == ForcesOutcome::Cancelled ||
                        result.outcome == ForcesOutcome::ResourceExceeded;
    if (halted) {
        derivation.rewind_to(mark);
        const bool cancelled =
            result.outcome == ForcesOutcome::Cancelled || meter.halt() == Halt::Cancelled;
        ForcesResult stopped;
        stopped.outcome = cancelled ? ForcesOutcome::Cancelled : ForcesOutcome::ResourceExceeded;
        stopped.detail = meter.stopped() ? halt_name(meter.halt()) : result.detail;
        stopped.status = cancelled ? DerivationStatus::NotRecorded
                                   : DerivationStatus::ResourceLimitReached;
        stopped.cost = meter.cost();
        record_context(derivation, budget, model, stopped.status, problem, assumptions);
        return stopped;
    }

    if (result.outcome == ForcesOutcome::Solved)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, model, result.status, problem, assumptions);
    return result;
}

}
