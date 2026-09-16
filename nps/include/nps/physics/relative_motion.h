#ifndef NPS_PHYSICS_RELATIVE_MOTION_H
#define NPS_PHYSICS_RELATIVE_MOTION_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
#include "nps/physics/vector_components.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

enum class RelativeMotionAxes : uint8_t {
    EastNorth,
};

const char *relative_motion_axes_name(RelativeMotionAxes axes);

// Which of the three velocities in v(A/C) = v(A/B) + v(B/C) a problem is solving for. The frames
// are the subject A, the medium B it moves through, and the reference C the answer is quoted in.
enum class RelativeMotionUnknown : uint8_t {
    SubjectRelativeToReference,
    SubjectRelativeToMedium,
    MediumRelativeToReference,
};

const char *relative_motion_unknown_name(RelativeMotionUnknown unknown);

struct RelativeMotionProblem {
    std::string subject_name;
    std::string reference_name;
    Vector subject_velocity;
    Vector reference_velocity;
    RelativeMotionAxes axes = RelativeMotionAxes::EastNorth;
};

enum class RelativeMotionOutcome : uint8_t {
    Solved,
    InvalidProblem,
    RankMismatch,
    FrameUndeclared,
    FrameMismatch,
    DimensionMismatch,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *relative_motion_outcome_name(RelativeMotionOutcome outcome);

enum class RelativeDirection : uint8_t {
    Stationary,
    East,
    West,
    North,
    South,
    Northeast,
    Northwest,
    Southeast,
    Southwest,
};

const char *relative_direction_name(RelativeDirection direction);

// A direction written the way a marker writes it, as a magnitude and an angle turned from the
// nearest cardinal. Twenty two point two degrees south of west and sixty seven point eight degrees
// west of south are the same vector, so the reference cardinal and the cardinal the angle turns
// toward are both recorded rather than left to be read out of the number.
struct RelativeBearing {
    bool has_bearing = false;
    RelativeDirection reference = RelativeDirection::Stationary;
    RelativeDirection sense = RelativeDirection::Stationary;
    NodeId magnitude = kNoNode;
    NodeId angle = kNoNode;
    AngleUnit angle_unit = AngleUnit::Degrees;
    std::string convention;
    std::string text;
    Cost cost;
};

struct RelativeMotionResult {
    RelativeMotionOutcome outcome = RelativeMotionOutcome::InvalidProblem;
    Vector velocity;
    bool has_value = false;
    RelativeDirection direction = RelativeDirection::Stationary;
    RelativeBearing bearing;
    std::string value_text;
    std::string interpretation;
    std::string detail;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// The three-frame statement. Two of the three velocities are read and the third is the unknown,
// so the arrangement is a property of the problem rather than of the family.
struct RelativeMotionIdentity {
    std::string subject_name;
    std::string medium_name;
    std::string reference_name;
    Vector subject_relative_to_medium;
    Vector medium_relative_to_reference;
    Vector subject_relative_to_reference;
    RelativeMotionUnknown unknown = RelativeMotionUnknown::SubjectRelativeToReference;
    RelativeMotionAxes axes = RelativeMotionAxes::EastNorth;
};

// The magnitude and the bearing for a solved relative velocity. The reference cardinal is the axis
// carrying the larger component and the trigonometry is components_to_magnitude_angle's, so what
// this adds is the choice of cardinal and the wording. The identity solver calls it for its answer
// whenever a backend is supplied; the two-frame entry point leaves the bearing unset so its
// existing backend traffic does not change.
RelativeBearing relative_motion_bearing(Arena &arena, Derivation &derivation, const Vector &velocity,
                                        AngleUnit angle_unit, Backend &giac,
                                        RelativeMotionAxes axes = RelativeMotionAxes::EastNorth,
                                        const Budget &budget = Budget());

RelativeMotionResult solve_relative_motion_identity(Arena &arena, Derivation &derivation,
                                                    const RelativeMotionIdentity &problem,
                                                    const Budget &budget = Budget(),
                                                    Backend *giac = nullptr);

RelativeMotionResult solve_relative_motion(Arena &arena, Derivation &derivation,
                                            const RelativeMotionProblem &problem,
                                            const Budget &budget = Budget(),
                                            Backend *giac = nullptr);

}  // namespace nps

#endif
