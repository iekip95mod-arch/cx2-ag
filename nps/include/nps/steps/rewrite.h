#ifndef NPS_REWRITE_H
#define NPS_REWRITE_H

#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class RewriteGoal : uint8_t {
    // ALG-001: work the arithmetic out and gather what is alike, one operation to a step.
    Simplify,
    // ALG-002's first two verbs. Expanding ends by collecting, because a distributed product with
    // its like terms left apart is not what expanding is for.
    Expand,
    // ALG-002's third verb.
    Factor,
};

const char *rewrite_goal_name(RewriteGoal g);

enum class RewriteOutcome : uint8_t {
    Rewritten,
    // Nothing to do: the expression is already in the form that was asked for.
    AlreadyInForm,
    UnsupportedForm,
    VerificationFailed,
    Refused,
    Cancelled,
    ResourceExceeded,
};

const char *rewrite_outcome_name(RewriteOutcome o);

struct RewriteResult {
    RewriteOutcome outcome = RewriteOutcome::Refused;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// ALG-001 and ALG-002. Rewrites an expression towards the requested form, one named rule to a step:
// each arithmetic operation worked out on its own, each distribution of a product over a sum, each
// gathering of like terms, and each factor taken out.
//
// The rules generate the answer forwards rather than checking one somebody else produced, which is
// what STEP-023 asks for and what stops a backend result being dressed up as a walkthrough. Giac is
// an optional second opinion on the finished expression and can only fail it.
//
// Exact numbers only. A decimal literal is refused rather than turned into a fraction, because
// choosing between 3/2 and 1.5 is a presentation mode this engine does not own.
//
// Factoring covers what a course covers: a common factor, then a monic quadratic in one variable by
// product and sum, with the difference of two squares named as itself. Anything else is refused
// rather than approximated.
RewriteResult rewrite(Arena &arena, Derivation &derivation, NodeId expression, RewriteGoal goal,
                      const Budget &budget = Budget(), Backend *giac = nullptr);

}  // namespace nps

#endif
