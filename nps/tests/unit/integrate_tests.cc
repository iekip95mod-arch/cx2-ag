#include <string>

#include "nps/cas/giac_adapter.h"
#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/steps/integrate.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

class IdentityBackend : public Backend {
  public:
    ResultTag tag = ResultTag::Exact;
    std::string answer = "0";
    size_t calls = 0;
    std::string target;
    bool *cancel = nullptr;
    bool exhaust = false;
    bool eval(const std::string &, std::string *, std::string *) override { return false; }
    bool typed(const Request &request, Arena &arena, TypedResult *out) override {
        ++calls;
        target = op_name(request.op) + std::string(":") + print(arena, request.target);
        out->tag = tag;
        out->value = parse(arena, answer).root;
        out->detail = "scripted identity response";
        if (cancel) *cancel = true;
        if (exhaust) arena.fail(Status::SizeExceeded);
        return true;
    }
};

struct Integrated {
    IntegrateOutcome outcome;
    std::string general;
    size_t steps;
    std::string detail;
    std::string status;
    std::string mode;
    std::string rules;
    std::string verifications;
    Cost cost;
};

Integrated run_in(const std::string &expression, const char *variable, NumericMode mode) {
    Arena arena;
    Derivation d;
    d.request.numeric_mode = mode;
    NodeId e = parse(arena, expression).root;
    NodeId v = arena.symbol(variable);
    IntegrateResult r = integrate(arena, d, e, v);

    Integrated out;
    out.outcome = r.outcome;
    out.steps = d.size();
    out.detail = r.detail;
    out.status = derivation_status_name(d.context.derivation_status);
    out.mode = numeric_mode_name(d.context.numeric_mode);
    out.verifications = verification_transcript(d);
    out.cost = r.cost;
    for (size_t i = 0; i < d.size(); ++i)
        out.rules += d.at(static_cast<StepId>(i)).rule_id + " ";
    if (r.antiderivative != kNoNode)
        out.general = print(arena, r.antiderivative);
    return out;
}

Integrated run(const std::string &expression, const char *variable) {
    return run_in(expression, variable, NumericMode::Exact);
}

// The particular antiderivative, without its constant, against the expected one in canonical form.
// The canonical form folds rational coefficients, so x^3/3 can be written as such here and still
// match a rule that produced x^3 * 3^-1.
bool agrees(const std::string &expression, const char *variable, const std::string &expected, Backend *backend = nullptr) {
    Arena arena;
    Derivation d;
    NodeId e = parse(arena, expression).root;
    NodeId v = arena.symbol(variable);
    IntegrateResult r = integrate(arena, d, e, v, Budget(), backend);
    if (r.particular == kNoNode)
        return false;
    NodeId want = parse(arena, expected).root;
    if (want == kNoNode)
        return false;
    return canonicalize(arena, r.particular) == canonicalize(arena, want);
}

bool always_cancel(void *) { return true; }

bool uses_rule(const std::string &expression, const char *variable, const std::string &rule_id) {
    Arena arena;
    Derivation d;
    NodeId e = parse(arena, expression).root;
    integrate(arena, d, e, arena.symbol(variable));
    for (size_t i = 0; i < d.size(); ++i) {
        if (d.at(static_cast<StepId>(i)).rule_id == rule_id)
            return true;
    }
    return false;
}

}  // namespace

void run_integrate_tests(TestSink &t) {
    {
        Arena arena;
        const NodeId list = arena.list({arena.integer("1"), arena.integer("2")});
        Derivation derivation;
        const IntegrateResult result = integrate(arena, derivation, list, arena.symbol("x"));
        t.check(result.outcome == IntegrateOutcome::UnsupportedForm &&
                    result.status == DerivationStatus::Unsupported &&
                    result.antiderivative == kNoNode && derivation.size() == 0,
                "a variable-free list is refused before the scalar constant integral rule");
    }
    for (const char *source : {"[]", "[x]", "[[1,2]]", "0*[1]", "f([1])", "x+[1]", "[1]^0"}) {
        Arena arena;
        const NodeId expression = parse(arena, source).root;
        Derivation derivation;
        const IntegrateResult result =
            integrate(arena, derivation, expression, arena.symbol("x"));
        t.check(result.outcome == IntegrateOutcome::UnsupportedForm &&
                    result.status == DerivationStatus::Unsupported &&
                    result.antiderivative == kNoNode && derivation.size() == 0,
                std::string("integration refuses nested list input before scalar work: ") + source);
    }
    {
        struct Case {
            const char *input;
            const char *variable;
            const char *expected;
            const char *coefficient;
        };
        for (const Case &example : {
                 Case{"sin(2x)", "x", "-cos(2x)/2", "2"},
                 Case{"cos(-3t+1)", "t", "sin(-3t+1)/(-3)", "-3"},
                 Case{"exp(y/2)", "y", "2*exp(y/2)", "1/2"},
                 Case{"(2z+1)^3", "z", "(2z+1)^4/8", "2"},
                 Case{"1/(3t+1)", "t", "ln(3t+1)/3", "3"},
                 Case{"sin(t+3)", "t", "-cos(t+3)", "1"}}) {
            Arena arena;
            Derivation d;
            const NodeId input = parse(arena, example.input).root;
            const IntegrateResult integrated = integrate(arena, d, input, arena.symbol(example.variable));
            bool found = false;
            for (size_t index = 0; index < d.size(); ++index) {
                const StepId id = static_cast<StepId>(index);
                const Step &step = d.at(id);
                if (step.rule_id != "i.linear-substitution") continue;
                found = true;
                const TransformationPayload *change = d.transformation(id);
                t.check(change && step.verified(), "the substitution instruction belongs to a verified transformation");
                if (!change) continue;
                const NodeId before = parse(arena, std::string("int(") + example.input + "," + example.variable + ")").root;
                t.check(canonicalize(arena, change->before) == canonicalize(arena, before),
                        "the substitution instruction starts from the requested integral");
                t.check(canonicalize(arena, change->after) == canonicalize(arena, parse(arena, example.expected).root),
                        "the substitution instruction connects to the independently scaled antiderivative");
                const std::string coefficient = print(arena, canonicalize(arena, parse(arena, example.coefficient).root));
                if (coefficient == "1")
                    t.check(change->concrete_action.find("divide") == std::string::npos,
                            "a unit inner derivative does not ask for unnecessary division");
                else
                    t.check(change->concrete_action.find("divide by " + coefficient) != std::string::npos,
                            std::string("the substitution action states its actual divisor for ") + example.input);
                t.check(step.explanation_detailed.find(std::string("with respect to ") + example.variable) != std::string::npos &&
                            (std::string(example.variable) == "x" || step.explanation_detailed.find(" dx") == std::string::npos),
                        "the substitution explanation uses the actual integration variable");
            }
            t.check(found && integrated.outcome == IntegrateOutcome::Integrated,
                    "the teaching case reaches linear substitution");
        }
    }
    {
        size_t over_budget = 0;
        size_t wrong_boundary = 0;
        size_t lost_policy = 0;
        for (NumericMode mode : {NumericMode::Exact, NumericMode::Decimal}) {
            for (const char *text : {"1", "x", "x^2", "x^2+x", "sin(x)", "ln(x)", "ln(2*x+1)+ln(x)", "sqrt(x)", "sqrt(2*x+1)"}) {
                Arena reference_arena;
                Derivation reference;
                reference.request.numeric_mode = mode;
                const NodeId expression = parse(reference_arena, text).root;
                IdentityBackend reference_backend;
                const IntegrateResult complete = integrate(reference_arena, reference, expression,
                                                          reference_arena.symbol("x"), Budget(), &reference_backend);
                t.check(complete.outcome == IntegrateOutcome::Integrated,
                        "the aggregate budget boundary has a successful control");
                for (size_t rewrites = 0; rewrites <= complete.cost.rewrites + 1; ++rewrites) {
                    for (size_t steps = 0; steps <= complete.cost.steps + 1; ++steps) {
                        Arena arena;
                        Derivation d;
                        d.request.numeric_mode = mode;
                        Budget budget;
                        budget.max_rewrites = rewrites;
                        budget.max_steps = steps;
                        const NodeId input = parse(arena, text).root;
                        IdentityBackend backend;
                        const IntegrateResult r = integrate(arena, d, input, arena.symbol("x"), budget, &backend);
                        const bool solved = r.outcome == IntegrateOutcome::Integrated;
                        if (solved && (r.cost.rewrites > rewrites || r.cost.steps > steps))
                            ++over_budget;
                        const bool enough = rewrites >= complete.cost.rewrites &&
                                            steps >= complete.cost.steps;
                        if (solved != enough ||
                            (!solved && (r.outcome != IntegrateOutcome::ResourceExceeded ||
                                         r.status != DerivationStatus::ResourceLimitReached ||
                                         r.antiderivative != kNoNode)))
                            ++wrong_boundary;
                        if (d.context.resource_policy != budget_policy(budget))
                            ++lost_policy;
                    }
                }
            }
        }
        t.check(over_budget == 0, "integration and its derivative check share one enforced budget");
        t.check(wrong_boundary == 0, "combined rewrite and step boundaries include verification and reporting");
        t.check(lost_policy == 0, "every aggregate budget outcome records the original policy");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2").root;
        unsigned polls = 0;
        Budget budget;
        budget.poll = [](void *state) { return ++*static_cast<unsigned *>(state) == 2; };
        budget.poll_context = &polls;
        const IntegrateResult r = integrate(arena, d, expression, arena.symbol("x"), budget);
        t.check(r.outcome == IntegrateOutcome::Cancelled && polls == 2,
                "cancellation is polled on entry to the derivative verification phase");
        t.check(r.antiderivative == kNoNode && d.size() > 0 && d.all_verified_from(0),
                "phase cancellation keeps checked integration steps without offering an answer");
        t.check(r.cost.rewrites == 1 && r.cost.steps == 2 && r.cost.backend_calls == 0,
                "phase cancellation charges no derivative or backend work");
    }
    {
        // An integrand this build handles two characters shorter. Past the depth limit the power
        // rule's own answer stops fitting, and the missing node used to read as a form with no
        // rule, telling a student their integrand is unsupported when the arena ran out. Swept so
        // the check does not depend on which k first exceeds the shipped depth.
        size_t mislabelled = 0;
        size_t exhausted = 0;
        for (size_t k = 55; k <= 70; ++k) {
            std::string sum = "x";
            for (size_t i = 0; i < k; ++i)
                sum += "+1";
            Arena arena;
            Derivation d;
            NodeId e = parse(arena, "(" + sum + ")^2").root;
            NodeId v = arena.symbol("x");
            const IntegrateResult r = integrate(arena, d, e, v);
            if (!arena.failed())
                continue;
            ++exhausted;
            if (r.outcome == IntegrateOutcome::UnsupportedForm)
                ++mislabelled;
        }
        t.check(exhausted > 0, "the sweep reaches an arena that ran out, so it is testing the case");
        // The prober's minimal case, which faulted rather than returned: the power rule builds
        // integer(n+1) into a full arena and passed kNoNode straight into a read. Reaching the
        // assertion below is itself the evidence that it no longer faults, since a fault takes the
        // process with it, and the count is what says the outcome is also labelled honestly.
        size_t answered_without_answer = 0;
        for (size_t cap = 1; cap <= 24; ++cap) {
            Limits limits;
            limits.max_nodes = cap;
            Arena arena(limits);
            Derivation d;
            NodeId e = parse(arena, "x^2").root;
            NodeId v = arena.symbol("x");
            const IntegrateResult r = integrate(arena, d, e, v);
            if (arena.failed() && r.outcome == IntegrateOutcome::UnsupportedForm)
                ++mislabelled;
            if (r.outcome == IntegrateOutcome::Integrated && r.antiderivative == kNoNode)
                ++answered_without_answer;
        }
        t.check(answered_without_answer == 0,
                "integrating x^2 under every tight node cap returns without faulting and never "
                "claims an antiderivative it does not have");
        t.check(mislabelled == 0,
                "and an integrand the arena ran out on is reported as a resource limit rather "
                "than as a form with no rule");
    }
    t.check(agrees("5", "x", "5*x"), "a constant integrates to the constant times the variable");
    t.check(agrees("0", "x", "0"), "zero integrates to zero");
    t.check(agrees("y", "x", "y*x"), "another symbol is a constant here");
    t.check(agrees("x", "x", "x^2/2"), "the variable is its own first power");
    t.check(agrees("x^2", "x", "x^3/3"), "the power rule");
    t.check(agrees("x^-2", "x", "-x^-1"), "and with a negative exponent");
    t.check(agrees("1/x", "x", "ln(x)"), "one over the variable is the logarithm");
    t.check(agrees("x^2 + x", "x", "x^3/3 + x^2/2"), "a sum integrates term by term");
    t.check(agrees("3x^2", "x", "x^3"), "a constant multiple keeps the constant");
    t.check(agrees("x/2", "x", "x^2/4"), "and a constant divisor is a constant multiple");
    t.check(agrees("k*x", "x", "k*x^2/2"), "a symbolic constant comes out too");
    t.check(agrees("-sin(x)", "x", "cos(x)"), "a negation integrates through");
    t.check(agrees("sin(x)", "x", "-cos(x)"), "sine");
    t.check(agrees("cos(x)", "x", "sin(x)"), "cosine");
    t.check(agrees("exp(x)", "x", "exp(x)"), "the exponential is its own antiderivative");
    for (const auto &example : {
             std::pair{"sqrt(x)", "2*x*sqrt(x)/3"},
             std::pair{"sqrt(2*x+1)", "(2*x+1)*sqrt(2*x+1)/3"},
             std::pair{"sqrt(1-2*x)", "-(1-2*x)*sqrt(1-2*x)/3"},
             std::pair{"sqrt(a*x+b)", "(2*(a*x+b)*sqrt(a*x+b)/3)/a"}}) {
        IdentityBackend backend;
        t.check(agrees(example.first, "x", example.second, &backend),
                std::string("affine square-root primitive: ") + example.first);
    }
    {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        const auto integral = integrate(arena, derivation, parse(arena, "sqrt(a*x+b)").root,
                                        arena.symbol("x"), Budget(), &backend);
        std::string conditions;
        for (const auto &condition : derivation.context.active_assumptions) conditions += condition + " ";
        t.check(integral.outcome == IntegrateOutcome::Integrated && conditions.find(">= 0") != std::string::npos &&
                    conditions.find("a is not zero") != std::string::npos,
                "square-root substitution retains nonnegative argument and nonzero coefficient conditions");
        bool explained = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &step = derivation.at(static_cast<StepId>(i));
            if (step.rule_id == "i.linear-substitution")
                explained = step.explanation_detailed.find("3/2") != std::string::npos &&
                            step.explanation_detailed.find("zero endpoint") != std::string::npos;
        }
        t.check(explained, "square-root step explains the power rule and continuous endpoint value");
    }
    for (const char *source : {"sqrt(-1)", "sqrt(x^2+1)", "sqrt(sin(x))", "1/sqrt(x)"}) {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        const auto integral = integrate(arena, derivation, parse(arena, source).root,
                                        arena.symbol("x"), Budget(), &backend);
        t.check(integral.antiderivative == kNoNode && backend.calls == 0,
                std::string("unsupported square-root neighbors stop before verification: ") + source);
    }

    for (const auto &example : {
             std::pair{"ln(x)", "x*ln(x)-x"},
             std::pair{"ln(2*x+1)", "(2*x+1)*ln(2*x+1)/2-x"},
             std::pair{"ln(1-2*x)", "-(1-2*x)*ln(1-2*x)/2-x"},
             std::pair{"-3*ln(x/2)", "-3*(x*ln(x/2)-x)"},
             std::pair{"ln(x)+ln(x+1)", "(x*ln(x)-x)+((x+1)*ln(x+1)-x)"},
             std::pair{"ln(a*x+b)", "(a*x+b)*ln(a*x+b)/a-x"}}) {
        IdentityBackend backend;
        t.check(agrees(example.first, "x", example.second, &backend),
                std::string("logarithm primitive checked by differentiation: ") + example.first);
        t.check(uses_rule(example.first, "x", "i.logarithm-parts"),
                "logarithmic integration records integration by parts");
    }
    {
        IdentityBackend backend;
        t.check(agrees("ln(3*t+1)", "t", "(3*t+1)*ln(3*t+1)/3-t", &backend),
                "logarithm integration uses the requested variable");
    }
    {
        const Integrated local = run("ln(x)", "x");
        t.check(local.outcome == IntegrateOutcome::Refused && local.general.empty() &&
                    local.status == "partially solved" && local.cost.backend_calls == 0 &&
                    local.verifications.find("inconclusive") != std::string::npos,
                "inconclusive canonical comparison withholds the primitive without claiming a wrong identity");
    }
    for (const ResultTag tag : {ResultTag::Exact, ResultTag::Approximate, ResultTag::Conditional,
                               ResultTag::Unevaluated, ResultTag::BackendError, ResultTag::MalformedResult,
                               ResultTag::UnsupportedOperation, ResultTag::Cancelled,
                               ResultTag::ResourceFailure, ResultTag::Timeout}) {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        backend.tag = tag;
        const NodeId expression = parse(arena, "ln(a*x+b)").root;
        const IntegrateResult integral = integrate(arena, derivation, expression, arena.symbol("x"),
                                                   Budget(), &backend);
        const bool terminal = tag == ResultTag::Cancelled || tag == ResultTag::ResourceFailure ||
                              tag == ResultTag::Timeout;
        t.check(backend.calls == 1 && integral.cost.backend_calls == 1 &&
                    backend.target.find("is_zero:") == 0,
                "the primitive identity makes one structured and metered backend call");
        // A tie-break that ran out of room leaves the check inconclusive, which is what every other
        // unusable tag already reports. Only cancellation stops the solve, because only the learner
        // asked it to. The backend is not asked again either way, which is what calls == 1 above is.
        t.check(tag == ResultTag::Exact
                    ? integral.outcome == IntegrateOutcome::Integrated &&
                      integral.status == DerivationStatus::SolvedAndVerified
                    : integral.antiderivative == kNoNode &&
                      (tag == ResultTag::Cancelled
                           ? integral.status == DerivationStatus::Cancelled
                           : integral.status == DerivationStatus::PartiallySolved),
                std::string("primitive verification preserves response status: ") + tag_name(tag));
        if (!terminal) {
            bool residual_check = false;
            for (size_t i = 0; i < derivation.size(); ++i) {
                const CheckPayload *check = derivation.check(static_cast<StepId>(i));
                if (!check) continue;
                residual_check = check->expected_relation == "zero" &&
                    (tag == ResultTag::Exact ? check->observed_result == "0"
                                            : check->observed_result.find(tag_name(tag)) == 0);
            }
            t.check(residual_check, "Giac difference checks compare the returned residual with zero");
        }
        if (tag == ResultTag::Exact) {
            const std::string assumptions = [&] {
                std::string joined;
                for (const auto &assumption : derivation.context.active_assumptions) joined += assumption + " ";
                return joined;
            }();
            t.check(assumptions.find("> 0") != std::string::npos && assumptions.find("a is not zero") != std::string::npos,
                    "Giac identity evidence retains positivity and the nonzero affine coefficient");
        }
    }
    for (const char *answer : {"1", "x"}) {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        backend.answer = answer;
        const IntegrateResult integral = integrate(arena, derivation, parse(arena, "ln(x)").root,
                                                   arena.symbol("x"), Budget(), &backend);
        t.check(integral.antiderivative == kNoNode &&
                    integral.status == (std::string(answer) == "1" ? DerivationStatus::VerificationFailed
                                                                 : DerivationStatus::PartiallySolved),
                "only an exact constant residual decides the identity");
        bool residual_shown = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const CheckPayload *check = derivation.check(static_cast<StepId>(i));
            if (check) residual_shown = check->expected_relation == "zero" && check->observed_result == answer;
        }
        t.check(residual_shown, "nonzero and symbolic residuals are displayed without substituting the native derivative");
    }
    {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        Budget budget;
        budget.max_backend_calls = 0;
        const IntegrateResult integral = integrate(arena, derivation, parse(arena, "ln(x)").root,
                                                   arena.symbol("x"), budget, &backend);
        t.check(integral.status == DerivationStatus::ResourceLimitReached &&
                    integral.antiderivative == kNoNode && backend.calls == 0,
                "the backend budget stops verification before entering Giac");
    }
    {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        bool cancelled = false;
        backend.cancel = &cancelled;
        Budget budget;
        budget.poll_context = &cancelled;
        budget.poll = [](void *state) { return *static_cast<bool *>(state); };
        const IntegrateResult integral = integrate(arena, derivation, parse(arena, "ln(x)").root,
                                                   arena.symbol("x"), budget, &backend);
        t.check(integral.status == DerivationStatus::Cancelled && integral.antiderivative == kNoNode &&
                    backend.calls == 1, "cancellation raised during the identity check withholds the answer");
    }
    {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        backend.exhaust = true;
        const IntegrateResult integral = integrate(arena, derivation, parse(arena, "ln(x)").root,
                                                   arena.symbol("x"), Budget(), &backend);
        t.check(integral.status == DerivationStatus::ResourceLimitReached && integral.antiderivative == kNoNode &&
                    derivation.size() == 0 && backend.calls == 1,
                "arena exhaustion during identity verification drops unusable steps and withholds the answer");
    }
    for (const char *source : {"ln(0)", "ln(-1)", "ln(x^2)", "ln(sin(x))", "x*ln(x)", "ln(x)^2"}) {
        Arena arena;
        Derivation derivation;
        IdentityBackend backend;
        const IntegrateResult integral = integrate(arena, derivation, parse(arena, source).root,
                                                   arena.symbol("x"), Budget(), &backend);
        t.check(integral.outcome != IntegrateOutcome::Integrated && backend.calls == 0,
                std::string("invalid domains and unsupported logarithms stop before backend verification: ") + source);
    }

    // Linear substitution: the argument or base is a x + b, so the answer divides by a.
    t.check(agrees("sin(2x)", "x", "-cos(2x)/2"), "sine of a linear argument");
    t.check(agrees("exp(3x + 1)", "x", "exp(3x + 1)/3"), "the exponential of a linear argument");
    t.check(agrees("(x + 1)^3", "x", "(x + 1)^4/4"), "a power of a linear base");
    t.check(agrees("(2x + 1)^2", "x", "(2x + 1)^3/6"), "and one with a coefficient");
    t.check(agrees("1/(x + 1)", "x", "ln(x + 1)"), "the reciprocal of a linear base");
    t.check(agrees("1/(2x)", "x", "ln(2x)/2"), "and one with a coefficient");
    t.check(agrees("cos(-x)", "x", "-sin(-x)"), "a negated argument is linear with coefficient -1");
    t.check(uses_rule("sin(2x)", "x", "i.linear-substitution"), "a linear argument records the substitution");
    t.check(!uses_rule("sin(x)", "x", "i.linear-substitution"), "and the bare variable does not");
    t.check(uses_rule("sin(x)", "x", "i.function"), "which is the function rule instead");
    t.check(uses_rule("3x^2", "x", "i.constant-multiple"), "a constant multiple records itself");
    t.check(uses_rule("x^2", "x", "i.constant-of-integration"), "every answer adds its constant");

    {
        Integrated s = run("x^2", "x");
        t.equal(integrate_outcome_name(s.outcome), "integrated", "the power rule case works");
        t.equal(s.general, "(((x^3) * (3^-1)) + C)", "and the general antiderivative carries C");
        t.equal(s.status, "solved and verified", "with every step checked, the derivative included");
        t.check(s.steps == 4, "recorded as a plan, the power rule, the constant and the check");
        t.check(s.cost.steps >= s.steps, "the cost includes the derivative check's own steps");
        // The status above reads the outcomes and never the sentences, so a detail that reverts to
        // a constant leaves it saying solved and verified.
        t.equal(s.verifications,
                "passed, every visited form matched a registered antiderivative rule | "
                "passed, every matched inner form met its registered linearity requirement | "
                "passed, every registered strategy precondition has passed evidence | "
                "passed, the base is the variable and the exponent is a constant integer other "
                "than minus one | "
                "passed, the derivative of a constant is zero | "
                "passed, the derivative of the result is the integrand",
                "and the inverse check names differentiating the answer back");
        t.evidence("CALC-004",
                   s.outcome == IntegrateOutcome::Integrated &&
                       s.general == "(((x^3) * (3^-1)) + C)" && s.status == "solved and verified" &&
                       uses_rule("x^2", "x", "i.constant-of-integration"),
                   "a supported elementary integral includes C and passes its inverse check");
    }
    {
        Integrated s = run("C*x", "x");
        t.check(s.general.find("C1") != std::string::npos,
                "an integrand that uses C gets C1 as its constant");
    }
    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2").root;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"));
        t.check(r.outcome == IntegrateOutcome::Integrated, "the structural case integrates");
        t.check(d.roots().size() == 1, "one plan at the root");
        const StepId plan = d.roots().empty() ? kNoStep : d.roots()[0];
        t.check(plan != kNoStep && d.at(plan).children.size() == 3,
                "with the rule, the constant and the check under it");
        if (plan != kNoStep && d.at(plan).children.size() == 3) {
            StepId check = d.at(plan).children[2];
            t.check(d.at(check).kind == StepKind::Check, "the last child is the check");
            t.check(d.at(check).verified(), "and it passed");
            const CheckPayload *p = d.check(check);
            t.evidence("VER-005", p && p->observed_result == "(x^2)",
                       "the derivative of the answer is the integrand");
        }
        t.equal(d.context.problem_family_id, "calculus.integral.indefinite.single-variable",
                "the context names the problem family");
        t.check(d.context.active_assumptions.empty(), "a polynomial assumes nothing");
        t.check(d.context.normalized_problem_model == e, "and records the expression it was given");
        t.evidence("MATH-010", d.context.branch_convention.find("real domain") != std::string::npos,
                   "integration records the real default domain");
    }
    {
        // 1/x^2 parses as a power whose base is a power, and was refused as needing a substitution
        // while the identical x^-2 integrated. Confirmed against SymPy: both give -1/x.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "1/x^2").root;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"));
        t.check(r.outcome == IntegrateOutcome::Integrated,
                "a reciprocal square integrates rather than being refused for its spelling");

        Arena other;
        Derivation d2;
        NodeId e2 = parse(other, "x^-2").root;
        IntegrateResult r2 = integrate(other, d2, e2, other.symbol("x"));
        t.check(r2.outcome == IntegrateOutcome::Integrated, "and so does the other spelling");
        t.equal(print(arena, canonicalize(arena, r.antiderivative)),
                print(other, canonicalize(other, r2.antiderivative)),
                "the two spellings reach the same antiderivative");
    }
    {
        // MATH-005: the logarithm's domain is recorded on the step and in the context, not hidden.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "1/(x + 1)").root;
        integrate(arena, d, e, arena.symbol("x"));
        bool restricted = false;
        for (size_t i = 0; i < d.size(); ++i) {
            for (const std::string &r : d.at(static_cast<StepId>(i)).domain_restrictions)
                restricted = restricted || r == "(x + 1) > 0";
        }
        t.check(restricted, "the logarithm step restricts its argument to positive values");
        t.check(d.context.active_assumptions.size() == 1 &&
                    d.context.active_assumptions[0] == "(x + 1) > 0",
                "and the context lists the same assumption");
        t.evidence("MATH-005",
                   restricted && d.context.active_assumptions.size() == 1 &&
                       d.context.active_assumptions[0] == "(x + 1) > 0",
                   "the logarithm domain restriction is retained on its step and solution context");
    }
    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "sin(k*x)").root;
        integrate(arena, d, e, arena.symbol("x"));
        t.check(d.context.active_assumptions.size() == 1 &&
                    d.context.active_assumptions[0] == "k is not zero",
                "a symbolic coefficient records that it was assumed non-zero");
    }
    {
        Integrated s = run("x*sin(x)", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form",
                "a product of two varying factors is refused rather than guessed at");
        t.check(s.detail.find("parts") != std::string::npos, "and names integration by parts");
        t.check(s.steps == 0, "leaving nothing in the record");
    }
    {
        // A product whose factors are powers of one base is that base to the sum of the exponents,
        // so the power rule answers it and the product router never sees it. The canonical form
        // leaves x * x as a product, so this has to be a recorded rewrite rather than a comparison:
        // asserting the answer alone passes for a gather that never reached the integrand the
        // derivative check compares against, which is what "solved and verified" below catches.
        struct Gathered {
            const char *input;
            const char *expected;
            const char *rule;
        };
        for (const Gathered &example : {
                 Gathered{"x*x", "x^3/3", "i.power"},
                 Gathered{"x*x^2", "x^4/4", "i.power"},
                 Gathered{"x^2*x^3", "x^6/6", "i.power"},
                 Gathered{"2*x*x", "2*x^3/3", "i.power"},
                 Gathered{"x*x+sin(x)", "x^3/3-cos(x)", "i.power"},
                 Gathered{"(2x+1)*(2x+1)", "(2x+1)^3/6", "i.linear-substitution"},
             }) {
            Integrated s = run(example.input, "x");
            t.equal(integrate_outcome_name(s.outcome), "integrated",
                    std::string("a product of repeated factors integrates: ") + example.input);
            t.check(agrees(example.input, "x", example.expected),
                    std::string("and gives the power rule's answer: ") + example.input);
            t.equal(s.status, "solved and verified",
                    std::string("and the derivative check sees one integrand: ") + example.input);
            t.check(uses_rule(example.input, "x", "alg.gather-powers"),
                    std::string("and the gathering is a step of its own: ") + example.input);
            t.check(uses_rule(example.input, "x", example.rule),
                    std::string("and the power rule is what answered it: ") + example.input);
        }
    }
    {
        Integrated s = run("sin(x)*sin(x)", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form",
                "a repeated factor with a base no rule can substitute for is still refused");
        t.check(s.detail.find("not linear") != std::string::npos,
                "and the refusal names the base rather than integration by parts");
    }
    {
        // x * x^-1 is 1 away from zero and undefined at it. Gathering it would write x^0, which
        // carries no condition, so the negative exponent is left as the factor it is and the
        // non-zero condition the integrand states survives with it.
        Arena arena;
        Derivation d;
        const NodeId e = parse(arena, "x*x^-1").root;
        const IntegrateResult r = integrate(arena, d, e, arena.symbol("x"));
        t.equal(integrate_outcome_name(r.outcome), "unsupported form",
                "a factor and its reciprocal are not gathered into a power");
        const std::vector<Restriction> held = restrictions_of(arena, e);
        t.check(held.size() == 1 && restriction_text(arena, held[0]) == "x is not zero",
                "and the non-zero condition that product carries is still readable");
        t.check(!uses_rule("x*x^-1", "x", "alg.gather-powers"),
                "and no gathering step was recorded for it");
    }
    {
        Integrated s = run("x^x", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form", "a variable exponent is refused");
        t.check(s.detail.find("exp(x)") != std::string::npos, "and points at exp for the exponential");
    }
    {
        Integrated s = run("tan(x)", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form", "a function with no rule is refused");
        t.check(s.detail.find("tan") != std::string::npos, "and names the function");
    }
    {
        Integrated s = run("sin(x^2)", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form",
                "a non-linear argument is refused");
        t.check(s.detail.find("linear") != std::string::npos, "and says what it needed");
    }
    {
        Integrated s = run("x^(1/2)", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form", "a fractional exponent is refused");
        t.check(s.detail.find("integer") != std::string::npos, "and says only integers are supported");
    }
    {
        // The failing term is second, so the square was integrated and checked before tan(x) was
        // reached, and STEP-025 asks for that prefix to survive the refusal.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2 + tan(x)").root;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"));
        t.equal(integrate_outcome_name(r.outcome), "unsupported form", "a refusal inside a sum refuses");
        t.equal(derivation_status_name(d.context.derivation_status), "partially solved",
                "and reports the part it did integrate rather than nothing at all");
        t.check(d.size() == 3, "keeping the plan, the sum rule and the term it integrated");
        t.check(r.antiderivative == kNoNode,
                "while offering no antiderivative for the goal it never reached");

        // VER-013 as the requirement words it, rather than through an emptied record, which was only
        // ever one way of making it true and stopped being available when the prefix started
        // surviving. A plan reads as verified exactly when its preconditions are settled, which is
        // also what decides whether the prefix is kept at all.
        bool plan_settled = true;
        for (size_t i = 0; i < d.size(); ++i) {
            const StepId id = static_cast<StepId>(i);
            if (d.at(id).kind == StepKind::Plan && !d.at(id).verified())
                plan_settled = false;
        }
        t.evidence("VER-013", plan_settled,
                   "an unsupported integration strategy leaves no plan with unevaluated preconditions");
        t.evidence("STEP-025", d.size() == 3 && r.antiderivative == kNoNode,
                   "a partially solved integral keeps its verified prefix and shows no terminal "
                   "answer for the goal");
    }
    {
        // The pair, and the guard on the claim above. tan(x) comes first, so nothing was integrated
        // before the refusal and there is no prefix for the status to point at.
        Integrated s = run("tan(x) + x^2", "x");
        t.equal(integrate_outcome_name(s.outcome), "unsupported form",
                "a refusal on the first term refuses");
        t.equal(s.status, "unsupported", "and says it got nowhere");
        t.check(s.steps == 0, "keeping nothing rather than a sum rule that restates the problem");
    }
    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2 + x^3 + x^4").root;
        Budget tight;
        tight.max_steps = 2;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"), tight);
        t.equal(integrate_outcome_name(r.outcome), "resource exceeded", "a step budget stops the solve");
        t.check(r.antiderivative == kNoNode, "with no antiderivative offered");
        t.equal(derivation_status_name(d.context.derivation_status), "resource limit reached",
                "the context records why it stopped");
    }
    {
        // STEP-025 through a composite, and the condition a kept step needs. The logarithm is the
        // one antiderivative rule that introduces a restriction, so a halt after it is the case that
        // shows the halt path settling its conditions rather than dropping them: it never did, which
        // was invisible for as long as a halt kept nothing.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "1/x + sin(x)").root;
        Budget tight;
        // Three, not two: the reciprocal reaches its rule through one more move than a plain power
        // does, so two stops before anything lands and there is correctly nothing to keep.
        tight.max_steps = 3;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"), tight);
        t.equal(integrate_outcome_name(r.outcome), "resource exceeded",
                "the halt inside the sum still stops the solve");
        t.check(r.antiderivative == kNoNode, "offering no antiderivative");
        t.check(d.size() > 0, "but keeping the sum rule and the logarithm under it");
        bool carried = false;
        for (size_t i = 0; i < d.size(); ++i) {
            if (!d.restrictions_at(static_cast<StepId>(i)).empty())
                carried = true;
        }
        t.evidence("STEP-003", carried,
                   "and the logarithm step kept through the halt carries the condition its answer "
                   "needs");
    }
    {
        // Verification must fit in the work budget left by integration.
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x^2 + x^3").root;
        Budget tight;
        tight.max_steps = 5;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"), tight);
        t.equal(integrate_outcome_name(r.outcome), "resource exceeded",
                "a budget the check cannot fit in halts the solve");
        // STEP-025. This is the case the requirement is written for: the rules all fit, so their
        // steps were each checked, and only the check on the whole answer ran out. The rules stay
        // and the answer does not, because nothing confirmed it differentiates back.
        t.check(d.size() > 0, "keeping the rules that were checked");
        t.check(d.all_verified_from(0), "each of which passed");
        t.check(r.antiderivative == kNoNode, "while offering no antiderivative");
        t.equal(derivation_status_name(d.context.derivation_status), "resource limit reached",
                "and saying what stopped it");
    }
    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "sin(2x)").root;
        Budget cancelling;
        cancelling.poll = always_cancel;
        IntegrateResult r = integrate(arena, d, e, arena.symbol("x"), cancelling);
        t.equal(integrate_outcome_name(r.outcome), "cancelled", "a cancel poll stops the solve");
        t.check(d.size() == 0, "and leaves no partially accepted derivation");
        t.equal(derivation_status_name(d.context.derivation_status), "not recorded",
                "the context claims no outcome, because nothing survived");
    }
    {
        // A divisor that is zero only once its like terms are gathered. This used to integrate to a
        // complete verified answer carrying the restriction "(y + (-y)) is not zero", a condition no
        // value of y can meet, so the derivation ruled out every value and read as diligence while
        // doing it. Both halves are asserted: that nothing is offered, and that nothing is recorded.
        const char *undefined[4] = {"1/(y-y)", "x/(y-y)", "x^2/(y-y)", "(y-y)^-1"};
        for (int i = 0; i < 4; ++i) {
            Arena arena;
            Derivation d;
            NodeId e = parse(arena, undefined[i]).root;
            IntegrateResult r = integrate(arena, d, e, arena.symbol("x"));
            t.equal(integrate_outcome_name(r.outcome), "unsupported form",
                    std::string(undefined[i]) + " has no value, so it is refused rather than solved");
            t.check(r.antiderivative == kNoNode && d.size() == 0,
                    std::string(undefined[i]) + " offers no answer and records no derivation");
        }
    }
    {
        Arena arena;
        Derivation d;
        NodeId e = parse(arena, "x").root;
        NodeId two = parse(arena, "2").root;
        IntegrateResult r = integrate(arena, d, e, two);
        t.equal(integrate_outcome_name(r.outcome), "not a variable",
                "integrating with respect to a number is refused");
        t.check(d.size() == 0, "and nothing is recorded for a request that was never valid");
    }

    {
        // The case that raised the mode. Exact mode reads the typed 0.5 as a half and answers, the
        // way the native machine does, and the reading is a step rather than a silent rewrite.
        const Integrated promoted = run_in("sin(0.5*x)", "x", NumericMode::Exact);
        t.equal(integrate_outcome_name(promoted.outcome), "integrated",
                "exact mode reads a typed decimal as the rational it names and integrates");
        t.equal(promoted.general, "(((-cos(((1 * (2^-1)) * x))) * 2) + C)",
                "to minus two cosine of a half x, in exact form");
        t.check(promoted.general.find(".") == std::string::npos,
                "and no decimal survives into an exact answer");
        t.check(promoted.rules.find("num.decimal-to-rational") != std::string::npos,
                "the promotion is a step the reader can see, which is the whole basis of allowing "
                "it");
        t.equal(promoted.mode, "exact",
                "the context records the mode the solve actually ran under");

        const Integrated approximated = run_in("sin(0.5*x)", "x", NumericMode::Decimal);
        t.equal(integrate_outcome_name(approximated.outcome), "integrated",
                "decimal mode integrates the same expression");
        t.equal(approximated.general, "(((-cos((0.5 * x))) * 2) + C)",
                "to minus two cosine of a half x, which is the antiderivative");
        t.equal(approximated.status, "solved and verified",
                "and it is verified, because every coefficient rewritten terminates exactly");
        t.equal(approximated.mode, "decimal", "and the context records decimal");

        // A measured decimal in an exponent is the one promotion that is not allowed, in either
        // mode: an exact power rule handed the two in 1.0+1.0 would claim a precision nobody typed.
        const Integrated laundered = run_in("x^(1.0+1.0)", "x", NumericMode::Decimal);
        t.equal(integrate_outcome_name(laundered.outcome), "unsupported form",
                "decimal mode refuses a decimal exponent rather than folding it to an exact one");
        t.check(laundered.detail.find("exponent") != std::string::npos,
                "and says it is the exponent that stopped it");

        // Nothing to convert, so decimal mode changes neither the answer nor the step count.
        const Integrated exact_both = run_in("x^2", "x", NumericMode::Decimal);
        t.equal(exact_both.general, run("x^2", "x").general,
                "an expression with no decimal and no terminating fraction reads the same in both "
                "modes");
        t.check(exact_both.steps == run("x^2", "x").steps, "and records the same number of steps");

        // A third has no decimal spelling, so decimal mode leaves it a third rather than rounding.
        const Integrated recurring = run_in("sin(x/3)", "x", NumericMode::Decimal);
        t.equal(integrate_outcome_name(recurring.outcome), "integrated",
                "decimal mode integrates a coefficient with no terminating decimal");
        t.check(recurring.general.find(".") == std::string::npos,
                "and reports it as a fraction rather than rounding it to fit the mode");

        // Two solves in one process, opposite modes, to prove the mode is per-solve rather than
        // anything the engine remembers between calls.
        const Integrated first = run_in("cos(0.25*x)", "x", NumericMode::Decimal);
        const Integrated second = run_in("cos(0.25*x)", "x", NumericMode::Exact);
        const Integrated third = run_in("cos(0.25*x)", "x", NumericMode::Decimal);
        t.check(first.general.find("0.25") != std::string::npos &&
                    second.general.find(".") == std::string::npos &&
                    first.general == third.general,
                "switching the mode between solves changes the next solve and not the one before");
    }

    {
        const Integrated s = run("0.1234567890123456789*x", "x");
        t.equal(integrate_outcome_name(s.outcome), "resource exceeded",
                "a decimal past the exact rational's capacity is refused as a resource limit");
        t.equal(s.status, "resource limit reached", "and carries the status the gates read");
        t.check(s.detail.find("more digits") != std::string::npos,
                "with the refusal naming the capacity it exceeded");
    }
}

}  // namespace nps
