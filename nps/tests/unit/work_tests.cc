#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "nps/physics/work.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Vector parsed_vector(const char *text, const char *frame = "lab") {
    Vector vector;
    std::string error;
    if (!parse_vector(text, &vector, &error)) {
        vector.rank = 0;
        vector.frame.name = "parse failed: " + error;
        return vector;
    }
    vector.frame.name = frame;
    return vector;
}

WorkProblem problem(const Vector &force, const Vector &displacement,
                    WorkForceProfile profile = WorkForceProfile::Constant) {
    WorkProblem input;
    input.force = force;
    input.displacement = displacement;
    input.force_profile = profile;
    return input;
}

struct Run {
    explicit Run(const WorkProblem &problem, const Budget &budget = Budget(),
                 Backend *backend = nullptr, const Limits &limits = Limits())
        : arena(limits), result(solve_work(arena, derivation, problem, budget, backend)) {}

    Arena arena;
    Derivation derivation;
    WorkResult result;
};

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return true;
    }
    return false;
}

size_t rule_index(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return index;
    }
    return derivation.size();
}

std::string rule_evidence(const Derivation &derivation, const char *rule) {
    std::string detail;
    const size_t index = rule_index(derivation, rule);
    if (index == derivation.size())
        return detail;
    for (const VerificationRecord &check :
         derivation.at(static_cast<StepId>(index)).verifications) {
        // The outcome ahead of the sentence, in verification_transcript's format. Without it a
        // passed check and a failed one carrying the same detail read identically here.
        detail += verification_outcome_name(check.outcome);
        detail += ", ";
        detail += check.detail;
    }
    return detail;
}

const CheckPayload *check_payload(const Derivation &derivation, const char *rule) {
    const size_t index = rule_index(derivation, rule);
    if (index == derivation.size())
        return nullptr;
    return derivation.check(static_cast<StepId>(index));
}

bool exact_value(const WorkResult &result, int64_t numerator, int64_t denominator) {
    return result.has_value && result.quantity.value.num == numerator &&
           result.quantity.value.den == denominator;
}

bool cancel_now(void *) { return true; }

class ScriptedBackend : public Backend {
  public:
    explicit ScriptedBackend(const std::string &reply) : reply_(reply) {}

    bool eval(const std::string &command, std::string *out, std::string *) override {
        commands.push_back(command);
        *out = reply_;
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::string reply_;
};

class FailingBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *, std::string *error) override {
        commands.push_back(command);
        *error = "backend down";
        return false;
    }

    std::vector<std::string> commands;
};

}

void run_work_tests(TestSink &t) {
    const Vector force = parsed_vector("(3, 4) kg*m/s^2");
    const Vector displacement = parsed_vector("(2, 1) m");

    {
        Run solved(problem(force, displacement));
        t.evidence("PHYS-009", work_outcome_name(solved.result.outcome), "solved",
                   "matching component vectors solve work");
        t.equal(solved.result.value_text, "10", "the exact dot product is reported");
        t.equal(solved.result.unit_text, "kg m^2/s^2", "work is reported in derived SI units");
        t.check(exact_value(solved.result, 10, 1), "the typed answer remains an exact rational");
        t.equal(work_sign_name(solved.result.sign), "positive", "positive work is classified");
        t.check(solved.result.interpretation.find("in the displacement direction") !=
                    std::string::npos,
                "positive work states the force direction relation");
        t.equal(derivation_status_name(solved.result.status), "solved and verified",
                "all local work claims are verified");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.work.constant-force-dot-product",
                "the context identifies the work family");
        t.check(solved.result.equation != kNoNode && solved.result.substituted != kNoNode &&
                    solved.result.value != kNoNode,
                "the typed answer retains its equation, substitution and value nodes");
        t.check(solved.result.cost.backend_calls == 0,
                "the local producer does not invent an implicit backend call");
        t.check(solved.result.cost.steps == solved.derivation.size(),
                "every charged work step has a provenance record");
    }

    {
        Run solved(problem(parsed_vector("(2, -3) kg*m/s^2"),
                           parsed_vector("(-1, 4) m")));
        t.equal(solved.result.value_text, "-14", "opposing components produce negative work");
        t.equal(work_sign_name(solved.result.sign), "negative", "negative work is classified");
        t.check(solved.result.interpretation.find("opposite") != std::string::npos,
                "negative work states the opposing direction relation");
    }
    {
        Run solved(problem(parsed_vector("(3, 4) kg*m/s^2"),
                           parsed_vector("(-4, 3) m")));
        t.equal(solved.result.value_text, "0", "perpendicular components produce zero work");
        t.equal(work_sign_name(solved.result.sign), "zero", "zero work is classified");
        t.check(solved.result.interpretation.find("no net component") != std::string::npos,
                "zero work states the component relation without overclaiming geometry");
    }
    {
        Run solved(problem(parsed_vector("(1, 2, 3) kg*m/s^2"),
                           parsed_vector("(4, 5, 6) m")));
        t.equal(solved.result.value_text, "32", "three-dimensional component vectors are supported");
        t.check(exact_value(solved.result, 32, 1), "the 3D result remains exact");
    }

    {
        Run solved(problem(parsed_vector("(1000, 0) g*m/s^2"),
                           parsed_vector("(50, 0) cm")));
        t.equal(solved.result.value_text, "0.5",
                "non-default force and displacement units convert exactly");
        t.check(exact_value(solved.result, 1, 2), "the converted result is exact internally");
        t.check(has_rule(solved.derivation, "physics.work.convert-si"),
                "an actual SI conversion has provenance");
        const size_t inputs = rule_index(solved.derivation, "physics.work.check-input-dimensions");
        const size_t conversion = rule_index(solved.derivation, "physics.work.convert-si");
        const size_t product = rule_index(solved.derivation, "physics.work.check-result-dimension");
        const CheckPayload *derived =
            check_payload(solved.derivation, "physics.work.check-result-dimension");
        t.evidence("PHYS-002",
                   inputs < conversion && conversion < product &&
                       product < solved.derivation.size() && derived != nullptr &&
                       derived->expected_relation == "L^2 M T^-2" &&
                       derived->observed_result == "L^2 M T^-2",
                   "the work derivation checks the input dimensions, substitutes the SI values, "
                   "then derives the product dimension again from what it substituted");
    }
    {
        Run solved(problem(parsed_vector("(2.50, 0) kg*m/s^2"),
                           parsed_vector("(3.40, 0) m")));
        t.equal(solved.result.value_text, "8.50", "measured precision is applied only at reporting");
        t.check(exact_value(solved.result, 17, 2), "rounding does not replace the exact answer");
        t.check(solved.result.quantity.precision.kind == NumberKind::Measured &&
                    solved.result.quantity.precision.significant_digits == 3,
                "the answer retains combined precision metadata");
        t.check(has_rule(solved.derivation, "physics.work.significant-figures"),
                "the final-only reporting rule is recorded");
        t.check(rule_index(solved.derivation, "physics.work.significant-figures") >
                    rule_index(solved.derivation, "physics.work.check-candidate"),
                "reporting follows exact candidate verification");
        t.equal(rule_evidence(solved.derivation, "physics.work.significant-figures"),
                "passed, 8.50 is within half a unit in the last place of 8.5",
                "the report step's evidence names its outcome and the two values it compared, so "
                "neither a constant detail nor a changed outcome can stand in for the comparison");
    }
    {
        Run solved(problem(parsed_vector("(2.5, 0) kg*m/s^2"),
                           parsed_vector("(3.40, 0) m")));
        t.equal(solved.result.value_text, "8.5",
                "the fewer measured significant figures control the report");
    }
    {
        // Work is a sum of products, so its report follows the sum's last place. 5.0 plus 4.96 is
        // 9.96 to the tenths, which is 10.0: a figure count taken from the exact value wrote 10.
        Run solved(problem(parsed_vector("(5.0, 1.0) kg*m/s^2"),
                           parsed_vector("(1.0, 4.96) m")));
        t.equal(solved.result.value_text, "10.0",
                "a work sum that rounds up into a new digit keeps its tenths place");
        t.check(solved.result.quantity.precision.kind == NumberKind::Measured &&
                    solved.result.quantity.precision.last_significant_decimal_place == -1,
                "and the answer's precision names that place");
        t.equal(rule_evidence(solved.derivation, "physics.work.significant-figures"),
                "passed, 10.0 is within half a unit in the last place of 9.96",
                "the report step compares the rounding it shows");
    }
    {
        // The product half of the same carry, reaching the printed answer. 2.0 times 4.98 is 9.96
        // to two figures, which is 10 in the units place, so the sum beside 1.0 times 1.0 reports
        // 11. A product place taken from the exact value put it in the tenths and printed 11.0.
        Run solved(problem(parsed_vector("(2.0, 1.0) kg*m/s^2"),
                           parsed_vector("(4.98, 1.0) m")));
        t.equal(solved.result.value_text, "11",
                "a product that carries moves the reported place up, not down");
        t.check(solved.result.quantity.precision.kind == NumberKind::Measured &&
                    solved.result.quantity.precision.last_significant_decimal_place == 0,
                "and the answer's precision names the units place");
    }

    {
        Run clarification(problem(force, displacement, WorkForceProfile::Unspecified));
        t.equal(work_outcome_name(clarification.result.outcome), "clarification required",
                "an unspecified force profile asks for the missing applicability fact");
        t.equal(derivation_status_name(clarification.result.status), "clarification required",
                "the derivation distinguishes missing applicability from invalid input");
        t.check(has_rule(clarification.derivation, "physics.work.check-applicability"),
                "the unresolved physical-law condition is recorded");
        t.check(!has_rule(clarification.derivation, "physics.work.constant-force-definition"),
                "the law does not fire before applicability is established");
    }
    {
        Run refused(problem(force, displacement, WorkForceProfile::Variable));
        t.equal(work_outcome_name(refused.result.outcome), "law not applicable",
                "a declared variable force refuses the constant-force shortcut");
        t.check(refused.result.detail.find("path integral") != std::string::npos,
                "the refusal names the required model class");
        t.check(!has_rule(refused.derivation, "physics.work.constant-force-definition"),
                "a negated applicability condition prevents law application");
    }
    {
        WorkProblem input = problem(force, displacement);
        input.force_profile = static_cast<WorkForceProfile>(99);
        Run refused(input);
        t.equal(work_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid force-profile enum is rejected");
        t.check(refused.derivation.size() == 0,
                "invalid typed metadata never enters a derivation");
    }

    {
        Run refused(problem(force, parsed_vector("(2, 1, 0) m")));
        t.equal(work_outcome_name(refused.result.outcome), "rank mismatch",
                "vectors of different supported ranks are rejected");
        t.check(!has_rule(refused.derivation, "physics.work.constant-force-definition"),
                "rank failure precedes law application");
    }
    {
        Vector invalid_rank = force;
        invalid_rank.rank = 1;
        Run refused(problem(invalid_rank, displacement));
        t.equal(work_outcome_name(refused.result.outcome), "rank mismatch",
                "a rank outside two and three is rejected");
    }
    {
        Vector unframed = force;
        unframed.frame.name.clear();
        Run refused(problem(unframed, displacement));
        t.equal(work_outcome_name(refused.result.outcome), "frame undeclared",
                "an empty force frame is rejected explicitly");
        t.check(!has_rule(refused.derivation, "physics.work.constant-force-definition"),
                "an undeclared frame prevents law application");
    }
    {
        Vector ramp_displacement = displacement;
        ramp_displacement.frame.name = "ramp";
        Run refused(problem(force, ramp_displacement));
        t.equal(work_outcome_name(refused.result.outcome), "frame mismatch",
                "different declared frames are not silently coerced");
        t.check(refused.result.detail.find("explicit basis transformation") != std::string::npos,
                "the mismatch names the missing frame operation");
    }

    {
        Run refused(problem(parsed_vector("(3, 4) m"), displacement));
        t.equal(work_outcome_name(refused.result.outcome), "dimension mismatch",
                "a non-force first vector is rejected by role");
        t.check(refused.result.detail.find("force L M T^-2") != std::string::npos,
                "the mismatch states the required force dimension");
        t.check(!has_rule(refused.derivation, "physics.work.constant-force-definition"),
                "dimension failure precedes law application");
    }
    {
        Run refused(problem(force, parsed_vector("(2, 1) s")));
        t.equal(work_outcome_name(refused.result.outcome), "dimension mismatch",
                "a non-length displacement is rejected by role");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m"),
                            parsed_vector("(2, 1) kg*m/s^2")));
        t.equal(work_outcome_name(refused.result.outcome), "dimension mismatch",
                "swapping the two roles is rejected even though dimensions would multiply to work");
    }

    {
        WorkProblem input = problem(force, displacement);
        input.force.x.den = 0;
        Run refused(input);
        t.equal(work_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid component rational is rejected");
        t.check(refused.derivation.size() == 0,
                "invalid exact input never enters the derivation graph");
    }
    {
        WorkProblem input = problem(force, displacement);
        input.force.unit.scale.den = 0;
        Run refused(input);
        t.equal(work_outcome_name(refused.result.outcome), "invalid problem",
                "an invalid unit scale is rejected");
    }
    {
        WorkProblem input = problem(force, displacement);
        input.force.precision.kind = NumberKind::Measured;
        input.force.precision.significant_digits = 0;
        Run refused(input);
        t.equal(work_outcome_name(refused.result.outcome), "invalid problem",
                "inconsistent precision metadata is rejected");
    }
    {
        WorkProblem input = problem(force, displacement);
        input.force.x.num = 6;
        input.force.x.den = 2;
        Run solved(input);
        t.equal(solved.result.value_text, "10",
                "a valid noncanonical component is normalized before use");
    }
    {
        Vector inactive = parsed_vector("(0, 0) kg*km/s^2");
        inactive.z.num = std::numeric_limits<int64_t>::max();
        Run solved(problem(inactive, displacement));
        t.equal(work_outcome_name(solved.result.outcome), "solved",
                "an inactive 2D z slot is not converted or evaluated");
        t.equal(solved.result.value_text, "0", "the inactive component cannot affect work");
    }

    {
        Vector huge_force = parsed_vector("(1, 0) kg*km/s^2");
        huge_force.x.num = std::numeric_limits<int64_t>::max();
        Run overflow(problem(huge_force, displacement));
        t.equal(work_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "SI conversion overflow is explicit");
        t.check(!overflow.result.has_value, "conversion overflow exposes no partial answer");
    }
    {
        Vector huge_force = force;
        huge_force.x.num = std::numeric_limits<int64_t>::max();
        huge_force.y.num = 0;
        Run overflow(problem(huge_force, parsed_vector("(2, 0) m")));
        t.equal(work_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "a dot-product term overflow is explicit");
    }
    {
        Vector huge_force = force;
        huge_force.x.num = std::numeric_limits<int64_t>::max();
        huge_force.y.num = std::numeric_limits<int64_t>::max();
        Run overflow(problem(huge_force, parsed_vector("(1, 1) m")));
        t.equal(work_outcome_name(overflow.result.outcome), "arithmetic overflow",
                "dot-product accumulation overflow is explicit");
    }

    {
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem(force, displacement), budget);
        t.evidence("PERF-003", work_outcome_name(cancelled.result.outcome), "cancelled",
                   "an existing cancellation stops work immediately");
        t.check(cancelled.derivation.size() == 0 && !cancelled.result.has_value,
                "cancellation leaves no partial provenance or answer");
    }
    {
        Budget budget;
        budget.max_steps = 2;
        Run stopped(problem(force, displacement), budget);
        t.equal(work_outcome_name(stopped.result.outcome), "resource exceeded",
                "the work solver enforces its step budget");
        t.equal(stopped.result.detail, "step limit", "the exhausted step resource is named");
        t.check(stopped.derivation.size() == 0, "a step halt rewinds partial provenance");
    }
    {
        Budget budget;
        budget.max_rewrites = 0;
        Run stopped(problem(force, displacement), budget);
        t.equal(work_outcome_name(stopped.result.outcome), "resource exceeded",
                "the work solver enforces its rewrite budget");
        t.equal(stopped.result.detail, "rewrite limit", "the exhausted rewrite resource is named");
    }
    {
        Limits limits;
        limits.max_nodes = 2;
        Run stopped(problem(force, displacement), Budget(), nullptr, limits);
        t.equal(work_outcome_name(stopped.result.outcome), "resource exceeded",
                "Arena exhaustion is a typed resource outcome");
        t.check(stopped.derivation.size() == 0 && !stopped.result.has_value,
                "Arena exhaustion exposes no proof or answer");
    }

    {
        ScriptedBackend backend("10");
        Run solved(problem(force, displacement), Budget(), &backend);
        t.equal(work_outcome_name(solved.result.outcome), "solved",
                "an agreeing exact backend preserves the local result");
        t.check(solved.result.backend_value != kNoNode,
                "the typed result retains the independent exact value");
        t.check(solved.result.cost.backend_calls == 1 && backend.commands.size() == 1,
                "one allowlisted backend request is charged");
        const std::string dot_request =
            backend.commands.empty() ? std::string("no backend request") : backend.commands[0];
        t.equal(dot_request, "dotprod([3,4],[2,1])",
                "the backend receives only the adapter-built dot request");
        t.check(has_rule(solved.derivation, "physics.work.giac-cross-check"),
                "the independent check is recorded separately");
    }
    {
        ScriptedBackend backend("11");
        Run refused(problem(force, displacement), Budget(), &backend);
        t.equal(work_outcome_name(refused.result.outcome), "verification failed",
                "a disagreeing exact backend withholds the answer");
        t.check(!refused.result.has_value && refused.result.value == kNoNode,
                "backend disagreement exposes no local answer");
        t.check(has_rule(refused.derivation, "physics.work.evaluate-dot") &&
                    has_rule(refused.derivation, "physics.work.giac-cross-check"),
                "local production and failed independent verification remain auditable");
    }
    {
        // The cross-check canonicalizes both sides, which allocates, so the arena can run out after
        // the backend has already answered. That has to be a resource halt rather than a verdict:
        // before this it folded into agrees == false and told the learner Giac disagreed.
        bool every_failure_refused = true;
        bool saw_failure_after_the_backend_answered = false;
        bool saw_success = false;
        for (size_t max_nodes = 0; max_nodes <= 512; ++max_nodes) {
            Limits limits;
            limits.max_nodes = max_nodes;
            ScriptedBackend backend("10+0+0+0+0+0+0+0+0+0+0+0+0+0+0+0+0+0+0+0");
            Run run(problem(force, displacement), Budget(), &backend, limits);
            if (run.arena.failed()) {
                every_failure_refused = every_failure_refused && !run.result.has_value &&
                                        run.result.outcome != WorkOutcome::VerificationFailed;
                saw_failure_after_the_backend_answered =
                    saw_failure_after_the_backend_answered || !backend.commands.empty();
            } else if (run.result.outcome == WorkOutcome::Solved) {
                saw_success = true;
            }
        }
        t.check(every_failure_refused && saw_failure_after_the_backend_answered && saw_success,
                "an arena that runs out while reading or comparing the backend reply is a resource "
                "halt, never a disagreement and never an answer");
    }

    // VER-014 asks that unknown, timeout, malformed and unavailable are not treated as passed, and
    // that the derivation becomes partial. Not that they are treated as failed: the check never ran,
    // and physics.work.check-candidate has already checked the exact dot product locally. Before
    // this, an optional backend that stumbled cost the learner an answer that solving without any
    // backend at all returns verified.
    {
        ScriptedBackend backend("10.0");
        Run partial(problem(force, displacement), Budget(), &backend);
        t.equal(work_outcome_name(partial.result.outcome), "solved",
                "an approximate backend cannot certify the local result and cannot destroy it");
        t.check(exact_value(partial.result, 10, 1),
                "the exact local work value survives a backend that will not certify it");
        t.equal(derivation_status_name(partial.result.status), "solved but unchecked",
                "an inconclusive cross-check leaves the derivation partial");
        t.equal(rule_evidence(partial.derivation, "physics.work.giac-cross-check"),
                "inconclusive, Giac returned approximate",
                "the record says the check could not be made, not that it failed");
    }
    {
        ScriptedBackend backend("not valid !");
        Run partial(problem(force, displacement), Budget(), &backend);
        t.evidence("VER-014",
                   derivation_status_name(partial.result.status) == std::string("solved but unchecked") &&
                       rule_evidence(partial.derivation, "physics.work.giac-cross-check")
                               .find("inconclusive, Giac returned malformed result") == 0 &&
                       exact_value(partial.result, 10, 1),
                   "malformed backend output is never passed, never becomes the answer, and leaves "
                   "the derivation partial rather than failed");
    }
    {
        FailingBackend backend;
        Run partial(problem(force, displacement), Budget(), &backend);
        t.equal(work_outcome_name(partial.result.outcome), "solved",
                "a backend transport failure does not withdraw the local answer");
        t.check(exact_value(partial.result, 10, 1),
                "the answer a backend-free solve would give is the answer a broken backend gives");
        t.check(rule_evidence(partial.derivation, "physics.work.giac-cross-check")
                        .find("inconclusive, Giac returned backend error") == 0,
                "the record keeps the adapter tag and calls it inconclusive");
    }
    {
        Budget budget;
        budget.max_backend_calls = 0;
        ScriptedBackend backend("10");
        Run stopped(problem(force, displacement), budget, &backend);
        t.equal(work_outcome_name(stopped.result.outcome), "resource exceeded",
                "a backend-call budget stops the optional cross-check");
        t.equal(stopped.result.detail, "backend call limit",
                "the backend budget failure is named");
        t.check(backend.commands.empty() && stopped.derivation.size() == 0,
                "a denied backend request is neither sent nor partially recorded");
    }

    {
        Run solved(problem(force, displacement));
        const size_t applicability =
            rule_index(solved.derivation, "physics.work.check-applicability");
        const size_t dimensions =
            rule_index(solved.derivation, "physics.work.check-input-dimensions");
        const size_t law =
            rule_index(solved.derivation, "physics.work.constant-force-definition");
        const size_t local_check =
            rule_index(solved.derivation, "physics.work.check-candidate");
        const size_t sign = rule_index(solved.derivation, "physics.work.interpret-sign");
        t.check(applicability < law && dimensions < law,
                "physical applicability and dimensions are recorded before law application");
        t.check(law < local_check && local_check < sign,
                "law application, exact checking and interpretation stay ordered");
        t.evidence("PHYS-025", applicability < law && dimensions < law && law < local_check && local_check < sign &&
                    has_rule(solved.derivation, "physics.work.check-rank") &&
                    has_rule(solved.derivation, "physics.work.check-frame-declared") &&
                    has_rule(solved.derivation, "physics.work.check-frame-match") &&
                    has_rule(solved.derivation, "physics.work.check-result-dimension") &&
                    has_rule(solved.derivation, "physics.work.evaluate-dot"),
                "rank, frame, dimensional and arithmetic provenance is complete");
        t.check(solved.derivation.all_verified_from(0),
                "every successful no-backend work claim has passing evidence");
    }
}

}
