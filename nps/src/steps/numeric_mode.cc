#include "nps/steps/numeric_mode.h"

#include "nps/core/canonical.h"

namespace nps {
namespace {

VerificationRecord equal_values(const std::string &detail) {
    VerificationRecord v;
    v.method = "rule-local invariant";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::StructurallyValid;
    v.detail = detail;
    return v;
}

}  // namespace

ModeStep read_decimals_exactly(Arena &arena, Derivation &derivation, StepId parent,
                               const char *phase, NodeId expression, NodeId *out) {
    *out = expression;
    if (expression == kNoNode || !has_decimal(arena, expression))
        return ModeStep::NothingToDo;
    if (has_decimal_exponent(arena, expression))
        return ModeStep::DecimalExponent;

    const NodeId exact = exactify(arena, expression);
    if (exact == kNoNode)
        return ModeStep::TooManyDigits;

    Step s;
    s.phase = phase;
    s.goal = "Read the decimals exactly";
    s.rule_id = "num.decimal-to-rational";
    s.rule_name = "Decimal as an exact fraction";
    s.claim = ClaimType::EquivalentExpression;
    s.explanation_short =
        "A decimal literal is a fraction over a power of ten, so the working below it is exact";
    s.explanation_detailed =
        "The arithmetic below runs in exact fractions whichever mode is set. Reading 0.5 as one half "
        "loses nothing, because that is the number 0.5 names.";
    s.proof_obligations.push_back({"obl.numeric.decimal-reads-as-written",
                                   "the fraction written here is the one the decimal literal names"});
    s.verifications.push_back(equal_values("each decimal equals the fraction it was replaced by"));

    TransformationPayload payload;
    payload.before = expression;
    payload.after = exact;
    payload.concrete_action = "Write each decimal as a fraction";
    payload.reversible = true;
    derivation.add_transformation(parent, std::move(s), std::move(payload));

    *out = exact;
    return ModeStep::Recorded;
}

const char *promotion_refusal(ModeStep step) {
    switch (step) {
        case ModeStep::TooManyDigits:
            return "a decimal here carries more digits than an exact fraction this build can hold";
        case ModeStep::DecimalExponent:
            return "a decimal sits in an exponent, and reading it as exact would give an exact rule "
                   "a precision the input does not have. Write the exponent as a whole number.";
        case ModeStep::NothingToDo:
        case ModeStep::Recorded:
            break;
    }
    return nullptr;
}

bool promotion_exhausted(ModeStep step) { return step == ModeStep::TooManyDigits; }

ModeStep report_in_decimals(Arena &arena, Derivation &derivation, StepId parent, const char *phase,
                            NodeId answer, NodeId *out) {
    *out = answer;
    if (answer == kNoNode)
        return ModeStep::NothingToDo;

    const NodeId reported = decimalize(arena, answer);
    if (reported == kNoNode)
        return ModeStep::TooManyDigits;
    if (reported == answer)
        return ModeStep::NothingToDo;

    Step s;
    s.phase = phase;
    s.goal = "Report the answer in decimals";
    s.rule_id = "num.rational-to-decimal";
    s.rule_name = "Decimal report";
    s.claim = ClaimType::EquivalentExpression;
    s.explanation_short = "Each exact fraction with a terminating decimal is written as that decimal";
    s.explanation_detailed =
        "The working above is exact and this last line rewrites it. A fraction with no terminating "
        "decimal, such as a third, is left as a fraction rather than rounded to fit the mode.";
    s.proof_obligations.push_back({"obl.numeric.decimal-report-equals-exact",
                                   "each decimal written here equals the fraction it replaced"});
    s.verifications.push_back(
        equal_values("every rewritten coefficient terminates, so no digit was dropped"));

    TransformationPayload payload;
    payload.before = answer;
    payload.after = reported;
    payload.concrete_action = "Write each exact fraction as a decimal";
    payload.reversible = true;
    derivation.add_transformation(parent, std::move(s), std::move(payload));

    *out = reported;
    return ModeStep::Recorded;
}

}  // namespace nps
