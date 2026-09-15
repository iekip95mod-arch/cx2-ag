#include <cstdint>
#include <limits>
#include <string>

#include "nps/core/print.h"
#include "nps/physics/unit_conversion.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

bool parse_problem(TestSink &tests, const std::string &source, const std::string &target,
                   UnitConversionProblem *problem) {
    const UnitConversionParseResult parsed =
        parse_unit_conversion_problem(source, target, problem);
    tests.check(parsed.ok(), "the unit conversion fixture parses: " + source + " to " + target);
    return parsed.ok();
}

UnitConversionResult solve(const UnitConversionProblem &problem, Derivation *derivation,
                           const Budget &budget = Budget()) {
    Arena arena;
    return solve_unit_conversion(arena, *derivation, problem, budget);
}

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return true;
    }
    return false;
}

StepId step_with_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return static_cast<StepId>(i);
    }
    return kNoStep;
}

bool cancel_now(void *) { return true; }

// Everything the derivation actually published, so a record that misstates a dimension shows up
// wherever it was written rather than only in the step a test happened to pick.
std::string recorded_model(const Arena &arena, const Derivation &derivation) {
    std::string text;
    for (size_t i = 0; i < derivation.size(); ++i) {
        const TransformationPayload *step = derivation.transformation(static_cast<StepId>(i));
        if (step == nullptr)
            continue;
        if (step->before != kNoNode)
            text += print(arena, step->before) + "\n";
        if (step->after != kNoNode)
            text += print(arena, step->after) + "\n";
    }
    return text;
}

}

void run_unit_conversion_tests(TestSink &tests) {
    {
        // The figure count survives an exact factor, the place does not. 9.96 km is good to the
        // hundredth of a kilometre, which is 10 m, so the converted answer's last figure is in the
        // tens place. Copying the source place said hundredths of a metre, out by the scale.
        UnitConversionProblem scaled;
        if (parse_problem(tests, "9.96 km", "m", &scaled)) {
            Derivation derivation;
            const UnitConversionResult converted = solve(scaled, &derivation);
            tests.equal(converted.value_text, "9960 m", "the conversion reports three figures");
            tests.check(converted.value.precision.significant_digits == 3 &&
                            converted.value.precision.last_significant_decimal_place == 1,
                        "and the reported place moves with the scale rather than copying the "
                        "source, so the answer is not claimed good to the hundredth of a metre");
        }
        // One source literal into four targets. The place published beside each answer has to be
        // that answer's, and copying the source's gave -4 for all four.
        struct Row {
            const char *source;
            const char *target;
            int32_t place;
        };
        const Row rows[] = {{"0.0001 cm", "m", -6},
                            {"0.0001 m", "cm", -2},
                            {"0.0001 km", "m", -1},
                            {"0.0001 m", "km", -7}};
        for (const Row &row : rows) {
            UnitConversionProblem problem;
            if (!parse_problem(tests, row.source, row.target, &problem))
                continue;
            Derivation derivation;
            const UnitConversionResult converted = solve(problem, &derivation);
            tests.check(converted.has_value &&
                            converted.value.precision.last_significant_decimal_place == row.place,
                        std::string("the published place follows the converted value for ") +
                            row.source + " to " + row.target);
        }
    }
    {
        // Zero has no figures to count, so reading its last place off its characters downgraded it.
        struct ZeroRow {
            const char *source;
            const char *target;
            const char *text;
        };
        const ZeroRow zeros[] = {{"0.00 km", "m", "0 m"},
                                 {"0.000 km", "m", "0 m"},
                                 {"0.0 km", "m", "0 m"},
                                 {"0.00 kg", "g", "0 g"}};
        for (const ZeroRow &row : zeros) {
            UnitConversionProblem problem;
            if (!parse_problem(tests, row.source, row.target, &problem))
                continue;
            Derivation derivation;
            const UnitConversionResult converted = solve(problem, &derivation);
            tests.equal(derivation_status_name(converted.status), "solved and verified",
                        std::string("a measured zero converts and its rounding is checked for ") +
                            row.source + " to " + row.target);
            tests.equal(converted.value_text, row.text,
                        std::string("and the exact answer is what gets reported for ") +
                            row.source);
        }
        // Without these the rows above would pass for a conversion that never reached the check.
        const ZeroRow nonzero[] = {{"1.00 km", "m", "1000 m"}, {"2.5 km", "m", "2500 m"}};
        for (const ZeroRow &row : nonzero) {
            UnitConversionProblem problem;
            if (!parse_problem(tests, row.source, row.target, &problem))
                continue;
            Derivation derivation;
            const UnitConversionResult converted = solve(problem, &derivation);
            tests.equal(derivation_status_name(converted.status), "solved and verified",
                        std::string("a nonzero measurement at the same figure count still "
                                    "verifies for ") +
                            row.source);
            tests.equal(converted.value_text, row.text,
                        std::string("and reports the converted value for ") + row.source);
        }
    }
    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "3 cm^2", "mm^2", &problem))
            return;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "converted",
                    "a compatible area converts");
        tests.equal(conversion.value_text, "300 mm^2",
                    "both prefix powers participate in the area chain");
        tests.check(conversion.has_value && conversion.value.value.num == 300 &&
                        conversion.value.value.den == 1,
                    "the structured area answer remains exact");
        tests.check(conversion.source_to_si_factor.num == 1 &&
                        conversion.source_to_si_factor.den == 10000 &&
                        conversion.si_to_target_factor.num == 1000000 &&
                        conversion.si_to_target_factor.den == 1 &&
                        conversion.combined_factor.num == 100 &&
                        conversion.combined_factor.den == 1,
                    "the area result exposes both exact links and their reduced product");
        tests.equal(derivation_status_name(conversion.status), "solved and verified",
                    "the area conversion verifies every recorded claim");
        tests.equal(derivation.context.problem_family_id, "units.chain-link-conversion",
                    "the conversion records its problem family");
        tests.check(has_rule(derivation, "unit.convert.plan"), "the conversion records its plan");
        tests.check(has_rule(derivation, "unit.convert.check-dimension"),
                    "the conversion records its dimension check");
        tests.check(has_rule(derivation, "unit.convert.source-to-si"),
                    "the conversion records its source-to-SI link");
        tests.check(has_rule(derivation, "unit.convert.si-to-target"),
                    "the conversion records its SI-to-target link");
        const StepId source_link = step_with_rule(derivation, "unit.convert.source-to-si");
        const StepId target_link = step_with_rule(derivation, "unit.convert.si-to-target");
        tests.check(source_link != kNoStep && target_link != kNoStep &&
                        derivation.at(target_link).parent == source_link,
                    "the target link follows the source link in the derivation graph");
        tests.evidence("PHYS-014",
                       conversion.value.value.num == 300 && conversion.value.value.den == 1 &&
                           conversion.source_to_si_factor.num == 1 &&
                           conversion.source_to_si_factor.den == 10000 &&
                           conversion.si_to_target_factor.num == 1000000 &&
                           conversion.si_to_target_factor.den == 1 && source_link != kNoStep &&
                           target_link != kNoStep && derivation.at(target_link).parent == source_link,
                       "compatible powered units convert through visible exact scale-factor links");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "2.50 cm^3", "m^3", &problem))
            return;
        Arena arena;
        Derivation derivation;
        const UnitConversionResult conversion =
            solve_unit_conversion(arena, derivation, problem, Budget());
        tests.equal(conversion.value_text, "0.00000250 m^3",
                    "a measured volume preserves its precision only in the final report");
        tests.check(conversion.value.value.num == 1 && conversion.value.value.den == 400000,
                    "the measured volume stays an exact rational internally");
        tests.check(conversion.value.precision.kind == NumberKind::Measured &&
                        conversion.value.precision.significant_digits == 3,
                    "the structured volume answer retains its measurement metadata");
        tests.check(has_rule(derivation, "unit.convert.report-precision"),
                    "the measured volume records one final precision step");
        const StepId report = step_with_rule(derivation, "unit.convert.report-precision");
        const TransformationPayload *reporting = derivation.transformation(report);
        tests.check(reporting != nullptr && !reporting->reversible &&
                        reporting->before != reporting->after,
                    "final rounding is an irreversible non-identity transformation");
        tests.check(reporting != nullptr &&
                        print(arena, reporting->after).find("0.00000250") != std::string::npos,
                    "the reporting transformation records the rounded spelling");
        std::string half_place;
        if (report != kNoStep) {
            for (const VerificationRecord &check : derivation.at(report).verifications) {
                if (check.method == "exact half-place comparison")
                    half_place = check.detail;
            }
        }
        tests.equal(half_place,
                    "0.00000250 is within half a unit in the last place of 0.0000025",
                    "the half-place check leaves a record naming the two values it compared, so "
                    "removing the check cannot pass unnoticed");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "36 km/h", "m/s", &problem))
            return;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation);
        tests.equal(conversion.value_text, "10 m/s", "compound units use the same exact chain");
        tests.check(conversion.combined_factor.num == 5 && conversion.combined_factor.den == 18,
                    "the compound conversion factor is reduced exactly");
        tests.check(!has_rule(derivation, "unit.convert.report-precision"),
                    "an exact quantity is never rounded");
    }

    {
        const char *units[] = {"mm", "cm", "m", "km"};
        for (int power = 1; power <= 3; ++power) {
            for (const char *source_unit : units) {
                for (const char *target_unit : units) {
                    const std::string exponent = "^" + std::to_string(power);
                    UnitConversionProblem forward_problem;
                    if (!parse_problem(tests, "7 " + std::string(source_unit) + exponent,
                                       std::string(target_unit) + exponent, &forward_problem))
                        return;
                    Derivation forward_derivation;
                    const UnitConversionResult forward = solve(forward_problem, &forward_derivation);
                    UnitConversionProblem reverse_problem;
                    reverse_problem.source = forward.value;
                    reverse_problem.target = forward_problem.source.unit;
                    Derivation reverse_derivation;
                    const UnitConversionResult reverse = solve(reverse_problem, &reverse_derivation);
                    tests.check(forward.outcome == UnitConversionOutcome::Converted &&
                                    reverse.outcome == UnitConversionOutcome::Converted &&
                                    rational_equal(reverse.value.value, forward_problem.source.value),
                                "length prefix conversion round-trips exactly through power " +
                                    std::to_string(power));
                }
            }
        }
    }

    {
        UnitConversionProblem untouched;
        untouched.source.value.num = 7;
        UnitConversionParseResult parsed =
            parse_unit_conversion_problem("1 furlong", "m", &untouched);
        tests.equal(unit_conversion_parse_outcome_name(parsed.outcome), "invalid quantity",
                    "an unsupported source spelling is rejected before the typed solver");
        tests.check(parsed.detail.find("unknown unit furlong") != std::string::npos &&
                        untouched.source.value.num == 7,
                    "the source refusal is useful and does not partly assign the problem");

        parsed = parse_unit_conversion_problem("1 m", "parsec", &untouched);
        tests.equal(unit_conversion_parse_outcome_name(parsed.outcome), "invalid target unit",
                    "an unsupported target spelling is rejected separately");
        tests.check(parsed.detail.find("unknown unit parsec") != std::string::npos &&
                        untouched.source.value.num == 7,
                    "the target refusal is useful and leaves the typed problem untouched");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "1 m^2", "s^2", &problem))
            return;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "dimension mismatch",
                    "incompatible powered dimensions are rejected");
        tests.check(!conversion.has_value && conversion.cost.rewrites == 0,
                    "the dimension check happens before conversion arithmetic");
        tests.equal(derivation_status_name(conversion.status), "invalid input",
                    "the mismatch is not reported as a numerical failure");
        tests.check(has_rule(derivation, "unit.convert.check-dimension") &&
                        derivation.at(1).has_failed_verification(),
                    "the rejected dimension claim is retained as provenance");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "1 km^2", "m^2", &problem))
            return;
        problem.source.value.num = std::numeric_limits<int64_t>::max();
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "arithmetic overflow",
                    "an exact converted value that cannot fit is refused explicitly");
        tests.check(!conversion.has_value && conversion.value_text.empty(),
                    "overflow exposes no partial answer");
    }

    {
        // No input reaches this refusal, because the rounding and the check that reads it back come
        // from the same exact value. The name is pinned here and the path is driven by mutation.
        tests.equal(unit_conversion_outcome_name(UnitConversionOutcome::VerificationFailed),
                    "verification failed",
                    "a rounding that fails its check is named as a failed check rather than as an "
                    "overflow the student never had");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "3 cm^2", "mm^2", &problem))
            return;
        Budget budget;
        budget.poll = cancel_now;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation, budget);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "cancelled",
                    "cancellation is reported explicitly");
        tests.check(!conversion.has_value && derivation.size() == 0,
                    "cancellation exposes no answer or partial derivation");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "3 cm^2", "mm^2", &problem))
            return;
        Budget budget;
        budget.max_steps = 1;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation, budget);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "resource exceeded",
                    "a step budget halt is reported explicitly");
        tests.check(!conversion.has_value && derivation.size() == 0,
                    "a step halt rewinds its plan");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "3 cm^2", "mm^2", &problem))
            return;
        Budget budget;
        budget.max_rewrites = 0;
        Derivation derivation;
        const UnitConversionResult conversion = solve(problem, &derivation, budget);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "resource exceeded",
                    "a rewrite budget halt is reported explicitly");
        tests.check(!conversion.has_value && derivation.size() == 0,
                    "a rewrite halt exposes no partial conversion");
    }

    {
        UnitConversionProblem problem;
        if (!parse_problem(tests, "3 cm^2", "mm^2", &problem))
            return;
        Limits limits;
        limits.max_nodes = 0;
        Arena arena(limits);
        Derivation derivation;
        const UnitConversionResult conversion =
            solve_unit_conversion(arena, derivation, problem);
        tests.equal(unit_conversion_outcome_name(conversion.outcome), "resource exceeded",
                    "an expression arena limit is reported explicitly");
        tests.check(!conversion.has_value && derivation.size() == 0,
                    "an arena limit exposes no answer or partial derivation");
    }

    {
        UnitConversionProblem resistance;
        UnitConversionProblem power;
        UnitConversionProblem current;
        if (!parse_problem(tests, "2.5 kohm", "ohm", &resistance) ||
            !parse_problem(tests, "2.5 kW", "W", &power) ||
            !parse_problem(tests, "2.5 mA", "A", &current))
            return;
        Arena arena;
        Derivation resistance_steps;
        Derivation power_steps;
        Derivation current_steps;
        solve_unit_conversion(arena, resistance_steps, resistance, Budget());
        solve_unit_conversion(arena, power_steps, power, Budget());
        solve_unit_conversion(arena, current_steps, current, Budget());
        const std::string resistance_model = recorded_model(arena, resistance_steps);
        const std::string power_model = recorded_model(arena, power_steps);
        const std::string current_model = recorded_model(arena, current_steps);
        tests.check(resistance_model.find("dimension(2, 1, -3, -2)") != std::string::npos,
                    "a resistance records every base exponent it has, current included");
        tests.check(power_model.find("dimension(2, 1, -3, 0)") != std::string::npos,
                    "a watt records the same three mechanical exponents with no current, so it "
                    "cannot be read as the ohm it used to share a node with");
        tests.check(current_model.find("dimension(0, 0, 0, 1)") != std::string::npos,
                    "and an ampere records a current rather than a dimensionless number");
    }
}

}
