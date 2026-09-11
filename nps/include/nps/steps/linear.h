#ifndef NPS_LINEAR_H
#define NPS_LINEAR_H

#include <vector>

#include "nps/core/ast.h"
#include "nps/core/task.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

enum class SolveOutcome : uint8_t {
    Solved,
    NoSolution,
    AllValues,
    NotLinear,
    NotAnEquation,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *solve_outcome_name(SolveOutcome o);

struct SolveResult {
    SolveOutcome outcome = SolveOutcome::Refused;
    NodeId solution = kNoNode;
    std::string detail;
    // Section 15's outcome, stated by the rule that decided it rather than inferred from the
    // outcome later. It is what the derivation's SolutionContext records.
    DerivationStatus status = DerivationStatus::NotRecorded;
    // What it spent, for the budgets PERF-010 freezes.
    Cost cost;
};

struct LinearKnown {
    std::string symbol;
    Rational value;
    Precision precision;
};

bool linear_solution_precision(const Arena &arena, NodeId equation, NodeId unknown,
                               const std::vector<LinearKnown> &knowns, Rational *value,
                               Precision *precision);

enum class LinearForm : uint8_t {
    Reduced,
    NotAnEquation,
    // Degree one in the subject is what this rule reads, and an equation that is not is refused
    // rather than approximated. Kept apart from Overflowed because one is a shape and the other is
    // a limit, and they reach different section 15 statuses.
    NotLinear,
    Overflowed,
    Halted,
};

// The pair a and b for which an equation reads a*subject + b = 0, in exact rationals. The subject is
// any node rather than only a symbol, which is the whole reason this is exposed: an engine that
// branches has to reach a solved form first, and the square-root rule reads its equation as linear
// in x^2 rather than in x. The arena interns, so the caller builds the subject node and gets the one
// already in the equation.
LinearForm linear_form(const Arena &arena, NodeId equation, NodeId subject, Meter &meter,
                       Rational *coefficient, Rational *constant);

// Solves a linear equation in one unknown by inverse operations, recording every move as a Step.
// The derivation is the product here and the answer is a by-product, which is why this writes into a
// Derivation rather than returning a number.
//
// It refuses anything it cannot do rather than approximating, because PRD section 17 says an
// unsupported technique has to stop explicitly instead of producing something that looks like a
// solution.
//
// The budget is PERF-003's cancellation and PERF-008's step limit. Halting keeps the run of checked
// steps and labels it with the outcome rather than rewinding, which is how PERF-009's requirement
// that nothing partial is left behind is met: a checked prefix is not a partial derivation. A halt
// that lands before anything was checked keeps nothing, which is the same rule and not a second one.
// Borrowed state must outlive the coroutine, including every suspension.
Coroutine<SolveResult> solve_linear_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                        NodeId equation, NodeId unknown, Meter &meter, Budget budget);

SolveResult solve_linear(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                         const Budget &budget = Budget());
SolveResult solve_linear(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                         const Budget &budget, size_t frame_bytes);

}  // namespace nps

#endif
