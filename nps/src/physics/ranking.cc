#include "nps/physics/ranking.h"

#include <algorithm>
#include <numeric>

namespace nps {
namespace {

RankingResult failed(RankingOutcome outcome, DerivationStatus status, const std::string &detail) {
    RankingResult result;
    result.outcome = outcome;
    result.status = status;
    result.detail = detail;
    return result;
}

enum class Cmp : uint8_t { Less, Equal, Greater, Unknown };

Cmp compare_magnitude(const RankingCriterion &criterion, const RankingValue &a, const RankingValue &b) {
    if (!a.known || !b.known)
        return Cmp::Unknown;
    Rational diff;
    if (!rational_sub(a.magnitude, b.magnitude, &diff))
        return Cmp::Unknown;
    Cmp result = diff.num == 0 ? Cmp::Equal : (diff.num > 0 ? Cmp::Greater : Cmp::Less);
    if (criterion.direction == RankingDirection::Decreasing) {
        if (result == Cmp::Greater)
            result = Cmp::Less;
        else if (result == Cmp::Less)
            result = Cmp::Greater;
    }
    return result;
}

struct PairOutcome {
    Cmp result = Cmp::Equal;
    size_t deciding_criterion = static_cast<size_t>(-1);
};

PairOutcome compare_situations(const RankingModel &model, const RankingSituation &a,
                               const RankingSituation &b) {
    for (size_t index = 0; index < model.criteria.size(); ++index) {
        const Cmp result = compare_magnitude(model.criteria[index], a.values[index], b.values[index]);
        if (result != Cmp::Equal)
            return PairOutcome{result, index};
    }
    return PairOutcome{Cmp::Equal, static_cast<size_t>(-1)};
}

// Records what problem 7's justification requires: which quantity in the expression differs
// between situations and which is common to every one of them.
void record_criterion_step(Derivation &derivation, const RankingModel &model,
                           const RankingProblem &problem, size_t criterion_index) {
    const RankingCriterion &criterion = model.criteria[criterion_index];
    // A pair unknown for this criterion is neither a confirmed difference nor a confirmed tie, so it
    // is tracked apart from `varies`: a later pair that is genuinely different still wins the report,
    // but an all-unknown-or-equal criterion must say "not known", not "common".
    bool varies = false;
    bool unknown = false;
    std::string differing;
    std::string unresolved;
    for (size_t i = 0; i + 1 < problem.situations.size() && !varies; ++i) {
        for (size_t j = i + 1; j < problem.situations.size() && !varies; ++j) {
            const Cmp result = compare_magnitude(criterion, problem.situations[i].values[criterion_index],
                                                 problem.situations[j].values[criterion_index]);
            if (result == Cmp::Unknown) {
                if (!unknown) {
                    unknown = true;
                    unresolved = std::string(problem.situations[i].name) + " and " +
                                problem.situations[j].name;
                }
            } else if (result != Cmp::Equal) {
                varies = true;
                differing = std::string(problem.situations[i].name) + " and " +
                           problem.situations[j].name;
            }
        }
    }

    Step step;
    step.phase = "ranking";
    step.kind = StepKind::Check;
    step.rule_id = std::string("physics.ranking.criterion.") + criterion.name;
    step.rule_name = criterion.name;

    CheckPayload payload;
    payload.target_claim = std::string(model.quantity_name) + " ranked by " + criterion.name;
    payload.check_method = "qualitative comparison";
    if (varies) {
        step.explanation_short = std::string(criterion.name) + " differs across situations";
        payload.expected_relation = "varies between situations";
        payload.observed_result = "differs between " + differing;
    } else if (unknown) {
        step.explanation_short = std::string(criterion.name) + " is not known for every situation";
        payload.expected_relation = "known for every situation";
        payload.observed_result = "unknown for " + unresolved;
    } else {
        step.explanation_short = std::string(criterion.name) + " is common to every situation";
        payload.expected_relation = "equal across situations";
        payload.observed_result = "common to all situations";
    }
    derivation.add_check(kNoStep, step, payload);
}

std::string tier_text(const RankingProblem &problem, const RankingTier &tier) {
    std::string text;
    for (size_t index : tier.situations) {
        if (!text.empty())
            text += "=";
        text += problem.situations[index].name;
    }
    return text;
}

std::string order_text(const RankingProblem &problem, const std::vector<RankingTier> &order) {
    std::string text;
    for (const RankingTier &tier : order) {
        if (!text.empty())
            text += " > ";
        text += tier_text(problem, tier);
    }
    return text;
}

}  // namespace

const char *ranking_outcome_name(RankingOutcome outcome) {
    switch (outcome) {
        case RankingOutcome::Solved: return "solved";
        case RankingOutcome::InvalidProblem: return "invalid problem";
        case RankingOutcome::TooFewSituations: return "too few situations";
        case RankingOutcome::IndeterminateOrder: return "indeterminate order";
        case RankingOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

RankingResult solve_ranking(Derivation &derivation, const RankingModel &model,
                            const RankingProblem &problem, const Budget & /*budget*/) {
    if (model.criteria.empty())
        return failed(RankingOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                      "a ranking needs at least one criterion");
    if (problem.situations.size() < 2)
        return failed(RankingOutcome::TooFewSituations, DerivationStatus::InvalidInput,
                      "a ranking needs at least two situations");
    for (const RankingSituation &situation : problem.situations) {
        if (situation.values.size() != model.criteria.size())
            return failed(RankingOutcome::InvalidProblem, DerivationStatus::InvalidInput,
                          std::string(situation.name) + " does not give a value for every criterion");
    }

    const size_t n = problem.situations.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const PairOutcome outcome = compare_situations(model, problem.situations[i], problem.situations[j]);
            if (outcome.result == Cmp::Unknown) {
                const std::string criterion_name = model.criteria[outcome.deciding_criterion].name;
                return failed(RankingOutcome::IndeterminateOrder, DerivationStatus::Unsupported,
                              std::string(problem.situations[i].name) + " and " +
                                  problem.situations[j].name + " cannot be ordered by " +
                                  model.quantity_name + ": " + criterion_name + " is not known for both");
            }
        }
    }

    for (size_t index = 0; index < model.criteria.size(); ++index)
        record_criterion_step(derivation, model, problem, index);

    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return compare_situations(model, problem.situations[a], problem.situations[b]).result == Cmp::Greater;
    });

    std::vector<RankingTier> order;
    for (size_t k = 0; k < n; ++k) {
        const size_t current = indices[k];
        if (order.empty() ||
            compare_situations(model, problem.situations[order.back().situations.back()],
                               problem.situations[current]).result != Cmp::Equal) {
            order.push_back(RankingTier{{current}});
        } else {
            order.back().situations.push_back(current);
        }
    }

    RankingResult result;
    result.outcome = RankingOutcome::Solved;
    result.order = order;
    result.status = DerivationStatus::SolvedAndVerified;
    result.detail = std::string(model.quantity_name) + ": " + order_text(problem, order);

    Step step;
    step.phase = "ranking";
    step.kind = StepKind::Check;
    step.rule_id = "physics.ranking.order";
    step.rule_name = "ranking order";
    step.explanation_short = result.detail;
    CheckPayload payload;
    payload.target_claim = std::string("order of ") + model.quantity_name;
    payload.check_method = "lexicographic comparison of ranking criteria";
    payload.expected_relation = "greatest first, with ties grouped together";
    payload.observed_result = result.detail;
    derivation.add_check(kNoStep, step, payload);

    return result;
}

}  // namespace nps
