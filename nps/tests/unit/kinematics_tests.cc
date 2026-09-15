#include <string>

#include "nps/physics/kinematics.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

struct Solved {
    std::string outcome;
    std::string answer;
    std::string detail;
    std::string status;
    size_t steps = 0;
    size_t checks = 0;
    size_t failed_checks = 0;
    std::string rules;
    std::string verifications;
    std::string alternatives;
    std::string rationale;
    std::string actions;
    std::string goals;
    std::string assumptions;
    std::string applicability;
    size_t backend = 0;
    size_t rewrites = 0;
    size_t replayed = 0;
    // The metered cost, which is not the record count: a halted solve keeps its verified prefix.
    size_t spent = 0;
};

Solved run(const std::string &text, const Budget &budget = Budget()) {
    Solved out;
    KinematicsProblem p;
    std::string why;
    if (!parse_kinematics(text, &p, &why)) {
        out.outcome = "parse refused";
        out.detail = why;
        return out;
    }
    Arena arena;
    Derivation d;
    KinematicsResult r = solve_kinematics(arena, d, p, budget);
    out.outcome = kinematics_outcome_name(r.outcome);
    out.detail = r.detail;
    out.status = derivation_status_name(d.context.derivation_status);
    out.steps = d.size();
    out.spent = r.cost.steps;
    out.rewrites = r.cost.rewrites;
    out.replayed = r.cost.replayed;
    out.verifications = verification_transcript(d);
    for (size_t i = 0; i < d.context.active_assumptions.size(); ++i)
        out.assumptions += (i ? " | " : "") + d.context.active_assumptions[i];
    if (r.outcome == KinematicsOutcome::Solved)
        out.answer = p.unknown + " = " + r.value_text + " " + r.unit_text;
    for (size_t i = 0; i < d.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        const Step &s = d.at(id);
        if (!s.rule_id.empty())
            out.rules += (out.rules.empty() ? "" : " ") + s.rule_id;
        out.goals += (out.goals.empty() ? "" : " | ") + s.goal;
        if (s.kind == StepKind::Check) {
            ++out.checks;
            for (size_t v = 0; v < s.verifications.size(); ++v) {
                if (s.verifications[v].outcome == VerificationOutcome::Failed)
                    ++out.failed_checks;
            }
        }
        const TransformationPayload *moved = d.transformation(id);
        if (moved)
            out.actions += (out.actions.empty() ? "" : " | ") + moved->concrete_action;
        const PlanPayload *plan = d.plan(id);
        if (plan) {
            for (size_t a = 0; a < plan->applicability_conditions.size(); ++a)
                out.applicability +=
                    (a ? " | " : "") + plan->applicability_conditions[a];
            for (size_t a = 0; a < plan->alternatives_considered.size(); ++a)
                out.alternatives += (a ? " | " : "") + plan->alternatives_considered[a];
            out.rationale += plan->selection_rationale;
        }
    }
    return out;
}

bool always_cancel(void *) { return true; }

bool has(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

// Both rules appear, in this order. Written as an order rather than as one adjacent pair, because
// what the requirement asks is that the conversion is visible before the substitution uses it, and
// a step between them is the dimensional check doing its job rather than a regression.
bool before(const std::string &text, const char *first, const char *second) {
    const size_t a = text.find(first);
    const size_t b = text.find(second);
    return a != std::string::npos && b != std::string::npos && a < b;
}

size_t count(const std::string &text, const char *piece) {
    const size_t len = std::char_traits<char>::length(piece);
    size_t found = 0;
    for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + len))
        ++found;
    return found;
}

// A Giac that answers from a list, one reply per call, and remembers what it was asked.
class SequencedGiac : public Backend {
  public:
    SequencedGiac(const std::string &first, const std::string &second) {
        replies_.push_back(first);
        replies_.push_back(second);
    }
    explicit SequencedGiac(const std::vector<std::string> &replies) : replies_(replies) {}
    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (replies_.empty()) {
            *error = "no reply scripted";
            return false;
        }
        *out = replies_.front();
        replies_.erase(replies_.begin());
        return true;
    }
    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
};

class FailingGiac : public Backend {
  public:
    bool eval(const std::string &, std::string *, std::string *error) override {
        *error = "backend down";
        return false;
    }
};

Solved run_with(const std::string &text, Backend *giac, std::string *rearranged,
                const Budget &budget = Budget()) {
    Solved out;
    KinematicsProblem p;
    std::string why;
    parse_kinematics(text, &p, &why);
    Arena arena;
    Derivation d;
    KinematicsResult r = solve_kinematics(arena, d, p, budget, giac);
    out.outcome = kinematics_outcome_name(r.outcome);
    out.detail = r.detail;
    out.status = derivation_status_name(d.context.derivation_status);
    out.steps = d.size();
    if (r.outcome == KinematicsOutcome::Solved)
        out.answer = p.unknown + " = " + r.value_text + " " + r.unit_text;
    *rearranged = r.isolated == kNoNode ? std::string() : print(arena, r.isolated);
    out.backend = r.cost.backend_calls;
    out.verifications = verification_transcript(d);
    for (size_t i = 0; i < d.size(); ++i) {
        const Step &s = d.at(static_cast<StepId>(i));
        if (!s.rule_id.empty())
            out.rules += (out.rules.empty() ? "" : " ") + s.rule_id;
        out.goals += (out.goals.empty() ? "" : " | ") + s.goal;
        for (size_t v = 0; v < s.verifications.size(); ++v) {
            if (s.verifications[v].outcome == VerificationOutcome::Failed)
                ++out.failed_checks;
        }
    }
    return out;
}

void preorder(const Derivation &derivation, StepId id, std::vector<StepId> *order) {
    order->push_back(id);
    for (StepId child : derivation.at(id).children)
        preorder(derivation, child, order);
}

void test_intermediate_teaching_order(TestSink &t) {
    for (const char *distance : {"20", "44"}) {
        for (bool with_backend : {false, true}) {
            KinematicsProblem problem;
            std::string why;
            t.check(parse_kinematics(std::string("find v; x = ") + distance +
                                        " m; t = 4 s; a = 3 m/s^2", &problem, &why),
                    "the two-hop teaching problem parses");
            Arena arena;
            Derivation derivation;
            SequencedGiac giac(std::vector<std::string>{
                "[(x-(1/2)*a*t^2)/t]", "0", "[v0+a*t]", "0"});
            const KinematicsResult solved = solve_kinematics(
                arena, derivation, problem, Budget(), with_backend ? &giac : nullptr);
            t.check(solved.outcome == KinematicsOutcome::Solved,
                    "positive and negative intermediates retain a solved result");
            t.equal(solved.value_text, std::string(distance) == "20" ? "11" : "17",
                    "ordering preserves the final velocity");
            size_t unverified_rearrangements = 0;
            size_t unverified_elsewhere = 0;
            for (size_t i = 0; i < derivation.size(); ++i) {
                const Step &step = derivation.at(static_cast<StepId>(i));
                if (step.verified())
                    continue;
                if (step.rule_id == "kin.rearrange")
                    ++unverified_rearrangements;
                else
                    ++unverified_elsewhere;
            }
            t.check(unverified_elsewhere == 0,
                    "every record retains its verification after nesting");
            t.check(unverified_rearrangements == (with_backend ? 2u : 0u),
                    "and the only ones without are the rearrangements Giac both wrote and judged");
            std::vector<StepId> order;
            for (StepId root : derivation.roots())
                preorder(derivation, root, &order);
            t.check(order.size() == derivation.size(), "tree preorder visits every recorded step");
            size_t produced = order.size();
            size_t checked = order.size();
            size_t consumed = order.size();
            const std::string intermediate = std::string(distance) == "20" ? "-1" : "5";
            bool in_intermediate_solve = false;
            for (size_t row = 0; row < order.size(); ++row) {
                const Step &step = derivation.at(order[row]);
                if (step.kind == StepKind::Plan)
                    in_intermediate_solve = step.goal == "Isolate v0";
                const TransformationPayload *move = derivation.transformation(order[row]);
                if (move && step.rule_id == "eq.divide-both-sides" &&
                    print(arena, move->after) == "(v0 = " + intermediate + ")")
                    produced = row;
                if (in_intermediate_solve && step.rule_id == "eq.linear.check-by-substitution")
                    checked = row;
                if (move && step.rule_id == "kin.substitute" &&
                    move->concrete_action.find("v0 = " + intermediate + " m/s") != std::string::npos)
                    consumed = row;
            }
            t.check(produced < consumed,
                    "display preorder derives the intermediate before its next substitution");
            t.check(checked < consumed,
                    "display preorder verifies the intermediate before the next hop uses it");
            bool prefix_uses_intermediate = false;
            for (size_t row = 0; row < produced; ++row) {
                const TransformationPayload *move = derivation.transformation(order[row]);
                if (move && move->concrete_action.find("v0 = " + intermediate + " m/s") !=
                                std::string::npos)
                    prefix_uses_intermediate = true;
            }
            t.check(!prefix_uses_intermediate,
                    "a hint prefix before the intermediate solve cannot expose its later substitution");
        }
    }
}

}  // namespace

void run_kinematics_tests(TestSink &t) {
    for (const std::string &reply : {"[]", "[-2,2]", "[2,2]"}) {
        SequencedGiac giac(reply, "0");
        std::string rearranged;
        const Solved solved = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2",
                                       &giac, &rearranged);
        t.check(solved.answer == "t = 4 s" && rearranged.empty() && giac.commands.size() == 1,
                "a non-singleton backend collection cannot supply a kinematics rearrangement");
        t.check(!has(solved.rules, "kin.rearrange"),
                "no symbolic transformation is recorded from an arbitrary selected root");
    }
    test_intermediate_teaching_order(t);
    {
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s");
        t.equal(s.outcome, "solved", "v from v0, a and t");
        t.equal(s.answer, "v = 17 m/s", "and it is 17 m/s");
        t.equal(s.status, "solved and verified", "with every step verified");
        t.check(has(s.rules, "kin.substitute") && has(s.rules, "eq.collect-like-terms"),
                "the substitution hands over to the linear solver through its registered plan");
        t.check(!has(s.rules, "kin.convert-units"), "and nothing needed converting");
        t.check(s.checks >= 2, "a dimensional check and the solver's own check");
        t.equal(integer_text(static_cast<int64_t>(s.failed_checks)), "0", "and none fails");
        t.check(has(s.alternatives, "x = v0*t + (1/2)*a*t^2: does not contain v"),
                "the plan names the equation without v");
        t.check(has(s.alternatives, "v^2 = v0^2 + 2*a*x: needs x, which is not given"),
                "and the one that would need x");
        // The route search already solved this equation into a scratch derivation to see whether it
        // was linear in v. The real solve replays that run rather than deriving it again, and the
        // count is what says so: the records read the same either way.
        t.check(s.replayed > 0 && s.replayed < s.steps,
                "the real solve replays the run the route search probed");
        t.check(count(s.rules, "eq.collect-like-terms") == 1,
                "and the record still shows the linear solve once");
        t.evidence("PHYS-007",
                   s.outcome == "solved" && has(s.assumptions, "acceleration is constant") &&
                       has(s.assumptions, "motion is along one axis, positive in the chosen direction") &&
                       has(s.applicability, "motion is along one axis with one positive direction"),
                   "one-dimensional constant-acceleration solving records its positive direction");
        t.evidence("PHYS-025", s.outcome == "solved" && s.checks >= 2 && s.failed_checks == 0 &&
                   has(s.rules, "kin.substitute") && has(s.rules, "eq.collect-like-terms") &&
                   has(s.assumptions, "acceleration is constant") &&
                   has(s.assumptions, "motion is along one axis, positive in the chosen direction") &&
                   has(s.applicability, "motion is along one axis with one positive direction"),
                   "kinematics records model applicability, its positive axis, equation substitution and checks");
    }
    {
        Solved s = run("find v; v0 = 18 km/h; a = 3 m/s^2; t = 4 s");
        t.equal(s.answer, "v = 17 m/s", "18 km/h converts to 5 m/s first");
        t.check(before(s.rules, "kin.convert-units", "kin.substitute"),
                "with a conversion step before the substitution");
        t.evidence("PHYS-006",
                   s.answer == "v = 17 m/s" &&
                       before(s.rules, "kin.convert-units", "kin.substitute") &&
                       has(s.actions, "18 km/h = 5 m/s"),
                   "kinematics carries and simplifies units in a visible pre-substitution step");
    }
    t.equal(run("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2").answer, "t = 4 s",
            "the same equation solved for t");
    t.equal(run("find a; v = 17 m/s; v0 = 5 m/s; t = 4 s").answer, "a = 3 m/s^2", "and for a");
    t.equal(run("find v0; v = 17 m/s; a = 3 m/s^2; t = 4 s").answer, "v0 = 5 m/s", "and for v0");
    t.equal(run("find x; v0 = 5 m/s; a = 3 m/s^2; t = 4 s").answer, "x = 44 m",
            "displacement from v0, a and t");
    t.equal(run("find x; v0 = 5 m/s; v = 17 m/s; a = 3 m/s^2").answer, "x = 44 m",
            "displacement without t uses v^2 = v0^2 + 2ax");
    t.equal(run("find a; v0 = 5 m/s; v = 17 m/s; x = 44 m").answer, "a = 3 m/s^2",
            "acceleration from the same equation");
    t.equal(run("find x; v0 = 5 m/s; v = 17 m/s; t = 4 s").answer, "x = 44 m",
            "displacement from the average velocity");
    t.equal(run("find t; v0 = 5 m/s; v = 17 m/s; x = 44 m").answer, "t = 4 s",
            "time from the average velocity");
    {
        // Two measured givens at two figures each, so the answer is entitled to two. The exact
        // -24.5 is what the steps hold, and the rounding is the last thing that happens.
        Solved s = run("find v; v0 = 0 m/s; a = -9.8 m/s^2; t = 2.5 s");
        t.equal(s.answer, "v = -25 m/s", "a downward acceleration gives a negative velocity");
        t.check(count(s.rules, "kin.significant-figures") == 1, "rounded once, at the end");
        t.check(has(s.actions, "Report -24.5 as -25"), "and the exact value is in the record");
    }
    t.equal(run("find x; v0 = 10 m/s; a = 1 m/s^2; t = 3 s").answer, "x = 34.5 m",
            "a half-integer answer prints as a decimal");
    t.equal(run("find t; v = 1 m/s; v0 = 0 m/s; a = 3 m/s^2").answer, "t = 1/3 s",
            "a non-terminating answer stays a fraction");
    t.equal(run("find v, v0 = 5 m/s, a = 3 m/s^2, t = 4 s").answer, "v = 17 m/s",
            "commas separate as well");
    t.equal(run("v = ?; v0 = 5 m/s; a = 3 m/s^2; t = 4 s").answer, "v = 17 m/s",
            "the unknown can be written as a question mark");
    t.equal(run("solve for v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s").answer, "v = 17 m/s",
            "or after solve for");
    t.equal(run("find d; v0 = 5 m/s; a = 3 m/s^2; t = 4 s").answer, "x = 44 m",
            "d is displacement too");
    t.equal(run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s; x = 44 m").answer, "v = 17 m/s",
            "an extra known is not a problem");

    // Significant figures, PRD section 13.
    {
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s");
        t.check(count(s.rules, "kin.significant-figures") == 0,
                "integers throughout means nothing to round to");
        s = run("find v; v0 = 0 m/s; a = 9.80 m/s^2; t = 2.50 s");
        t.equal(s.answer, "v = 24.5 m/s", "three figures in, three figures out");
        t.check(count(s.rules, "kin.significant-figures") == 0,
                "and no step, because the exact answer already reads as three");
        // The same 24.25 reported twice, and only the givens differ: three figures each way keeps
        // the tenths, and one given at two figures takes them away.
        s = run("find v; v0 = 0 m/s; a = 9.70 m/s^2; t = 2.50 s");
        t.equal(s.answer, "v = 24.3 m/s", "three figures keep the tenth");
        s = run("find v; v0 = 0 m/s; a = 9.7 m/s^2; t = 2.50 s");
        t.equal(s.answer, "v = 24 m/s", "the fewest figures among the givens is what the answer gets");
        s = run("find x; v0 = 2.0 m/s; t = 3.0 s; a = 0 m/s^2");
        t.equal(s.answer, "x = 6.0 m", "a short answer is padded to the figures it is entitled to");
        s = run("find t; v = 1.0 m/s; v0 = 0 m/s; a = 3 m/s^2");
        t.equal(s.answer, "t = 0.33 s", "and a value with no exact decimal is reported rather than left as 1/3");
        t.check(has(s.actions, "Report 1/3 as 0.33"), "with the exact fraction still in the record");
        s = run("find v0; v = 20.0 m/s; a = 6.00 m/s^2; t = 4.00 s");
        t.equal(s.answer, "v0 = -4.0 m/s",
                "a difference reports to its least precise decimal place after the product");
        t.check(has(s.actions, "Report -4 as -4.0"),
                "cancellation changes the final significant-figure count without rounding early");
        s = run("find v; v0 = 1.234 m/s; a = 0 m/s^2; t = 2.0 s");
        t.equal(s.answer, "v = 1.234 m/s",
                "an exact zero acceleration makes its measured-time product exact");
        t.check(count(s.rules, "kin.significant-figures") == 0,
                "the annihilated product does not degrade the measured initial velocity");
    }

    {
        // This used to be the one input whose rounding could not be read back: 9223372036854775810
        // does not fit int64, so narrowing the reported text refused a rounding that is correct.
        // The comparison now reads the text as an exact value and never narrows it, so the check
        // runs, passes, and the rounded answer is the one reported.
        Solved s = run(
            "find x; v0 = 9223372036854775807 m/s; a = 0 m/s^2; t = 1.00000000000000000 s");
        t.equal(s.answer, "x = 9223372036854775810 m",
                "a rounding wider than int64 is now checked and reported rather than refused");
        t.equal(s.status, "solved and verified",
                "and the derivation says the check settled it");
        t.check(count(s.rules, "kin.significant-figures") == 1 && s.failed_checks == 0,
                "the rounding is one step in the record and nothing failed");
        // By value, because a status and a failure count cannot tell a check that passed from one
        // that never ran, and neither of them reads the sentence at all.
        t.check(has(s.verifications,
                    "passed, 9223372036854775810 is within half a unit in the last place of "
                    "9223372036854775807"),
                "the record names the outcome and the two values it compared");
    }

    {
        // giac_rearrangement calls the backend twice and builds the comparison between the two, so
        // an arena that runs out in between used to send the second request anyway, carrying a
        // kNoNode target. Reading that id faulted: BUS in NodeStore::operator[] at max_nodes 13.
        // Swept rather than pinned to one cap, because the exhausting allocation moves with the
        // route and a single number would stop testing this the moment the route changed.
        // Counting commands alone would not say this: each hop builds its own Adapter, so a
        // two-hop route sends two requests legitimately. What matters is whether the arena had
        // already failed when a request went out, so the stub asks the arena at call time.
        class WatchingGiac : public Backend {
          public:
            explicit WatchingGiac(const Arena &arena) : arena_(arena) {}
            bool eval(const std::string &, std::string *out, std::string *) override {
                if (arena_.failed())
                    ++after_failure;
                *out = replies_ % 2 == 0 ? "[(v-v0)/a]" : "0";
                ++replies_;
                return true;
            }
            size_t after_failure = 0;

          private:
            const Arena &arena_;
            size_t replies_ = 0;
        };

        size_t called_after_failure = 0;
        size_t solved_on_failed_arena = 0;
        for (size_t cap = 1; cap <= 400; ++cap) {
            KinematicsProblem p;
            std::string why;
            parse_kinematics("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &p, &why);
            Limits limits;
            limits.max_nodes = cap;
            Arena arena(limits);
            WatchingGiac giac(arena);
            Derivation d;
            const KinematicsResult r = solve_kinematics(arena, d, p, Budget(), &giac);
            called_after_failure += giac.after_failure;
            if (arena.failed() && r.outcome == KinematicsOutcome::Solved)
                ++solved_on_failed_arena;
        }
        t.check(called_after_failure == 0,
                "no arena cap lets a backend request go out once the arena has failed");
        t.check(solved_on_failed_arena == 0,
                "and an exhausted arena never comes back as a solved kinematics run");
    }

    // With Giac attached, the symbolic rearrangement is Giac's, checked against the solver.
    {
        SequencedGiac giac("[[(v-v0)/a]]", "0");
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.equal(s.answer, "t = 4 s", "the answer is the linear solver's");
        t.equal(rearranged, "((v + (-v0)) * (a^(-1)))", "and the symbolic form is Giac's");
        t.check(has(s.rules, "kin.rearrange kin.substitute"),
                "the rearrangement step comes before the substitution");
        t.evidence("PHYS-005",
                   s.answer == "t = 4 s" && has(s.rules, "kin.rearrange kin.substitute"),
                   "the physical equation is isolated symbolically before values are substituted");
        t.equal(s.status, "solved and corroborated",
                "and the run is corroborated rather than verified, because Giac wrote the form it "
                "is being asked about");
        t.check(giac.commands.size() == 2 && has(giac.commands[0], "solve(") &&
                    has(giac.commands[0], ",t)"),
                "Giac was asked to solve for t");
        t.check(giac.commands.size() == 2 && has(giac.commands[1], "17"),
                "and then whether its form with the values in matches the solver");
    }
    {
        SequencedGiac giac("[[(v-v0)/a]]", "0");
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.check(has(s.verifications,
                    "inconclusive, Giac agrees, but Giac was also asked for part of the answer it "
                    "is checking, so this corroborates the result rather than proving it"),
                "the record says in words what the outcome says in a field");
        Solved native = run("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2");
        t.equal(native.status, "solved and verified",
                "and the same problem with no backend is untouched, so only the self-check moved");

        // Every way the comparison can end, walked by the audit rather than read off the status.
        // Nothing else in this suite audits a backend-assisted kinematics record, which is how a
        // transformation with no passing verification got past criterion 4 unnoticed.
        // survives names the breaks issue #62 leaves standing, so anything else that turns up is a
        // regression rather than a number somebody has to go and look up. Written as a list rather
        // than as an empty assertion, because a record that satisfies criterion 4 without being
        // able to fail would read as coverage and this gap is real.
        // Each survivor carries how many rows of it are expected, not only that some appear. A
        // substring alone lets a fourth criterion 8 row in silently, because it is named and the
        // first row already satisfies "still broken".
        struct Survivor { const char *text; size_t rows; };
        struct Ending { const char *label; std::vector<std::string> replies;
                        std::vector<Survivor> survives; };
        const Ending endings[] = {
            {"agreed", {"[[(v-v0)/a]]", "0"}, {}},
            {"disagreed", {"[[(v+v0)/a]]", "1"}, {{"criterion 4", 1}}},
            {"could not compare", {"[[(v-v0)/a]]"}, {{"criterion 4", 1}}},
            {"declined", {"not an expression"}, {}},
        };
        for (const Ending &ending : endings) {
            KinematicsProblem p;
            std::string ignored;
            parse_kinematics("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &p, &ignored);
            Arena arena;
            Derivation d;
            SequencedGiac again(ending.replies);
            const KinematicsResult r = solve_kinematics(arena, d, p, Budget(), &again);
            const bool refusal = r.outcome != KinematicsOutcome::Solved;
            invariants::Pass audit;
            std::vector<std::string> broken;
            audit.walk(arena, d, refusal, true, &broken);
            std::string all;
            bool unexpected = false;
            for (size_t i = 0; i < broken.size(); ++i) {
                all += (i ? " | " : ", got ") + broken[i];
                bool named = false;
                for (const Survivor &known : ending.survives)
                    named = named || has(broken[i], known.text);
                unexpected = unexpected || !named;
            }
            t.check(!unexpected, std::string("the record left behind when the backend ") +
                                     ending.label + " breaks no invariant beyond the ones #62 names" +
                                     all);
            // The other half of the pin. Without it, closing #62 leaves a row that passes because
            // it allows a break rather than because the break went away.
            for (const Survivor &known : ending.survives) {
                size_t seen = 0;
                for (size_t i = 0; i < broken.size(); ++i)
                    seen += has(broken[i], known.text) ? 1 : 0;
                t.equal(integer_text(static_cast<int64_t>(seen)),
                        integer_text(static_cast<int64_t>(known.rows)),
                        std::string("and ") + known.text + " still breaks exactly as often when the "
                                   "backend " + ending.label + ", so #62 is closed by the break "
                                   "going away rather than by an allowance widening");
            }
        }
    }
    {
        // The comparison substitutes the knowns first, so it asks about one point rather than about
        // the two forms. 2*(17-5)/3 - 4 is 4, which is the solver's own answer, so an honest backend
        // returns zero here and the check still catches nothing.
        SequencedGiac giac("[[2*(v-v0)/a - 4]]", "0");
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.equal(s.answer, "t = 4 s", "the answer stands, because the linear solver reached it alone");
        t.check(!rearranged.empty() && rearranged != "((v + (-v0)) * (a^(-1)))",
                "while the form beside it is a rearrangement no correct engine would return");
        t.equal(s.status, "solved and corroborated",
                "so the derivation may not be read as verified on the strength of that comparison");
        t.check(s.failed_checks == 0, "and nothing is recorded as failed, because nothing disagreed");
    }
    {
        SequencedGiac giac("[(v+v0)/a]", "1");
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.evidence("VER-008", s.outcome, "verification failed",
                   "a rearrangement Giac cannot reconcile withholds the answer");
        t.check(s.answer.empty(), "so there is no answer");
        t.evidence("VER-008", s.failed_checks == 1,
                   "and the rearrangement step carries the failed check");
        t.check(!has(s.rules, "kin.substitute") && !has(s.rules, "eq.collect-like-terms") &&
                    !has(s.rules, "eq.divide-both-sides") && s.steps == 3,
                "a contradicted rearrangement stops before substitution or linear solve moves");
    }
    {
        // The rearrangement arrives but the comparison does not, because only one reply is scripted
        // and the is_zero call gets an error. Giac never said the two differ, so calling it a
        // verification failure and withholding the solver's own answer states something nobody
        // measured. The check step's record is the whole difference between this and the row above.
        SequencedGiac giac(std::vector<std::string>{"[[(v-v0)/a]]"});
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.equal(s.answer, "t = 4 s",
                "a comparison that could not be made does not withdraw the solver's answer");
        t.equal(s.status, "solved but unchecked",
                "the run is partial rather than verified, because the cross-check never landed");
        t.check(s.failed_checks == 0,
                "and nothing is recorded as a failed check, because nothing disagreed");
        t.check(has(s.rules, "kin.rearrange"),
                "the rearrangement Giac did supply is still shown");
    }
    {
        FailingGiac giac;
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged);
        t.equal(s.answer, "t = 4 s", "a backend that fails leaves the solve to the linear solver");
        t.check(rearranged.empty() && !has(s.rules, "kin.rearrange"),
                "with no rearrangement step rather than an unchecked one");
        // A backend that was asked and declined has to look different from no backend at all,
        // which it did not: both left a solve with no rearrangement and nothing saying why.
        t.check(has(s.goals, "Rearrange for t symbolically"),
                "and a check step recording that the engine was asked and gave nothing");
        Solved none = run("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2");
        t.check(!has(none.goals, "Rearrange for t symbolically"),
                "which a solve with no backend does not carry");
    }
    {
        // Four adapter calls, a solve and an is_zero for each hop. Counting them by assignment
        // reported the last hop's two and lost the first hop's.
        std::vector<std::string> replies;
        replies.push_back("[(x-(1/2)*a*t^2)/t]");
        replies.push_back("0");
        replies.push_back("[v0+a*t]");
        replies.push_back("0");
        SequencedGiac giac(replies);
        std::string rearranged;
        Solved s = run_with("find v; x = 20 m; t = 4 s; a = 3 m/s^2", &giac, &rearranged);
        t.equal(s.answer, "v = 11 m/s", "a two hop solve with a backend on each hop");
        t.equal(integer_text(static_cast<int64_t>(s.backend)), "4",
                "and every hop's backend calls are counted, not just the last one's");
    }
    {
        // Counting the adapter's calls on the way out cannot refuse one, so ask the gate first.
        Budget none;
        none.max_backend_calls = 0;
        SequencedGiac giac(std::vector<std::string>{"[(v-v0)/a]", "0"});
        std::string rearranged;
        Solved s = run_with("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2", &giac, &rearranged, none);
        t.equal(integer_text(static_cast<int64_t>(giac.commands.size())), "0",
                "a kinematics budget of no backend calls sends no command");
        t.evidence("PERF-008", s.outcome, "resource exceeded",
                   "the backend call limit can stop a kinematics solve");
        t.equal(s.detail, "backend call limit", "and the reason names the limit reached");
    }
    {
        // Every hop was handed the whole budget: 13 steps at caps 8, 16 and 64 alike.
        std::string measured;
        for (size_t cap = 1; cap <= 16; ++cap) {
            Budget budget;
            budget.max_steps = cap;
            Solved s = run("find v; x = 20 m; t = 4 s; a = 3 m/s^2", budget);
            measured += (measured.empty() ? "" : " ") + integer_text(static_cast<int64_t>(cap)) +
                        (s.outcome == "solved" ? ":s" : ":r") +
                        integer_text(static_cast<int64_t>(s.spent)) + "/" +
                        integer_text(static_cast<int64_t>(s.steps));
            // The meter counts the unit it refused, so a halt reports one past the cap.
            t.check(s.spent <= cap + 1,
                    "a two hop route spends no more steps than its step budget allows");
        }
        t.evidence("PERF-008", measured,
                   "1:r2/0 2:r3/0 3:r4/0 4:r5/3 5:r6/5 6:r7/6 7:r8/7 8:r9/8 9:r10/9 10:r11/9 "
                   "11:r12/11 12:r13/12 13:s13/13 14:s13/13 15:s13/13 16:s13/13",
                   "the work spent and the work recorded both answer to the step cap rather than "
                   "to the number of engines the route runs through");
    }

    // Two hops. M1's archetype 4 and the case a one-hop solver refused: no single equation reaches
    // v from x, t and a, and the answer exists behind v0.
    {
        Solved s = run("find v; x = 20 m; t = 4 s; a = 3 m/s^2");
        t.equal(s.outcome, "solved", "v from x, t and a needs an intermediate quantity");
        t.equal(s.answer, "v = 11 m/s", "and it is 11 m/s");
        t.equal(s.status, "solved and verified", "with every step verified");
        t.check(has(s.rationale, "no single constant-acceleration equation reaches v"),
                "the plan says why one equation was not enough");
        t.check(has(s.rationale, "v0 is found first"), "and names the intermediate quantity");
        t.check(has(s.rationale, "x = v0*t + (1/2)*a*t^2 for v0, then v = v0 + a*t for v"),
                "and spells the route out in order");
        t.check(count(s.rules, "kin.substitute") == 2, "each hop substitutes into its own equation");
        t.check(s.checks >= 4, "and each hop carries a dimensional check and the solver's own");
        t.equal(integer_text(static_cast<int64_t>(s.failed_checks)), "0", "with none failing");
        // Whether an equation the search passed over would also have worked is the solver's answer.
        // Labelling it from the table alone called a quadratic applicable, in the plan a student
        // reads.
        t.check(has(s.alternatives, "v^2 = v0^2 + 2*a*x: this equation is not linear in v"),
                "an equation the solver would refuse is not offered as one that would have worked");
        t.check(has(s.alternatives, "x = (1/2)*(v0 + v)*t: also applicable, not needed"),
                "and one it would take is");
        t.evidence("PHYS-004",
                   has(s.rationale, "no single constant-acceleration equation reaches v") &&
                       has(s.rationale, "v0 is found first") &&
                       has(s.alternatives, "x = (1/2)*(v0 + v)*t: also applicable, not needed"),
                   "the kinematics plan explains its selected route and viable alternative");
    }
    {
        // Two quantities missing from every equation that contains v, so no route exists. The
        // hop budget is spent on the whole route rather than on each branch, which is what keeps
        // a search like this from building one longer than the depth it was asked for.
        Solved s = run("find v; x = 20 m; a = 3 m/s^2");
        t.equal(s.outcome, "no applicable equation", "v from x and a alone has no route");
        t.check(has(s.detail, "and no equation reaches"), "and the refusal says what it looked for");
    }
    {
        // The intermediate is negative, which PRD section 13 wants read rather than hidden: the
        // body was already moving backwards when the clock started.
        Solved s = run("find v0; x = 20 m; t = 4 s; a = 3 m/s^2");
        t.equal(s.answer, "v0 = -1 m/s", "the intermediate on its own is a one-hop solve");
        t.check(count(s.rules, "kin.substitute") == 1, "and takes one hop, not two");
    }
    {
        // A shorter route wins. v is reachable in one hop here and in two through x, and the
        // deepening is what guarantees the one-hop answer is the one recorded.
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s; x = 44 m");
        t.equal(s.answer, "v = 17 m/s", "with more than enough given");
        t.check(count(s.rules, "kin.substitute") == 1, "the search still stops at one hop");
    }

    // Refusals, each with the reason where the user can read it.
    {
        Solved s = run("find t; x = 44 m; v0 = 5 m/s; a = 3 m/s^2");
        t.equal(s.outcome, "no applicable equation", "t from x, v0 and a is a quadratic");
        t.check(has(s.detail, "x = v0*t + (1/2)*a*t^2: this equation is not linear in t"),
                "and the refusal carries the solver's verdict");
        t.equal(integer_text(static_cast<int64_t>(s.steps)), "0", "with no steps written");
    }
    {
        KinematicsProblem problem;
        std::string why;
        t.check(parse_kinematics("find t; x = 4 m; v0 = 4 m/s; a = -2 m/s^2", &problem, &why),
                "a one-root quadratic fallback problem parses");
        Arena arena;
        Derivation derivation;
        KinematicsResult result = solve_kinematics(arena, derivation, problem);
        t.equal(kinematics_outcome_name(result.outcome), "no applicable equation",
                "the local method still refuses the quadratic fallback candidate");
        t.equal(derivation_status_name(result.status), "unsupported",
                "and preserves the unsupported derivation status");
        t.check(result.equation != kNoNode && result.substituted != kNoNode &&
                    result.unknown != kNoNode && result.unit_text == "s",
                "while retaining the fully specified dimension-valid candidate for the adapter");
        t.check(has(result.answer_candidate_detail,
                    "backend candidate x = v0*t + (1/2)*a*t^2 is fully specified and dimensionally valid"),
                "and records why that exact candidate may be offered");
        t.equal(integer_text(static_cast<int64_t>(derivation.size())), "0",
                "without turning the candidate into a derivation");
    }
    {
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; x = 44 m");
        t.equal(s.outcome, "no applicable equation", "v from v0, a and x needs a square root");
        t.check(has(s.detail, "v^2 = v0^2 + 2*a*x: this equation is not linear in v"),
                "and the refusal says so");
    }
    {
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2");
        t.equal(s.outcome, "no applicable equation", "two knowns are not enough");
        // The planner searched rather than looked, so the honest reason is no longer "t was not
        // given": it is that nothing derives t either.
        t.check(has(s.detail, "needs t, and no equation reaches t"),
                "and it names what is missing and that the search failed to reach it");
    }
    {
        Solved s = run("find t; v = 6 m/s; v0 = 5 m/s; a = 0 m/s^2");
        t.equal(s.outcome, "no solution",
                "inconsistent constant-velocity data keeps the linear solver's answer");
        t.equal(s.status, "solved and verified",
                "and keeps the verified status of the no-solution derivation");
        t.equal(s.detail,
                "the unknown cancels and leaves a false statement, so nothing satisfies it",
                "with the linear solver's reason intact");
        t.check(has(s.rules, "eq.linear.inspect-collected-coefficient") && s.checks > 0 &&
                    s.failed_checks == 0,
                "and the coefficient check that proves inconsistency remains in the walkthrough");

        KinematicsProblem problem;
        std::string why;
        t.check(parse_kinematics("find t; v = 6 m/s; v0 = 5 m/s; a = 0 m/s^2", &problem, &why),
                "the inconsistent backend fixture parses");
        Arena arena;
        Derivation derivation;
        SequencedGiac giac(std::vector<std::string>{});
        const KinematicsResult with_backend =
            solve_kinematics(arena, derivation, problem, Budget(), &giac);
        t.check(with_backend.outcome == KinematicsOutcome::NoSolution && giac.commands.empty(),
                "a verified no-solution answer does not ask a backend for a scalar rearrangement");
    }
    {
        Solved s = run("find v; x = 1 m; a = 3 m/s^2; t = 0 s");
        t.equal(s.outcome, "no solution",
                "a contradiction found while deriving an intermediate remains terminal");
        t.check(has(s.rationale, "while deriving v0 on the way to v") &&
                    has(s.rationale,
                        "x = v0*t + (1/2)*a*t^2 for v0 reduces to a contradiction"),
                "the plan identifies the contradictory intermediate instead of a direct solve");
        t.check(!has(s.rationale, "first that has v") && has(s.goals, "Find v") &&
                    has(s.goals, "Isolate v0") && !has(s.goals, "Isolate v |"),
                "the plan and solver steps keep their distinct requested and intermediate targets");
    }
    {
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 m");
        t.evidence("VER-007", s.outcome, "dimension mismatch", "a time given in metres is refused");
        t.check(has(s.detail, "time has dimension T, and m has L"), "with both dimensions named");
    }
    {
        Solved s = run("find v; v0 = 5; a = 3 m/s^2; t = 4 s");
        t.equal(s.outcome, "dimension mismatch", "a bare number is not a velocity");
        t.check(has(s.detail, "a bare number has 1"), "and it says so");
    }
    t.equal(run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s; v = 1 m/s").outcome, "invalid input",
            "the unknown cannot also be given");
    t.equal(run("find p; v0 = 5 m/s").outcome, "invalid input", "an unknown symbol is refused");
    t.equal(run("find v; q = 5 m/s").outcome, "invalid input", "so is an unknown known");

    // Entry refusals name the part they could not read.
    t.equal(run("v0 = 5 m/s; a = 3 m/s^2; t = 4 s").detail, "no unknown: say find v, or v = ?",
            "no unknown");
    t.equal(run("find v; v0 5 m/s").detail,
            "could not read \"v0 5 m/s\": expected symbol = value unit, or find symbol",
            "a part without an equals sign");
    t.equal(run("find v; v0 = five m/s").detail,
            "could not read v0 = five m/s: a quantity starts with a number", "a word for a value");
    t.equal(run("find v; v0 = 5 furlongs").detail,
            "could not read v0 = 5 furlongs: unknown unit furlongs", "an unknown unit");
    t.equal(run("find v; v0 = 5 m/s; v0 = 6 m/s").detail, "v0 is given twice", "a repeated symbol");
    t.equal(run("").detail, "nothing to work on: give the unknown and the known quantities",
            "nothing at all");

    // Budgets.
    {
        Budget tight;
        tight.max_steps = 2;
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", tight);
        t.equal(s.outcome, "resource exceeded", "a step budget of two halts the solve");
        t.equal(integer_text(static_cast<int64_t>(s.steps)), "0",
                "and the partial derivation is withdrawn");
        t.equal(s.status, "resource limit reached", "with the status saying why");
        // The budget runs out inside the nested solve, whose meter is not ours. Reading the reason
        // off our own meter reported a halted solve as still running.
        t.equal(s.detail, "step limit", "and the reason names the limit that was reached");
    }
    {
        // The entry poll catches cancellation before a short route can miss the rewrite stride.
        Budget cancelling;
        cancelling.poll = always_cancel;
        Solved s = run("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", cancelling);
        t.evidence("PERF-003", s.outcome, "cancelled", "an existing cancel stops a one hop solve");
        t.check(s.answer.empty(), "and a cancelled solve returns no answer");
        t.equal(integer_text(static_cast<int64_t>(s.steps)), "0",
                "with no partial derivation left behind");
        Solved r = run("find v; x = 20 m; t = 4 s; a = 3 m/s^2", cancelling);
        t.equal(r.outcome, "cancelled", "and an existing cancel stops a two hop solve");
        t.check(r.answer.empty(), "without returning its answer");
    }
    {
        // The route search is metered on the rewrite counter, so a table too large to search inside
        // the budget stops rather than running to the end of it.
        Budget tiny;
        tiny.max_rewrites = 1;
        Solved s = run("find v; x = 20 m; t = 4 s; a = 3 m/s^2", tiny);
        t.check(s.outcome != "solved", "a rewrite budget the search cannot fit in halts it");
        t.equal(s.detail, "rewrite limit", "and the reason names the limit that was reached");
    }
    {
        Budget none;
        none.max_rewrites = 0;
        Solved s = run("find v; x = 20 m; t = 4 s; a = 3 m/s^2", none);
        t.equal(s.detail, "rewrite limit", "a search refused its first candidate stops there");
        t.equal(std::to_string(s.rewrites), "1",
                "and reports the one refusal once, not the shared meter counted twice");
    }
    {
        Solved s = run("find x; v0 = 9000000000 m/s; a = 3 m/s^2; t = 4000000000 s");
        t.equal(s.outcome, "resource exceeded",
                "values whose arithmetic outgrows int64 halt the sweep rather than reading as no "
                "equation applying");
        t.check(has(s.detail, "exact integer arithmetic"), "and the refusal says why");
    }
}

}  // namespace nps
