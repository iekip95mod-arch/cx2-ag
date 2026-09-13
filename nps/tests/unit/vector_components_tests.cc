#include <string>
#include <utility>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/physics/vector_components.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

class SequenceBackend : public Backend {
  public:
    explicit SequenceBackend(std::vector<std::string> replies) : replies_(std::move(replies)) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "no scripted reply";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    size_t next_ = 0;
};

class InvalidNodeBackend : public Backend {
  public:
    bool eval(const std::string &, std::string *, std::string *) override { return false; }

    bool typed(const Request &, Arena &, TypedResult *out) override {
        out->tag = ResultTag::Exact;
        out->value = 4000000000u;
        return true;
    }
};

NodeId parsed(Arena &arena, const std::string &text) {
    const ParseResult result = parse(arena, text);
    return result.ok() ? result.root : kNoNode;
}

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        if (derivation.at(static_cast<StepId>(i)).rule_id == rule)
            return true;
    }
    return false;
}

bool has_obligation(const Derivation &derivation, const char *obligation) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        const Step &step = derivation.at(static_cast<StepId>(i));
        for (const ProofObligation &candidate : step.proof_obligations) {
            if (candidate.id == obligation)
                return true;
        }
    }
    return false;
}

// The first verification on a rule's step, by name. Two rules in one derivation can be verified on
// evidence of different kinds, and a status alone cannot tell them apart.
std::string check_outcome(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        const Step &step = derivation.at(static_cast<StepId>(i));
        if (step.rule_id == rule && !step.verifications.empty())
            return verification_outcome_name(step.verifications[0].outcome);
    }
    return "no such rule";
}

std::string check_detail(const Derivation &derivation, const char *rule) {
    for (size_t i = 0; i < derivation.size(); ++i) {
        const Step &step = derivation.at(static_cast<StepId>(i));
        if (step.rule_id == rule && !step.verifications.empty())
            return step.verifications[0].detail;
    }
    return "no such rule";
}

void set_length_unit(MagnitudeAngleExpr *input) {
    input->unit.text = "m";
    input->unit.dimension = {1, 0, 0};
    input->unit.scale = {1, 1};
}

bool cancel_now(void *) { return true; }

}  // namespace

void run_vector_components_tests(TestSink &t) {
    for (const AngleUnit unit : {AngleUnit::Radians, AngleUnit::Degrees}) {
        for (const bool measured : {false, true}) {
            for (const bool expression : {false, true}) {
                Arena arena;
                Derivation derivation;
                SequenceBackend backend(std::vector<std::string>(10, "0"));
                Vector input;
                input.x = {0, 1};
                input.y = {0, 1};
                input.frame.name = "lab";
                input.unit.text = "m";
                input.unit.dimension = {1, 0, 0};
                input.unit.scale = {1, 1};
                if (measured) {
                    input.precision.kind = NumberKind::Measured;
                    input.precision.significant_digits = 3;
                }
                VectorExpr expr = vector_expr_from_exact(arena, input);
                expr.x = parsed(arena, "1-1");
                expr.y = parsed(arena, "0*2");
                const VectorComponentsResult result = expression
                    ? components_to_magnitude_angle(arena, derivation, expr, unit, backend)
                    : components_to_magnitude_angle(arena, derivation, input, unit, backend);
                t.check(result.outcome == VectorComponentsOutcome::InvalidInput &&
                            result.status == DerivationStatus::InvalidInput &&
                            result.detail == "the zero vector has no defined direction" &&
                            !result.has_polar && result.polar.angle == kNoNode,
                        "zero vectors refuse a defined polar direction in either angle unit");
                t.check(backend.commands.size() == (expression ? 0u : 1u) &&
                            !has_rule(derivation, "vec.polar.direction") &&
                            !has_obligation(derivation,
                                            "obl.vector-components.quadrant-direction") &&
                            derivation.context.derivation_status == DerivationStatus::InvalidInput,
                        "zero vectors stop before atan2 and quadrant evidence or approximation");
            }
        }
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0.0", "0", "0.0", "0", "0", "0.0"});
        VectorExpr input;
        input.x = parsed(arena, "0.0-0.0");
        input.y = parsed(arena, "0.0*2");
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::InvalidInput && !result.has_polar &&
                    result.detail == "the zero vector has no defined direction",
                "measured decimal expressions refuse a direction for the zero vector");
        t.check(backend.commands.empty() &&
                    !has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "exactly evaluable decimal zero components stop before backend direction work");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0.0", "0"});
        VectorExpr input;
        input.x = parsed(arena, "pi-pi");
        input.y = parsed(arena, "0.0*2");
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::InvalidInput && !result.has_polar &&
                    result.detail == "the zero vector has no defined direction",
                "symbolic exact-zero expressions refuse a polar direction");
        t.check(backend.commands.size() == 2 &&
                    !has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "a verified symbolic zero magnitude stops before approximation and atan2");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"pi-pi", "0", "0.0", "0", "0", "0.0"});
        VectorExpr input;
        input.x = parsed(arena, "pi-pi");
        input.y = parsed(arena, "0");
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::InvalidInput && !result.has_polar &&
                    result.detail == "the zero vector has no defined direction",
                "an exact equivalent-zero magnitude refuses a polar direction");
        t.check(backend.commands.size() == 3 &&
                    backend.commands[2].find("simplify") == 0 &&
                    backend.commands[2].find("pi") != std::string::npos &&
                    !has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "an unclassified magnitude gets an independent zero check before atan2");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"pi-pi", "0", "pi-pi", "0.0", "0", "0", "0.0"});
        VectorExpr input;
        input.x = parsed(arena, "pi-pi");
        input.y = parsed(arena, "0");
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::VerificationFailed &&
                    result.status == DerivationStatus::VerificationFailed && !result.has_polar &&
                    result.detail == "Giac did not classify the exact magnitude",
                "an equivalent-zero classification cannot stand as nonzero evidence");
        t.check(backend.commands.size() == 3 &&
                    !has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "an inconclusive magnitude classification stops before approximation and atan2");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0.004", "0", "0.0", "0", "0", "0.0"});
        VectorExpr input;
        input.x = parsed(arena, "0.004");
        input.y = parsed(arena, "0");
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 1;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::Solved && result.has_polar &&
                    print(arena, result.polar.magnitude) == "0.0",
                "a nonzero exact magnitude remains directional after final approximation");
        t.check(backend.commands.size() == 6 &&
                    has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "final approximate zero does not trigger exact zero-vector refusal");
    }

    for (const bool vertical : {false, true}) {
        Arena arena;
        Derivation derivation;
        const std::string angle = vertical ? "pi/2" : "0";
        SequenceBackend backend({"0", angle, "0"});
        Vector input;
        input.x = {vertical ? 0 : 1, 1};
        input.y = {vertical ? 1 : 0, 1};
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.has_polar && print(arena, result.polar.magnitude) == "1" &&
                    print(arena, result.polar.angle) == print(arena, parsed(arena, angle)) &&
                    backend.commands.size() == 3 &&
                    has_rule(derivation, "vec.polar.direction") &&
                    has_obligation(derivation, "obl.vector-components.quadrant-direction"),
                "a single zero component retains its defined axis direction and quadrant record");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"sqrt(3)/2", "0", "5*sqrt(3)", "0",
                                 "1/2", "0", "5", "0"});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        input.unit.text = "m/s";
        input.unit.dimension = {1, 0, -1};
        input.unit.scale = {1, 1};

        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.equal(vector_components_outcome_name(result.outcome), "solved",
                "magnitude and degrees convert to components");
        t.check(result.has_components && result.components.frame.name == "lab" &&
                    result.components.unit.text == "m/s" &&
                    result.components.unit.dimension == input.unit.dimension &&
                    result.components.precision.kind == NumberKind::Exact,
                "component conversion retains frame, dimension, unit and precision metadata");
        t.check(print(arena, result.components.x).find("sqrt(3)") != std::string::npos &&
                    print(arena, result.components.y) == "5",
                "component conversion retains exact symbolic and rational values");
        t.check(!backend.commands.empty() && backend.commands[0].find("pi") != std::string::npos,
                "degree conversion sends exact pi to Giac");
        t.evidence("PHYS-025", has_rule(derivation, "vec.components.plan") &&
                    has_rule(derivation, "vec.components.check-frame") &&
                    has_rule(derivation, "vec.components.check-angle-unit") &&
                    has_rule(derivation, "vec.components.check-dimension") &&
                    has_rule(derivation, "vec.components.x") &&
                    has_rule(derivation, "vec.components.y") &&
                    has_rule(derivation, "vec.components.report-precision"),
                "component conversion records plan, precondition, law and precision rules");
        t.check(has_obligation(derivation, "obl.vector-components.frame-declared") &&
                    has_obligation(derivation, "obl.vector-components.angle-unit-explicit") &&
                    has_obligation(derivation, "obl.vector-components.dimensions-preserved") &&
                    has_obligation(derivation, "obl.vector-components.component-relations") &&
                    has_obligation(derivation, "obl.vector-components.precision-final"),
                "component conversion exposes its proof obligations");
        t.check(derivation.roots().size() == 1 && derivation.roots()[0] == 0 &&
                    derivation.context.problem_family_id ==
                        "physics.vectors.magnitude-components.two-dimension" &&
                    derivation.context.angle_convention.find("degrees") != std::string::npos &&
                    derivation.context.derivation_status == DerivationStatus::SolvedAndCorroborated,
                "component conversion records one rooted derivation and reproducible context");
        t.check(check_outcome(derivation, "vec.components.x") == "inconclusive" &&
                    check_outcome(derivation, "vec.components.y") == "inconclusive",
                "and both components rest on the backend agreeing with itself");
    }

    {
        // The direction has no native value to compare against, so the zero check asks the engine
        // that produced the angle whether the angle is right. Answering 99 and then agreeing with
        // itself used to come back solved and verified, with the magnitude beside it verified on a
        // native 5. Only the self-checked step moves, which is what makes this a split rather than
        // a blanket downgrade.
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0", "99", "0", "99", "0"});
        Vector input;
        input.x = {-3, 1};
        input.y = {-4, 1};
        input.rank = 2;
        input.frame.name = "ground";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Degrees, backend);
        t.equal(vector_components_outcome_name(result.outcome), "solved",
                "a backend that never contradicts itself still reaches an answer");
        t.check(result.has_polar && print(arena, result.polar.angle) == "99",
                "and the direction it invented for (-3, -4) is the one reported");
        t.check(derivation.context.derivation_status == DerivationStatus::SolvedAndCorroborated,
                "so the run may not be read as a verified walkthrough");
        t.equal(check_outcome(derivation, "vec.polar.magnitude"), "passed",
                "the magnitude keeps a passing check, because 5 came from us and Giac only judged it");
        t.equal(check_outcome(derivation, "vec.polar.direction"), "inconclusive",
                "while the direction records what one engine agreeing with itself is worth");
        t.check(check_detail(derivation, "vec.polar.magnitude").find("independent") !=
                        std::string::npos &&
                    check_detail(derivation, "vec.polar.direction").find("independent") ==
                        std::string::npos,
                "and the word independent survives only where it is true");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend(
            {"0", "atan2(-4,-3)", "0", "atan2(-4,-3)*180/pi", "0"});
        Vector input;
        input.x = {-3, 1};
        input.y = {-4, 1};
        input.rank = 2;
        input.frame.name = "ground";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Degrees, backend);
        t.check(result.has_polar && print(arena, result.polar.magnitude) == "5",
                "the exact Rational vector fast path reuses the exact magnitude");
        t.check(result.polar.angle_unit == AngleUnit::Degrees &&
                    print(arena, result.polar.angle).find("atan2") != std::string::npos &&
                    print(arena, result.polar.angle).find("-4") != std::string::npos,
                "the inverse conversion retains an exact negative-quadrant degree expression");
        t.check(result.polar.frame.name == "ground" &&
                    result.polar.unit.dimension == input.unit.dimension &&
                    result.polar.precision.kind == NumberKind::Exact &&
                    result.cost.backend_calls == 5,
                "the inverse conversion retains frame, dimension, precision and backend cost");
        t.check(has_rule(derivation, "vec.polar.check-rank") &&
                    has_rule(derivation, "vec.polar.check-frame") &&
                    has_rule(derivation, "vec.polar.check-angle-unit") &&
                    has_rule(derivation, "vec.polar.check-dimension") &&
                    has_rule(derivation, "vec.polar.magnitude") &&
                    has_rule(derivation, "vec.polar.direction") &&
                    has_rule(derivation, "vec.polar.report-precision"),
                "the inverse conversion records precondition, magnitude, atan2 and precision rules");
        t.check(has_obligation(derivation, "obl.vector-components.rank-two") &&
                    has_obligation(derivation, "obl.vector-components.frame-declared") &&
                    has_obligation(derivation, "obl.vector-components.angle-unit-explicit") &&
                    has_obligation(derivation, "obl.vector-components.dimensions-preserved") &&
                    has_obligation(derivation, "obl.vector-components.magnitude-relation") &&
                    has_obligation(derivation, "obl.vector-components.quadrant-direction") &&
                    has_obligation(derivation, "obl.vector-components.precision-final") &&
                    derivation.context.problem_family_id ==
                        "physics.vectors.magnitude-components.two-dimension",
                "the inverse conversion exposes law obligations and family provenance");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0", "atan2(4,3)", "0"});
        Vector input;
        input.x = {3, 1};
        input.y = {4, 1};
        input.rank = 2;
        input.frame.name = "map";
        input.unit.text = "km";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1000, 1};

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::Solved && result.has_polar &&
                    print(arena, result.polar.magnitude) == "5" &&
                    result.polar.unit.text == "km" && result.cost.backend_calls == 3,
                "the exact vector path verifies and reports magnitude in the component unit");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"1/2", "0", "5", "0", "5.0", "sqrt(3)/2", "0",
                                 "5*sqrt(3)", "0", "8.66"});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10.0");
        input.angle = parsed(arena, "60");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.check(result.has_components && print(arena, result.components.x) == "5.0" &&
                    print(arena, result.components.y) == "8.66" &&
                    result.components.precision.significant_digits == 3,
                "measured components are approximated only after exact formula checks");
        t.check(result.cost.backend_calls == 10 &&
                    has_rule(derivation, "vec.components.report-precision"),
                "measured component reporting meters approximation and records its policy");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0", "5.0", "atan2(4,3)", "0", "0.927"});
        Vector input;
        input.x = {3, 1};
        input.y = {4, 1};
        input.rank = 2;
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        input.precision.kind = NumberKind::Measured;
        input.precision.significant_digits = 3;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.has_polar && print(arena, result.polar.magnitude) == "5.0" &&
                    print(arena, result.polar.angle) == "0.927" &&
                    result.polar.precision.significant_digits == 3,
                "measured polar values are approximated only after exact relation checks");
        t.check(result.cost.backend_calls == 5 && has_rule(derivation, "vec.polar.report-precision"),
                "measured polar reporting meters both final approximations");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        set_length_unit(&input);

        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.check(result.outcome == VectorComponentsOutcome::InvalidInput &&
                    result.detail == "a vector frame is required" && backend.commands.empty() &&
                    has_rule(derivation, "vec.components.check-frame") &&
                    derivation.context.derivation_status == DerivationStatus::InvalidInput,
                "component conversion records and refuses a missing coordinate frame");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({});
        Vector input;
        input.x = {3, 1};
        input.y = {4, 1};
        input.rank = 3;
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend);
        t.check(result.outcome == VectorComponentsOutcome::InvalidInput &&
                    result.detail == "magnitude-angle conversion requires a rank 2 vector" &&
                    backend.commands.empty() && has_rule(derivation, "vec.polar.check-rank") &&
                    derivation.context.derivation_status == DerivationStatus::InvalidInput,
                "polar conversion records and refuses a rank outside its two-dimensional envelope");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend(
            {"sqrt(3)/2", "0", "5*sqrt(3)", "0", "Error: Bad Argument Value"});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.equal(vector_components_outcome_name(result.outcome), "backend failure",
                "a late refused trig operation is explicit backend failure");
        t.check(backend.commands.size() == 5 && !result.has_components && derivation.size() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::DependencyUnavailable,
                "a backend failure rolls back an already-recorded component");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"1/2", "1"});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.equal(vector_components_outcome_name(result.outcome), "verification failed",
                "a nonzero formula cross-check is a verification failure");
        t.check(!result.has_components && derivation.size() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::VerificationFailed,
                "a failed cross-check carries no component vector or partial derivation");
    }

    {
        // VER-012. The backend is scripted to answer the check with the very result it gave for the
        // value, which is the thing the requirement forbids a rule from accepting. The check asks
        // whether a difference is zero, so echoing a non-zero value back cannot answer it, and the
        // component is refused rather than justified by its own source.
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"sqrt(3)/2", "sqrt(3)/2"});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        const bool refused = result.outcome == VectorComponentsOutcome::VerificationFailed &&
                             !result.has_components && derivation.size() == 0;
        const bool two_distinct_questions =
            backend.commands.size() >= 2 && backend.commands[0] != backend.commands[1];
        t.check(refused, "a result echoed back as its own evidence does not stand");
        t.check(two_distinct_questions,
                "because the value and the check are two different questions to the backend");
        t.evidence("VER-012", refused && two_distinct_questions,
                   "the value a rule takes from the backend and the evidence that it is correct "
                   "are separate backend questions, and a backend that answers the check with the "
                   "same result it gave for the value fails the check rather than passing it");
    }

    {
        Arena arena;
        Derivation derivation;
        InvalidNodeBackend backend;
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);

        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend);
        t.check(result.outcome == VectorComponentsOutcome::BackendFailure &&
                    result.detail == "the backend returned an invalid expression reference" &&
                    !result.has_components && derivation.size() == 0,
                "an invalid typed backend expression is refused without exposing partial work");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        Budget budget;
        budget.max_backend_calls = 0;
        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend, budget);
        t.equal(vector_components_outcome_name(result.outcome), "resource exceeded",
                "the backend-call budget halts conversion explicitly");
        // STEP-025: the budget is spent before any conversion runs, so there is nothing checked to
        // keep and the record stays empty. The claim worth asserting is that Giac was never asked.
        t.check(backend.commands.empty() && derivation.all_verified_from(0) &&
                    derivation.context.derivation_status == DerivationStatus::ResourceLimitReached,
                "a zero backend budget never enters Giac or leaves unchecked work behind");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({});
        MagnitudeAngleExpr input;
        input.magnitude = parsed(arena, "10");
        input.angle = parsed(arena, "30");
        input.angle_unit = AngleUnit::Degrees;
        input.frame.name = "lab";
        set_length_unit(&input);
        Budget budget;
        budget.poll = cancel_now;
        const VectorComponentsResult result =
            magnitude_angle_to_components(arena, derivation, input, backend, budget);
        t.equal(vector_components_outcome_name(result.outcome), "cancelled",
                "component conversion honors cancellation before Giac");
        t.check(backend.commands.empty() && derivation.size() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::NotRecorded,
                "cancelled conversion leaves no backend call or partial derivation");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({"0", "Error: Bad Argument Value"});
        Vector input;
        input.x = {-3, 1};
        input.y = {-4, 1};
        input.rank = 2;
        input.frame.name = "ground";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Degrees, backend);
        t.check(result.outcome == VectorComponentsOutcome::BackendFailure &&
                    backend.commands.size() == 2 && !result.has_polar && derivation.size() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::DependencyUnavailable,
                "a polar backend failure rolls back a recorded exact magnitude");
    }

    {
        Arena arena;
        Derivation derivation;
        SequenceBackend backend({});
        Vector input;
        input.x = {3, 1};
        input.y = {4, 1};
        input.rank = 2;
        input.frame.name = "lab";
        input.unit.text = "m";
        input.unit.dimension = {1, 0, 0};
        input.unit.scale = {1, 1};
        Budget budget;
        budget.poll = cancel_now;

        const VectorComponentsResult result = components_to_magnitude_angle(
            arena, derivation, input, AngleUnit::Radians, backend, budget);
        t.check(result.outcome == VectorComponentsOutcome::Cancelled && backend.commands.empty() &&
                    !result.has_polar && derivation.size() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::NotRecorded,
                "cancelled polar conversion leaves no backend call or partial derivation");
    }
}

}  // namespace nps
