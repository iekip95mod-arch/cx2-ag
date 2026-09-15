#ifndef NPS_OPTICS_H
#define NPS_OPTICS_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// The five introductory relations PHYS-022 names. Each one is a separate law with its own
// preconditions, so the relation is part of the problem rather than inferred from the knowns.
enum class OpticsRelation : uint8_t {
    // n1*sin(t1) = n2*sin(t2), with reflection as the same-medium case sin(t2) = sin(t1).
    Refraction,
    // 1/do + 1/di = 1/f for a thin lens.
    ThinLens,
    // 1/do + 1/di = 1/f for a spherical mirror, where f = R/2.
    SphericalMirror,
    // d*sin(t) = m*lambda, the two-slit bright fringes.
    DoubleSlit,
    // a*sin(t) = m*lambda, the single-slit diffraction minima.
    SingleSlit,
};

const char *optics_relation_name(OpticsRelation relation);

// Sines rather than angles, because the exact engine has no transcendental arithmetic and a
// decimal sine entered as a measured quantity keeps the precision the reader supplied.
enum class OpticsVariable : uint8_t {
    IndexIncident,
    SineIncident,
    IndexTransmitted,
    SineTransmitted,
    FocalLength,
    ObjectDistance,
    ImageDistance,
    SlitSpacing,
    SineFringe,
    FringeOrder,
    Wavelength,
};

const char *optics_variable_name(OpticsVariable variable);

struct OpticsKnown {
    OpticsVariable variable = OpticsVariable::IndexIncident;
    Quantity quantity;
};

struct OpticsProblem {
    OpticsRelation relation = OpticsRelation::Refraction;
    OpticsVariable unknown = OpticsVariable::SineTransmitted;
    std::vector<OpticsKnown> knowns;
};

enum class OpticsOutcome : uint8_t {
    Solved,
    // Not a failure. The refraction law has no transmitted ray, and the critical sine is reported.
    TotalInternalReflection,
    InvalidProblem,
    MissingKnown,
    DuplicateKnown,
    DimensionMismatch,
    // A domain rule this family states: a sine outside [-1, 1], an index below one, a
    // non-positive length or wavelength, or a fringe order that is not an integer.
    UnphysicalValue,
    // The relation does not determine the unknown, as when a lens is asked for its focal length
    // from two distances that sum to zero.
    Indeterminate,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *optics_outcome_name(OpticsOutcome outcome);

struct OpticsResult {
    OpticsOutcome outcome = OpticsOutcome::InvalidProblem;
    Quantity quantity;
    std::string value_text;
    std::string unit_text;
    std::string detail;
    // The declared sign or order convention the answer is read under. Visible rather than baked in.
    std::string convention;
    // Lateral magnification -di/do, filled for the lens and mirror relations only.
    bool has_magnification = false;
    std::string magnification_text;
    // sin of the critical angle, n2/n1, filled only when n2 <= n1 and a critical angle exists.
    bool has_critical_sine = false;
    std::string critical_sine_text;
    NodeId value = kNoNode;
    NodeId unknown = kNoNode;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

OpticsResult solve_optics(Arena &arena, Derivation &derivation, const OpticsProblem &problem,
                          const Budget &budget = Budget());

}  // namespace nps

#endif
