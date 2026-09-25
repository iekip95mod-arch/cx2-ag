#ifndef NPS_QUADRATIC_H
#define NPS_QUADRATIC_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/rational.h"
#include "nps/steps/derivation.h"

namespace nps {

// Whether a set of case values is every real root of x^2 = square. This is VER-017's completeness
// half as a predicate, exposed rather than kept inside the rule for two reasons: the invariant pass
// can only ask whether a claim is backed by a check, never whether the check was right, so the
// arithmetic that decides it has to be testable on its own; and a monic polynomial is its roots, so
// nothing here is particular to degree two except how many cases are expected, which is where a
// later split with more of them attaches rather than writing a second predicate.
//
// A single case stands for a repeated root, which is why one case can be complete for a square of
// zero and cannot be complete for a square of four.
// Rebuilt and Missing are the two answers the predicate exists to give. OutOfRoom is the third thing
// that can happen to it, this build's exact arithmetic running out before it decided either, and it
// is separate because a check that never finished is not a case that is missing.
enum class Reconstruction : uint8_t { Rebuilt, OutOfRoom, Missing };

Reconstruction cases_reconstruct_the_square(const std::vector<Rational> &roots,
                                            const Rational &square, std::string *why);

// The same identity with a linear term in it, which the predicate above is the zero case of.
Reconstruction cases_reconstruct_the_monic(const std::vector<Rational> &roots, const Rational &linear,
                                           const Rational &constant, std::string *why);

enum class QuadraticOutcome : uint8_t {
    Solved,
    // The equation holds for no real value, which is an answer rather than a refusal: x^2 = -4 has
    // an empty solution set over the reals and the derivation says so with evidence.
    NoRealSolution,
    NotPureQuadratic,
    NotAnEquation,
    // Degree two with no linear term, but the square is not a rational whose square root is exact.
    // Inside the family and outside the envelope, which VER-017 asks to be two different answers.
    OutsideEnvelope,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *quadratic_outcome_name(QuadraticOutcome o);

struct QuadraticResult {
    QuadraticOutcome outcome = QuadraticOutcome::Refused;
    // Every root, in the order the cases were recorded. Empty for an empty solution set, which is
    // why the outcome rather than this decides whether the engine got anywhere.
    std::vector<NodeId> solutions;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// Solves an equation of degree two with no term of degree one, by isolating the square and splitting
// on the two roots. The first rule in this engine that branches, so it is the first that has to
// answer STEP-024: the split records whether its cases are exhaustive, mutually exclusive and
// domain-consistent, and no case is left unresolved.
//
// The envelope is rational perfect squares over the reals, declared rather than assumed: an equation
// whose square is 5 is refused as outside it rather than answered with a decimal, which is what
// VER-017's "within the declared family envelope" asks for and what section 17 asks of every
// unsupported technique.
//
// Soundness is per case, by substituting the candidate back into the original equation.
// Completeness is checked over the recorded cases rather than over what the rule meant to record:
// the monic quadratic rebuilt from them has to be the one that was solved.
QuadraticResult solve_by_square_root(Arena &arena, Derivation &derivation, NodeId equation,
                                     NodeId unknown, const Budget &budget = Budget());

// The quadratic formula, for the equations with a term of degree one the rule above refuses.
QuadraticResult solve_quadratic(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                                const Budget &budget = Budget());

// ALG-004 by factoring, refusing before any step when no integer pair has product a*c and sum b.
QuadraticResult solve_by_factoring(Arena &arena, Derivation &derivation, NodeId equation,
                                   NodeId unknown, const Budget &budget = Budget());

}  // namespace nps

#endif
