#include <string>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/steps/differentiate.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

struct Diffed {
    DiffOutcome outcome;
    std::string derivative;
    size_t steps;
    std::string detail;
    std::string status;
    std::string verifications;
};

Diffed run(const std::string &expression, const char *variable) {
    Arena arena;
    Derivation d;
    NodeId e = parse(arena, expression).root;
    NodeId v = arena.symbol(variable);
    DiffResult r = differentiate(arena, d, e, v);

    Diffed out;
    out.outcome = r.outcome;
    out.steps = d.size();
    out.detail = r.detail;
    out.status = derivation_status_name(r.status);
    out.verifications = verification_transcript(d);
    if (r.derivative != kNoNode)
        out.derivative = print(arena, canonicalize(arena, r.derivative));
    return out;
}

// Two expressions agree when their canonical forms do. That covers a difference of ordering or
// bracketing, which means nothing, but it is not mathematical equality: canonicalize does not
// distribute or factor, so 2*(a + b) and 2*a + 2*b are different canonical forms. Expectations here
// are therefore written in the shape the rules produce, and a real equivalence check is the
// verifier's job with Giac behind it.
bool agrees(const std::string &expression, const char *variable, const std::string &expected) {
    Arena arena;
    Derivation d;
    NodeId e = parse(arena, expression).root;
    NodeId v = arena.symbol(variable);
    DiffResult r = differentiate(arena, d, e, v);
    if (r.derivative == kNoNode)
        return false;
    NodeId want = parse(arena, expected).root;
    if (want == kNoNode)
        return false;
    return canonicalize(arena, r.derivative) == canonicalize(arena, want);
}

bool has_restriction(const Derivation &derivation, const std::string &expected) {
    for (size_t i = 0; i < derivation.size(); ++i)
        for (const std::string &condition : derivation.at(static_cast<StepId>(i)).domain_restrictions)
            if (condition == expected)
                return true;
    return false;
}

bool cancel_after_completed_steps(void *context) {
    return static_cast<const Derivation *>(context)->size() >= 4;
}

void test_quotient_teaching(TestSink &t) {
    struct Quotient {
        const char *expression;
        const char *variable;
        const char *action;
        const char *derivative;
    };
    const Quotient cases[] = {
        {"sin(x)/x", "x",
         "Multiply the derivative of sin(x) by x, subtract sin(x) times the derivative of x, then divide by x squared",
         "(cos(x)*x-sin(x)*1)*x^-2"},
        {"x/sin(x)", "x",
         "Multiply the derivative of x by sin(x), subtract x times the derivative of sin(x), then divide by sin(x) squared",
         "(1*sin(x)-x*cos(x))*sin(x)^-2"},
        {"(-sin(x))/x", "x",
         "Multiply the derivative of (-sin(x)) by x, subtract (-sin(x)) times the derivative of x, then divide by x squared",
         "((-cos(x))*x-(-sin(x))*1)*x^-2"},
        {"sin(x)/(-x)", "x",
         "Multiply the derivative of sin(x) by (-x), subtract sin(x) times the derivative of (-x), then divide by (-x) squared",
         "(cos(x)*(-x)-sin(x)*(-1))*(-x)^-2"},
        {"(x+1)/(x-2)", "x",
         "Multiply the derivative of (x + 1) by (x + (-2)), subtract (x + 1) times the derivative of (x + (-2)), then divide by (x + (-2)) squared",
         "((1+0)*(x+(-2))-(x+1)*(1+0))*(x+(-2))^-2"},
        {"sin(t)/t", "t",
         "Multiply the derivative of sin(t) by t, subtract sin(t) times the derivative of t, then divide by t squared",
         "(cos(t)*t-sin(t)*1)*t^-2"},
        {"sin(x)/(x*x)", "x",
         "Multiply the derivative of sin(x) by (x * x), subtract sin(x) times the derivative of (x * x), then divide by (x * x) squared",
         "(cos(x)*(x*x)-sin(x)*(1*x+x*1))*(x*x)^-2"},
    };
    for (const Quotient &example : cases) {
        Arena arena;
        Derivation derivation;
        const NodeId input = parse(arena, example.expression).root;
        const DiffResult differentiated =
            differentiate(arena, derivation, input, arena.symbol(example.variable));
        t.check(differentiated.outcome == DiffOutcome::Differentiated &&
                    derivation.all_verified_from(0),
                std::string("quotient teaching retains a verified answer for ") + example.expression);
        t.check(agrees(example.expression, example.variable, example.derivative),
                std::string("quotient teaching preserves the derivative for ") + example.expression);
        bool found = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const StepId id = static_cast<StepId>(i);
            const Step &step = derivation.at(id);
            if (step.rule_id != "d.quotient")
                continue;
            found = true;
            const TransformationPayload *change = derivation.transformation(id);
            t.check(change && change->after != kNoNode && change->concrete_action == example.action,
                    std::string("quotient Do names both operands and the subtraction order for ") +
                        example.expression);
            t.check(step.explanation_short.find("subtract") != std::string::npos &&
                        step.explanation_short.find("square") != std::string::npos,
                    "the standard quotient Why states the subtraction and denominator square");
            t.check(step.explanation_detailed.find("pull the value of the fraction down") ==
                        std::string::npos,
                    "quotient Why makes no false downward claim when the numerator can be negative");
        }
        t.check(found, "the teaching case exercises the quotient producer");
    }
    {
        Arena arena;
        Derivation derivation;
        const NodeId input = parse(arena, "x/sin(x^x)").root;
        const DiffResult stopped = differentiate(arena, derivation, input, arena.symbol("x"));
        t.check(stopped.status == DerivationStatus::PartiallySolved &&
                    stopped.derivative == kNoNode && derivation.all_verified_from(0),
                "concrete quotient instructions preserve a verified unsupported prefix");
        for (size_t i = 0; i < derivation.size(); ++i) {
            const StepId id = static_cast<StepId>(i);
            if (derivation.at(id).rule_id == "d.quotient") {
                const TransformationPayload *change = derivation.transformation(id);
                t.check(change && change->concrete_action ==
                            "Multiply the derivative of x by sin((x^x)), subtract x times the derivative of sin((x^x)), then divide by sin((x^x)) squared",
                        "an unfinished quotient keeps explicit operations without inventing a derivative");
            }
        }
    }
}

void test_completed_domains(TestSink &t) {
    for (const char *expression : {"sqrt(2*x)", "sqrt(2*x)+x", "3*sqrt(2*x)",
                                  "-sqrt(2*x)", "sqrt(2*x)*sin(x)",
                                  "sqrt(2*x)/x", "(sqrt(2*x))^3"}) {
        Arena arena;
        Derivation derivation;
        const NodeId input = parse(arena, expression).root;
        const DiffResult differentiated = differentiate(arena, derivation, input, arena.symbol("x"));
        t.check(differentiated.outcome == DiffOutcome::Differentiated &&
                    differentiated.derivative != kNoNode && derivation.all_verified_from(0),
                std::string("completed derivative remains verified for ") + expression);
        t.check(has_restriction(derivation, "(2 * x) >= 0"),
                std::string("the input square root keeps its real-domain condition for ") + expression);
        t.check(has_restriction(derivation, "sqrt((2 * x)) is not zero"),
                std::string("the completed derivative excludes its zero square-root denominator for ") +
                    expression);
        if (std::string(expression) == "sqrt(2*x)/x")
            t.check(has_restriction(derivation, "x is not zero"),
                    "quotient completion also retains the original denominator condition");
    }
    for (const char *expression : {"sqrt(3*x+1)", "sqrt(ln(x))"}) {
        Arena arena;
        Derivation derivation;
        const NodeId input = parse(arena, expression).root;
        const DiffResult differentiated = differentiate(arena, derivation, input, arena.symbol("x"));
        const bool shifted = std::string(expression) == "sqrt(3*x+1)";
        t.check(differentiated.outcome == DiffOutcome::Differentiated,
                "shifted and logarithmic square-root chains remain supported");
        t.check(has_restriction(derivation, shifted ? "((3 * x) + 1) >= 0" : "ln(x) >= 0") &&
                    has_restriction(derivation, shifted ? "sqrt(((3 * x) + 1)) is not zero" :
                                                       "sqrt(ln(x)) is not zero"),
                "the completed chain excludes its boundary at minus one third or one");
        if (!shifted)
            t.check(has_restriction(derivation, "x > 0"),
                    "completing an outer chain retains the inner logarithm domain");
    }
    t.check(agrees("sqrt(2*x)", "x", "1/sqrt(2*x)"),
            "adding the missing domain does not change the scaled square-root derivative");
    t.check(agrees("sqrt(3*x+1)", "x", "(3/2)/sqrt(3*x+1)"),
            "adding the missing domain does not change the shifted square-root derivative");

    for (bool limited : {false, true}) {
        Arena arena;
        Derivation derivation;
        const char *expression = limited ? "sqrt(x+x^2)" : "sqrt(x+x^x)";
        const NodeId input = parse(arena, expression).root;
        Budget budget;
        if (limited)
            budget.max_steps = 3;
        const DiffResult stopped = differentiate(arena, derivation, input, arena.symbol("x"), budget);
        t.check(stopped.status == (limited ? DerivationStatus::ResourceLimitReached :
                                             DerivationStatus::PartiallySolved) &&
                    stopped.derivative == kNoNode && derivation.size() > 0 &&
                    derivation.all_verified_from(0),
                "a partial composite retains verified work without a terminal answer");
        t.check(has_restriction(derivation, limited ? "sqrt((x + (x^2))) is not zero" :
                                                    "sqrt((x + (x^x))) is not zero"),
                "a partial or resource-limited chain retains its new denominator exclusion");
        t.check(has_restriction(derivation, limited ? "(x + (x^2)) >= 0" :
                                                    "(x + (x^x)) >= 0"),
                "a partial or resource-limited chain also retains its original domain");
    }

    {
        Arena arena;
        Derivation derivation;
        std::string argument = "x";
        for (size_t i = 0; i < 7; ++i)
            argument = "(" + argument + "+" + argument + ")";
        const std::string expression = "sqrt(" + argument + ")";
        const NodeId input = parse(arena, expression).root;
        Budget budget;
        budget.poll = cancel_after_completed_steps;
        budget.poll_context = &derivation;
        const DiffResult stopped = differentiate(arena, derivation, input, arena.symbol("x"), budget);
        t.check(stopped.outcome == DiffOutcome::Cancelled &&
                    stopped.status == DerivationStatus::Cancelled &&
                    stopped.derivative == kNoNode && derivation.size() > 0 &&
                    derivation.all_verified_from(0),
                "cancellation inside a composite keeps only verified work and no final answer");
        t.check(has_restriction(derivation, print(arena, input) + " is not zero"),
                "a cancelled chain keeps its completed denominator exclusion");
    }

    Arena arena;
    Derivation derivation;
    const NodeId input = parse(arena, "sin(2*x)").root;
    const DiffResult entire = differentiate(arena, derivation, input, arena.symbol("x"));
    bool restricted = false;
    for (size_t i = 0; i < derivation.size(); ++i)
        restricted = restricted || !derivation.at(static_cast<StepId>(i)).domain_restrictions.empty();
    t.check(entire.outcome == DiffOutcome::Differentiated && !restricted,
            "completing an everywhere-defined chain introduces no artificial condition");
}

bool always_cancel(void *) { return true; }

bool uses_rule(const std::string &expression, const char *variable, const std::string &rule_id) {
    Arena arena;
    Derivation d;
    NodeId e = parse(arena, expression).root;
    differentiate(arena, d, e, arena.symbol(variable));
    for (size_t i = 0; i < d.size(); ++i) {
        if (d.at(static_cast<StepId>(i)).rule_id == rule_id)
            return true;
    }
    return false;
}

}  // namespace

class ScriptedGiac : public Backend {
  public:
    explicit ScriptedGiac(std::string reply) : reply_(std::move(reply)) {}

    size_t calls = 0;

    bool eval(const std::string &, std::string *out, std::string *error) override {
        ++calls;
        if (reply_.empty()) {
            *error = "the backend is unavailable";
            return false;
        }
        *out = reply_;
        return true;
    }

  private:
    std::string reply_;
};

// The cross-check as the derivation records it: the method, what it came back with, and what that
// answer is worth. Reported together because VER-004 is about the last of the three.
struct CrossCheck {
    bool found = false;
    std::string outcome;
    std::string strength;
    std::string status;
    bool declared = false;
    std::vector<std::string> broken;
};

CrossCheck cross_check(const std::string &reply, bool backend_helped_earlier) {
    Arena arena;
    Derivation d;

    // A step that went to the backend before the derivative was taken, which is the suboperation
    // VER-004's second sentence is about. Built by hand rather than by running a Giac-assisted
    // rewrite, because the point under test is what the record says, not how it got that way.
    if (backend_helped_earlier) {
        Step earlier;
        earlier.kind = StepKind::Transformation;
        earlier.phase = "rewrite";
        earlier.goal = "Tidy the expression before differentiating";
        earlier.rule_id = "algebra.rewrite.collect";
        earlier.rule_name = "Collect like terms";
        earlier.claim = ClaimType::EquivalentExpression;
        earlier.backend_requests = 1;
        earlier.explanation_short = "Gather the terms that share a power";
        earlier.explanation_detailed = "Reach for this when the same power appears more than once.";
        TransformationPayload moved;
        moved.before = parse(arena, "x^2 + x^2").root;
        moved.after = parse(arena, "2x^2").root;
        moved.concrete_action = "Add the two matching terms";
        const StepId id = d.add_transformation(kNoStep, std::move(earlier), std::move(moved));
        d.complete_transformation(id, moved.after);
    }

    ScriptedGiac giac(reply);
    const NodeId e = parse(arena, "x^2").root;
    DiffResult r = differentiate(arena, d, e, arena.symbol("x"), Budget(), &giac);

    CrossCheck out;
    out.status = derivation_status_name(r.status);
    for (size_t i = 0; i < d.size(); ++i) {
        const Step &s = d.at(static_cast<StepId>(i));
        if (s.rule_id != "calculus.differentiate.giac-cross-check")
            continue;
        out.found = true;
        for (size_t v = 0; v < s.verifications.size(); ++v) {
            out.outcome = verification_outcome_name(s.verifications[v].outcome);
            out.strength = evidence_strength_name(s.verifications[v].strength);
        }
    }
    out.declared = rule_schema("calculus.differentiate.giac-cross-check") != nullptr;
    invariants::Pass pass;
    pass.walk(arena, d, false, false, &out.broken);
    return out;
}

// VER-004. Both sentences of it. The first is the comparison, and the second is the one that is
// easy to let slide, because a check that ran and agreed reads as a pass whether or not the thing
// it agreed with helped produce the answer.
void test_ver004_giac_cross_check(TestSink &t) {
    const CrossCheck agreed = cross_check("2*x", false);
    t.check(agreed.found, "VER-004: supplying a backend records a cross-check step");
    t.equal(agreed.outcome, "passed", "VER-004: Giac's derivative agreeing with ours passes");
    t.equal(agreed.strength, "symbolically equivalent under assumptions",
            "VER-004: and an independent agreement is worth a symbolic equivalence");
    t.equal(agreed.status, "solved and verified",
            "VER-004: so the derivation is verified rather than merely solved");

    // Deliberately a different form of the same input rather than nonsense, because comparing
    // printed text would call this a disagreement and comparing canonically must not.
    const CrossCheck spelled_differently = cross_check("x*2", false);
    t.equal(spelled_differently.outcome, "passed",
            "VER-004: x*2 and 2*x are the same derivative, which is what comparing after "
            "normalization is for");

    const CrossCheck disagreed = cross_check("3*x", false);
    t.equal(disagreed.outcome, "failed", "VER-004: a different derivative is a failed check");
    t.equal(disagreed.status, "verification failed",
            "VER-004: and it labels the whole derivation, rather than being left hanging off a "
            "step that still claims to be verified");

    const CrossCheck unavailable = cross_check("", false);
    t.equal(unavailable.outcome, "inconclusive",
            "VER-004: a backend that will not answer settles nothing either way");
    t.equal(unavailable.status, "solved but unchecked",
            "VER-004: and the derivation is labelled unchecked rather than verified, because the "
            "check it was going to be verified by never ran");

    // The second sentence. Same reply, same agreement, different worth.
    const CrossCheck not_independent = cross_check("2*x", true);
    t.equal(not_independent.outcome, "inconclusive",
            "VER-004: Giac agreeing with an answer Giac helped produce is not recorded as a pass");
    t.equal(not_independent.strength, "symbolically equivalent under assumptions",
            "VER-004: and it keeps what the method is worth, because this check ran and agreed, "
            "which is not what the backend that would not answer above left behind");
    t.equal(not_independent.status, "solved and corroborated",
            "VER-004: so the derivation is neither verified nor unchecked, and the status is the "
            "third thing rather than the nearest of the two");

    t.evidence("VER-004",
               agreed.found && agreed.outcome == "passed" &&
                   agreed.strength == "symbolically equivalent under assumptions" &&
                   spelled_differently.outcome == "passed" && disagreed.outcome == "failed" &&
                   disagreed.status == "verification failed" &&
                   not_independent.outcome == "inconclusive" &&
                   not_independent.status == "solved and corroborated" &&
                   unavailable.status == "solved but unchecked",
               "a derivative is compared with Giac's own after both are canonicalized, so the same "
               "answer written differently is agreement and a different answer fails the whole "
               "derivation. When Giac was also asked for part of the working, the identical "
               "agreement is recorded as inconclusive and the derivation reports solved and "
               "corroborated, which the requirement separates both from an independent proof and "
               "from a check that never ran");

    // #199. The schema names this rule, and what that is worth is whether VER-016 accepts each of
    // the four records the check can write rather than whether the id is spelled somewhere.
    const auto ver016 = [](const std::vector<std::string> &broken) {
        std::string joined;
        for (size_t i = 0; i < broken.size(); ++i) {
            if (broken[i].find("VER-016") != std::string::npos)
                joined += broken[i] + " | ";
        }
        return joined;
    };
    t.check(agreed.declared, "VER-016: the cross-check rule declares a proof-obligation schema");
    t.equal(ver016(agreed.broken), std::string(),
            "VER-016: and an independent agreement conforms to it");
    t.equal(ver016(not_independent.broken), std::string(),
            "VER-016: and so does the identical agreement recorded as corroboration, which only a "
            "method the schema says may corroborate is allowed to write");
    t.equal(ver016(disagreed.broken), std::string(), "VER-016: and so does a disagreement");
    t.equal(ver016(unavailable.broken), std::string(),
            "VER-016: and so does a backend that would not answer");
}

// PERF-013's third end. The requirement names three ends to reach after a stop, and for a while
// only cancellation and resource failure had a test, so a tag would have read as covering three
// cases while covering two. The backend cross-check gives the third one a producer, so this is
// that case rather than a fourth reading of the other two. The other two carry rows of their own.
//
// Written as a differential because "unverified intermediate state shall not be reused" is a claim
// about what the answer owes to the failed call, and the only way to show it owes nothing is to
// produce the same answer without the call at all.
void test_perf013_recoverable_backend_error(TestSink &t) {
    Arena alone;
    Derivation unassisted;
    const DiffResult without =
        differentiate(alone, unassisted, parse(alone, "x^2").root, alone.symbol("x"));

    Arena asked;
    Derivation attempted;
    ScriptedGiac refuses("");
    const DiffResult with = differentiate(asked, attempted, parse(asked, "x^2").root,
                                          asked.symbol("x"), Budget(), &refuses);

    const std::string answer_alone =
        without.derivative == kNoNode ? "none" : print(alone, without.derivative);
    const std::string answer_after = with.derivative == kNoNode ? "none" : print(asked, with.derivative);

    t.equal(diff_outcome_name(with.outcome), "differentiated",
            "PERF-013: a backend that will not answer does not stop the rules finishing");
    t.equal(answer_after, answer_alone,
            "PERF-013: and the answer is the one the rules reach without any backend at all, so "
            "nothing the failed call left behind was folded into it");
    t.equal(derivation_status_name(without.status), "solved and verified",
            "PERF-013: the unassisted run is verified");
    t.equal(derivation_status_name(with.status), "solved but unchecked",
            "PERF-013: and the interrupted one is not, rather than inheriting the label it would "
            "have earned had the check run");
    t.check(attempted.size() == unassisted.size() + 1,
            "PERF-013: the record grows by the check that was attempted and by nothing else");

    // A failed call must leave no failed verification behind either, or the derivation would be
    // labelled by the backend's absence rather than by what the rules established.
    bool any_failed = false;
    for (size_t i = 0; i < attempted.size(); ++i) {
        const Step &s = attempted.at(static_cast<StepId>(i));
        for (size_t v = 0; v < s.verifications.size(); ++v) {
            if (s.verifications[v].outcome == VerificationOutcome::Failed)
                any_failed = true;
        }
    }
    t.check(!any_failed,
            "PERF-013: and a backend that could not answer is recorded as inconclusive rather than "
            "as a check the rules failed");

    t.evidence("PERF-013",
               with.outcome == DiffOutcome::Differentiated && answer_after == answer_alone &&
                   with.status == DerivationStatus::SolvedButUnchecked &&
                   without.status == DerivationStatus::SolvedAndVerified &&
                   attempted.size() == unassisted.size() + 1 && !any_failed,
               "after a recoverable backend error the derivation is the one the rules built on "
               "their own, identical to a run that never had a backend, offered under a status "
               "that says it went unchecked rather than under the verified one it would have "
               "earned. Nothing the failed call touched is carried into the answer. The other two "
               "ends the requirement names carry their own rows, in the linear group for a "
               "resource stop and in the rewrite group for a cancellation");
}

void run_differentiate_tests(TestSink &t) {
    {
        Arena arena;
        const NodeId list = arena.list({arena.integer("1"), arena.integer("2")});
        Derivation derivation;
        const DiffResult result = differentiate(arena, derivation, list, arena.symbol("x"));
        t.check(result.outcome == DiffOutcome::UnsupportedForm &&
                    result.status == DerivationStatus::Unsupported &&
                    result.derivative == kNoNode && derivation.size() == 0,
                "a variable-free list is refused before the scalar constant derivative rule");
    }
    for (const char *source : {"[]", "[x]", "[[1,2]]", "0*[1]", "f([1])", "x+[1]", "[1]^0"}) {
        Arena arena;
        const NodeId expression = parse(arena, source).root;
        Derivation derivation;
        ScriptedGiac backend("0");
        const DiffResult result =
            differentiate(arena, derivation, expression, arena.symbol("x"), Budget(), &backend);
        t.check(result.outcome == DiffOutcome::UnsupportedForm &&
                    result.status == DerivationStatus::Unsupported &&
                    result.derivative == kNoNode && derivation.size() == 0 && backend.calls == 0,
                std::string("differentiation refuses nested list input without backend work: ") + source);
    }
    test_completed_domains(t);
    {
        for (const char *variable : {"x", "t"}) {
            Arena arena;
            Derivation d;
            const std::string v = variable;
            const NodeId input = parse(arena, v + "^2*sin(" + v + ")").root;
            const DiffResult differentiated = differentiate(arena, d, input, arena.symbol(v));
            bool found = false;
            for (size_t index = 0; index < d.size(); ++index) {
                const StepId id = static_cast<StepId>(index);
                const Step &step = d.at(id);
                if (step.rule_id != "d.product") continue;
                found = true;
                const TransformationPayload *change = d.transformation(id);
                t.check(change && step.verified(), "the product instruction belongs to a verified transformation");
                if (!change) continue;
                const NodeId before = parse(arena, "d(" + v + "^2*sin(" + v + ")," + v + ")").root;
                t.check(canonicalize(arena, change->before) == canonicalize(arena, before),
                        "the product instruction starts from the requested derivative");
                const NodeId expected = parse(arena, "2*" + v + "*sin(" + v + ")+" + v + "^2*cos(" + v + ")").root;
                t.check(canonicalize(arena, change->after) == canonicalize(arena, expected),
                        "the product instruction retains the unchanged factor in each written term");
                t.equal(change->concrete_action,
                        "Differentiate each factor while keeping the other unchanged, then add the two products",
                        "the product action explains both multiplications rather than naming the rule");
                t.check(step.explanation_short.find("unchanged") != std::string::npos,
                        "the short product explanation also retains the unchanged factor");
                t.check(change->concrete_action.find("cos(") == std::string::npos &&
                            change->concrete_action.find("2 *") == std::string::npos,
                        "the parent instruction contains no computed child answers");
            }
            t.check(found && differentiated.outcome == DiffOutcome::Differentiated,
                    "the teaching case reaches the product rule");
        }
    }
    for (unsigned input = 0; input < 5; ++input) {
        for (unsigned terminal = 0; terminal < 4; ++terminal) {
            Limits limits;
            if (input == 4) limits.max_nodes = 0;
            Arena arena(limits);
            Derivation d;
            NodeId expression = parse(arena, input == 2 ? "1/0" : input == 3 ? "ln(-1)" : "x").root;
            NodeId variable = input == 1 ? arena.integer("2") : arena.symbol("x");
            if (input == 0) expression = kNoNode;
            bool cancel = false;
            Budget budget;
            budget.max_steps = 2;
            budget.poll = [](void *state) { return *static_cast<bool *>(state); };
            budget.poll_context = &cancel;
            Meter meter(budget);
            t.check(meter.rewrite() && meter.step() && meter.replay() && meter.branch() &&
                        meter.backend_call() && meter.reached(37),
                    "the shared validation check starts with every counter charged");
            if (terminal == 1) meter.step();
            if (terminal >= 2) cancel = true;
            if (terminal == 2) meter.checkpoint();
            const Cost before = meter.cost();
            ScriptedGiac giac("1");
            const DiffResult r = differentiate(arena, d, expression, variable, meter, &giac);
            t.check(r.cost.rewrites == before.rewrites && r.cost.steps == before.steps &&
                        r.cost.branches == before.branches && r.cost.backend_calls == before.backend_calls &&
                        r.cost.states == before.states && r.cost.replayed == before.replayed,
                    "every shared validation return preserves the owning meter's complete cost");
            t.check(r.derivative == kNoNode && d.size() == 0 && giac.calls == 0 &&
                        d.context.resource_policy == budget_policy(budget),
                    "shared validation or terminal refusal creates no work and retains its policy");
            if (terminal == 0)
                t.check(r.status == DerivationStatus::InvalidInput,
                        "a running shared meter preserves invalid-input classification");
            else
                t.check(r.outcome == (cancel ? DiffOutcome::Cancelled : DiffOutcome::ResourceExceeded) &&
                            r.status == (cancel ? DerivationStatus::NotRecorded : DerivationStatus::ResourceLimitReached),
                        "shared cancellation and resource stops take priority over invalid input");
        }
        Limits limits;
        if (input == 4) limits.max_nodes = 0;
        Arena arena(limits);
        Derivation d;
        NodeId expression = parse(arena, input == 2 ? "1/0" : input == 3 ? "ln(-1)" : "x").root;
        NodeId variable = input == 1 ? arena.integer("2") : arena.symbol("x");
        if (input == 0) expression = kNoNode;
        unsigned polls = 0;
        Budget budget;
        budget.poll = [](void *state) { ++*static_cast<unsigned *>(state); return true; };
        budget.poll_context = &polls;
        const DiffResult r = differentiate(arena, d, expression, variable, budget);
        t.check(r.status == DerivationStatus::InvalidInput && polls == 0 && r.cost.rewrites == 0,
                "the public budget API still validates before constructing or polling its meter");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2").root;
        unsigned polls = 0;
        Budget budget;
        budget.max_rewrites = 2;
        budget.max_steps = 2;
        budget.poll = [](void *state) { ++*static_cast<unsigned *>(state); return false; };
        budget.poll_context = &polls;
        Meter meter(budget);
        t.check(meter.rewrite() && meter.step(), "the parent operation spends shared work first");
        const DiffResult r = differentiate(arena, d, expression, arena.symbol("x"), meter);
        t.check(r.outcome == DiffOutcome::Differentiated && !meter.stopped(),
                "a derivative can use exactly the remaining parent budget");
        t.check(r.cost.rewrites == 2 && r.cost.steps == 2 && meter.rewrites() == 2 && meter.steps() == 2,
                "the derivative returns aggregate cost without charging prior work twice");
        t.check(polls == 2 && d.context.resource_policy == budget_policy(budget),
                "shared phase entry polls once and preserves the owning budget policy");
        ScriptedGiac giac("2*x");
        Derivation exhausted;
        const DiffResult stopped = differentiate(arena, exhausted, expression, arena.symbol("x"), meter, &giac);
        t.check(stopped.outcome == DiffOutcome::ResourceExceeded && stopped.derivative == kNoNode &&
                    giac.calls == 0 && meter.halt() == Halt::RewriteLimit,
                "the following phase stops at the shared limit before backend work");
    }
    for (unsigned reason = 0; reason < 3; ++reason) {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2").root;
        bool cancel = false;
        Budget budget;
        budget.max_steps = 0;
        budget.max_backend_calls = 0;
        budget.poll = [](void *state) { return *static_cast<bool *>(state); };
        budget.poll_context = &cancel;
        Meter meter(budget);
        if (reason == 0) meter.step();
        else if (reason == 1) meter.backend_call();
        else cancel = true;
        const Cost before = meter.cost();
        ScriptedGiac giac("2*x");
        const DiffResult r = differentiate(arena, d, expression, arena.symbol("x"), meter, &giac);
        t.check(r.outcome == (cancel ? DiffOutcome::Cancelled : DiffOutcome::ResourceExceeded) &&
                    r.derivative == kNoNode && d.size() == 0 && giac.calls == 0,
                "a shared terminal or newly cancelled meter starts no derivative or backend work");
        t.check(r.cost.rewrites == before.rewrites && r.cost.steps == before.steps &&
                    r.cost.backend_calls == before.backend_calls,
                "terminal phase entry preserves every previously charged counter");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2").root;
        Budget budget;
        budget.max_backend_calls = 0;
        ScriptedGiac giac("2*x");
        const DiffResult r = differentiate(arena, d, expression, arena.symbol("x"), budget, &giac);
        t.check(r.outcome == DiffOutcome::ResourceExceeded && r.derivative == kNoNode,
                "a derivative cross-check cannot turn a terminal backend budget into success");
        t.check(giac.calls == 0 && r.status == DerivationStatus::ResourceLimitReached,
                "the exhausted cross-check budget invokes no backend and records resource failure");
    }
    {
        Arena arena;
        Derivation d;
        d.request.numeric_mode = NumericMode::Decimal;
        const NodeId expression = parse(arena, "x^2").root;
        Budget budget;
        budget.max_steps = 1;
        const DiffResult r = differentiate(arena, d, expression, arena.symbol("x"), budget);
        t.check(r.outcome == DiffOutcome::ResourceExceeded && r.derivative == kNoNode,
                "decimal derivative reporting cannot succeed after its step budget is exhausted");
    }
    test_ver004_giac_cross_check(t);
    test_perf013_recoverable_backend_error(t);
    test_quotient_teaching(t);

    {
        // The guard, not the predicate. Before it, 1/0 came back as outcome differentiated with
        // status SolvedAndVerified and derivative 0, which is a fabricated answer to an undefined
        // expression and exactly what MVP criterion 11 forbids.
        const char *undefined[] = {"x/0", "1/0", "x/(1-1)", "2/0 + x"};
        for (const char *src : undefined) {
            Arena arena;
            Derivation d;
            ParseResult p = parse(arena, src);
            DiffResult r = differentiate(arena, d, p.root, arena.symbol("x"), Budget());
            t.check(r.outcome == DiffOutcome::UnsupportedForm && r.derivative == kNoNode &&
                        r.status == DerivationStatus::InvalidInput,
                    std::string("differentiating ") + src + " is refused, not answered");
        }
    }
    {
        // An exponent that is arithmetically an integer but structurally a sum or a quotient. The
        // rules read it through small_integer, which sees the spelling, so x^(1+1) was refused
        // while the identical x^2 differentiated.
        const char *spellings[] = {"x^(1+1)", "x^(4/2)", "x^(3-1)"};
        for (const char *src : spellings) {
            Arena arena;
            Derivation d;
            ParseResult p = parse(arena, src);
            DiffResult r = differentiate(arena, d, p.root, arena.symbol("x"), Budget());
            t.check(r.outcome == DiffOutcome::Differentiated,
                    std::string(src) + " reaches the power rule through its folded exponent");
        }
    }
    t.check(agrees("5", "x", "0"), "a constant differentiates to zero");
    t.check(agrees("x", "x", "1"), "the variable differentiates to one");
    t.check(agrees("y", "x", "0"), "another symbol is a constant here");
    t.check(agrees("x^2", "x", "2*x^1"), "the power rule");
    t.check(agrees("x^3", "x", "3*x^2"), "and again at a higher power");
    t.check(agrees("3x", "x", "3"), "a constant multiple keeps the constant");
    t.check(agrees("x + 5", "x", "1"), "a sum differentiates term by term");
    t.check(agrees("x^2 + x", "x", "2*x^1 + 1"), "and adds the results");
    t.check(agrees("sin(x)", "x", "cos(x)"), "sine");
    t.check(agrees("cos(x)", "x", "-sin(x)"), "cosine keeps its sign");
    t.check(agrees("exp(x)", "x", "exp(x)"), "the exponential is its own derivative");
    t.check(agrees("ln(x)", "x", "x^-1"), "the logarithm");

    t.check(agrees("x^2*sin(x)", "x", "2*x^1*sin(x) + x^2*cos(x)"),
            "the product rule on the milestone 0 expression");
    t.check(agrees("sin(x^2)", "x", "cos(x^2)*2*x^1"), "the chain rule through sine");
    t.check(agrees("(x + 1)^3", "x", "3*(x + 1)^2*1"), "the chain rule through a power");
    t.check(agrees("exp(2x)", "x", "exp(2x)*2"), "the chain rule through the exponential");
    t.check(agrees("2*sin(x)*x", "x", "2*cos(x)*x + 2*sin(x)*1"),
            "a constant comes out of the factor that carries it");

    // The quotient rule. Division is Mul with a negative power, so all of these arrive as products
    // and the question each one settles is which rule explains it.
    t.check(agrees("x/sin(x)", "x", "(1*sin(x) - x*cos(x))*sin(x)^-2"), "the quotient rule");
    t.check(agrees("sin(x)/x", "x", "(cos(x)*x - sin(x)*1)*x^-2"), "and with the parts the other way");
    t.check(agrees("x^2/x^3", "x", "(2*x^1*x^3 - x^2*3*x^2)*(x^3)^-2"),
            "a quotient of two powers");
    t.check(uses_rule("x/sin(x)", "x", "d.quotient"), "a quotient records the quotient rule");
    t.check(!uses_rule("x/sin(x)", "x", "d.product"),
            "and not the product rule it used to be explained as");
    t.check(!uses_rule("x/2", "x", "d.quotient"),
            "a constant denominator stays a constant multiple");
    t.check(uses_rule("x/2", "x", "d.constant-multiple"), "which is the rule that explains it");
    t.check(!uses_rule("2/x", "x", "d.quotient"),
            "a constant numerator is a constant multiple of a reciprocal power");
    t.check(uses_rule("2/x", "x", "d.power"), "explained by the power rule");
    t.check(agrees("x/2", "x", "1/2"), "and the constant denominator answer is still right");
    t.check(agrees("2/x", "x", "2*(-1*x^-2)"), "as is the reciprocal one");
    t.evidence("CALC-002",
               agrees("5", "x", "0") && agrees("x^3", "x", "3*x^2") &&
                   agrees("x^2 + x", "x", "2*x^1 + 1") &&
                   agrees("x^2*sin(x)", "x", "2*x^1*sin(x) + x^2*cos(x)") &&
                   agrees("x/sin(x)", "x", "(1*sin(x) - x*cos(x))*sin(x)^-2") &&
                   agrees("sin(x^2)", "x", "cos(x^2)*2*x^1") &&
                   agrees("exp(x)", "x", "exp(x)") && agrees("ln(x)", "x", "x^-1") &&
                   agrees("sin(x)", "x", "cos(x)") && agrees("cos(x)", "x", "-sin(x)") &&
                   agrees("tan(x)", "x", "cos(x)^-2"),
               "the derivative rule envelope covers arithmetic, composition, exp, log and trig");

    {
        // A composite rule states its decomposition before its children have run, so its result is
        // filled in afterwards. A step that shows no result is one a student cannot follow.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x)").root;
        differentiate(arena, d, e, arena.symbol("x"));
        for (size_t i = 0; i < d.size(); ++i) {
            const TransformationPayload *p = d.transformation(static_cast<StepId>(i));
            if (p)
                t.check(p->after != kNoNode, "every transformation records what it produced");
        }
    }

    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x/sin(x)").root;
        differentiate(arena, d, e, arena.symbol("x"));

        t.check(d.roots().size() == 1, "the quotient has one plan at the root");
        const StepId plan = d.roots().empty() ? kNoStep : d.roots()[0];
        if (plan != kNoStep && d.at(plan).children.size() == 1) {
            StepId quotient = d.at(plan).children[0];
            t.equal(d.at(quotient).rule_name, "Quotient rule", "with the quotient rule under it");
            t.check(d.at(quotient).children.size() == 2,
                    "and the top and the bottom derivatives hanging off it");
            t.check(d.at(quotient).verified(), "the quotient step carries its own verification");
            t.evidence("CALC-003",
                       d.at(quotient).rule_name == "Quotient rule" &&
                           d.transformation(quotient) != nullptr && d.at(quotient).verified(),
                       "the quotient rule is named beside its verified transformation");
        }
    }

    {
        Diffed s = run("x^2*sin(x)", "x");
        t.equal(diff_outcome_name(s.outcome), "differentiated", "the milestone expression works");
        t.check(s.steps == 4,
                "recorded as a plan, the product rule, and one step for each of the two parts");
    }

    {
        // Nesting is the point of section 9's nested step expansion: the product rule's record has
        // the two derivatives it needed as children, not as an unexplained jump.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x)").root;
        differentiate(arena, d, e, arena.symbol("x"));

        t.check(d.roots().size() == 1, "one plan at the root");
        const StepId plan = d.roots().empty() ? kNoStep : d.roots()[0];
        t.check(plan != kNoStep && d.at(plan).children.size() == 1,
                "with the product rule under it");
        if (plan != kNoStep && d.at(plan).children.size() == 1) {
            StepId product = d.at(plan).children[0];
            t.equal(d.at(product).rule_name, "Product rule", "the top rule is the product rule");
            t.check(d.at(product).children.size() == 2,
                    "and the two derivatives it needed hang off it");
            for (StepId child : d.at(product).children)
                t.check(d.at(child).verified(), "each child carries its own verification");
        }
    }

    {
        Diffed s = run("x^x", "x");
        t.equal(diff_outcome_name(s.outcome), "unsupported form",
                "a variable exponent is refused rather than guessed at");
        t.check(s.detail.find("logarithmic") != std::string::npos,
                "and names what it would need");
    }
    {
        // A refusal under a composite rule used to leave the sum step behind with no result in it,
        // marked verified. A record with a hole reads as a step that was taken.
        Diffed s = run("x^x + sin(x)", "x");
        t.equal(diff_outcome_name(s.outcome), "unsupported form", "a refusal inside a sum refuses");
        t.check(s.steps == 0, "and rewinds the record rather than leaving the sum step half filled");
        t.evidence("STEP-005", s.outcome == DiffOutcome::UnsupportedForm && s.steps == 0,
                   "an unsupported child transition stops without speculative recorded work");
        t.evidence("VER-013", s.outcome == DiffOutcome::UnsupportedForm && s.steps == 0,
                   "an unsupported derivative strategy leaves no plan with unevaluated preconditions");
    }
    {
        // The mirror of the block above, and the pair that decides whether STEP-025's "preserve its
        // verified prefix" means anything for a form with no rule. Order is the whole difference:
        // here the sine is differentiated and checked before the traversal reaches x^x, so there is
        // a prefix, where above the refusal came first and there was none.
        Diffed s = run("sin(x) + x^x", "x");
        t.equal(diff_outcome_name(s.outcome), "unsupported form",
                "a sum that gets partway still refuses");
        t.equal(s.status, "partially solved", "but says it got partway rather than nowhere");
        t.check(s.steps == 3, "keeping the plan, the sum rule and the term it did differentiate");
        t.check(s.derivative.empty(), "and offers no derivative for the goal it did not reach");
        t.check(s.detail.find("logarithmic") != std::string::npos,
                "while still naming the subproblem that stopped it");
        t.evidence("STEP-025", s.status == "partially solved" && s.steps == 3 &&
                                   s.derivative.empty(),
                   "a partially solved derivative keeps its verified prefix and shows no terminal "
                   "answer for the goal");
    }
    {
        // The same shape one rule up. A product records itself before either factor returns, so the
        // cosine's step could only survive if the product step completes with the factor it could
        // not differentiate left as a derivative-of.
        Diffed p = run("sin(x) * x^x", "x");
        t.equal(diff_outcome_name(p.outcome), "unsupported form", "a product that gets partway refuses");
        t.equal(p.status, "partially solved", "and keeps what it had");
        t.check(p.steps == 3, "the plan, the product rule and the factor it differentiated");
    }
    {
        // Two levels, which is why the rules ask the record what landed below them rather than
        // asking their own children. The sum inside completes with a remainder, and the product
        // above it has to notice that before it decides whether it has anything worth keeping.
        Diffed q = run("(sin(x) + x^x) * cos(x)", "x");
        t.equal(q.status, "partially solved", "a refusal two levels down still leaves a prefix");
        t.check(q.steps == 4, "and every step above the failing form is kept");
    }
    {
        Diffed c = run("3 * (sin(x) + x^x)", "x");
        t.equal(c.status, "partially solved", "the constant multiple rule keeps a prefix too");
        t.check(c.steps == 4, "with the constant still lifted out");
    }
    {
        // The other half of the pair, and the guard on the claim. The failing factor is first, so
        // nothing was differentiated before it and there is no prefix to keep. Claiming partially
        // solved here would be the same overstatement as claiming solved.
        Diffed d = run("x^x * sin(x)", "x");
        t.equal(d.status, "unsupported", "a product whose first factor has no rule got nowhere");
        t.check(d.steps == 0, "so it keeps nothing rather than recording a restatement");
    }
    {
        // The chain rule and the negation are composites too, and each has exactly one child, so a
        // prefix can only reach them from a level further down.
        Diffed chain = run("sin(sin(x) + x^x)", "x");
        t.equal(chain.status, "partially solved", "the chain rule keeps a prefix from below it");
        t.check(chain.steps == 4, "the plan, the chain step, the sum under it and the sine");

        Diffed neg = run("-(sin(x) + x^x)", "x");
        t.equal(neg.status, "partially solved", "and so does a negation");
        t.check(neg.steps == 4, "with the sign step kept alongside the term that differentiated");

        Diffed powr = run("(sin(x) + x^x)^2", "x");
        t.equal(powr.status, "partially solved", "and a power whose base is the partway sum");
        t.check(powr.steps == 4, "keeping the power step above it");

        // A chain whose argument has no rule at all reached nothing below it, so there is no prefix
        // and completing the chain step would be a restatement rather than work.
        Diffed bare = run("sin(x^x)", "x");
        t.equal(bare.status, "unsupported", "while a chain over a form with no rule got nowhere");
        t.check(bare.steps == 0, "and keeps nothing");
    }
    {
        t.check(agrees("-sin(x)", "x", "-cos(x)"), "a negation differentiates through");
        t.check(uses_rule("-sin(x)", "x", "d.constant-multiple"),
                "and records the sign change as a constant multiple rather than skipping it");
        t.check(run("-sin(x)", "x").steps == 3, "as a plan, the sign step and the sine under it");
    }
    {
        // A canonical tree can carry INT64_MIN as a literal. The power rule computed n - 1 on it and
        // wrapped, reporting a derivative with the wrong exponent.
        Arena arena;
        Derivation d;
        NodeId e = canonicalize(arena, parse(arena, "x^(-9223372036854775807 - 1)").root);
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"));
        t.equal(diff_outcome_name(r.outcome), "unsupported form",
                "an exponent at the int64 edge is refused rather than wrapped");
    }
    {
        Diffed s = run("arctan(x)", "x");
        t.equal(diff_outcome_name(s.outcome), "unsupported form",
                "a function with no rule is refused");
        t.check(s.detail.find("arctan") != std::string::npos, "and names the function");
    }
    {
        // PERF-008 and PERF-009. A budget that runs out has to stop the solve and offer no answer.
        // Two steps buys the sum rule and the product rule below it, and the product's own children
        // never start, so nothing under either parent ever returned a result and there is no prefix
        // to keep. That is the reason the record is empty here, not a rule that a halt empties it:
        // the block below is the same halt with one child landed, and it keeps three steps.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x) + x^3*cos(x) + x^4*tan(x)").root;
        Budget tight;
        tight.max_steps = 2;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"), tight);
        t.equal(diff_outcome_name(r.outcome), "resource exceeded", "a step budget stops the solve");
        t.check(d.size() == 0, "and nothing below either parent had returned, so nothing is kept");
        t.check(r.derivative == kNoNode, "with no derivative offered");
        t.equal(derivation_status_name(d.context.derivation_status), "resource limit reached",
                "the context records why it stopped");
    }

    {
        // STEP-025 through a composite rule, which is what task 34 is about. Three steps reach the
        // product rule and both factors of the first one, so when the meter stops there is checked
        // work sitting under an unfilled parent. Before this, verified_prefix_end cut at the parent
        // and took the checked children with it, and every halt inside a sum, product, quotient or
        // chain kept nothing at all.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x)").root;
        Budget tight;
        tight.max_steps = 2;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"), tight);
        t.equal(diff_outcome_name(r.outcome), "resource exceeded", "the budget still stops the solve");
        t.check(r.derivative == kNoNode, "and STEP-025's other half holds: no answer is offered");
        t.check(d.size() == 3, "but the plan, the product rule and the power rule survive");
        t.evidence("STEP-025", d.size() == 3 && r.derivative == kNoNode,
                   "a halt inside a composite rule preserves the verified prefix under it while "
                   "still refusing a terminal answer for the original goal");
        t.equal(derivation_status_name(d.context.derivation_status), "resource limit reached",
                "labelled for why it stopped, which is what PERF-009 asks of what is left behind");

        // The parent says what it produced, and what it produced names the factor nobody reached
        // rather than claiming a derivative for it. A halted leaf used to hand its result back even
        // though its step was never recorded, which put cos(x) in this line with no step behind it.
        const TransformationPayload *product = nullptr;
        for (size_t i = 0; i < d.size() && product == nullptr; ++i) {
            if (d.at(static_cast<StepId>(i)).rule_id == "d.product")
                product = d.transformation(static_cast<StepId>(i));
        }
        t.check(product != nullptr && product->after != kNoNode,
                "the product rule filled itself in, which is what lets its child be read");
        t.equal(product == nullptr ? "no product step" : print(arena, product->after),
                "(((2 * (x^1)) * sin(x)) + ((x^2) * d(sin(x), x)))",
                "with the factor the meter never reached left as a derivative of it");
    }

    {
        // A kept step keeps the condition it needs. The halt path never settled its restrictions,
        // which was harmless only while a halt rewound to entry, and stopped being harmless the
        // moment a halt could keep the logarithm rule: a step that needs a positive argument and
        // does not say so reads as holding everywhere.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "ln(x)*x^3").root;
        Budget tight;
        tight.max_steps = 2;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"), tight);
        t.equal(diff_outcome_name(r.outcome), "resource exceeded", "the logarithm halt stops too");
        bool carried = false;
        for (size_t i = 0; i < d.size(); ++i) {
            if (!d.restrictions_at(static_cast<StepId>(i)).empty())
                carried = true;
        }
        t.check(d.size() > 0, "the logarithm rule survives the halt");
        t.evidence("STEP-003", carried,
                   "and a step kept through a halt carries the domain restriction it introduced");
    }

    {
        // An existing cancel is observed before a short derivative starts.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2").root;
        Budget cancelling;
        cancelling.poll = always_cancel;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"), cancelling);
        t.evidence("PERF-003", diff_outcome_name(r.outcome), "cancelled",
                   "a cancel poll stops the solve");
        t.evidence("PERF-009", d.size() == 0, "and leaves no partially accepted derivation");
        t.equal(derivation_status_name(d.context.derivation_status), "not recorded",
                "the context claims no outcome, because nothing survived");
    }

    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2*sin(x)").root;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"));
        t.equal(diff_outcome_name(r.outcome), "differentiated", "the default budget solves it");
        t.equal(derivation_status_name(d.context.derivation_status), "solved and verified",
                "and the context says every step was checked");
        t.check(context_known(d.context.application_version), "with the build that produced it");
        t.check(d.context.normalized_problem_model == e, "and the expression it was given");
        t.evidence("MATH-010", d.context.branch_convention.find("real domain") != std::string::npos,
                   "differentiation records the real default domain");
    }

    {
        // Not through run(), which would make a symbol named "2" rather than the integer 2.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x").root;
        NodeId two = parse(arena, "2").root;
        DiffResult r = differentiate(arena, d, e, two);
        t.equal(diff_outcome_name(r.outcome), "not a variable",
                "differentiating with respect to a number is refused");
        t.check(d.size() == 0, "and nothing is recorded for a request that was never valid");
    }

    {
        Arena arena;
        Derivation d;
        d.request.numeric_mode = NumericMode::Decimal;
        NodeId e = parse(arena, "x/2").root;
        DiffResult r = differentiate(arena, d, e, arena.symbol("x"));
        t.equal(diff_outcome_name(r.outcome), "differentiated",
                "decimal mode differentiates a quotient by a constant");
        t.equal(r.derivative == kNoNode ? std::string() : print(arena, r.derivative), "0.5",
                "and writes the half on the answer line as a decimal");
        t.equal(derivation_status_name(d.context.derivation_status), "solved and verified",
                "and stays verified, because rewriting a terminating fraction drops no digit");
        t.equal(numeric_mode_name(d.context.numeric_mode), "decimal",
                "and the context records the mode it ran under");

        Arena exact_arena;
        Derivation exact;
        NodeId same = parse(exact_arena, "x/2").root;
        DiffResult e_r = differentiate(exact_arena, exact, same, exact_arena.symbol("x"));
        t.check(e_r.derivative != kNoNode &&
                    print(exact_arena, e_r.derivative).find('.') == std::string::npos,
                "while the default, exact mode, keeps the same derivative a fraction");
        t.equal(numeric_mode_name(exact.context.numeric_mode), "exact",
                "and a caller that sets no mode gets exact");

        // The one promotion neither mode is allowed. 1.0 + 1.0 is two, but it is a measured two, and
        // reading it as exact would hand the power rule a precision nobody typed. The refusal
        // records nothing, which is why the acceptance corpus cannot tell this run from the
        // exact-mode one and why the pair is pinned here rather than there.
        Arena laundered_arena;
        Derivation laundered;
        laundered.request.numeric_mode = NumericMode::Decimal;
        NodeId measured = parse(laundered_arena, "x^(1.0+1.0)").root;
        DiffResult l_r =
            differentiate(laundered_arena, laundered, measured, laundered_arena.symbol("x"));
        t.equal(diff_outcome_name(l_r.outcome), "unsupported form",
                "decimal mode refuses a decimal exponent rather than folding it to an exact one");
        t.check(l_r.detail.find("exponent") != std::string::npos,
                "and says it is the exponent that stopped it");
    }

    {
        // A decimal past the exact rational's capacity is room running out, not a form with no rule.
        Diffed s = run("0.1234567890123456789*x", "x");
        t.equal(diff_outcome_name(s.outcome), "resource exceeded",
                "a decimal past the exact rational's capacity is refused as a resource limit");
        t.equal(s.status, "resource limit reached", "and carries the status the gates read");
        t.check(s.detail.find("more digits") != std::string::npos,
                "with the refusal naming the capacity it exceeded");
    }
}

}  // namespace nps
