// VER-015's evidence row, shared by the passes that write it and the join that reads it.
#ifndef NPS_TOOLS_CHECK_KINDS_H
#define NPS_TOOLS_CHECK_KINDS_H

#include <cstddef>
#include <string>

#include "nps/steps/derivation.h"
#include "nps/steps/schema.h"

namespace nps_tools {

// Spelled here rather than borrowed from derivation.cc, which the coverage tool does not link.
inline const char *check_outcome_word(nps::VerificationOutcome outcome) {
    switch (outcome) {
        case nps::VerificationOutcome::NotAttempted:
            return "not-attempted";
        case nps::VerificationOutcome::Passed:
            return "passed";
        case nps::VerificationOutcome::Failed:
            return "failed";
        case nps::VerificationOutcome::Inconclusive:
            return "inconclusive";
    }
    return "unknown";
}

inline bool check_outcome_from_word(const std::string &word, nps::VerificationOutcome *outcome) {
    for (nps::VerificationOutcome candidate :
         {nps::VerificationOutcome::NotAttempted, nps::VerificationOutcome::Passed,
          nps::VerificationOutcome::Failed, nps::VerificationOutcome::Inconclusive}) {
        if (word == check_outcome_word(candidate)) {
            *outcome = candidate;
            return true;
        }
    }
    return false;
}

inline bool check_kind_from_name(const std::string &name, nps::CheckKind *kind) {
    for (size_t i = 0; i < nps::kCheckKindCount; ++i) {
        const nps::CheckKind candidate = static_cast<nps::CheckKind>(i);
        if (name == nps::check_kind_name(candidate)) {
            *kind = candidate;
            return true;
        }
    }
    return false;
}

// One evidence file line, whose leading word keeps traceability.cc reading past it.
inline std::string check_kind_row(const std::string &rule_id, nps::CheckKind kind,
                                  nps::VerificationOutcome outcome, size_t count,
                                  const std::string &source) {
    return "check_kind\t" + rule_id + "\t" + nps::check_kind_name(kind) + "\t" +
           check_outcome_word(outcome) + "\t" + std::to_string(count) + "\t" + source;
}

}  // namespace nps_tools

#endif
