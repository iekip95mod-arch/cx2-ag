#include <cstddef>
#include <string>

#include "nps/physics/ranking.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

RankingSituation situation(const char *name, RankingValue value) {
    RankingSituation entry;
    entry.name = name;
    entry.values.push_back(value);
    return entry;
}

RankingSituation situation(const char *name, RankingValue first, RankingValue second) {
    RankingSituation entry;
    entry.name = name;
    entry.values.push_back(first);
    entry.values.push_back(second);
    return entry;
}

std::string order_summary(const RankingResult &result, const RankingProblem &problem) {
    std::string text;
    for (const RankingTier &tier : result.order) {
        if (!text.empty())
            text += " > ";
        for (size_t i = 0; i < tier.situations.size(); ++i) {
            if (i != 0)
                text += "=";
            text += problem.situations[tier.situations[i]].name;
        }
    }
    return text;
}

size_t checks_of_kind(const Derivation &derivation, const std::string &rule_prefix) {
    size_t count = 0;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        if (step.rule_id.rfind(rule_prefix, 0) == 0)
            ++count;
    }
    return count;
}

const CheckPayload *check_payload_for(const Derivation &derivation, const std::string &rule_name) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        const StepId id = static_cast<StepId>(index);
        if (derivation.at(id).rule_id == "physics.ranking.criterion" &&
            derivation.at(id).rule_name == rule_name)
            return derivation.check(id);
    }
    return nullptr;
}

}  // namespace

void run_ranking_tests(TestSink &t) {
    {
        // Problem 6(a): four paths between the same two points over the same interval. Average
        // velocity is displacement over elapsed time, and both are the same for every path, so all
        // four situations tie.
        RankingModel model;
        model.quantity_name = "average velocity";
        model.criteria.push_back(RankingCriterion{"displacement over elapsed time",
                                                   RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("path 1", ranking_known(1)));
        problem.situations.push_back(situation("path 2", ranking_known(1)));
        problem.situations.push_back(situation("path 3", ranking_known(1)));
        problem.situations.push_back(situation("path 4", ranking_known(1)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved", "problem 6a solves");
        t.equal(order_summary(result, problem), "path 1=path 2=path 3=path 4",
                "problem 6a: all four paths tie on average velocity");
    }
    {
        // Problem 6(b): average speed is distance covered over the same elapsed time. Path 4 covers
        // the greatest distance, paths 1 and 2 cover an equal, smaller distance, and path 3 covers
        // the least, which is exactly the tie structure the marker's key records.
        RankingModel model;
        model.quantity_name = "average speed";
        model.criteria.push_back(RankingCriterion{"distance covered", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("path 1", ranking_known(4)));
        problem.situations.push_back(situation("path 2", ranking_known(4)));
        problem.situations.push_back(situation("path 3", ranking_known(3)));
        problem.situations.push_back(situation("path 4", ranking_known(6)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved", "problem 6b solves");
        t.equal(order_summary(result, problem), "path 4 > path 1=path 2 > path 3",
                "problem 6b: 4, then 1 and 2 tied, then 3");
        t.check(checks_of_kind(derivation, "physics.ranking.criterion") == 1,
                "problem 6b records one criterion step");
        t.check(result.cost.steps == derivation.size() && result.cost.rewrites > 0,
                "ranking reports its comparison and derivation work");
    }
    {
        // Problem 7's key version, parts (a) and (b): three footballs kicked from ground level, all
        // reaching the same maximum height, so time of flight and initial vertical velocity both tie
        // across the three.
        RankingModel model;
        model.quantity_name = "time of flight";
        model.criteria.push_back(RankingCriterion{"maximum height", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("football 1", ranking_known(5)));
        problem.situations.push_back(situation("football 2", ranking_known(5)));
        problem.situations.push_back(situation("football 3", ranking_known(5)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved", "problem 7a solves");
        t.equal(order_summary(result, problem), "football 1=football 2=football 3",
                "problem 7a: all three tie, it depends only on the maximum height");
    }
    {
        // Problem 7(c): initial horizontal velocity, ranked 3, 2, 1 by the horizontal distance each
        // ball covers in the same time of flight.
        RankingModel model;
        model.quantity_name = "initial horizontal velocity";
        model.criteria.push_back(RankingCriterion{"horizontal distance covered",
                                                   RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("football 1", ranking_known(1)));
        problem.situations.push_back(situation("football 2", ranking_known(2)));
        problem.situations.push_back(situation("football 3", ranking_known(3)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved", "problem 7c solves");
        t.equal(order_summary(result, problem), "football 3 > football 2 > football 1",
                "problem 7c: 3, then 2, then 1");
    }
    {
        // Problem 7(d): initial speed. The vertical component ties across all three footballs
        // because they share a maximum height, so the horizontal component, which is not tied,
        // decides the order without either component ever being combined into a speed.
        RankingModel model;
        model.quantity_name = "initial speed";
        model.criteria.push_back(RankingCriterion{"initial vertical velocity",
                                                   RankingDirection::Increasing});
        model.criteria.push_back(RankingCriterion{"initial horizontal velocity",
                                                   RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("football 1", ranking_known(9), ranking_known(1)));
        problem.situations.push_back(situation("football 2", ranking_known(9), ranking_known(2)));
        problem.situations.push_back(situation("football 3", ranking_known(9), ranking_known(3)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved", "problem 7d solves");
        t.equal(order_summary(result, problem), "football 3 > football 2 > football 1",
                "problem 7d: the vertical component ties so the horizontal one decides");
        t.check(checks_of_kind(derivation, "physics.ranking.criterion") == 2,
                "problem 7d records a justification step for each criterion");
        const Step &vertical = derivation.at(0);
        const Step &horizontal = derivation.at(1);
        t.equal(vertical.rule_id, "physics.ranking.criterion",
                "the vertical criterion uses the stable ranking rule");
        t.equal(horizontal.rule_id, "physics.ranking.criterion",
                "the horizontal criterion uses the same stable ranking rule");
        t.equal(vertical.rule_name, "initial vertical velocity",
                "the stable rule retains the vertical criterion name");
        t.equal(horizontal.rule_name, "initial horizontal velocity",
                "the stable rule retains the horizontal criterion name");
        t.check(derivation.check(0)->target_claim.find("initial vertical velocity") != std::string::npos &&
                    derivation.check(1)->target_claim.find("initial horizontal velocity") != std::string::npos,
                "the stable rule retains each criterion in its check payload");
    }
    {
        // The test paper's version of problem 7 is not solved: final speed on landing depends on the
        // drop between launch and landing height, which is not given here, so the ranking must
        // refuse rather than guess.
        RankingModel model;
        model.quantity_name = "final speed";
        model.criteria.push_back(RankingCriterion{"landing height", RankingDirection::Decreasing});

        RankingProblem problem;
        problem.situations.push_back(situation("terrain 1", ranking_unknown()));
        problem.situations.push_back(situation("terrain 2", ranking_unknown()));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "indeterminate order",
                "final speed against unstated landing heights refuses rather than guesses");
        t.check(result.order.empty(), "a refused ranking reports no order");
        t.check(result.detail.find("landing height") != std::string::npos,
                "the refusal names the criterion that could not be compared");
    }
    {
        // Regression for the reviewer's finding on PR #362: a second criterion that is unknown for a
        // pair already decided by the first criterion must not be recorded as "differs". The first
        // criterion (1, 2, 3) decides every pair by itself, so the second criterion's gap for
        // football 2 never affects the order, but its own justification step must say it is not
        // known for every situation rather than falsely claiming a difference.
        RankingModel model;
        model.quantity_name = "initial speed";
        model.criteria.push_back(RankingCriterion{"deciding component", RankingDirection::Increasing});
        model.criteria.push_back(RankingCriterion{"secondary component", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("football 1", ranking_known(1), ranking_known(5)));
        problem.situations.push_back(situation("football 2", ranking_known(2), ranking_unknown()));
        problem.situations.push_back(situation("football 3", ranking_known(3), ranking_known(5)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "solved",
                "a criterion that already decides every pair solves even when a later one has a gap");
        t.equal(order_summary(result, problem), "football 3 > football 2 > football 1",
                "the deciding component alone determines the order");

        const CheckPayload *secondary = check_payload_for(derivation, "secondary component");
        t.check(secondary != nullptr, "the secondary criterion records its own justification step");
        if (secondary != nullptr) {
            t.equal(secondary->observed_result, "unknown for football 1 and football 2",
                    "an unresolved pair is reported as unknown rather than as a difference");
            t.check(secondary->observed_result.find("differs") == std::string::npos,
                    "a gap in one situation's value is never reported as a confirmed difference");
        }
    }
    {
        // A problem too small to rank at all: one situation cannot be placed against anything.
        RankingModel model;
        model.quantity_name = "average speed";
        model.criteria.push_back(RankingCriterion{"distance covered", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("path 1", ranking_known(1)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(ranking_outcome_name(result.outcome), "too few situations",
                "a single situation cannot be ranked against anything");
    }
    {
        RankingModel model;
        model.quantity_name = "average speed";
        model.criteria.push_back(RankingCriterion{"distance covered", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("path 1", ranking_known(1)));
        problem.situations.push_back(situation("path 2", ranking_known(2)));

        Budget budget;
        budget.max_rewrites = 0;
        Derivation derivation;
        const RankingResult stopped = solve_ranking(derivation, model, problem, budget);
        t.equal(ranking_outcome_name(stopped.outcome), "resource exceeded",
                "comparison work respects the rewrite limit");
        t.equal(stopped.detail, "rewrite limit", "the exhausted comparison budget is named");
        t.check(stopped.status == DerivationStatus::ResourceLimitReached && stopped.order.empty() &&
                    derivation.size() == 0,
                "a comparison halt reports no order or partial derivation");

        budget = Budget();
        budget.max_steps = 0;
        const RankingResult step_stopped = solve_ranking(derivation, model, problem, budget);
        t.equal(ranking_outcome_name(step_stopped.outcome), "resource exceeded",
                "ranking steps respect the step limit");
        t.equal(step_stopped.detail, "step limit", "the exhausted step budget is named");
        t.check(step_stopped.status == DerivationStatus::ResourceLimitReached &&
                    step_stopped.order.empty() && derivation.size() == 0,
                "a step halt reports no order or partial derivation");

        budget.max_steps = 1;
        const RankingResult partial = solve_ranking(derivation, model, problem, budget);
        t.equal(ranking_outcome_name(partial.outcome), "resource exceeded",
                "a later step budget halt refuses an otherwise computed order");
        t.check(partial.cost.steps == 2 && partial.order.empty() && derivation.size() == 0,
                "a step halt rewinds a previously recorded ranking check");

        budget = Budget();
        budget.poll = [](void *) { return true; };
        const RankingResult cancelled = solve_ranking(derivation, model, problem, budget);
        t.equal(ranking_outcome_name(cancelled.outcome), "cancelled",
                "ranking polls for cancellation on entry");
        t.check(cancelled.status == DerivationStatus::NotRecorded && cancelled.order.empty() &&
                    derivation.size() == 0,
                "an entry cancellation reports no order or partial derivation");

        problem.situations.clear();
        for (int index = 0; index < 12; ++index)
            problem.situations.push_back(situation("path", ranking_known(index)));
        size_t polls = 0;
        budget.poll = [](void *context) { return ++*static_cast<size_t *>(context) == 14; };
        budget.poll_context = &polls;
        const RankingResult interrupted = solve_ranking(derivation, model, problem, budget);
        t.equal(ranking_outcome_name(interrupted.outcome), "cancelled",
                "ranking polls during pairwise comparison work");
        t.check(polls == 14 && interrupted.cost.rewrites >= 64 && interrupted.order.empty() &&
                    derivation.size() == 0,
                "cancellation during comparisons stops without publishing an order");
    }
    {
        // Issue 416's shape here, where the family recorded no context of its own at all.
        RankingModel model;
        model.quantity_name = "average speed";
        model.criteria.push_back(RankingCriterion{"distance covered", RankingDirection::Increasing});

        RankingProblem problem;
        problem.situations.push_back(situation("path 1", ranking_known(1)));
        problem.situations.push_back(situation("path 2", ranking_known(2)));

        Derivation derivation;
        const RankingResult result = solve_ranking(derivation, model, problem);
        t.equal(derivation.context.problem_family_id, "physics.ranking.comparative-order",
                "a solved ranking names this family rather than leaving the field to a nested "
                "engine");
        t.equal(derivation.context.problem_family_envelope_version, "1",
                "and records the envelope version the catalog declares");
        t.check(derivation.context.derivation_status == result.status,
                "the context binds to the outcome the ranking reached");
        t.check(derivation.context.requested_method.find("criteria") != std::string::npos,
                "the context states the method this family ran");

        RankingProblem undecidable;
        undecidable.situations.push_back(situation("path 1", ranking_unknown()));
        undecidable.situations.push_back(situation("path 2", ranking_known(2)));
        Derivation refused_derivation;
        const RankingResult refused = solve_ranking(refused_derivation, model, undecidable);
        t.equal(ranking_outcome_name(refused.outcome), "indeterminate order",
                "the control on the line below, so the refusal really is a refusal");
        t.equal(refused_derivation.context.problem_family_id, "physics.ranking.comparative-order",
                "and a refused ranking names the family too, since a refusal is a walkthrough as "
                "much as an answer is");
        std::string assumptions;
        for (const std::string &one : derivation.context.active_assumptions)
            assumptions += one + " | ";
        const CheckPayload *ordering = check_payload_for(derivation, "distance covered");
        t.evidence("PHYS-025",
                   assumptions.find("priority order") != std::string::npos &&
                       !derivation.context.requested_method.empty() &&
                       ordering != nullptr && !ordering->check_method.empty() &&
                       derivation.context.problem_family_id ==
                           "physics.ranking.comparative-order",
                   "the ranking family records its priority-order condition, the comparison rule it "
                   "applied and the justification step for each criterion");
    }
}

}  // namespace nps
