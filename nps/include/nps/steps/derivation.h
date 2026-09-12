#ifndef NPS_DERIVATION_H
#define NPS_DERIVATION_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/core/canonical.h"
#include "nps/core/task.h"

namespace nps {

// PRD section 10. Every record uses a common envelope and then a payload appropriate to its kind,
// and the section is explicit that fields which do not apply to a kind are not filled with dummy
// expressions or generic "verified" values. That is enforced here rather than left to discipline:
// the payload is a tagged union and the accessors return null for the wrong kind, so a caller that
// reaches for a TransformationPayload on a Plan record gets nothing instead of something plausible.

enum class StepKind : uint8_t {
    Plan,
    Transformation,
    Branch,
    Check,
};

const char *step_kind_name(StepKind k);

enum class ClaimType : uint8_t {
    // What the step asserts, which is not the same as whether it was checked. A step can be an
    // EquivalentExpression and still be unverified, and the two must not be conflated.
    EquivalentExpression,
    SolutionSetPreserved,
    SolutionSetNarrowed,
    Implication,
    Definition,
    NoClaim,
    RowEquivalent,
};

const char *claim_type_name(ClaimType c);

enum class VerificationOutcome : uint8_t {
    NotAttempted,
    Passed,
    Failed,
    Inconclusive,
};

const char *verification_outcome_name(VerificationOutcome v);

// VER-011's eight named strengths. A separate axis from the outcome above, which says whether the
// check ran and what it came back with: this says what the answer is worth. The pair that motivates
// keeping them apart is a sampled check that could not evaluate. Its outcome is Inconclusive and its
// strength is Unsupported, and neither of those is Failed, which is the distinction #24 needs before
// PartiallySolved can mean one thing.
enum class EvidenceStrength : uint8_t {
    StructurallyValid,
    SymbolicallyEquivalentUnderAssumptions,
    ImplicationOnly,
    CandidateChecked,
    DimensionallyValid,
    NumericallyCorroborated,
    Unsupported,
    Failed,
};

const char *evidence_strength_name(EvidenceStrength s);

// The one free choice is what a method's evidence is worth when it passes, which is a property of
// the method. The rest follows from the outcome, so it is derived rather than asked for: a
// disagreement is worth nothing, and a check that could not run is the understatement the record
// defaults to.
EvidenceStrength strength_for(VerificationOutcome outcome, EvidenceStrength passing);

struct VerificationRecord {
    std::string method;
    VerificationOutcome outcome = VerificationOutcome::NotAttempted;
    // Unsupported by default because a record that does not say what its evidence is worth is worth
    // nothing. The default understates rather than overstates, which is the safe direction, and the
    // invariant pass counts what is still sitting on it rather than letting it read as classified.
    EvidenceStrength strength = EvidenceStrength::Unsupported;
    std::string detail;
    std::string evidence_id;

    // A check that ran, agreed, and was not independent of what it checked. The pair is the whole
    // answer: Inconclusive alone is also what a check that could not evaluate leaves behind, and
    // that one keeps the Unsupported default.
    bool corroborates() const;
};

// PRD section 19.9 wants proof-obligation coverage reported on its own, and section 27's catalog
// holds proof_obligation_ids. Both need an obligation to have a name a report can count, which prose
// alone cannot give it. The text stays because it is what a reader of the step sees.
struct ProofObligation {
    std::string id;
    std::string text;
};

struct TransformationPayload {
    NodeId before = kNoNode;
    NodeId after = kNoNode;
    // Which subexpression was rewritten, as the argument indices to walk from the root. Empty means
    // the whole expression.
    std::vector<uint32_t> path;
    std::string concrete_action;
    bool reversible = false;
};

struct StrategyPrecondition {
    std::string id;
    uint32_t condition_index = 0xFFFFFFFFu;
    std::string evidence_id;
    // Kept here rather than only on the record, because a precondition is registered before its
    // method runs and completed afterwards. Completion needs to know what the evidence is worth.
    EvidenceStrength passing_strength = EvidenceStrength::Unsupported;
};

struct PlanPayload {
    std::string strategy_id;
    std::string selected_strategy;
    std::vector<StrategyPrecondition> preconditions;
    std::vector<std::string> applicability_conditions;
    std::vector<std::string> matched_problem_facts;
    std::vector<std::string> alternatives_considered;
    std::string selection_rationale;
};

// STEP-024's second half. A case is not finished by being recorded: it is solved, or it is rejected
// and says what rejected it. Unresolved leads so a case nobody settled cannot read as settled, which
// is the same reason NotRecorded leads the status list below.
enum class BranchResolution : uint8_t {
    Unresolved,
    Solved,
    Rejected,
};

const char *branch_resolution_name(BranchResolution r);

// One record per case, with the split identified by the parent the cases share. STEP-024 asks for
// three properties of the split rather than of a case, and they are held here on each member because
// that is what the field already named siblings_exhaustive says: a case knows what its sibling set
// claims. The invariant pass reads them back across a parent's children and breaks when two siblings
// disagree, so a claim stated in several places cannot be stated inconsistently.
// Held here rather than in a split record on Derivation because rewind_to counts payloads by
// StepKind and PERF-008's evidence is written against one meter spend per add_branch call, so a
// parallel structure would be invisible to both.
struct BranchPayload {
    NodeId condition = kNoNode;
    bool siblings_exhaustive = false;
    bool siblings_exclusive = false;
    bool siblings_domain_consistent = false;
    std::string feasibility_status;
    // The verification method that argues the split covers everything. The invariant pass looks for
    // a passing verification by this method among the split's own records, so exhaustiveness cannot
    // be claimed by setting the bool above: a rule that sets one without the other is broken rather
    // than trusted. Named as a method rather than as an obligation id so a second branching rule
    // with a different argument is checked by the same gate instead of slipping past it.
    std::string exhaustive_evidence;
    BranchResolution resolution = BranchResolution::Unresolved;
    // What settled this case: the method that solved it, or the reason it was rejected. Empty is a
    // case claiming an outcome it will not account for, which the pass breaks on.
    std::string resolution_evidence;
};

struct CheckPayload {
    std::string target_claim;
    std::string check_method;
    std::string expected_relation;
    std::string observed_result;
};

using StepId = uint32_t;
const StepId kNoStep = 0xFFFFFFFFu;

struct Step {
    StepId id = kNoStep;
    StepId parent = kNoStep;
    StepKind kind = StepKind::Transformation;
    std::string phase;
    std::string goal;
    std::string rule_id;
    std::string rule_name;
    std::string explanation_short;
    std::string explanation_detailed;
    // PHYS-027's two kinds, kept apart by which field a producer can reach rather than by a tag on
    // one list. A physical modelling assumption is written here by the rule that rests on it. A
    // mathematical domain condition is a property of the expression as written, so canonical.h
    // derives it and RestrictionSet::settle is the only writer of the field below. Disjoint writers
    // mean no rule can file one kind as the other, which a kind enum set at the call site would
    // allow and nothing would catch.
    std::vector<std::string> assumptions_before;
    std::vector<std::string> assumptions_after;
    std::vector<std::string> domain_restrictions;
    ClaimType claim = ClaimType::NoClaim;
    std::vector<ProofObligation> proof_obligations;
    std::vector<VerificationRecord> verifications;
    uint32_t backend_requests = 0;
    std::vector<StepId> children;

    // Section 17 wants an unverified result labelled prominently, so this is a question the record
    // can answer rather than something the UI has to work out from the verification list.
    bool verified() const;
    bool has_failed_verification() const;
    bool has_corroborating_verification() const;
};

void register_strategy_precondition(PlanPayload &plan, Step &step, std::string id,
                                    std::string condition, std::string method,
                                    EvidenceStrength passing_strength, VerificationOutcome outcome,
                                    std::string detail);

enum class DerivationStatus : uint8_t {
    // Section 15's outcome list. NotRecorded leads so a context nobody finished does not claim one.
    NotRecorded,
    SolvedAndVerified,
    // PRD:808's meaning, a verified derivation up to a clearly identified unsupported subproblem.
    // Nothing writes it. The two states that used to are Cancelled and SolvedButUnchecked below,
    // and neither was ever this one. Left in place because the state is real and unimplemented
    // rather than absent: an engine that gives up mid-derivation on a form it has no rule for
    // currently reports Unsupported and throws away the prefix it had already checked.
    PartiallySolved,
    Unsupported,
    InvalidInput,
    ClarificationRequired,
    InterpretationUnsupported,
    ModelCommitFailed,
    ConditionallySolved,
    NumericallyApproximated,
    VerificationFailed,
    ResourceLimitReached,
    OpaqueSubproblem,
    DependencyUnavailable,
    // The two section 15 does not name. It asks for "at least" its list, and until these had names
    // both wore PartiallySolved, which is false about a cancellation and false about an answer whose
    // check merely could not run. Appended rather than placed in reading order: a context serializes
    // the number, so inserting one would reread every stored status as its neighbour.
    Cancelled,
    SolvedButUnchecked,
    // A check ran, it agreed, and it was not independent of the answer it was checking. Distinct
    // from SolvedButUnchecked, which is nothing having run. differentiate.cc:651 reached the same
    // reading first and had to spend Inconclusive on it for want of a status.
    SolvedAndCorroborated,
};

const char *derivation_status_name(DerivationStatus s);

// The serialized form carries the number, so a reader needs the bound. It lives beside the enum so a
// status added above cannot be forgotten in a file that never mentions DerivationStatus.
bool derivation_status_in_range(uint64_t value);

enum class NumericMode : uint8_t {
    Exact,
    Decimal,
};

const char *numeric_mode_name(NumericMode m);

struct SolutionContext {
    std::string application_version;
    std::string capability_manifest_id;
    std::string problem_family_id;
    std::string problem_family_envelope_version;
    NodeId normalized_problem_model = kNoNode;
    std::string original_expression;
    std::string normalized_expression;
    std::string requested_method;
    std::vector<std::string> active_assumptions;
    std::string angle_convention;
    std::string branch_convention;
    std::string unit_policy;
    std::string detail_projection;
    std::string resource_policy;
    std::vector<std::string> content_pack_versions;
    NumericMode numeric_mode = NumericMode::Exact;
    DerivationStatus derivation_status = DerivationStatus::NotRecorded;
};

// What the caller knows and no engine can derive. Given to the solve rather than written onto the
// context, which is an output make_context assembles and would overwrite.
struct SolveRequest {
    std::string original_expression;
    NumericMode numeric_mode = NumericMode::Exact;
};

class Derivation {
  public:
    // Every add takes the payload for its kind, so there is no way to create a record without one
    // and no way to attach the wrong one.
    StepId add_transformation(StepId parent, Step envelope, TransformationPayload payload);
    StepId add_plan(StepId parent, Step envelope, PlanPayload payload);
    StepId add_check(StepId parent, Step envelope, CheckPayload payload);

    // PERF-008's branch count, taken here rather than at the call site so no engine can split
    // without spending one. Returns kNoStep when the budget is gone, and records nothing: a branch
    // whose siblings were never allowed to exist would read as an exhaustive split that is not one.
    StepId add_branch(Meter &meter, StepId parent, Step envelope, BranchPayload payload);

    bool complete_plan_precondition(StepId id, const std::string &precondition_id,
                                    VerificationOutcome outcome, std::string detail);

    // kNoStep is the same 0xFFFFFFFF sentinel kNoNode is, and add_branch above returns it when the
    // branch budget is gone, so the id a caller holds can be one no step ever had. Refused here for
    // the reason Arena::at refuses its own, rather than trusting five callers to stay bounded. The
    // inert Step answers with its own id still kNoStep, so a caller that ignores this reads a step
    // that says it is not one.
    const Step &at(StepId id) const {
        if (id >= steps_.size())
            return absent_step();
        return steps_[id];
    }

    // A domain restriction is only known to be the strongest one on its subject once the whole
    // derivation has been walked, so RestrictionSet settles that set at the end and writes it back
    // here. Root adoption also updates parent links.
    // Refused for at()'s reason, and because rewind_to can drop a step whose id a holder still has.
    std::vector<std::string> &restrictions_at(StepId id) {
        if (id >= steps_.size())
            return discarded_restrictions();
        return steps_[id].domain_restrictions;
    }

    // Pending plan evidence may resolve while checked mathematical records only append.
    bool publish_verified(const Derivation &source);

    size_t size() const { return steps_.size(); }
    const std::vector<StepId> &roots() const { return roots_; }

    // Taken before a solve appends, so PERF-009 can drop what a cancelled one left behind.
    size_t mark() const { return steps_.size(); }

    // Keep nested solves in display order without changing their records.
    bool adopt_roots_since(size_t checkpoint, StepId parent);

    // Drops every record at or after the checkpoint, with its payload and every reference to it.
    void rewind_to(size_t checkpoint);

    // Whether every record from the checkpoint on carries a verification that passed. A solve with
    // an unchecked step is not solved and verified, and section 17 wants that visible.
    bool all_verified_from(size_t checkpoint) const;

    // Section 15's outcome for a solve that reached the end, which is three-valued where the
    // predicate above is two: every check passed, one disagreed, or one could not run. Engines ask
    // here rather than branching on all_verified_from, which collapsed the last two and so reported
    // a check that disagreed as the engine having done less. acceptance_corpus.cc:617 already refuses
    // that pairing, so this is the shape the corpus was written against.
    DerivationStatus outcome_from(size_t checkpoint) const;

    // Where the run of checked records starting at the checkpoint ends, which is what STEP-025 asks
    // a halted solve to keep. An unfilled composite is a hole rather than a step, so it ends the run
    // even when its own verification passed: nothing below it ever returned.
    size_t verified_prefix_end(size_t checkpoint, bool retain_plans = false) const;

    // Whether any real work landed below a step, which is how a composite rule that failed partway
    // decides between completing itself with what it holds and leaving a hole. Asked of the record
    // rather than of the rule's own children, because the work may be a level further down: the
    // first factor of a product can be a sum that itself got partway.
    bool completed_transformation_after(size_t checkpoint) const;

    // Fills in the result of a transformation whose rule could not know it when the record was
    // made, which is every composite rule: the product rule states its decomposition before its
    // children have returned. It fills a hole once and refuses to change an answer already there,
    // so a record is still written once and never revised. False when there was nothing to fill.
    bool complete_transformation(StepId id, NodeId after);

    // Null unless the record is of that kind. Asking for the wrong one is a caller error that used
    // to be invisible, so it returns nothing rather than a default constructed payload.
    const TransformationPayload *transformation(StepId id) const;
    const PlanPayload *plan(StepId id) const;
    const BranchPayload *branch(StepId id) const;
    const CheckPayload *check(StepId id) const;

    bool remember(const char *engine, const Arena &arena, NodeId subject, NodeId unknown,
                  size_t checkpoint, NodeId solution);
    static Coroutine<bool> recall_steps(TaskContext &task, Derivation &derivation,
                                        const char *engine, const Arena &arena,
                                 NodeId subject, NodeId unknown, Meter &meter, NodeId *solution);
    bool recall(const char *engine, const Arena &arena, NodeId subject, NodeId unknown, Meter &meter,
                NodeId *solution);
    // The owner must outlive borrowers and remain at the same address.
    void share_runs(Derivation &owner);

    SolveRequest request;
    SolutionContext context;

  private:
    struct RememberedRun;

    // Default constructed, so its id and parent are already kNoStep and its payload indices are
    // empty. Shared, so the refused read hands back something with a lifetime.
    static const Step &absent_step() {
        static const Step step;
        return step;
    }

    // Cleared on the way out, so a refused write is discarded rather than read back by the next one.
    static std::vector<std::string> &discarded_restrictions() {
        static std::vector<std::string> discarded;
        discarded.clear();
        return discarded;
    }

    StepId add(StepId parent, Step &&envelope, StepKind kind);
    bool valid_parent(StepId parent) const {
        return parent == kNoStep || parent < steps_.size();
    }
    void refresh_plan_verification(StepId id);
    bool append(const Derivation &source, size_t from, Meter *meter, size_t end);
    std::vector<RememberedRun> &runs() { return shared_runs_ ? *shared_runs_ : runs_; }

    std::vector<Step> steps_;
    std::vector<StepId> roots_;
    std::vector<TransformationPayload> transformations_;
    std::vector<PlanPayload> plans_;
    std::vector<BranchPayload> branches_;
    std::vector<CheckPayload> checks_;
    std::vector<uint32_t> payload_index_;
    std::vector<RememberedRun> runs_;
    std::vector<RememberedRun> *shared_runs_ = nullptr;
};

struct Derivation::RememberedRun {
    std::string engine;
    const Arena *arena = nullptr;
    NodeId subject = kNoNode;
    NodeId unknown = kNoNode;
    NodeId solution = kNoNode;
    Derivation run;
};

// Trims a solve that did not reach its goal back to the records that were checked, which is what
// STEP-025 asks for and what section 15 asks of every non-success outcome, not of halts alone. True
// when anything survived, because a status of "not recorded" is a lie the moment something is.
//
// The arena is asked rather than assumed. A record built while the arena was failing may name nodes
// that were never made, and that is not a prefix at any length, so it keeps nothing. Taking it here
// rather than at each call site is the point: an engine cannot forget to ask.
bool keep_verified_prefix(Derivation &derivation, size_t mark, const Arena &arena,
                          bool retain_plans = false);

// What an engine gathers as it walks, so every rule states its conditions the same way. Held rather
// than written straight onto the step, because which condition on a subject is the strongest is not
// known until the walk ends: an integrand's reciprocal asks for a non-zero denominator one step
// before the logarithm rule asks for a positive one, and only the second should reach the reader.
// The surviving condition is attached to the step that asked for it, which is the one whose
// explanation makes sense of it.
class RestrictionSet {
public:
    void add(const Restriction &r, StepId step);

    // Writes each survivor onto its step and appends its text, in the order the conditions were met.
    void settle(const Arena &arena, Derivation &derivation, std::vector<std::string> *texts) const;

private:
    struct Held {
        Restriction restriction;
        StepId step = kNoStep;
    };
    std::vector<Held> held_;
};

}  // namespace nps

#endif
