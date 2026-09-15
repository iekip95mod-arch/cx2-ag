#ifndef NPS_PHYSICS_GRAVITATION_H
#define NPS_PHYSICS_GRAVITATION_H

#include <cstdint>

#include "nps/physics/relation.h"

namespace nps {

// The four positions in Newton's law of gravitation, in the order the model declares them.
enum class GravitationVariable : uint8_t {
    Force = 0,
    FirstMass = 1,
    SecondMass = 2,
    Separation = 3,
};

const char *gravitation_variable_name(GravitationVariable variable);

const RelationModel &gravitation_model();

RelationKnown gravitation_known(GravitationVariable variable, const Quantity &quantity);

RelationProblem gravitation_problem(GravitationVariable unknown);

RelationResult solve_gravitation(Arena &arena, Derivation &derivation,
                                 const RelationProblem &problem, const Budget &budget = Budget());

}  // namespace nps

#endif
