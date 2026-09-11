#ifndef NPS_VECTOR_COMPONENTS_H
#define NPS_VECTOR_COMPONENTS_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

enum class AngleUnit : uint8_t {
    Radians,
    Degrees,
};

// Component IDs are valid only for the lifetime of the Arena that created them.
struct VectorExpr {
    NodeId x = kNoNode;
    NodeId y = kNoNode;
    NodeId z = kNoNode;
    uint8_t rank = 2;
    Frame frame;
    Unit unit;
    Precision precision;
};

struct MagnitudeAngleExpr {
    NodeId magnitude = kNoNode;
    NodeId angle = kNoNode;
    AngleUnit angle_unit = AngleUnit::Radians;
    Frame frame;
    Unit unit;
    Precision precision;
};

enum class VectorComponentsOutcome : uint8_t {
    Solved,
    InvalidInput,
    BackendFailure,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *vector_components_outcome_name(VectorComponentsOutcome outcome);

struct VectorComponentsResult {
    VectorComponentsOutcome outcome = VectorComponentsOutcome::InvalidInput;
    VectorExpr components;
    MagnitudeAngleExpr polar;
    bool has_components = false;
    bool has_polar = false;
    std::string detail;
    DerivationStatus status = DerivationStatus::InvalidInput;
    Cost cost;
};

VectorExpr vector_expr_from_exact(Arena &arena, const Vector &value);

VectorComponentsResult magnitude_angle_to_components(
    Arena &arena, Derivation &derivation, const MagnitudeAngleExpr &input, Backend &backend,
    const Budget &budget = Budget());

VectorComponentsResult components_to_magnitude_angle(
    Arena &arena, Derivation &derivation, const VectorExpr &input, AngleUnit output_unit,
    Backend &backend, const Budget &budget = Budget());

VectorComponentsResult components_to_magnitude_angle(
    Arena &arena, Derivation &derivation, const Vector &input, AngleUnit output_unit,
    Backend &backend, const Budget &budget = Budget());

}  // namespace nps

#endif
