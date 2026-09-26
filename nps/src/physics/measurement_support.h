#ifndef NPS_PHYSICS_MEASUREMENT_SUPPORT_H
#define NPS_PHYSICS_MEASUREMENT_SUPPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/core/rational.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {
namespace measure {

inline bool normalize_copy(const Rational &source, Rational *normalized) {
    *normalized = source;
    return normalise(&normalized->num, &normalized->den);
}

inline bool valid_quantity(const Quantity &quantity, std::string *detail) {
    Rational normalized;
    if (!normalize_copy(quantity.value, &normalized)) {
        *detail = "the quantity has an invalid exact value";
        return false;
    }
    if (!normalize_copy(quantity.unit.scale, &normalized) || normalized.num <= 0) {
        *detail = "the unit has an invalid SI conversion scale";
        return false;
    }
    switch (quantity.precision.kind) {
        case NumberKind::Exact:
            if (quantity.precision.significant_digits != 0) {
                *detail = "an exact value cannot carry a measured significant-figure count";
                return false;
            }
            return true;
        case NumberKind::Measured:
            if (quantity.precision.significant_digits == 0 ||
                quantity.precision.significant_digits > 18) {
                *detail = "a measured value needs between 1 and 18 significant figures";
                return false;
            }
            return true;
    }
    *detail = "the quantity has an invalid precision kind";
    return false;
}

inline std::string value_text(const Quantity &quantity) {
    Rational normalized;
    if (!normalize_copy(quantity.value, &normalized))
        return "invalid exact value";
    std::string text = rational_text(normalized);
    if (quantity.precision.kind == NumberKind::Measured) {
        std::string measured;
        if (rounded_text(normalized, quantity.precision.significant_digits, &measured))
            text = measured;
    }
    return text;
}

inline std::string known_text(const std::string &name, const Quantity &quantity) {
    std::string text = name + " = " + value_text(quantity);
    if (!quantity.unit.text.empty())
        text += " " + quantity.unit.text;
    return text;
}

inline Unit si_unit(const Dimension &dimension) {
    Unit unit;
    unit.dimension = dimension;
    unit.text = si_unit_text(dimension);
    unit.scale.num = 1;
    unit.scale.den = 1;
    return unit;
}

inline NodeId rational_node(Arena &arena, const Rational &rational) {
    if (rational.den == 1)
        return arena.integer(integer_text(rational.num));
    NodeId numerator = arena.integer(integer_text(rational.num));
    NodeId denominator = arena.integer(integer_text(rational.den));
    NodeId reciprocal = arena.binary(Kind::Pow, denominator, arena.integer("-1"));
    return arena.binary(Kind::Mul, numerator, reciprocal);
}

// dimension_powers owns the count, so a new base dimension is still one edit rather than nine.
inline NodeId dimension_node(Arena &arena, const Dimension &dimension) {
    int powers[kDimensionCount];
    dimension_powers(dimension, powers);
    int written = kDimensionCount;
    // Trailing zero powers past current are left out so records from before PHYS-018 read unchanged.
    while (written > 4 && powers[written - 1] == 0)
        --written;
    std::vector<NodeId> exponents;
    for (int i = 0; i < written; ++i)
        exponents.push_back(arena.integer(integer_text(powers[i])));
    return arena.call("dimension", exponents);
}

inline bool rational_of_node(const Arena &arena, NodeId id, Rational *value) {
    if (id == kNoNode)
        return false;
    const Node &node = arena.at(id);
    int64_t integer;
    if (node.kind == Kind::Integer || node.kind == Kind::Neg) {
        if (small_integer(arena, id, &integer)) {
            value->num = integer;
            value->den = 1;
            return true;
        }
        if (node.kind != Kind::Neg)
            return false;
        Rational inner;
        Rational zero;
        return rational_of_node(arena, arena.children(node)[0], &inner) &&
               rational_sub(zero, inner, value);
    }
    const ChildView children = arena.children(node);
    if (node.kind != Kind::Mul || children.size() != 2)
        return false;
    Rational numerator;
    if (!rational_of_node(arena, children[0], &numerator) || numerator.den != 1)
        return false;
    const Node &reciprocal = arena.at(children[1]);
    const ChildView reciprocal_children = arena.children(reciprocal);
    int64_t denominator;
    int64_t exponent;
    if (reciprocal.kind != Kind::Pow || reciprocal_children.size() != 2 ||
        !small_integer(arena, reciprocal_children[0], &denominator) ||
        !small_integer(arena, reciprocal_children[1], &exponent) || exponent != -1 ||
        denominator == 0) {
        return false;
    }
    value->num = numerator.num;
    value->den = denominator;
    return normalise(&value->num, &value->den);
}

inline VerificationRecord verification(const char *method, const std::string &detail,
                                       EvidenceStrength passing, VerificationOutcome outcome) {
    VerificationRecord record;
    record.method = method;
    record.detail = detail;
    record.outcome = outcome;
    record.strength = strength_for(outcome, passing);
    return record;
}

// detailed has no default on purpose. STEP-021 wants every transformation to say how to recognise
// its rule again, and a builder that lets the field be left out is why it was empty here.
inline Step transformation_step(const std::string &goal, const std::string &rule_id,
                                const char *rule_name, const std::string &explanation,
                                const std::string &detailed) {
    Step step;
    step.phase = "solve";
    step.goal = goal;
    step.rule_id = rule_id;
    step.rule_name = rule_name;
    step.explanation_short = explanation;
    step.explanation_detailed = detailed;
    step.claim = ClaimType::SolutionSetPreserved;
    return step;
}

// What the measured-precision tail did, so each caller maps it onto its own outcome enum.
enum class ReportOutcome : uint8_t {
    Unchanged,
    Rounded,
    Unreadable,
    OutsideHalfPlace,
    Overflow,
    Cancelled,
    ArenaFailed,
};

// The reporting tail every measured family ends with. value_text carries the exact spelling in and
// the reported one out, and detail carries the sentence the caller records with its status.
inline ReportOutcome report_measured_precision(Arena &arena, Derivation &derivation, Meter &meter,
                                               const Rational &candidate, NodeId solution,
                                               const Precision &precision,
                                               const std::string &rule_id,
                                               const std::string &rounding_detailed,
                                               std::string *value_text, std::string *detail) {
    std::string reported;
    if (!rounded_text(candidate, precision.significant_digits, &reported)) {
        *detail = "reporting the measured precision exceeds exact integer arithmetic";
        return ReportOutcome::Overflow;
    }
    if (reported == *value_text)
        return ReportOutcome::Unchanged;
    // Checked against the string this step records. The predicate parses the reported text back and
    // measures the error, so it shares no path with rounded_text. A wrong predicate it cannot
    // catch, and units_tests pins that with negative cases.
    const HalfPlace checked = precision_rounding_valid(candidate, reported, precision);
    if (checked == HalfPlace::Outside) {
        *detail = reported + " is further than half a unit in its last place from " + *value_text;
        return ReportOutcome::OutsideHalfPlace;
    }
    if (!meter.step())
        return ReportOutcome::Cancelled;
    if (checked == HalfPlace::Unreadable) {
        // Never compared, so there is no verdict to refuse on. The exact value above is the answer
        // and it stands; what is withheld is the rounded spelling.
        *detail = reported + " could not be read back as a decimal, so it was never compared " +
                  "against " + *value_text;
        Step step;
        step.phase = "report";
        step.goal = "Report the answer to the measured precision";
        step.rule_id = rule_id;
        step.rule_name = "Significant figures";
        step.claim = ClaimType::NoClaim;
        step.explanation_short = "Use the fewest significant figures among the measured givens";
        step.explanation_detailed =
            "The rounded spelling could not be read back as a decimal, so it was never compared "
            "against the exact value. The exact value is reported instead, since showing a "
            "rounding nothing checked would be showing an answer with no evidence behind it.";
        step.proof_obligations.push_back(
            {"obl.physics.reported-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        step.verifications.push_back(verification("exact comparison against the unrounded value",
                                                  *detail, EvidenceStrength::CandidateChecked,
                                                  VerificationOutcome::Inconclusive));
        CheckPayload check;
        check.target_claim =
            "the reported value is within half a unit in the last place of the exact one";
        check.check_method = "read the rounded text back and compare it against the exact value";
        check.expected_relation = "the difference is at most half a unit in the last place";
        check.observed_result = *detail;
        derivation.add_check(kNoStep, std::move(step), std::move(check));
        return ReportOutcome::Unreadable;
    }
    Step step;
    step.phase = "report";
    step.goal = "Report the answer to the measured precision";
    step.rule_id = rule_id;
    step.rule_name = "Significant figures";
    step.explanation_short = "Use the fewest significant figures among the measured givens";
    step.explanation_detailed = rounding_detailed;
    step.claim = ClaimType::NoClaim;
    step.verifications.push_back(
        verification("exact comparison against the unrounded value",
                     reported + " is within half a unit in the last place of " + *value_text,
                     EvidenceStrength::CandidateChecked, VerificationOutcome::Passed));
    step.proof_obligations.push_back(
        {"obl.physics.reported-within-half-place",
         "the reported value is within half a unit in the last place of the exact one"});
    TransformationPayload payload;
    payload.before = solution;
    payload.after = reported.find('.') == std::string::npos ? arena.integer(reported)
                                                            : arena.decimal(reported);
    payload.concrete_action = "Report " + *value_text + " as " + reported;
    payload.reversible = false;
    if (arena.failed())
        return ReportOutcome::ArenaFailed;
    derivation.add_transformation(kNoStep, std::move(step), std::move(payload));
    *value_text = reported;
    return ReportOutcome::Rounded;
}

}  // namespace measure
}  // namespace nps

#endif
