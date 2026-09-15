#ifndef NPS_PHYSICS_ALGEBRA_FIRST_H
#define NPS_PHYSICS_ALGEBRA_FIRST_H

#include <string>
#include <utility>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/steps/rearrange.h"

namespace nps {
namespace physics {

// The marker isolates the unknown while the equation is still symbolic and only then puts numbers
// in, so the algebra is on the page as steps rather than implied by the answer. ALG-007 already
// undoes one operation at a time and records each move, so the moves are borrowed from it here
// instead of a second rearranger being written inside the physics tree.
struct IsolationRecord {
    bool recorded = false;
    NodeId isolated = kNoNode;
    size_t moves = 0;
    bool halted = false;
    RearrangeOutcome outcome = RearrangeOutcome::Refused;
};

// Recorded with no backend on purpose. The step this replaces existed only when Giac answered, and
// the walkthrough that runs without one is the walkthrough being graded.
inline IsolationRecord record_symbolic_isolation(Arena &arena, Derivation &derivation, StepId parent,
                                                 NodeId equation, NodeId unknown,
                                                 const Budget &budget, Meter &meter) {
    IsolationRecord out;
    if (equation == kNoNode || unknown == kNoNode)
        return out;
    // ALG-007 opens a plan of its own at the root, and a physics derivation already has one, so the
    // rearrangement is recorded aside and its moves are adopted under the plan that asked for them.
    Derivation aside;
    aside.request = derivation.request;
    const RearrangeResult r = rearrange(arena, aside, equation, unknown, budget, nullptr);
    out.outcome = r.outcome;
    if (r.outcome != RearrangeOutcome::Isolated || r.expression == kNoNode)
        return out;
    for (size_t i = 0; i < aside.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        const TransformationPayload *payload = aside.transformation(id);
        if (!payload)
            continue;
        // Spent on the caller's meter as it is adopted, so one cap covers the whole solve rather
        // than the isolation running on a budget of its own beside it.
        if (!meter.step()) {
            out.halted = true;
            break;
        }
        Step moved = aside.at(id);
        moved.id = kNoStep;
        moved.parent = kNoStep;
        moved.children.clear();
        TransformationPayload copy = *payload;
        derivation.add_transformation(parent, std::move(moved), std::move(copy));
        ++out.moves;
    }
    out.recorded = out.moves > 0 && !out.halted;
    if (out.recorded)
        out.isolated = r.expression;
    return out;
}

// A term the marker strikes out. The path is what makes this a strikeout rather than a rewrite of
// the whole equation: it points at the term that went, and the action names the factor that killed
// it, so a reader sees the full equation first and then sees what removed part of it.
inline StepId record_vanishing_term(Derivation &derivation, StepId parent, NodeId before, NodeId after,
                                    std::vector<uint32_t> path, const std::string &term,
                                    const std::string &factor) {
    Step s;
    s.phase = "transform";
    s.goal = "Strike out " + term;
    s.rule_id = "physics.term-vanishes";
    s.rule_name = "A term with a zero factor";
    s.claim = ClaimType::EquivalentExpression;
    s.explanation_short = term + " vanishes because " + factor + " is zero";
    s.explanation_detailed =
        "The general equation is written with every term in it. This one is removed rather than "
        "never written, because " + factor +
        " is zero here and multiplying by zero leaves nothing behind.";
    s.proof_obligations.push_back(
        {"obl.physics.zero-factor-eliminates-term", "a product with a zero factor equals zero"});
    VerificationRecord v;
    v.method = "rule-local invariant";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
    v.detail = factor + " is zero, so " + term + " contributes nothing to the sum";
    s.verifications.push_back(std::move(v));
    TransformationPayload p;
    p.before = before;
    p.after = after;
    p.path = std::move(path);
    p.concrete_action = "Remove " + term + ", since " + factor + " is zero";
    p.reversible = false;
    return derivation.add_transformation(parent, std::move(s), std::move(p));
}

}  // namespace physics
}  // namespace nps

#endif
