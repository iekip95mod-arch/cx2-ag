#include <string>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/print.h"
#include "nps/physics/forces.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Quantity parsed(const char *text) {
    Quantity quantity;
    std::string error;
    if (!parse_quantity(text, &quantity, &error))
        quantity.unit.text = "parse failed: " + error;
    return quantity;
}

ForcesProblem base(ForcesUnknown unknown) {
    ForcesProblem problem;
    problem.body = "block";
    problem.support = "table";
    problem.mass = parsed("2 kg");
    problem.gravity = parsed("10 m/s^2");
    problem.unknown = unknown;
    return problem;
}

struct Run {
    explicit Run(const ForcesProblem &problem, const Budget &budget = Budget())
        : result(solve_forces(arena, derivation, problem, budget)) {}

    Arena arena;
    Derivation derivation;
    ForcesResult result;
};

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return true;
    }
    return false;
}

bool has_obligation_contract(const Derivation &derivation, const char *rule,
                             const char *obligation_id, const char *obligation_text,
                             const char *method, ClaimType claim) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (step.rule_id != rule)
            continue;
        return step.claim == claim &&
               step.proof_obligations.size() == 1 &&
               step.proof_obligations[0].id == obligation_id &&
               step.proof_obligations[0].text == obligation_text && step.verifications.size() == 1 &&
               step.verifications[0].evidence_id == obligation_id &&
               step.verifications[0].method == method &&
               step.verifications[0].strength == EvidenceStrength::DimensionallyValid &&
               step.verifications[0].outcome == VerificationOutcome::Passed;
    }
    return false;
}

const Step *step_by(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (step.rule_id == rule)
            return &step;
    }
    return nullptr;
}

const TransformationPayload *moved_by(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        const StepId id = static_cast<StepId>(index);
        if (derivation.at(id).rule_id == rule)
            return derivation.transformation(id);
    }
    return nullptr;
}

const ForceEntry *entry_of(const ForcesResult &result, ForceKind kind) {
    for (const ForceEntry &entry : result.inventory) {
        if (entry.kind == kind)
            return &entry;
    }
    return nullptr;
}

bool value_is(const ForcesResult &result, int64_t numerator, int64_t denominator) {
    return result.has_value && result.value.num == numerator && result.value.den == denominator;
}

bool cancel_now(void *) { return true; }

}

void run_forces_tests(TestSink &t) {
    {
        // A horizontal push against kinetic friction, solved for the acceleration.
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.applied = parsed("12 N");
        problem.has_applied = true;
        problem.friction = FrictionModel::Kinetic;
        problem.friction_coefficient = Rational{1, 4};
        problem.motion = MotionSense::UpTheAxis;
        problem.assume_equilibrium = false;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved, "a kinetic-friction push solves");
        t.check(value_is(solved.result, 7, 2), "12 N less 5 N of friction over 2 kg is 3.5 m/s^2");
        t.equal(solved.result.unit_text, "m/s^2", "the acceleration carries its SI unit");
        const ForceEntry *normal = entry_of(solved.result, ForceKind::Normal);
        t.check(normal != nullptr && normal->magnitude.num == 20 && normal->magnitude.den == 1,
                "the normal force balances the 20 N weight");
        const ForceEntry *friction = entry_of(solved.result, ForceKind::Friction);
        t.check(friction != nullptr && friction->along.num == -5 && friction->along.den == 1,
                "kinetic friction opposes the declared motion");
        t.check(has_rule(solved.derivation, "physics.forces.kinetic-friction") &&
                    !has_rule(solved.derivation, "physics.forces.static-friction-limit"),
                "the kinetic path records its own rule and not the static one");
        t.check(has_rule(solved.derivation, "physics.forces.check-residual") &&
                    has_rule(solved.derivation, "physics.forces.check-result-dimension"),
                "the residual and dimension checks are recorded");
        t.check(solved.derivation.all_verified_from(0),
                "every claim in a solved force problem has passing evidence");
        t.check(has_obligation_contract(
                    solved.derivation, "physics.forces.weight", "obl.forces.weight-components",
                    "weight is mass times gravity and its components are its exact incline projections",
                    "exact rational product", ClaimType::EquivalentExpression),
                "the weight product discharges its component obligation");
        t.check(has_obligation_contract(
                    solved.derivation, "physics.forces.normal-force",
                    "obl.forces.normal-from-balance",
                    "the normal force makes the exact across-axis sum zero",
                    "exact across-axis sum", ClaimType::SolutionSetPreserved),
                "the across-axis sum discharges the normal-force obligation");
        t.check(has_obligation_contract(
                    solved.derivation, "physics.forces.kinetic-friction",
                    "obl.forces.kinetic-friction",
                    "kinetic friction has magnitude mu_k N and points opposite the declared motion",
                    "exact rational product", ClaimType::EquivalentExpression),
                "the friction product discharges the kinetic-friction obligation");
        t.check(has_obligation_contract(
                    solved.derivation, "physics.forces.solve-unknown",
                    "obl.forces.unknown-isolated",
                    "exact rearrangement isolates the requested unknown from its force-balance equation",
                    "exact rearrangement", ClaimType::SolutionSetPreserved),
                "the rearrangement discharges the unknown-isolation obligation");
        const TransformationPayload *kinetic =
            moved_by(solved.derivation, "physics.forces.kinetic-friction");
        const Step *kinetic_step = step_by(solved.derivation, "physics.forces.kinetic-friction");
        t.check(kinetic != nullptr && kinetic_step != nullptr &&
                    kinetic_step->claim == ClaimType::EquivalentExpression && kinetic->reversible,
                "the kinetic-friction record claims a reversible equivalent expression");
        if (kinetic != nullptr) {
            t.check(solved.arena.at(kinetic->before).kind == Kind::Neg &&
                        literal_sign(solved.arena, kinetic->after) == Sign::Negative,
                    "the kinetic-friction expression and value both carry the opposing sign");
            t.check(canonicalize(solved.arena, kinetic->before) ==
                        canonicalize(solved.arena, kinetic->after),
                    "the signed kinetic-friction expression is exactly equivalent to its value");
        }
        const TransformationPayload *normal_move =
            moved_by(solved.derivation, "physics.forces.normal-force");
        const Step *normal_step = step_by(solved.derivation, "physics.forces.normal-force");
        t.check(normal_move != nullptr && normal_step != nullptr &&
                    normal_step->claim == ClaimType::SolutionSetPreserved &&
                    normal_move->reversible &&
                    solved.arena.at(normal_move->before).kind == Kind::Equals &&
                    solved.arena.at(normal_move->after).kind == Kind::Equals,
                "normal-force isolation preserves an equation's solution set reversibly");
        if (normal_move != nullptr)
            t.equal(print(solved.arena, normal_move->after), "(N = 20)",
                    "normal-force isolation records the solved equation rather than a scalar");
        const TransformationPayload *solve_move =
            moved_by(solved.derivation, "physics.forces.solve-unknown");
        const Step *solve_step = step_by(solved.derivation, "physics.forces.solve-unknown");
        t.check(solve_move != nullptr && solve_step != nullptr &&
                    solve_step->claim == ClaimType::SolutionSetPreserved &&
                    solve_move->reversible &&
                    solved.arena.at(solve_move->before).kind == Kind::Equals &&
                    solved.arena.at(solve_move->after).kind == Kind::Equals,
                "unknown isolation preserves an equation's solution set reversibly");
        if (solve_move != nullptr)
            t.equal(print(solved.arena, solve_move->after), "(a = (7 * (2^-1)))",
                    "unknown isolation records the solved equation rather than a scalar");
        bool every_entry_known = true;
        for (const ForceEntry &entry : solved.result.inventory)
            every_entry_known = every_entry_known && entry.known;
        t.check(every_entry_known,
                "an acceleration request supplies every force, so no entry is marked unknown");
        t.evidence("PHYS-008",
                   value_is(solved.result, 7, 2) && solved.result.inventory.size() == 4,
                   "weight, normal, applied and friction are inventoried and summed");
        t.evidence("PHYS-025",
                   has_rule(solved.derivation, "physics.forces.plan") &&
                       has_rule(solved.derivation, "physics.forces.check-input-dimensions") &&
                       has_rule(solved.derivation, "physics.forces.weight") &&
                       has_rule(solved.derivation, "physics.forces.normal-force") &&
                       has_rule(solved.derivation, "physics.forces.kinetic-friction") &&
                       has_rule(solved.derivation, "physics.forces.solve-unknown") &&
                       has_rule(solved.derivation, "physics.forces.check-residual") &&
                       has_rule(solved.derivation, "physics.forces.check-result-dimension") &&
                       solved.result.assumptions.size() >= 3,
                   "the force family records its axis choice, the governing sum, the friction model "
                   "it selected and its residual and dimension checks");
    }
    {
        // Static friction holds the block on an exact 3-4-5 incline, so equilibrium is consistent.
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.support = "ramp";
        problem.surface = SurfaceKind::Incline;
        problem.incline_sin = Rational{3, 5};
        problem.incline_cos = Rational{4, 5};
        problem.friction = FrictionModel::Static;
        problem.friction_coefficient = Rational{1, 1};
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved, "static equilibrium on a ramp solves");
        t.check(value_is(solved.result, 12, 1), "friction matches the 12 N component down the slope");
        t.check(solved.result.static_checked &&
                    solved.result.maximum_static_friction.num == 16 &&
                    solved.result.maximum_static_friction.den == 1,
                "the maximum static friction is mu_s N and is reported apart from the actual one");
        t.check(solved.result.required_friction.num == 12,
                "the friction equilibrium needs is distinct from the maximum available");
        t.check(has_rule(solved.derivation, "physics.forces.static-friction-limit"),
                "the static limit comparison is recorded");
        const ForceEntry *wanted = entry_of(solved.result, ForceKind::Friction);
        t.check(wanted != nullptr && !wanted->known,
                "the friction the request asked for is inventoried as the unknown one");
        const ForceEntry *given_weight = entry_of(solved.result, ForceKind::Weight);
        t.check(given_weight != nullptr && given_weight->known,
                "the weight supplied with the problem stays marked known beside it");
        const TransformationPayload *isolation =
            moved_by(solved.derivation, "physics.forces.solve-unknown");
        const NodeId friction_symbol = solved.arena.symbol("f");
        t.check(isolation != nullptr && depends_on(solved.arena, isolation->before, friction_symbol),
                "friction isolation starts from an equation that contains friction");
        if (isolation != nullptr)
            t.equal(print(solved.arena, isolation->after), "(f = 12)",
                    "friction isolation preserves the solved equation");
        t.check(solved.derivation.all_verified_from(0),
                "a consistent static equilibrium carries passing evidence throughout");
    }
    {
        // The kinetic branch of the same request: the friction has a value the surfaces fix, so the
        // entry that carries it is still the one the request asked for.
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.applied = parsed("9 N");
        problem.has_applied = true;
        problem.friction = FrictionModel::Kinetic;
        problem.friction_coefficient = Rational{1, 4};
        problem.motion = MotionSense::UpTheAxis;
        problem.assume_equilibrium = false;
        problem.acceleration = parsed("2 m/s^2");
        problem.has_acceleration = true;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved,
                "a kinetic friction the request asks for solves");
        t.check(value_is(solved.result, -5, 1),
                "mu_k N is 5 N against the declared motion up the axis");
        const ForceEntry *wanted = entry_of(solved.result, ForceKind::Friction);
        t.check(wanted != nullptr && !wanted->known,
                "the kinetic friction the request asked for is inventoried as the unknown one");
        const ForceEntry *given_weight = entry_of(solved.result, ForceKind::Weight);
        t.check(given_weight != nullptr && given_weight->known,
                "the weight supplied beside the kinetic friction stays marked known");
        t.check(has_rule(solved.derivation, "physics.forces.kinetic-friction"),
                "the kinetic friction rule is recorded for the requested unknown");
        const TransformationPayload *isolation =
            moved_by(solved.derivation, "physics.forces.solve-unknown");
        if (isolation != nullptr)
            t.equal(print(solved.arena, isolation->after), "(f = -5)",
                    "a consistent kinetic candidate matches the isolated balance equation");
        t.check(solved.derivation.all_verified_from(0),
                "a solved kinetic friction request carries passing evidence throughout");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.applied = parsed("9 N");
        problem.has_applied = true;
        problem.friction = FrictionModel::Kinetic;
        problem.friction_coefficient = Rational{1, 4};
        problem.motion = MotionSense::UpTheAxis;
        problem.assume_equilibrium = false;
        problem.acceleration = parsed("0 m/s^2");
        problem.has_acceleration = true;
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::VerificationFailed,
                "a kinetic candidate inconsistent with the declared acceleration is refused");
        t.check(!refused.result.has_value,
                "an inconsistent kinetic candidate does not publish a friction value");
        const TransformationPayload *candidate =
            moved_by(refused.derivation, "physics.forces.kinetic-friction");
        const TransformationPayload *isolation =
            moved_by(refused.derivation, "physics.forces.solve-unknown");
        if (candidate != nullptr)
            t.equal(print(refused.arena, candidate->after), "-5",
                    "the coefficient-derived kinetic candidate stays recorded separately");
        if (isolation != nullptr) {
            t.equal(print(refused.arena, isolation->before), "((9 + f) = 0)",
                    "friction isolation starts from the declared force balance");
            t.equal(print(refused.arena, isolation->after), "(f = -9)",
                    "friction isolation derives its value from the same balance equation");
        }
        t.check(candidate != nullptr && isolation != nullptr,
                "the inconsistent kinetic candidate and algebraic isolation are both auditable");
        t.check(!refused.derivation.all_verified_from(0),
                "the residual check records the inconsistent kinetic candidate as failing");
    }
    {
        // The same ramp with too little friction: the assumption is tested and fails.
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.support = "ramp";
        problem.surface = SurfaceKind::Incline;
        problem.incline_sin = Rational{3, 5};
        problem.incline_cos = Rational{4, 5};
        problem.friction = FrictionModel::Static;
        problem.friction_coefficient = Rational{1, 10};
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::EquilibriumImpossible,
                "an equilibrium static friction cannot supply is refused rather than asserted");
        t.check(!refused.result.has_value, "no friction value is offered once the assumption fails");
        t.check(refused.result.required_friction.num == 12 &&
                    refused.result.maximum_static_friction.num == 8 &&
                    refused.result.maximum_static_friction.den == 5,
                "the refusal still reports what was needed against what was available");
        t.check(!refused.derivation.all_verified_from(0),
                "the failed static check is recorded as failing rather than dropped");
    }
    {
        // Third-law partners belong to the other bodies and never to this body's sum.
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.applied = parsed("6 N");
        problem.has_applied = true;
        problem.assume_equilibrium = false;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved, "a frictionless push solves");
        t.check(value_is(solved.result, 3, 1), "6 N over 2 kg is 3 m/s^2");
        t.check(solved.result.pairs.size() == 2, "both contact forces name a third-law partner");
        bool separate = true;
        for (const InteractionPair &pair : solved.result.pairs) {
            if (pair.reaction_on == problem.body)
                separate = false;
        }
        t.check(separate, "no third-law reaction acts on the body whose forces were summed");
        t.check(has_rule(solved.derivation, "physics.forces.third-law-pairs"),
                "the third-law separation is recorded as a check");
    }
    {
        // The boundary between the static and kinetic stories: exactly at mu_s N it still holds.
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.support = "ramp";
        problem.surface = SurfaceKind::Incline;
        problem.incline_sin = Rational{3, 5};
        problem.incline_cos = Rational{4, 5};
        problem.friction = FrictionModel::Static;
        problem.friction_coefficient = Rational{3, 4};
        Run boundary(problem);
        t.check(boundary.result.outcome == ForcesOutcome::Solved,
                "a required friction exactly equal to mu_s N is still an equilibrium");
        t.check(value_is(boundary.result, 12, 1) &&
                    boundary.result.maximum_static_friction.num == 12,
                "the boundary case reports the needed friction at its limit");
    }
    {
        // Solving for the force that produces a declared acceleration on a ramp.
        ForcesProblem problem = base(ForcesUnknown::AppliedForce);
        problem.support = "ramp";
        problem.surface = SurfaceKind::Incline;
        problem.incline_sin = Rational{3, 5};
        problem.incline_cos = Rational{4, 5};
        problem.assume_equilibrium = false;
        problem.acceleration = parsed("1 m/s^2");
        problem.has_acceleration = true;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved, "an applied-force request solves");
        t.check(value_is(solved.result, 14, 1),
                "2 N of net force plus the 12 N slope component needs 14 N");
        t.equal(solved.result.unit_text, "N", "a force answer carries newtons");
        const ForceEntry *push = entry_of(solved.result, ForceKind::Applied);
        t.check(push != nullptr,
                "the applied force being solved for joins the inventory it is missing from");
        t.check(push != nullptr && push->along.num == 14 && push->along.den == 1 &&
                    push->magnitude.num == 14 && push->magnitude.den == 1,
                "the inventoried applied force carries the solved 14 N along the slope");
        t.equal(push != nullptr ? push->magnitude_text : std::string(), std::string("14 N"),
                "the solved entry is rendered like every other inventory entry");
        t.check(push != nullptr && !push->known,
                "the force the request asked for is the one entry marked unknown");
        bool others_known = true;
        for (const ForceEntry &entry : solved.result.inventory) {
            if (entry.kind != ForceKind::Applied)
                others_known = others_known && entry.known;
        }
        t.check(others_known, "the forces the problem supplied stay marked known");
        const TransformationPayload *isolation =
            moved_by(solved.derivation, "physics.forces.solve-unknown");
        const NodeId applied_symbol = solved.arena.symbol("F");
        t.check(isolation != nullptr && depends_on(solved.arena, isolation->before, applied_symbol),
                "applied-force isolation starts from an equation that contains the applied force");
        if (isolation != nullptr)
            t.equal(print(solved.arena, isolation->after), "(F = 14)",
                    "applied-force isolation preserves the solved equation");
    }
    {
        // The shape the bridge suite publishes: a frictionless horizontal push solved for.
        ForcesProblem problem = base(ForcesUnknown::AppliedForce);
        problem.assume_equilibrium = false;
        problem.acceleration = parsed("1 m/s^2");
        problem.has_acceleration = true;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved &&
                    value_is(solved.result, 2, 1),
                "a 2 kg block accelerating at 1 m/s^2 needs a 2 N push");
        t.equal(solved.result.value_text, std::string("2"),
                "the solved applied force prints as a whole number of newtons");
        t.check(solved.result.inventory.size() == 3,
                "the weight, the normal force and the solved push are the whole inventory");
        const ForceEntry *push = entry_of(solved.result, ForceKind::Applied);
        t.equal(push != nullptr ? push->along_text : std::string(), std::string("2 N"),
                "the solved entry's along component is rendered in newtons");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::FrictionForce);
        problem.surface = SurfaceKind::Incline;
        problem.incline_sin = Rational{1, 2};
        problem.incline_cos = Rational{1, 2};
        problem.friction = FrictionModel::Static;
        problem.friction_coefficient = Rational{1, 2};
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::InclineAngleNotExact,
                "an angle whose sine and cosine are not an exact pair is refused");
        t.check(!refused.result.has_value, "the inexact-angle refusal offers no value");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.applied = parsed("6 N");
        problem.has_applied = true;
        problem.assume_equilibrium = false;
        problem.friction = FrictionModel::Kinetic;
        problem.friction_coefficient = Rational{1, 4};
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::MotionSenseUndeclared,
                "kinetic friction without a declared direction is refused, not guessed");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.mass = parsed("2 m");
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::DimensionMismatch,
                "a length supplied as a mass is refused on its dimension");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::AppliedForce);
        problem.assume_equilibrium = false;
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::Underdetermined,
                "an accelerating problem with no acceleration supplied reports the missing "
                "constraint instead of assuming equilibrium");
    }
    {
        ForcesProblem problem = base(ForcesUnknown::NormalForce);
        problem.applied = parsed("4 N");
        problem.has_applied = true;
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem, budget);
        t.check(cancelled.result.outcome == ForcesOutcome::Cancelled,
                "a cancelled force problem reports cancellation");
        t.check(cancelled.derivation.size() == 0,
                "a cancelled force problem leaves no partial steps behind");
    }
    {
        // A string pulling alongside the push, so the tension joins the inventory and the sum.
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.applied = parsed("12 N");
        problem.has_applied = true;
        problem.tension = parsed("6 N");
        problem.has_tension = true;
        problem.assume_equilibrium = false;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved, "a tension alongside a push solves");
        t.check(value_is(solved.result, 9, 1), "12 N and 6 N over 2 kg is 9 m/s^2");
        const ForceEntry *tension = entry_of(solved.result, ForceKind::Tension);
        t.check(tension != nullptr && tension->along.num == 6 && tension->along.den == 1,
                "the tension enters the along axis with its own inventory entry");
        t.equal(tension == nullptr ? std::string() : tension->along_text, std::string("6 N"),
                "the tension component carries its unit the way every other entry does");
        ForcesProblem without_string = base(ForcesUnknown::Acceleration);
        without_string.applied = parsed("12 N");
        without_string.has_applied = true;
        without_string.assume_equilibrium = false;
        Run unstrung(without_string);
        t.check(unstrung.result.outcome == ForcesOutcome::Solved &&
                    unstrung.result.inventory.size() == 3,
                "the same push with no string carries the weight, the normal force and the push");
        const ForceEntry *push = entry_of(solved.result, ForceKind::Applied);
        t.check(solved.result.inventory.size() == unstrung.result.inventory.size() + 1 &&
                    push != nullptr && push->along.num == 12 && push->along.den == 1,
                "the tension adds an entry rather than folding into the applied force");
        bool massless = false;
        for (const std::string &assumption : solved.result.assumptions) {
            if (assumption.find("tension unchanged") != std::string::npos)
                massless = true;
        }
        t.check(massless, "the massless-string assumption is stated rather than left implicit");
        t.check(solved.derivation.all_verified_from(0),
                "a tension problem carries passing evidence throughout");
    }
    {
        // The other arm of the kinetic sign choice: motion down the axis puts friction up it.
        ForcesProblem problem = base(ForcesUnknown::Acceleration);
        problem.applied = parsed("12 N");
        problem.has_applied = true;
        problem.friction = FrictionModel::Kinetic;
        problem.friction_coefficient = Rational{1, 4};
        problem.motion = MotionSense::DownTheAxis;
        problem.assume_equilibrium = false;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved,
                "a body moving down the axis solves");
        const ForceEntry *friction = entry_of(solved.result, ForceKind::Friction);
        t.check(friction != nullptr && friction->along.num == 5 && friction->along.den == 1,
                "kinetic friction points up the axis when the motion is down it");
        t.check(value_is(solved.result, 17, 2),
                "12 N plus 5 N of friction over 2 kg is 8.5 m/s^2");
    }
    {
        // The normal force as the requested answer rather than as an intermediate.
        ForcesProblem problem = base(ForcesUnknown::NormalForce);
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved,
                "the normal force can be the requested unknown");
        t.check(value_is(solved.result, 20, 1),
                "a 2 kg block under 10 m/s^2 rests on a 20 N normal force");
        t.equal(solved.result.unit_text, "N", "the normal force answer carries the newton");
        t.equal(solved.result.value_text, std::string("20"),
                "the answer text is the solved normal force rather than an axis total");
        t.check(has_rule(solved.derivation, "physics.forces.check-residual"),
                "the normal force answer is substituted back into the axis sum");
        const ForceEntry *support = entry_of(solved.result, ForceKind::Normal);
        t.check(support != nullptr && !support->known,
                "the normal force the request asked for is inventoried as the unknown one");
        const TransformationPayload *isolation =
            moved_by(solved.derivation, "physics.forces.solve-unknown");
        const NodeId normal_symbol = solved.arena.symbol("N");
        t.check(isolation != nullptr && depends_on(solved.arena, isolation->before, normal_symbol),
                "normal-force isolation starts from the across-axis equation containing N");
        if (isolation != nullptr)
            t.equal(print(solved.arena, isolation->after), "(N = 20)",
                    "normal-force isolation preserves the solved equation");
        t.check(solved.derivation.all_verified_from(0),
                "a normal force answer carries passing evidence throughout");
    }
    {
        // Static friction takes whatever value the balance needs, so it absorbs any applied force
        // inside its limit. One equation with both unknown does not fix either of them.
        ForcesProblem problem = base(ForcesUnknown::AppliedForce);
        problem.friction = FrictionModel::Static;
        problem.friction_coefficient = Rational{1, 2};
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::Underdetermined,
                "static friction with the applied force requested reports the missing constraint");
        t.check(!refused.result.has_value,
                "the underdetermined static-friction arrangement offers no value");
    }
    {
        // An unbalanced push with equilibrium assumed leaves the along-axis residual non-zero, so
        // the answer is withheld rather than published.
        ForcesProblem problem = base(ForcesUnknown::NormalForce);
        problem.applied = parsed("4 N");
        problem.has_applied = true;
        Run refused(problem);
        t.check(refused.result.outcome == ForcesOutcome::VerificationFailed,
                "an answer that leaves a non-zero along-axis residual is refused");
        t.check(!refused.result.has_value, "a failed residual check offers no value");
    }
    {
        // The same gate reached through the applied-force arm. A solved problem cannot publish an
        // inventory that disagrees with the sum its own answer came out of, so the comparison is
        // driven here on the published inventory with one entry moved off its value.
        ForcesProblem problem = base(ForcesUnknown::AppliedForce);
        problem.assume_equilibrium = false;
        problem.acceleration = parsed("1 m/s^2");
        problem.has_acceleration = true;
        Run solved(problem);
        t.check(solved.result.outcome == ForcesOutcome::Solved && solved.result.has_value,
                "the applied force a 2 kg block needs for 1 m/s^2 is published");
        Rational target;
        target.num = 2;
        target.den = 1;
        const ForceBalance held = forces_along_balance(solved.result.inventory, target);
        t.check(held.exact && held.balanced && held.residual.num == 0,
                "the published inventory of that solved push sums to m a exactly");
        std::vector<ForceEntry> perturbed = solved.result.inventory;
        bool moved = false;
        for (ForceEntry &entry : perturbed) {
            if (entry.kind == ForceKind::Applied && entry.along.den == 1) {
                entry.along.num += 1;
                moved = true;
            }
        }
        t.check(moved, "the solved push is published as its own inventory entry to perturb");
        const ForceBalance broken = forces_along_balance(perturbed, target);
        t.check(broken.exact && !broken.balanced,
                "an applied-force entry that disagrees with the sum fails the residual gate");
        t.check(broken.residual.num == 1 && broken.residual.den == 1,
                "the gate reports how far the inventory is out rather than only that it is out");
        const ForceBalance restored = forces_along_balance(solved.result.inventory, target);
        t.check(restored.exact && restored.balanced,
                "the unperturbed inventory still passes, so the row above is about the entry");
    }
}

}
