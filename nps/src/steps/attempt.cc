#include "nps/steps/attempt.h"

#include "nps/core/canonical.h"
#include "nps/core/evaluate.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {

const char *attempt_equivalence_name(AttemptEquivalence e) {
    switch (e) {
        case AttemptEquivalence::Equivalent: return "equivalent";
        case AttemptEquivalence::Corroborated: return "corroborated";
        case AttemptEquivalence::NotEquivalent: return "not equivalent";
        case AttemptEquivalence::NotComparable: return "not comparable";
        case AttemptEquivalence::Cancelled: return "cancelled";
        case AttemptEquivalence::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

const char *attempt_usefulness_name(AttemptUsefulness u) {
    switch (u) {
        case AttemptUsefulness::Advances: return "advances";
        case AttemptUsefulness::NoProgress: return "no progress";
        case AttemptUsefulness::ValidNotOnRoute: return "valid not on route";
        case AttemptUsefulness::NotJudged: return "not judged";
    }
    return "unknown";
}

namespace {

// Six, the count every other sample comparison in this tree uses.
const size_t kSamples = 6;

bool is_relation(Kind kind) {
    return kind == Kind::Equals || kind == Kind::Assign || kind == Kind::Approx ||
           kind == Kind::Identity || kind == Kind::Less || kind == Kind::LessEqual ||
           kind == Kind::Greater || kind == Kind::GreaterEqual;
}

AttemptVerdict verdict(AttemptEquivalence equivalence, EvidenceStrength strength, const char *method,
                       std::string detail) {
    AttemptVerdict v;
    v.equivalence = equivalence;
    v.strength = strength;
    v.method = method;
    v.detail = std::move(detail);
    return v;
}

AttemptVerdict halted(const Meter &meter) {
    const bool cancelled = meter.halt() == Halt::Cancelled;
    return verdict(cancelled ? AttemptEquivalence::Cancelled : AttemptEquivalence::ResourceExceeded,
                   EvidenceStrength::Unsupported, "none",
                   cancelled ? "the judgment was cancelled" : halt_name(meter.halt()));
}

AttemptVerdict exhausted() {
    return verdict(AttemptEquivalence::ResourceExceeded, EvidenceStrength::Unsupported, "none",
                   "the expression limits were reached");
}

// The x in a*x + b = 0, or false when the equation has no single solution this can read.
bool single_solution(Arena &arena, NodeId equation, NodeId variable, Meter &meter, Rational *out) {
    Rational coefficient;
    Rational constant;
    if (linear_form(arena, equation, variable, meter, &coefficient, &constant) != LinearForm::Reduced)
        return false;
    Rational quotient;
    return rational_div(constant, coefficient, &quotient) && rational_sub(Rational(), quotient, out);
}

NodeId side_difference(Arena &arena, NodeId equation) {
    const ChildView sides = arena.children(equation);
    if (sides.size() != 2)
        return kNoNode;
    return arena.binary(Kind::Add, sides[0], arena.unary(Kind::Neg, sides[1]));
}

AttemptVerdict compare_equations(Arena &arena, NodeId current, NodeId attempt, NodeId variable,
                                 Meter &meter) {
    Rational was;
    Rational now;
    const bool linear_before = single_solution(arena, current, variable, meter, &was);
    const bool linear_after = single_solution(arena, attempt, variable, meter, &now);
    if (meter.stopped())
        return halted(meter);
    if (linear_before && linear_after) {
        if (rational_equal(was, now)) {
            return verdict(AttemptEquivalence::Equivalent,
                           EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
                           "comparison of the single solutions",
                           "both have the one solution " + rational_text(now));
        }
        return verdict(AttemptEquivalence::NotEquivalent, EvidenceStrength::Failed,
                       "comparison of the single solutions",
                       "the attempt's solution is " + rational_text(now) + " and the state's is " +
                           rational_text(was));
    }
    const NodeId before = side_difference(arena, current);
    const NodeId after = side_difference(arena, attempt);
    if (arena.failed())
        return exhausted();
    if (before == kNoNode || after == kNoNode)
        return verdict(AttemptEquivalence::NotComparable, EvidenceStrength::Unsupported, "none",
                       "an equation needs two sides");
    const SampleAgreement samples = agrees_on_samples(arena, before, after, kSamples);
    if (samples.evaluated > 0 && samples.agreed == samples.evaluated) {
        return verdict(AttemptEquivalence::Corroborated, EvidenceStrength::NumericallyCorroborated,
                       "exact agreement at sample values",
                       "the two sides differ by the same amount at " +
                           std::to_string(samples.evaluated) + " samples");
    }
    // Differences that disagree can still share their roots, so this is no verdict against it.
    return verdict(AttemptEquivalence::NotComparable, EvidenceStrength::Unsupported, "none",
                   "neither a single solution nor the same difference of sides decides this pair");
}

AttemptVerdict compare_expressions(Arena &arena, NodeId current, NodeId attempt) {
    const SampleAgreement samples = agrees_on_samples(arena, current, attempt, kSamples);
    if (arena.failed())
        return exhausted();
    if (samples.evaluated == 0) {
        return verdict(AttemptEquivalence::NotComparable, EvidenceStrength::Unsupported, "none",
                       "neither form has an exact value at any sample");
    }
    if (samples.agreed < samples.evaluated) {
        return verdict(AttemptEquivalence::NotEquivalent, EvidenceStrength::Failed,
                       "exact agreement at sample values",
                       "the state and the attempt differ at " + samples.disagreement);
    }
    return verdict(AttemptEquivalence::Corroborated, EvidenceStrength::NumericallyCorroborated,
                   "exact agreement at sample values",
                   "they agree at " + std::to_string(samples.evaluated) + " samples");
}

void judge_usefulness(Arena &arena, NodeId canonical_current, NodeId canonical_attempt,
                      const std::vector<NodeId> &route, AttemptVerdict *v) {
    if (canonical_attempt == kNoNode)
        return;
    if (canonical_attempt == canonical_current) {
        v->usefulness = AttemptUsefulness::NoProgress;
        return;
    }
    for (size_t i = 0; i < route.size(); ++i) {
        const NodeId state = canonicalize(arena, route[i]);
        if (arena.failed())
            return;
        if (state != kNoNode && state == canonical_attempt) {
            v->usefulness = AttemptUsefulness::Advances;
            v->reaches = i + 1;
            return;
        }
    }
    v->usefulness = AttemptUsefulness::ValidNotOnRoute;
}

}  // namespace

AttemptVerdict judge_attempt(Arena &arena, NodeId current, NodeId attempt, NodeId variable,
                             const std::vector<NodeId> &route, const Budget &budget) {
    Meter meter(budget);
    if (meter.stopped())
        return halted(meter);
    if (arena.failed() || current >= arena.node_count() || attempt >= arena.node_count() ||
        variable >= arena.node_count()) {
        return exhausted();
    }

    const NodeId canonical_current = canonicalize(arena, current);
    const NodeId canonical_attempt = canonicalize(arena, attempt);
    if (arena.failed())
        return exhausted();

    AttemptVerdict v;
    const Kind before = arena.at(current).kind;
    const Kind after = arena.at(attempt).kind;
    if (canonical_current != kNoNode && canonical_current == canonical_attempt) {
        v = verdict(AttemptEquivalence::Equivalent,
                    EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
                    "identical canonical form", "the attempt is the state in another spelling");
    } else if (is_relation(before) != is_relation(after)) {
        v = verdict(AttemptEquivalence::NotEquivalent, EvidenceStrength::Failed, "kind of statement",
                    is_relation(before) ? "the state is an equation or relation and the attempt is not"
                                        : "the state is an expression and the attempt is a relation");
    } else if (before == Kind::Equals && after == Kind::Equals) {
        v = compare_equations(arena, current, attempt, variable, meter);
    } else if (is_relation(before)) {
        v = verdict(AttemptEquivalence::NotComparable, EvidenceStrength::Unsupported, "none",
                    "only equations and expressions are compared");
    } else {
        v = compare_expressions(arena, current, attempt);
    }

    if (v.equivalence == AttemptEquivalence::Equivalent ||
        v.equivalence == AttemptEquivalence::Corroborated) {
        judge_usefulness(arena, canonical_current, canonical_attempt, route, &v);
    }
    return v;
}

}  // namespace nps
