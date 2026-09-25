#ifndef NPS_STEPS_SYSTEM_H
#define NPS_STEPS_SYSTEM_H

#include <span>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/rational.h"
#include "nps/steps/derivation.h"

namespace nps {

// The augmented matrix has to fit the 4 by 6 envelope the matrix rules already verify.
inline constexpr size_t kSystemMaxEquations = 4;
inline constexpr size_t kSystemMaxUnknowns = 5;

enum class SystemOutcome : uint8_t {
    Solved,
    NoSolution,
    // Infinitely many solutions, written with the free unknowns as their own parameters.
    Family,
    NotLinear,
    InvalidInput,
    OutsideEnvelope,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *system_outcome_name(SystemOutcome outcome);

struct SystemResult {
    SystemOutcome outcome = SystemOutcome::OutsideEnvelope;
    // One equation per unknown in the order the unknowns were given. Empty for no solution.
    std::vector<NodeId> solutions;
    // The same equations as one list, which is what the bridge prints.
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

enum class LinearRowRead : uint8_t { Read, NotLinear, OtherSymbol, Inexact, Overflowed, Halted };

// Reads a1 x1 + ... + an xn = b into row, which holds the n coefficients and then b.
LinearRowRead read_linear_row(const Arena &arena, NodeId equation, std::span<const NodeId> unknowns,
                              Meter &meter, std::span<Rational> row);

// ALG-013. Solves a list of linear equations in a list of unknowns by Gauss-Jordan elimination on the
// augmented matrix, and checks every answer in the equations as they were typed.
SystemResult solve_linear_system(Arena &arena, Derivation &derivation, NodeId equations,
                                 NodeId unknowns, const Budget &budget = Budget());

}  // namespace nps

#endif
