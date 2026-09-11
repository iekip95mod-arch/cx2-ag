// PRD section 27's coverage catalog, read once for the two tools that need it. It was a copy in each
// until the traceability report needed the families as well.
#ifndef NPS_TOOLS_CATALOG_H
#define NPS_TOOLS_CATALOG_H

#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace nps_tools {

struct Rule {
    std::string id;
    std::string evidence;
};

struct Family {
    std::string id;
    std::vector<Rule> rules;
    // The test groups that exercise this family, as run_tests.cc names them.
    std::vector<std::string> groups;
    // The proof obligations its steps raise, which section 19.9 wants counted on their own.
    std::vector<std::string> obligations;
    // Which field names the block carried, for reporting what section 27 asks for and is absent.
    std::set<std::string> fields;
    // The subset that answered their question rather than dodging it with a bare none.
    std::set<std::string> answered;
    // The engine strings required_assumptions quotes, which are what the assumption join is over.
    std::vector<std::string> assumption_quotes;
    bool assumptions_none = false;
    bool assumptions_malformed = false;
};

inline std::string trimmed(const std::string &s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
        ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
        --end;
    return s.substr(begin, end - begin);
}

// The first word of a line and the rest of it, which is the whole of the catalog's syntax.
inline bool first_word(const std::string &line, std::string *word, std::string *rest) {
    const std::string s = trimmed(line);
    const size_t space = s.find(' ');
    if (space == std::string::npos)
        return false;
    *word = s.substr(0, space);
    *rest = trimmed(s.substr(space + 1));
    return !rest->empty();
}

inline std::vector<std::string> comma_separated(const std::string &value) {
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == ',') {
            const std::string one = trimmed(current);
            if (!one.empty())
                out.push_back(one);
            current.clear();
            continue;
        }
        current.push_back(value[i]);
    }
    const std::string last = trimmed(current);
    if (!last.empty())
        out.push_back(last);
    return out;
}

// A value opening with the word none, which the catalog writes where a field is empty on purpose.
inline bool declares_none(const std::string &value) {
    if (value.rfind("none", 0) != 0)
        return false;
    return value.size() == 4 || value[4] == ' ' || value[4] == ',';
}

// The catalog's rule is that a none says why, so a bare none is the question dodged rather than
// answered. Asked of every field rather than of the two that happen to read declares_none today.
inline bool answers_the_question(const std::string &value) {
    if (!declares_none(value))
        return !value.empty();
    size_t at = 4;
    while (at < value.size() && (value[at] == ' ' || value[at] == ',' || value[at] == '\t'))
        ++at;
    return at < value.size();
}

// A catalog line quotes an engine string in double quotes, so a prose field can still be joined
// exactly. An unclosed or empty pair is malformed rather than skipped.
inline bool quoted_fragments(const std::string &value, std::vector<std::string> *out) {
    size_t at = 0;
    while (at < value.size()) {
        if (value[at] != '"') {
            ++at;
            continue;
        }
        const size_t close = value.find('"', at + 1);
        if (close == std::string::npos)
            return false;
        const std::string inside = value.substr(at + 1, close - at - 1);
        if (inside.empty())
            return false;
        out->push_back(inside);
        at = close + 1;
    }
    return true;
}

inline bool read_catalog(const std::string &path, std::vector<Family> *out) {
    std::ifstream in(path.c_str());
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        std::string word, rest;
        if (!first_word(line, &word, &rest))
            continue;
        if (word[0] == '#')
            continue;
        if (word == "family") {
            std::string field, value;
            if (!first_word(rest, &field, &value) || field != "id")
                continue;
            Family f;
            f.id = value;
            f.fields.insert("id");
            out->push_back(f);
            continue;
        }
        if (out->empty())
            continue;
        Family &f = out->back();
        if (word == "rule") {
            std::string id, evidence;
            if (!first_word(rest, &id, &evidence))
                continue;
            Rule r;
            r.id = id;
            r.evidence = evidence;
            f.rules.push_back(r);
            f.fields.insert("rule_ids");
            continue;
        }
        // A repeated line adds to the list rather than replacing it, the same as a rule line does.
        if (word == "test_group_ids") {
            const std::vector<std::string> more = comma_separated(rest);
            f.groups.insert(f.groups.end(), more.begin(), more.end());
        }
        if (word == "required_assumptions") {
            f.assumptions_none = declares_none(rest);
            if (!quoted_fragments(rest, &f.assumption_quotes))
                f.assumptions_malformed = true;
        }
        // A declared none is the field answered with an empty list, not an id spelled none.
        if (word == "proof_obligation_ids" && !declares_none(rest)) {
            const std::vector<std::string> more = comma_separated(rest);
            f.obligations.insert(f.obligations.end(), more.begin(), more.end());
        }
        f.fields.insert(word);
        if (answers_the_question(rest))
            f.answered.insert(word);
    }
    return true;
}

inline std::string count_text(size_t n) {
    std::string digits;
    do {
        digits.insert(digits.begin(), static_cast<char>('0' + (n % 10)));
        n /= 10;
    } while (n);
    return digits;
}

}  // namespace nps_tools

#endif
