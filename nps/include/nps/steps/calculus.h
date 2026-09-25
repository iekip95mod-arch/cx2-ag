#ifndef NPS_CALCULUS_H
#define NPS_CALCULUS_H

#include "nps/cas/giac_adapter.h"
#include "nps/steps/command.h"
#include "nps/steps/derivation.h"

namespace nps {

enum class CalculusOutcome : uint8_t {
    Evaluated,
    InfiniteLimit,
    DoesNotExist,
    UnsupportedForm,
    InvalidInput,
    Refused,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *calculus_outcome_name(CalculusOutcome outcome);

struct CalculusResult {
    CalculusOutcome outcome = CalculusOutcome::UnsupportedForm;
    NodeId value = kNoNode;
    int infinity = 0;
    bool does_not_exist = false;
    // The tangent-line family. slope and point_value are the two exact facts the line is
    // built from, and approximate says the answer is a linearization rather than an equality.
    NodeId slope = kNoNode;
    NodeId point_value = kNoNode;
    bool approximate = false;
    // The Taylor family. remainder is the Lagrange form of what the polynomial leaves out.
    NodeId remainder = kNoNode;
    bool answer_only = false;
    bool backend_attempted = false;
    bool backend_compared = false;
    bool agrees = false;
    bool comparison_attempted = false;
    Response backend_result;
    Response comparison_result;
    DerivationStatus status = DerivationStatus::Unsupported;
    std::string detail;
    Cost cost;
};

CalculusResult calculus_walkthrough(Arena &arena, Derivation &derivation, const Command &command,
                                   const Budget &budget = Budget(), Backend *backend = nullptr);

}

#endif
