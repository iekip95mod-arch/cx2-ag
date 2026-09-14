#include <string>
#include <vector>

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
        t.evidence("PHYS-008",
                   value_is(solved.result, 7, 2) && solved.result.inventory.size() == 4,
                   "weight, normal, applied and friction are inventoried and summed");
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
        t.check(solved.derivation.all_verified_from(0),
                "a consistent static equilibrium carries passing evidence throughout");
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
        t.check(solved.result.inventory.size() == 5,
                "the tension is a fifth entry rather than folded into the applied force");
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
        t.check(solved.derivation.all_verified_from(0),
                "a normal force answer carries passing evidence throughout");
    }
}

}
