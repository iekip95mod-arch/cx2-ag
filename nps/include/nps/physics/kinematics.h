#ifndef NPS_KINEMATICS_H
#define NPS_KINEMATICS_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/cas/giac_adapter.h"
#include "nps/units/units.h"

namespace nps {

// PRD section 13: structured entry rather than prose. The symbols are the textbook ones for
// motion along one axis under constant acceleration: v0, v, a, t and x.
struct Known {
    std::string symbol;
    Quantity quantity;
};

struct KinematicsProblem {
    std::string unknown;
    std::vector<Known> knowns;
};

// "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s". Parts are separated by semicolons, commas or new
// lines; the unknown is "find v", "unknown v" or "v = ?"; every other part is "symbol = value
// unit". Refuses with a message that names the part it could not read.
bool parse_kinematics(const std::string &text, KinematicsProblem *out, std::string *error);

enum class KinematicsOutcome : uint8_t {
    Solved,
    NoSolution,
    InvalidInput,
    NoApplicableEquation,
    DimensionMismatch,
    VerificationFailed,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *kinematics_outcome_name(KinematicsOutcome o);

struct KinematicsResult {
    KinematicsOutcome outcome = KinematicsOutcome::Refused;
    // The answer in SI, as text a reader expects (17 or 4.5) and the unit it is in.
    std::string value_text;
    std::string unit_text;
    Precision precision;
    NodeId value = kNoNode;
    NodeId unknown = kNoNode;
    // The last hop's equation, symbolic, and the same with the SI values in: the second is what the
    // linear solver was handed, and what an independent engine can solve to cross-check. Earlier
    // hops are in the derivation rather than here, since only the last one produces the unknown.
    NodeId equation = kNoNode;
    // Giac's symbolic rearrangement, when a backend was given and it answered exactly.
    NodeId isolated = kNoNode;
    NodeId substituted = kNoNode;
    std::string answer_candidate_detail;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// Milestone 4's vertical slice, with PRD section 15's planner. Every quantity is converted to SI,
// then a backward chain from the unknown looks for a route: each candidate equation is offered to
// the linear solver on a scratch derivation, and a branch is dropped when it needs more quantities
// than there are hops left. The search deepens one hop at a time, so a shorter route always wins,
// and every equation it passed over is recorded in the plan with the solver's own reason.
//
// Each hop is then recorded in full: a dimensional check of the symbolic equation, Giac's symbolic
// rearrangement when a backend is given (checked against the solver's value), the substitution, and
// the linear solver's own derivation, checks included. An intermediate joins the known quantities,
// so the next hop substitutes it exactly as it substitutes a given. Nothing here isolates or
// evaluates on its own: the engines that already do that do it here.
KinematicsResult solve_kinematics(Arena &arena, Derivation &derivation,
                                  const KinematicsProblem &problem,
                                  const Budget &budget = Budget(), Backend *giac = nullptr);

}  // namespace nps

#endif
