// PRD section 27 and section 19.9's second sentence. The coverage catalog is checked against what
// the golden fixtures actually record, so a family cannot claim a rule the corpus never runs and a
// rule cannot run without the catalog knowing about it. What is claimed and what happened are two
// files, and this is the join.
//
// Also reports which of section 27's fields the catalog does not carry, because a field left out is
// a question nobody answered and a field filled with a placeholder reads as one that was.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "nps/steps/schema.h"

#include "catalog.h"
#include "evidence.h"
#include "rule_cases.h"
#include "scratch_directory.h"

using nps_tools::append_evidence;
using nps_tools::count_text;
using nps_tools::Family;
using nps_tools::read_catalog;
using nps_tools::RuleCaseKind;
using nps_tools::trimmed;

namespace {

// Section 27 requires each field for every family.
const char *kSection27Fields[] = {
    "id",
    "reference_curriculum_set_ids",
    "curriculum_source_locations",
    "topic_and_level",
    "family_envelope_version",
    "accepted_expression_grammar",
    "domains_and_parameter_assumptions",
    "supported_branches_and_degenerate_cases",
    "exact_special_function_and_numerical_result_policy",
    "accepted_input_forms",
    "word_language_profile_ids",
    "parser_module_ids",
    "required_assumptions",
    "supported_methods",
    "unsupported_near_neighbors",
    "strategy_ids",
    "rule_ids",
    "proof_obligation_ids",
    "solution_soundness_status",
    "solution_completeness_status",
    "corpus_case_ids",
    "explanation_review_status",
    "learner_transfer_status",
    "device_performance_status",
    "direct_keypad_entry_status",
    "isolated_runtime_status",
    "capability_manifest_ids",
    "release_status",
};

// Section 5.5's envelope is the subset checked for MATH-013.
const char *kEnvelopeFields[] = {
    "accepted_expression_grammar",
    "domains_and_parameter_assumptions",
    "required_assumptions",
    "exact_special_function_and_numerical_result_policy",
    "supported_branches_and_degenerate_cases",
    "unsupported_near_neighbors",
    "supported_methods",
    "proof_obligation_ids",
};

// A test group that ran and belongs to no problem family, with what it exercises instead.
const struct {
    const char *group;
    const char *what;
} kGroupsWithNoFamily[] = {
    {"acceptance corpus", "the corpus audit's own evidence"},
    {"canonical", "core normalization every family shares"},
    {"coverage", "this tool's own evidence"},
    {"derivation", "the derivation record itself"},
    {"fuzz", "the parser fuzz vehicle"},
    {"integrity", "package staging and digests"},
    {"luax", "the Lua bridge vehicle"},
    {"native menu", "the native menu surface"},
    {"solve_task", "dispatch onto the algebra families their own groups name"},
    {"task", "the core task and coroutine drivers"},
    {"ui canvas", "host canvas rendering"},
    {"ui v4", "the Ki V4 shell vehicle"},
    {"vectors", "a sub-group inside the units tests"},
};

// An engine family with no catalog block, which is the state graph integration was in before #441.
struct AwaitingGroup {
    const char *group;
    const char *engine;
};

// A vector rather than an array, because a zero-length array is not valid and this table empties.
const std::vector<AwaitingGroup> kGroupsAwaitingCatalog = {};

// One line decides whether a family with no catalog block fails the run or only reports.
const bool kAwaitingCatalogIsFatal = true;

// A family id the run stamps that no engine implements, with what stamps it instead.
struct FamilyWithNoEngine {
    const char *family;
    const char *what;
};

const std::vector<FamilyWithNoEngine> kFamiliesWithNoEngine = {
    {"calculus.derivative", "tests/unit/context_tests.cc, a staged context for the serializer"},
    {"calculus.integral", "tests/unit/context_tests.cc, a staged context for the serializer"},
    {"linear.one-unknown", "tests/unit/context_tests.cc, a staged context for the serializer"},
};

// A catalogued family that no run stamps, with the reason nothing does.
struct FamilyWithNoRun {
    const char *family;
    const char *why;
};

const std::vector<FamilyWithNoRun> kFamiliesNoRunStamps = {};

struct Outcome {
    bool join_ran = false;
    size_t uncatalogued = 0;
    size_t awaiting_catalog = 0;
    size_t stale_exemptions = 0;
    bool family_census_ran = false;
    size_t uncatalogued_families = 0;
    size_t unstamped_families = 0;
};

// Every group the test run reported, in the rows evidence.h writes.
void read_groups_run(const std::string &path, std::set<std::string> *groups) {
    std::ifstream in(path.c_str());
    if (!in)
        return;
    const std::string key = "group\t";
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        const std::string name = trimmed(line.substr(key.size()));
        if (!name.empty())
            groups->insert(name);
    }
}

// Every family id the run stamped, which the host build records at make_context.
void read_families_stamped(const std::string &path, std::set<std::string> *families) {
    std::ifstream in(path.c_str());
    if (!in)
        return;
    const std::string key = "family\t";
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        const std::string name = trimmed(line.substr(key.size()));
        if (!name.empty())
            families->insert(name);
    }
}

// What a fixture records: the family it belongs to and every rule id in it. The fixture is the
// corpus case, so this is the evidence side of the join.
void read_fixture(const std::string &path, std::string *family, std::set<std::string> *rules,
                  std::set<std::string> *obligations, std::set<std::string> *carried) {
    std::ifstream in(path.c_str());
    if (!in)
        return;
    const std::string family_key = "problem family:";
    const std::string rule_key = "rule:";
    const std::string obligation_key = "proof obligation:";
    // The two channels that carry an assumption. A domain restriction is a condition one step met
    // along the way, which is what the catalog's derivative line says it is not.
    const std::string active_key = "active assumption:";
    const std::string before_key = "assumption before:";
    std::string line;
    while (std::getline(in, line)) {
        const std::string s = trimmed(line);
        if (s.rfind(family_key, 0) == 0) {
            *family = trimmed(s.substr(family_key.size()));
            continue;
        }
        // Both are written as an id, a comma and the prose that goes with it.
        if (s.rfind(rule_key, 0) == 0) {
            const std::string rest = trimmed(s.substr(rule_key.size()));
            const size_t comma = rest.find(',');
            rules->insert(comma == std::string::npos ? rest : rest.substr(0, comma));
            continue;
        }
        if (s.rfind(obligation_key, 0) == 0) {
            const std::string rest = trimmed(s.substr(obligation_key.size()));
            const size_t comma = rest.find(',');
            obligations->insert(comma == std::string::npos ? rest : rest.substr(0, comma));
            continue;
        }
        // Whole, unlike a rule or an obligation. An assumption is a sentence and one of them is
        // "motion is along one axis, positive in the chosen direction".
        if (s.rfind(active_key, 0) == 0) {
            carried->insert(trimmed(s.substr(active_key.size())));
            continue;
        }
        if (s.rfind(before_key, 0) == 0)
            carried->insert(trimmed(s.substr(before_key.size())));
    }
}

// Rule and kind pairs with no case, pinned so a change that moves the count updates it.
constexpr size_t kRuleCaseGaps = 913;

struct RuleCaseJoin {
    size_t rows = 0;
    size_t faults = 0;
    size_t complete = 0;
    size_t gaps = 0;
    size_t per_kind[nps_tools::kRuleCaseKindCount] = {};
};

// Every registered rule against the rule case rows. A bad row is a fault, a missing kind a gap.
RuleCaseJoin join_rule_cases(const std::string &evidence_path, const std::vector<Family> &families,
                             std::ostream &report) {
    RuleCaseJoin out;
    std::map<std::string, std::set<RuleCaseKind> > kinds;
    std::map<std::string, std::set<std::string> > sources;
    std::ifstream in(evidence_path.c_str());
    if (!in) {
        ++out.faults;
        std::cout << "coverage: no evidence file to read rule cases from at " << evidence_path
                  << "\n";
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("rule_case\t", 0) != 0)
            continue;
        std::vector<std::string> fields;
        size_t start = 0;
        for (size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', start)) {
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        fields.push_back(line.substr(start));
        ++out.rows;
        RuleCaseKind kind = RuleCaseKind::Positive;
        if (fields.size() != 6) {
            ++out.faults;
            std::cout << "coverage: a rule case row has " << count_text(fields.size())
                      << " fields rather than 6: " << line << "\n";
            continue;
        }
        if (nps::rule_schema(fields[1]) == nullptr) {
            ++out.faults;
            std::cout << "coverage: a rule case names " << fields[1]
                      << ", which is not a registered rule\n";
            continue;
        }
        if (!nps_tools::rule_case_kind_from_name(fields[2], &kind)) {
            ++out.faults;
            std::cout << "coverage: a rule case of " << fields[1] << " names the unknown kind "
                      << fields[2] << "\n";
            continue;
        }
        if (fields[3] != "pass") {
            ++out.faults;
            std::cout << "coverage: the " << fields[2] << " case of " << fields[1]
                      << " did not pass: " << fields[5] << "\n";
            continue;
        }
        kinds[fields[1]].insert(kind);
        sources[fields[1]].insert(fields[4]);
    }

    report << "\n## Rule case kinds\n\n";
    report << "VER-010 asks every solver rule for positive, negative, boundary and regression "
              "cases. Positive and negative are read off derivations by the invariant pass over the "
              "golden fixtures and the acceptance corpus. Boundary and regression are declared by a "
              "unit test whose derivation has to reach the rule, and a regression has to name its "
              "issue. A dash is a kind no passing case supplies.\n\n";
    report << "| Rule | Family | Positive | Negative | Boundary | Regression | Sources |\n"
              "|---|---|---|---|---|---|---|\n";
    const size_t declared = nps::declared_rule_count();
    for (size_t i = 0; i < declared; ++i) {
        const std::string id = nps::declared_rule(i).rule_id;
        std::string family;
        for (const Family &f : families) {
            for (const nps_tools::Rule &r : f.rules) {
                if (r.id == id)
                    family += (family.empty() ? "" : ", ") + f.id;
            }
        }
        if (family.empty())
            family = "-";
        const std::set<RuleCaseKind> &have = kinds[id];
        report << "| " << id << " | " << family << " |";
        for (size_t k = 0; k < nps_tools::kRuleCaseKindCount; ++k) {
            const bool present = have.count(static_cast<RuleCaseKind>(k)) != 0;
            if (present)
                ++out.per_kind[k];
            else
                ++out.gaps;
            report << " " << (present ? "yes" : "-") << " |";
        }
        if (have.size() == nps_tools::kRuleCaseKindCount)
            ++out.complete;
        std::string joined;
        for (const std::string &source : sources[id])
            joined += (joined.empty() ? "" : ", ") + source;
        report << " " << (joined.empty() ? std::string("-") : joined) << " |\n";
    }
    report << "\n" << count_text(out.complete) << " of " << count_text(declared)
           << " registered rules have all four kinds. Positive " << count_text(out.per_kind[0])
           << ", negative " << count_text(out.per_kind[1]) << ", boundary "
           << count_text(out.per_kind[2]) << ", regression " << count_text(out.per_kind[3]) << ", "
           << count_text(out.gaps) << " rule and kind pairs with no case, from "
           << count_text(out.rows) << " rule case rows. VER-010 is unmet until that last count is "
              "zero, so no evidence row is written for it.\n";
    return out;
}

}  // namespace

int coverage(const std::string &catalog_path, const std::string &fixtures_dir,
             const std::string &report_path, const char *evidence_path, Outcome *out = nullptr,
             const std::vector<AwaitingGroup> &awaiting = kGroupsAwaitingCatalog,
             const std::vector<FamilyWithNoEngine> &no_engine = kFamiliesWithNoEngine,
             const std::vector<FamilyWithNoRun> &no_run = kFamiliesNoRunStamps,
             bool rule_cases = false);

// Staged catalogs test reader conventions and reporting without changing the release catalog.
int selftest() {
    const nps::ScratchDirectory scratch("nps_coverage_");
    const std::string &made = scratch.path();
    if (made.empty()) {
        std::cout << "coverage selftest: no temporary directory\n";
        return 1;
    }
    const std::string path = made + "/families.md";
    std::ofstream out(path.c_str());
    out << "family id calculus.derivative\n"
        << "proof_obligation_ids none, every rule here rewrites the expression in place\n"
        << "required_assumptions none, the rules are identities\n"
        << "family id linear.isolate\n"
        << "proof_obligation_ids obl.linear.balance, obl.linear.invertible\n"
        << "required_assumptions the axis is declared, carried as \"motion is along one axis, "
           "positive in the chosen direction\"\n"
        << "family id quadratic.square-root\n"
        << "proof_obligation_ids nonesuch.obligation\n"
        << "required_assumptions carried as \"x > 0\" and \"y > 0\"\n"
        << "family id physics.work.constant-force-dot-product\n"
        << "required_assumptions carried as \"force profile is constant\n"
        << "family id staged.bare.none\n"
        << "supported_methods none\n"
        << "unsupported_near_neighbors none,\n"
        << "accepted_expression_grammar none of the trigonometric forms\n";
    out.close();

    std::vector<Family> families;
    if (!read_catalog(path, &families) || families.size() != 5) {
        std::cout << "coverage selftest: the staged catalog did not read back\n";
        return 1;
    }

    int failures = 0;
    const bool none_empty = families[0].obligations.empty();
    const bool none_declared = families[0].fields.count("proof_obligation_ids") == 1;
    const bool ids_kept = families[1].obligations.size() == 2 &&
                          families[1].obligations[0] == "obl.linear.balance" &&
                          families[1].obligations[1] == "obl.linear.invertible";
    const bool prefix_kept = families[2].obligations.size() == 1 &&
                             families[2].obligations[0] == "nonesuch.obligation";
    const bool quote_whole =
        families[1].assumption_quotes.size() == 1 &&
        families[1].assumption_quotes[0] == "motion is along one axis, positive in the chosen direction";
    const bool two_quotes = families[2].assumption_quotes.size() == 2 &&
                            families[2].assumption_quotes[0] == "x > 0" &&
                            families[2].assumption_quotes[1] == "y > 0";
    const bool none_quotes_nothing =
        families[0].assumptions_none && families[0].assumption_quotes.empty();
    const bool unclosed_faulted = families[3].assumptions_malformed;
    const bool bare_none_declared = families[4].fields.count("supported_methods") == 1;
    const bool bare_none_unanswered = families[4].answered.count("supported_methods") == 0;
    const bool trailing_comma_unanswered = families[4].answered.count("unsupported_near_neighbors") == 0;
    const bool none_with_reason_answered = families[0].answered.count("required_assumptions") == 1;
    const bool none_as_a_word_answered =
        families[4].answered.count("accepted_expression_grammar") == 1;
    const struct {
        bool ok;
        const char *what;
    } checks[] = {
        {none_empty, "a field opening with none raises no obligation"},
        {none_declared, "a field opening with none still counts as declared"},
        {ids_kept, "a list of ids is read as those ids"},
        {prefix_kept, "an id that merely starts with none is read as an id"},
        {quote_whole, "a quoted assumption is read whole, comma and all"},
        {two_quotes, "two quoted assumptions are read as two"},
        {none_quotes_nothing, "an assumptions line declaring none quotes nothing"},
        {unclosed_faulted, "an unclosed quotation is a fault rather than a skip"},
        {bare_none_declared, "a field answered with a bare none is still carried"},
        {bare_none_unanswered, "but it does not answer its question"},
        {trailing_comma_unanswered, "and neither does a none with a comma and no reason after it"},
        {none_with_reason_answered, "a none that says why does answer it"},
        {none_as_a_word_answered, "and so does a real answer that happens to start with the word none"},
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
        if (!checks[i].ok)
            ++failures;
        std::cout << "coverage selftest: " << (checks[i].ok ? "ok   " : "FAIL ") << checks[i].what
                  << "\n";
    }
    const std::string metadata_path = made + "/metadata.md";
    const std::string fixtures_dir = made + "/fixtures";
    const std::string report_path = made + "/report.md";
    std::filesystem::create_directory(fixtures_dir);
    std::ofstream fixture(fixtures_dir + "/metadata.txt");
    fixture << "problem family: staged.complete\nrule: eq.divide-both-sides, staged rule\n";
    fixture.close();

    // The evidence word on a rule line names a closed set, so an unknown one is a typo that took the
    // device path at every consumer and removed the rule from the join with nothing said about it.
    const std::string evidence_catalog = made + "/evidence-class.md";
    const struct {
        const char *rule_line;
        bool reads;
        // What the refusal has to name, so a rule line that lost its id is still said out loud.
        const char *fault_names;
        const char *what;
    } evidence_cases[] = {
        {"rule eq.divide-both-sides fixture", true, "", "a rule claiming fixture evidence reads back"},
        {"rule eq.divide-both-sides device", true, "", "and so does one declaring device evidence"},
        {"rule eq.divide-both-sides devicex", false, "eq.divide-both-sides",
         "a misspelled device is refused rather than read as one"},
        {"rule eq.divide-both-sides Fixture", false, "eq.divide-both-sides",
         "and so is a capitalised fixture"},
        {"rule eq.divide-both-sides fixture device", false, "eq.divide-both-sides",
         "and so is a line naming two classes"},
        {"rule eq.divide-both-sides", false, "eq.divide-both-sides",
         "a rule line naming no evidence class is refused"},
        {"rule", false, "no known evidence class: rule",
         "a rule line that lost its id as well is refused, not skipped"},
        {"rule\teq.divide-both-sides fixture", true, "",
         "a tab before the rule id reads as a rule rather than as a field name"},
        {"rule eq.divide-both-sides\tfixture", true, "",
         "and so does a tab before its evidence word"},
        {"rule\teq.divide-both-sides\tdevicex", false, "eq.divide-both-sides",
         "a tab-separated rule line with an unknown evidence class is refused"},
    };
    for (size_t i = 0; i < sizeof(evidence_cases) / sizeof(evidence_cases[0]); ++i) {
        std::ofstream staged(evidence_catalog.c_str());
        staged << "family id staged.evidence\n" << evidence_cases[i].rule_line << "\n";
        staged.close();
        std::vector<Family> staged_families;
        std::string fault;
        const bool ok = read_catalog(evidence_catalog, &staged_families, &fault);
        // A read that keeps the rule also keeps the catalog's field names clean, because a rule
        // line misread as a field puts its own text into the union coverage reports against.
        const bool as_expected =
            ok == evidence_cases[i].reads &&
            (evidence_cases[i].reads
                 ? fault.empty() && staged_families.size() == 1 &&
                       staged_families[0].rules.size() == 1 &&
                       staged_families[0].rules[0].id == "eq.divide-both-sides" &&
                       staged_families[0].fields.size() == 2 &&
                       staged_families[0].fields.count("rule_ids") == 1
                 : staged_families.empty() &&
                       fault.find(evidence_cases[i].fault_names) != std::string::npos);
        if (!as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                  << evidence_cases[i].what << "\n";
    }
    // A consumer of the catalog refuses that input rather than reporting over it, so no report is
    // written for a catalog whose evidence class nobody defined.
    const std::string refused_report = made + "/refused.md";
    {
        std::ofstream staged(evidence_catalog.c_str());
        staged << "family id staged.evidence\nrule eq.divide-both-sides devicex\n";
        staged.close();
        const bool refused = coverage(evidence_catalog, fixtures_dir, refused_report, nullptr) != 0 &&
                             !std::filesystem::exists(refused_report);
        if (!refused)
            ++failures;
        std::cout << "coverage selftest: " << (refused ? "ok   " : "FAIL ")
                  << "the coverage report is not written over an unknown evidence class\n";
    }
    const std::string header_catalog = made + "/family-header.md";
    const struct {
        const char *header_line;
        bool reads;
        const char *fault_names;
        const char *what;
    } header_cases[] = {
        {"family id staged.one", true, "", "a valid family header reads back"},
        {"family ix staged.one", false, "family ix staged.one",
         "a family header with a misspelled id word is refused"},
        {"family", false, "malformed: family", "a bare family header is refused, not skipped"},
        {"family id", false, "family id", "a family header with no family id is refused"},
        {"family id.staged.one", false, "family id.staged.one",
         "a dot instead of space after family id is refused"},
        {"family id staged.one extra", false, "family id staged.one extra",
         "a family header with extra tokens is refused"},
    };
    for (size_t i = 0; i < sizeof(header_cases) / sizeof(header_cases[0]); ++i) {
        std::ofstream staged(header_catalog.c_str());
        staged << "family id staged.previous\n"
               << "rule eq.divide-both-sides fixture\n"
               << header_cases[i].header_line << "\n"
               << "rule eq.divide-both-sides fixture\n";
        staged.close();
        std::vector<Family> staged_families;
        std::string fault;
        const bool ok = read_catalog(header_catalog, &staged_families, &fault);
        const bool as_expected =
            ok == header_cases[i].reads &&
            (header_cases[i].reads
                 ? fault.empty() && staged_families.size() == 2 &&
                       staged_families[1].id == "staged.one"
                 : staged_families.empty() &&
                       fault.find(header_cases[i].fault_names) != std::string::npos);
        if (!as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                  << header_cases[i].what << "\n";
    }
    {
        std::ofstream staged(header_catalog.c_str());
        staged << "family id staged.first\n"
               << "topic_and_level first topic\n"
               << "rule eq.one fixture\n"
               << "family ix staged.second\n"
               << "topic_and_level second topic\n"
               << "rule eq.two fixture\n";
        staged.close();
        std::vector<Family> staged_families;
        std::string fault;
        const bool ok = read_catalog(header_catalog, &staged_families, &fault);
        const bool refused = !ok && staged_families.empty() &&
                             fault.find("family ix staged.second") != std::string::npos;
        if (!refused)
            ++failures;
        std::cout << "coverage selftest: " << (refused ? "ok   " : "FAIL ")
                  << "a malformed family header does not merge into the previous family\n";
    }
    {
        std::ofstream staged(header_catalog.c_str());
        staged << "family id staged.first\n"
               << "rule eq.divide-both-sides fixture\n"
               << "family ix staged.bad\nrule eq.divide-both-sides fixture\n";
        staged.close();
        const bool refused = coverage(header_catalog, fixtures_dir, refused_report, nullptr) != 0 &&
                             !std::filesystem::exists(refused_report);
        if (!refused)
            ++failures;
        std::cout << "coverage selftest: " << (refused ? "ok   " : "FAIL ")
                  << "the coverage report is not written over a malformed family header\n";
    }
    {
        // Before any family is read there is no block for the header to merge into, which is why
        // this one was skipped rather than refused.
        std::ofstream staged(header_catalog.c_str());
        staged << "family ix staged.first\n"
               << "topic_and_level first topic\n"
               << "rule eq.one fixture\n"
               << "family id staged.second\n"
               << "topic_and_level second topic\n"
               << "rule eq.two fixture\n";
        staged.close();
        std::vector<Family> staged_families;
        std::string fault;
        const bool ok = read_catalog(header_catalog, &staged_families, &fault);
        const bool refused = !ok && staged_families.empty() &&
                             fault.find("family ix staged.first") != std::string::npos;
        if (!refused)
            ++failures;
        std::cout << "coverage selftest: " << (refused ? "ok   " : "FAIL ")
                  << "a malformed first family header is refused rather than dropping its block\n";
    }
    {
        std::ofstream staged(header_catalog.c_str());
        staged << "A preamble line of prose.\n"
               << "Another that talks about a family without starting with the word.\n"
               << "family id staged.second\n"
               << "rule eq.two fixture\n";
        staged.close();
        std::vector<Family> staged_families;
        std::string fault;
        const bool read = read_catalog(header_catalog, &staged_families, &fault) &&
                          fault.empty() && staged_families.size() == 1 &&
                          staged_families[0].id == "staged.second" &&
                          staged_families[0].rules.size() == 1;
        if (!read)
            ++failures;
        std::cout << "coverage selftest: " << (read ? "ok   " : "FAIL ")
                  << "and a preamble of prose before the first header still reads\n";
    }
    for (const std::string answer : {"complete", "omitted", "none", ""}) {
        std::ofstream metadata(metadata_path);
        for (const std::string id : {"staged.complete", "staged.incomplete"}) {
            metadata << "family id " << id << "\n";
            for (const char *field : kSection27Fields) {
                if (std::string(field) == "id")
                    continue;
                if (id == "staged.complete" && std::string(field) == "rule_ids") {
                    metadata << "rule eq.divide-both-sides fixture\n";
                    continue;
                }
                if (id == "staged.incomplete" && std::string(field) == "strategy_ids" &&
                    answer != "complete") {
                    if (answer != "omitted")
                        metadata << field << " " << answer << "\n";
                    continue;
                }
                metadata << field << " none, this field is unused in the staged catalog\n";
            }
        }
        metadata.close();
        const int status = coverage(metadata_path, fixtures_dir, report_path, nullptr);
        std::ifstream generated(report_path);
        std::string report_text, line;
        while (std::getline(generated, line))
            report_text += line + "\n";
        const std::string expected =
            answer == "complete"
                ? "| staged.incomplete | 28 of 28 | - |"
                : "| staged.incomplete | 27 of 28 | strategy_ids" +
                      std::string(answer == "none" ? " (unanswered)" : "") + " |";
        const bool detected = status == 0 &&
                              report_text.find("| staged.complete | 28 of 28 | - |") !=
                                  std::string::npos &&
                              report_text.find(expected) != std::string::npos &&
                              report_text.find("28 of 28 section 27 field names occur in at least "
                                               "one family.") != std::string::npos;
        if (!detected)
            ++failures;
        std::cout << "coverage selftest: " << (detected ? "ok   " : "FAIL ")
                  << "per-family strategy_ids answer " << (answer.empty() ? "empty" : answer)
                  << " is distinguished from the complete schema union\n";
    }
    // The group join is staged, because the catalog in the tree can never show the check firing.
    const std::string group_catalog = made + "/group-join.md";
    const std::string group_evidence = made + "/group-evidence.txt";
    const std::string group_report = made + "/group-report.md";
    const struct {
        const char *group_line;
        const char *ran;
        int status;
        size_t uncatalogued;
        size_t awaiting;
        size_t stale;
        const char *what;
    } group_cases[] = {
        {"staged group", "staged group", 0, 0, 0, 0,
         "a group that ran and a catalogued family names is coverage the catalog describes"},
        {nullptr, "staged group", 1, 1, 0, 0,
         "the same group with its family block gone is an uncatalogued family"},
        {nullptr, "staged awaiting", 1, 0, 1, 0,
         "a group whose family is written down as not catalogued yet is counted and fails the run"},
        {"fuzz", "fuzz", 1, 0, 0, 1,
         "a group excused as having no family and named by one is a stale exemption"},
        {"staged awaiting", "staged awaiting", 1, 0, 0, 1,
         "and so is one excused as awaiting a block after the block arrives"},
        {"staged group", nullptr, 1, 0, 0, 0,
         "an evidence file with no group rows is refused rather than read as clean"},
        {nullptr, "", 0, 0, 0, 0,
         "and a run with no evidence file at all reports the join as one that did not run"},
    };
    // Staged, because a check that needs the real table non-empty dies the day it is emptied.
    const std::vector<AwaitingGroup> staged_awaiting = {
        {"staged awaiting", "src/physics/staged.cc, stamping no id of its own"}};
    const std::vector<FamilyWithNoEngine> staged_no_engine = {
        {"staged.exempt", "tests/staged.cc, a staged context"}};
    const std::vector<FamilyWithNoRun> staged_no_run_none = {};
    const std::vector<FamilyWithNoRun> staged_no_run_dormant = {
        {"staged.complete", "src/steps/staged.cc calls make_context nowhere"}};
    const std::vector<FamilyWithNoRun> staged_no_run_absent = {
        {"staged.absent", "src/steps/absent.cc calls make_context nowhere"}};
    for (size_t i = 0; i < sizeof(group_cases) / sizeof(group_cases[0]); ++i) {
        {
            std::ofstream staged(group_catalog.c_str());
            staged << "family id staged.complete\n";
            for (const char *field : kSection27Fields) {
                if (std::string(field) == "id")
                    continue;
                if (std::string(field) == "rule_ids") {
                    staged << "rule eq.divide-both-sides fixture\n";
                    continue;
                }
                staged << field << " none, this field is unused in the staged catalog\n";
            }
            if (group_cases[i].group_line != nullptr)
                staged << "test_group_ids " << group_cases[i].group_line << "\n";
        }
        // An empty group name is the run with no evidence file, where the join is skipped entirely.
        const bool joins = group_cases[i].ran == nullptr || group_cases[i].ran[0] != '\0';
        {
            std::ofstream staged(group_evidence.c_str());
            if (group_cases[i].ran != nullptr && group_cases[i].ran[0] != '\0') {
                staged << "group\tacceptance corpus\ngroup\t" << group_cases[i].ran << "\n";
                staged << "family\tstaged.complete\nfamily\tstaged.exempt\n";
            }
        }
        std::filesystem::remove(group_report);
        Outcome counted;
        const int status = coverage(group_catalog, fixtures_dir, group_report,
                                    joins ? group_evidence.c_str() : nullptr, &counted,
                                    staged_awaiting, staged_no_engine, staged_no_run_none);
        const bool wrote = std::filesystem::exists(group_report);
        const bool as_expected =
            status == group_cases[i].status && wrote == (group_cases[i].ran != nullptr) &&
            counted.join_ran == joins && counted.uncatalogued == group_cases[i].uncatalogued &&
            counted.awaiting_catalog == group_cases[i].awaiting &&
            counted.stale_exemptions == group_cases[i].stale;
        if (!as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                  << group_cases[i].what << "\n";
        // Seen by the counter and met by a reader of the report are separate claims.
        if (!wrote)
            continue;
        std::ifstream written(group_report.c_str());
        std::string report_text, report_line;
        while (std::getline(written, report_line))
            report_text += report_line + "\n";
        const bool wants_row = group_cases[i].awaiting > 0;
        const bool has_row =
            report_text.find("| staged awaiting | src/physics/staged.cc, stamping no id of its "
                             "own |") != std::string::npos;
        const bool says_none =
            report_text.find("No test group is awaiting a catalog block.") != std::string::npos;
        // A join that never ran is a third answer, not an empty one.
        const bool says_unanswered =
            report_text.find("this table is unanswered rather than empty") != std::string::npos;
        const bool row_as_expected = has_row == wants_row && says_none == (joins && !wants_row) &&
                                     says_unanswered == !joins;
        if (!row_as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (row_as_expected ? "ok   " : "FAIL ")
                  << (!joins ? "and a join that did not run leaves that table unanswered"
                             : wants_row
                                   ? "and that deferral is written into the report as its own row"
                                   : "and the report says outright that none is awaiting one")
                  << "\n";
    }
    // The family join, which answers the half a group join cannot.
    {
        std::ofstream staged(group_catalog.c_str());
        staged << "family id staged.complete\n";
        for (const char *field : kSection27Fields) {
            if (std::string(field) == "id")
                continue;
            if (std::string(field) == "rule_ids") {
                staged << "rule eq.divide-both-sides fixture\n";
                continue;
            }
            staged << field << " none, this field is unused in the staged catalog\n";
        }
        staged << "test_group_ids staged group\n";
    }
    const struct {
        const char *families;
        int status;
        size_t missing;
        bool census_ran;
        size_t stale;
        size_t unstamped;
        const std::vector<FamilyWithNoRun> *no_run;
        const char *what;
    } family_cases[] = {
        {"family\tstaged.complete\nfamily\tstaged.exempt\n", 0, 0, true, 0, 0, &staged_no_run_none,
         "a family the run stamps and the catalog describes is coverage the catalog knows about"},
        {"family\tstaged.complete\nfamily\tstaged.exempt\nfamily\tstaged.second\n", 1, 1, true, 0, 0,
         &staged_no_run_none,
         "a second family sharing that group is caught even though the group is already claimed"},
        {"family\tstaged.exempt\nfamily\tstaged.complete\n", 0, 0, true, 0, 0, &staged_no_run_none,
         "a family excused as stamped by no engine is not held against the catalog"},
        {"family\tstaged.complete\n", 1, 0, true, 1, 0, &staged_no_run_none,
         "and an excuse this run stamps nowhere is a stale exemption rather than a line nobody "
         "reads"},
        {"", 1, 0, false, 0, 0, &staged_no_run_none,
         "and an evidence file naming no family at all refuses rather than reporting none missing"},
        {"family\tstaged.exempt\n", 1, 0, true, 0, 1, &staged_no_run_none,
         "a catalogued family no run stamps fails, which is the converse the group join never "
         "asks"},
        {"family\tstaged.exempt\n", 0, 0, true, 0, 0, &staged_no_run_dormant,
         "and a written reason nothing stamps it keeps that family out of the count"},
        {"family\tstaged.complete\nfamily\tstaged.exempt\n", 1, 0, true, 1, 0,
         &staged_no_run_dormant,
         "and that reason goes stale the moment a run stamps it, which retires the excuse when "
         "the engine is fixed"},
        {"family\tstaged.complete\nfamily\tstaged.exempt\n", 1, 0, true, 1, 0, &staged_no_run_absent,
         "and a reason written for a family the catalog does not describe is stale as well"},
    };
    for (size_t i = 0; i < sizeof(family_cases) / sizeof(family_cases[0]); ++i) {
        {
            std::ofstream staged(group_evidence.c_str());
            staged << "group\tacceptance corpus\ngroup\tstaged group\n"
                   << family_cases[i].families;
        }
        std::filesystem::remove(group_report);
        Outcome counted;
        const int status = coverage(group_catalog, fixtures_dir, group_report,
                                    group_evidence.c_str(), &counted, staged_awaiting,
                                    staged_no_engine, *family_cases[i].no_run);
        const bool as_expected = status == family_cases[i].status &&
                                 counted.uncatalogued_families == family_cases[i].missing &&
                                 counted.family_census_ran == family_cases[i].census_ran &&
                                 counted.stale_exemptions == family_cases[i].stale &&
                                 counted.unstamped_families == family_cases[i].unstamped;
        if (!as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                  << family_cases[i].what << "\n";
    }
    // VER-010's join, on staged rows. Each fault is one row added to an otherwise clean file.
    {
        const std::string rule = nps::declared_rule(0).rule_id;
        const std::string other = nps::declared_rule(1).rule_id;
        const std::string clean =
            nps_tools::rule_case_row(rule, RuleCaseKind::Positive, true, "staged", "a") + "\n" +
            nps_tools::rule_case_row(rule, RuleCaseKind::Negative, true, "staged", "b") + "\n" +
            nps_tools::rule_case_row(rule, RuleCaseKind::Boundary, true, "staged", "c") + "\n" +
            nps_tools::rule_case_row(rule, RuleCaseKind::Regression, true, "staged", "#1") + "\n" +
            nps_tools::rule_case_row(other, RuleCaseKind::Positive, true, "staged", "d") + "\n" +
            "evidence\tVER-010\tpass\tstaged\tan evidence row is not a rule case\n";
        const size_t all_gaps = nps::declared_rule_count() * nps_tools::kRuleCaseKindCount;
        const struct {
            std::string extra;
            size_t faults;
            size_t complete;
            size_t gaps;
            const char *what;
        } join_cases[] = {
            {"", 0, 1, all_gaps - 5, "a clean file completes the rule with four passing kinds"},
            {"rule_case\tno.such.rule\tpositive\tpass\tstaged\te\n", 1, 1, all_gaps - 5,
             "a row naming no registered rule is a fault"},
            {"rule_case\t" + other + "\tbordering\tpass\tstaged\te\n", 1, 1, all_gaps - 5,
             "a row naming an unknown kind is a fault"},
            {nps_tools::rule_case_row(other, RuleCaseKind::Boundary, false, "staged", "e") + "\n", 1,
             1, all_gaps - 5, "a failed case is a fault and supplies no kind"},
            {"rule_case\t" + other + "\tboundary\tpass\tstaged\n", 1, 1, all_gaps - 5,
             "a row missing a field is a fault"},
            {nps_tools::rule_case_row(other, RuleCaseKind::Boundary, true, "staged", "e") + "\n", 0,
             1, all_gaps - 6, "a passing boundary case closes one gap"},
        };
        for (const auto &c : join_cases) {
            const std::string staged_evidence = std::string(made) + "/rule-cases.txt";
            std::ofstream staged(staged_evidence.c_str());
            staged << "group\tstaged\n" << clean << c.extra;
            staged.close();
            std::ostringstream staged_report;
            const RuleCaseJoin got = join_rule_cases(staged_evidence, {}, staged_report);
            const bool as_expected = got.faults == c.faults && got.complete == c.complete &&
                                     got.gaps == c.gaps;
            if (!as_expected)
                ++failures;
            std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ") << c.what
                      << "\n";
        }
        std::ostringstream missing_report;
        const RuleCaseJoin missing =
            join_rule_cases(std::string(made) + "/absent.txt", {}, missing_report);
        const bool absent_faults = missing.faults == 1 && missing.gaps == all_gaps;
        if (!absent_faults)
            ++failures;
        std::cout << "coverage selftest: " << (absent_faults ? "ok   " : "FAIL ")
                  << "an evidence file that cannot be read is a fault rather than an empty join\n";

        // The whole run passes only at the recorded gap count with no failed case.
        const size_t present = all_gaps - kRuleCaseGaps;
        const struct {
            size_t kinds;
            bool failed_row;
        } runs[] = {{present, false}, {present - 1, false}, {present + 1, false}, {present, true}};
        for (const auto &run : runs) {
            const std::string staged_evidence = std::string(made) + "/gap-count.txt";
            std::ofstream staged(staged_evidence.c_str());
            staged << "group\tacceptance corpus\nfamily\tstaged.complete\nfamily\tstaged.incomplete\n";
            for (size_t n = 0; n < run.kinds; ++n)
                staged << nps_tools::rule_case_row(
                              nps::declared_rule(n / nps_tools::kRuleCaseKindCount).rule_id,
                              static_cast<RuleCaseKind>(n % nps_tools::kRuleCaseKindCount), true,
                              "staged", "#1")
                       << "\n";
            if (run.failed_row)
                staged << nps_tools::rule_case_row(nps::declared_rule(0).rule_id,
                                                   RuleCaseKind::Boundary, false, "staged", "e")
                       << "\n";
            staged.close();
            const bool passed =
                coverage(metadata_path, fixtures_dir, report_path, staged_evidence.c_str(), nullptr,
                         {}, {}, {}, true) == 0;
            const bool as_expected = passed == (run.kinds == present && !run.failed_row);
            if (!as_expected)
                ++failures;
            std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                      << count_text(run.kinds) << " staged kinds "
                      << (run.kinds == present ? "match" : "move") << " the recorded gap count"
                      << (run.failed_row ? ", beside a failed case" : "") << "\n";
        }
    }

    // MATH-013 claims the required assumptions are declared, so a line the run faults cannot pass it.
    for (const bool malformed : {false, true}) {
        {
            std::ofstream staged(group_catalog.c_str());
            staged << "family id staged.complete\n";
            for (const char *field : kSection27Fields) {
                if (std::string(field) == "id")
                    continue;
                if (std::string(field) == "rule_ids") {
                    staged << "rule eq.divide-both-sides fixture\n";
                    continue;
                }
                if (malformed && std::string(field) == "required_assumptions") {
                    staged << "required_assumptions the solver assumes \"unclosed\n";
                    continue;
                }
                staged << field << " none, this field is unused in the staged catalog\n";
            }
            staged << "test_group_ids staged group\n";
        }
        {
            std::ofstream staged(group_evidence.c_str());
            staged << "group\tacceptance corpus\ngroup\tstaged group\n"
                   << "family\tstaged.complete\nfamily\tstaged.exempt\n";
        }
        std::filesystem::remove(group_report);
        const int status = coverage(group_catalog, fixtures_dir, group_report,
                                    group_evidence.c_str(), nullptr, staged_awaiting,
                                    staged_no_engine, staged_no_run_none);
        std::ifstream written(group_evidence.c_str());
        std::string verdict;
        std::string line;
        const std::string key = "evidence\tMATH-013\t";
        while (std::getline(written, line)) {
            if (line.rfind(key, 0) == 0)
                verdict = line.substr(key.size(), line.find('\t', key.size()) - key.size());
        }
        const bool as_expected = status == (malformed ? 1 : 0) &&
                                 verdict == (malformed ? "fail" : "pass");
        if (!as_expected)
            ++failures;
        std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                  << (malformed ? "a required_assumptions line the run faults writes MATH-013 as "
                                  "fail, saw "
                                : "a well formed envelope writes MATH-013 as pass, saw ")
                  << (verdict.empty() ? "no row" : verdict) << "\n";
    }
    std::cout << "coverage selftest: " << count_text(static_cast<size_t>(failures)) << " failed\n";
    return failures == 0 ? 0 : 1;
}

int coverage(const std::string &catalog_path, const std::string &fixtures_dir,
             const std::string &report_path, const char *evidence_path, Outcome *out,
             const std::vector<AwaitingGroup> &awaiting_table,
             const std::vector<FamilyWithNoEngine> &no_engine_table,
             const std::vector<FamilyWithNoRun> &no_run_table, bool rule_cases) {
    std::vector<Family> families;
    std::string fault;
    if (!read_catalog(catalog_path, &families, &fault) || families.empty()) {
        if (!fault.empty())
            std::cout << "coverage: " << catalog_path << ": " << fault << "\n";
        std::cout << "coverage: no families read from " << catalog_path << "\n";
        return 1;
    }

    std::map<std::string, std::set<std::string> > fixture_rules;
    std::map<std::string, std::set<std::string> > fixture_obligations;
    std::map<std::string, std::set<std::string> > fixture_assumptions;
    std::map<std::string, std::vector<std::string> > fixture_cases;
    size_t fixtures = 0;
    std::error_code ec;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(fixtures_dir, ec)) {
        if (!entry.is_regular_file())
            continue;
        std::string family;
        std::set<std::string> rules;
        std::set<std::string> obligations;
        std::set<std::string> carried;
        read_fixture(entry.path().string(), &family, &rules, &obligations, &carried);
        if (family.empty())
            continue;
        ++fixtures;
        fixture_cases[family].push_back(entry.path().filename().string());
        for (std::set<std::string>::const_iterator it = rules.begin(); it != rules.end(); ++it)
            fixture_rules[family].insert(*it);
        for (std::set<std::string>::const_iterator it = obligations.begin();
             it != obligations.end(); ++it)
            fixture_obligations[family].insert(*it);
        for (std::set<std::string>::const_iterator it = carried.begin(); it != carried.end(); ++it)
            fixture_assumptions[family].insert(*it);
    }
    if (ec || fixtures == 0) {
        std::cout << "coverage: no fixtures read from " << fixtures_dir << "\n";
        return 1;
    }

    size_t undeclared = 0;
    size_t unevidenced = 0;
    size_t uncatalogued = 0;
    size_t obligation_faults = 0;
    size_t schemaless = 0;
    size_t envelope_gaps = 0;
    size_t assumption_faults = 0;

    // VER-016's other half. The invariant pass checks a step against the schema its rule declares,
    // which can only reach a rule some derivation runs, and the catalog names rules that only a run
    // on the calculator produces. This is where those are held to the requirement: the catalog is
    // the inventory of what exists, so a rule in it with no table entry is a rule that declares
    // nothing, whether or not the host can execute it.
    for (size_t i = 0; i < families.size(); ++i) {
        for (size_t r = 0; r < families[i].rules.size(); ++r) {
            const std::string &id = families[i].rules[r].id;
            if (nps::rule_schema(id) != nullptr)
                continue;
            ++schemaless;
            std::cout << "coverage: the catalogued rule " << id
                      << " declares no proof-obligation schema\n";
        }
    }

    // A family that ships fixtures and has no catalog entry is coverage nobody described.
    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             fixture_cases.begin();
         it != fixture_cases.end(); ++it) {
        bool known = false;
        for (size_t i = 0; i < families.size(); ++i) {
            if (families[i].id == it->first)
                known = true;
        }
        if (!known) {
            ++uncatalogued;
            std::cout << "coverage: " << it->first << " has fixtures and no catalog entry\n";
        }
    }

    // The same question for a family no fixture reaches, because a fixture is optional and a group is not.
    size_t awaiting_catalog = 0;
    size_t stale_exemptions = 0;
    // Kept so the report carries a row per deferral, which used to reach stdout alone.
    std::vector<const AwaitingGroup *> awaiting_rows;
    size_t uncatalogued_families = 0;
    size_t unstamped_families = 0;
    bool family_census_ran = false;
    const bool join_ran = evidence_path != nullptr;
    if (join_ran) {
        std::set<std::string> groups_run;
        read_groups_run(evidence_path, &groups_run);
        if (groups_run.empty()) {
            std::cout << "coverage: no test groups read from " << evidence_path
                      << ", run the unit tests first\n";
            if (out != nullptr)
                out->join_ran = true;
            return 1;
        }
        std::set<std::string> claimed;
        for (size_t i = 0; i < families.size(); ++i) {
            for (size_t g = 0; g < families[i].groups.size(); ++g)
                claimed.insert(families[i].groups[g]);
        }
        // An excuse the catalog has outgrown shadows a live family, so it is a fault of its own.
        for (const auto &entry : kGroupsWithNoFamily) {
            if (!claimed.count(entry.group))
                continue;
            ++stale_exemptions;
            std::cout << "coverage: the test group " << entry.group
                      << " is excused as belonging to no family and a catalogued family names it\n";
        }
        for (const auto &entry : awaiting_table) {
            if (!claimed.count(entry.group))
                continue;
            ++stale_exemptions;
            std::cout << "coverage: the test group " << entry.group
                      << " is excused as awaiting a catalog block and a catalogued family names "
                         "it\n";
        }
        for (const std::string &group : groups_run) {
            if (claimed.count(group))
                continue;
            bool excused = false;
            for (const auto &entry : kGroupsWithNoFamily) {
                if (group == entry.group)
                    excused = true;
            }
            if (excused)
                continue;
            const AwaitingGroup *awaiting = nullptr;
            for (const auto &entry : awaiting_table) {
                if (group == entry.group)
                    awaiting = &entry;
            }
            if (awaiting != nullptr) {
                ++awaiting_catalog;
                awaiting_rows.push_back(awaiting);
                std::cout << "coverage: the test group " << group << " exercises "
                          << awaiting->engine << ", and no catalog block describes it yet\n";
                continue;
            }
            ++uncatalogued;
            std::cout << "coverage: the test group " << group
                      << " ran and no catalogued family names it\n";
        }

        // The family-granular half, because a group join cannot see a second family under a claimed group.
        std::set<std::string> stamped;
        read_families_stamped(evidence_path, &stamped);
        family_census_ran = !stamped.empty();
        if (!family_census_ran) {
            std::cout << "coverage: no stamped families read from " << evidence_path
                      << ", so the family join did not run, which is not the same as nothing "
                         "missing\n";
            if (out != nullptr) {
                out->join_ran = true;
                out->stale_exemptions = stale_exemptions;
            }
            return 1;
        }
        std::set<std::string> catalogued;
        for (size_t i = 0; i < families.size(); ++i)
            catalogued.insert(families[i].id);
        // An excuse goes stale from either side, so both are checked rather than the obvious one.
        for (const auto &entry : no_engine_table) {
            if (catalogued.count(entry.family)) {
                ++stale_exemptions;
                std::cout << "coverage: the family " << entry.family
                          << " is excused as stamped by no engine and a catalog block describes "
                             "it\n";
                continue;
            }
            if (stamped.count(entry.family))
                continue;
            ++stale_exemptions;
            std::cout << "coverage: the family " << entry.family
                      << " is excused as stamped by " << entry.what
                      << " and this run stamps it nowhere\n";
        }
        for (const std::string &family : stamped) {
            if (catalogued.count(family))
                continue;
            bool excused = false;
            for (const auto &entry : no_engine_table) {
                if (family == entry.family)
                    excused = true;
            }
            if (excused)
                continue;
            ++uncatalogued_families;
            std::cout << "coverage: the family " << family
                      << " is stamped by a run and no catalog block describes it\n";
        }
        for (const auto &entry : no_run_table) {
            if (!catalogued.count(entry.family)) {
                ++stale_exemptions;
                std::cout << "coverage: the family " << entry.family
                          << " is excused as stamped by no run and no catalog block describes "
                             "it\n";
                continue;
            }
            if (!stamped.count(entry.family))
                continue;
            ++stale_exemptions;
            std::cout << "coverage: the family " << entry.family << " is excused because "
                      << entry.why << " and this run stamps it\n";
        }
        for (size_t i = 0; i < families.size(); ++i) {
            const std::string &family = families[i].id;
            if (stamped.count(family))
                continue;
            bool excused = false;
            for (const auto &entry : no_run_table) {
                if (family == entry.family)
                    excused = true;
            }
            if (excused)
                continue;
            ++unstamped_families;
            std::cout << "coverage: the catalog describes the family " << family
                      << " and no run stamps it\n";
        }
    }

    std::ofstream report(report_path.c_str());
    if (!report) {
        std::cout << "coverage: could not write " << report_path << "\n";
        return 1;
    }
    report << "# Coverage\n\n";
    report << "Generated by tools/coverage.cc from catalog/families.md and the golden fixtures. Do "
              "not edit.\n\n";
    report << "| Family | Rules claimed | Covered by a fixture | Fixtures |\n|---|---:|---:|---:|\n";

    std::set<std::string> carried_fields;
    for (size_t i = 0; i < families.size(); ++i) {
        const Family &f = families[i];
        for (std::set<std::string>::const_iterator it = f.fields.begin(); it != f.fields.end(); ++it)
            carried_fields.insert(*it);
        const std::set<std::string> &seen = fixture_rules[f.id];
        size_t covered = 0;
        for (size_t r = 0; r < f.rules.size(); ++r) {
            const bool in_fixture = seen.count(f.rules[r].id) > 0;
            if (in_fixture)
                ++covered;
            // A rule claimed as fixture evidence and absent from every fixture is a claim with
            // nothing behind it. One declared as device evidence is a gap that is written down.
            if (f.rules[r].evidence == "fixture" && !in_fixture) {
                ++unevidenced;
                std::cout << "coverage: " << f.id << " claims " << f.rules[r].id
                          << " as fixture evidence and no fixture records it\n";
            }
        }
        // A rule the corpus runs and the catalog does not name is coverage the catalog does not know
        // it has, which is the half of the join that keeps the catalog honest as the code moves.
        for (std::set<std::string>::const_iterator it = seen.begin(); it != seen.end(); ++it) {
            bool claimed = false;
            for (size_t r = 0; r < f.rules.size(); ++r) {
                if (f.rules[r].id == *it)
                    claimed = true;
            }
            if (!claimed) {
                ++undeclared;
                std::cout << "coverage: " << f.id << " runs " << *it
                          << " and the catalog does not name it\n";
            }
        }
        // Section 19.9 wants proof-obligation coverage on its own, so the same join runs over the
        // obligations and is reported in its own table rather than folded into the rule counts.
        const std::set<std::string> &raised = fixture_obligations[f.id];
        for (size_t o = 0; o < f.obligations.size(); ++o) {
            if (raised.count(f.obligations[o]))
                continue;
            ++obligation_faults;
            std::cout << "coverage: " << f.id << " names obligation " << f.obligations[o]
                      << " and no fixture raises it\n";
        }
        for (std::set<std::string>::const_iterator it = raised.begin(); it != raised.end(); ++it) {
            bool claimed = false;
            for (size_t o = 0; o < f.obligations.size(); ++o) {
                if (f.obligations[o] == *it)
                    claimed = true;
            }
            if (!claimed) {
                ++obligation_faults;
                std::cout << "coverage: " << f.id << " raises obligation " << *it
                          << " and the catalog does not name it\n";
            }
        }
        report << "| " << f.id << " | " << count_text(f.rules.size()) << " | "
               << count_text(covered) << " | " << count_text(fixture_cases[f.id].size()) << " |\n";
    }

    report << "\n## Rules\n\n| Family | Rule | Evidence | In a fixture |\n|---|---|---|---|\n";
    for (size_t i = 0; i < families.size(); ++i) {
        const Family &f = families[i];
        const std::set<std::string> &seen = fixture_rules[f.id];
        for (size_t r = 0; r < f.rules.size(); ++r) {
            report << "| " << f.id << " | " << f.rules[r].id << " | " << f.rules[r].evidence << " | "
                   << (seen.count(f.rules[r].id) ? "yes" : "no") << " |\n";
        }
    }

    report << "\n## Proof obligations\n\n";
    report << "Reported here rather than with the rules, because section 19.9 asks for this coverage "
              "separately from rule and corpus coverage.\n\n";
    report << "| Family | Obligation | Raised by a fixture |\n|---|---|---|\n";
    size_t obligations_total = 0;
    size_t obligations_raised = 0;
    for (size_t i = 0; i < families.size(); ++i) {
        const Family &f = families[i];
        for (size_t o = 0; o < f.obligations.size(); ++o) {
            const bool raised = fixture_obligations[f.id].count(f.obligations[o]) > 0;
            ++obligations_total;
            if (raised)
                ++obligations_raised;
            report << "| " << f.id << " | " << f.obligations[o] << " | " << (raised ? "yes" : "no")
                   << " |\n";
        }
    }

    // A family with no obligations just vanishes from the table, which reads the same as one nobody
    // wrote a line for. Saying it outright is the difference between an answer and an absence.
    std::string without;
    for (size_t i = 0; i < families.size(); ++i) {
        if (!families[i].obligations.empty())
            continue;
        if (!without.empty())
            without += ", ";
        without += families[i].id;
    }
    if (!without.empty()) {
        report << "\nFamilies naming no obligation: " << without
               << ". No fixture raises one for them either, which the join above would have failed "
                  "on.\n";
    }

    report << "\n## Proof-obligation schemas declared\n\n";
    report << "VER-016 asks every rule and strategy to declare one. The invariant pass checks a step "
              "against the schema its rule declares, which only reaches a rule some derivation "
              "runs, so this is where the rules marked device are held to it: they are declared and "
              "never exercised, and the two readings are different claims.\n\n";
    size_t catalogued_rules = 0;
    for (size_t i = 0; i < families.size(); ++i)
        catalogued_rules += families[i].rules.size();
    report << count_text(catalogued_rules - schemaless) << " of " << count_text(catalogued_rules)
           << " catalogued rules declare a proof-obligation schema.\n";

    report << "\n## Symbolic envelope, per family\n\n";
    report << "MATH-013 asks each supported family to declare its own envelope, so this is counted "
              "per family rather than over the catalog. A field one family carries says nothing "
              "about the family beside it.\n\n";
    report << "| Family | Envelope fields declared | Absent |\n|---|---:|---|\n";
    size_t complete_families = 0;
    const size_t envelope_count = sizeof(kEnvelopeFields) / sizeof(kEnvelopeFields[0]);
    for (size_t i = 0; i < families.size(); ++i) {
        const Family &f = families[i];
        std::string missing;
        size_t declared = 0;
        for (size_t e = 0; e < envelope_count; ++e) {
            // Answered rather than present, since a field carrying a bare none states nothing and the
            // catalog's own rule is that a none says why.
            if (f.answered.count(kEnvelopeFields[e])) {
                ++declared;
                continue;
            }
            const bool dodged = f.fields.count(kEnvelopeFields[e]) != 0;
            ++envelope_gaps;
            if (!missing.empty())
                missing += "; ";
            missing += kEnvelopeFields[e];
            std::cout << "coverage: " << f.id << (dodged ? " answers " : " does not declare ")
                      << kEnvelopeFields[e] << (dodged ? " with a none that says nothing" : "")
                      << "\n";
        }
        if (missing.empty())
            ++complete_families;
        report << "| " << f.id << " | " << count_text(declared) << " of "
               << count_text(envelope_count) << " | " << (missing.empty() ? std::string("-") : missing)
               << " |\n";
    }
    report << "\n" << count_text(complete_families) << " of " << count_text(families.size())
           << " families declare all " << count_text(envelope_count) << " fields.\n";

    report << "\n## Required assumptions, joined\n\n";
    report << "A required_assumptions line is prose, so the join is over the engine strings it "
              "quotes. Each quoted string has to be recorded by a fixture of that family, in the "
              "channels that carry an assumption rather than as a domain restriction, which is a "
              "condition one step met along the way. A line that declares none has to have nothing "
              "carried against it, and a line that declares an assumption has to quote one, or the "
              "sentence is present without being true.\n\n";
    report << "| Family | Quoted | Carried by fixtures | Unjoined |\n|---|---:|---:|---|\n";
    for (size_t i = 0; i < families.size(); ++i) {
        const Family &f = families[i];
        const std::set<std::string> &carried = fixture_assumptions[f.id];
        std::string unjoined;
        if (f.assumptions_malformed) {
            ++assumption_faults;
            unjoined = "the line has an unclosed or empty quotation";
            std::cout << "coverage: " << f.id << " has a malformed required_assumptions line\n";
        }
        for (size_t q = 0; q < f.assumption_quotes.size(); ++q) {
            if (carried.count(f.assumption_quotes[q]))
                continue;
            ++assumption_faults;
            if (!unjoined.empty())
                unjoined += "; ";
            unjoined += f.assumption_quotes[q];
            std::cout << "coverage: " << f.id << " quotes the assumption " << f.assumption_quotes[q]
                      << " and no fixture carries it\n";
        }
        // The other direction. A family carrying an assumption while its line says none has outgrown
        // the line, and one claiming an assumption while quoting nothing cannot be joined at all.
        if (f.assumptions_none && !carried.empty()) {
            ++assumption_faults;
            if (!unjoined.empty())
                unjoined += "; ";
            unjoined += "declares none and carries " + count_text(carried.size());
            std::cout << "coverage: " << f.id << " declares no required assumption and its fixtures "
                      << "carry " << count_text(carried.size()) << "\n";
        }
        if (!f.assumptions_none && f.fields.count("required_assumptions") &&
            f.assumption_quotes.empty()) {
            ++assumption_faults;
            if (!unjoined.empty())
                unjoined += "; ";
            unjoined += "declares an assumption and quotes none";
            std::cout << "coverage: " << f.id << " declares a required assumption and quotes no "
                      << "engine string, so nothing joins it to what runs\n";
        }
        report << "| " << f.id << " | " << count_text(f.assumption_quotes.size()) << " | "
               << count_text(carried.size()) << " | " << (unjoined.empty() ? std::string("-") : unjoined)
               << " |\n";
    }

    report << "\n## Section 27 metadata, per family\n\n";
    report << "Each family must answer every required field. A bare none is unanswered. "
              "Metadata completeness records answers, including an explicit unqualified status. "
              "It does not establish release acceptance or validate those answers.\n\n";
    report << "| Family | Metadata fields answered | Missing or unanswered |\n|---|---:|---|\n";
    const size_t field_count = sizeof(kSection27Fields) / sizeof(kSection27Fields[0]);
    size_t metadata_complete = 0;
    size_t metadata_gaps = 0;
    for (const Family &f : families) {
        size_t answered = 0;
        std::string missing;
        for (const char *field : kSection27Fields) {
            const bool has_answer = f.answered.count(field) != 0 ||
                                    (std::string(field) == "id" && !f.id.empty()) ||
                                    (std::string(field) == "rule_ids" && !f.rules.empty());
            if (has_answer) {
                ++answered;
                continue;
            }
            ++metadata_gaps;
            if (!missing.empty())
                missing += ", ";
            missing += field;
            if (f.fields.count(field))
                missing += " (unanswered)";
        }
        if (missing.empty())
            ++metadata_complete;
        report << "| " << f.id << " | " << count_text(answered) << " of "
               << count_text(field_count) << " | "
               << (missing.empty() ? std::string("-") : missing) << " |\n";
    }
    report << "\n" << count_text(metadata_complete) << " of " << count_text(families.size())
           << " families answer every section 27 field, " << count_text(metadata_gaps)
           << " missing or unanswered fields. These are reported release gaps and do not change "
              "the rule, envelope or evidence checks.\n";

    report << "\n## Section 27 schema inventory across the catalog\n\n";
    report << "This union counts a field present in any family. It does not measure per-family "
              "completeness or whether the field is answered.\n\n";
    size_t absent = 0;
    for (size_t i = 0; i < field_count; ++i) {
        if (carried_fields.count(kSection27Fields[i]))
            continue;
        ++absent;
        report << "- " << kSection27Fields[i] << "\n";
    }
    if (absent == 0)
        report << "No field names are absent from the catalog-wide union.\n";
    report << "\n" << count_text(field_count - absent) << " of " << count_text(field_count)
           << " section 27 field names occur in at least one family.\n";

    report << "\n## Test groups awaiting a catalog block\n\n";
    report << "A group here ran and no catalogued family names it, and the tool is carrying a "
              "written exemption for it rather than failing. The exemption is the deferral, so it "
              "belongs in the generated report where a reader of the coverage claim meets it, "
              "rather than only in the run output.\n\n";
    if (!join_ran) {
        report << "The test group join did not run, so this table is unanswered rather than "
                  "empty.\n";
    } else if (awaiting_rows.empty()) {
        report << "No test group is awaiting a catalog block.\n";
        report << "\nThe family census is the other half of this question, and it "
               << (!join_ran ? "did not run either"
                             : family_census_ran
                                   ? "reported " + count_text(uncatalogued_families) +
                                         " stamped families with no catalog block and " +
                                         count_text(unstamped_families) +
                                         " catalogued families no run stamps"
                                   : "did not run, so no family was checked")
               << ".\n";
    } else {
        report << "| Test group | What it exercises |\n|---|---|\n";
        for (const AwaitingGroup *row : awaiting_rows)
            report << "| " << row->group << " | " << row->engine << " |\n";
        report << "\n" << count_text(awaiting_rows.size())
               << " test groups are awaiting a catalog block, and that is "
               << (kAwaitingCatalogIsFatal ? "a failure of this run" : "reported without failing "
                                                                      "this run")
               << ".\n";
    }

    // The rule case rows come from the evidence file, so a run that was not handed one says so.
    RuleCaseJoin cases;
    bool gap_count_moved = false;
    if (rule_cases && evidence_path != nullptr) {
        cases = join_rule_cases(evidence_path, families, report);
        gap_count_moved = cases.gaps != kRuleCaseGaps;
        if (gap_count_moved)
            std::cout << "coverage: " << count_text(cases.gaps)
                      << " rule and kind pairs have no case where kRuleCaseGaps records "
                      << count_text(kRuleCaseGaps) << ", so update it with the cases that moved it\n";
    } else {
        report << "\n## Rule case kinds\n\nThis run was not asked to join VER-010's rule cases.\n";
    }
    report.close();

    // An append that was asked for and did not happen leaves the requirement looking unclaimed for a
    // plumbing reason, so it fails here rather than being read off the report as an absence.
    bool evidence_refused = false;
    if (evidence_path != nullptr) {
        const std::string row =
            std::string("evidence\tMATH-013\t") +
            (envelope_gaps == 0 && assumption_faults == 0 ? "pass" : "fail") +
            "\tcoverage\tevery one of " + count_text(families.size()) +
            " catalogued families declares its own accepted expression grammar, domains and "
            "parameter assumptions, required assumptions, exact and special-function and numerical "
            "result policy, supported branches and degenerate cases, unsupported near neighbors, "
            "supported methods and proof obligations, and a family whose answer to one of them is "
            "nothing says none rather than leaving the line out";
        std::string error;
        // The catalog is where a family declares its envelope, so MATH-013 is answered here, after
        // the corpus group that is the last one written before this report reads the file.
        if (!append_evidence(evidence_path, "acceptance corpus", "coverage",
                             std::vector<std::string>(1, row), &error)) {
            evidence_refused = true;
            std::cout << "coverage: evidence not written, " << error << "\n";
        }
    }

    std::cout << "coverage: " << count_text(families.size()) << " families, "
              << count_text(fixtures) << " fixtures, " << count_text(obligations_raised) << " of "
              << count_text(obligations_total) << " proof obligations raised, "
              << count_text(field_count - absent) << " of " << count_text(field_count)
              << " section 27 field names in the catalog-wide union, "
              << count_text(metadata_complete) << " of " << count_text(families.size())
              << " families with complete section 27 metadata, " << count_text(metadata_gaps)
              << " missing or unanswered metadata fields, " << count_text(complete_families) << " of "
              << count_text(families.size()) << " families declaring a full envelope, "
              << count_text(assumption_faults) << " assumption faults, "
              << count_text(undeclared) << " undeclared rules, "
              << count_text(unevidenced) << " unevidenced claims, " << count_text(obligation_faults)
              << " obligation faults, " << count_text(uncatalogued)
              << " uncatalogued families, "
              << (join_ran ? count_text(awaiting_catalog) + " test groups awaiting a catalog block, " +
                                 count_text(stale_exemptions) + " stale group exemptions, " +
                                 count_text(uncatalogued_families) +
                                 " stamped families with no catalog block, " +
                                 count_text(unstamped_families) +
                                 " catalogued families no run stamps, "
                           : std::string("the test group join did not run with no evidence file, "))
              << count_text(schemaless)
              << " rules with no proof-obligation schema, " << count_text(cases.complete)
              << " of " << count_text(nps::declared_rule_count())
              << " registered rules with all four case kinds, " << count_text(cases.gaps)
              << " missing case kinds, " << count_text(cases.faults)
              << " rule case faults, report in " << report_path << "\n";
    if (out != nullptr) {
        out->join_ran = join_ran;
        out->uncatalogued = uncatalogued;
        out->awaiting_catalog = awaiting_catalog;
        out->stale_exemptions = stale_exemptions;
        out->family_census_ran = family_census_ran;
        out->uncatalogued_families = uncatalogued_families;
        out->unstamped_families = unstamped_families;
    }
    return undeclared == 0 && unevidenced == 0 && uncatalogued == 0 && obligation_faults == 0 &&
                   schemaless == 0 && envelope_gaps == 0 && assumption_faults == 0 &&
                   stale_exemptions == 0 && (!kAwaitingCatalogIsFatal || awaiting_catalog == 0) &&
                   uncatalogued_families == 0 && unstamped_families == 0 && cases.faults == 0 &&
                   !gap_count_moved && !evidence_refused
               ? 0
               : 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest")
        return selftest();
    if (argc < 4) {
        std::cout << "usage: nps_coverage <catalog.md> <fixtures dir> <report.md>\n"
                     "       nps_coverage --selftest\n";
        return 2;
    }
    return coverage(argv[1], argv[2], argv[3], getenv("NPS_EVIDENCE"), nullptr,
                    kGroupsAwaitingCatalog, kFamiliesWithNoEngine, kFamiliesNoRunStamps, true);
}
