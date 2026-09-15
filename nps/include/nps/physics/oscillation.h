#ifndef NPS_PHYSICS_OSCILLATION_H
#define NPS_PHYSICS_OSCILLATION_H

#include <cstdint>

#include "nps/physics/relation.h"

namespace nps {

// Simple harmonic motion through its defining linear restoring force.
enum class OscillationVariable : uint8_t {
    RestoringForce = 0,
    Stiffness = 1,
    Displacement = 2,
};

// The basic mechanical wave relation v = f*lambda.
enum class WaveVariable : uint8_t {
    Speed = 0,
    Frequency = 1,
    Wavelength = 2,
};

const char *oscillation_variable_name(OscillationVariable variable);
const char *wave_variable_name(WaveVariable variable);

const RelationModel &oscillation_model();
const RelationModel &wave_model();

RelationKnown oscillation_known(OscillationVariable variable, const Quantity &quantity);
RelationKnown wave_known(WaveVariable variable, const Quantity &quantity);

RelationProblem oscillation_problem(OscillationVariable unknown);
RelationProblem wave_problem(WaveVariable unknown);

RelationResult solve_oscillation(Arena &arena, Derivation &derivation,
                                 const RelationProblem &problem, const Budget &budget = Budget());

RelationResult solve_wave(Arena &arena, Derivation &derivation, const RelationProblem &problem,
                          const Budget &budget = Budget());

}  // namespace nps

#endif
