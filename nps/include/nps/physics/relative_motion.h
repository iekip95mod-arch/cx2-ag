#ifndef NPS_PHYSICS_RELATIVE_MOTION_H
#define NPS_PHYSICS_RELATIVE_MOTION_H

#include <cstdint>
#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/budgets.h"
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

struct RelativeMotionResult {
    RelativeMotionOutcome outcome = RelativeMotionOutcome::InvalidProblem;
    Vector velocity;
    bool has_value = false;
    RelativeDirection direction = RelativeDirection::Stationary;
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
