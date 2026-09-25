#include <string>

#include "nps/steps/derivation.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/linear.h"
#include "nps/steps/rearrange.h"
#include "nps/steps/rewrite.h"
#include "nps/steps/schema.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

Step envelope(const char *goal, ClaimType claim) {
    Step s;
    s.goal = goal;
    s.claim = claim;
    return s;
}

// Default member initialisers make these non aggregates under C++11, so they are filled by hand.
VerificationRecord verification(const char *method, VerificationOutcome outcome,
                                 const char *detail = "") {
    VerificationRecord v;
    v.method = method;
    v.outcome = outcome;
    v.detail = detail;
    return v;
}

Step plan_envelope(const char *strategy_id) {
    Step step = envelope("Choose a strategy", ClaimType::NoClaim);
    step.rule_id = strategy_id;
    return step;
}

PlanPayload plan_payload(const char *strategy_id) {
    PlanPayload plan;
    plan.strategy_id = strategy_id;
    plan.selected_strategy = "Registered strategy";
    return plan;
}

void test_plan_preconditions(TestSink &t) {
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.missing-registration");
        const StepId id = d.add_plan(kNoStep, plan_envelope(plan.strategy_id.c_str()), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified(),
                   "a plan with no registered preconditions is unverified");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.missing-evidence");
        StrategyPrecondition precondition;
        precondition.id = "pre.missing";
        precondition.condition_index = 0;
        precondition.evidence_id = "evidence.missing";
        plan.applicability_conditions.push_back("the input has the required form");
        plan.preconditions.push_back(std::move(precondition));
        Step step = plan_envelope(plan.strategy_id.c_str());
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified(),
                   "a registered precondition without associated evidence is unverified");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.failed-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.failed", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Failed, "the input does not match");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "failed precondition evidence makes the plan unverified");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.inconclusive-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.inconclusive",
                                       "the input has the required form", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Inconclusive, "the form is unknown");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified(),
                   "inconclusive precondition evidence makes the plan unverified");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.mismatched-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.expected", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "the input matches");
        plan.preconditions[0].evidence_id = "evidence.for-another-precondition";
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified(),
                   "evidence for a different precondition cannot verify the plan");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.deferred-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.deferred", "every visited form has a rule",
                                       "registered rule dispatch", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::NotAttempted, "checked during traversal");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        const bool initially_unverified = !d.at(id).verified();
        const bool completed = d.complete_plan_precondition(
            id, "pre.deferred", VerificationOutcome::Passed, "every visited form matched");
        t.evidence("VER-013",
                   initially_unverified && completed && d.at(id).verified() &&
                       d.at(id).claim == ClaimType::NoClaim &&
                       !d.complete_plan_precondition(id, "pre.deferred",
                                                     VerificationOutcome::Failed, "overwrite"),
                   "a no-claim plan becomes verified only after its registered evidence passes once");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.no-algebraic-claim");
        Step step = plan_envelope(plan.strategy_id.c_str());
        step.claim = ClaimType::EquivalentExpression;
        register_strategy_precondition(plan, step, "pre.valid", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "the input matches");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013",
                   d.at(id).claim == ClaimType::NoClaim && d.at(id).verified(),
                   "the plan API cannot label strategy selection as algebraic equivalence");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.duplicate-condition");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.first", "the first condition holds",
                                       "first check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::NotAttempted, "pending");
        register_strategy_precondition(plan, step, "pre.second", "the second condition holds",
                                       "second check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        plan.preconditions[1].condition_index = plan.preconditions[0].condition_index;
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013",
                   !d.at(id).verified() && d.at(id).has_failed_verification() &&
                       !d.complete_plan_precondition(id, "pre.first", VerificationOutcome::Passed,
                                                     "overwrite"),
                   "two preconditions cannot claim the same applicability condition");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.orphan-condition");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        plan.applicability_conditions.push_back("an unregistered condition");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "an applicability condition without a precondition invalidates the plan");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.orphan-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        VerificationRecord orphan = verification("unregistered check", VerificationOutcome::Passed);
        orphan.evidence_id = "evidence.orphan";
        step.verifications.push_back(std::move(orphan));
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "evidence without a registered precondition invalidates the plan");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.duplicate-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        VerificationRecord duplicate = verification("duplicate check", VerificationOutcome::Passed);
        duplicate.evidence_id = plan.preconditions[0].evidence_id;
        step.verifications.push_back(std::move(duplicate));
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "duplicate evidence cannot ambiguously verify a precondition");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.reserved-evidence");
        Step step = plan_envelope(plan.strategy_id.c_str());
        StrategyPrecondition precondition;
        precondition.id = "pre.reserved";
        precondition.condition_index = 0;
        precondition.evidence_id = "strategy.preconditions";
        plan.preconditions.push_back(std::move(precondition));
        plan.applicability_conditions.push_back("the input is supported");
        VerificationRecord evidence = verification("structural check", VerificationOutcome::Passed);
        evidence.evidence_id = "evidence.unrelated";
        step.verifications.push_back(std::move(evidence));
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "precondition evidence cannot use the reserved summary id");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.reserved-summary-collision");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        VerificationRecord collision = verification("caller summary", VerificationOutcome::Passed);
        collision.evidence_id = "strategy.preconditions";
        step.verifications.push_back(std::move(collision));
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.evidence("VER-013", !d.at(id).verified() && d.at(id).has_failed_verification(),
                   "caller evidence cannot collide with the reserved summary id");
    }
}

// PERF-009. The case worth care is a parent that survives while the children it grew do not.
// The accessor's own answer, pinned rather than a caller's use of it. add_branch returns kNoStep
// when the branch budget is gone, and kNoStep is the same 0xFFFFFFFF sentinel kNoNode is, so the
// id a caller holds can be one no step ever had. Every caller is bounded today, which is why this
// asks the accessor directly: the next caller is the one this is for.
void test_absent_step(TestSink &t) {
    Derivation d;
    PlanPayload strategy;
    strategy.selected_strategy = "Inverse operations";
    d.add_plan(kNoStep, envelope("Isolate x", ClaimType::NoClaim), std::move(strategy));

    t.check(d.at(kNoStep).id == kNoStep && d.at(kNoStep).kind == StepKind::Transformation,
            "reading kNoStep gives a step that says it is not one rather than reading out of "
            "bounds");
    t.check(d.at(static_cast<StepId>(d.size())).id == kNoStep &&
                d.at(static_cast<StepId>(d.size() + 500)).id == kNoStep,
            "and so does any id at or past the end");
    t.check(d.at(kNoStep).rule_id.empty() && d.at(kNoStep).goal.empty() &&
                d.at(kNoStep).verifications.empty(),
            "the refused read carries no rule, goal or verification a walk could mistake for work");
    t.check(&d.at(kNoStep) == &d.at(static_cast<StepId>(d.size() + 1)),
            "every refused read is the one shared step, so the reference outlives the call");
    t.check(d.size() == 1, "and asking for an absent step records nothing");

    // The write half of the same sentinel. Nothing reaches it today either: settle skips a kNoStep
    // holder before writing, so this asks the accessor rather than a caller.
    d.restrictions_at(kNoStep).push_back("x != 0");
    d.restrictions_at(static_cast<StepId>(d.size() + 500)).push_back("x > 0");
    t.check(d.at(static_cast<StepId>(0)).domain_restrictions.empty() && d.size() == 1,
            "a restriction written against an absent step lands on no step and creates none");
    t.check(d.restrictions_at(kNoStep).empty(),
            "and is discarded rather than read back by the next refused write");

    d.restrictions_at(static_cast<StepId>(0)).push_back("x != 0");
    t.check(d.at(static_cast<StepId>(0)).domain_restrictions.size() == 1,
            "while a real step still takes the restriction it is given");
}

void test_absent_parent(TestSink &t) {
    for (const StepKind kind : {StepKind::Plan, StepKind::Transformation, StepKind::Branch, StepKind::Check}) {
        const auto append = [kind](Derivation &d, Meter &meter, StepId parent) {
            Step step;
            step.rule_name = "Parent bounds probe";
            switch (kind) {
                case StepKind::Plan: return d.add_plan(parent, std::move(step), PlanPayload{});
                case StepKind::Transformation: return d.add_transformation(parent, std::move(step), TransformationPayload{});
                case StepKind::Branch: return d.add_branch(meter, parent, std::move(step), BranchPayload{});
                case StepKind::Check: return d.add_check(parent, std::move(step), CheckPayload{});
            }
            return kNoStep;
        };
        for (const StepId missing : {StepId{0}, StepId{2}, kNoStep - 1}) {
            Derivation d;
            Meter meter(Budget{});
            t.check(append(d, meter, missing) == kNoStep && d.size() == 0 &&
                        d.roots().empty() && meter.branches() == 0,
                    "every add API refuses an absent parent without records or budget use");
            const StepId root = append(d, meter, kNoStep);
            t.check(root == 0 && d.at(root).kind == kind && d.at(root).children.empty() &&
                        d.roots() == std::vector<StepId>{root},
                    "an invalid parent leaves the next root and payload indices intact");
            t.check((kind == StepKind::Plan && d.plan(root)) ||
                        (kind == StepKind::Transformation && d.transformation(root)) ||
                        (kind == StepKind::Branch && d.branch(root)) ||
                        (kind == StepKind::Check && d.check(root)),
                    "the next accepted record has its own payload kind");
        }
        Derivation d;
        Meter meter(Budget{});
        const StepId root = append(d, meter, kNoStep);
        const StepId child = append(d, meter, root);
        t.check(child == 1 && d.at(child).parent == root &&
                    d.at(root).children == std::vector<StepId>{child},
                "all add APIs still accept an existing earlier parent");
        d.rewind_to(1);
        const size_t charged = meter.branches();
        t.check(append(d, meter, child) == kNoStep && d.size() == 1 &&
                    d.at(root).children.empty() && d.roots() == std::vector<StepId>{root} &&
                    meter.branches() == charged,
                "a removed parent cannot become the new record's self-parent after rewind");
    }
}

void test_adopt_roots(TestSink &t) {
    Derivation d;
    const StepId older = d.add_check(
        kNoStep, envelope("Check the givens", ClaimType::NoClaim), CheckPayload{});
    const StepId parent = d.add_transformation(
        kNoStep, envelope("Substitute known values", ClaimType::NoClaim), TransformationPayload{});
    const StepId existing = d.add_check(
        parent, envelope("Check the dimensions", ClaimType::NoClaim), CheckPayload{});
    const size_t checkpoint = d.mark();
    TransformationPayload payload;
    payload.before = 11;
    payload.after = 12;
    payload.concrete_action = "Divide both sides by 4";
    Step solving = envelope("Isolate v0", ClaimType::SolutionSetPreserved);
    solving.verifications.push_back(verification("equality", VerificationOutcome::Passed, "equal"));
    const StepId first = d.add_transformation(kNoStep, std::move(solving), payload);
    const StepId descendant = d.add_check(
        first, envelope("Check the answer", ClaimType::NoClaim), CheckPayload{});
    const StepId interleaved = d.add_check(
        parent, envelope("Check the units", ClaimType::NoClaim), CheckPayload{});
    const StepId second = d.add_check(
        kNoStep, envelope("Check the completed hop", ClaimType::NoClaim), CheckPayload{});
    const auto roots_before = d.roots();
    const auto children_before = d.at(parent).children;
    for (const auto &invalid : std::vector<std::pair<size_t, StepId>>{
             {d.size() + 1, parent}, {checkpoint, kNoStep}, {checkpoint, first},
             {0, parent}, {checkpoint, descendant}}) {
        t.check(!d.adopt_roots_since(invalid.first, invalid.second),
                "root adoption refuses a missing parent, invalid checkpoint or cycle");
        t.check(d.roots() == roots_before && d.at(parent).children == children_before &&
                    d.at(first).parent == kNoStep && d.at(second).parent == kNoStep,
                "invalid adoption leaves every affected root and parent unchanged");
    }
    const size_t count_before = d.size();
    t.check(d.adopt_roots_since(checkpoint, parent), "new roots can be nested under an earlier step");
    t.check(d.size() == count_before && d.roots() == std::vector<StepId>{older, parent},
            "adoption preserves record IDs and removes only adopted roots");
    t.check(d.at(parent).children == std::vector<StepId>{existing, first, interleaved, second},
            "adopted roots retain creation order beside existing children");
    t.check(d.at(first).parent == parent && d.at(second).parent == parent &&
                d.at(descendant).parent == first,
            "adoption moves roots without disturbing descendants");
    t.check(d.transformation(first)->before == payload.before &&
                d.transformation(first)->after == payload.after &&
                d.transformation(first)->concrete_action == payload.concrete_action &&
                d.at(first).verified(),
            "adoption preserves mathematical payloads and verification");
    t.check(d.adopt_roots_since(checkpoint, parent) &&
                d.adopt_roots_since(d.size(), parent) &&
                d.at(parent).children.size() == 4,
            "repeated and empty adoption cannot duplicate children");
    d.rewind_to(checkpoint);
    t.check(d.size() == checkpoint && d.roots() == std::vector<StepId>{older, parent} &&
                d.at(parent).children == std::vector<StepId>{existing},
            "rewind removes adopted descendants and retains the earlier tree");
}

void test_rewind(TestSink &t) {
    Derivation d;

    PlanPayload strategy;
    strategy.selected_strategy = "Inverse operations";
    StepId plan =
        d.add_plan(kNoStep, envelope("Isolate x", ClaimType::NoClaim), std::move(strategy));

    TransformationPayload committed_payload;
    committed_payload.concrete_action = "Subtract 5 from both sides";
    StepId first = d.add_transformation(plan, envelope("Isolate x", ClaimType::SolutionSetPreserved),
                                        std::move(committed_payload));

    size_t committed = d.mark();
    std::vector<StepId> roots_before = d.roots();
    std::vector<StepId> plan_children = d.at(plan).children;
    std::vector<StepId> first_children = d.at(first).children;

    TransformationPayload nested;
    nested.concrete_action = "Divide both sides by 2";
    StepId second =
        d.add_transformation(first, envelope("Isolate x", ClaimType::NoClaim), std::move(nested));
    d.add_transformation(plan, envelope("Isolate x", ClaimType::NoClaim), TransformationPayload{});
    Meter branching(Budget{});
    StepId branch =
        d.add_branch(branching, second, envelope("Case split", ClaimType::NoClaim), BranchPayload{});
    d.add_check(kNoStep, envelope("Substitute back", ClaimType::NoClaim), CheckPayload{});

    t.check(d.size() == committed + 4, "four records were added past the checkpoint");
    t.check(d.roots().size() == 2, "and one of them became a root of its own");
    t.evidence("STEP-011",
               d.at(second).parent == first && d.at(branch).parent == second &&
                   d.transformation(second) != nullptr && d.branch(branch) != nullptr,
               "the derivation model retains nested transformations and an attached branch");

    d.rewind_to(committed);

    t.check(d.size() == committed, "rewinding drops every record added at or after the checkpoint");
    t.check(d.roots() == roots_before, "and leaves the roots the checkpoint had");
    t.check(d.at(plan).children == plan_children,
            "a parent that survives keeps the children it had rather than the ones that went");
    t.check(d.at(first).children == first_children,
            "and a parent whose only child was dropped has none");

    size_t dangling = 0;
    for (StepId id = 0; id < d.size(); ++id) {
        for (StepId child : d.at(id).children) {
            if (child >= d.size())
                ++dangling;
        }
    }
    t.check(dangling == 0, "no surviving record names a child that no longer exists");

    t.check(d.plan(plan) != nullptr && d.plan(plan)->selected_strategy == "Inverse operations",
            "a survivor keeps its own payload rather than one the truncation shifted");
    t.check(d.transformation(first) != nullptr &&
                d.transformation(first)->concrete_action == "Subtract 5 from both sides",
            "and so does the transformation below it");

    d.rewind_to(d.mark());
    t.check(d.size() == committed, "rewinding to the current mark changes nothing");
    d.rewind_to(committed);
    t.check(d.size() == committed && d.roots() == roots_before,
            "and rewinding twice to the same checkpoint is safe");

    StepId resumed = d.add_transformation(plan, envelope("Isolate x", ClaimType::NoClaim),
                                          TransformationPayload{});
    t.check(resumed == committed, "a solve that starts again takes the id the dropped record had");
    t.check(d.at(plan).children.size() == plan_children.size() + 1,
            "and hangs off its parent once, not beside the sibling that was dropped");

    d.rewind_to(0);
    t.check(d.size() == 0 && d.roots().empty(), "a rewind to the start leaves nothing behind");
}

// STEP-025. Where the trim lands is the whole question, and there are three places it can land that
// a solve reaching a step limit will not show you on its own.
// Whether the invariant pass said a particular thing, so a guard can be shown to fire on the shape
// it names rather than on any complaint at all.
bool mentions(const std::vector<std::string> &broken, const std::string &what) {
    for (size_t i = 0; i < broken.size(); ++i)
        if (broken[i].find(what) != std::string::npos)
            return true;
    return false;
}

// PHYS-026's first sentence, which nothing was asking. A strategy that registers no applicability
// condition at all is the violation PRD:435 names, and both halves passed it: check_plan_associations
// calls an empty registration well formed, and the invariant arm only looked at plans that had
// registered something. No engine produces this today, so the shape is built here.
void test_plan_records_its_applicability(TestSink &t) {
    Arena arena;
    {
        invariants::Pass pass;
        Derivation d;
        PlanPayload plan = plan_payload("strategy.no-applicability-record");
        d.add_plan(kNoStep, plan_envelope(plan.strategy_id.c_str()), std::move(plan));
        std::vector<std::string> broken;
        pass.walk(arena, d, false, false, &broken);
        const invariants::Claim *applicability = pass.find("PHYS-026");
        t.check(mentions(broken, "registers no applicability condition"),
                "a strategy selected with no applicability record at all is caught");
        t.check(applicability != nullptr && applicability->checked == 1,
                "and the plan is counted in the population the claim is quantified over");
        t.check(pass.planned_strategies() == 1,
                "so the count reported beside the claim is a count of plans rather than of plans "
                "that registered something");
    }
    {
        // The arm must still pass a plan that does state a condition and back it, or the claim
        // reads as held from the other side by breaking on everything.
        invariants::Pass pass;
        Derivation d;
        PlanPayload plan = plan_payload("strategy.records-its-conditions");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.recorded", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "the input matches");
        d.add_plan(kNoStep, std::move(step), std::move(plan));
        std::vector<std::string> clean;
        pass.walk(arena, d, false, false, &clean);
        t.check(!mentions(clean, "registers no applicability condition"),
                "a strategy that states a condition and backs it beyond dimensions is no fault");
    }
    {
        // The second sentence still has to fire, and an empty plan must not be reported as this,
        // because dimensional agreement alone and no record at all are different faults.
        invariants::Pass pass;
        Derivation d;
        PlanPayload plan = plan_payload("strategy.dimensions-only");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.dimensions", "the units agree",
                                       "dimensional check", EvidenceStrength::DimensionallyValid,
                                       VerificationOutcome::Passed, "the units agree");
        d.add_plan(kNoStep, std::move(step), std::move(plan));
        std::vector<std::string> dimensional;
        pass.walk(arena, d, false, false, &dimensional);
        t.check(mentions(dimensional, "rests its applicability on dimensional agreement alone"),
                "a strategy backed only by matching units is still caught");
        t.check(!mentions(dimensional, "registers no applicability condition"),
                "and is not reported as the strategy that registered nothing");
    }
}

// VER-019. The requirement forbids a confirmation from turning a failed obligation into a passed
// verification. There is no code that could do it: active_assumptions is written into the context,
// serialized, parsed and joined for display, and no engine reads it to decide anything, while the
// Lua bridge pushes verification outcomes outward and takes none in. So this asserts the property
// the absence produces rather than a guard against a caller that does not exist, and it asserts it
// with an assumption recorded, because "an assumption was chosen" is the only user act the record
// can carry.
void test_ver019_an_assumption_cannot_pass_a_failed_check(TestSink &t) {
    Arena arena;
    const NodeId before = parse(arena, "x + 1").root;
    const NodeId after = parse(arena, "x + 2").root;

    const auto status_with = [&](bool assume) {
        Derivation d;
        Step s = envelope("Add one to both sides", ClaimType::SolutionSetPreserved);
        s.rule_id = "algebra.rearrange.add";
        s.rule_name = "Add to both sides";
        s.explanation_short = "Add the same amount to each side";
        s.explanation_detailed = "Reach for this when a term is in the way on one side.";
        s.proof_obligations.push_back({"obl.rearrange.same-solutions",
                                       "the equation after the step has the solutions it had"});
        s.verifications.push_back(verification("substitution of the candidate",
                                               VerificationOutcome::Failed,
                                               "the two sides no longer agree"));
        TransformationPayload p;
        p.before = before;
        p.concrete_action = "Add 1 to both sides";
        const StepId id = d.add_transformation(kNoStep, std::move(s), std::move(p));
        d.complete_transformation(id, after);
        if (assume)
            d.context.active_assumptions.push_back("x is real and positive");
        return std::string(derivation_status_name(d.outcome_from(0)));
    };

    const std::string bare = status_with(false);
    const std::string assumed = status_with(true);
    t.equal(bare, "verification failed", "VER-019: a failed obligation fails the derivation");
    t.equal(assumed, bare,
            "VER-019: and recording an assumption alongside it changes nothing, which is the whole "
            "of what the requirement asks");

    t.evidence("VER-019", bare == "verification failed" && assumed == bare,
               "a recorded assumption cannot lift a failed proof obligation into a passed "
               "verification. It cannot because nothing reads assumptions to decide an outcome and "
               "the interface layer only reads outcomes outward, so the requirement is met by "
               "there being no path rather than by a guard on one, and this pins the behaviour that "
               "absence produces so the day a path is added it has to be argued for");
}

void test_verified_prefix(TestSink &t) {
    // Sound throughout, so the arena guard never fires here. Its own case is below.
    Arena arena;

    // A step that claims something and passed says so. One that claims something and was never
    // looked at is where the prefix ends, whatever came after it.
    Step checked = envelope("Isolate x", ClaimType::SolutionSetPreserved);
    checked.verifications.push_back(verification("sample agreement", VerificationOutcome::Passed));
    const auto exhaustive_case = []() {
        BranchPayload payload;
        payload.siblings_exhaustive = true;
        payload.siblings_exclusive = true;
        payload.siblings_domain_consistent = true;
        payload.exhaustive_evidence = "case reconstruction";
        payload.resolution = BranchResolution::Solved;
        payload.resolution_evidence = "substitution";
        return payload;
    };
    const auto retains_complete_split = [&](BranchPayload first_payload,
                                            BranchPayload second_payload) {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        const StepId split =
            d.add_transformation(kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        d.add_branch(meter, split, checked, std::move(first_payload));
        const StepId first = d.add_branch(meter, split, checked, std::move(second_payload));
        Step closing = envelope("Check every case", ClaimType::SolutionSetPreserved);
        closing.verifications.push_back(
            verification("case reconstruction", VerificationOutcome::Passed));
        d.add_check(split, std::move(closing), CheckPayload{});
        const StepId after = d.add_transformation(
            kNoStep, envelope("Unfinished later work", ClaimType::SolutionSetPreserved),
            TransformationPayload{});

        return first != kNoStep && d.verified_prefix_end(0) == after;
    };

    t.check(retains_complete_split(exhaustive_case(), exhaustive_case()),
            "a consistent resolved split remains in the verified prefix");
    {
        BranchPayload changed = exhaustive_case();
        changed.siblings_exhaustive = false;
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "siblings that disagree about exhaustiveness roll back before the split");
    }
    {
        BranchPayload changed = exhaustive_case();
        changed.siblings_exclusive = false;
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "siblings that disagree about exclusivity roll back before the split");
    }
    {
        BranchPayload changed = exhaustive_case();
        changed.siblings_domain_consistent = false;
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "siblings that disagree about domain consistency roll back before the split");
    }
    {
        BranchPayload changed = exhaustive_case();
        changed.exhaustive_evidence = "different reconstruction";
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "siblings that name different exhaustive evidence roll back before the split");
    }
    {
        BranchPayload changed = exhaustive_case();
        changed.resolution = BranchResolution::Unresolved;
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "an unresolved sibling rolls back before the split");
    }
    {
        BranchPayload changed = exhaustive_case();
        changed.resolution_evidence.clear();
        t.check(!retains_complete_split(exhaustive_case(), std::move(changed)),
                "a sibling without resolution evidence rolls back before the split");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        StepId split = d.add_transformation(
            kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        const StepId first = d.add_branch(meter, split, checked, exhaustive_case());

        Step unchecked = envelope("Take the other square root", ClaimType::SolutionSetNarrowed);
        const StepId second =
            d.add_branch(meter, split, std::move(unchecked), exhaustive_case());

        t.check(second != kNoStep && d.verified_prefix_end(0) == first,
                "a prefix ending in the second case rolls back to before the split");
        t.check(keep_verified_prefix(d, 0, arena) && d.size() == first,
                "so retaining it keeps the checked setup and no half split");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        StepId split = d.add_transformation(
            kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        const StepId only = d.add_branch(meter, split, checked, exhaustive_case());

        t.check(d.verified_prefix_end(0) == only,
                "an individually checked case without its split completion is not a valid prefix");
        t.check(keep_verified_prefix(d, 0, arena) && d.size() == only,
                "so an end-of-record halt also keeps no half split");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        StepId split = d.add_transformation(kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        d.add_branch(meter, split, checked, exhaustive_case());
        d.add_branch(meter, split, checked, exhaustive_case());
        Step closing = envelope("Check every case", ClaimType::SolutionSetPreserved);
        closing.verifications.push_back(
            verification("case reconstruction", VerificationOutcome::Passed));
        d.add_check(split, std::move(closing), CheckPayload{});
        const StepId after = d.add_transformation(
            kNoStep, envelope("Unfinished later work", ClaimType::SolutionSetPreserved),
            TransformationPayload{});

        t.check(d.verified_prefix_end(0) == after,
                "a completed split remains in the prefix when later work is unchecked");
        t.check(keep_verified_prefix(d, 0, arena) && d.size() == after,
                "so the group guard does not discard a valid completed split");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        const StepId split = d.add_transformation(kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        const StepId first = d.add_branch(meter, split, checked, exhaustive_case());
        d.add_branch(meter, split, checked, exhaustive_case());
        Step closing = envelope("Check every case", ClaimType::SolutionSetPreserved);
        closing.verifications.push_back(
            verification("case reconstruction", VerificationOutcome::Passed));
        d.add_check(split, std::move(closing), CheckPayload{});
        const StepId failed = d.add_check(
            first, envelope("Check the first result", ClaimType::SolutionSetPreserved),
            CheckPayload{});

        t.check(failed != kNoStep && d.at(failed).parent == first &&
                    d.verified_prefix_end(0) == first,
                "a failed direct branch child rolls the prefix back before the split");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        const StepId split = d.add_transformation(kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        const StepId first = d.add_branch(meter, split, checked, exhaustive_case());
        d.add_branch(meter, split, checked, exhaustive_case());
        Step closing = envelope("Check every case", ClaimType::SolutionSetPreserved);
        closing.verifications.push_back(
            verification("case reconstruction", VerificationOutcome::Passed));
        d.add_check(split, std::move(closing), CheckPayload{});
        TransformationPayload inner_payload;
        inner_payload.concrete_action = "Simplify the first result";
        const StepId inner = d.add_transformation(first, checked, std::move(inner_payload));
        d.complete_transformation(inner, 2);
        const StepId failed = d.add_check(
            inner, envelope("Check the first result", ClaimType::SolutionSetPreserved),
            CheckPayload{});

        t.check(d.verified_prefix_end(0) == first,
                "a failed nested branch descendant rolls the prefix back before the split");
        t.check(failed != kNoStep && d.at(failed).parent == inner && d.at(inner).parent == first,
                "the nested control reaches a grandchild beyond the complete branch records");
    }

    {
        Derivation d;
        TransformationPayload split_payload;
        split_payload.concrete_action = "Isolate the square";
        const StepId split = d.add_transformation(kNoStep, checked, std::move(split_payload));
        d.complete_transformation(split, 1);

        Meter meter(Budget{});
        const StepId first = d.add_branch(meter, split, checked, exhaustive_case());
        d.add_branch(meter, split, checked, exhaustive_case());

        TransformationPayload inner_payload;
        inner_payload.concrete_action = "Split the first result";
        const StepId inner = d.add_transformation(first, checked, std::move(inner_payload));
        d.complete_transformation(inner, 2);
        d.add_branch(meter, inner, checked, exhaustive_case());
        d.add_branch(meter, inner, checked, exhaustive_case());
        Step inner_closing = envelope("Check every inner case", ClaimType::SolutionSetPreserved);
        inner_closing.verifications.push_back(
            verification("case reconstruction", VerificationOutcome::Passed));
        const StepId inner_check = d.add_check(inner, std::move(inner_closing), CheckPayload{});
        const StepId after = d.add_transformation(
            kNoStep, envelope("Unfinished later work", ClaimType::SolutionSetPreserved),
            TransformationPayload{});

        t.check(d.verified_prefix_end(0) == first,
                "a nested split cannot supply its enclosing split's completion evidence");
        t.check(d.at(inner_check).parent == inner && d.verified_prefix_end(inner) == after,
                "the evidence-scope control still recognizes the completed inner split");
    }

    {
        Derivation d;
        PlanPayload strategy = plan_payload("strategy.inverse-operations");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered",
                                       "the input is supported", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(strategy));

        TransformationPayload done;
        done.concrete_action = "Subtract 5 from both sides";
        StepId first = d.add_transformation(plan, checked, std::move(done));
        d.complete_transformation(first, 1);

        TransformationPayload unchecked;
        unchecked.concrete_action = "Divide both sides by 2";
        StepId second = d.add_transformation(
            plan, envelope("Isolate x", ClaimType::SolutionSetPreserved), std::move(unchecked));
        d.complete_transformation(second, 2);

        t.check(d.verified_prefix_end(0) == second, "the prefix ends at the record nobody checked");
        t.check(keep_verified_prefix(d, 0, arena), "so a halt here has something to keep");
        t.check(d.size() == 2, "which is the plan and the one move that passed");
        t.check(d.at(first).verified(), "and what survived had passed");
    }

    {
        // A composite states its decomposition before its children return, so one that never got
        // its result is a hole rather than a step. Its own verification passing is not enough.
        Derivation d;
        PlanPayload strategy = plan_payload("strategy.product-rule");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered", "every form has a rule",
                                       "rule dispatch", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(strategy));

        TransformationPayload open;
        open.concrete_action = "Differentiate the product";
        StepId composite = d.add_transformation(plan, checked, std::move(open));

        t.check(d.at(composite).verified(), "the composite's own verification passed");
        t.check(d.verified_prefix_end(0) == composite,
                "but with no result filled in it still ends the prefix");
        t.check(!keep_verified_prefix(d, 0, arena), "and a plan on its own is not worth keeping");
        t.check(d.size() == 0, "so the halt records nothing at all");
    }

    {
        // The same shape with the parent filled in, which is what the engines now do when they stop
        // partway with work below them. The block above says an unfilled parent cuts, and that is
        // still true: the criterion is not that a composite loses its children, it is that a parent
        // has to say what it produced before anything under it can be read.
        Derivation d;
        PlanPayload strategy = plan_payload("strategy.product-rule");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered", "every form has a rule",
                                       "rule dispatch", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(strategy));

        TransformationPayload open;
        open.concrete_action = "Differentiate the product";
        StepId composite = d.add_transformation(plan, checked, std::move(open));
        TransformationPayload child;
        child.concrete_action = "Differentiate the first factor";
        StepId inner = d.add_transformation(composite, checked, std::move(child));
        d.complete_transformation(inner, 1);
        // Last, the way a composite rule fills itself in once its children have returned.
        d.complete_transformation(composite, 2);

        t.check(d.completed_transformation_after(composite),
                "the engine can see that a child landed, which is what lets it complete the parent");
        t.check(d.verified_prefix_end(0) == d.size(),
                "and a filled parent no longer ends the prefix");
        t.check(keep_verified_prefix(d, 0, arena) && d.size() == 3,
                "so a halt inside a composite keeps the parent and the child under it");
    }

    {
        // The prefix reaches the end when nothing is wrong with it, which is the case that would
        // hide a predicate that always cuts.
        Derivation d;
        PlanPayload strategy = plan_payload("strategy.whole");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(strategy));
        TransformationPayload done;
        done.concrete_action = "Subtract 5 from both sides";
        StepId only = d.add_transformation(plan, checked, std::move(done));
        d.complete_transformation(only, 1);
        t.check(d.verified_prefix_end(0) == d.size(), "a sound record keeps every one of its steps");
        t.check(keep_verified_prefix(d, 0, arena) && d.size() == 2, "and the trim takes nothing");
    }

    {
        // The same sound record, in an arena that ran out. Every step still reads as checked, which
        // is exactly why the arena is asked separately: a record whose nodes may never have been
        // made is not a shorter derivation, it is a broken one.
        Limits limits;
        limits.max_nodes = 2;
        Arena exhausted(limits);
        exhausted.integer("1");
        exhausted.integer("2");
        exhausted.integer("3");

        Derivation d;
        PlanPayload strategy = plan_payload("strategy.exhausted");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered",
                                       "the input is supported", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(strategy));
        TransformationPayload done;
        done.concrete_action = "Subtract 5 from both sides";
        StepId only = d.add_transformation(plan, checked, std::move(done));
        d.complete_transformation(only, 1);

        t.check(exhausted.failed(), "the arena did run out");
        t.check(d.verified_prefix_end(0) == d.size(), "and the record still reads as checked");
        t.check(!keep_verified_prefix(d, 0, exhausted), "but a halt keeps none of it");
        t.check(d.size() == 0, "leaving nothing that names a node nobody made");
    }

    {
        // Criterion 8's two new arms, shown firing. Neither state has a producer in the engines, so
        // the shape is built here rather than solved for: a guard nothing exercises reads as cover
        // and is worse than an admitted gap, because the next reader trusts it.
        invariants::Pass pass;

        // A refusal keeping a checked prefix, which is the state the two engines now reach and the
        // one the arms must not catch.
        Derivation partial;
        PlanPayload strategy = plan_payload("strategy.partial");
        Step plan_step = plan_envelope(strategy.strategy_id.c_str());
        register_strategy_precondition(strategy, plan_step, "pre.registered", "the input is supported",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "passed");
        StepId plan = partial.add_plan(kNoStep, std::move(plan_step), std::move(strategy));
        TransformationPayload done;
        done.concrete_action = "Subtract 5 from both sides";
        // Real nodes, because the pass reads the states rather than only counting the steps.
        done.before = parse(arena, "x + 5").root;
        StepId only = partial.add_transformation(plan, checked, std::move(done));
        partial.complete_transformation(only, parse(arena, "x").root);
        partial.context.derivation_status = DerivationStatus::PartiallySolved;
        std::vector<std::string> allowed;
        pass.walk(arena, partial, true, true, &allowed);
        t.check(!mentions(allowed, "partial solution but kept no transformation"),
                "a refusal that kept a checked step may call itself partially solved");

        // The same status with nothing behind it. Partially solved is a claim about a prefix, so
        // making it with an empty record overstates exactly as far as claiming solved would.
        Derivation empty;
        empty.context.derivation_status = DerivationStatus::PartiallySolved;
        std::vector<std::string> bare;
        pass.walk(arena, empty, true, true, &bare);
        t.check(mentions(bare, "partial solution but kept no transformation"),
                "but claiming it with an empty record is caught");

        // The outcome named in task 24 and left without a guard until now. It says the checks could
        // not run, which is a statement about the verifier, while a refusal is a statement about the
        // answer, so the pairing still claims a solution nobody was given.
        Derivation unchecked;
        unchecked.context.derivation_status = DerivationStatus::SolvedButUnchecked;
        std::vector<std::string> claimed;
        pass.walk(arena, unchecked, true, true, &claimed);
        t.check(mentions(claimed, "claims a complete solution under the status solved but unchecked"),
                "and a refusal calling itself solved but unchecked is caught too");
    }
}

// PERF-008's last two limits: the branch count and the repeated canonical state. Both are exercised
// through the interface an engine uses rather than through the meter alone, because what the
// requirement asks is that the solver cannot exceed them, not that a counter counts.
void test_branch_and_cycle_limits(TestSink &t) {
    {
        Derivation d;
        Budget three;
        three.max_branches = 3;
        Meter meter(three);

        for (int i = 0; i < 3; ++i) {
            const StepId id =
                d.add_branch(meter, kNoStep, envelope("Case split", ClaimType::NoClaim),
                             BranchPayload{});
            t.check(id != kNoStep, "a branch inside the budget is recorded");
        }
        t.check(!meter.stopped(), "and three of three has not stopped anything");

        const size_t before = d.size();
        const StepId over = d.add_branch(meter, kNoStep, envelope("Case split", ClaimType::NoClaim),
                                         BranchPayload{});
        t.check(over == kNoStep, "the fourth branch past a budget of three is refused");
        t.check(d.size() == before, "and nothing was recorded for it");
        t.equal(halt_name(meter.halt()), "branch limit", "with the halt naming the branch count");
        t.check(meter.cost().branches == 4, "which counts the refused split as spent");
        t.evidence("PERF-008",
                   over == kNoStep && d.size() == before && meter.halt() == Halt::BranchLimit,
                   "the branch count is bounded at the point a derivation splits, so no engine can "
                   "split without spending one");
    }

    {
        // The repeated canonical state. The arena interns, so a state already reached is the same
        // NodeId. Two different states pass and the second arrival of the first one does not.
        Arena arena;
        const NodeId first = parse(arena, "2x + 5 = 13").root;
        const NodeId second = parse(arena, "2x = 8").root;
        t.check(first != second, "two different equations are two different nodes");
        t.check(parse(arena, "2x + 5 = 13").root == first,
                "and the same equation parsed twice is one node, which is what makes a repeat an "
                "equality rather than a comparison");

        Meter meter{Budget{}};
        t.check(meter.reached(first), "the state a solve starts from is new");
        t.check(meter.reached(second), "and so is the one it moves to");
        t.check(!meter.reached(first), "arriving back at the first is refused");
        t.equal(halt_name(meter.halt()), "repeated state", "and named as the cycle it is");
        t.evidence("PERF-008", meter.halt() == Halt::RepeatedState,
                   "a canonical state reached twice stops the solve");
    }

    {
        // The wiring, from the other end. No rule in the tree cycles: every pass moves toward fewer
        // terms or simpler numbers, and a pass that changes nothing records nothing, so the halt
        // cannot be reached through an engine today. Removing the two asks would therefore break
        // nothing, which is why the count is checked rather than the halt: an engine that stopped
        // asking reports fewer states, and that is visible whether or not a rule ever cycles.
        Arena arena;
        Derivation d;
        RewriteResult r = rewrite(arena, d, parse(arena, "2x + 3x + 4*5").root,
                                  RewriteGoal::Simplify, Budget{}, nullptr);
        t.check(r.outcome == RewriteOutcome::Rewritten, "an ordinary simplify still finishes");
        t.check(r.detail != "repeated state", "without the state gate firing on honest progress");
        t.equal(print(arena, r.expression), "((5 * x) + 20)", "and reaches the answer it always did");
        size_t moves = 0;
        for (size_t i = 0; i < d.size(); ++i)
            moves += d.at(static_cast<StepId>(i)).kind == StepKind::Transformation ? 1 : 0;
        t.equal(std::to_string(r.cost.states), std::to_string(moves + 1),
                "having offered the gate the form it started from and the result of every move");
        t.evidence("PERF-008", r.cost.states == moves + 1 && moves > 1,
                   "the rewrite engine offers every canonical state it reaches to the repeated "
                   "state gate");
    }

    {
        // The same, on the peel. It offers the equation each turn starts from, so the count is the
        // number of turns rather than the number of records.
        Arena arena;
        Derivation d;
        RearrangeResult r = rearrange(arena, d, parse(arena, "y = 2x + 5").root, arena.symbol("x"),
                                      Budget{});
        t.check(r.outcome == RearrangeOutcome::Isolated, "an ordinary rearrange still finishes");
        t.check(r.cost.states == 2, "having offered the gate both forms the peel started a turn on");
        t.evidence("PERF-008", r.cost.states == 2,
                   "the rearrange engine offers every canonical state it reaches to the repeated "
                   "state gate");
    }

    {
        // STEP-018's third clause, which the other two do not cover. An expression that grows while
        // it is being worked on is stopped by the arena rather than by the meter, and the case that
        // matters is growth during a solve: parsing something already too big is a different
        // refusal, and run_tests.cc has that one.
        Limits room;
        room.max_nodes = 64;
        Arena arena(room);
        Derivation d;
        const NodeId product = parse(arena, "(x + 1)*(x + 2)*(x + 3)*(x + 4)*(x + 5)").root;
        t.check(product != kNoNode && !arena.failed(), "the product itself fits");

        RewriteResult r = rewrite(arena, d, product, RewriteGoal::Expand, Budget{}, nullptr);
        t.check(arena.failed(), "but multiplying it out does not");
        t.equal(status_name(arena.status()), "size exceeded", "and the arena says why");
        t.check(r.expression == kNoNode, "so no expanded form is offered");
        t.check(d.size() == 0, "and nothing unchecked is left in the record");
        t.evidence("STEP-018",
                   arena.failed() && arena.status() == Status::SizeExceeded &&
                       r.expression == kNoNode,
                   "the engine detects repeated canonical states, the rewrite cycles they stand "
                   "for, and an expression that grows past its size limit while being solved");
    }
}

}  // namespace

// Walks a finished record the way the Lua bridge does: every step, every payload accessor, every
// NodeId printed. src/lua_module.cc is not in this build, so nothing else here covers the one thing
// it does that can reach an invalid node, and that gap is why its first version went to the
// calculator untested.
void walk_like_the_bridge(TestSink &t, const Arena &arena, const Derivation &d, const char *what) {
    size_t printed = 0;
    for (size_t i = 0; i < d.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        const TransformationPayload *tr = d.transformation(id);
        if (tr) {
            if (tr->before != kNoNode) {
                t.check(tr->before < arena.node_count(), "a recorded before is a node this arena has");
                printed += print(arena, tr->before).size();
            }
            if (tr->after != kNoNode) {
                t.check(tr->after < arena.node_count(), "a recorded after is a node this arena has");
                printed += print(arena, tr->after).size();
            }
        }
        const BranchPayload *b = d.branch(id);
        if (b && b->condition != kNoNode) {
            t.check(b->condition < arena.node_count(), "a branch condition is a node this arena has");
            printed += print(arena, b->condition).size();
        }
        // Called on every step, including the ones with no payload of that kind, because that is
        // what the bridge does and a wrong id would be read here rather than refused.
        d.plan(id);
        d.check(id);
    }
    t.check(printed > 0, std::string("the record for ") + what + " carries expressions to show");
}

// A precondition declares what its method is worth before the method runs, and the outcome only
// arrives later, so the two meet in complete_plan_precondition. Every arm is exercised here because
// an engine reaches at most one of them per run, and the two that understate are the ones a bulk
// classification would quietly skip.
void test_evidence_strength(TestSink &t) {
    const auto strength_of = [](const Derivation &d, StepId id, const char *evidence_id) {
        for (size_t i = 0; i < d.at(id).verifications.size(); ++i) {
            if (d.at(id).verifications[i].evidence_id == evidence_id)
                return evidence_strength_name(d.at(id).verifications[i].strength);
        }
        return "no such evidence";
    };

    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.declared-at-registration");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.dimensions",
                                       "the quantities have compatible dimensions",
                                       "dimensional analysis", EvidenceStrength::DimensionallyValid,
                                       VerificationOutcome::Passed, "the dimensions agree");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.equal(strength_of(d, id, "pre.dimensions"), "dimensionally valid",
                "a precondition that passes at registration keeps the kind its method declared");
        t.equal(strength_of(d, id, "strategy.preconditions"), "structurally valid",
                "and the summary answers for the record's own shape, not for what it summarizes");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.declared-then-completed");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.substitution",
                                       "the candidate satisfies the original", "substitution",
                                       EvidenceStrength::CandidateChecked,
                                       VerificationOutcome::NotAttempted, "checked after solving");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        const bool understated_first =
            std::string(strength_of(d, id, "pre.substitution")) == "unsupported";
        d.complete_plan_precondition(id, "pre.substitution", VerificationOutcome::Passed,
                                     "the candidate satisfied it");
        t.check(understated_first, "a method that has not run yet is worth nothing, and says so");
        t.equal(strength_of(d, id, "pre.substitution"), "candidate checked",
                "and passing later is what promotes it to the kind declared at registration");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.disagreed");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.substitution",
                                       "the candidate satisfies the original", "substitution",
                                       EvidenceStrength::CandidateChecked,
                                       VerificationOutcome::NotAttempted, "checked after solving");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        d.complete_plan_precondition(id, "pre.substitution", VerificationOutcome::Failed,
                                     "the candidate did not satisfy it");
        t.equal(strength_of(d, id, "pre.substitution"), "failed",
                "a check that disagreed is worth nothing, whatever its method would have been");
        t.equal(strength_of(d, id, "strategy.preconditions"), "failed",
                "and the summary over it cannot read as sound while it holds a disagreement");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.could-not-run");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.sampled",
                                       "the two sides agree at the sample points",
                                       "exact evaluation at 6 rational assignments",
                                       EvidenceStrength::NumericallyCorroborated,
                                       VerificationOutcome::NotAttempted, "checked after rewriting");
        const StepId id = d.add_plan(kNoStep, std::move(step), std::move(plan));
        d.complete_plan_precondition(id, "pre.sampled", VerificationOutcome::Inconclusive,
                                     "no sample point could be evaluated");
        t.equal(strength_of(d, id, "pre.sampled"), "unsupported",
                "a check that could not run understates rather than claiming its method's kind");
    }
}

// VER-016's fourth part: what a rule does when its obligation is not discharged. Built by hand for
// the same reason criterion 8's arms were, and the reason is sharper here. Not one rule in the
// corpus or the fixtures ever fails a check and then reports a solution, so both arms would sit in
// the tree unexercised, reading as cover. The check-by-substitution rule is the one the requirement
// was written against: it declares that a failed candidate withholds the answer.
void test_failure_behavior(TestSink &t) {
    const auto substitution_failed = [](VerificationOutcome outcome) {
        Step s = envelope("Check the answer", ClaimType::SolutionSetPreserved);
        s.rule_id = "eq.linear.check-by-substitution";
        s.rule_name = "Check by substitution";
        ProofObligation obligation;
        obligation.id = "obl.linear.candidate-satisfies";
        obligation.text = "the candidate satisfies the collected equation";
        s.proof_obligations.push_back(std::move(obligation));
        VerificationRecord v = verification("substitution", outcome, "the left side was not zero");
        v.strength = strength_for(outcome, EvidenceStrength::CandidateChecked);
        s.verifications.push_back(std::move(v));
        return s;
    };

    Arena arena;
    invariants::Pass pass;
    {
        Derivation d;
        d.add_check(kNoStep, substitution_failed(VerificationOutcome::Failed), CheckPayload{});
        d.context.derivation_status = DerivationStatus::VerificationFailed;
        std::vector<std::string> withheld;
        pass.walk(arena, d, true, true, &withheld);
        t.check(!mentions(withheld, "declares that it withholds the result"),
                "a failed candidate check that withholds the answer is what the schema declares");
    }
    {
        Derivation d;
        d.add_check(kNoStep, substitution_failed(VerificationOutcome::Failed), CheckPayload{});
        d.context.derivation_status = DerivationStatus::SolvedAndVerified;
        std::vector<std::string> claimed;
        pass.walk(arena, d, false, false, &claimed);
        t.evidence("VER-016", mentions(claimed, "declares that it withholds the result"),
                   "a rule that failed its obligation cannot report the answer it declared it would "
                   "withhold");
    }
    {
        Derivation d;
        d.add_check(kNoStep, substitution_failed(VerificationOutcome::Passed), CheckPayload{});
        d.context.derivation_status = DerivationStatus::SolvedAndVerified;
        std::vector<std::string> passed;
        pass.walk(arena, d, false, false, &passed);
        t.check(!mentions(passed, "declares that it withholds the result"),
                "and the arm reads the outcome, not the status: a passing check still solves");
    }
    {
        Step s = substitution_failed(VerificationOutcome::Failed);
        VerificationRecord earlier = verification("substitution", VerificationOutcome::Passed,
                                                  "an earlier candidate came out equal");
        earlier.strength = strength_for(VerificationOutcome::Passed,
                                        EvidenceStrength::CandidateChecked);
        s.verifications.insert(s.verifications.begin(), std::move(earlier));
        Derivation d;
        d.add_check(kNoStep, std::move(s), CheckPayload{});
        d.context.derivation_status = DerivationStatus::SolvedAndVerified;
        std::vector<std::string> hidden;
        pass.walk(arena, d, false, false, &hidden);
        t.check(mentions(hidden, "declares that it withholds the result"),
                "a failed record behind a passing one under the same method is still weighed");
    }
    {
        // What a record is worth, held against strength_for on every outcome rather than only on a
        // pass. Eight files write Inconclusive and none overstates one today, so the shape has to be
        // built: a check that could not be computed, filed as though it had confirmed the candidate.
        Step s = substitution_failed(VerificationOutcome::Inconclusive);
        s.verifications[0].strength = EvidenceStrength::CandidateChecked;
        Derivation d;
        d.add_check(kNoStep, std::move(s), CheckPayload{});
        d.context.derivation_status = DerivationStatus::SolvedButUnchecked;
        std::vector<std::string> overstated;
        pass.walk(arena, d, false, false, &overstated);
        t.check(mentions(overstated, "as inconclusive at candidate checked, where a check with "
                                     "that outcome is worth unsupported"),
                "a check that could not run is worth nothing, and a record claiming its method's "
                "own kind is caught saying otherwise");
    }
    {
        Step s = substitution_failed(VerificationOutcome::Failed);
        s.verifications[0].strength = EvidenceStrength::CandidateChecked;
        Derivation d;
        d.add_check(kNoStep, std::move(s), CheckPayload{});
        d.context.derivation_status = DerivationStatus::VerificationFailed;
        std::vector<std::string> disagreed;
        pass.walk(arena, d, false, false, &disagreed);
        t.check(mentions(disagreed, "as failed at candidate checked, where a check with that "
                                    "outcome is worth failed"),
                "and a check that disagreed is worth nothing either, whichever method ran it");
    }
}

// The STEP-007 gate, driven from a hand-built record for the same reason the two arms above are:
// every implication step the engines produce today already carries its obligation and its check, so
// no engine can be asked for the shape the gate exists to refuse. The rule below is the one the
// requirement is written against, a squaring step, which no engine has and which would claim an
// implication when one is written, because that is the only honest claim for a step whose solution
// set can grow.
// The STEP-019 fault arm. No engine produces a step whose provenance is the backend alone, which
// is the requirement holding rather than the check being unnecessary, so the shape it refuses is
// built here by hand the way the VER-016 arms are. The three faults are separate because they fail
// separately: a step can name no rule, produce nothing, or produce something while saying nothing
// about what it did.
void test_backend_step_stands_alone(TestSink &t) {
    const auto consulted = [](const char *rule, const char *action) {
        Step s = envelope("Subtract the components", ClaimType::EquivalentExpression);
        s.rule_id = rule;
        s.rule_name = "Relative velocity component";
        s.backend_requests = 1;
        s.verifications.push_back(verification("Giac Simplify and local canonical comparison",
                                               VerificationOutcome::Passed, "Giac agrees"));
        TransformationPayload p;
        p.concrete_action = action;
        return std::make_pair(std::move(s), std::move(p));
    };

    Arena arena;
    const NodeId before = parse(arena, "7 - 3").root;
    const NodeId after = parse(arena, "4").root;
    invariants::Pass pass;
    {
        Derivation d;
        auto step = consulted("physics.relative-motion.component-i", "7 - 3");
        step.second.before = before;
        StepId id = d.add_transformation(kNoStep, std::move(step.first), std::move(step.second));
        d.complete_transformation(id, after);
        std::vector<std::string> clean;
        pass.walk(arena, d, false, false, &clean);
        t.check(!mentions(clean, "STEP-019"),
                "a step that consulted the backend and recorded its own result is no fault");
    }
    {
        Derivation d;
        auto step = consulted("physics.relative-motion.component-i", "7 - 3");
        step.second.before = before;
        d.add_transformation(kNoStep, std::move(step.first), std::move(step.second));
        std::vector<std::string> unfilled;
        pass.walk(arena, d, false, false, &unfilled);
        t.evidence("STEP-019", mentions(unfilled, "recorded no result of its own"),
                   "a step that consulted the backend and produced nothing itself is a fault, "
                   "because a step with no result of its own can only be showing the backend's");
    }
    {
        Derivation d;
        auto step = consulted("physics.relative-motion.component-i", "");
        step.second.before = before;
        StepId id = d.add_transformation(kNoStep, std::move(step.first), std::move(step.second));
        d.complete_transformation(id, after);
        std::vector<std::string> silent;
        pass.walk(arena, d, false, false, &silent);
        t.check(mentions(silent, "does not say what it did itself"),
                "and one that will not say what it did is a fault too, whatever it recorded");
    }
    {
        Derivation d;
        auto step = consulted("", "7 - 3");
        step.second.before = before;
        StepId id = d.add_transformation(kNoStep, std::move(step.first), std::move(step.second));
        d.complete_transformation(id, after);
        std::vector<std::string> nameless;
        pass.walk(arena, d, false, false, &nameless);
        t.check(mentions(nameless, "names no rule of its own"),
                "and a nameless one is the plainest case of the backend being the only author");
    }
}

// VER-002. The record this is built around is the one issue 237 printed: a step that keeps its
// claim, its rule id, its registered obligation and its passing rule-local verification and moves
// only the node it produced. Every record-reading arm of the pass is satisfied by it, which is why
// the arm that recomputes had to exist, and why the arms below are driven from hand-built records
// rather than from an engine: no engine writes a false equivalence today, so none can be asked for
// the shape the gate refuses.
void test_equivalence_is_checked_against_the_arena(TestSink &t) {
    const auto claimed = [](const char *rule, ClaimType claim) {
        Step s = envelope("Rewrite the expression", claim);
        s.rule_id = rule;
        s.rule_name = "Selftest rewrite";
        s.explanation_short = "Rewriting the expression";
        s.explanation_detailed = "Reach for this when the expression has this shape.";
        ProofObligation obligation;
        obligation.id = "obl.selftest.rule-preserves-value";
        obligation.text = "the rule preserves the requested value";
        s.proof_obligations.push_back(std::move(obligation));
        VerificationRecord v = verification("rule-local invariant", VerificationOutcome::Passed,
                                            "the rule's own invariant held");
        v.strength = EvidenceStrength::StructurallyValid;
        s.verifications.push_back(std::move(v));
        return s;
    };
    const auto walk_claiming = [&](ClaimType claim, Arena &arena, NodeId before, NodeId after,
                                   std::vector<std::string> *broken, invariants::Pass *pass) {
        Derivation d;
        TransformationPayload payload;
        payload.before = before;
        payload.concrete_action = "Rewrite it";
        const StepId id =
            d.add_transformation(kNoStep, claimed("selftest.rewrite", claim), std::move(payload));
        d.complete_transformation(id, after);
        pass->walk(arena, d, false, false, broken);
    };
    const auto walked = [&](Arena &arena, NodeId before, NodeId after,
                            std::vector<std::string> *broken, invariants::Pass *pass) {
        walk_claiming(ClaimType::EquivalentExpression, arena, before, after, broken, pass);
    };

    {
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walked(arena, parse(arena, "2 * x").root, parse(arena, "6").root, &broken, &pass);
        t.evidence("VER-002", mentions(broken, "claims an equivalent expression and its two sides "
                                               "disagree"),
                   "a step claiming 2x equals 6 is caught by the invariant pass, though its claim, "
                   "its obligation and its passing rule-local record are all in order");
        t.check(!mentions(broken, "STEP-002") && !mentions(broken, "VER-016"),
                "and no record-reading arm sees anything wrong with it, which is why this one was "
                "needed");
    }
    {
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walked(arena, parse(arena, "2 * x").root, parse(arena, "x + x").root, &broken, &pass);
        t.check(broken.empty() && pass.equivalence_sampled() == 1 && pass.equivalence_exact() == 0,
                "a rewrite that holds at every assignment tried is recorded as sample agreement "
                "and never as the exact reading");
    }
    {
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walked(arena, parse(arena, "7 - 3").root, parse(arena, "4").root, &broken, &pass);
        t.check(broken.empty() && pass.equivalence_exact() == 1 && pass.equivalence_sampled() == 0,
                "and a rewrite with no symbol left free on either side is settled exactly, which "
                "is the stronger of the two readings and is counted apart from it");
    }
    {
        // #254. A limit head made the whole side unevaluable, so the number the rule produced here
        // was never compared with anything.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        const NodeId question = arena.call(
            "limit", {parse(arena, "2 * x").root, arena.symbol("x"), parse(arena, "3").root});
        walked(arena, question, parse(arena, "7").root, &broken, &pass);
        t.evidence("VER-002",
                   mentions(broken, "claims an equivalent expression and its two sides disagree") &&
                       pass.equivalence_unevaluated() == 0,
                   "a limit step whose after state is not the value its approach point gives is "
                   "caught, where a head the evaluator cannot reduce used to decline the whole "
                   "comparison");
    }
    {
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        const NodeId question = arena.call(
            "limit", {parse(arena, "2 * x").root, arena.symbol("x"), parse(arena, "3").root});
        walked(arena, question, parse(arena, "6").root, &broken, &pass);
        t.check(broken.empty() && pass.equivalence_exact() == 1 &&
                    pass.equivalence_unevaluated() == 0,
                "and the same limit against the value its approach point gives is settled exactly, "
                "which is the control the caught row needs");
    }
    {
        // #244. An after state leaving free a symbol the before state never had is a family rather
        // than one expression, so a step claiming both is reported instead of being counted and
        // left alone. Sampling would give C a value the left side never carried and report a
        // disagreement about the sampler, which is why the arm names the symbol rather than a point.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walked(arena, parse(arena, "x^2").root, parse(arena, "x^2 + C").root, &broken, &pass);
        t.evidence("VER-002",
                   mentions(broken, "claims an equivalent expression while its after state leaves "
                                    "free the symbol C") &&
                       pass.equivalence_sampled() == 0 && pass.equivalence_exact() == 0,
                   "a step claiming an equivalent expression while its after state introduces a "
                   "free constant is refused, because the two differ by that constant at every "
                   "assignment giving it a nonzero value");
    }
    {
        // The claim that shape means, judged rather than declined. The constant comes off and what
        // is left is put to the same sampler the equivalence arm uses.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "x^2 + C").root, &broken, &pass);
        t.check(broken.empty() && pass.family_steps() == 1 && pass.family_judged() == 1,
                "and the same two states under a family claim are read as that family, which is "
                "the population the arm used to decline");
    }
    {
        // Three ways the family claim is wrong, each of which the declining arm accepted in
        // silence. The constant on the wrong expression is the one an integration rule would
        // actually reach.
        Arena arena;
        invariants::Pass wrong_body;
        std::vector<std::string> caught;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "2 * x + C").root, &caught, &wrong_body);
        t.check(mentions(caught, "claims a family up to a constant and what is left under its "
                                 "constant disagrees") &&
                    wrong_body.family_judged() == 0,
                "a family whose constant sits on an expression that is not the one it was given is "
                "refused at the disagreement the sampler found");

        invariants::Pass no_constant;
        std::vector<std::string> single;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "x * x").root, &single, &no_constant);
        t.check(mentions(single, "leaves no new symbol free, so it names one expression rather "
                                 "than a family"),
                "and a step claiming a family while naming one expression is refused, which is the "
                "reading that keeps the claim from becoming a way to say nothing");

        invariants::Pass not_added;
        std::vector<std::string> multiplied;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "x^2 * C").root, &multiplied, &not_added);
        t.check(mentions(multiplied, "is not the before state with a constant added to it"),
                "and a constant that multiplies rather than adds is not the family this claim "
                "names");
    }
    {
        // #329. The three read_family faults nothing had put to it, each picking a refusal's sentence.
        Arena arena;

        invariants::Pass two_symbols;
        std::vector<std::string> several;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "x^2 + C * D").root, &several, &two_symbols);
        t.check(mentions(several, "introduces 2 free symbols where a family up to a constant "
                                  "introduces one") &&
                    two_symbols.family_judged() == 0,
                "a family whose after state leaves two symbols free is refused for the count it "
                "introduced, which is read before the shape carrying them is looked at");

        invariants::Pass carried_twice;
        std::vector<std::string> still_free;
        walk_claiming(ClaimType::FamilyUpToConstant, arena, parse(arena, "x^2").root,
                      parse(arena, "x^2 + C + C").root, &still_free, &carried_twice);
        t.check(mentions(still_free, "what is left under its constant still leaves free a symbol "
                                     "the before state never had") &&
                    carried_twice.family_judged() == 0,
                "and an after state carrying its constant twice is refused for the symbol left "
                "under it rather than for a disagreement, because taking one C off x^2 + C + C "
                "leaves a family instead of the expression it was given");

        invariants::Pass unfinished;
        std::vector<std::string> unrecorded;
        Derivation d;
        TransformationPayload payload;
        payload.before = parse(arena, "x^2").root;
        payload.concrete_action = "Rewrite it";
        d.add_transformation(kNoStep, claimed("selftest.rewrite", ClaimType::FamilyUpToConstant),
                             std::move(payload));
        unfinished.walk(arena, d, false, false, &unrecorded);
        t.check(mentions(unrecorded, "claims a family up to a constant and one of its two states "
                                     "is missing") &&
                    unfinished.family_steps() == 1 && unfinished.family_judged() == 0,
                "and a family claim whose transformation never recorded an after state is counted "
                "into the population and refused for the missing state, which is what keeps the "
                "readings under it from asking the arena for a node it does not hold");
    }
    {
        // The one symbol the new-symbol arm must not read as a variable. A conversion carries the
        // unit's name as the first argument of its unit() call, so cm^3 becoming m^3 introduces a
        // symbol that is a label rather than a free variable, and the gate above reported it as a
        // false equivalence until the reading learned the difference.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        const NodeId source =
            arena.call("quantity", {parse(arena, "5").root,
                                    arena.call("unit", {arena.symbol("cm^3"),
                                                        parse(arena, "1/1000000").root})});
        const NodeId target =
            arena.call("quantity", {parse(arena, "5/1000000").root,
                                    arena.call("unit", {arena.symbol("m^3"), parse(arena, "1").root})});
        walked(arena, source, target, &broken, &pass);
        t.check(!mentions(broken, "VER-002"),
                "a conversion whose after state names a different unit is not a step introducing a "
                "free symbol, because the unit's name is a label on the quantity rather than a "
                "variable in it");
    }
    {
        // MATH-014 and VER-009 in one record: a disagreement at a single assignment is proof the
        // two differ, so the gate fires on a rewrite that holds at five of six points.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        walked(arena, parse(arena, "x^2").root, parse(arena, "x * x + 0").root, &broken, &pass);
        t.check(broken.empty(), "an identity that holds everywhere is left alone");

        invariants::Pass narrow;
        std::vector<std::string> caught;
        walked(arena, parse(arena, "x^2").root, parse(arena, "2 * x").root, &caught, &narrow);
        t.check(mentions(caught, "claims an equivalent expression and its two sides disagree") &&
                    narrow.equivalence_sampled() == 0,
                "and a rewrite agreeing at x = 2 alone is refused, because the sampler tries more "
                "than the one point a wrong rule happens to be right at");
    }
    {
        // The exemption, held to the same shape criterion 6 uses. A step whose job is to report a
        // rounded value disagrees with its own input by construction, and reading that as a false
        // equivalence would make the gate fire on every rounding rule in the tree.
        Arena arena;
        invariants::Pass pass;
        std::vector<std::string> broken;
        Derivation d;
        Step s = claimed("selftest.report-significant-figures", ClaimType::EquivalentExpression);
        TransformationPayload payload;
        payload.before = parse(arena, "1 / 3").root;
        payload.concrete_action = "Report it as a decimal";
        const StepId id = d.add_transformation(kNoStep, std::move(s), std::move(payload));
        d.complete_transformation(id, arena.decimal("0.333"));
        pass.walk(arena, d, false, false, &broken);
        t.check(broken.empty() && pass.equivalence_steps() == 0,
                "the step that declares it rounds is outside the claim, the same exemption "
                "criterion 6 grants it, and is not counted in the population either");
    }
}

void test_implication_owes_a_check(TestSink &t) {
    const auto squared = [](bool with_obligation, VerificationOutcome outcome) {
        Step s = envelope("Square both sides", ClaimType::Implication);
        s.rule_id = "eq.square-both-sides";
        s.rule_name = "Square both sides";
        if (with_obligation) {
            ProofObligation obligation;
            obligation.id = "obl.square.candidate-satisfies";
            obligation.text = "the candidate satisfies the equation before squaring";
            s.proof_obligations.push_back(std::move(obligation));
        }
        if (outcome != VerificationOutcome::NotAttempted) {
            VerificationRecord v =
                verification("substitution", outcome, "the candidate was put back before squaring");
            v.strength = strength_for(outcome, EvidenceStrength::CandidateChecked);
            s.verifications.push_back(std::move(v));
        }
        return s;
    };

    Arena arena;
    invariants::Pass pass;
    {
        Derivation d;
        d.add_check(kNoStep, squared(false, VerificationOutcome::Passed), CheckPayload{});
        std::vector<std::string> unowed;
        pass.walk(arena, d, false, false, &unowed);
        t.evidence("STEP-007", mentions(unowed, "raises no obligation"),
                   "a step that may widen the solution set and raises no obligation for its "
                   "candidate is a fault, not a step");
    }
    {
        Derivation d;
        d.add_check(kNoStep, squared(true, VerificationOutcome::NotAttempted), CheckPayload{});
        std::vector<std::string> unattempted;
        pass.walk(arena, d, false, false, &unattempted);
        t.check(mentions(unattempted, "attempted no verification"),
                "and declaring the obligation without running the check does not discharge it");
    }
    {
        // Failed, not passed, because the requirement is that the check runs rather than that the
        // candidate survives it. A gate reading the outcome would call the catch-up domain check a
        // fault at the moment it caught an extraneous root.
        Derivation d;
        d.add_check(kNoStep, squared(true, VerificationOutcome::Failed), CheckPayload{});
        std::vector<std::string> checked;
        pass.walk(arena, d, false, false, &checked);
        t.check(!mentions(checked, "claims an implication"),
                "while a check that ran and rejected the candidate is the gate satisfied");
    }
}

// Every status, so adding one forces a decision here rather than leaving it to whichever caller
// happens to compare against a list.
void test_status_carries_answer(TestSink &t) {
    const DerivationStatus carrying[] = {
        DerivationStatus::SolvedAndVerified,    DerivationStatus::ConditionallySolved,
        DerivationStatus::NumericallyApproximated, DerivationStatus::SolvedButUnchecked,
        DerivationStatus::SolvedAndCorroborated};
    const DerivationStatus withholding[] = {
        DerivationStatus::NotRecorded,        DerivationStatus::PartiallySolved,
        DerivationStatus::Unsupported,        DerivationStatus::InvalidInput,
        DerivationStatus::ClarificationRequired, DerivationStatus::InterpretationUnsupported,
        DerivationStatus::ModelCommitFailed,  DerivationStatus::VerificationFailed,
        DerivationStatus::ResourceLimitReached, DerivationStatus::OpaqueSubproblem,
        DerivationStatus::DependencyUnavailable, DerivationStatus::Cancelled};
    size_t counted = 0;
    for (const DerivationStatus status : carrying) {
        ++counted;
        t.check(status_carries_answer(status),
                std::string(derivation_status_name(status)) + " reports an answer beside itself");
    }
    for (const DerivationStatus status : withholding) {
        ++counted;
        t.check(!status_carries_answer(status),
                std::string(derivation_status_name(status)) + " reports no answer at all");
    }
    t.check(counted == static_cast<size_t>(DerivationStatus::SolvedAndCorroborated) + 1,
            "the two lists together name every status the enumeration holds");
}

// Asked here rather than through an engine because the three answers need three different
// verification outcomes in the record, and an engine that produces a Failed one refuses long before
// it reaches this predicate. Testing the mapping directly is what makes the third answer more than
// an assertion about code nobody can run.
void test_outcome_from(TestSink &t) {
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.all-passed");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.passed", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, "the input matches");
        d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.equal(derivation_status_name(d.outcome_from(0)), "solved and verified",
                "every check passing is the only route to a verified claim");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.inconclusive");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.inconclusive",
                                       "the input has the required form", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Inconclusive, "the form is unknown");
        d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.equal(derivation_status_name(d.outcome_from(0)), "solved but unchecked",
                "a check that could not run leaves the answer standing but unconfirmed");
    }
    {
        Derivation d;
        PlanPayload plan = plan_payload("strategy.failed");
        Step step = plan_envelope(plan.strategy_id.c_str());
        register_strategy_precondition(plan, step, "pre.failed", "the input has the required form",
                                       "structural check", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Failed, "the input does not match");
        d.add_plan(kNoStep, std::move(step), std::move(plan));
        t.equal(derivation_status_name(d.outcome_from(0)), "verification failed",
                "a check that disagreed says so, rather than reporting less work done");
    }
    {
        // The ordering that the old two-valued form could not express: an inconclusive record before
        // a failed one. Whichever comes first, a disagreement is the answer.
        Derivation d;
        PlanPayload first = plan_payload("strategy.inconclusive-first");
        Step first_step = plan_envelope(first.strategy_id.c_str());
        register_strategy_precondition(first, first_step, "pre.inconclusive",
                                       "the input has the required form", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Inconclusive, "the form is unknown");
        d.add_plan(kNoStep, std::move(first_step), std::move(first));
        PlanPayload second = plan_payload("strategy.failed-second");
        Step second_step = plan_envelope(second.strategy_id.c_str());
        register_strategy_precondition(second, second_step, "pre.failed",
                                       "the input has the required form", "structural check",
                                       EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Failed, "the input does not match");
        d.add_plan(kNoStep, std::move(second_step), std::move(second));
        t.equal(derivation_status_name(d.outcome_from(0)), "verification failed",
                "and it outranks an inconclusive record recorded before it");
        t.evidence("VER-013", d.outcome_from(0) == DerivationStatus::VerificationFailed,
                   "a verification that disagreed is reported as a failed verification rather than "
                   "as a solve that stopped early");
    }
    {
        // Section 15 names neither of these, and it asks for at least its list rather than exactly
        // it. Until they had names both were PartiallySolved, which is a false statement about a
        // cancellation and a false statement about an answer whose check merely could not run.
        t.equal(derivation_status_name(DerivationStatus::Cancelled), "cancelled",
                "a user cancellation that kept a prefix has its own name");
        t.equal(derivation_status_name(DerivationStatus::SolvedButUnchecked), "solved but unchecked",
                "and so does an answer whose independent check could not run");
        t.equal(derivation_status_name(DerivationStatus::SolvedAndCorroborated),
                "solved and corroborated",
                "and so does an answer whose check ran, agreed, and was not independent of it");
        t.check(derivation_status_in_range(
                    static_cast<uint64_t>(DerivationStatus::SolvedAndCorroborated)),
                "all three are readable back out of a serialized context");
        t.check(!derivation_status_in_range(
                    static_cast<uint64_t>(DerivationStatus::SolvedAndCorroborated) + 1),
                "and the bound still refuses the value above them");
    }
}

void test_payload_expressions(TestSink &t) {
    {
        Arena arena;
        Derivation d;
        NodeId eq = parse(arena, "2x + 5 = 13").root;
        SolveResult r = solve_linear(arena, d, eq, arena.symbol("x"));
        t.check(r.outcome == SolveOutcome::Solved, "the worked solve still solves");
        const bool plan_verified = d.roots().size() == 1 && d.at(d.roots()[0]).verified();
        const PlanPayload *plan = plan_verified ? d.plan(d.roots()[0]) : nullptr;
        t.evidence("VER-013",
                   plan != nullptr && plan->strategy_id == "eq.linear.inverse-operations" &&
                       plan->preconditions.size() == 2,
                   "the linear producer emits a stable strategy with verified preconditions");
        walk_like_the_bridge(t, arena, d, "2x + 5 = 13");
    }

    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x)").root;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"));
        t.check(r.outcome == DiffOutcome::Differentiated, "the worked derivative still differentiates");
        const bool plan_verified = d.roots().size() == 1 && d.at(d.roots()[0]).verified();
        const PlanPayload *plan = plan_verified ? d.plan(d.roots()[0]) : nullptr;
        t.evidence("VER-013",
                   plan != nullptr && plan->strategy_id == "calculus.differentiate.rules" &&
                       plan->preconditions.size() == 1,
                   "the differentiation producer verifies its registered rule precondition");
        walk_like_the_bridge(t, arena, d, "x^2*sin(x)");
    }

    {
        // A halted solve rewinds its record. Whatever survives still has to be printable, because
        // the bridge walks whatever is there rather than asking how it got that way.
        Arena arena;
        Derivation d;
        Budget tight;
        tight.max_steps = 1;
        NodeId eq = parse(arena, "2x + 5 = 13").root;
        solve_linear(arena, d, eq, arena.symbol("x"), tight);
        for (size_t i = 0; i < d.size(); ++i) {
            const TransformationPayload *tr = d.transformation(static_cast<StepId>(i));
            if (tr && tr->before != kNoNode)
                t.check(tr->before < arena.node_count(), "a rewound record leaves no dangling node");
        }
        t.check(true, "walking a halted record does not fault");
    }
}

// Whether the records from one checkpoint on repeat the records from another, id for id, with
// parents shifted by the distance between them.
bool same_run(const Derivation &d, size_t first, size_t second, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const Step &a = d.at(static_cast<StepId>(first + i));
        const Step &b = d.at(static_cast<StepId>(second + i));
        if (a.kind != b.kind || a.rule_id != b.rule_id || a.goal != b.goal ||
            a.verifications.size() != b.verifications.size() ||
            a.children.size() != b.children.size())
            return false;
        const StepId shifted =
            a.parent == kNoStep ? kNoStep : static_cast<StepId>(a.parent + (second - first));
        if (b.parent != shifted)
            return false;
        const TransformationPayload *pa = d.transformation(a.id);
        const TransformationPayload *pb = d.transformation(b.id);
        if ((pa == nullptr) != (pb == nullptr))
            return false;
        if (pa && (pa->before != pb->before || pa->after != pb->after))
            return false;
    }
    return true;
}

void test_remembered_runs(TestSink &t) {
    {
        Limits limits;
        limits.max_nodes = 128;
        Arena arena(limits);
        Derivation d;
        const NodeId equation = parse(arena, "2*x+5=13").root;
        const NodeId unknown = arena.symbol("x");
        const SolveResult first = solve_linear(arena, d, equation, unknown);
        t.check(first.outcome == SolveOutcome::Solved, "the arena refusal case seeds a checked run");
        for (size_t i = 0; i <= limits.max_nodes && !arena.failed(); ++i)
            arena.symbol("extra" + std::to_string(i));
        t.check(arena.failed(), "the arena refusal case reaches its node limit");
        const SolveResult repeated = solve_linear(arena, d, equation, unknown);
        Derivation uncached;
        const SolveResult refused = solve_linear(arena, uncached, equation, unknown);
        t.check(repeated.outcome != SolveOutcome::Solved && repeated.outcome == refused.outcome &&
                    repeated.status == refused.status &&
                    repeated.solution == kNoNode && repeated.cost.replayed == 0,
                "a cached solve cannot bypass sticky arena failure");
    }
    {
        for (size_t rewrites : {size_t{0}, size_t{32}}) {
            size_t polls = 0;
            Budget budget;
            budget.poll = [](void *context) { return ++*static_cast<size_t *>(context) > 1; };
            budget.poll_context = &polls;
            Meter meter(budget);
            for (size_t i = 0; i < rewrites; ++i)
                meter.rewrite();
            for (size_t i = 0; i < 128 && meter.replay(); ++i) {}
            t.check(meter.halt() == Halt::Cancelled && polls == 2 &&
                        meter.cost().rewrites + meter.cost().replayed < 64,
                    "replayed and derived work share a bounded cancellation polling stride");
        }
    }
    {
        size_t polls = 0;
        Budget budget;
        budget.poll = [](void *context) { return ++*static_cast<size_t *>(context) > 1; };
        budget.poll_context = &polls;
        Meter meter(budget);
        t.check(!meter.stopped() && polls == 1, "the entry poll leaves an uncancelled solve running");
        t.check(!meter.backend_call(), "a cancel that arrives before a backend call stops it");
        t.evidence("PERF-003", meter.halt() == Halt::Cancelled && polls == 2,
                   "the longest wait in a solve asks on every call rather than on the shared "
                   "stride, which a short route never reaches");
    }
    {
        size_t polls = 0;
        Budget budget;
        budget.poll = [](void *context) { return ++*static_cast<size_t *>(context) > 1; };
        budget.poll_context = &polls;
        Meter meter(budget);
        for (size_t i = 0; i < 32 && meter.step(); ++i) {}
        t.check(!meter.stopped() && polls == 1, "half a stride of steps is still inside it");
        for (size_t i = 0; i < 32 && meter.branch(); ++i) {}
        t.evidence("PERF-003", meter.halt() == Halt::Cancelled && polls == 2,
                   "recorded steps and branches advance the same cancellation stride as rewrites, "
                   "so a solve that only records cannot outrun the poll");
    }
    {
        // The second solve of one equation appends the checked run the first one left, and spends
        // no rewrites doing it. The replayed count is the behaviour: the records alone read the
        // same whether they were derived or copied.
        Arena arena;
        Derivation d;
        const NodeId eq = parse(arena, "2x + 5 = 13").root;
        const NodeId x = arena.symbol("x");
        SolveResult first = solve_linear(arena, d, eq, x);
        const size_t run = d.size();
        t.check(first.outcome == SolveOutcome::Solved && first.cost.rewrites > 0 &&
                    first.cost.replayed == 0 && run > 1,
                "the first solve derives its run and replays nothing");

        SolveResult second = solve_linear(arena, d, eq, x);
        t.check(second.outcome == SolveOutcome::Solved && second.solution == first.solution,
                "the second solve of the same equation reaches the same answer node");
        t.equal(std::to_string(second.cost.replayed), std::to_string(run),
                "by replaying every record of the first run");
        t.check(second.cost.rewrites == 0, "and deriving nothing");
        t.check(d.size() == 2 * run && same_run(d, 0, run, run),
                "the replayed records repeat the derived ones, parents shifted, payloads intact");
        t.check(d.all_verified_from(run) && second.status == DerivationStatus::SolvedAndVerified,
                "and they carry their passed verifications with them");
        t.check(d.roots().size() == 2, "each run is a root of its own");

        SolveResult other_unknown = solve_linear(arena, d, eq, arena.symbol("y"));
        t.check(other_unknown.cost.replayed == 0, "the same equation in another unknown is a miss");
        SolveResult other_equation = solve_linear(arena, d, parse(arena, "3x + 5 = 14").root, x);
        t.check(other_equation.outcome == SolveOutcome::Solved && other_equation.cost.replayed == 0 &&
                    other_equation.cost.rewrites > 0,
                "and another equation is derived on its own");
    }

    {
        // A halted run commits nothing. PERF-013 forbids reusing unverified intermediate state, and
        // the run a step limit cut short is exactly that, so the next full solve derives afresh.
        Arena arena;
        Derivation d;
        const NodeId eq = parse(arena, "2x + 5 = 13").root;
        const NodeId x = arena.symbol("x");
        Budget tight;
        tight.max_steps = 2;
        SolveResult cut = solve_linear(arena, d, eq, x, tight);
        t.check(cut.outcome == SolveOutcome::ResourceExceeded && d.size() > 0,
                "a step budget stops the first solve with a checked prefix kept");
        const size_t kept = d.size();
        SolveResult again = solve_linear(arena, d, eq, x);
        t.check(again.outcome == SolveOutcome::Solved && again.cost.replayed == 0 &&
                    again.cost.rewrites > 0,
                "and the full solve after it derives rather than replaying the cut run");

        // Replay is metered like derivation. A budget too small for the remembered run halts part
        // way and the wrapper keeps the verified prefix, as it does for a derived run.
        const size_t whole = d.size() - kept;
        Budget shorter;
        shorter.max_steps = whole - 1;
        SolveResult partial = solve_linear(arena, d, eq, x, shorter);
        t.check(partial.outcome == SolveOutcome::ResourceExceeded && partial.solution == kNoNode,
                "a replay that runs out of steps is a resource halt with no answer offered");
        t.check(partial.cost.replayed == whole - 1 && partial.cost.replayed > 0,
                "having replayed up to the limit");
        t.check(d.all_verified_from(kept + whole) && d.size() < kept + 2 * whole,
                "and keeping only the checked prefix of the replay");
    }

    {
        // A scratch derivation sharing the owner's memory, which is how kinematics probes a candidate
        // equation and then solves the chosen one for real. The mode is not in the key: the probe
        // runs exact and the real solve may report in decimals, which is a step after the run.
        Arena arena;
        Derivation owner;
        owner.request.numeric_mode = NumericMode::Decimal;
        Derivation scratch;
        scratch.share_runs(owner);
        const NodeId eq = parse(arena, "(2*x) = 5").root;
        const NodeId x = arena.symbol("x");
        SolveResult probe = solve_linear(arena, scratch, eq, x);
        t.check(probe.outcome == SolveOutcome::Solved && probe.cost.replayed == 0 &&
                    owner.size() == 0,
                "the probe derives into the scratch and leaves the owner's record alone");

        SolveResult real = solve_linear(arena, owner, eq, x);
        t.check(real.outcome == SolveOutcome::Solved && real.cost.rewrites == 0,
                "the owner's solve of the probed equation derives nothing");
        t.equal(std::to_string(real.cost.replayed), std::to_string(scratch.size()),
                "replaying the probe's whole run");
        t.check(owner.size() == scratch.size() + 1 && arena.at(real.solution).kind == Kind::Decimal,
                "then reporting the answer in decimals as its own mode asks, one step after");
        t.check(owner.all_verified_from(0) && real.status == DerivationStatus::SolvedAndVerified,
                "with every record checked");
    }
}

void test_rule_schema_coverage(TestSink &t) {
    const auto declares = [&t](const char *rule_id, ClaimType claim, FailureBehavior on_failure,
                               const char *obligation_id, const char *method,
                               EvidenceStrength strength) {
        const RuleSchema *schema = rule_schema(rule_id);
        t.check(schema && schema->claim == claim && schema->on_failure == on_failure &&
                    schema->obligation_count == 1 &&
                    std::string(schema->obligations[0].id) == obligation_id &&
                    schema->obligations[0].evidence_count == 1 &&
                    std::string(schema->obligations[0].evidence[0].method) == method &&
                    schema->obligations[0].evidence[0].strength == strength,
                std::string(rule_id) +
                    " declares its emitted claim, obligation, evidence and failure behavior");
    };

    const RuleSchema *cross_check = rule_schema("calculus.differentiate.giac-cross-check");
    t.check(cross_check && cross_check->claim == ClaimType::NoClaim &&
                cross_check->on_failure == FailureBehavior::WithholdResult &&
                cross_check->obligation_count == 1 &&
                std::string(cross_check->obligations[0].id) == "obl.differentiate.matches-backend",
            "the derivative cross-check declares its recorded obligation");

    const RuleSchema *handover = rule_schema("kin.coupled.handover");
    t.check(handover && handover->claim == ClaimType::NoClaim &&
                handover->on_failure == FailureBehavior::CannotFail &&
                handover->obligation_count == 0,
            "the coupled-body handover declares its no-claim record");

    const RuleSchema *forces = rule_schema("physics.forces.plan");
    t.check(forces && forces->claim == ClaimType::NoClaim &&
                forces->on_failure == FailureBehavior::WithholdResult &&
                forces->obligation_count == 5,
            "the force plan declares its four preconditions and summary");

    const RuleSchema *ranking = rule_schema("physics.ranking.order");
    t.check(ranking && ranking->claim == ClaimType::NoClaim &&
                ranking->on_failure == FailureBehavior::WithholdResult &&
                ranking->obligation_count == 0,
            "the ranking order declares its no-claim check");

    const RuleSchema *criterion = rule_schema("physics.ranking.criterion");
    t.check(criterion && criterion->claim == ClaimType::NoClaim &&
                criterion->on_failure == FailureBehavior::WithholdResult &&
                criterion->obligation_count == 0,
            "all ranking criteria share one no-claim schema");

    const RuleSchema *vanishing = rule_schema("physics.term-vanishes");
    t.check(vanishing && vanishing->claim == ClaimType::EquivalentExpression &&
                vanishing->on_failure == FailureBehavior::CannotFail &&
                vanishing->obligation_count == 1 &&
                std::string(vanishing->obligations[0].id) ==
                    "obl.physics.zero-factor-eliminates-term",
            "a vanishing term declares its zero-factor obligation");

    declares("physics.forces.check-input-dimensions", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.forces.input-dimensions", "dimensional analysis",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.check-angle", ClaimType::Definition, FailureBehavior::WithholdResult,
             "obl.forces.exact-angle",
             "trigonometric identity", EvidenceStrength::StructurallyValid);
    declares("physics.forces.weight", ClaimType::EquivalentExpression,
             FailureBehavior::CannotFail,
             "obl.forces.weight-components", "exact rational product",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.normal-force", ClaimType::SolutionSetPreserved,
             FailureBehavior::CannotFail,
             "obl.forces.normal-from-balance", "exact across-axis sum",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.third-law-pairs", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.forces.pairs-separate", "inventory and pair comparison",
             EvidenceStrength::StructurallyValid);
    declares("physics.forces.kinetic-friction", ClaimType::EquivalentExpression,
             FailureBehavior::CannotFail,
             "obl.forces.kinetic-friction", "exact rational product",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.static-friction-limit", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.forces.static-within-limit", "exact comparison of required friction against mu_s N",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.solve-unknown", ClaimType::SolutionSetPreserved,
             FailureBehavior::CannotFail,
             "obl.forces.unknown-isolated", "exact rearrangement",
             EvidenceStrength::DimensionallyValid);
    declares("physics.forces.check-residual", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.forces.residual-zero", "exact substitution into the along-axis sum",
             EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
    declares("physics.forces.check-result-dimension", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.forces.result-dimension", "dimensional analysis",
             EvidenceStrength::DimensionallyValid);

    declares("physics.relative-motion.significant-figures", ClaimType::NoClaim,
             FailureBehavior::WithholdResult,
             "obl.relative-motion.rounding-within-half-place",
             "exact comparison against the unrounded value", EvidenceStrength::CandidateChecked);
    declares("physics.relative-motion.bearing-convention", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.relative-motion.bearing-convention-stated",
             "nearest cardinal by component magnitude", EvidenceStrength::StructurallyValid);
    declares("physics.relative-motion.subscript-cancellation", ClaimType::Definition,
             FailureBehavior::WithholdResult,
             "obl.relative-motion.subscript-cancellation", "subscript chain",
             EvidenceStrength::StructurallyValid);
    declares("physics.relative-motion.isolate-unknown", ClaimType::EquivalentExpression,
             FailureBehavior::CannotFail,
             "obl.relative-motion.isolate-before-substitute",
             "symbolic rearrangement of the subscript identity",
             EvidenceStrength::StructurallyValid);

    declares("physics.work.giac-cross-check", ClaimType::Definition,
             FailureBehavior::WithholdResult, "obl.work.backend-agrees",
             "Giac Adapter Op::Dot and local canonical comparison",
             EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
}

void run_derivation_tests(TestSink &t) {
    Arena arena;
    Derivation d;

    // The PRD's own worked example, section 10.
    PlanPayload plan_payload;
    plan_payload.strategy_id = "eq.linear.inverse-operations";
    plan_payload.selected_strategy = "Inverse operations";
    plan_payload.matched_problem_facts.push_back("one unknown");
    plan_payload.matched_problem_facts.push_back("degree one");
    plan_payload.alternatives_considered.push_back("Factoring");
    plan_payload.selection_rationale =
        "the equation is already in a form inverse operations clear in two moves";
    Step plan_step = envelope("Isolate x", ClaimType::NoClaim);
    plan_step.rule_id = plan_payload.strategy_id;
    register_strategy_precondition(plan_payload, plan_step, "pre.linear-form",
                                   "the equation is linear in x", "structural degree check",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed, "degree one in x");
    StepId plan = d.add_plan(kNoStep, std::move(plan_step), std::move(plan_payload));

    TransformationPayload sub;
    sub.before = parse(arena, "2x + 5 = 13").root;
    sub.after = parse(arena, "2x = 8").root;
    sub.concrete_action = "Subtract 5 from both sides";
    sub.reversible = true;

    Step step = envelope("Isolate x", ClaimType::SolutionSetPreserved);
    step.rule_id = "eq.subtract-both-sides";
    step.rule_name = "Subtraction property of equality";
    step.explanation_short = "Applying the same operation to both sides preserves equality";
    step.verifications.push_back(
        verification("rule-local equality invariant", VerificationOutcome::Passed));
    StepId first = d.add_transformation(plan, std::move(step), std::move(sub));

    t.check(d.size() == 2, "two records were added");
    t.check(d.roots().size() == 1, "the plan is the only root");
    t.check(d.at(first).parent == plan, "the transformation hangs off the plan");
    t.check(d.at(plan).children.size() == 1, "the plan knows its child");

    t.check(d.transformation(first) != nullptr, "a transformation record has its payload");
    t.check(d.plan(first) == nullptr,
            "and asking a transformation for a plan payload gets nothing rather than a blank one");
    t.check(d.plan(plan) != nullptr, "a plan record has its payload");
    t.check(d.transformation(plan) == nullptr, "and not a transformation payload");
    t.check(d.branch(first) == nullptr && d.check(first) == nullptr,
            "nor the two payloads it is not");

    t.equal(print(arena, d.transformation(first)->before), "(((2 * x) + 5) = 13)",
            "the before expression is the AST, not formatted text");
    t.equal(print(arena, d.transformation(first)->after), "((2 * x) = 8)",
            "and so is the after expression");
    const TransformationPayload *recorded = d.transformation(first);
    t.evidence("STEP-002",
               recorded != nullptr && recorded->before != kNoNode && recorded->after != kNoNode &&
                   !d.at(first).rule_id.empty() && !d.at(first).rule_name.empty() &&
                   !d.at(first).explanation_short.empty(),
               "a transformation records before, after, rule id, rule name and explanation");

    t.check(d.at(first).verified(), "a passed check makes the step verified");
    t.check(!d.at(first).has_failed_verification(), "and nothing failed");

    {
        Step unchecked = envelope("Isolate x", ClaimType::EquivalentExpression);
        StepId id = d.add_transformation(first, std::move(unchecked), TransformationPayload{});
        t.check(!d.at(id).verified(),
                "a claim with no verification record is not verified, which is what section 17 "
                "requires to be labelled");
    }

    {
        Step failed = envelope("Isolate x", ClaimType::EquivalentExpression);
        failed.verifications.push_back(
            verification("giac cross-check", VerificationOutcome::Passed));
        failed.verifications.push_back(
            verification("substitution", VerificationOutcome::Failed, "x = 4 does not satisfy"));
        StepId id = d.add_transformation(first, std::move(failed), TransformationPayload{});
        t.check(!d.at(id).verified(),
                "one failed check outweighs a passed one, rather than the other way round");
        t.check(d.at(id).has_failed_verification(), "and the failure is visible on its own");
    }

    {
        Step inconclusive = envelope("Isolate x", ClaimType::EquivalentExpression);
        inconclusive.verifications.push_back(
            verification("rule-local invariant", VerificationOutcome::Passed));
        inconclusive.verifications.push_back(
            verification("giac cross-check", VerificationOutcome::Inconclusive));
        StepId id =
            d.add_transformation(first, std::move(inconclusive), TransformationPayload{});
        t.evidence("VER-014", !d.at(id).verified(),
                   "an inconclusive check cannot be masked by a passing check");
    }

    {
        Step not_attempted = envelope("Isolate x", ClaimType::EquivalentExpression);
        not_attempted.verifications.push_back(
            verification("rule-local invariant", VerificationOutcome::Passed));
        not_attempted.verifications.push_back(
            verification("candidate substitution", VerificationOutcome::NotAttempted));
        StepId id =
            d.add_transformation(first, std::move(not_attempted), TransformationPayload{});
        t.evidence("VER-014", !d.at(id).verified(),
                   "a required check that was not attempted cannot pass");
    }

    {
        Step implication = envelope("Square both sides", ClaimType::Implication);
        implication.verifications.push_back(
            verification("forward implication", VerificationOutcome::Passed));
        TransformationPayload candidate_changing;
        candidate_changing.reversible = false;
        StepId id =
            d.add_transformation(first, std::move(implication), std::move(candidate_changing));
        const TransformationPayload *payload = d.transformation(id);
        t.evidence("STEP-006",
                   payload != nullptr && !payload->reversible &&
                       d.at(id).claim == ClaimType::Implication,
                   "a candidate-changing transformation is distinct from a reversible one");
    }

    {
        Step failed_no_claim = envelope("Check strategy", ClaimType::NoClaim);
        failed_no_claim.verifications.push_back(
            verification("strategy precondition", VerificationOutcome::Failed));
        StepId id = d.add_check(first, std::move(failed_no_claim), CheckPayload{});
        t.evidence("VER-014", !d.at(id).verified(),
                   "a failed check cannot pass merely because its step makes no claim");
    }

    {
        Step nothing = envelope("Restate the problem", ClaimType::NoClaim);
        CheckPayload nothing_to_check;
        nothing_to_check.target_claim = "none";
        StepId id = d.add_check(kNoStep, std::move(nothing), std::move(nothing_to_check));
        t.check(d.at(id).verified(),
                "a record that claims nothing needs no proof, which is not the same as being proven");
        t.check(d.roots().size() == 2, "a record with no parent is a second root");
    }

    test_absent_step(t);
    test_absent_parent(t);
    test_rewind(t);
    test_adopt_roots(t);
    test_ver019_an_assumption_cannot_pass_a_failed_check(t);
    test_verified_prefix(t);
    test_branch_and_cycle_limits(t);
    test_outcome_from(t);
    test_status_carries_answer(t);
    test_evidence_strength(t);
    test_failure_behavior(t);
    test_implication_owes_a_check(t);
    test_backend_step_stands_alone(t);
    test_equivalence_is_checked_against_the_arena(t);
    test_payload_expressions(t);
    test_plan_preconditions(t);
    test_plan_records_its_applicability(t);
    test_remembered_runs(t);
    test_rule_schema_coverage(t);
}

}  // namespace nps
