#ifndef NPS_PHYSICS_RANKING_H
#define NPS_PHYSICS_RANKING_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/rational.h"
#include "nps/steps/derivation.h"

namespace nps {

// One situation's standing on one ranking criterion. Unset means the given facts do not determine
// this situation's value for this criterion, which the ranking engine treats as a reason to refuse
// an order rather than a reason to guess one.
struct RankingValue {
    bool known = false;
    Rational magnitude;
};

inline RankingValue ranking_known(int64_t num, int64_t den = 1) {
    RankingValue value;
    value.known = true;
    value.magnitude = Rational{num, den};
    return value;
}

inline RankingValue ranking_unknown() { return RankingValue(); }

// Whether the ranked quantity grows or shrinks with a criterion's magnitude. Average speed grows
// with distance covered (Increasing) and shrinks with the time taken to cover it (Decreasing).
enum class RankingDirection : uint8_t { Increasing, Decreasing };

struct RankingCriterion {
    const char *name = "";
    RankingDirection direction = RankingDirection::Increasing;
};

struct RankingSituation {
    const char *name = "";
    // Aligned by index with RankingModel::criteria.
    std::vector<RankingValue> values;
};

// A quantity ranked across several named situations by one or more criteria, tried in priority
// order. The first criterion that is not tied between two situations decides their order, the way
// problem 7(d)'s initial speed ties on the vertical component and is decided by the horizontal one.
// The family id is the engine's own, because nothing ever set or read the field that was here.
struct RankingModel {
    const char *quantity_name = "";
    std::vector<RankingCriterion> criteria;
};

enum class RankingOutcome : uint8_t {
    Solved,
    InvalidProblem,
    TooFewSituations,
    // The given facts genuinely do not decide the order between at least two situations. Distinct
    // from InvalidProblem: the problem is well formed, the answer is that it cannot be determined.
    IndeterminateOrder,
    ResourceExceeded,
    Cancelled,
};

const char *ranking_outcome_name(RankingOutcome outcome);

struct RankingProblem {
    std::vector<RankingSituation> situations;
};

// One tier of the ranking, greatest first. Ties share a tier: problem 6(b) reports one situation,
// then a tier holding two tied situations, then one more.
struct RankingTier {
    std::vector<size_t> situations;  // indices into RankingProblem::situations
};

struct RankingResult {
    RankingOutcome outcome = RankingOutcome::InvalidProblem;
    std::vector<RankingTier> order;  // populated only when outcome == Solved
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

RankingResult solve_ranking(Derivation &derivation, const RankingModel &model,
                            const RankingProblem &problem, const Budget &budget = Budget());

}  // namespace nps

#endif
