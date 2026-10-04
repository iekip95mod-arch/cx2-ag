#ifndef NPS_PHYSICS_FLUIDS_H
#define NPS_PHYSICS_FLUIDS_H

#include "nps/physics/relation.h"

namespace nps {

// PHYS-018's fluid relations that are products of powers, each solved by the shared relation engine.
const RelationModel &pressure_model();
const RelationModel &hydrostatic_model();
const RelationModel &buoyancy_model();
const RelationModel &continuity_model();

RelationResult solve_pressure(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                              const Budget &budget = Budget());
RelationResult solve_hydrostatic(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                 const Budget &budget = Budget());
RelationResult solve_buoyancy(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                              const Budget &budget = Budget());
RelationResult solve_continuity(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                                const Budget &budget = Budget());

}  // namespace nps

#endif
