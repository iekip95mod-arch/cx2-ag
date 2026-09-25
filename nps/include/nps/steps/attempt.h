#ifndef NPS_ATTEMPT_H
#define NPS_ATTEMPT_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"

namespace nps {

// STEP-013's first half. Whether a transformation the student wrote keeps the meaning of the state
// it was written against. Proof and corroboration stay apart, because PRD section 17 forbids
// reporting a sample agreement as an identity.
enum class AttemptEquivalence : uint8_t {
    Equivalent,
    // Agreed at every sample that had a value, which is evidence rather than proof.
    Corroborated,
    NotEquivalent,
    // Nothing this engine can decide either way, which is not a verdict against the student.
    NotComparable,
    Cancelled,
    ResourceExceeded,
};

const char *attempt_equivalence_name(AttemptEquivalence e);

// STEP-014. A valid move can still go nowhere, so usefulness is judged apart from validity and only
// once the move is known or corroborated to be valid.
enum class AttemptUsefulness : uint8_t {
    // Lands on a state the recorded route passes through later.
    Advances,
    // The same state it was written against, in another spelling or none.
    NoProgress,
    // Valid, and not a state on the recorded route.
    ValidNotOnRoute,
    NotJudged,
};

const char *attempt_usefulness_name(AttemptUsefulness u);

struct AttemptVerdict {
    AttemptEquivalence equivalence = AttemptEquivalence::NotComparable;
    // The same scale the derivation's own checks use, so sample agreement reads as corroboration.
    EvidenceStrength strength = EvidenceStrength::Unsupported;
    std::string method;
    std::string detail;
    AttemptUsefulness usefulness = AttemptUsefulness::NotJudged;
    // How many route states the attempt reaches, counted from one. Zero unless it advances.
    size_t reaches = 0;
};

// Judges an attempt against the state it was written from and the states the recorded derivation
// passes through after it. Every node belongs to the one arena. The verdict is a record of its own
// and never touches a derivation, so a student's move cannot turn a failed obligation into a passed
// one, which is VER-019.
AttemptVerdict judge_attempt(Arena &arena, NodeId current, NodeId attempt, NodeId variable,
                             const std::vector<NodeId> &route, const Budget &budget = Budget());

}  // namespace nps

#endif
