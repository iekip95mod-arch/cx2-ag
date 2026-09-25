#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/steps/attempt.h"
#include "nps/steps/linear.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

struct Judged {
    AttemptVerdict verdict;
    bool parsed = false;
};

Judged judge(const std::string &current, const std::string &attempt,
             const std::vector<std::string> &route = {}, const Budget &budget = Budget()) {
    Arena arena;
    Judged out;
    const ParseResult c = parse(arena, current);
    const ParseResult a = parse(arena, attempt);
    std::vector<NodeId> states;
    bool ok = c.ok() && a.ok();
    for (const std::string &text : route) {
        const ParseResult r = parse(arena, text);
        ok = ok && r.ok();
        states.push_back(r.root);
    }
    if (!ok)
        return out;
    out.parsed = true;
    out.verdict = judge_attempt(arena, c.root, a.root, arena.symbol("x"), states, budget);
    return out;
}

std::string equivalence(const Judged &j) {
    return j.parsed ? attempt_equivalence_name(j.verdict.equivalence) : "unparsed";
}

std::string usefulness(const Judged &j) {
    return j.parsed ? attempt_usefulness_name(j.verdict.usefulness) : "unparsed";
}

bool cancel_now(void *) { return true; }

void test_equivalence(TestSink &t) {
    const Judged reordered = judge("2*x + 3", "3 + x*2");
    t.evidence("STEP-013", equivalence(reordered), "equivalent",
               "an attempt that only reorders the terms is equivalent");
    t.equal(evidence_strength_name(reordered.verdict.strength),
            evidence_strength_name(EvidenceStrength::SymbolicallyEquivalentUnderAssumptions),
            "an identical canonical form is an exact decision");
    t.equal(reordered.verdict.method, "identical canonical form", "and names the comparison it made");

    const Judged balanced = judge("2*x + 3 = 7", "2*x = 4");
    t.evidence("STEP-013", equivalence(balanced), "equivalent",
               "subtracting three from both sides keeps the one solution");
    t.equal(balanced.verdict.method, "comparison of the single solutions", "a linear pair is decided by its solution");

    const Judged scaled = judge("2*x + 3 = 7", "4*x + 6 = 14");
    t.equal(equivalence(scaled), "equivalent", "scaling both sides keeps the solution");

    const Judged slipped = judge("2*x + 3 = 7", "2*x = 10");
    t.evidence("STEP-013", equivalence(slipped), "not equivalent",
               "adding where the step needed a subtraction changes the solution");
    t.equal(evidence_strength_name(slipped.verdict.strength),
            evidence_strength_name(EvidenceStrength::Failed), "a changed solution is a failed check");
    t.equal(slipped.verdict.detail, "the attempt's solution is 5 and the state's is 2",
            "and says which solutions differ");

    const Judged expanded = judge("(x + 1)^2", "x^2 + 2*x + 1");
    t.evidence("STEP-013", equivalence(expanded), "corroborated",
               "an expansion the canonical form does not reach is corroborated rather than proven");
    t.equal(evidence_strength_name(expanded.verdict.strength),
            evidence_strength_name(EvidenceStrength::NumericallyCorroborated),
            "sample agreement is reported at the corroboration strength");
    t.check(expanded.verdict.method == "exact agreement at sample values",
            "and says it came from samples");

    const Judged dropped = judge("(x + 1)^2", "x^2 + 1");
    t.equal(equivalence(dropped), "not equivalent", "dropping the middle term is caught");
    t.check(dropped.verdict.detail.find("differ at") != std::string::npos,
            "at the sample where the two first differ");

    const Judged factored = judge("x^2 - 5*x + 6 = 0", "(x - 2)*(x - 3) = 0");
    t.equal(equivalence(factored), "corroborated",
            "a factored quadratic whose difference of sides agrees is corroborated");

    const Judged rescaled = judge("x^2 - 5*x + 6 = 0", "2*x^2 - 10*x + 12 = 0");
    t.equal(equivalence(rescaled), "not comparable",
            "a quadratic scaled by two is beyond what this engine decides");

    const Judged kind = judge("2*x + 3 = 7", "2*x + 3");
    t.equal(equivalence(kind), "not equivalent", "an equation rewritten as an expression is not a step");

    const Judged relation = judge("x < 3", "3 > x");
    t.equal(equivalence(relation), "not comparable", "an inequality is not something this judges");

    const Judged opaque = judge("f(x)", "g(x)");
    t.equal(equivalence(opaque), "not comparable",
            "two forms with no value at any sample are not comparable");
}

void test_usefulness(TestSink &t) {
    const std::vector<std::string> route = {"2*x - 4 = 0", "x = 2"};
    const Judged next = judge("2*x + 3 = 7", "2*x + -4 = 0", route);
    t.evidence("STEP-014", usefulness(next), "advances", "the next state on the route advances");
    t.check(next.verdict.reaches == 1, "and reaches the first route state");

    const Judged answer = judge("2*x + 3 = 7", "x = 2", route);
    t.evidence("STEP-014", usefulness(answer), "advances", "jumping to the answer still advances");
    t.check(answer.verdict.reaches == 2, "and reaches the last route state");

    const Judged detour = judge("2*x + 3 = 7", "4*x + 6 = 14", route);
    t.evidence("STEP-014", equivalence(detour) + ", " + usefulness(detour),
               "equivalent, valid not on route",
               "doubling both sides is valid and not a step this walkthrough takes");

    const Judged idle = judge("2*x + 3 = 7", "3 + 2*x = 7", route);
    t.evidence("STEP-014", usefulness(idle), "no progress",
               "rewriting the state it came from makes no progress");

    const Judged wrong = judge("2*x + 3 = 7", "2*x = 10", route);
    t.evidence("STEP-014", usefulness(wrong), "not judged",
               "an attempt that is not valid is not judged for usefulness");

    const Judged unknown = judge("f(x)", "g(x)", {"g(x)"});
    t.equal(usefulness(unknown), "not judged",
            "nor is one whose validity nothing decided, even when it names a route state");
    t.check(unknown.verdict.reaches == 0, "and reaches nothing");

    const Judged corroborated = judge("(x + 1)^2", "x^2 + 2*x + 1", {"x^2 + 2*x + 1"});
    t.equal(usefulness(corroborated), "advances", "a corroborated attempt can still advance");
}

void test_halts(TestSink &t) {
    Budget cancelled;
    cancelled.poll = cancel_now;
    const Judged stopped = judge("2*x + 3 = 7", "2*x = 4", {}, cancelled);
    t.evidence("STEP-013", equivalence(stopped), "cancelled", "a cancelled judgment says so");
    t.equal(usefulness(stopped), "not judged", "and judges nothing further");
    t.equal(equivalence(judge("x + x", "x + x", {}, cancelled)), "cancelled",
            "a cancelled judgment is not decided by the canonical form either");

    Budget starved;
    starved.max_rewrites = 0;
    const Judged limited = judge("2*x + 3 = 7", "2*x = 4", {}, starved);
    t.evidence("STEP-013", equivalence(limited), "resource exceeded",
               "a judgment out of rewrites is a resource limit rather than a verdict");
}

// VER-019. The verdict is its own record, so judging an attempt against a derivation whose check
// failed leaves that check failed whatever the attempt says.
void test_failed_obligations_stay_failed(TestSink &t) {
    Arena arena;
    Derivation d;
    const NodeId equation = parse(arena, "2*x + 3 = 7").root;
    solve_linear(arena, d, equation, arena.symbol("x"));
    Step failed;
    VerificationRecord check;
    check.method = "substitution";
    check.outcome = VerificationOutcome::Failed;
    check.detail = "the left side was not the right side";
    failed.verifications.push_back(check);
    d.add_check(kNoStep, failed, CheckPayload{});
    d.context.derivation_status = DerivationStatus::VerificationFailed;
    const std::string transcript = verification_transcript(d);
    t.check(transcript.find("failed, the left side") != std::string::npos,
            "the control derivation carries a failed obligation");
    const size_t steps = d.size();
    const DerivationStatus status = d.context.derivation_status;

    const AttemptVerdict verdict = judge_attempt(
        arena, equation, parse(arena, "2*x = 4").root, arena.symbol("x"), {});
    t.check(verdict.equivalence == AttemptEquivalence::Equivalent,
            "the attempt itself is judged equivalent");
    t.evidence("VER-019", verification_transcript(d), transcript,
               "judging an equivalent attempt leaves the failed obligation failed");
    t.evidence("VER-019", d.size() == steps && d.context.derivation_status == status,
               "and adds nothing to the derivation or its status");
}

void test_names(TestSink &t) {
    t.equal(attempt_equivalence_name(AttemptEquivalence::NotComparable), "not comparable",
            "the not comparable name");
    t.equal(attempt_usefulness_name(AttemptUsefulness::ValidNotOnRoute), "valid not on route",
            "the off route name");
}

}  // namespace

void run_attempt_tests(TestSink &t) {
    test_equivalence(t);
    test_usefulness(t);
    test_halts(t);
    test_failed_obligations_stay_failed(t);
    test_names(t);
}

}  // namespace nps
