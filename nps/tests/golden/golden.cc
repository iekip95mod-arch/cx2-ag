#include "golden/golden.h"
#include "step_invariants.h"

#include <cstdlib>
#include <fstream>
#include <vector>

#include "nps/core/print.h"

namespace nps {
namespace {

const size_t kDiffLinesShown = 40;

struct Writer {
    const Arena &arena;
    const Derivation &derivation;
    std::string out;
    std::vector<bool> seen;

    Writer(const Arena &a, const Derivation &d) : arena(a), derivation(d), seen(d.size(), false) {}

    void indent(size_t depth) {
        for (size_t i = 0; i < depth; ++i)
            out += "  ";
    }

    void field(size_t depth, const char *key, const std::string &value) {
        if (value.empty())
            return;
        indent(depth);
        out += key;
        out += ": ";
        out += value;
        out += "\n";
    }

    void list(size_t depth, const char *key, const std::vector<std::string> &values) {
        for (size_t i = 0; i < values.size(); ++i)
            field(depth, key, values[i]);
    }

    void expression(size_t depth, const char *key, NodeId id) {
        if (id == kNoNode || id >= arena.node_count())
            return;
        field(depth, key, print(arena, id));
    }

    void payload(size_t depth, StepId id) {
        if (const PlanPayload *p = derivation.plan(id)) {
            field(depth, "strategy", p->selected_strategy);
            list(depth, "applicability", p->applicability_conditions);
            list(depth, "fact", p->matched_problem_facts);
            list(depth, "alternative", p->alternatives_considered);
            field(depth, "rationale", p->selection_rationale);
            return;
        }
        if (const TransformationPayload *p = derivation.transformation(id)) {
            field(depth, "action", p->concrete_action);
            expression(depth, "before", p->before);
            expression(depth, "after", p->after);
            if (!p->path.empty()) {
                std::string where;
                for (size_t i = 0; i < p->path.size(); ++i) {
                    if (i)
                        where += ".";
                    where += number(p->path[i]);
                }
                field(depth, "at", where);
            }
            field(depth, "reversible", p->reversible ? "yes" : "no");
            return;
        }
        if (const BranchPayload *p = derivation.branch(id)) {
            expression(depth, "condition", p->condition);
            field(depth, "siblings exhaustive", p->siblings_exhaustive ? "yes" : "no");
            field(depth, "siblings exclusive", p->siblings_exclusive ? "yes" : "no");
            field(depth, "siblings domain consistent",
                  p->siblings_domain_consistent ? "yes" : "no");
            field(depth, "exhaustive evidence", p->exhaustive_evidence);
            field(depth, "feasibility", p->feasibility_status);
            field(depth, "resolution", branch_resolution_name(p->resolution));
            field(depth, "resolution evidence", p->resolution_evidence);
            return;
        }
        if (const CheckPayload *p = derivation.check(id)) {
            field(depth, "target claim", p->target_claim);
            field(depth, "check method", p->check_method);
            field(depth, "expected", p->expected_relation);
            field(depth, "observed", p->observed_result);
            return;
        }
    }

    static std::string number(size_t v) {
        std::string s;
        do {
            s.push_back(static_cast<char>('0' + (v % 10)));
            v /= 10;
        } while (v);
        for (size_t i = 0, j = s.size(); i + 1 < j; ++i, --j) {
            char t = s[i];
            s[i] = s[j - 1];
            s[j - 1] = t;
        }
        return s;
    }

    void step(StepId id, const std::string &path, size_t depth) {
        if (id >= derivation.size())
            return;
        seen[id] = true;
        const Step &s = derivation.at(id);

        indent(depth);
        out += path;
        out += " ";
        out += step_kind_name(s.kind);
        out += "\n";

        const size_t inner = depth + 1;
        field(inner, "phase", s.phase);
        field(inner, "goal", s.goal);
        if (!s.rule_id.empty() || !s.rule_name.empty())
            field(inner, "rule", s.rule_id + (s.rule_name.empty() ? "" : ", " + s.rule_name));
        field(inner, "claim", claim_type_name(s.claim));
        field(inner, "explanation", s.explanation_short);
        field(inner, "detail", s.explanation_detailed);
        list(inner, "assumption before", s.assumptions_before);
        list(inner, "assumption after", s.assumptions_after);
        list(inner, "domain restriction", s.domain_restrictions);
        for (size_t i = 0; i < s.proof_obligations.size(); ++i) {
            field(inner, "proof obligation",
                  s.proof_obligations[i].id + ", " + s.proof_obligations[i].text);
        }
        payload(inner, id);
        for (size_t i = 0; i < s.verifications.size(); ++i) {
            const VerificationRecord &v = s.verifications[i];
            std::string line = v.method;
            line += ", ";
            line += verification_outcome_name(v.outcome);
            if (!v.detail.empty())
                line += ", " + v.detail;
            field(inner, "verification", line);
        }
        // Three states rather than two. Step::verified() answers yes for a record that claims
        // nothing, which is the model's own rule and not the same as having been checked, so
        // writing a bare yes into a fixture a human reads would state the conflation the record
        // model exists to prevent.
        field(inner, "verified",
              s.claim == ClaimType::NoClaim ? "nothing claimed" : (s.verified() ? "yes" : "no"));
        if (s.backend_requests != 0)
            field(inner, "backend requests", number(s.backend_requests));

        for (size_t i = 0; i < s.children.size(); ++i)
            step(s.children[i], path + "." + number(i + 1), inner);
    }
};

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (text[i] != '\r')
            current.push_back(text[i]);
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

// Longest common subsequence over lines, so one inserted step shows as one inserted line rather
// than making every line after it look changed. The records are tens of lines, so the table costs
// nothing and a readable diff is the whole point of the fixture.
std::string line_diff(const std::vector<std::string> &want, const std::vector<std::string> &got) {
    const size_t n = want.size();
    const size_t m = got.size();
    std::vector<size_t> table((n + 1) * (m + 1), 0);
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            table[i * (m + 1) + j] = want[i] == got[j]
                                         ? table[(i + 1) * (m + 1) + (j + 1)] + 1
                                         : (table[(i + 1) * (m + 1) + j] >
                                                    table[i * (m + 1) + (j + 1)]
                                                ? table[(i + 1) * (m + 1) + j]
                                                : table[i * (m + 1) + (j + 1)]);
        }
    }

    std::string out;
    size_t shown = 0;
    size_t i = 0, j = 0;
    while (i < n && j < m && shown < kDiffLinesShown) {
        if (want[i] == got[j]) {
            ++i;
            ++j;
            continue;
        }
        if (table[(i + 1) * (m + 1) + j] >= table[i * (m + 1) + (j + 1)]) {
            out += "\n      - " + want[i++];
        } else {
            out += "\n      + " + got[j++];
        }
        ++shown;
    }
    while (i < n && shown < kDiffLinesShown) {
        out += "\n      - " + want[i++];
        ++shown;
    }
    while (j < m && shown < kDiffLinesShown) {
        out += "\n      + " + got[j++];
        ++shown;
    }
    if (i < n || j < m)
        out += "\n      ... more lines differ";
    return out;
}

std::string golden_path(const std::string &name) {
    const char *dir = getenv("NPS_GOLDEN_DIR");
    return std::string(dir ? dir : "tests/golden/fixtures") + "/" + name + ".txt";
}

bool read_file(const std::string &path, std::string *text) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;
    text->assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const std::string &path, const std::string &text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out)
        return false;
    out << text;
    return out.good();
}

std::string stable_record(std::string record) {
    const std::string prefix = "  capability manifest: ";
    const size_t value = record.find(prefix);
    if (value == std::string::npos)
        return record;
    const size_t begin = value + prefix.size();
    const size_t end = record.find('\n', begin);
    record.replace(begin, end == std::string::npos ? record.size() - begin : end - begin,
                   "<current>");
    return record;
}

}  // namespace

namespace {

invariants::Pass g_pass;
std::vector<std::string> g_breaks;
size_t g_derivations = 0;

}  // namespace

void check_golden_invariants(TestSink &sink) {
    for (size_t i = 0; i < g_breaks.size(); ++i)
        sink.check(false, "golden invariants: " + g_breaks[i]);

    // Criterion 8 asks whether a refusal claimed a verified solution, and the outcome is in the
    // fixture header rather than in the derivation, so this population cannot answer it. Left
    // uncounted rather than counted as held: the corpus is where criterion 8 is evidenced.
    size_t looked_at = 0;
    for (size_t i = 0; i < g_pass.claims().size(); ++i)
        looked_at += g_pass.claims()[i].checked;
    sink.check(g_derivations > 0 && looked_at > 0,
               "golden invariants: the pass saw " + std::to_string(looked_at) + " steps across " +
                   std::to_string(g_derivations) + " fixture derivations");

    const invariants::Claim *fields = g_pass.find("STEP-002");
    const invariants::Claim *labels = g_pass.find("STEP-022");
    sink.evidence("STEP-002", fields != nullptr && fields->checked > 0 && fields->broken == 0,
                  "every one of " + std::to_string(fields ? fields->checked : 0) +
                      " transformations in the golden fixtures carries a before state, an after "
                      "state, a rule id, a readable rule name and a short explanation");
    sink.evidence("STEP-022", labels != nullptr && labels->checked > 0 && labels->broken == 0,
                  "across " + std::to_string(labels ? labels->checked : 0) +
                      " fixture steps, no rule name and no recorded action is the bare label "
                      "simplify, solve, after algebra or by CAS");

    // VER-011 and STEP-004 both want the strength beside the method. A record's claim type is its
    // step's claim, which criterion 5 already holds every major step to, so what was missing was the
    // strength. This population is the second reading of it: the corpus covers what the engines
    // produce for real problems, and the fixtures cover the refusals and halts it does not reach.
    const size_t classified = g_pass.passing_verifications() - g_pass.unclassified_verifications();
    sink.evidence("VER-011",
                  g_pass.passing_verifications() > 0 && g_pass.unclassified_verifications() == 0,
                  "every one of " + std::to_string(classified) +
                      " passing verifications across the golden fixtures names what its evidence "
                      "is worth, under the step whose claim type it answers for");
    sink.evidence("STEP-004",
                  g_pass.passing_verifications() > 0 && g_pass.methodless_verifications() == 0,
                  "and each names the method beside it, so the strength is recorded with the "
                  "validation rather than in place of it");

    // VER-016's declaration count over this population. The fixtures reach 114 distinct rules where
    // the corpus reaches 32, so this is the reading that says how much of the engine is declared;
    // the corpus report is where the undeclared ones are listed with what they were seen doing.
    // Reported rather than gated while the table is filled, and it becomes a claim at zero.
    sink.check(g_pass.rules_seen() > 0 && g_pass.undeclared_rules() == 0 &&
                   g_pass.nameless_steps() == 0,
               "golden invariants: all " + std::to_string(g_pass.rules_seen()) +
                   " recorded steps name a rule, and every one of those rules declares a "
                   "proof-obligation schema");
    // STEP-007 over this population. The fixtures are where the rejecting case lives: the catch-up
    // shared-domain check records a failed verification when the algebraic root precedes the shared
    // interval, and the derivation reports unsupported rather than that root. Counting the
    // implication steps beside the claim keeps it honest, because a gate that judged none would
    // print the same line.
    const invariants::Claim *implications = g_pass.find("STEP-007");
    sink.evidence("STEP-007",
                  implications != nullptr && implications->checked > 0 &&
                      implications->broken == 0 && g_pass.implication_steps() > 0,
                  "each of " + std::to_string(g_pass.implication_steps()) +
                      " steps across the golden fixtures whose claim runs one way raises an "
                      "obligation for the candidate it produced and records a check of it, and the "
                      "fixture whose root falls outside the shared active interval reports "
                      "unsupported rather than the root");

    const invariants::Claim *applicability = g_pass.find("PHYS-026");
    sink.evidence("PHYS-026",
                  applicability != nullptr && applicability->broken == 0 &&
                      g_pass.planned_strategies() > 0,
                  "each of " + std::to_string(g_pass.planned_strategies()) +
                      " plans registers at least one applicability condition and backs at least one "
                      "of them with evidence of some kind other than dimensional agreement, so no "
                      "physical model in these fixtures was selected on matching units alone, and "
                      "none was selected on no record at all. What each record maps to is checked "
                      "separately and structurally: derivation.cc refuses a plan whose applicability "
                      "conditions, preconditions and evidence do not correspond one to one, though "
                      "it admits an empty registration, which is why the count above is of plans "
                      "rather than of plans that registered something");

    const invariants::Claim *backend = g_pass.find("STEP-019");
    sink.evidence("STEP-019",
                  backend != nullptr && backend->broken == 0 && g_pass.backend_steps() > 0,
                  "each of " + std::to_string(g_pass.backend_steps()) +
                      " fixture steps that consulted the backend still names its own rule and "
                      "records its own result, so none of them is a backend answer presented as a "
                      "walkthrough");

    // MATH-014's third clause over every transformation in the set. Parameter survival is evidenced
    // by the rearrangement family, which is the one that declares symbolic parameters, and branch
    // completeness by VER-017 below rather than a second time here.
    const invariants::Claim *sampling = g_pass.find("MATH-014");
    sink.evidence("MATH-014",
                  sampling != nullptr && sampling->checked > 0 && sampling->broken == 0,
                  "none of " + std::to_string(sampling != nullptr ? sampling->checked : 0) +
                      " fixture transformations rests on numeric sampling: sampling appears only on "
                      "checks, where it corroborates an answer rather than obtaining one");

    // STEP-024 and VER-017 over this population, which is the only one that reaches a split: the
    // acceptance corpus runs the five families PRD section 22.1 names and none of them branches.
    // Both counts are printed because the claims are quantified over splits rather than over steps,
    // and a pass that met no split would print these same two sentences.
    // VER-002 over this population, with the readings printed apart. The exact count is the
    // one that settles a claim: both sides closed, both evaluated, and equal. The sampled count is
    // corroboration and is worded as such, because VER-009 forbids a spot check standing in for a
    // symbolic proof. The declined count is printed beside them so an exclusion that grew
    // would be visible rather than quiet.
    const invariants::Claim *equivalence = g_pass.find("VER-002");
    sink.evidence("VER-002",
                  equivalence != nullptr && equivalence->broken == 0 &&
                      g_pass.equivalence_exact() > 0 && g_pass.equivalence_sampled() > 0,
                  std::to_string(g_pass.equivalence_exact()) +
                      " fixture steps claiming an equivalent expression were settled exactly, both "
                      "sides closed and equal, and " +
                      std::to_string(g_pass.equivalence_sampled()) +
                      " more agreed at every rational assignment tried, which corroborates them "
                      "and does not prove them. " +
                      std::to_string(g_pass.family_judged()) + " of " +
                      std::to_string(g_pass.family_steps()) +
                      " steps naming a family up to a constant were read as that family, and " +
                      std::to_string(g_pass.equivalence_unevaluated() +
                                     g_pass.family_unevaluated()) +
                      " had no rational value to compare, which is declined rather than judged");

    const invariants::Claim *branching = g_pass.find("STEP-024");
    sink.evidence("STEP-024",
                  branching != nullptr && branching->checked > 0 && branching->broken == 0 &&
                      g_pass.split_count() > 0,
                  "each of " + std::to_string(g_pass.split_count()) + " splits across " +
                      std::to_string(g_pass.branch_steps()) +
                      " branch records says whether its cases are exhaustive, mutually exclusive "
                      "and domain-consistent, its members agree on all three, no two of them state "
                      "the same condition, and every case is solved or rejected with what settled "
                      "it named. Exclusivity is read as far as duplicate conditions, which for a "
                      "split into distinct rational roots is the whole of it but is not the general "
                      "property, and domain consistency is discharged by construction here rather "
                      "than checked");
    // The conditional arm of domain consistency, reported apart. A split with no restriction in
    // force discharges the property by construction, so the arm that would ask for an argument has
    // never run, and saying the claim held over zero of them is the honest reading rather than
    // letting the vacuous arm speak for both.
    sink.check(g_pass.restricted_splits() == 0,
               "golden invariants: " + std::to_string(g_pass.restricted_splits()) +
                   " splits ran with a domain restriction in force, and STEP-024 counts those "
                   "rather than judging them, having no evidence field to judge one by");

    // Both counts gate the row, because a split where one side is empty is not a split. If every
    // step-level assumption were of one kind, the field holding it would read as a distinction and
    // would not be one, and that is the failure this row exists to make visible rather than the
    // one it assumes away.
    const invariants::Claim *assumption_kinds = g_pass.find("PHYS-027");
    sink.evidence("PHYS-027",
                  assumption_kinds != nullptr && assumption_kinds->checked > 0 &&
                      assumption_kinds->broken == 0 && g_pass.physical_assumptions() > 0 &&
                      g_pass.mathematical_restrictions() > 0,
                  std::to_string(g_pass.physical_assumptions()) +
                      " physical modelling assumptions and " +
                      std::to_string(g_pass.mathematical_restrictions()) +
                      " mathematical domain conditions across " +
                      std::to_string(assumption_kinds == nullptr ? 0 : assumption_kinds->checked) +
                      " fixture steps are each attached to the step they bear on and held in the "
                      "field for their kind, both kinds occur, and no physics rule holds one of the "
                      "mathematical kind. The two are told apart by which writer can reach the "
                      "field, not by reading the string back");

    const invariants::Claim *completeness = g_pass.find("VER-017");
    sink.evidence("VER-017",
                  completeness != nullptr && completeness->checked > 0 &&
                      completeness->broken == 0 && g_pass.split_count() > 0,
                  "each of " + std::to_string(g_pass.split_count()) +
                      " splits names the method that argues it covers every case and carries a "
                      "passing verification by that method, every case it records as solved "
                      "carries a passing candidate check against the original problem, and none "
                      "reports a solution over a case left unsettled");

    const invariants::Claim *schema = g_pass.find("VER-016");
    sink.evidence("VER-016", schema != nullptr && schema->checked > 0 && schema->broken == 0,
                  "every one of " + std::to_string(schema ? schema->checked : 0) +
                      " fixture steps matches its rule's declared schema: the claim it makes, the "
                      "obligations it raises, and a verification of a declared method for each "
                      "obligation at the strength the schema says that method is worth");
}

std::string render_derivation(const Arena &arena, const Derivation &derivation) {
    ++g_derivations;
    g_pass.walk(arena, derivation, false, false, &g_breaks);
    Writer w(arena, derivation);

    w.out += "context\n";
    const SolutionContext &c = derivation.context;
    w.field(1, "application version", c.application_version);
    w.field(1, "capability manifest", c.capability_manifest_id);
    w.field(1, "problem family", c.problem_family_id);
    w.field(1, "envelope version", c.problem_family_envelope_version);
    w.expression(1, "normalized problem", c.normalized_problem_model);
    w.field(1, "requested method", c.requested_method);
    w.list(1, "active assumption", c.active_assumptions);
    w.field(1, "angle convention", c.angle_convention);
    w.field(1, "branch convention", c.branch_convention);
    w.field(1, "unit policy", c.unit_policy);
    w.field(1, "detail projection", c.detail_projection);
    w.field(1, "resource policy", c.resource_policy);
    w.list(1, "content pack", c.content_pack_versions);
    w.field(1, "derivation status", derivation_status_name(c.derivation_status));

    w.out += "steps\n";
    if (derivation.size() == 0)
        w.out += "  none\n";
    const std::vector<StepId> &roots = derivation.roots();
    for (size_t i = 0; i < roots.size(); ++i)
        w.step(roots[i], Writer::number(i + 1), 1);

    // A record that no root reaches would otherwise vanish from the fixture, so it is named here
    // rather than dropped. Nothing should ever land in this section.
    bool any_unreached = false;
    for (StepId id = 0; id < derivation.size(); ++id) {
        if (w.seen[id])
            continue;
        if (!any_unreached) {
            w.out += "unreachable\n";
            any_unreached = true;
        }
        w.step(id, Writer::number(id), 1);
    }

    return w.out;
}

void check_golden(TestSink &sink, const std::string &name, const std::string &record) {
    const std::string path = golden_path(name);
    const bool regenerating = getenv("NPS_REGOLD") != 0;
    const std::string stable = stable_record(record);

    if (regenerating) {
        if (!write_file(path, stable)) {
            sink.check(false, "golden " + name + ": could not write " + path);
            return;
        }
        sink.check(false, "golden " + name + ": fixture rewritten, rerun without NPS_REGOLD to "
                                             "check it");
        return;
    }

    std::string want;
    if (!read_file(path, &want)) {
        sink.check(false, "golden " + name + ": no fixture at " + path +
                              ", run cmake --build build/host --target regold from nps and review the fixture");
        return;
    }

    if (want == stable) {
        sink.check(true, "golden " + name);
        return;
    }

    std::vector<std::string> want_lines = split_lines(want);
    std::vector<std::string> got_lines = split_lines(stable);
    sink.check(false, "golden " + name + " differs from " + path + " (- fixture, + this run)" +
                          line_diff(want_lines, got_lines));
}

}  // namespace nps
