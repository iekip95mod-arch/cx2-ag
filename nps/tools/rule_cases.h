// VER-010's four case kinds, spelled once for the rows' writers and the join that reads them.
#ifndef NPS_TOOLS_RULE_CASES_H
#define NPS_TOOLS_RULE_CASES_H

#include <cstddef>
#include <string>

namespace nps_tools {

// Positive and negative are read off records, and boundary and regression are declared by a test.
enum class RuleCaseKind {
    // The rule ran in a derivation that carries an answer and none of its own checks failed.
    Positive,
    // The rule's own verification failed, or its strategy refused the input before it ran.
    Negative,
    // A declared case at the edge of the rule's envelope that still reaches the rule.
    Boundary,
    // A declared case that reaches the rule and names the issue it pins with a hash and a number.
    Regression,
};

constexpr size_t kRuleCaseKindCount = 4;

inline const char *rule_case_kind_name(RuleCaseKind kind) {
    switch (kind) {
        case RuleCaseKind::Positive:
            return "positive";
        case RuleCaseKind::Negative:
            return "negative";
        case RuleCaseKind::Boundary:
            return "boundary";
        case RuleCaseKind::Regression:
            return "regression";
    }
    return "unknown";
}

inline bool rule_case_kind_from_name(const std::string &name, RuleCaseKind *kind) {
    for (size_t i = 0; i < kRuleCaseKindCount; ++i) {
        const RuleCaseKind candidate = static_cast<RuleCaseKind>(i);
        if (name == rule_case_kind_name(candidate)) {
            *kind = candidate;
            return true;
        }
    }
    return false;
}

// A regression case has to say which report it pins, so the name has to carry a hash and a digit.
inline bool names_an_issue(const std::string &what) {
    for (size_t i = 0; i + 1 < what.size(); ++i) {
        if (what[i] == '#' && what[i + 1] >= '0' && what[i + 1] <= '9')
            return true;
    }
    return false;
}

// One evidence file line, whose leading word keeps traceability.cc reading past it.
inline std::string rule_case_row(const std::string &rule_id, RuleCaseKind kind, bool passed,
                                 const std::string &source, const std::string &what) {
    return "rule_case\t" + rule_id + "\t" + rule_case_kind_name(kind) + "\t" +
           (passed ? "pass" : "fail") + "\t" + source + "\t" + what;
}

}  // namespace nps_tools

#endif
