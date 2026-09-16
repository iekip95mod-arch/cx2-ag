#ifndef NPS_PHYSICS_CIRCULAR_MOTION_H
#define NPS_PHYSICS_CIRCULAR_MOTION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "nps/physics/relation.h"

namespace nps {

// The three quantities uniform circular motion relates by T = 2*pi*r/v. Acceleration is not a
// choice here: it is always reported alongside whichever of these three was asked for.
enum class CircularMotionVariable : uint8_t {
    Speed = 0,
    Radius = 1,
    Period = 2,
};

const char *circular_motion_variable_name(CircularMotionVariable variable);

struct CircularMotionKnown {
    CircularMotionVariable variable = CircularMotionVariable::Speed;
    Quantity quantity;
};

struct CircularMotionProblem {
    // Must be one of Speed, Radius or Period. The other two are given as knowns.
    CircularMotionVariable unknown = CircularMotionVariable::Period;
    CircularMotionKnown knowns[2];
    size_t known_count = 0;
};

struct CircularMotionResult {
    RelationOutcome outcome = RelationOutcome::InvalidProblem;
    DerivationStatus status = DerivationStatus::NotRecorded;
    std::string detail;
    // The requested one of speed, radius or period.
    RelationResult unknown_result;
    // The centripetal acceleration, computed from speed and radius once both are known.
    RelationResult acceleration_result;
};

CircularMotionResult solve_circular_motion(Arena &arena, Derivation &derivation,
                                           const CircularMotionProblem &problem,
                                           const Budget &budget = Budget());

}  // namespace nps

#endif
