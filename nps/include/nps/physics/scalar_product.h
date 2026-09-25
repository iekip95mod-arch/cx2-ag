#ifndef NPS_SCALAR_PRODUCT_H
#define NPS_SCALAR_PRODUCT_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/physics/vector_components.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// The angle is asked for separately because it needs both operands to be non-zero and the product
// does not, so a zero operand refuses one question without refusing the other.
struct ScalarProductProblem {
    Vector first;
    Vector second;
    bool angle = false;
    AngleUnit angle_unit = AngleUnit::Degrees;
};

enum class ScalarProductOutcome : uint8_t {
    Solved,
    RankMismatch,
    FrameMismatch,
    DimensionMismatch,
    ZeroVector,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *scalar_product_outcome_name(ScalarProductOutcome outcome);

// Where the angle falls, as far as an exact rational dot product can say without a square root.
enum class ScalarAngle : uint8_t {
    NotAsked,
    Acute,
    Perpendicular,
    Obtuse,
};

const char *scalar_angle_name(ScalarAngle angle);

struct ScalarProductResult {
    ScalarProductOutcome outcome = ScalarProductOutcome::ResourceExceeded;
    Quantity value;
    bool has_value = false;
    std::string value_text;
    ScalarAngle angle = ScalarAngle::NotAsked;
    std::string angle_text;
    // The sign reading above is exact and always there. The number needs a backend, so it is a
    // separate fact rather than another spelling of the same one.
    NodeId numeric_angle = kNoNode;
    std::string numeric_angle_text;
    bool has_numeric_angle = false;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

ScalarProductResult solve_scalar_product(Arena &arena, Derivation &derivation,
                                         const ScalarProductProblem &problem,
                                         const Budget &budget = Budget(), Backend *giac = nullptr);

}  // namespace nps

#endif
