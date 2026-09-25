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

// VER-010 asks for four kinds of case per rule, and this many registered rule and kind pairs have
// none today. A gap closed or opened has to move this number in the same change, so it only shrinks.
constexpr size_t kRuleCaseGaps = 724;

struct RuleCaseJoin {
    size_t rows = 0;
    size_t faults = 0;
    size_t complete = 0;
    size_t gaps = 0;
    size_t per_kind[nps_tools::kRuleCaseKindCount] = {};
};

// Every registered rule against the rule case rows the test runs wrote. A row that names no
// registered rule, an unknown kind or a failed case is a fault, and a missing kind is a gap.
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
             const std::string &report_path, const char *evidence_path);

// Staged catalogs test reader conventions and reporting without changing the release catalog.
int selftest() {
    char pattern[] = "/tmp/nps_coverage_XXXXXX";
    const char *made = ::mkdtemp(pattern);
    if (made == nullptr) {
        std::cout << "coverage selftest: no temporary directory\n";
        return 1;
    }
    const std::string path = std::string(made) + "/families.md";
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
    const std::string metadata_path = std::string(made) + "/metadata.md";
    const std::string fixtures_dir = std::string(made) + "/fixtures";
    const std::string report_path = std::string(made) + "/report.md";
    std::filesystem::create_directory(fixtures_dir);
    std::ofstream fixture(fixtures_dir + "/metadata.txt");
    fixture << "problem family: staged.complete\nrule: eq.divide-both-sides, staged rule\n";
    fixture.close();

    // The evidence word on a rule line names a closed set, so an unknown one is a typo that took the
    // device path at every consumer and removed the rule from the join with nothing said about it.
    const std::string evidence_catalog = std::string(made) + "/evidence-class.md";
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
    const std::string refused_report = std::string(made) + "/refused.md";
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
    const std::string header_catalog = std::string(made) + "/family-header.md";
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

        // The whole run passes at the recorded gap count and fails one rule either side of it, or
        // at that count with one failed case beside it.
        const size_t full_rules = (all_gaps - kRuleCaseGaps) / nps_tools::kRuleCaseKindCount;
        const struct {
            size_t rules;
            bool failed_row;
        } runs[] = {{full_rules, false}, {full_rules - 1, false}, {full_rules + 1, false},
                    {full_rules, true}};
        for (const auto &run : runs) {
            const size_t rules = run.rules;
            const std::string staged_evidence = std::string(made) + "/gap-count.txt";
            std::ofstream staged(staged_evidence.c_str());
            staged << "group\tacceptance corpus\n";
            for (size_t r = 0; r < rules; ++r) {
                for (size_t k = 0; k < nps_tools::kRuleCaseKindCount; ++k)
                    staged << nps_tools::rule_case_row(nps::declared_rule(r).rule_id,
                                                       static_cast<RuleCaseKind>(k), true, "staged",
                                                       "#1")
                           << "\n";
            }
            if (run.failed_row)
                staged << nps_tools::rule_case_row(nps::declared_rule(0).rule_id,
                                                   RuleCaseKind::Boundary, false, "staged", "e")
                       << "\n";
            staged.close();
            const bool passed = coverage(metadata_path, fixtures_dir, report_path,
                                         staged_evidence.c_str()) == 0;
            const bool as_expected = passed == (rules == full_rules && !run.failed_row);
            if (!as_expected)
                ++failures;
            std::cout << "coverage selftest: " << (as_expected ? "ok   " : "FAIL ")
                      << count_text(rules) << " complete rules "
                      << (rules == full_rules ? "match" : "move") << " the recorded gap count"
                      << (run.failed_row ? ", beside a failed case" : "") << "\n";
        }
    }

    std::cout << "coverage selftest: " << count_text(static_cast<size_t>(failures)) << " failed\n";
    return failures == 0 ? 0 : 1;
}

int coverage(const std::string &catalog_path, const std::string &fixtures_dir,
             const std::string &report_path, const char *evidence_path) {
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

    // The rule case rows come from the evidence file, so a run that was not handed one says so.
    RuleCaseJoin cases;
    bool gap_count_moved = false;
    if (evidence_path != nullptr) {
        cases = join_rule_cases(evidence_path, families, report);
        gap_count_moved = cases.gaps != kRuleCaseGaps;
        if (gap_count_moved)
            std::cout << "coverage: " << count_text(cases.gaps)
                      << " rule and kind pairs have no case where kRuleCaseGaps records "
                      << count_text(kRuleCaseGaps) << ", so update it with the cases that moved it\n";
    } else {
        report << "\n## Rule case kinds\n\nNo evidence file was named, so VER-010 was not "
                  "joined in this run.\n";
    }
    report.close();

    // An append that was asked for and did not happen leaves the requirement looking unclaimed for a
    // plumbing reason, so it fails here rather than being read off the report as an absence.
    bool evidence_refused = false;
    if (evidence_path != nullptr) {
        const std::string row =
            std::string("evidence\tMATH-013\t") + (envelope_gaps == 0 ? "pass" : "fail") +
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
              << " uncatalogued families, " << count_text(schemaless)
              << " rules with no proof-obligation schema, " << count_text(cases.complete)
              << " of " << count_text(nps::declared_rule_count())
              << " registered rules with all four case kinds, " << count_text(cases.gaps)
              << " missing case kinds, " << count_text(cases.faults)
              << " rule case faults, report in " << report_path << "\n";
    return undeclared == 0 && unevidenced == 0 && uncatalogued == 0 && obligation_faults == 0 &&
                   schemaless == 0 && envelope_gaps == 0 && assumption_faults == 0 &&
                   cases.faults == 0 && !gap_count_moved && !evidence_refused
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
    return coverage(argv[1], argv[2], argv[3], getenv("NPS_EVIDENCE"));
}
