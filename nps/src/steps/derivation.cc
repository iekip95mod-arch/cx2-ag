#include "nps/steps/derivation.h"

#include <algorithm>

namespace nps {

namespace {

const char kStrategyPreconditionsEvidenceId[] = "strategy.preconditions";

// Raised here rather than at the fourteen call sites, so the fifteenth cannot forget it.
const char kPreconditionsHoldObligation[] = "obl.plan.preconditions-hold";

struct PlanAssociationCheck {
    bool well_formed = false;
    VerificationOutcome outcome = VerificationOutcome::Failed;
    const char *detail = "strategy precondition associations are malformed";
};

int outcome_priority(VerificationOutcome outcome) {
    switch (outcome) {
        case VerificationOutcome::Passed: return 0;
        case VerificationOutcome::NotAttempted: return 1;
        case VerificationOutcome::Inconclusive: return 2;
        case VerificationOutcome::Failed: return 3;
    }
    return 3;
}

PlanAssociationCheck check_plan_associations(const PlanPayload &payload, const Step &step) {
    size_t summary_count = 0;
    size_t evidence_count = 0;
    for (const VerificationRecord &verification : step.verifications) {
        if (verification.evidence_id == kStrategyPreconditionsEvidenceId)
            ++summary_count;
        else
            ++evidence_count;
    }

    if (summary_count != 1)
        return {false, VerificationOutcome::Failed,
                "the reserved strategy summary evidence id is not unique"};
    if (payload.strategy_id.empty() || step.rule_id != payload.strategy_id)
        return {false, VerificationOutcome::Failed,
                "the plan rule does not match a registered strategy id"};
    if (payload.preconditions.size() != payload.applicability_conditions.size() ||
        payload.preconditions.size() != evidence_count)
        return {false, VerificationOutcome::Failed,
                "strategy condition, precondition, and evidence cardinalities differ"};
    if (payload.preconditions.empty())
        return {true, VerificationOutcome::NotAttempted,
                "the strategy has no registered preconditions"};

    for (size_t index = 0; index < payload.preconditions.size(); ++index) {
        const StrategyPrecondition &precondition = payload.preconditions[index];
        if (precondition.id.empty() ||
            precondition.condition_index >= payload.applicability_conditions.size() ||
            payload.applicability_conditions[precondition.condition_index].empty() ||
            precondition.evidence_id.empty())
            return {false, VerificationOutcome::Failed,
                    "a strategy precondition is missing its stable association"};
        if (precondition.evidence_id == kStrategyPreconditionsEvidenceId)
            return {false, VerificationOutcome::Failed,
                    "a strategy precondition uses the reserved summary evidence id"};

        for (size_t prior = 0; prior < index; ++prior) {
            const StrategyPrecondition &registered = payload.preconditions[prior];
            if (registered.id == precondition.id)
                return {false, VerificationOutcome::Failed,
                        "strategy precondition ids must be unique"};
            if (registered.condition_index == precondition.condition_index)
                return {false, VerificationOutcome::Failed,
                        "strategy applicability conditions must have one precondition"};
            if (registered.evidence_id == precondition.evidence_id)
                return {false, VerificationOutcome::Failed,
                        "strategy evidence ids must be unique"};
        }

        size_t matches = 0;
        for (const VerificationRecord &verification : step.verifications) {
            if (verification.evidence_id == precondition.evidence_id)
                ++matches;
        }
        if (matches != 1)
            return {false, VerificationOutcome::Failed,
                    matches == 0 ? "a registered precondition has no associated evidence"
                                 : "a registered precondition has ambiguous evidence"};
    }

    for (size_t index = 0; index < step.verifications.size(); ++index) {
        const VerificationRecord &evidence = step.verifications[index];
        if (evidence.evidence_id == kStrategyPreconditionsEvidenceId)
            continue;
        if (evidence.evidence_id.empty())
            return {false, VerificationOutcome::Failed,
                    "strategy evidence is missing its stable id"};
        for (size_t prior = 0; prior < index; ++prior) {
            const VerificationRecord &registered = step.verifications[prior];
            if (registered.evidence_id != kStrategyPreconditionsEvidenceId &&
                registered.evidence_id == evidence.evidence_id)
                return {false, VerificationOutcome::Failed,
                        "strategy evidence ids must be unique"};
        }

        size_t matches = 0;
        for (const StrategyPrecondition &precondition : payload.preconditions) {
            if (precondition.evidence_id == evidence.evidence_id)
                ++matches;
        }
        if (matches != 1)
            return {false, VerificationOutcome::Failed,
                    "strategy evidence is not associated with exactly one precondition"};
    }

    return {true, VerificationOutcome::Passed,
            "every registered strategy precondition has passed evidence"};
}

}  // namespace

const char *step_kind_name(StepKind k) {
    switch (k) {
        case StepKind::Plan: return "plan";
        case StepKind::Transformation: return "transformation";
        case StepKind::Branch: return "branch";
        case StepKind::Check: return "check";
    }
    return "unknown";
}

const char *claim_type_name(ClaimType c) {
    switch (c) {
        case ClaimType::EquivalentExpression: return "equivalent expression";
        case ClaimType::SolutionSetPreserved: return "solution set preserved";
        case ClaimType::SolutionSetNarrowed: return "solution set narrowed";
        case ClaimType::Implication: return "implication";
        case ClaimType::Definition: return "definition";
        case ClaimType::NoClaim: return "no claim";
        case ClaimType::RowEquivalent: return "row equivalent";
    }
    return "unknown";
}

const char *branch_resolution_name(BranchResolution r) {
    switch (r) {
        case BranchResolution::Unresolved: return "unresolved";
        case BranchResolution::Solved: return "solved";
        case BranchResolution::Rejected: return "rejected";
    }
    return "unknown";
}

const char *verification_outcome_name(VerificationOutcome v) {
    switch (v) {
        case VerificationOutcome::NotAttempted: return "not attempted";
        case VerificationOutcome::Passed: return "passed";
        case VerificationOutcome::Failed: return "failed";
        case VerificationOutcome::Inconclusive: return "inconclusive";
    }
    return "unknown";
}

const char *evidence_strength_name(EvidenceStrength s) {
    switch (s) {
        case EvidenceStrength::StructurallyValid: return "structurally valid";
        case EvidenceStrength::SymbolicallyEquivalentUnderAssumptions:
            return "symbolically equivalent under assumptions";
        case EvidenceStrength::ImplicationOnly: return "implication only";
        case EvidenceStrength::CandidateChecked: return "candidate checked";
        case EvidenceStrength::DimensionallyValid: return "dimensionally valid";
        case EvidenceStrength::NumericallyCorroborated: return "numerically corroborated";
        case EvidenceStrength::Unsupported: return "unsupported";
        case EvidenceStrength::Failed: return "failed";
    }
    return "unknown";
}

const char *derivation_status_name(DerivationStatus s) {
    switch (s) {
        case DerivationStatus::NotRecorded: return "not recorded";
        case DerivationStatus::SolvedAndVerified: return "solved and verified";
        case DerivationStatus::PartiallySolved: return "partially solved";
        case DerivationStatus::Unsupported: return "unsupported";
        case DerivationStatus::InvalidInput: return "invalid input";
        case DerivationStatus::ClarificationRequired: return "clarification required";
        case DerivationStatus::InterpretationUnsupported: return "interpretation unsupported";
        case DerivationStatus::ModelCommitFailed: return "model commit failed";
        case DerivationStatus::ConditionallySolved: return "conditionally solved";
        case DerivationStatus::NumericallyApproximated: return "numerically approximated";
        case DerivationStatus::VerificationFailed: return "verification failed";
        case DerivationStatus::ResourceLimitReached: return "resource limit reached";
        case DerivationStatus::OpaqueSubproblem: return "opaque subproblem";
        case DerivationStatus::DependencyUnavailable: return "dependency unavailable";
        case DerivationStatus::Cancelled: return "cancelled";
        // Named as the pair of "solved and verified" rather than after the machinery, because a
        // student reads this and has no reason to know what verification is.
        case DerivationStatus::SolvedButUnchecked: return "solved but unchecked";
        case DerivationStatus::SolvedAndCorroborated: return "solved and corroborated";
    }
    return "unknown";
}

bool derivation_status_in_range(uint64_t value) {
    return value <= static_cast<uint64_t>(DerivationStatus::SolvedAndCorroborated);
}

const char *numeric_mode_name(NumericMode m) {
    switch (m) {
        case NumericMode::Exact: return "exact";
        case NumericMode::Decimal: return "decimal";
    }
    return "unknown";
}

bool VerificationRecord::corroborates() const {
    return outcome == VerificationOutcome::Inconclusive && strength != EvidenceStrength::Unsupported;
}

bool Step::verified() const {
    bool any_passed = false;
    for (const VerificationRecord &v : verifications) {
        if (v.outcome != VerificationOutcome::Passed)
            return false;
        any_passed = true;
    }
    return claim == ClaimType::NoClaim || any_passed;
}

bool Step::has_failed_verification() const {
    for (const VerificationRecord &v : verifications) {
        if (v.outcome == VerificationOutcome::Failed)
            return true;
    }
    return false;
}

bool Step::has_corroborating_verification() const {
    for (const VerificationRecord &v : verifications) {
        if (v.corroborates())
            return true;
    }
    return false;
}

EvidenceStrength strength_for(VerificationOutcome outcome, EvidenceStrength passing) {
    if (outcome == VerificationOutcome::Passed)
        return passing;
    return outcome == VerificationOutcome::Failed ? EvidenceStrength::Failed
                                                  : EvidenceStrength::Unsupported;
}

void register_strategy_precondition(PlanPayload &plan, Step &step, std::string id,
                                    std::string condition, std::string method,
                                    EvidenceStrength passing_strength, VerificationOutcome outcome,
                                    std::string detail) {
    StrategyPrecondition precondition;
    precondition.id = id;
    precondition.condition_index = static_cast<uint32_t>(plan.applicability_conditions.size());
    precondition.evidence_id = id;
    precondition.passing_strength = passing_strength;
    plan.preconditions.push_back(std::move(precondition));
    plan.applicability_conditions.push_back(std::move(condition));

    VerificationRecord evidence;
    evidence.method = std::move(method);
    evidence.outcome = outcome;
    evidence.strength = strength_for(outcome, passing_strength);
    evidence.detail = std::move(detail);
    evidence.evidence_id = std::move(id);
    step.verifications.push_back(std::move(evidence));
}

StepId Derivation::add(StepId parent, Step &&envelope, StepKind kind) {
    if (!valid_parent(parent))
        return kNoStep;
    StepId id = static_cast<StepId>(steps_.size());
    envelope.id = id;
    envelope.parent = parent;
    envelope.kind = kind;
    steps_.push_back(std::move(envelope));
    payload_index_.push_back(0);
    if (parent == kNoStep)
        roots_.push_back(id);
    else
        steps_[parent].children.push_back(id);
    return id;
}

StepId Derivation::add_transformation(StepId parent, Step envelope, TransformationPayload payload) {
    StepId id = add(parent, std::move(envelope), StepKind::Transformation);
    if (id == kNoStep)
        return id;
    payload_index_[id] = static_cast<uint32_t>(transformations_.size());
    transformations_.push_back(std::move(payload));
    return id;
}

StepId Derivation::add_plan(StepId parent, Step envelope, PlanPayload payload) {
    envelope.claim = ClaimType::NoClaim;
    envelope.proof_obligations.push_back(
        {kPreconditionsHoldObligation,
         "every registered strategy precondition has passing evidence"});
    VerificationRecord summary;
    summary.method = "registered strategy preconditions";
    summary.evidence_id = kStrategyPreconditionsEvidenceId;
    envelope.verifications.push_back(std::move(summary));
    StepId id = add(parent, std::move(envelope), StepKind::Plan);
    if (id == kNoStep)
        return id;
    payload_index_[id] = static_cast<uint32_t>(plans_.size());
    plans_.push_back(std::move(payload));
    refresh_plan_verification(id);
    return id;
}

StepId Derivation::add_branch(Meter &meter, StepId parent, Step envelope, BranchPayload payload) {
    if (!valid_parent(parent) || !meter.branch())
        return kNoStep;
    StepId id = add(parent, std::move(envelope), StepKind::Branch);
    payload_index_[id] = static_cast<uint32_t>(branches_.size());
    branches_.push_back(std::move(payload));
    return id;
}

StepId Derivation::add_check(StepId parent, Step envelope, CheckPayload payload) {
    StepId id = add(parent, std::move(envelope), StepKind::Check);
    if (id == kNoStep)
        return id;
    payload_index_[id] = static_cast<uint32_t>(checks_.size());
    checks_.push_back(std::move(payload));
    return id;
}

void Derivation::refresh_plan_verification(StepId id) {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Plan)
        return;

    Step &step = steps_[id];
    const PlanPayload &payload = plans_[payload_index_[id]];
    VerificationRecord *summary = nullptr;
    for (VerificationRecord &verification : step.verifications) {
        if (verification.evidence_id == kStrategyPreconditionsEvidenceId)
            summary = &verification;
    }
    if (!summary)
        return;

    const PlanAssociationCheck association = check_plan_associations(payload, step);
    summary->outcome = association.outcome;
    summary->detail = association.detail;
    if (association.well_formed && association.outcome == VerificationOutcome::Passed) {
        for (const StrategyPrecondition &precondition : payload.preconditions) {
            const VerificationRecord *matched = nullptr;
            for (const VerificationRecord &verification : step.verifications) {
                if (verification.evidence_id == precondition.evidence_id)
                    matched = &verification;
            }
            if (outcome_priority(matched->outcome) > outcome_priority(summary->outcome)) {
                summary->outcome = matched->outcome;
                summary->detail = "precondition " + precondition.id + " is " +
                                  verification_outcome_name(matched->outcome);
            }
        }
    }

    // Last, because the loop above can still worsen the outcome and the strength has to answer for
    // the outcome that settled. The summary asks whether every registered precondition has matching
    // passing evidence, which is a question about the record's own shape rather than about the
    // mathematics. It is not the weakest of what it summarizes: the eight strengths are kinds of
    // evidence rather than amounts of it, so there is no weakest to take.
    summary->strength = strength_for(summary->outcome, EvidenceStrength::StructurallyValid);
}

bool Derivation::complete_plan_precondition(StepId id, const std::string &precondition_id,
                                            VerificationOutcome outcome, std::string detail) {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Plan ||
        outcome == VerificationOutcome::NotAttempted)
        return false;

    PlanPayload &payload = plans_[payload_index_[id]];
    if (!check_plan_associations(payload, steps_[id]).well_formed)
        return false;

    const StrategyPrecondition *precondition = nullptr;
    size_t precondition_matches = 0;
    for (const StrategyPrecondition &candidate : payload.preconditions) {
        if (candidate.id == precondition_id) {
            precondition = &candidate;
            ++precondition_matches;
        }
    }
    if (precondition_matches != 1)
        return false;

    VerificationRecord *evidence = nullptr;
    size_t evidence_matches = 0;
    for (VerificationRecord &candidate : steps_[id].verifications) {
        if (candidate.evidence_id == precondition->evidence_id) {
            evidence = &candidate;
            ++evidence_matches;
        }
    }
    if (evidence_matches != 1 || evidence->outcome != VerificationOutcome::NotAttempted)
        return false;

    evidence->outcome = outcome;
    // What the method is worth was declared when the precondition was registered, because it is a
    // property of the method. Only the outcome was open until now, so this is where the two meet.
    evidence->strength = strength_for(outcome, precondition->passing_strength);
    evidence->detail = std::move(detail);
    refresh_plan_verification(id);
    return true;
}

bool Derivation::all_verified_from(size_t checkpoint) const {
    for (size_t i = checkpoint; i < steps_.size(); ++i) {
        if (!steps_[i].verified())
            return false;
    }
    return true;
}

DerivationStatus Derivation::outcome_from(size_t checkpoint) const {
    bool unchecked = false;
    bool corroborated = false;
    for (size_t i = checkpoint; i < steps_.size(); ++i) {
        // A disagreement anywhere in the range outranks an inconclusive one before it, so the whole
        // range is scanned rather than the first unverified record answering.
        if (steps_[i].has_failed_verification())
            return DerivationStatus::VerificationFailed;
        if (steps_[i].has_corroborating_verification())
            corroborated = true;
        else if (!steps_[i].verified())
            unchecked = true;
    }
    // A step nobody checked outranks a step that was corroborated, because the weaker claim is the
    // honest one for the range as a whole.
    if (unchecked)
        return DerivationStatus::SolvedButUnchecked;
    return corroborated ? DerivationStatus::SolvedAndCorroborated : DerivationStatus::SolvedAndVerified;
}

bool keep_verified_prefix(Derivation &derivation, size_t mark, const Arena &arena, bool retain_plans) {
    if (arena.failed()) {
        derivation.rewind_to(mark);
        return false;
    }

    size_t kept = derivation.verified_prefix_end(mark, retain_plans);

    // A plan on its own is a statement of intent rather than a prefix of the work. Keeping one puts
    // preconditions in front of the reader that no move ever tested, which is the vacuous condition
    // this project has already had to take back out once.
    bool worked = false;
    for (size_t i = mark; i < kept && !worked; ++i)
        worked = derivation.at(static_cast<StepId>(i)).kind != StepKind::Plan;
    if (!worked)
        kept = mark;

    derivation.rewind_to(kept);
    return kept > mark;
}

bool Derivation::completed_transformation_after(size_t checkpoint) const {
    for (size_t i = checkpoint + 1; i < steps_.size(); ++i) {
        if (steps_[i].kind == StepKind::Transformation &&
            transformations_[payload_index_[i]].after != kNoNode)
            return true;
    }
    return false;
}

size_t Derivation::verified_prefix_end(size_t checkpoint, bool retain_plans) const {
    size_t end = steps_.size();
    for (size_t i = checkpoint; i < steps_.size(); ++i) {
        if (retain_plans && steps_[i].kind == StepKind::Plan)
            continue;
        if (!steps_[i].verified()) {
            end = i;
            break;
        }
        if (steps_[i].kind == StepKind::Transformation &&
            transformations_[payload_index_[i]].after == kNoNode) {
            end = i;
            break;
        }
    }

    // Branch siblings and the checks attached to them form one publishable unit. Reconsider after
    // each rollback because the boundary can then land inside an enclosing split.
    bool moved = true;
    while (moved) {
        moved = false;
        for (size_t i = checkpoint; i < end; ++i) {
            if (steps_[i].kind != StepKind::Branch)
                continue;
            const StepId parent = steps_[i].parent;
            bool seen = false;
            for (size_t prior = checkpoint; prior < i; ++prior) {
                if (steps_[prior].kind == StepKind::Branch && steps_[prior].parent == parent) {
                    seen = true;
                    break;
                }
            }
            if (seen)
                continue;

            std::vector<StepId> members;
            size_t first = steps_.size();
            for (size_t candidate = 0; candidate < steps_.size(); ++candidate) {
                if (steps_[candidate].kind == StepKind::Branch &&
                    steps_[candidate].parent == parent) {
                    members.push_back(static_cast<StepId>(candidate));
                    first = std::min(first, candidate);
                }
            }

            std::vector<StepId> records = members;
            if (parent != kNoStep) {
                for (StepId child : steps_[parent].children) {
                    if (steps_[child].kind != StepKind::Branch)
                        records.push_back(child);
                }
            }
            for (StepId member : members) {
                for (StepId child : steps_[member].children)
                    records.push_back(child);
            }

            bool complete = true;
            for (StepId record : records)
                complete = complete && record < end;

            const BranchPayload &first_payload = branches_[payload_index_[members.front()]];
            for (StepId member : members) {
                const BranchPayload &payload = branches_[payload_index_[member]];
                complete = complete &&
                           payload.siblings_exhaustive == first_payload.siblings_exhaustive &&
                           payload.siblings_exclusive == first_payload.siblings_exclusive &&
                           payload.siblings_domain_consistent ==
                               first_payload.siblings_domain_consistent &&
                           payload.exhaustive_evidence == first_payload.exhaustive_evidence &&
                           payload.resolution != BranchResolution::Unresolved &&
                           !payload.resolution_evidence.empty();
            }

            // The named passing evidence is the only record-level indication that an exhaustive
            // group reached its closing check rather than stopping after an individually valid case.
            if (first_payload.siblings_exhaustive) {
                bool passing = false;
                for (StepId record : records) {
                    for (const VerificationRecord &verification : steps_[record].verifications) {
                        if (verification.method == first_payload.exhaustive_evidence &&
                            verification.outcome == VerificationOutcome::Passed)
                            passing = true;
                    }
                }
                complete = complete && !first_payload.exhaustive_evidence.empty() && passing;
            }

            if (!complete) {
                const size_t rollback = std::max(checkpoint, first);
                if (rollback < end) {
                    end = rollback;
                    moved = true;
                    break;
                }
            }
        }
    }
    return end;
}

bool Derivation::adopt_roots_since(size_t checkpoint, StepId parent) {
    if (checkpoint > steps_.size() || parent >= checkpoint)
        return false;
    std::vector<StepId> &children = steps_[parent].children;
    size_t kept = 0;
    for (StepId root : roots_) {
        if (root < checkpoint) {
            roots_[kept++] = root;
        } else {
            children.insert(std::lower_bound(children.begin(), children.end(), root), root);
            steps_[root].parent = parent;
        }
    }
    roots_.resize(kept);
    return true;
}

void Derivation::rewind_to(size_t checkpoint) {
    if (checkpoint >= steps_.size())
        return;

    // Payloads are appended in step order, so the dropped ones are the tail of each kind's vector.
    size_t plans = 0;
    size_t transformations = 0;
    size_t branches = 0;
    size_t checks = 0;
    for (size_t i = 0; i < checkpoint; ++i) {
        switch (steps_[i].kind) {
            case StepKind::Plan: ++plans; break;
            case StepKind::Transformation: ++transformations; break;
            case StepKind::Branch: ++branches; break;
            case StepKind::Check: ++checks; break;
        }
    }
    plans_.resize(plans);
    transformations_.resize(transformations);
    branches_.resize(branches);
    checks_.resize(checks);

    // A parent still naming a dropped child is the mislabeled state PERF-009 is about.
    StepId first_dropped = static_cast<StepId>(checkpoint);
    for (size_t i = 0; i < checkpoint; ++i) {
        std::vector<StepId> &children = steps_[i].children;
        size_t kept = 0;
        for (StepId child : children) {
            if (child < first_dropped)
                children[kept++] = child;
        }
        children.resize(kept);
    }

    size_t kept_roots = 0;
    for (StepId root : roots_) {
        if (root < first_dropped)
            roots_[kept_roots++] = root;
    }
    roots_.resize(kept_roots);

    steps_.resize(checkpoint);
    payload_index_.resize(checkpoint);
}

bool Derivation::complete_transformation(StepId id, NodeId after) {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Transformation)
        return false;
    TransformationPayload &p = transformations_[payload_index_[id]];
    if (p.after != kNoNode || after == kNoNode)
        return false;
    p.after = after;
    return true;
}

bool Derivation::publish_verified(const Derivation &source) {
    for (size_t i = 0; i < size() && i < source.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        if (const PlanPayload *plan = source.plan(id)) {
            for (const StrategyPrecondition &precondition : plan->preconditions) {
                for (const VerificationRecord &evidence : source.at(id).verifications) {
                    if (evidence.evidence_id == precondition.evidence_id)
                        complete_plan_precondition(id, precondition.id, evidence.outcome, evidence.detail);
                }
            }
        }
    }
    const size_t end = source.verified_prefix_end(size(), true);
    if (end <= size())
        return true;
    return append(source, size(), nullptr, end);
}

bool Derivation::append(const Derivation &source, size_t from, Meter *meter, size_t end) {
    const StepId base = static_cast<StepId>(steps_.size());
    for (size_t i = from; i < end; ++i) {
        const Step &original = source.steps_[i];
        if (meter && !meter->replay())
            return false;
        if (meter && original.kind == StepKind::Branch && !meter->branch())
            return false;
        Step copy = original;
        copy.children.clear();
        const StepId parent = original.parent == kNoStep
                                  ? kNoStep
                                  : static_cast<StepId>(original.parent - from) + base;
        const StepId id = add(parent, std::move(copy), original.kind);
        const uint32_t at = source.payload_index_[i];
        switch (original.kind) {
            case StepKind::Plan:
                payload_index_[id] = static_cast<uint32_t>(plans_.size());
                plans_.push_back(source.plans_[at]);
                break;
            case StepKind::Transformation:
                payload_index_[id] = static_cast<uint32_t>(transformations_.size());
                transformations_.push_back(source.transformations_[at]);
                break;
            case StepKind::Branch:
                payload_index_[id] = static_cast<uint32_t>(branches_.size());
                branches_.push_back(source.branches_[at]);
                break;
            case StepKind::Check:
                payload_index_[id] = static_cast<uint32_t>(checks_.size());
                checks_.push_back(source.checks_[at]);
                break;
        }
    }
    return true;
}

bool Derivation::remember(const char *engine, const Arena &arena, NodeId subject, NodeId unknown,
                          size_t checkpoint, NodeId solution) {
    if (arena.failed() || solution == kNoNode || checkpoint >= steps_.size() ||
        verified_prefix_end(checkpoint) != steps_.size())
        return false;
    // A record hanging off something before the checkpoint is not part of this run.
    for (size_t i = checkpoint; i < steps_.size(); ++i) {
        const StepId parent = steps_[i].parent;
        if (parent != kNoStep && parent < checkpoint)
            return false;
    }
    std::vector<RememberedRun> &remembered = runs();
    for (const RememberedRun &r : remembered) {
        if (r.arena == &arena && r.subject == subject && r.unknown == unknown && r.engine == engine)
            return false;
    }
    RememberedRun entry;
    entry.engine = engine;
    entry.arena = &arena;
    entry.subject = subject;
    entry.unknown = unknown;
    entry.solution = solution;
    entry.run.append(*this, checkpoint, nullptr, size());
    remembered.push_back(std::move(entry));
    return true;
}

bool Derivation::recall(const char *engine, const Arena &arena, NodeId subject, NodeId unknown,
                        Meter &meter, NodeId *solution) {
    if (arena.failed())
        return false;
    for (const RememberedRun &r : runs()) {
        if (r.arena != &arena || r.subject != subject || r.unknown != unknown || r.engine != engine)
            continue;
        if (!append(r.run, 0, &meter, r.run.size()))
            return false;
        *solution = r.solution;
        return true;
    }
    return false;
}

Coroutine<bool> Derivation::recall_steps(TaskContext &task, Derivation &derivation,
                                         const char *engine, const Arena &arena,
                                         NodeId subject, NodeId unknown, Meter &meter, NodeId *solution) {
    if (arena.failed())
        co_return false;
    for (const RememberedRun &run : derivation.runs()) {
        co_await task.checkpoint();
        if (run.arena != &arena || run.subject != subject || run.unknown != unknown || run.engine != engine)
            continue;
        for (size_t i = 0; i < run.run.size(); ++i) {
            co_await task.checkpoint();
            if (!derivation.append(run.run, i, &meter, i + 1))
                co_return false;
        }
        *solution = run.solution;
        co_return true;
    }
    co_return false;
}

void Derivation::share_runs(Derivation &owner) {
    shared_runs_ = &owner.runs();
}

const TransformationPayload *Derivation::transformation(StepId id) const {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Transformation)
        return nullptr;
    return &transformations_[payload_index_[id]];
}

const PlanPayload *Derivation::plan(StepId id) const {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Plan)
        return nullptr;
    return &plans_[payload_index_[id]];
}

const BranchPayload *Derivation::branch(StepId id) const {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Branch)
        return nullptr;
    return &branches_[payload_index_[id]];
}

const CheckPayload *Derivation::check(StepId id) const {
    if (id >= steps_.size() || steps_[id].kind != StepKind::Check)
        return nullptr;
    return &checks_[payload_index_[id]];
}

void RestrictionSet::add(const Restriction &r, StepId step) {
    for (size_t i = 0; i < held_.size(); ++i) {
        if (subsumes(held_[i].restriction, r))
            return;
    }
    // A stronger condition on the same subject replaces the weaker one and takes its step, since the
    // rule that asked for the stronger one is the rule whose explanation accounts for it.
    for (size_t i = held_.size(); i > 0; --i) {
        if (subsumes(r, held_[i - 1].restriction))
            held_.erase(held_.begin() + static_cast<long>(i - 1));
    }
    held_.push_back(Held{r, step});
}

void RestrictionSet::settle(const Arena &arena, Derivation &derivation,
                            std::vector<std::string> *texts) const {
    for (size_t i = 0; i < held_.size(); ++i) {
        if (held_[i].step == kNoStep)
            continue;
        std::string text = restriction_text(arena, held_[i].restriction);
        derivation.restrictions_at(held_[i].step).push_back(text);
        if (texts != nullptr)
            texts->push_back(std::move(text));
    }
}

}  // namespace nps
