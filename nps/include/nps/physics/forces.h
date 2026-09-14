#ifndef NPS_PHYSICS_FORCES_H
#define NPS_PHYSICS_FORCES_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// PHYS-008. One body, two axes chosen along and across the supporting surface, and the force sum
// on each axis. An incline is declared by the exact sine and cosine of its angle so the whole
// derivation stays in exact rational arithmetic, which is the supported envelope rather than a
// simplification: an angle whose trig values are not exact is refused and said so.
enum class SurfaceKind : uint8_t {
    Horizontal,
    Incline,
};

const char *surface_kind_name(SurfaceKind kind);

enum class FrictionModel : uint8_t {
    None,
    Static,
    Kinetic,
};

const char *friction_model_name(FrictionModel model);

// Which way the body is taken to move along the surface axis. Kinetic friction opposes it, so it
// has to be declared rather than guessed.
enum class MotionSense : uint8_t {
    Undeclared,
    UpTheAxis,
    DownTheAxis,
};

const char *motion_sense_name(MotionSense sense);

enum class ForceKind : uint8_t {
    Weight,
    Normal,
    Applied,
    Tension,
    Friction,
};

const char *force_kind_name(ForceKind kind);

// One entry of the force inventory on the body, resolved onto the two axes. #158 draws its labels
// from these records, so the magnitude, the components and the agent all travel together.
struct ForceEntry {
    ForceKind kind = ForceKind::Weight;
    std::string label;
    std::string agent;
    Rational along;
    Rational across;
    Rational magnitude;
    std::string magnitude_text;
    std::string along_text;
    std::string across_text;
    bool known = false;
};

// Newton's third law acts between bodies, so a pair never joins the inventory of either one.
struct InteractionPair {
    ForceKind kind = ForceKind::Normal;
    std::string on_body;
    std::string by_body;
    std::string reaction_on;
    std::string reaction_by;
    Rational magnitude;
    std::string magnitude_text;
};

enum class ForcesUnknown : uint8_t {
    Acceleration,
    AppliedForce,
    NormalForce,
    FrictionForce,
};

const char *forces_unknown_name(ForcesUnknown unknown);

struct ForcesProblem {
    std::string body = "block";
    std::string support = "surface";
    Quantity mass;
    Quantity gravity;
    SurfaceKind surface = SurfaceKind::Horizontal;
    // Exact sine and cosine of the incline angle, ignored for a horizontal surface.
    Rational incline_sin;
    Rational incline_cos{1, 1};
    // Applied push or pull along the surface axis, positive up the axis.
    Quantity applied;
    bool has_applied = false;
    // A string pulling along the surface axis, positive up the axis.
    Quantity tension;
    bool has_tension = false;
    FrictionModel friction = FrictionModel::None;
    Rational friction_coefficient;
    MotionSense motion = MotionSense::Undeclared;
    // Equilibrium is an assumption the solver tests, not one it takes on trust.
    bool assume_equilibrium = true;
    Quantity acceleration;
    bool has_acceleration = false;
    ForcesUnknown unknown = ForcesUnknown::Acceleration;
};

enum class ForcesOutcome : uint8_t {
    Solved,
    InvalidProblem,
    ClarificationRequired,
    UnsupportedArrangement,
    InclineAngleNotExact,
    DimensionMismatch,
    MotionSenseUndeclared,
    EquilibriumImpossible,
    Underdetermined,
    InconsistentInput,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *forces_outcome_name(ForcesOutcome outcome);

struct ForcesResult {
    ForcesOutcome outcome = ForcesOutcome::InvalidProblem;
    std::vector<ForceEntry> inventory;
    std::vector<InteractionPair> pairs;
    std::vector<std::string> assumptions;
    bool has_value = false;
    Rational value;
    std::string value_text;
    std::string unit_text;
    std::string along_equation_text;
    std::string across_equation_text;
    // The friction the equilibrium needed against the most static friction available.
    bool static_checked = false;
    Rational required_friction;
    Rational maximum_static_friction;
    std::string consistency;
    std::string detail;
    NodeId model = kNoNode;
    NodeId along_equation = kNoNode;
    NodeId across_equation = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

ForcesResult solve_forces(Arena &arena, Derivation &derivation, const ForcesProblem &problem,
                          const Budget &budget = Budget());

}

#endif
