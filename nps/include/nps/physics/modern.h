#ifndef NPS_MODERN_H
#define NPS_MODERN_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// PRD section 9, PHYS-024. The introductory modern-physics relations whose method is an exact
// linear derivation over the electronvolt and nanometre working units this family declares.
enum class ModernRelation : uint8_t {
    // E * lambda = hc, which is the photon energy of a wavelength and the de Broglie wavelength of
    // a momentum written as pc in the same electronvolt units.
    PhotonWavelength,
    // Kmax = E - phi, the photoelectric effect above the threshold.
    Photoelectric,
    // E = dm * c^2 written as the introductory 931.49 MeV per atomic mass unit.
    MassEnergy,
};

// The typed positions, three per relation. Which three depends on the relation.
enum class ModernVariable : uint8_t {
    PhotonEnergy,
    Wavelength,
    KineticEnergy,
    WorkFunction,
    MassDefect,
    RestEnergy,
};

const char *modern_relation_name(ModernRelation relation);
const char *modern_variable_name(ModernVariable variable);

// The unit each variable is declared in, as text. This family does not convert to SI: a photon
// energy in joules does not fit the exact integer rationals every other step here relies on.
const char *modern_variable_unit(ModernVariable variable);
Dimension modern_variable_dimension(ModernVariable variable);

bool modern_relation_has(ModernRelation relation, ModernVariable variable);

struct ModernKnown {
    ModernVariable variable = ModernVariable::PhotonEnergy;
    Quantity quantity;
};

struct ModernProblem {
    ModernRelation relation = ModernRelation::PhotonWavelength;
    ModernVariable unknown = ModernVariable::PhotonEnergy;
    std::vector<ModernKnown> knowns;
};

enum class ModernOutcome : uint8_t {
    Solved,
    InvalidProblem,
    MissingKnown,
    DuplicateKnown,
    DimensionMismatch,
    // A source-backed domain rule: an energy, a wavelength or a work function that is not positive,
    // and a photoelectric photon below the threshold, which emits nothing rather than a negative
    // kinetic energy.
    UnphysicalValue,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *modern_outcome_name(ModernOutcome outcome);

struct ModernResult {
    ModernOutcome outcome = ModernOutcome::InvalidProblem;
    Quantity quantity;
    std::string value_text;
    std::string unit_text;
    std::string detail;
    NodeId value = kNoNode;
    NodeId unknown = kNoNode;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

ModernResult solve_modern(Arena &arena, Derivation &derivation, const ModernProblem &problem,
                          const Budget &budget = Budget());

}  // namespace nps

#endif
