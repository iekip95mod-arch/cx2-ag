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

// Every repeated factor written as the power it is, recorded as one step. *out is the expression to
// carry on with, and false means the meter stopped rather than that nothing matched.
//
// Shared because the canonical form deliberately leaves x * x as a product, so any engine needing
// x * x and x^2 to be one form has to ask. A negative exponent is left alone, which is what keeps
// x * x^-1 the product it is written as, along with the non-zero condition it carries.
bool gather_repeated_factors(Arena &arena, Derivation &derivation, StepId parent, const char *phase,
                             Meter &meter, NodeId expression, NodeId *out);

// A rule's rewrite of one node, or kNoNode when it has nothing to do there, with what it did.
using SubtermRule = NodeId (*)(Arena &arena, NodeId id, void *state, std::string *what);

// The first node, outermost first, where the rule applies, rewritten inside the whole expression.
// kNoNode when the rule applies nowhere. The same walk the rewrite rules use, shared rather than copied.
NodeId rewrite_first_subterm(Arena &arena, NodeId expression, SubtermRule rule, void *state,
                             std::string *what);

}  // namespace nps

#endif
