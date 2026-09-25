#include "nps/physics/relativity.h"

#include <utility>

#include "nps/core/context.h"
#include "nps/core/rational.h"
#include "measurement_support.h"

namespace nps {
namespace {

const size_t kMaxVariables = 3;

// The SI definition of the metre fixes this exactly, which is what lets a Lorentz transformation
// written in metres and seconds stay inside exact rational arithmetic.
const int64_t kLightSpeed = 299792458;

struct RelationShape {
    const char *family_id;
    const char *written;
    RelativityVariable reads[kMaxVariables];
    size_t read_count;
    RelativityVariable reports[kMaxVariables];
    size_t report_count;
    const char *assumption;
    bool needs_factor;
};

const RelationShape &shape_of(RelativityRelation relation) {
    static const RelationShape dilation = {
        "physics.relativity.time-dilation", "dt = gamma * dt0",
        {RelativityVariable::ProperTime}, 1,
        {RelativityVariable::DilatedTime}, 1,
        "the two events happen at the same place in the moving frame, so its reading is the proper "
        "time",
        true};
    static const RelationShape contraction = {
        "physics.relativity.length-contraction", "L = L0 / gamma",
        {RelativityVariable::ProperLength}, 1,
        {RelativityVariable::ContractedLength}, 1,
        "the rod lies along the direction of motion and is at rest in the moving frame, so its "
        "length there is the proper length",
        true};
    static const RelationShape lorentz = {
        "physics.relativity.lorentz-transformation", "x' = gamma (x - beta c t), c t' = gamma (c t - beta x)",
        {RelativityVariable::EventPosition, RelativityVariable::EventTime}, 2,
        {RelativityVariable::TransformedPosition, RelativityVariable::TransformedTime}, 2,
        "the axes are parallel and the origins coincide at t = t' = 0, so the transformation carries "
        "no offset",
        true};
    static const RelationShape addition = {
        "physics.relativity.velocity-addition", "beta = (beta' + beta_boost) / (1 + beta' beta_boost)",
        {RelativityVariable::ObjectVelocity}, 1,
        {RelativityVariable::TransformedVelocity}, 1,
        "the object moves along the same shared x axis the boost points along",
        false};
    static const RelationShape energy = {
        "physics.relativity.energy-momentum", "E = gamma E0, pc = gamma beta E0, K = E - E0",
        {RelativityVariable::RestEnergy}, 1,
        {RelativityVariable::TotalEnergy, RelativityVariable::MomentumEnergy,
         RelativityVariable::KineticEnergy},
        3,
        "the particle is at rest in the moving frame, so it travels at the boost velocity in the "
        "rest frame",
        true};
    switch (relation) {
        case RelativityRelation::TimeDilation: return dilation;
        case RelativityRelation::LengthContraction: return contraction;
        case RelativityRelation::LorentzTransformation: return lorentz;
        case RelativityRelation::VelocityAddition: return addition;
        case RelativityRelation::EnergyMomentum: return energy;
    }
    return dilation;
}

Dimension time_dimension_local() {
    Dimension d;
    d.time = 1;
    return d;
}

Dimension length_dimension_local() {
    Dimension d;
    d.length = 1;
    return d;
}

Dimension energy_dimension_local() {
    Dimension d;
    d.length = 2;
    d.mass = 1;
    d.time = -2;
    return d;
}

RelativityResult failed(RelativityOutcome outcome, DerivationStatus status,
                        const std::string &detail) {
    RelativityResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

using measure::normalize_copy;
using measure::normalized_rational_node;
using measure::verification;

bool positive(const Rational &value) {
    Rational normalized;
    if (!normalize_copy(value, &normalized))
        return false;
    return normalized.num > 0;
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

Unit family_unit(RelativityVariable variable) {
    Unit unit;
    unit.dimension = relativity_variable_dimension(variable);
    unit.text = relativity_variable_unit(variable);
    unit.scale.num = 1;
    unit.scale.den = 1;
    return unit;
}

Frame frame_of(const RelativityProblem &problem, RelativityVariable variable) {
    return relativity_variable_in_moving_frame(variable) ? problem.moving_frame
                                                         : problem.rest_frame;
}

std::string convention_text(const RelativityProblem &problem, const Rational &beta) {
    Rational normalized;
    normalize_copy(beta, &normalized);
    const std::string direction = normalized.num < 0 ? "negative" : "positive";
    return problem.moving_frame.name + " moves at beta = " + rational_text(normalized) +
           " c along the " + direction + " x axis of " + problem.rest_frame.name +
           ", with parallel axes and origins coinciding at t = 0";
}

std::string known_text(const RelativityKnown &known) {
    return std::string(relativity_variable_name(known.variable)) + " = " +
           rational_text(known.quantity.value) + " " +
           relativity_variable_unit(known.variable);
}

void record_context(Derivation &derivation, const Budget &budget, const RelationShape &shape,
                    const RelativityProblem &problem, const Rational &beta, NodeId model,
                    DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = shape.family_id;
    inputs.requested_method =
        std::string(shape.written) + ", exact rational substitution between two named frames";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.branch_convention = "real domain";
    inputs.active_assumptions.push_back(shape.assumption);
    inputs.active_assumptions.push_back(convention_text(problem, beta));
    inputs.active_assumptions.push_back(
        "both frames are inertial, so no acceleration enters the transformation");
    inputs.unit_policy =
        "read velocities as fractions of c, times in seconds, lengths in metres and energies in "
        "MeV, and round only the reported answer";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

int read_index(const RelationShape &shape, RelativityVariable variable) {
    for (size_t i = 0; i < shape.read_count; ++i) {
        if (shape.reads[i] == variable)
            return static_cast<int>(i);
    }
    return -1;
}

// The relation written over its own symbols, so the model and the substituted copy share a shape.
// Relations that report more than one value record one equation per reported value, in a list.
NodeId build_relation(Arena &arena, RelativityRelation relation, const NodeId *reads,
                      const NodeId *reports, NodeId beta, NodeId gamma, NodeId light) {
    switch (relation) {
        case RelativityRelation::TimeDilation:
            return arena.binary(Kind::Equals, reports[0], arena.binary(Kind::Mul, gamma, reads[0]));
        case RelativityRelation::LengthContraction:
            return arena.binary(
                Kind::Equals, arena.binary(Kind::Mul, reports[0], gamma), reads[0]);
        case RelativityRelation::LorentzTransformation: {
            // c carries the event time into a length, which is what makes the two halves of the
            // transformation the same equation written twice.
            const NodeId event_light_time = arena.binary(Kind::Mul, light, reads[1]);
            const NodeId transformed_light_time = arena.binary(Kind::Mul, light, reports[1]);
            const NodeId position = arena.binary(
                Kind::Equals, reports[0],
                arena.binary(Kind::Mul, gamma,
                             arena.binary(Kind::Add, reads[0],
                                          arena.unary(Kind::Neg,
                                                      arena.binary(Kind::Mul, beta,
                                                                   event_light_time)))));
            const NodeId time = arena.binary(
                Kind::Equals, transformed_light_time,
                arena.binary(Kind::Mul, gamma,
                             arena.binary(Kind::Add, event_light_time,
                                          arena.unary(Kind::Neg,
                                                      arena.binary(Kind::Mul, beta, reads[0])))));
            return arena.list({position, time});
        }
        case RelativityRelation::VelocityAddition:
            return arena.binary(
                Kind::Equals,
                arena.binary(Kind::Mul, reports[0],
                             arena.binary(Kind::Add, arena.integer("1"),
                                          arena.binary(Kind::Mul, reads[0], beta))),
                arena.binary(Kind::Add, reads[0], beta));
        case RelativityRelation::EnergyMomentum: {
            const NodeId total =
                arena.binary(Kind::Equals, reports[0], arena.binary(Kind::Mul, gamma, reads[0]));
            const NodeId momentum = arena.binary(
                Kind::Equals, reports[1],
                arena.binary(Kind::Mul, arena.binary(Kind::Mul, gamma, beta), reads[0]));
            const NodeId kinetic = arena.binary(
                Kind::Equals, reports[2],
                arena.binary(Kind::Add, reports[0], arena.unary(Kind::Neg, reads[0])));
            return arena.list({total, momentum, kinetic});
        }
    }
    return kNoNode;
}

struct Computed {
    Rational values[kMaxVariables];
    bool ok = false;
    std::string detail;
};

// Every relation's arithmetic, in exact rationals. The Lorentz transformation converts the event
// time to the distance light travels in it, which keeps c out of the squared invariant.
Computed apply_relation(RelativityRelation relation, const Rational &beta, const Rational &gamma,
                        const Rational &inverse_gamma, const Rational *reads) {
    Computed out;
    const Rational one{1, 1};
    const Rational light{kLightSpeed, 1};
    switch (relation) {
        case RelativityRelation::TimeDilation:
            out.ok = rational_mul(gamma, reads[0], &out.values[0]);
            break;
        case RelativityRelation::LengthContraction:
            out.ok = rational_mul(inverse_gamma, reads[0], &out.values[0]);
            break;
        case RelativityRelation::LorentzTransformation: {
            Rational light_time;
            Rational shifted_position;
            Rational shifted_time;
            Rational beta_time;
            Rational beta_position;
            Rational transformed_light_time;
            out.ok = rational_mul(light, reads[1], &light_time) &&
                     rational_mul(beta, light_time, &beta_time) &&
                     rational_sub(reads[0], beta_time, &shifted_position) &&
                     rational_mul(gamma, shifted_position, &out.values[0]) &&
                     rational_mul(beta, reads[0], &beta_position) &&
                     rational_sub(light_time, beta_position, &shifted_time) &&
                     rational_mul(gamma, shifted_time, &transformed_light_time) &&
                     rational_div(transformed_light_time, light, &out.values[1]);
            break;
        }
        case RelativityRelation::VelocityAddition: {
            Rational numerator;
            Rational product;
            Rational denominator;
            out.ok = rational_add(reads[0], beta, &numerator) &&
                     rational_mul(reads[0], beta, &product) &&
                     rational_add(one, product, &denominator);
            if (out.ok && denominator.num == 0) {
                out.ok = false;
                out.detail = "the velocity-addition denominator is zero";
                return out;
            }
            out.ok = out.ok && rational_div(numerator, denominator, &out.values[0]);
            break;
        }
        case RelativityRelation::EnergyMomentum: {
            Rational factor_beta;
            out.ok = rational_mul(gamma, reads[0], &out.values[0]) &&
                     rational_mul(gamma, beta, &factor_beta) &&
                     rational_mul(factor_beta, reads[0], &out.values[1]) &&
                     rational_sub(out.values[0], reads[0], &out.values[2]);
            break;
        }
    }
    if (!out.ok && out.detail.empty())
        out.detail = "applying the relation exceeds exact integer arithmetic";
    return out;
}

struct Invariant {
    bool computed = false;
    bool holds = false;
    std::string claim;
    std::string observed;
};

// The final check is a conserved quantity rather than a restatement of the step that produced the
// answer, so a wrong transformation shows up here instead of agreeing with itself.
Invariant check_invariant(RelativityRelation relation, const Rational &beta, const Rational *reads,
                          const Rational *reports) {
    Invariant out;
    const Rational one{1, 1};
    const Rational light{kLightSpeed, 1};
    Rational beta_square;
    if (!rational_mul(beta, beta, &beta_square))
        return out;
    Rational one_minus;
    if (!rational_sub(one, beta_square, &one_minus))
        return out;
    switch (relation) {
        case RelativityRelation::TimeDilation: {
            Rational dilated_square;
            Rational scaled;
            Rational proper_square;
            if (!rational_mul(reports[0], reports[0], &dilated_square) ||
                !rational_mul(dilated_square, one_minus, &scaled) ||
                !rational_mul(reads[0], reads[0], &proper_square))
                return out;
            out.computed = true;
            out.holds = rational_equal(scaled, proper_square);
            out.claim = "dt^2 (1 - beta^2) = dt0^2, so the moving clock reads the shorter interval";
            out.observed = rational_text(scaled) + " against " + rational_text(proper_square);
            return out;
        }
        case RelativityRelation::LengthContraction: {
            Rational contracted_square;
            Rational proper_square;
            Rational scaled;
            if (!rational_mul(reports[0], reports[0], &contracted_square) ||
                !rational_mul(reads[0], reads[0], &proper_square) ||
                !rational_mul(proper_square, one_minus, &scaled))
                return out;
            out.computed = true;
            out.holds = rational_equal(contracted_square, scaled);
            out.claim = "L^2 = L0^2 (1 - beta^2), so the measured rod is the shorter one";
            out.observed = rational_text(contracted_square) + " against " + rational_text(scaled);
            return out;
        }
        case RelativityRelation::LorentzTransformation: {
            Rational light_time;
            Rational transformed_light_time;
            Rational before;
            Rational after;
            Rational light_square;
            Rational position_square;
            Rational transformed_light_square;
            Rational transformed_position_square;
            if (!rational_mul(light, reads[1], &light_time) ||
                !rational_mul(light, reports[1], &transformed_light_time) ||
                !rational_mul(light_time, light_time, &light_square) ||
                !rational_mul(reads[0], reads[0], &position_square) ||
                !rational_sub(light_square, position_square, &before) ||
                !rational_mul(transformed_light_time, transformed_light_time,
                              &transformed_light_square) ||
                !rational_mul(reports[0], reports[0], &transformed_position_square) ||
                !rational_sub(transformed_light_square, transformed_position_square, &after))
                return out;
            out.computed = true;
            out.holds = rational_equal(before, after);
            out.claim = "(c t)^2 - x^2 is the same in both frames";
            out.observed = rational_text(before) + " against " + rational_text(after);
            return out;
        }
        case RelativityRelation::VelocityAddition: {
            Rational numerator;
            Rational product;
            Rational denominator;
            Rational recovered;
            if (!rational_sub(reports[0], beta, &numerator) ||
                !rational_mul(reports[0], beta, &product) ||
                !rational_sub(one, product, &denominator) || denominator.num == 0 ||
                !rational_div(numerator, denominator, &recovered))
                return out;
            out.computed = true;
            out.holds = rational_equal(recovered, reads[0]);
            out.claim = "the inverse boost carries the answer back to the given velocity";
            out.observed = rational_text(recovered) + " against " + rational_text(reads[0]);
            return out;
        }
        case RelativityRelation::EnergyMomentum: {
            Rational energy_square;
            Rational momentum_square;
            Rational difference;
            Rational rest_square;
            if (!rational_mul(reports[0], reports[0], &energy_square) ||
                !rational_mul(reports[1], reports[1], &momentum_square) ||
                !rational_sub(energy_square, momentum_square, &difference) ||
                !rational_mul(reads[0], reads[0], &rest_square))
                return out;
            out.computed = true;
            out.holds = rational_equal(difference, rest_square);
            out.claim = "E^2 - (pc)^2 = E0^2, the invariant the rest energy names";
            out.observed = rational_text(difference) + " against " + rational_text(rest_square);
            return out;
        }
    }
    return out;
}

RelativityResult solve_body(Arena &arena, Derivation &derivation, Meter &meter,
                            const RelativityProblem &problem, const Budget &budget,
                            const RelationShape &shape, Rational *beta_out, NodeId *model) {
    if (problem.rest_frame.name.empty() || problem.moving_frame.name.empty())
        return failed(RelativityOutcome::FrameUndeclared, DerivationStatus::InvalidInput,
                      "both the rest frame and the moving frame need a name");
    const bool frames_distinct = problem.rest_frame != problem.moving_frame;
    if (!frames_distinct)
        return failed(RelativityOutcome::FrameMismatch, DerivationStatus::InvalidInput,
                      problem.rest_frame.name + " cannot be both frames of the same boost");

    std::string invalid_detail;
    if (!valid_quantity(problem.boost, &invalid_detail))
        return failed(RelativityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the boost velocity: " + invalid_detail);
    if (problem.boost.unit.dimension != Dimension() || problem.boost.unit.text != "c")
        return failed(RelativityOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                      "the boost velocity is read as a dimensionless fraction of c, not as " +
                          problem.boost.unit.text);
    Rational beta;
    if (!normalize_copy(problem.boost.value, &beta))
        return failed(RelativityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "the boost velocity is not an exact fraction");
    *beta_out = beta;

    const RelativityKnown *knowns[kMaxVariables] = {nullptr, nullptr, nullptr};
    for (const RelativityKnown &known : problem.knowns) {
        const int index = read_index(shape, known.variable);
        if (index < 0)
            return failed(RelativityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(relativity_variable_name(known.variable)) +
                              " is not a given of " + shape.written);
        if (knowns[index])
            return failed(RelativityOutcome::DuplicateKnown, DerivationStatus::InvalidInput,
                          std::string(relativity_variable_name(known.variable)) +
                              " is given twice");
        if (!valid_quantity(known.quantity, &invalid_detail))
            return failed(RelativityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(relativity_variable_name(known.variable)) + ": " +
                              invalid_detail);
        const Dimension expected = relativity_variable_dimension(known.variable);
        if (known.quantity.unit.dimension != expected)
            return failed(RelativityOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " has dimension " +
                              dimension_text(known.quantity.unit.dimension) + ", but " +
                              relativity_variable_name(known.variable) + " requires " +
                              dimension_text(expected));
        // Nothing converts here, so a given written in another unit of the same dimension would be
        // substituted unchanged and silently wrong.
        if (known.quantity.unit.text != relativity_variable_unit(known.variable))
            return failed(RelativityOutcome::DimensionMismatch, DerivationStatus::InvalidInput,
                          known_text(known) + " was given in " + known.quantity.unit.text +
                              ", but this family reads " +
                              relativity_variable_name(known.variable) + " in " +
                              relativity_variable_unit(known.variable));
        knowns[index] = &known;
    }
    for (size_t index = 0; index < shape.read_count; ++index) {
        if (!knowns[index])
            return failed(RelativityOutcome::MissingKnown, DerivationStatus::InvalidInput,
                          std::string("missing known ") +
                              relativity_variable_name(shape.reads[index]));
    }

    // A proper interval and a rest energy are positive by definition. An event coordinate is not,
    // so the domain rule is asked per variable rather than of every given.
    for (size_t index = 0; index < shape.read_count; ++index) {
        const RelativityVariable variable = shape.reads[index];
        const bool must_be_positive = variable == RelativityVariable::ProperTime ||
                                      variable == RelativityVariable::ProperLength ||
                                      variable == RelativityVariable::RestEnergy;
        if (must_be_positive && !positive(knowns[index]->quantity.value))
            return failed(RelativityOutcome::UnphysicalValue, DerivationStatus::InvalidInput,
                          known_text(*knowns[index]) + " is not positive");
    }
    if (shape.reads[0] == RelativityVariable::ObjectVelocity) {
        Rational object;
        Rational magnitude;
        const Rational zero;
        if (!normalize_copy(knowns[0]->quantity.value, &object) ||
            !rational_sub(zero, object, &magnitude))
            return failed(RelativityOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "the given object velocity does not fit exact integer arithmetic");
        const Rational &compared = object.num < 0 ? magnitude : object;
        if (!(compared.num < compared.den))
            return failed(RelativityOutcome::SuperluminalSpeed, DerivationStatus::InvalidInput,
                          known_text(*knowns[0]) +
                              " is at or above the speed of light in its own frame");
    }

    NodeId read_symbols[kMaxVariables] = {kNoNode, kNoNode, kNoNode};
    NodeId report_symbols[kMaxVariables] = {kNoNode, kNoNode, kNoNode};
    for (size_t index = 0; index < shape.read_count; ++index)
        read_symbols[index] = arena.symbol(relativity_variable_symbol(shape.reads[index]));
    for (size_t index = 0; index < shape.report_count; ++index)
        report_symbols[index] = arena.symbol(relativity_variable_symbol(shape.reports[index]));
    const NodeId beta_symbol = arena.symbol("beta");
    const NodeId gamma_symbol = arena.symbol("gamma");
    const NodeId equation = build_relation(arena, problem.relation, read_symbols, report_symbols,
                                           beta_symbol, gamma_symbol, arena.symbol("c"));
    *model = equation;
    if (arena.failed())
        return failed(RelativityOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    PlanPayload plan;
    plan.strategy_id = "physics.relativity.plan";
    plan.selected_strategy = std::string("Apply ") + shape.written;
    for (const RelativityKnown &known : problem.knowns)
        plan.matched_problem_facts.push_back(known_text(known));
    plan.matched_problem_facts.push_back("boost beta = " + rational_text(beta) + " c of " +
                                         problem.moving_frame.name + " relative to " +
                                         problem.rest_frame.name);
    plan.alternatives_considered.push_back("a Galilean relative-motion model");
    plan.selection_rationale =
        std::string(shape.written) +
        " is exact in the declared fraction-of-c units, and the Galilean model it replaces is wrong "
        "at this speed rather than merely imprecise";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Relate ") + problem.moving_frame.name + " and " +
                     problem.rest_frame.name + " through " + shape.written;
    plan_step.rule_id = "physics.relativity.plan";
    plan_step.rule_name = relativity_relation_name(problem.relation);
    plan_step.explanation_short =
        std::string("Use ") + shape.written + " between the two declared inertial frames";
    plan_step.explanation_detailed = convention_text(problem, beta);
    plan_step.claim = ClaimType::NoClaim;
    register_strategy_precondition(plan, plan_step, "pre.relativity.frames-named",
                                   "the two frames are named and distinct", "frame declaration",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked against the declared frame names");
    register_strategy_precondition(plan, plan_step, "pre.relativity.subluminal-boost",
                                   "the boost speed is below the speed of light",
                                   "exact comparison against the speed of light",
                                   EvidenceStrength::CandidateChecked,
                                   VerificationOutcome::NotAttempted,
                                   "checked against beta");
    if (!meter.step())
        return RelativityResult();
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const std::string frames_observed = problem.moving_frame.name + " relative to " +
                                        problem.rest_frame.name;
    if (!meter.step())
        return RelativityResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check that both frames are named";
        step.rule_id = "physics.relativity.check-frames";
        step.rule_name = "Frame declaration";
        step.explanation_short =
            "A relativistic quantity is only defined once the frame reading it is named";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back(
            {"obl.relativity.frames-distinct", "the answer names two distinct inertial frames"});
        step.verifications.push_back(verification("frame declaration", frames_observed,
                                                  EvidenceStrength::StructurallyValid,
                                                  VerificationOutcome::Passed));
        CheckPayload check;
        check.target_claim = "the boost names two distinct inertial frames";
        check.check_method = "compare the declared frame names";
        check.expected_relation = "two nonempty and different names";
        check.observed_result = frames_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(plan_id, "pre.relativity.frames-named",
                                          VerificationOutcome::Passed, frames_observed);

    Rational beta_magnitude;
    const Rational zero;
    if (!rational_sub(zero, beta, &beta_magnitude))
        return failed(RelativityOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "negating the boost exceeds exact integer arithmetic");
    const Rational &speed = beta.num < 0 ? beta_magnitude : beta;
    const bool subluminal = speed.num < speed.den;
    const std::string speed_observed =
        "|beta| = " + rational_text(speed) + " against the light speed 1";
    if (!meter.step())
        return RelativityResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check that the boost is below the speed of light";
        step.rule_id = "physics.relativity.check-boost";
        step.rule_name = "Speed limit";
        step.explanation_short = "No massive frame reaches or passes c, so |beta| stays below 1";
        step.claim = ClaimType::Definition;
        step.proof_obligations.push_back({"obl.relativity.speed-below-light",
                                          "the boost speed is strictly below the speed of light"});
        step.verifications.push_back(
            verification("exact comparison against the speed of light", speed_observed,
                         EvidenceStrength::CandidateChecked,
                         subluminal ? VerificationOutcome::Passed : VerificationOutcome::Failed));
        CheckPayload check;
        check.target_claim = "|beta| < 1";
        check.check_method = "compare the exact boost fraction against one";
        check.expected_relation = "the magnitude is strictly below one";
        check.observed_result = speed_observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    derivation.complete_plan_precondition(
        plan_id, "pre.relativity.subluminal-boost",
        subluminal ? VerificationOutcome::Passed : VerificationOutcome::Failed, speed_observed);
    if (!subluminal)
        return failed(RelativityOutcome::SuperluminalSpeed, DerivationStatus::VerificationFailed,
                      "a boost of " + rational_text(beta) +
                          " c is at or above the speed of light");

    Rational beta_square;
    Rational one_minus;
    const Rational one{1, 1};
    if (!rational_mul(beta, beta, &beta_square) || !rational_sub(one, beta_square, &one_minus))
        return failed(RelativityOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "forming 1 - beta^2 exceeds exact integer arithmetic");
    Rational inverse_gamma;
    Rational gamma = one;
    bool factor_exact = true;
    if (shape.needs_factor) {
        if (!rational_sqrt_exact(one_minus, &inverse_gamma) ||
            !rational_div(one, inverse_gamma, &gamma))
            factor_exact = false;
    } else {
        inverse_gamma = one;
    }

    Rational identity;
    bool identity_computed = false;
    bool identity_holds = false;
    if (factor_exact && shape.needs_factor) {
        Rational gamma_square;
        identity_computed = rational_mul(gamma, gamma, &gamma_square) &&
                            rational_mul(gamma_square, one_minus, &identity);
        identity_holds = identity_computed && rational_equal(identity, one);
    }
    if (shape.needs_factor) {
        const std::string factor_observed =
            !factor_exact
                ? "1 - beta^2 = " + rational_text(one_minus) + " is not the square of a fraction"
                : "gamma = " + rational_text(gamma) + " with gamma^2 (1 - beta^2) = " +
                      rational_text(identity);
        if (!meter.step())
            return RelativityResult();
        {
            Step step;
            step.phase = "solve";
            step.goal = "Compute the Lorentz factor";
            step.rule_id = "physics.relativity.lorentz-factor";
            step.rule_name = "Lorentz factor";
            step.explanation_short = "Apply gamma = 1 / sqrt(1 - beta^2)";
            step.explanation_detailed =
                "The factor depends on beta squared, so a boost along the negative x axis gives the "
                "same factor as the matching boost along the positive one. This family reports the "
                "factor only when it is an exact fraction, since a decimal nothing checked is not "
                "evidence.";
            step.claim = ClaimType::Definition;
            step.proof_obligations.push_back(
                {"obl.relativity.factor-identity", "the reported factor satisfies gamma^2 (1 - beta^2) = 1"});
            const VerificationOutcome outcome = !identity_computed
                                                    ? VerificationOutcome::Inconclusive
                                                    : identity_holds ? VerificationOutcome::Passed
                                                                     : VerificationOutcome::Failed;
            step.verifications.push_back(verification("exact rational identity", factor_observed,
                                                      EvidenceStrength::CandidateChecked, outcome));
            CheckPayload check;
            check.target_claim = "gamma^2 (1 - beta^2) = 1";
            check.check_method = "square the exact factor and multiply by 1 - beta^2";
            check.expected_relation = "the product is exactly one";
            check.observed_result = factor_observed;
            derivation.add_check(plan_id, std::move(step), std::move(check));
        }
        if (!factor_exact)
            return failed(RelativityOutcome::InexactLorentzFactor,
                          DerivationStatus::VerificationFailed,
                          "1 - beta^2 = " + rational_text(one_minus) +
                              " is not the square of an exact fraction, so gamma has no exact value "
                              "here");
        if (!identity_computed)
            return failed(RelativityOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "checking the Lorentz factor exceeds exact integer arithmetic");
        if (!identity_holds)
            return failed(RelativityOutcome::VerificationFailed,
                          DerivationStatus::VerificationFailed,
                          "the computed Lorentz factor does not satisfy its own identity");
    }

    Rational reads[kMaxVariables];
    NodeId read_values[kMaxVariables] = {kNoNode, kNoNode, kNoNode};
    Precision answer_precision = problem.boost.precision;
    for (size_t index = 0; index < shape.read_count; ++index) {
        if (!normalize_copy(knowns[index]->quantity.value, &reads[index]))
            return failed(RelativityOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          known_text(*knowns[index]) + " is not an exact fraction");
        read_values[index] = normalized_rational_node(arena, reads[index]);
        answer_precision = precision_combine(answer_precision, knowns[index]->quantity.precision);
    }
    const Computed computed =
        apply_relation(problem.relation, beta, gamma, inverse_gamma, reads);
    if (!computed.ok)
        return failed(RelativityOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      computed.detail);

    NodeId report_values[kMaxVariables] = {kNoNode, kNoNode, kNoNode};
    for (size_t index = 0; index < shape.report_count; ++index)
        report_values[index] = normalized_rational_node(arena, computed.values[index]);
    const NodeId substituted =
        build_relation(arena, problem.relation, read_values, report_values,
                       normalized_rational_node(arena, beta), normalized_rational_node(arena, gamma),
                       normalized_rational_node(arena, Rational{kLightSpeed, 1}));
    if (arena.failed())
        return failed(RelativityOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                      status_name(arena.status()));

    if (!meter.step())
        return RelativityResult();
    {
        Step step;
        step.phase = "solve";
        step.goal = std::string("Apply ") + shape.written;
        step.rule_id = "physics.relativity.apply";
        step.rule_name = relativity_relation_name(problem.relation);
        step.explanation_short = "Substitute the exact declared values into the relation";
        step.explanation_detailed =
            std::string("Each reported quantity names the frame it belongs to. ") +
            convention_text(problem, beta) + ", and " + shape.assumption + ".";
        step.claim = ClaimType::SolutionSetPreserved;
        step.proof_obligations.push_back(
            {"obl.physics.lookup-preserves-solutions",
             "the value put in place of a symbol is the one the problem declared for it"});
        step.verifications.push_back(verification("typed known-quantity lookup",
                                                  "every given was substituted in its declared unit",
                                                  EvidenceStrength::StructurallyValid,
                                                  VerificationOutcome::Passed));
        TransformationPayload payload;
        payload.before = equation;
        payload.after = substituted;
        payload.concrete_action =
            std::string("Substitute the declared givens into ") + shape.written;
        payload.reversible = true;
        derivation.add_transformation(plan_id, std::move(step), std::move(payload));
    }

    const Invariant invariant =
        check_invariant(problem.relation, beta, reads, computed.values);
    if (!meter.step())
        return RelativityResult();
    {
        Step step;
        step.phase = "check";
        step.goal = "Check the relativistic invariant";
        step.rule_id = "physics.relativity.check-invariant";
        step.rule_name = "Invariant check";
        step.explanation_short =
            "Check the answer against a quantity both frames have to agree on";
        step.explanation_detailed =
            "This check does not repeat the relation that produced the answer. It compares a "
            "quantity the two frames must agree on, so an answer that came out of a wrong "
            "transformation fails here instead of agreeing with itself.";
        step.claim = ClaimType::Implication;
        step.proof_obligations.push_back(
            {"obl.relativity.invariant-holds", "the reported values satisfy the frame invariant"});
        const VerificationOutcome outcome = !invariant.computed
                                                ? VerificationOutcome::Inconclusive
                                                : invariant.holds ? VerificationOutcome::Passed
                                                                  : VerificationOutcome::Failed;
        const std::string observed =
            !invariant.computed ? "the exact comparison exceeds integer arithmetic"
                                : invariant.observed;
        step.verifications.push_back(verification("exact invariant comparison", observed,
                                                  EvidenceStrength::CandidateChecked, outcome));
        CheckPayload check;
        check.target_claim = invariant.computed ? invariant.claim : std::string("the frame invariant");
        check.check_method = "evaluate the invariant on both frames' exact values";
        check.expected_relation = "the two frames agree exactly";
        check.observed_result = observed;
        derivation.add_check(plan_id, std::move(step), std::move(check));
    }
    if (!invariant.computed)
        return failed(RelativityOutcome::ArithmeticOverflow, DerivationStatus::ResourceLimitReached,
                      "checking the invariant exceeds exact integer arithmetic");
    if (!invariant.holds)
        return failed(RelativityOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
                      "the reported values do not preserve " + invariant.claim);

    RelativityResult result;
    result.equation = equation;
    result.substituted = substituted;
    result.has_factor = shape.needs_factor;
    result.lorentz_factor = gamma;
    result.factor_text = shape.needs_factor ? rational_text(gamma) : std::string();
    result.convention = convention_text(problem, beta);
    for (size_t index = 0; index < shape.report_count; ++index) {
        RelativityOutput output;
        output.variable = shape.reports[index];
        output.quantity.value = computed.values[index];
        output.quantity.unit = family_unit(output.variable);
        output.quantity.precision =
            precision_at_digits(computed.values[index], answer_precision);
        output.value_text = rational_text(computed.values[index]);
        output.unit_text = output.quantity.unit.text;
        output.frame = frame_of(problem, output.variable);
        result.outputs.push_back(std::move(output));
    }

    // One report step covers every reported value, because they share the figure count the givens
    // and the boost decide between them.
    bool any_rounded = false;
    bool any_unreadable = false;
    std::string rounding_observed;
    for (RelativityOutput &output : result.outputs) {
        if (output.quantity.precision.kind != NumberKind::Measured)
            continue;
        std::string reported;
        if (!precision_rounded_text(output.quantity.value, output.quantity.precision, &reported))
            return failed(RelativityOutcome::ArithmeticOverflow,
                          DerivationStatus::ResourceLimitReached,
                          "reporting the measured precision exceeds exact integer arithmetic");
        if (reported == output.value_text)
            continue;
        const HalfPlace checked =
            precision_rounding_valid(output.quantity.value, reported, output.quantity.precision);
        if (checked == HalfPlace::Outside)
            return failed(RelativityOutcome::VerificationFailed,
                          DerivationStatus::VerificationFailed,
                          reported + " is further than half a unit in its last place from " +
                              output.value_text);
        if (!rounding_observed.empty())
            rounding_observed += ", ";
        rounding_observed += reported + " for " + output.value_text;
        if (checked == HalfPlace::Unreadable) {
            any_unreadable = true;
            continue;
        }
        any_rounded = true;
        output.value_text = reported;
    }
    if (any_rounded || any_unreadable) {
        if (!meter.step())
            return RelativityResult();
        Step step;
        step.phase = "report";
        step.goal = "Report the answers to the measured precision";
        step.rule_id = "physics.relativity.significant-figures";
        step.rule_name = "Significant figures";
        step.explanation_short =
            "Use the fewest significant figures among the boost and the measured givens";
        step.explanation_detailed =
            "The boost is a measurement too, so it enters the figure count alongside the givens. "
            "Round once, at the end.";
        step.claim = ClaimType::NoClaim;
        step.proof_obligations.push_back(
            {"obl.physics.reported-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        step.verifications.push_back(
            verification("exact comparison against the unrounded value", rounding_observed,
                         EvidenceStrength::CandidateChecked,
                         any_unreadable ? VerificationOutcome::Inconclusive
                                        : VerificationOutcome::Passed));
        CheckPayload check;
        check.target_claim =
            "each reported value is within half a unit in the last place of the exact one";
        check.check_method = "read the rounded text back and compare it against the exact value";
        check.expected_relation = "the difference is at most half a unit in the last place";
        check.observed_result = rounding_observed;
        derivation.add_check(kNoStep, std::move(step), std::move(check));
    }
    result.outcome = RelativityOutcome::Solved;
    return result;
}

}  // namespace

const char *relativity_relation_name(RelativityRelation relation) {
    switch (relation) {
        case RelativityRelation::TimeDilation: return "Time dilation";
        case RelativityRelation::LengthContraction: return "Length contraction";
        case RelativityRelation::LorentzTransformation: return "Lorentz transformation";
        case RelativityRelation::VelocityAddition: return "Relativistic velocity addition";
        case RelativityRelation::EnergyMomentum: return "Relativistic energy and momentum";
    }
    return "invalid relation";
}

const char *relativity_relation_written(RelativityRelation relation) {
    return shape_of(relation).written;
}

const char *relativity_variable_name(RelativityVariable variable) {
    switch (variable) {
        case RelativityVariable::ProperTime: return "proper time";
        case RelativityVariable::DilatedTime: return "dilated time";
        case RelativityVariable::ProperLength: return "proper length";
        case RelativityVariable::ContractedLength: return "contracted length";
        case RelativityVariable::EventPosition: return "event position";
        case RelativityVariable::EventTime: return "event time";
        case RelativityVariable::TransformedPosition: return "transformed event position";
        case RelativityVariable::TransformedTime: return "transformed event time";
        case RelativityVariable::ObjectVelocity: return "object velocity in the moving frame";
        case RelativityVariable::TransformedVelocity: return "object velocity in the rest frame";
        case RelativityVariable::RestEnergy: return "rest energy";
        case RelativityVariable::TotalEnergy: return "total energy";
        case RelativityVariable::MomentumEnergy: return "momentum energy pc";
        case RelativityVariable::KineticEnergy: return "relativistic kinetic energy";
    }
    return "invalid variable";
}

const char *relativity_variable_symbol(RelativityVariable variable) {
    switch (variable) {
        case RelativityVariable::ProperTime: return "dt0";
        case RelativityVariable::DilatedTime: return "dt";
        case RelativityVariable::ProperLength: return "L0";
        case RelativityVariable::ContractedLength: return "L";
        case RelativityVariable::EventPosition: return "x";
        case RelativityVariable::EventTime: return "t";
        case RelativityVariable::TransformedPosition: return "xprime";
        case RelativityVariable::TransformedTime: return "tprime";
        case RelativityVariable::ObjectVelocity: return "betaprime";
        case RelativityVariable::TransformedVelocity: return "betaobject";
        case RelativityVariable::RestEnergy: return "E0";
        case RelativityVariable::TotalEnergy: return "E";
        case RelativityVariable::MomentumEnergy: return "pc";
        case RelativityVariable::KineticEnergy: return "K";
    }
    return "?";
}

const char *relativity_variable_unit(RelativityVariable variable) {
    switch (variable) {
        case RelativityVariable::ProperTime:
        case RelativityVariable::DilatedTime:
        case RelativityVariable::EventTime:
        case RelativityVariable::TransformedTime: return "s";
        case RelativityVariable::ProperLength:
        case RelativityVariable::ContractedLength:
        case RelativityVariable::EventPosition:
        case RelativityVariable::TransformedPosition: return "m";
        case RelativityVariable::ObjectVelocity:
        case RelativityVariable::TransformedVelocity: return "c";
        case RelativityVariable::RestEnergy:
        case RelativityVariable::TotalEnergy:
        case RelativityVariable::MomentumEnergy:
        case RelativityVariable::KineticEnergy: return "MeV";
    }
    return "";
}

Dimension relativity_variable_dimension(RelativityVariable variable) {
    switch (variable) {
        case RelativityVariable::ProperTime:
        case RelativityVariable::DilatedTime:
        case RelativityVariable::EventTime:
        case RelativityVariable::TransformedTime: return time_dimension_local();
        case RelativityVariable::ProperLength:
        case RelativityVariable::ContractedLength:
        case RelativityVariable::EventPosition:
        case RelativityVariable::TransformedPosition: return length_dimension_local();
        case RelativityVariable::ObjectVelocity:
        case RelativityVariable::TransformedVelocity: return Dimension();
        case RelativityVariable::RestEnergy:
        case RelativityVariable::TotalEnergy:
        case RelativityVariable::MomentumEnergy:
        case RelativityVariable::KineticEnergy: return energy_dimension_local();
    }
    return Dimension();
}

bool relativity_variable_in_moving_frame(RelativityVariable variable) {
    switch (variable) {
        case RelativityVariable::ProperTime:
        case RelativityVariable::ProperLength:
        case RelativityVariable::TransformedPosition:
        case RelativityVariable::TransformedTime:
        case RelativityVariable::ObjectVelocity:
        case RelativityVariable::RestEnergy: return true;
        default: break;
    }
    return false;
}

bool relativity_relation_reads(RelativityRelation relation, RelativityVariable variable) {
    return read_index(shape_of(relation), variable) >= 0;
}

bool relativity_relation_reports(RelativityRelation relation, RelativityVariable variable) {
    const RelationShape &shape = shape_of(relation);
    for (size_t index = 0; index < shape.report_count; ++index) {
        if (shape.reports[index] == variable)
            return true;
    }
    return false;
}

const char *relativity_outcome_name(RelativityOutcome outcome) {
    switch (outcome) {
        case RelativityOutcome::Solved: return "solved";
        case RelativityOutcome::InvalidProblem: return "invalid problem";
        case RelativityOutcome::MissingKnown: return "missing known";
        case RelativityOutcome::DuplicateKnown: return "duplicate known";
        case RelativityOutcome::FrameUndeclared: return "frame undeclared";
        case RelativityOutcome::FrameMismatch: return "frame mismatch";
        case RelativityOutcome::DimensionMismatch: return "dimension mismatch";
        case RelativityOutcome::SuperluminalSpeed: return "superluminal speed";
        case RelativityOutcome::InexactLorentzFactor: return "inexact Lorentz factor";
        case RelativityOutcome::UnphysicalValue: return "unphysical value";
        case RelativityOutcome::ArithmeticOverflow: return "arithmetic overflow";
        case RelativityOutcome::VerificationFailed: return "verification failed";
        case RelativityOutcome::Cancelled: return "cancelled";
        case RelativityOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

RelativityResult solve_relativity(Arena &arena, Derivation &derivation,
                                  const RelativityProblem &problem, const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    const RelationShape &shape = shape_of(problem.relation);
    NodeId model = kNoNode;
    Rational beta;
    RelativityResult result =
        solve_body(arena, derivation, meter, problem, budget, shape, &beta, &model);

    if (meter.stopped()) {
        const bool cancelled = meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        RelativityResult halted;
        halted.outcome =
            cancelled ? RelativityOutcome::Cancelled : RelativityOutcome::ResourceExceeded;
        halted.detail = halt_name(meter.halt());
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        halted.cost = meter.cost();
        record_context(derivation, budget, shape, problem, beta, model, halted.status);
        return halted;
    }

    if (result.outcome == RelativityOutcome::Solved)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, shape, problem, beta, model, result.status);
    return result;
}

}  // namespace nps
