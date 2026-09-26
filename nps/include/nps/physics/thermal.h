#ifndef NPS_PHYSICS_THERMAL_H
#define NPS_PHYSICS_THERMAL_H

#include "nps/physics/relation.h"

namespace nps {

// PHYS-018's heat and gas relations that are products of powers, solved by the shared relation engine.
const RelationModel &sensible_heat_model();
const RelationModel &latent_heat_model();
const RelationModel &ideal_gas_model();

RelationResult solve_sensible_heat(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                   const Budget &budget = Budget());
RelationResult solve_latent_heat(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                 const Budget &budget = Budget());
RelationResult solve_ideal_gas(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                               const Budget &budget = Budget());

}  // namespace nps

#endif
