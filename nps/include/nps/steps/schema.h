#ifndef NPS_STEPS_SCHEMA_H
#define NPS_STEPS_SCHEMA_H

#include <cstddef>
#include <string>

#include "nps/steps/derivation.h"

namespace nps {

// VER-016 asks every rule and strategy to declare four things: the exact claim it makes, the
// evidence that claim requires, which combinations of that evidence are allowed, and what happens
// when the evidence does not arrive. All four are already true of every site in the engine, but as
// convention: linear.cc's substitution check makes a SolutionSetPreserved claim, raises
// obl.linear.candidate-satisfies, discharges it with a CandidateChecked substitution, and refuses
// the answer when that fails, and nothing anywhere says it must. This is where it is said, so a
// step can be read against it rather than against the reviewer's memory.
//
// The table is const char data rather than the engine's usual std::string, because it is read-only
// for the life of the process and belongs in the image rather than on a handheld's heap.

// One way of discharging an obligation: a verification method, and what its evidence is worth when
// it passes. The strength is the same declaration strength_for takes, so an entry and the call site
// that produces the record can be compared directly.
struct EvidenceAlternative {
    const char *method;
    EvidenceStrength strength;
    // Whether this method can come back agreeing without being independent of what it checked. Such
    // a record is Inconclusive at the strength above, which is byte for byte what a check that could
    // not evaluate would write if it overstated itself, so the schema is the only thing that can
    // tell the two apart.
    bool may_corroborate = false;
};

// Any one of the alternatives discharges the obligation, which is the only combination the engine
// uses: a rule with two ways to check itself accepts either, and no rule requires both. All-of is
// left unexpressible rather than defined and unused, so the first rule that needs it has to say so.
struct ObligationSchema {
    const char *id;
    // The exact claim, in the words the obligation is about. The step's ProofObligation carries the
    // same sentence for the reader, and nothing checks that the two agree: two texts name the
    // problem's own variable and would differ between correct runs, so conformance compares ids
    // only. Change one of the pair and change the other by hand.
    const char *text;
    const EvidenceAlternative *evidence;
    size_t evidence_count;
};

// What the engine does when a rule's obligation is not discharged. Two behaviours rather than a
// status, because several statuses express withholding and the requirement asks what the rule does
// rather than which enumerator it reaches. Keeping the checked prefix is not among them, and the
// distinction is worth stating: a halt is a budget running out rather than an obligation coming
// back false, so no rule answers for it. The value existed here until it turned out to have no
// writer, which is the same reason all-of is left unexpressible above.
enum class FailureBehavior : uint8_t {
    // Nothing is offered. The derivation reaches a status that claims no solution, and the answer
    // the rule was working towards is withheld rather than reported with a caveat.
    WithholdResult,
    // The rule cannot fail its own obligation, because the obligation is discharged by construction
    // rather than by a check that could disagree. A rule declaring this and then recording a failed
    // verification is a fault, which is the whole reason the value exists rather than being absent.
    CannotFail,
};

const char *failure_behavior_name(FailureBehavior b);

struct RuleSchema {
    const char *rule_id;
    ClaimType claim;
    const ObligationSchema *obligations;
    size_t obligation_count;
    FailureBehavior on_failure;
};

// Null when the rule has not been declared yet, which the invariant pass counts rather than treats
// as permission.
const RuleSchema *rule_schema(const std::string &rule_id);

size_t declared_rule_count();
const RuleSchema &declared_rule(size_t index);

}  // namespace nps

#endif
