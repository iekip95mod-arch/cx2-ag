#ifndef NPS_UNIT_CONVERSION_H
#define NPS_UNIT_CONVERSION_H

#include <cstdint>
#include <string>

#include "nps/core/budgets.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

struct UnitConversionProblem {
    Quantity source;
    Unit target;
};

enum class UnitConversionParseOutcome : uint8_t {
    Parsed,
    InvalidQuantity,
    InvalidTargetUnit,
};

const char *unit_conversion_parse_outcome_name(UnitConversionParseOutcome outcome);

struct UnitConversionParseResult {
    UnitConversionParseOutcome outcome = UnitConversionParseOutcome::InvalidQuantity;
    std::string detail;

    bool ok() const { return outcome == UnitConversionParseOutcome::Parsed; }
};

UnitConversionParseResult parse_unit_conversion_problem(const std::string &source,
                                                         const std::string &target_unit,
                                                         UnitConversionProblem *problem);

enum class UnitConversionOutcome : uint8_t {
    Converted,
    DimensionMismatch,
    ArithmeticOverflow,
    // A check the conversion ran and failed, as distinct from one it could not afford to run.
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *unit_conversion_outcome_name(UnitConversionOutcome outcome);

struct UnitConversionResult {
    UnitConversionOutcome outcome = UnitConversionOutcome::ResourceExceeded;
    Quantity value;
    bool has_value = false;
    Rational source_to_si_factor;
    Rational si_to_target_factor;
    Rational combined_factor;
    std::string value_text;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

UnitConversionResult solve_unit_conversion(Arena &arena, Derivation &derivation,
                                            const UnitConversionProblem &problem,
                                            const Budget &budget = Budget());

}

#endif
