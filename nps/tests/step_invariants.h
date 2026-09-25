#ifndef NPS_TEST_STEP_INVARIANTS_H
#define NPS_TEST_STEP_INVARIANTS_H

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/evaluate.h"
#include "nps/steps/derivation.h"
#include "nps/steps/schema.h"

namespace nps {
namespace invariants {

// Five criteria of PRD section 20.2, and the PRD requirements of the same shape, are each claims
// about every displayed step. A claim like that needs a population to quantify over, so the check
// is a pass rather than a test: it walks one derivation and the caller runs it over as many as it
// has. The acceptance corpus is one population and the golden fixtures are another, and the point
// of the pass living here rather than inside either of them is that both get the same reading.
//
// What this pass cannot do, written here because every claim below inherits the limit. It reads
// records. It asks whether a claim a step makes is backed by a check the step recorded, and it
// cannot recompute the mathematics that check was about. So a rule recording a passing verification
// it did not earn is indistinguishable here from one that did, and every gate below will pass it.
// Shown rather than assumed: dropping a root from a quadratic split fires VER-017, but dropping the
// root and forcing the completeness verdict to true fires nothing at all, and the derivation returns
// x = 2 for x^2 = 4 with both rows green.
//
// The answer is not another arm. An arm reading records could not tell those two apart either, and
// it would read as closing the gap while leaving it open. What closes it is a second kind of
// evidence: the predicate unit tested against known answers, the engine mutated at its source, and a
// golden fixture a human reads. Claims here are worded to promise the record-reading layer only, and
// one that needs another layer should name which.
struct Claim {
    const char *id;
    // A PRD requirement rather than a section 20.2 criterion. Reported apart, so the section 20.2
    // count stays the count of section 20.2 criteria.
    bool requirement;
    const char *claim;
    size_t broken;
    size_t checked;
};

inline bool major(const Derivation &derivation, StepId id) {
    const Step &step = derivation.at(id);
    if (step.parent == kNoStep)
        return true;
    return derivation.at(step.parent).parent == kNoStep;
}

inline void collect_decimals(const Arena &arena, NodeId id, std::set<std::string> *out) {
    if (id == kNoNode)
        return;
    if (arena.at(id).kind == Kind::Decimal)
        out->insert(arena.text(id));
    const ChildView kids = arena.children(id);
    for (size_t i = 0; i < kids.size(); ++i)
        collect_decimals(arena, kids[i], out);
}

// Criterion 6's sanctioned exception is the step whose job is to round, and there is no single way
// to recognise it, because the five families that round announce it differently. Kinematics carries
// both a significant-figures rule id and a rounding proof obligation; work, density and catch-up
// carry the rule id alone; unit conversion carries the obligation alone, under the rule id
// unit.convert.report-precision. Either signal is accepted here because either one is the step
// saying rounding is what it is for. That the families disagree is a defect in them rather than in
// this predicate, and matching on only one of the two silently exempts some and reports the rest.
// Decimal mode's closing report is the sixth family, and it carries a decimal-report obligation.
inline bool declares_the_approximation(const Step &step) {
    if (step.rule_id.find("significant-figures") != std::string::npos)
        return true;
    if (step.rule_id == "num.rational-to-decimal")
        return true;
    for (size_t i = 0; i < step.proof_obligations.size(); ++i) {
        const std::string &id = step.proof_obligations[i].id;
        if (id.find("rounding") != std::string::npos ||
            id.find("decimal-report") != std::string::npos)
            return true;
    }
    return false;
}

// The four labels STEP-022 names, compared against the whole label rather than searched for inside
// it: "Simplify the left side" says which expression and what happens to it, and is not what the
// requirement forbids. A label that is only the generic word is.
inline bool generic_label(const std::string &text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && (text[first] == ' ' || text[first] == '\t'))
        ++first;
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' ||
                            text[last - 1] == '.' || text[last - 1] == ':'))
        --last;
    std::string t = text.substr(first, last - first);
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] >= 'A' && t[i] <= 'Z')
            t[i] = static_cast<char>(t[i] - 'A' + 'a');
    }
    return t == "simplify" || t == "solve" || t == "after algebra" || t == "by cas";
}

// The one reading in this pass that recomputes instead of reading a record, written because every
// other claim here inherits the limit named at the top of the file: a rule that records a passing
// verification it did not earn is invisible to a gate that only reads records. A step claiming
// EquivalentExpression asserts something about two nodes in the arena, so that assertion can be put
// to the arena rather than to the step's own paperwork.
//
// What it is worth is not the same on the two arms, and the split is the point rather than an
// implementation detail. With no symbol left free on either side both expressions have one exact
// rational value, so equality settles the claim and inequality refutes it. With symbols free the
// most this can do is try a handful of assignments, which PRD section 17 and VER-009 both forbid
// presenting as proof of a symbolic identity: a disagreement at one assignment is proof the two
// differ, and agreement everywhere tried is evidence that they do not. The counts are reported
// apart for that reason, so nobody reads the sampled population as settled.
//
// One shape is neither, and the exclusion is one-directional rather than symmetric. A step whose
// after state leaves free a symbol its before state never had is not asserting that two expressions
// are equal: it is naming a family, which is what an antiderivative's constant of integration is.
// That shape is a fault in the claim rather than a limit of the sampler, so it is reported here and
// the step that means it says FamilyUpToConstant instead, which read_family below judges on its own
// terms. The other direction is judged as an equivalence, because an after state with fewer free
// symbols is one definite expression and comparing it against the before state at several
// assignments is exactly the claim the step made.
enum class EquivalenceReading {
    // Nothing evaluated on either arm, so the claim was not reached.
    NotComparable,
    // The after state leaves free a symbol the before state never had, so no assignment makes the
    // two one comparison. Reported rather than counted, because an equivalence is not what the step
    // means.
    SymbolsChanged,
    // Closed on both sides and equal, which settles the claim at the strength of arithmetic.
    Exact,
    // The same symbols free on both sides, agreeing at every assignment that evaluated.
    Sampled,
    Disagreed,
};

struct EquivalenceReport {
    EquivalenceReading reading = EquivalenceReading::NotComparable;
    size_t evaluated = 0;
    std::string disagreement;
    // The symbol the after state introduced, when that is what the reading found.
    std::string introduced;
};

// Six assignments, the count agrees_on_samples was written around, where every third is fractional
// so a rewrite that only holds on integers cannot pass by choosing its own points.
const size_t kEquivalenceSamples = 6;

// The symbols a comparison is about. A unit's name is the first argument of the unit() call that
// carries it, and that argument is a label rather than a free variable: cm^3 becoming m^3 is the
// conversion working rather than a symbol appearing from nowhere. Everything else is collected the
// way collect_symbols collects it.
inline void collect_value_symbols(const Arena &arena, NodeId id, std::vector<std::string> *out) {
    if (id == kNoNode || id >= arena.node_count())
        return;
    const Node &n = arena.at(id);
    if (n.kind == Kind::Symbol) {
        if (std::find(out->begin(), out->end(), arena.text(id)) == out->end())
            out->push_back(arena.text(id));
        return;
    }
    const bool unit_call = n.kind == Kind::Call && arena.text(id) == "unit";
    const ChildView kids = arena.children(id);
    for (size_t i = 0; i < kids.size(); ++i) {
        if (unit_call && i == 0)
            continue;
        collect_value_symbols(arena, kids[i], out);
    }
}

inline EquivalenceReport read_equivalence(const Arena &arena, NodeId before, NodeId after) {
    EquivalenceReport out;
    if (before == kNoNode || after == kNoNode || before >= arena.node_count() ||
        after >= arena.node_count())
        return out;

    std::vector<std::string> left, right;
    collect_value_symbols(arena, before, &left);
    collect_value_symbols(arena, after, &right);
    for (size_t i = 0; i < right.size(); ++i) {
        if (std::find(left.begin(), left.end(), right[i]) == left.end()) {
            out.reading = EquivalenceReading::SymbolsChanged;
            out.introduced = right[i];
            return out;
        }
    }

    const bool closed = left.empty();
    const SampleAgreement agreement =
        agrees_on_samples(arena, before, after, closed ? 1 : kEquivalenceSamples);
    out.evaluated = agreement.evaluated;
    out.disagreement = agreement.disagreement;
    if (!agreement.disagreement.empty())
        out.reading = EquivalenceReading::Disagreed;
    else if (agreement.evaluated == 0)
        out.reading = EquivalenceReading::NotComparable;
    else
        out.reading = closed ? EquivalenceReading::Exact : EquivalenceReading::Sampled;
    return out;
}

// What a FamilyUpToConstant step owes, put to the arena the same way the equivalence reading above
// is. The claim is that the after state names every expression differing from the before state by a
// constant, so the reading finds the symbol the after state introduced, takes it back off, and asks
// the equivalence question of what is left. That is judgeable: a rule adding its constant to the
// wrong expression, adding more than one free symbol, or claiming a family while naming a single
// expression all come back faulted.
enum class FamilyReading {
    Faulted,
    // The constant came off and the remainder agrees with the before state.
    Judged,
    // The remainder had no rational value at any assignment, so the comparison was never reached.
    NotComparable,
};

struct FamilyReport {
    FamilyReading reading = FamilyReading::Faulted;
    std::string fault;
};

inline FamilyReport read_family(const Arena &arena, NodeId before, NodeId after) {
    FamilyReport out;
    if (before == kNoNode || after == kNoNode || before >= arena.node_count() ||
        after >= arena.node_count()) {
        out.fault = "one of its two states is missing";
        return out;
    }

    std::vector<std::string> left, right, introduced;
    collect_value_symbols(arena, before, &left);
    collect_value_symbols(arena, after, &right);
    for (size_t i = 0; i < right.size(); ++i) {
        if (std::find(left.begin(), left.end(), right[i]) == left.end() &&
            std::find(introduced.begin(), introduced.end(), right[i]) == introduced.end())
            introduced.push_back(right[i]);
    }
    if (introduced.empty()) {
        out.fault = "its after state leaves no new symbol free, so it names one expression rather "
                    "than a family";
        return out;
    }
    if (introduced.size() != 1) {
        out.fault = "its after state introduces " + std::to_string(introduced.size()) +
                    " free symbols where a family up to a constant introduces one";
        return out;
    }

    const ChildView kids = arena.children(after);
    if (arena.at(after).kind != Kind::Add || kids.size() != 2) {
        out.fault = "its after state is not the before state with a constant added to it";
        return out;
    }
    NodeId constant = kNoNode;
    NodeId body = kNoNode;
    for (size_t i = 0; i < kids.size(); ++i) {
        const bool is_the_constant = arena.at(kids[i]).kind == Kind::Symbol &&
                                     arena.text(kids[i]) == introduced[0];
        if (is_the_constant && constant == kNoNode)
            constant = kids[i];
        else
            body = kids[i];
    }
    if (constant == kNoNode || body == kNoNode) {
        out.fault = "the symbol " + introduced[0] +
                    " its after state introduced is not a constant added to the before state";
        return out;
    }

    const EquivalenceReport rest = read_equivalence(arena, before, body);
    switch (rest.reading) {
        case EquivalenceReading::Exact:
        case EquivalenceReading::Sampled:
            out.reading = FamilyReading::Judged;
            break;
        case EquivalenceReading::NotComparable:
            out.reading = FamilyReading::NotComparable;
            break;
        case EquivalenceReading::SymbolsChanged:
            out.fault = "what is left under its constant still leaves free a symbol the before "
                        "state never had";
            break;
        case EquivalenceReading::Disagreed:
            out.fault = "what is left under its constant disagrees with the state it was given, " +
                        rest.disagreement;
            break;
    }
    return out;
}

class Pass {
  public:
    Pass() {
        static const Claim kClaims[] = {
            {"3", false,
             "every transformation names a registered rule, and plan and check steps name a "
             "registered strategy or validation method",
             0, 0},
            {"4", false, "every transformation carries a passing verification whose method is readable",
             0, 0},
            {"5", false, "every major step names its rule and says how to carry it out", 0, 0},
            {"6", false, "no step introduces an inexact value, except the significant-figures step",
             0, 0},
            {"8", false,
             "a refusal claims no solution and keeps only a checked prefix permitted by its "
             "outcome or the failing step when a check failed",
             0, 0},
            {"STEP-002", true,
             "every transformation carries a before state, an after state, a rule id, a readable "
             "rule name and a short explanation",
             0, 0},
            {"STEP-007", true,
             "every step claiming an implication raises an obligation for the candidate it "
             "produced and records an attempt at checking it",
             0, 0},
            {"STEP-019", true,
             "every transformation that consulted the backend still names a registered rule and "
             "carries its own recorded result, so no step's provenance is the backend alone",
             0, 0},
            {"STEP-021", true,
             "every transformation says how to recognise its rule again, not only what it did",
             0, 0},
            {"STEP-022", true,
             "no step's rule name or action is one of the four labels the requirement names: "
             "simplify, solve, after algebra, by CAS",
             0, 0},
            {"MATH-014", true,
             "no transformation rests on numeric sampling: sampling corroborates an answer on a "
             "check and never obtains one on the step that produced it. Parameter survival and "
             "branch completeness are counted elsewhere, over the populations that have them",
             0, 0},
            {"STEP-024", true,
             "every split records whether its cases are exhaustive, mutually exclusive and "
             "domain-consistent, its members agree on all three, no two of them state the same "
             "condition, and no case is left unresolved: each is solved or rejected and says what "
             "settled it. Exclusivity is read as far as duplicate conditions and no further, and "
             "domain consistency is discharged by construction rather than checked",
             0, 0},
            {"VER-017", true,
             "a split claiming to cover everything carries a passing verification by the method it "
             "names for that claim, every solved case carries a passing check of the candidate "
             "against the original, and no derivation reports a solution over an unresolved case",
             0, 0},
            {"PHYS-026", true,
             "every plan registers at least one applicability condition, and none rests on "
             "dimensional agreement alone: at least one registered condition is backed by evidence "
             "of some other kind, because matching units are a necessary condition for a model and "
             "never a sufficient one",
             0, 0},
            {"PHYS-027", true,
             "every step-level assumption is recorded as one kind or the other by which field "
             "holds it, physical modelling assumptions on the step whose rule rests on them and "
             "mathematical domain conditions where canonical.h settled them, and no physics rule "
             "holds a condition of the mathematical kind. Which kind a string is cannot be read "
             "back out of it, so the split is fixed by disjoint writers and checked against named "
             "rules in the unit tests: this counts the two populations and catches one crossing",
             0, 0},
            {"VER-016", true,
             "every step whose rule declares a proof-obligation schema matches it: the claim it "
             "makes, the obligations it raises, and a verification of a declared method for each "
             "obligation, passing at the strength the schema says that method is worth",
             0, 0},
            {"VER-002", true,
             "every transformation claiming an equivalent expression agrees with itself wherever "
             "both sides can be evaluated: exactly, when neither side leaves a symbol free, and at "
             "a series of rational assignments otherwise. Agreement at samples is evidence and not "
             "proof, a disagreement at one assignment is proof the two differ, and a step whose "
             "after state leaves free a symbol its before state never had is naming a family "
             "rather than an expression, so it is counted rather than sampled",
             0, 0},
        };
        for (size_t i = 0; i < sizeof(kClaims) / sizeof(kClaims[0]); ++i)
            claims_.push_back(kClaims[i]);
    }

    // A caller that cannot tell whether the engine refused passes knows_refusal false, and
    // criterion 8 is left uncounted rather than counted as held. Nothing examined must never read
    // as nothing broken.
    void walk(const Arena &arena, const Derivation &derivation, bool refusal, bool knows_refusal,
              std::vector<std::string> *broken, const bool *has_answer = nullptr) {
        for (size_t i = 0; i < derivation.size(); ++i) {
            const StepId id = static_cast<StepId>(i);
            const Step &step = derivation.at(id);

            looked_at("STEP-022");
            if (generic_label(step.rule_name))
                broke(broken, "STEP-022",
                      "a step whose rule name is the generic label '" + step.rule_name + "'");
            const TransformationPayload *labelled = derivation.transformation(id);
            if (labelled != nullptr && generic_label(labelled->concrete_action))
                broke(broken, "STEP-022",
                      "a step whose action is the generic label '" + labelled->concrete_action +
                          "'");

            // STEP-007. An implication is this engine's name for a step whose solution set is not
            // preserved both ways, and both sides of that pair claim it: squaring, clearing a
            // denominator and multiplying by a possibly-zero expression run forwards only, and so
            // does the check that discharges one, because "the candidate satisfies the original" is
            // an entailment rather than an equivalence. Either side owes a candidate check, so the
            // gate asks for the obligation and an attempt at it. A passing outcome is deliberately
            // not required: the shared-domain check records Failed when the root is extraneous,
            // which is the check doing its job rather than the step misbehaving.
            if (step.claim == ClaimType::Implication) {
                looked_at("STEP-007");
                ++implications_;
                if (step.proof_obligations.empty())
                    broke(broken, "STEP-007",
                          "the step " + step.rule_id +
                              " claims an implication and raises no obligation for the candidate "
                              "check it owes");
                bool attempted = false;
                for (size_t v = 0; v < step.verifications.size(); ++v) {
                    if (step.verifications[v].outcome != VerificationOutcome::NotAttempted)
                        attempted = true;
                }
                if (!attempted)
                    broke(broken, "STEP-007",
                          "the step " + step.rule_id +
                              " claims an implication and attempted no verification of the "
                              "candidate it produced");
            }

            // STEP-019 and STEP-023. A backend may propose and it may check, and either way the
            // step it touched has to stand on its own: a registered rule that produced the after
            // state, not a number the backend handed back. The record cannot show which code
            // computed the value, so what is asked is the thing that would be missing if the
            // backend had produced it, a named rule and a filled-in result.
            if (step.kind == StepKind::Transformation && step.backend_requests > 0) {
                looked_at("STEP-019");
                ++backend_steps_;
                const TransformationPayload *p = derivation.transformation(id);
                if (step.rule_id.empty())
                    broke(broken, "STEP-019",
                          "a transformation consulted the backend and names no rule of its own");
                if (p == nullptr || p->after == kNoNode)
                    broke(broken, "STEP-019",
                          "the transformation " + step.rule_id +
                              " consulted the backend and recorded no result of its own");
                if (p != nullptr && p->concrete_action.empty())
                    broke(broken, "STEP-019",
                          "the transformation " + step.rule_id +
                              " consulted the backend and does not say what it did itself");
            }

            looked_at("3");
            if (step.kind == StepKind::Transformation && step.rule_id.empty())
                broke(broken, "3", "a transformation with no rule id");
            if (step.kind == StepKind::Plan) {
                const PlanPayload *p = derivation.plan(id);
                if (p == nullptr || p->strategy_id.empty())
                    broke(broken, "3", "a plan step naming no registered strategy");

                // PHYS-026, both sentences. check_plan_associations makes the conditions, the
                // preconditions and their evidence correspond one to one, but it calls an empty
                // registration well formed, so the requirement's first sentence has to be asked
                // here: a strategy that registers nothing has no applicability record to map.
                // The second is asked of the plan rather than of each precondition, because the
                // requirement is about what the whole applicability rests on: one dimensional
                // condition among several is ordinary, and nothing else behind it is the fault.
                if (p != nullptr) {
                    looked_at("PHYS-026");
                    ++planned_strategies_;
                    if (p->preconditions.empty()) {
                        // Kept apart from the sentence below, because selecting a model on no
                        // record and selecting it on matching units are different faults and the
                        // second would be a false reading of the first.
                        broke(broken, "PHYS-026",
                              "the strategy " + p->strategy_id +
                                  " registers no applicability condition at all");
                    } else {
                        bool beyond_dimensions = false;
                        for (size_t v = 0; v < step.verifications.size(); ++v) {
                            const VerificationRecord &record = step.verifications[v];
                            if (record.evidence_id == "strategy.preconditions")
                                continue;
                            if (record.strength != EvidenceStrength::DimensionallyValid)
                                beyond_dimensions = true;
                        }
                        if (!beyond_dimensions)
                            broke(broken, "PHYS-026",
                                  "the strategy " + p->strategy_id +
                                      " rests its applicability on dimensional agreement alone");
                    }
                }
            }
            if (step.kind == StepKind::Check) {
                const CheckPayload *p = derivation.check(id);
                if (p == nullptr || p->check_method.empty())
                    broke(broken, "3", "a check step naming no validation method");
            }

            if (step.kind == StepKind::Transformation) {
                looked_at("STEP-002");
                const TransformationPayload *p = derivation.transformation(id);
                if (p == nullptr || p->before == kNoNode || p->after == kNoNode)
                    broke(broken, "STEP-002", "a transformation with no before or after state");
                if (step.rule_id.empty() || step.rule_name.empty())
                    broke(broken, "STEP-002",
                          "a transformation with no rule id or no readable rule name");
                if (step.explanation_short.empty())
                    broke(broken, "STEP-002", "a transformation with no short explanation");
                // STEP-021 asks for the recognition cue rather than a second description, and the
                // shortest honest test of that is that it is not simply the short one again.
                looked_at("STEP-021");
                if (step.explanation_detailed.empty())
                    broke(broken, "STEP-021",
                          "the transformation " + step.rule_id + " does not say when to reach for "
                          "its rule");
                else if (step.explanation_detailed == step.explanation_short)
                    broke(broken, "STEP-021",
                          "the transformation " + step.rule_id +
                              " repeats its short explanation instead of saying when to use it");

                // MATH-014's third clause. Sampling at a handful of assignments is corroboration and
                // never a reason a step is right, so it may sit on a check and never on the step that
                // produced the answer. Keyed on the strength rather than on the method text, so a
                // second sampling method is read by this arm instead of slipping past it.
                looked_at("MATH-014");
                for (size_t v = 0; v < step.verifications.size(); ++v) {
                    if (step.verifications[v].strength ==
                        EvidenceStrength::NumericallyCorroborated)
                        broke(broken, "MATH-014",
                              "the transformation " + step.rule_id +
                                  " rests on numeric sampling by " + step.verifications[v].method +
                                  ", which corroborates an answer and cannot obtain one");
                }

                // VER-002 over the two nodes the step is about. Steps that declare they round are
                // out of it for the same reason criterion 6 exempts them: their whole job is to
                // report a value the exact one is near, so a disagreement there is the rule
                // working. Everything else claiming an equivalent expression is asked.
                if (step.claim == ClaimType::EquivalentExpression && p != nullptr &&
                    !declares_the_approximation(step)) {
                    looked_at("VER-002");
                    ++equivalence_steps_;
                    const EquivalenceReport reading = read_equivalence(arena, p->before, p->after);
                    switch (reading.reading) {
                        case EquivalenceReading::Disagreed:
                            broke(broken, "VER-002",
                                  "the transformation " + step.rule_id +
                                      " claims an equivalent expression and its two sides "
                                      "disagree, " + reading.disagreement);
                            break;
                        case EquivalenceReading::Exact:
                            ++equivalence_exact_;
                            break;
                        case EquivalenceReading::Sampled:
                            ++equivalence_sampled_;
                            break;
                        case EquivalenceReading::SymbolsChanged:
                            broke(broken, "VER-002",
                                  "the transformation " + step.rule_id +
                                      " claims an equivalent expression while its after state "
                                      "leaves free the symbol " + reading.introduced +
                                      ", which its before state never had, so the two are a family "
                                      "rather than one expression");
                            break;
                        case EquivalenceReading::NotComparable:
                            ++equivalence_unevaluated_;
                            break;
                    }
                }

                // The other half of the same criterion. A step naming a family is judged on what a
                // family claims rather than declined, which is what #244 asked of the arm above.
                if (step.claim == ClaimType::FamilyUpToConstant && p != nullptr) {
                    looked_at("VER-002");
                    ++family_steps_;
                    const FamilyReport reading = read_family(arena, p->before, p->after);
                    switch (reading.reading) {
                        case FamilyReading::Faulted:
                            broke(broken, "VER-002",
                                  "the transformation " + step.rule_id +
                                      " claims a family up to a constant and " + reading.fault);
                            break;
                        case FamilyReading::Judged:
                            ++family_judged_;
                            break;
                        case FamilyReading::NotComparable:
                            ++family_unevaluated_;
                            break;
                    }
                }
            }

            // STEP-004 wants the strength recorded beside the method, and VER-011 wants it of every
            // verification result rather than of a transformation's. Counted rather than broken
            // while the rules are still being classified, because a claim reporting a number nobody
            // has driven to zero is a red gate that teaches people to read red as normal. It becomes
            // a claim when this count reaches zero.
            for (size_t v = 0; v < step.verifications.size(); ++v) {
                if (step.verifications[v].outcome != VerificationOutcome::Passed)
                    continue;
                ++passing_;
                if (step.verifications[v].strength == EvidenceStrength::Unsupported)
                    ++unclassified_;
                // STEP-004 asks for the strength beside the method, and the strength half above is
                // VER-011's whole claim. Counted separately so the two rows stop sharing one
                // condition: a record naming no method was passing both of them on the strength of
                // the other one's evidence.
                if (step.verifications[v].method.empty())
                    ++methodless_;
            }

            // VER-016 asks every rule and strategy to declare its proof-obligation schema. Counted
            // rather than broken while the table is being filled, the same staging the strength
            // count above went through, and the undeclared rules are collected by name because the
            // list of them is the work: a rule id nothing has declared is one entry still to write.
            // A step with no rule id has no identity to declare a schema against, so VER-016
            // cannot be answered for it at all. Counted separately from the undeclared ones,
            // because the two need different fixes: an undeclared rule needs a table entry, and a
            // nameless one needs a name first.
            if (step.rule_id.empty() && step.kind != StepKind::Branch)
                ++nameless_;

            // PHYS-027's two kinds, counted where they are attached. Which kind a condition is
            // cannot be read back out of a string, so the pass counts the populations and leaves
            // the classifying to the writer and to the unit tests, which check named rules against
            // known answers. The one crossing it can see is a physics rule holding a mathematical
            // condition: those are derived from expression form by canonical.h and settled onto the
            // rule that introduced them, so a physics rule carrying one means a modelling
            // assumption was filed as a domain restriction, which is what catch_up did.
            looked_at("PHYS-027");
            physical_assumptions_ += step.assumptions_before.size();
            mathematical_restrictions_ += step.domain_restrictions.size();
            if (!step.domain_restrictions.empty() && step.rule_id.compare(0, 8, "physics.") == 0)
                broke(broken, "PHYS-027",
                      "the physics rule " + step.rule_id + " carries " +
                          std::to_string(step.domain_restrictions.size()) +
                          " mathematical domain restriction(s), which are derived from the "
                          "expression rather than assumed about the world, so a modelling "
                          "assumption has been filed as the wrong kind");
            if (!step.rule_id.empty()) {
                ++rules_seen_;
                const RuleSchema *schema = rule_schema(step.rule_id);
                if (schema == nullptr) {
                    ++undeclared_;
                    observe(step, derivation.context.derivation_status);
                } else {
                    conforms(step, *schema, derivation.plan(id), derivation.context.derivation_status,
                             broken);
                }
            }

            if (step.kind == StepKind::Transformation) {
                looked_at("4");
                // A check that ran, agreed and was not independent is a third answer, not the
                // absence of one. It is weaker than a pass and the derivation status says so, but
                // demanding a pass here would ask an engine to overstate what it did.
                bool checked_and_readable = false;
                for (size_t v = 0; v < step.verifications.size(); ++v) {
                    const VerificationRecord &record = step.verifications[v];
                    if ((record.outcome == VerificationOutcome::Passed || record.corroborates()) &&
                        !record.method.empty())
                        checked_and_readable = true;
                }
                if (!checked_and_readable)
                    broke(broken, "4",
                          "a transformation with no verification naming its method");
            }

            if (major(derivation, id) && step.kind != StepKind::Branch) {
                looked_at("5");
                const TransformationPayload *t = derivation.transformation(id);
                const CheckPayload *c = derivation.check(id);
                const PlanPayload *p = derivation.plan(id);
                // What rule was used, and how to apply it. For a transformation the how is its
                // concrete action, for a check it is the method it used, and for a plan it is the
                // strategy it selected. A step that cannot answer both halves is one a user cannot
                // learn from. A check step's registered identity is its validation method rather
                // than a rule, which is what criterion 3 says of it and what the payload encodes.
                const bool names_a_rule = !step.rule_name.empty() ||
                                          (p && !p->selected_strategy.empty()) ||
                                          (c && !c->check_method.empty());
                const bool says_how = (t && !t->concrete_action.empty()) ||
                                      (c && !c->check_method.empty()) ||
                                      (p && !p->selection_rationale.empty());
                if (!names_a_rule || !says_how)
                    broke(broken, "5",
                          "a major step that does not both name its rule and say how to apply it");
                if (step.explanation_short.empty())
                    broke(broken, "5", "a major step with no explanation");
            }

            const TransformationPayload *t = derivation.transformation(id);
            if (t != nullptr && !declares_the_approximation(step)) {
                looked_at("6");
                std::set<std::string> before, after;
                collect_decimals(arena, t->before, &before);
                collect_decimals(arena, t->after, &after);
                for (std::set<std::string>::const_iterator it = after.begin(); it != after.end();
                     ++it) {
                    if (before.count(*it) == 0)
                        broke(broken, "6", "a step introduced the inexact value " + *it);
                }
            }
        }

        splits(derivation, broken);

        if (knows_refusal && refusal) {
            const DerivationStatus status = derivation.context.derivation_status;
            looked_at("8");
            // Both halves of "solved" are lies here, not just the checked one. The unchecked half
            // says the checks could not run, which is a claim about the verifier, while the engine
            // refusing is a claim about the answer, so a refusal wearing it still claims solved.
            if (status == DerivationStatus::SolvedAndVerified ||
                status == DerivationStatus::ConditionallySolved ||
                status == DerivationStatus::SolvedButUnchecked)
                broke(broken, "8",
                      "a refusal claims a complete solution under the status " +
                          std::string(derivation_status_name(status)));

            // Unsupported prefixes require the caller to attest that no answer escaped.
            if (has_answer && *has_answer)
                broke(broken, "8", "a refusal returned a final answer");
            const bool refused_prefix = has_answer && !*has_answer &&
                                        (status == DerivationStatus::Unsupported ||
                                         status == DerivationStatus::InvalidInput);
            const bool keeps_prefix = status == DerivationStatus::ResourceLimitReached ||
                                      status == DerivationStatus::Cancelled ||
                                      status == DerivationStatus::PartiallySolved;
            size_t survivors = 0;
            for (size_t i = 0; i < derivation.size(); ++i) {
                const Step &step = derivation.at(static_cast<StepId>(i));
                if (step.kind != StepKind::Transformation)
                    continue;
                // VER-008 wants the step that failed its check named rather than hidden, so that
                // one refusal keeps an unverified record on purpose.
                if (status == DerivationStatus::VerificationFailed && step.has_failed_verification())
                    continue;
                // A failed whole-path plan can still contain independently checked outer moves.
                const TransformationPayload *move = derivation.transformation(step.id);
                const bool stands_alone = step.claim == ClaimType::EquivalentExpression ||
                                          step.claim == ClaimType::SolutionSetPreserved ||
                                          step.claim == ClaimType::Definition;
                const bool complete = move && move->before != kNoNode && move->after != kNoNode &&
                                      move->before < arena.node_count() && move->after < arena.node_count();
                if ((keeps_prefix || (refused_prefix && stands_alone && complete)) && step.verified()) {
                    ++survivors;
                    continue;
                }
                broke(broken, "8",
                      "a refusal kept the transformation " + step.rule_id + " under the status " +
                          derivation_status_name(status));
            }
            // The other direction, which the count above cannot see. "Partially solved" is the
            // status for a prefix, so claiming it with no prefix to show is the same overstatement
            // as claiming solved: the engine got nowhere and Unsupported is what that is called.
            if (status == DerivationStatus::PartiallySolved && survivors == 0)
                broke(broken, "8", "a refusal claims a partial solution but kept no transformation");
        }
    }

    const std::vector<Claim> &claims() const { return claims_; }

    size_t passing_verifications() const { return passing_; }
    size_t unclassified_verifications() const { return unclassified_; }

    // How many passing verifications name no method, which is STEP-004's own half of the pair.
    size_t methodless_verifications() const { return methodless_; }

    // How many plans the claim was asked of, reported beside PHYS-026 because a claim about every
    // strategy's applicability reads as held when no plan was examined.
    size_t planned_strategies() const { return planned_strategies_; }

    size_t rules_seen() const { return rules_seen_; }
    size_t undeclared_rules() const { return undeclared_; }
    size_t nameless_steps() const { return nameless_; }

    // How many steps the STEP-007 gate had to judge. Reported beside it because a claim quantified
    // over an empty population reads as held when it was never asked.
    size_t implication_steps() const { return implications_; }

    // How many steps consulted the backend, reported beside STEP-019 for the same reason the
    // implication count is reported beside STEP-007: a population of none reads as held.
    size_t backend_steps() const { return backend_steps_; }

    // The two populations STEP-024 and VER-017 are quantified over. A pass that met no split judged
    // nothing, and both claims would print the same sentence, so the counts go beside them.
    size_t branch_steps() const { return branch_steps_; }
    size_t split_count() const { return split_count_; }

    // Splits reached with a domain restriction in force, which is the arm of STEP-024 that asks a
    // domain-consistency claim to be argued rather than assumed. Zero today, and reported as zero
    // rather than folded into the count above, because the vacuous arm holding says nothing at all
    // about the arm that has never run.
    size_t restricted_splits() const { return restricted_splits_; }
    // The four readings VER-002 separates. Reported apart rather than summed, because the exact arm
    // settles a claim and the sampled arm corroborates one, and a single number would let the
    // weaker evidence speak for the stronger. The last two are populations the check declined to
    // judge, printed so an exclusion nobody can see cannot grow quietly.
    size_t equivalence_steps() const { return equivalence_steps_; }
    size_t equivalence_exact() const { return equivalence_exact_; }
    size_t equivalence_sampled() const { return equivalence_sampled_; }
    size_t family_steps() const { return family_steps_; }
    size_t family_judged() const { return family_judged_; }
    size_t family_unevaluated() const { return family_unevaluated_; }
    size_t equivalence_unevaluated() const { return equivalence_unevaluated_; }

    size_t physical_assumptions() const { return physical_assumptions_; }
    size_t mathematical_restrictions() const { return mathematical_restrictions_; }

    // What the undeclared rules were seen doing, which is the material an entry is written from:
    // the claim they made, the obligations they raised, and the methods that discharged them with
    // what each was worth. Transcribed rather than summarised, because an entry that paraphrases
    // the engine is an entry that can be wrong about it.
    struct Observed {
        std::set<std::string> claims;
        std::set<std::string> obligations;
        std::set<std::string> evidence;
        std::set<std::string> statuses_on_failure;
        size_t steps = 0;
    };

    const std::map<std::string, Observed> &observations() const { return observed_; }

    const Claim *find(const std::string &id) const {
        for (size_t i = 0; i < claims_.size(); ++i) {
            if (id == claims_[i].id)
                return &claims_[i];
        }
        return nullptr;
    }

    size_t count(bool requirement) const {
        size_t n = 0;
        for (size_t i = 0; i < claims_.size(); ++i) {
            if (claims_[i].requirement == requirement)
                ++n;
        }
        return n;
    }

    // The ids that broke, joined, which is what a selftest compares against the id it broke.
    std::string broken_ids() const {
        std::string out;
        for (size_t i = 0; i < claims_.size(); ++i) {
            if (!claims_[i].broken)
                continue;
            if (!out.empty())
                out += " and ";
            out += claims_[i].id;
        }
        return out;
    }

    void reset() {
        for (size_t i = 0; i < claims_.size(); ++i) {
            claims_[i].broken = 0;
            claims_[i].checked = 0;
        }
        passing_ = 0;
        unclassified_ = 0;
        methodless_ = 0;
        planned_strategies_ = 0;
        rules_seen_ = 0;
        undeclared_ = 0;
        nameless_ = 0;
        implications_ = 0;
        backend_steps_ = 0;
        branch_steps_ = 0;
        split_count_ = 0;
        restricted_splits_ = 0;
        physical_assumptions_ = 0;
        mathematical_restrictions_ = 0;
        equivalence_steps_ = 0;
        equivalence_exact_ = 0;
        equivalence_sampled_ = 0;
        family_steps_ = 0;
        family_judged_ = 0;
        family_unevaluated_ = 0;
        equivalence_unevaluated_ = 0;
        observed_.clear();
    }

  private:
    // STEP-024 and VER-017 are claims about a split rather than about a step, and a split is the set
    // of Branch records sharing a parent. Read in a pass of its own for that reason: no single case
    // carries a property of the set, so answering from one member would answer for the set from a
    // part of it, which is the reading that lets a dropped case look like a complete split.
    void splits(const Derivation &derivation, std::vector<std::string> *broken) {
        std::vector<StepId> parents;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &step = derivation.at(static_cast<StepId>(i));
            if (step.kind != StepKind::Branch)
                continue;
            ++branch_steps_;
            bool seen = false;
            for (size_t p = 0; p < parents.size(); ++p) {
                if (parents[p] == step.parent)
                    seen = true;
            }
            if (!seen)
                parents.push_back(step.parent);
        }

        for (size_t p = 0; p < parents.size(); ++p) {
            std::vector<StepId> members;
            for (size_t i = 0; i < derivation.size(); ++i) {
                const StepId id = static_cast<StepId>(i);
                const Step &step = derivation.at(id);
                if (step.kind == StepKind::Branch && step.parent == parents[p])
                    members.push_back(id);
            }
            ++split_count_;
            judge_split(derivation, parents[p], members, broken);
        }
    }

    // Every record the split is made of: the cases, whatever hangs under each case, and the parent's
    // other children, which is where a check on the whole split sits. Collected rather than assumed
    // so a rule may put its completeness argument on a closing check or on a case, and the gate
    // finds it either way.
    static std::vector<StepId> split_records(const Derivation &derivation, StepId parent,
                                             const std::vector<StepId> &members) {
        std::vector<StepId> out = members;
        if (parent != kNoStep) {
            const std::vector<StepId> &siblings = derivation.at(parent).children;
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (derivation.at(siblings[i]).kind != StepKind::Branch)
                    out.push_back(siblings[i]);
            }
        }
        for (size_t i = 0; i < members.size(); ++i) {
            const std::vector<StepId> &kids = derivation.at(members[i]).children;
            for (size_t k = 0; k < kids.size(); ++k)
                out.push_back(kids[k]);
        }
        return out;
    }

    static bool passing_by(const Derivation &derivation, const std::vector<StepId> &records,
                           const std::string &method) {
        for (size_t i = 0; i < records.size(); ++i) {
            const Step &step = derivation.at(records[i]);
            for (size_t v = 0; v < step.verifications.size(); ++v) {
                if (step.verifications[v].outcome == VerificationOutcome::Passed &&
                    step.verifications[v].method == method)
                    return true;
            }
        }
        return false;
    }

    // Whether a case was checked against the problem rather than against the step that produced it.
    // Recognised by the strength rather than by a method name, because VER-011 defines
    // CandidateChecked as exactly that evidence, and a gate keyed to one rule's spelling would pass
    // the next branching rule without reading it.
    static bool candidate_checked(const Derivation &derivation, StepId member) {
        std::vector<StepId> records;
        records.push_back(member);
        const std::vector<StepId> &kids = derivation.at(member).children;
        for (size_t k = 0; k < kids.size(); ++k)
            records.push_back(kids[k]);
        for (size_t i = 0; i < records.size(); ++i) {
            const Step &step = derivation.at(records[i]);
            for (size_t v = 0; v < step.verifications.size(); ++v) {
                if (step.verifications[v].outcome == VerificationOutcome::Passed &&
                    step.verifications[v].strength == EvidenceStrength::CandidateChecked)
                    return true;
            }
        }
        return false;
    }

    void judge_split(const Derivation &derivation, StepId parent,
                     const std::vector<StepId> &members, std::vector<std::string> *broken) {
        looked_at("STEP-024");
        looked_at("VER-017");
        const BranchPayload *first = derivation.branch(members[0]);
        if (first == nullptr) {
            broke(broken, "STEP-024", "a branch record carrying no branch payload");
            return;
        }
        const std::string &rule = derivation.at(members[0]).rule_id;
        const std::string where = rule.empty() ? std::string("an unnamed split")
                                               : "the split " + rule;

        for (size_t i = 1; i < members.size(); ++i) {
            const BranchPayload *m = derivation.branch(members[i]);
            if (m == nullptr) {
                broke(broken, "STEP-024", "a branch record carrying no branch payload");
                continue;
            }
            if (m->siblings_exhaustive != first->siblings_exhaustive ||
                m->siblings_exclusive != first->siblings_exclusive ||
                m->siblings_domain_consistent != first->siblings_domain_consistent)
                broke(broken, "STEP-024",
                      where + " has cases that disagree about whether the set is exhaustive, "
                              "mutually exclusive or domain-consistent");
        }

        for (size_t i = 0; i < members.size(); ++i) {
            const BranchPayload *m = derivation.branch(members[i]);
            if (m == nullptr)
                continue;
            if (m->resolution == BranchResolution::Unresolved) {
                broke(broken, "STEP-024",
                      where + " left a case unresolved, so it is neither solved nor rejected");
                continue;
            }
            if (m->resolution_evidence.empty())
                broke(broken, "STEP-024",
                      where + " records a case as " + branch_resolution_name(m->resolution) +
                          " and does not say what settled it");
        }

        // Duplicate conditions, which is less than exclusivity and is all this can compare. The arena
        // interns, so two cases stating the same condition are the same node. Two cases that overlap
        // without printing alike, x = 2 against x^2 = 4, pass here, so a future rule must not read
        // this arm as having checked exclusivity in general. For a split into distinct rational roots
        // the two coincide, which is why the arm is worth having now and worth naming narrowly.
        if (first->siblings_exclusive) {
            for (size_t i = 0; i < members.size(); ++i) {
                const BranchPayload *a = derivation.branch(members[i]);
                for (size_t j = i + 1; a != nullptr && j < members.size(); ++j) {
                    const BranchPayload *b = derivation.branch(members[j]);
                    if (b != nullptr && a->condition != kNoNode && a->condition == b->condition)
                        broke(broken, "STEP-024",
                              where + " claims its cases are mutually exclusive and states one of "
                                      "them twice");
                }
            }
        }

        const std::vector<StepId> records = split_records(derivation, parent, members);

        // VER-017's completeness half. The bool is not the claim: the method named beside it is, and
        // it has to have passed somewhere in the split. Setting siblings_exhaustive without the
        // check that argues it is exactly the silent omission the requirement is about.
        if (first->siblings_exhaustive) {
            if (first->exhaustive_evidence.empty())
                broke(broken, "VER-017",
                      where + " claims to cover every case and names no method that argues it");
            else if (!passing_by(derivation, records, first->exhaustive_evidence))
                broke(broken, "VER-017",
                      where + " claims to cover every case by " + first->exhaustive_evidence +
                          ", and no record in the split carries a passing verification by that "
                          "method");
        }

        // VER-017's soundness half, one case at a time.
        for (size_t i = 0; i < members.size(); ++i) {
            const BranchPayload *m = derivation.branch(members[i]);
            if (m == nullptr || m->resolution != BranchResolution::Solved)
                continue;
            if (!candidate_checked(derivation, members[i]))
                broke(broken, "VER-017",
                      where + " records a case as solved with no passing candidate check of it "
                              "against the problem");
        }

        // A status claiming an answer over a case nobody settled. Both halves of solved are wrong
        // here for the reason criterion 8 gives: the unchecked one is a claim about the verifier and
        // an unresolved case is a claim about the answer, so a solution reported over one overstates
        // whichever way it is worn.
        const DerivationStatus status = derivation.context.derivation_status;
        if (status == DerivationStatus::SolvedAndVerified ||
            status == DerivationStatus::SolvedButUnchecked) {
            for (size_t i = 0; i < members.size(); ++i) {
                const BranchPayload *m = derivation.branch(members[i]);
                if (m != nullptr && m->resolution == BranchResolution::Unresolved)
                    broke(broken, "VER-017",
                          "a derivation reports " + std::string(derivation_status_name(status)) +
                              " over a case " + where + " never settled");
            }
        }

        // Splits with a domain restriction in force, counted and not judged. Judging one would need
        // an evidence field naming the method that argues the restriction, the way exhaustiveness
        // has one, and there is no such field yet. Backing it with the exhaustiveness method instead
        // would be a guard that only fires where VER-017 has already fired, which reads as a check
        // and is none. The count is reported beside the claim so an empty population cannot pass for
        // a satisfied one.
        bool restricted = false;
        for (size_t i = 0; i < records.size(); ++i)
            restricted = restricted || !derivation.at(records[i]).domain_restrictions.empty();
        if (parent != kNoStep)
            restricted = restricted || !derivation.at(parent).domain_restrictions.empty();
        if (restricted)
            ++restricted_splits_;
    }

    // VER-016's five parts, read off one step. The obligation texts are not compared, because two
    // of them name the problem's own variable and would differ between two correct runs of the same
    // rule: the id is the stable half and the text is the reader's.
    void conforms(const Step &step, const RuleSchema &schema, const PlanPayload *plan,
                  DerivationStatus status, std::vector<std::string> *broken) {
        looked_at("VER-016");
        if (step.claim != schema.claim)
            broke(broken, "VER-016",
                  std::string("the rule ") + schema.rule_id + " claims " +
                      claim_type_name(step.claim) + " where its schema declares " +
                      claim_type_name(schema.claim));

        for (size_t i = 0; i < step.proof_obligations.size(); ++i) {
            bool declared = false;
            for (size_t d = 0; d < schema.obligation_count; ++d) {
                if (step.proof_obligations[i].id == schema.obligations[d].id)
                    declared = true;
            }
            if (!declared)
                broke(broken, "VER-016",
                      std::string("the rule ") + schema.rule_id + " raises " +
                          step.proof_obligations[i].id + ", which its schema does not declare");
        }

        // A registered precondition the schema does not name is the other half of the containment,
        // and it is the half a rule can break by registering an id nobody declared.
        if (plan != nullptr) {
            for (size_t p = 0; p < plan->preconditions.size(); ++p) {
                bool declared = false;
                for (size_t d = 0; d < schema.obligation_count; ++d) {
                    if (plan->preconditions[p].id == schema.obligations[d].id)
                        declared = true;
                }
                if (!declared)
                    broke(broken, "VER-016",
                          std::string("the rule ") + schema.rule_id + " registers the precondition " +
                              plan->preconditions[p].id + ", which its schema does not declare");
            }
        }

        for (size_t d = 0; d < schema.obligation_count; ++d) {
            const ObligationSchema &obligation = schema.obligations[d];
            // Two channels raise an obligation: a step pushes one, and a plan registers a
            // precondition. Containment is the same rule over the union, so no entry needs a kind.
            const StrategyPrecondition *precondition = nullptr;
            if (plan != nullptr) {
                for (size_t p = 0; p < plan->preconditions.size(); ++p) {
                    if (plan->preconditions[p].id == obligation.id)
                        precondition = &plan->preconditions[p];
                }
            }
            bool raised = precondition != nullptr;
            for (size_t i = 0; i < step.proof_obligations.size() && !raised; ++i) {
                if (step.proof_obligations[i].id == obligation.id)
                    raised = true;
            }
            if (!raised)
                broke(broken, "VER-016", std::string("the rule ") + schema.rule_id +
                      " declares " + obligation.id + " and never raises it");

            if (precondition != nullptr) {
                const VerificationRecord *found = nullptr;
                const EvidenceAlternative *matched = nullptr;
                // A registered precondition names the one record that discharges it, so match on
                // that rather than on the method. Sibling preconditions share a method name, and a
                // method search lets one surviving record answer for a registration that is gone.
                for (size_t v = 0; v < step.verifications.size() && found == nullptr; ++v) {
                    if (step.verifications[v].evidence_id == precondition->evidence_id)
                        found = &step.verifications[v];
                }
                if (found == nullptr) {
                    broke(broken, "VER-016",
                          std::string("the rule ") + schema.rule_id + " registers " + obligation.id +
                              " but recorded no verification carrying its evidence id");
                    continue;
                }
                for (size_t a = 0; a < obligation.evidence_count; ++a) {
                    if (found->method == obligation.evidence[a].method)
                        matched = &obligation.evidence[a];
                }
                if (matched == nullptr) {
                    broke(broken, "VER-016",
                          std::string("the rule ") + schema.rule_id + " discharged " +
                              obligation.id + " by " + found->method +
                              ", which its schema does not name as evidence for it");
                    continue;
                }
                judge(schema, obligation, *found, *matched, status, broken);
                continue;
            }
            // Every match is weighed rather than the first, which would let a passing record answer
            // for a failed one recorded behind it under the same method.
            size_t judged = 0;
            for (size_t v = 0; v < step.verifications.size(); ++v) {
                for (size_t a = 0; a < obligation.evidence_count; ++a) {
                    if (step.verifications[v].method != obligation.evidence[a].method)
                        continue;
                    judge(schema, obligation, step.verifications[v], obligation.evidence[a], status,
                          broken);
                    ++judged;
                    break;
                }
            }
            if (judged == 0)
                broke(broken, "VER-016",
                      std::string("the rule ") + schema.rule_id + " declares " + obligation.id +
                          " but recorded no verification by any method that discharges it");
        }
    }

    // One record weighed against the alternative it matched.
    void judge(const RuleSchema &schema, const ObligationSchema &obligation,
               const VerificationRecord &record, const EvidenceAlternative &alternative,
               DerivationStatus status, std::vector<std::string> *broken) {
        // strength_for is the contract every producer builds a record with, and only its passing arm
        // was held here. A check that disagreed or could not run is worth nothing, so a record that
        // kept its method's passing strength overstates what the step established.
        const EvidenceStrength owed = strength_for(record.outcome, alternative.strength);
        // Inconclusive is the one outcome with two readings, and the record cannot separate them:
        // a check that could not evaluate and one that agreed without being independent write the
        // same two fields. The schema is asked instead, so a method that cannot corroborate is
        // still caught claiming its own passing strength on an outcome that did not earn it.
        const bool corroborated = alternative.may_corroborate &&
                                  record.outcome == VerificationOutcome::Inconclusive &&
                                  record.strength == alternative.strength;
        if (record.strength != owed && !corroborated)
            broke(broken, "VER-016",
                  std::string("the rule ") + schema.rule_id + " recorded " + obligation.id + " by " +
                      record.method + " as " + verification_outcome_name(record.outcome) + " at " +
                      evidence_strength_name(record.strength) +
                      ", where a check with that outcome is worth " + evidence_strength_name(owed));
        if (record.outcome != VerificationOutcome::Failed)
            return;
        if (schema.on_failure == FailureBehavior::CannotFail)
            broke(broken, "VER-016",
                  std::string("the rule ") + schema.rule_id + " declares that " + obligation.id +
                      " cannot fail, and it failed");
        // Withholding is not one status, so it is checked as the absence of the two that claim an
        // answer, the same pair criterion 8 calls a lie for a refusal. No engine reaches this arm:
        // outcome_from turns a failed record in its range into VerificationFailed before a family
        // sets its status. The channel it cannot see is the Lua bridge, which rewrites that status
        // afterwards at lua_module.cc:434, and no pass walks a bridge-produced derivation.
        if (schema.on_failure == FailureBehavior::WithholdResult &&
            (status == DerivationStatus::SolvedAndVerified ||
             status == DerivationStatus::SolvedButUnchecked))
            broke(broken, "VER-016",
                  std::string("the rule ") + schema.rule_id + " failed " + obligation.id +
                      " and declares that it withholds the result, but the derivation reports " +
                      derivation_status_name(status));
    }

    void observe(const Step &step, DerivationStatus status) {
        Observed &o = observed_[step.rule_id];
        ++o.steps;
        if (step.has_failed_verification())
            o.statuses_on_failure.insert(derivation_status_name(status));
        o.claims.insert(claim_type_name(step.claim));
        for (size_t i = 0; i < step.proof_obligations.size(); ++i)
            o.obligations.insert(step.proof_obligations[i].id);
        for (size_t v = 0; v < step.verifications.size(); ++v) {
            const VerificationRecord &record = step.verifications[v];
            if (record.outcome != VerificationOutcome::Passed)
                continue;
            o.evidence.insert(record.method + std::string(" at ") +
                              evidence_strength_name(record.strength));
        }
    }

    void broke(std::vector<std::string> *broken, const char *id, const std::string &what) {
        for (size_t i = 0; i < claims_.size(); ++i) {
            if (std::string(id) != claims_[i].id)
                continue;
            claims_[i].broken += 1;
            const std::string prefix = claims_[i].requirement ? std::string() : "criterion ";
            broken->push_back(prefix + id + ": " + what);
            return;
        }
    }

    void looked_at(const char *id) {
        for (size_t i = 0; i < claims_.size(); ++i) {
            if (std::string(id) == claims_[i].id)
                claims_[i].checked += 1;
        }
    }

    std::vector<Claim> claims_;
    size_t passing_ = 0;
    size_t unclassified_ = 0;
    size_t methodless_ = 0;
    size_t planned_strategies_ = 0;
    size_t rules_seen_ = 0;
    size_t undeclared_ = 0;
    size_t nameless_ = 0;
    size_t implications_ = 0;
    size_t backend_steps_ = 0;
    size_t branch_steps_ = 0;
    size_t split_count_ = 0;
    size_t restricted_splits_ = 0;
    size_t equivalence_steps_ = 0;
    size_t equivalence_exact_ = 0;
    size_t equivalence_sampled_ = 0;
    size_t family_steps_ = 0;
    size_t family_judged_ = 0;
    size_t family_unevaluated_ = 0;
    size_t equivalence_unevaluated_ = 0;
    size_t physical_assumptions_ = 0;
    size_t mathematical_restrictions_ = 0;
    std::map<std::string, Observed> observed_;
};

}  // namespace invariants
}  // namespace nps

#endif
