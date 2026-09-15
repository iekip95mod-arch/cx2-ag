#ifndef NPS_PHYSICS_RELATIVITY_H
#define NPS_PHYSICS_RELATIVITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

// PRD section 9, PHYS-023. Introductory special relativity between two named inertial frames whose
// relative motion is along their shared x axis. Every relation here names both frames and the
// direction the boost points, because a relativistic answer without its frame is not an answer.
enum class RelativityRelation : uint8_t {
    // dt = gamma * dt0, the proper time of the moving frame read in the rest frame.
    TimeDilation,
    // L = L0 / gamma, the proper length of the moving frame measured in the rest frame.
    LengthContraction,
    // x' = gamma (x - beta c t) and c t' = gamma (c t - beta x), one event in both frames.
    LorentzTransformation,
    // beta = (beta' + beta_boost) / (1 + beta' beta_boost), one object's velocity in both frames.
    VelocityAddition,
    // E = gamma E0 with pc = gamma beta E0, the energy and momentum of a moving particle.
    EnergyMomentum,
};

// The typed positions this family reads and reports. Which ones a relation uses is fixed per
// relation, and each carries the frame it is stated in rather than leaving that to the caller.
enum class RelativityVariable : uint8_t {
    ProperTime,
    DilatedTime,
    ProperLength,
    ContractedLength,
    EventPosition,
    EventTime,
    TransformedPosition,
    TransformedTime,
    ObjectVelocity,
    TransformedVelocity,
    RestEnergy,
    TotalEnergy,
    MomentumEnergy,
    KineticEnergy,
};

const char *relativity_relation_name(RelativityRelation relation);
const char *relativity_relation_written(RelativityRelation relation);
const char *relativity_variable_name(RelativityVariable variable);
const char *relativity_variable_symbol(RelativityVariable variable);

// The unit each variable is declared in, as text. Velocities are fractions of c and energies are
// megaelectronvolts, because the SI value of either does not fit the exact rationals every step
// here compares with.
const char *relativity_variable_unit(RelativityVariable variable);
Dimension relativity_variable_dimension(RelativityVariable variable);

// Which frame a variable is stated in. A proper time belongs to the moving frame that carries the
// clock, and the time the rest frame reads for the same pair of events does not.
bool relativity_variable_in_moving_frame(RelativityVariable variable);

bool relativity_relation_reads(RelativityRelation relation, RelativityVariable variable);
bool relativity_relation_reports(RelativityRelation relation, RelativityVariable variable);

struct RelativityKnown {
    RelativityVariable variable = RelativityVariable::ProperTime;
    Quantity quantity;
};

struct RelativityProblem {
    RelativityRelation relation = RelativityRelation::TimeDilation;
    // The frame the boost is measured against, and the frame that moves at that boost. Both are
    // required, and they cannot be the same frame.
    std::string rest_frame_name;
    std::string moving_frame_name;
    // The velocity of the moving frame along the shared positive x axis of the rest frame, as a
    // fraction of c. A negative value points along the negative x axis and is accepted.
    Quantity boost;
    std::vector<RelativityKnown> knowns;
};

enum class RelativityOutcome : uint8_t {
    Solved,
    InvalidProblem,
    MissingKnown,
    DuplicateKnown,
    FrameUndeclared,
    FrameMismatch,
    DimensionMismatch,
    // |beta| is at or above one, which no massive frame or particle reaches.
    SuperluminalSpeed,
    // 1 - beta^2 is not the square of an exact fraction, so gamma has no exact rational value and
    // this family reports nothing rather than a decimal nothing checked.
    InexactLorentzFactor,
    // A proper time, a proper length or a rest energy that is not positive.
    UnphysicalValue,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *relativity_outcome_name(RelativityOutcome outcome);

struct RelativityOutput {
    RelativityVariable variable = RelativityVariable::DilatedTime;
    Quantity quantity;
    std::string value_text;
    std::string unit_text;
    std::string frame;
};

struct RelativityResult {
    RelativityOutcome outcome = RelativityOutcome::InvalidProblem;
    std::vector<RelativityOutput> outputs;
    Rational lorentz_factor;
    bool has_factor = false;
    std::string factor_text;
    // The frame and sign convention this answer is stated under, in words.
    std::string convention;
    std::string detail;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

RelativityResult solve_relativity(Arena &arena, Derivation &derivation,
                                  const RelativityProblem &problem, const Budget &budget = Budget());

}  // namespace nps

#endif
