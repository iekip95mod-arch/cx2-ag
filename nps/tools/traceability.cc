// PRD section 19.9 and MVP criterion 15. Every requirement in the PRD, joined against the evidence a
// test run wrote, so the answer to "is this requirement met" comes from a check that passed rather
// than from a list somebody kept by hand. Section 19.9 rejects the hand-kept list by name.
//
// Reads the PRD and the evidence file, writes the report, and refuses two things: evidence naming a
// requirement the PRD does not have, and evidence from a check that failed.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "catalog.h"
#include "evidence.h"

using nps_tools::count_text;
using nps_tools::Family;
using nps_tools::is_requirement_id;
using nps_tools::read_catalog;
using nps_tools::trimmed;

namespace {

struct Requirement {
    std::string id;
    std::string priority;
    std::string text;
};

struct Evidence {
    std::string requirement;
    std::string group;
    std::string what;
    bool passed = false;
};

struct UniversalCoverage {
    std::string domain;
    std::set<std::string> applicable_families;
    std::set<std::string> missing_families;
    bool malformed = false;
};

enum class UniversalScope {
    NotUniversal,
    Valid,
    Malformed,
};

std::vector<std::string> split(const std::string &line, char on) {
    std::vector<std::string> fields;
    std::string current;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == on) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(line[i]);
    }
    fields.push_back(current);
    return fields;
}

// The PRD prioritises most requirements and stages the word-problem ones instead, so the second
// column is P0 to P2 or INV in most tables and WP1 to WP3 in section 24's. Both are requirements and
// both belong in the report; only the first kind is what MVP criterion 15 gates on.
bool is_priority(const std::string &s) {
    return s == "P0" || s == "P1" || s == "P2" || s == "INV";
}

bool is_stage(const std::string &s) { return s == "WP1" || s == "WP2" || s == "WP3"; }

bool is_mvp_requirement(const std::string &s) { return s == "P0" || s == "INV"; }

// The PRD's requirement tables are markdown rows of id, priority and text. Any other table has a
// different shape in one of those three columns and is skipped by that rather than by its heading,
// so a table added later needs no change here.
bool read_requirements(const std::string &path, std::vector<Requirement> *out,
                       std::string *repeated) {
    std::ifstream in(path.c_str());
    if (!in)
        return false;
    std::set<std::string> seen;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != '|')
            continue;
        const std::vector<std::string> fields = split(line, '|');
        if (fields.size() < 4)
            continue;
        Requirement r;
        r.id = trimmed(fields[1]);
        r.priority = trimmed(fields[2]);
        if (!is_requirement_id(r.id) || (!is_priority(r.priority) && !is_stage(r.priority)))
            continue;
        r.text = trimmed(fields[3]);
        // Listed twice it would be counted twice in every total, and which row to believe is a guess.
        if (!seen.insert(r.id).second) {
            *repeated = r.id;
            return false;
        }
        out->push_back(r);
    }
    return true;
}

// The test run writes two kinds of line: every group it ran, and every check that named a
// requirement. Both are needed, because a group with no tagged check and a group that does not
// exist look the same from the evidence alone.
bool read_evidence(const std::string &path, std::vector<Evidence> *out,
                   std::set<std::string> *groups_run) {
    std::ifstream in(path.c_str());
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() == 2 && fields[0] == "group") {
            groups_run->insert(trimmed(fields[1]));
            continue;
        }
        if (fields.size() < 5 || fields[0] != "evidence")
            continue;
        Evidence e;
        e.requirement = fields[1];
        e.passed = fields[2] == "pass";
        e.group = fields[3];
        e.what = trimmed(fields[4]);
        out->push_back(e);
    }
    return true;
}

// A requirement is evidenced by a check that passed. A failing check names the requirement and
// proves nothing about it, so counting it would report the opposite of what happened.
bool evidenced_by(const std::map<std::string, std::vector<Evidence> > &by_requirement,
                   const std::string &id) {
    std::map<std::string, std::vector<Evidence> >::const_iterator found = by_requirement.find(id);
    if (found == by_requirement.end())
        return false;
    for (size_t i = 0; i < found->second.size(); ++i) {
        if (found->second[i].passed)
            return true;
    }
    return false;
}

std::string grammar_token(const std::string &token) {
    const std::ctype<char> &characters =
        std::use_facet<std::ctype<char> >(std::locale::classic());
    size_t first = 0;
    while (first < token.size() && characters.is(std::ctype_base::punct, token[first]))
        ++first;
    size_t last = token.size();
    while (last > first && characters.is(std::ctype_base::punct, token[last - 1]))
        --last;
    return token.substr(first, last - first);
}

UniversalScope universal_domain(const Requirement &requirement, std::string *domain) {
    std::istringstream input(requirement.text);
    input.imbue(std::locale::classic());
    std::vector<std::string> words;
    std::string word;
    while (input >> word) {
        const std::string normalized = grammar_token(word);
        if (!normalized.empty())
            words.push_back(normalized);
    }
    if (words.empty() || words[0] != "Every")
        return UniversalScope::NotUniversal;
    bool attempted_scope = words.size() > 2 && words[2] == "module";
    attempted_scope = attempted_scope ||
                      (words.size() > 2 && words[1] == "module" && words[2].starts_with("shal"));
    bool saw_shall = false;
    for (size_t i = 1; i < words.size() && !attempted_scope; ++i) {
        if (words[i] == "shall")
            saw_shall = true;
        if (i >= 3 && words[i] == "module" && !saw_shall) {
            attempted_scope = true;
            break;
        }
    }
    if (!attempted_scope)
        return UniversalScope::NotUniversal;
    if (words.size() < 4 || words[2] != "module" || words[3] != "shall")
        return UniversalScope::Malformed;
    const std::string &candidate = words[1];
    if (candidate.empty() ||
        candidate.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
        return UniversalScope::Malformed;
    *domain = candidate;
    return UniversalScope::Valid;
}

}  // namespace

// On a staged PRD, since the one in the tree lists each requirement once and a refusal that has
// never fired is indistinguishable from no refusal.
int selftest() {
    char pattern[] = "/tmp/nps_traceability_XXXXXX";
    const char *made = ::mkdtemp(pattern);
    if (made == nullptr) {
        std::cout << "traceability selftest: no temporary directory\n";
        return 1;
    }
    const std::string once_path = std::string(made) + "/once.md";
    const std::string twice_path = std::string(made) + "/twice.md";
    {
        std::ofstream out(once_path.c_str());
        out << "| ID | Priority | Requirement |\n|---|---|---|\n"
            << "| MATH-001 | P0 | The first |\n"
            << "| MATH-002 | P1 | The second |\n";
    }
    {
        std::ofstream out(twice_path.c_str());
        out << "| ID | Priority | Requirement |\n|---|---|---|\n"
            << "| MATH-001 | P0 | The first |\n"
            << "| MATH-002 | P1 | The second |\n\n"
            << "| ID | Priority | Requirement |\n|---|---|---|\n"
            << "| MATH-001 | P2 | The first again, at another priority |\n";
    }

    std::vector<Requirement> once;
    std::string once_repeated;
    const bool once_read = read_requirements(once_path, &once, &once_repeated);
    std::vector<Requirement> twice;
    std::string twice_repeated;
    const bool twice_read = read_requirements(twice_path, &twice, &twice_repeated);

    int failures = 0;
    const struct {
        bool ok;
        const char *what;
    } checks[] = {
        {once_read && once.size() == 2 && once_repeated.empty(),
         "a table listing each requirement once reads as those requirements"},
        {!twice_read && twice_repeated == "MATH-001",
         "a requirement listed twice is refused and named, whatever its second priority"},
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
        if (!checks[i].ok)
            ++failures;
        std::cout << "traceability selftest: " << (checks[i].ok ? "ok   " : "FAIL ")
                  << checks[i].what << "\n";
    }
    std::cout << "traceability selftest: " << count_text(static_cast<size_t>(failures))
              << " failed\n";
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest")
        return selftest();
    if (argc < 5) {
        std::cout << "usage: nps_traceability <prd.md> <evidence.txt> <catalog.md> <report.md>\n"
                     "       nps_traceability --selftest\n";
        return 2;
    }
    const std::string prd = argv[1];
    const std::string evidence_path = argv[2];
    const std::string catalog_path = argv[3];
    const std::string report_path = argv[4];

    std::vector<Requirement> requirements;
    std::string repeated;
    if (!read_requirements(prd, &requirements, &repeated) || requirements.empty()) {
        if (!repeated.empty())
            std::cout << "traceability: the PRD lists " << repeated << " twice\n";
        else
            std::cout << "traceability: no requirements read from " << prd << "\n";
        return 1;
    }
    std::vector<Evidence> evidence;
    std::set<std::string> groups_run;
    if (!read_evidence(evidence_path, &evidence, &groups_run)) {
        std::cout << "traceability: no evidence file at " << evidence_path
                  << ", run the unit tests first\n";
        return 1;
    }

    // Section 19.9 wants a requirement linked to catalog families as well as to tests. The link runs
    // through the test group: the catalog says which groups exercise a family, and the evidence says
    // which group a check came from.
    std::vector<Family> families;
    if (!read_catalog(catalog_path, &families) || families.empty()) {
        std::cout << "traceability: no families read from " << catalog_path << "\n";
        return 1;
    }
    std::map<std::string, std::vector<std::string> > families_of_group;
    for (size_t i = 0; i < families.size(); ++i) {
        for (size_t g = 0; g < families[i].groups.size(); ++g)
            families_of_group[families[i].groups[g]].push_back(families[i].id);
    }

    std::map<std::string, std::vector<Evidence> > by_requirement;
    std::set<std::string> groups_seen;
    for (size_t i = 0; i < evidence.size(); ++i) {
        by_requirement[evidence[i].requirement].push_back(evidence[i]);
        groups_seen.insert(evidence[i].group);
    }

    std::map<std::string, UniversalCoverage> universal_coverage;
    size_t universal_family_gaps = 0;
    for (size_t i = 0; i < requirements.size(); ++i) {
        UniversalCoverage coverage;
        const UniversalScope scope = universal_domain(requirements[i], &coverage.domain);
        if (scope == UniversalScope::NotUniversal)
            continue;
        if (scope == UniversalScope::Malformed) {
            coverage.malformed = true;
            ++universal_family_gaps;
            std::cout << "traceability: " << requirements[i].id
                      << " has a malformed universal module scope in requirement text\n";
            universal_coverage[requirements[i].id] = coverage;
            continue;
        }
        const std::string family_prefix = coverage.domain + ".";
        std::map<std::string, std::vector<Evidence> >::const_iterator requirement_evidence =
            by_requirement.find(requirements[i].id);
        for (size_t family_index = 0; family_index < families.size(); ++family_index) {
            if (!families[family_index].id.starts_with(family_prefix))
                continue;
            coverage.applicable_families.insert(families[family_index].id);
            bool passed = false;
            if (requirement_evidence != by_requirement.end()) {
                for (size_t evidence_index = 0;
                     evidence_index < requirement_evidence->second.size() && !passed;
                     ++evidence_index) {
                    if (!requirement_evidence->second[evidence_index].passed)
                        continue;
                    for (size_t group_index = 0;
                         group_index < families[family_index].groups.size(); ++group_index) {
                        if (requirement_evidence->second[evidence_index].group ==
                            families[family_index].groups[group_index]) {
                            passed = true;
                            break;
                        }
                    }
                }
            }
            if (!passed) {
                coverage.missing_families.insert(families[family_index].id);
                ++universal_family_gaps;
                std::cout << "traceability: " << requirements[i].id
                          << " is missing passing evidence for catalog family "
                          << families[family_index].id << "\n";
            }
        }
        if (coverage.applicable_families.empty()) {
            ++universal_family_gaps;
            std::cout << "traceability: " << requirements[i].id << " applies to every "
                      << coverage.domain << " module, but no catalog family begins " << family_prefix
                      << "\n";
        }
        universal_coverage[requirements[i].id] = coverage;
    }

    // A catalog naming a test group that never ran is a link to nothing, and an error. A group that
    // ran and produced no evidence yet is a gap worth counting rather than a fault.
    size_t missing_groups = 0;
    size_t untagged_groups = 0;
    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             families_of_group.begin();
         it != families_of_group.end(); ++it) {
        if (!groups_run.count(it->first)) {
            ++missing_groups;
            std::cout << "traceability: the catalog names test group " << it->first
                      << ", which no test run reports\n";
            continue;
        }
        if (!groups_seen.count(it->first))
            ++untagged_groups;
    }

    size_t unknown = 0;
    size_t failing = 0;
    for (std::map<std::string, std::vector<Evidence> >::const_iterator it = by_requirement.begin();
         it != by_requirement.end(); ++it) {
        bool known = false;
        for (size_t i = 0; i < requirements.size(); ++i) {
            if (requirements[i].id == it->first) {
                known = true;
                break;
            }
        }
        if (!known) {
            ++unknown;
            std::cout << "traceability: evidence names " << it->first
                      << ", which is not a requirement in the PRD\n";
        }
        for (size_t i = 0; i < it->second.size(); ++i) {
            if (!it->second[i].passed)
                ++failing;
        }
    }

    std::ofstream report(report_path.c_str());
    if (!report) {
        std::cout << "traceability: could not write " << report_path << "\n";
        return 1;
    }
    report << "# Requirement traceability\n\n";
    report << "Generated by tools/traceability.cc from the PRD and the evidence the host tests "
              "wrote. Do not edit.\n\n";
    report << "A requirement is evidenced when a check that names it passed. A requirement beginning "
              "with `Every <domain> module shall` additionally needs passing evidence from a test "
              "group belonging to every `<domain>.` catalog family. The family column comes from "
              "the catalog, which says which test groups exercise each family. Section 19.9 "
              "asks for three more links this report does not carry: implementation components, "
              "device evidence and release status. Nothing here should be read as coverage of "
              "those.\n\n";

    size_t evidenced = 0;
    size_t prioritised = 0;
    size_t prioritised_evidenced = 0;
    size_t mvp = 0;
    size_t mvp_evidenced = 0;
    std::map<std::string, size_t> per_family;
    std::map<std::string, size_t> per_family_evidenced;
    for (size_t i = 0; i < requirements.size(); ++i) {
        const std::string family = requirements[i].id.substr(0, requirements[i].id.find('-'));
        ++per_family[family];
        const bool scoped = is_priority(requirements[i].priority);
        if (scoped)
            ++prioritised;
        const bool mvp_scoped = is_mvp_requirement(requirements[i].priority);
        if (mvp_scoped)
            ++mvp;
        bool requirement_evidenced = evidenced_by(by_requirement, requirements[i].id);
        std::map<std::string, UniversalCoverage>::const_iterator universal =
            universal_coverage.find(requirements[i].id);
        if (universal != universal_coverage.end()) {
            requirement_evidenced = !universal->second.applicable_families.empty() &&
                                     universal->second.missing_families.empty();
        }
        if (requirement_evidenced) {
            ++evidenced;
            ++per_family_evidenced[family];
            if (scoped)
                ++prioritised_evidenced;
            if (mvp_scoped)
                ++mvp_evidenced;
        }
    }

    report << "MVP criterion 15 gates on the P0 and applicable INV requirements. Until release "
              "applicability is recorded, every INV requirement is conservatively in MVP scope. "
              "This run evidences "
           << count_text(mvp_evidenced) << " of " << count_text(mvp)
           << " MVP requirements. Section 24's word-problem requirements are staged WP1 to WP3 "
              "rather than prioritised, and are counted here as well so the report does not quietly "
              "leave them out.\n\n";
    report << "| Family | Requirements | Evidenced |\n|---|---:|---:|\n";
    for (std::map<std::string, size_t>::const_iterator it = per_family.begin();
         it != per_family.end(); ++it) {
        report << "| " << it->first << " | " << count_text(it->second) << " | "
               << count_text(per_family_evidenced[it->first]) << " |\n";
    }
    report << "| total | " << count_text(requirements.size()) << " | " << count_text(evidenced)
           << " |\n\n";

    report << "## Every requirement\n\n";
    report << "| ID | Priority | Status | Families | Evidence |\n|---|---|---|---|---|\n";
    for (size_t i = 0; i < requirements.size(); ++i) {
        const Requirement &r = requirements[i];
        report << "| " << r.id << " | " << r.priority << " | ";
        std::map<std::string, std::vector<Evidence> >::const_iterator found =
            by_requirement.find(r.id);
        std::map<std::string, UniversalCoverage>::const_iterator universal =
            universal_coverage.find(r.id);
        if (found == by_requirement.end() && universal == universal_coverage.end()) {
            report << "unmet, no evidence | | |\n";
            continue;
        }
        bool all_passed = found != by_requirement.end();
        std::set<std::string> named_families;
        if (found != by_requirement.end()) {
            for (size_t j = 0; j < found->second.size(); ++j) {
                if (!found->second[j].passed)
                    all_passed = false;
                const std::vector<std::string> &of = families_of_group[found->second[j].group];
                for (size_t k = 0; k < of.size(); ++k)
                    named_families.insert(of[k]);
            }
        }
        if (universal != universal_coverage.end()) {
            if (universal->second.malformed) {
                report << "unmet, malformed universal module scope";
            } else if (universal->second.applicable_families.empty()) {
                report << "unmet, no applicable catalog families for " << universal->second.domain;
            } else if (!universal->second.missing_families.empty()) {
                report << "unmet, missing family evidence: ";
                for (std::set<std::string>::const_iterator missing =
                         universal->second.missing_families.begin();
                     missing != universal->second.missing_families.end(); ++missing) {
                    if (missing != universal->second.missing_families.begin())
                        report << "<br>";
                    report << *missing;
                }
            } else {
                report << (all_passed ? "evidenced" : "evidence failing");
            }
            named_families = universal->second.applicable_families;
        } else {
            report << (all_passed ? "evidenced" : "evidence failing");
        }
        report << " | ";
        if (named_families.empty()) {
            if (universal != universal_coverage.end() && universal->second.malformed)
                report << "none, malformed scope";
            else if (universal != universal_coverage.end())
                report << "none, no applicable families";
            else
                report << "none, shared machinery";
        }
        for (std::set<std::string>::const_iterator f = named_families.begin();
             f != named_families.end(); ++f) {
            if (f != named_families.begin())
                report << "<br>";
            report << *f;
        }
        report << " | ";
        if (found != by_requirement.end()) {
            for (size_t j = 0; j < found->second.size(); ++j) {
                if (j)
                    report << "<br>";
                report << found->second[j].group << ": " << found->second[j].what;
            }
        }
        report << " |\n";
    }
    report.close();

    std::cout << "traceability: " << count_text(mvp_evidenced) << " of " << count_text(mvp)
              << " MVP requirements evidenced, " << count_text(prioritised_evidenced) << " of "
              << count_text(prioritised) << " adopted prioritised requirements evidenced, "
              << count_text(evidenced) << " of " << count_text(requirements.size())
              << " counting the staged ones, " << count_text(unknown) << " unknown ids, "
              << count_text(failing) << " failing evidence, " << count_text(missing_groups)
              << " catalog groups that did not run, " << count_text(untagged_groups)
              << " that ran with nothing tagged, " << count_text(universal_family_gaps)
              << " universal family evidence gaps, report in " << report_path << "\n";
    return unknown == 0 && failing == 0 && missing_groups == 0 ? 0 : 1;
}
