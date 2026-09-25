#include "nps/steps/calculus.h"

#include "nps/core/evaluate.h"
#include "nps/core/canonical.h"
#include "nps/core/print.h"
#include "golden/golden.h"
#include "step_invariants.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {
class CalculusBackend : public Backend {
  public:
    std::vector<std::string> replies;
    std::vector<std::string> expected_commands;
    std::vector<std::string> commands;
    ResultTag interrupt = ResultTag::Exact;
    size_t interrupt_from = 0;
    bool typed(const Request &, Arena &, TypedResult *out) override {
        if (interrupt == ResultTag::Exact || commands.size() < interrupt_from) return false;
        commands.push_back("interrupted identity");
        out->tag = interrupt;
        return true;
    }
    bool eval(const std::string &command, std::string *out, std::string *error) override {
        const size_t index = commands.size();
        commands.push_back(command);
        if (!expected_commands.empty() &&
            (index >= expected_commands.size() || command != expected_commands[index])) {
            *error = "unexpected backend request";
            return false;
        }
        if (index >= replies.size()) return false;
        *out = replies[index];
        return true;
    }
    bool complete() const {
        return !expected_commands.empty() && commands == expected_commands &&
               commands.size() == replies.size();
    }
};
}

void run_calculus_tests(TestSink &t) {
    for (const auto &fixture : {
             std::pair{"defint_polynomial", "int(x^2,x,0,1)"},
             std::pair{"defint_zero_width", "int(x,x,2,2)"},
             std::pair{"defint_reciprocal", "int(1/x,x,1,2)"},
             std::pair{"defint_elementary", "int(sin(2*x),x,0,1)"},
             std::pair{"limit_continuous", "limit(x^2,x,2)"},
             std::pair{"limit_root_boundary", "limit(sqrt(x),x,0,1)"},
             std::pair{"limit_removable", "limit((x^2-1)/(x-1),x,1)"},
             std::pair{"limit_pole", "limit(1/x,x,0,1)"},
             std::pair{"limit_no_two_sided", "limit(1/x,x,0)"},
             std::pair{"limit_at_infinity", "limit((3*x^2+1)/(2*x^2+1),x,infinity)"}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = fixture.second;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, fixture.second, "x"));
        t.check(result.status == DerivationStatus::SolvedAndVerified, "every calculus golden records a completed derivation");
        const std::string answer = result.value != kNoNode ? print(arena, result.value)
            : result.does_not_exist ? "does not exist" : result.infinity > 0 ? "+infinity" : "-infinity";
        check_golden(t, fixture.first, "problem: " + std::string(fixture.second) + "\nresult: " + answer + "\n" +
                     render_derivation(arena, derivation));
    }
    struct Example { const char *command; const char *answer; };
    struct PrimitiveFixture {
        const char *name;
        const char *command;
        const char *answer;
        std::vector<std::string> backend_commands;
    };
    for (const auto &fixture : {
             PrimitiveFixture{
                 "defint_logarithm_affine", "int(ln(2*x+1),x,0,1/2)", "ln(2)-1/2",
                 {"simplify(((-1+((1+(2*x))*((1+(2*x)))^(-1))+ln((1+(2*x))))+"
                  "(-(ln((1+(2*x)))))))",
                  "integrate(ln(((2*x)+1)),x,0,(1*(2)^((-(1)))))",
                  "simplify((((-1*(2)^(-1))+ln(2))+"
                  "(-((ln(2)+(-((1*(2)^((-(1)))))))))))"}},
             PrimitiveFixture{
                 "defint_logarithm_reversed", "int(ln(x),x,2,1)", "1-2*ln(2)",
                 {"simplify(((-1+(x*(x)^(-1))+ln(x))+(-(ln(x)))))",
                  "integrate(ln(x),x,2,1)",
                  "simplify(((-1+(-1*(-2+(2*ln(2)))))+(-((1+(-((2*ln(2)))))))))"}},
             PrimitiveFixture{
                 "defint_square_root_endpoint", "int(sqrt(1-2*x),x,-3/2,1/2)", "8/3",
                 {"simplify(((-1*((-2*sqrt((1+(-2*x))))+"
                  "(-1*(1+(-2*x))*(sqrt((1+(-2*x))))^(-1)))*(3)^(-1))+"
                  "(-(sqrt((1+(-2*x)))))))",
                  "integrate(sqrt((1+(-((2*x))))),x,((-(3))*(2)^((-(1)))),"
                  "(1*(2)^((-(1)))))",
                  "simplify(((8*(3)^(-1))+(-((8*(3)^((-(1))))))))"}}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = fixture.command;
        CalculusBackend backend;
        backend.replies = {"0", fixture.answer, "0"};
        backend.expected_commands = fixture.backend_commands;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, fixture.command, "x"), Budget(), &backend);
        t.check(result.status == DerivationStatus::SolvedAndVerified && result.value != kNoNode,
                "elementary integral golden records verified endpoints and a final answer");
        t.check(backend.complete() && result.backend_compared && result.agrees,
                "elementary integral golden records the primitive identity and independent definite-integral comparison");
        const std::string answer = result.value == kNoNode ? "" : print(arena, result.value);
        check_golden(t, fixture.name, "problem: " + std::string(fixture.command) + "\nresult: " + answer + "\n" +
                     render_derivation(arena, derivation));
    }
    const Example examples[] = {
        {"int(x^2,x,0,1)", "1/3"}, {"int(3*x^2+2*x+1,x,-1,2)", "15"},
        {"int(x^2,x,1,0)", "-1/3"}, {"int(x^2,x,2,2)", "0"},
        {"int(2*t,t,1/2,3/2)", "2"}, {"int(5,x,-3,4)", "35"},
        {"int(1/x^2,x,1,2)", "1/2"},
        {"int(1/x^2,x,-2,-1)", "1/2"},
        {"int(1/x,x,-2,-1)", "ln(1)-ln(2)"},
        {"int(1/x,x,-1,-2)", "ln(2)-ln(1)"},
        {"int(1/(2*x+1),x,-2,-1)", "ln(1)/2-ln(3)/2"},
        {"int(1/(1-2*x),x,1,2)", "ln(1)/2-ln(3)/2"},
        {"int(1/x+1/(x+3),x,-2,-1)", "ln(1)+ln(2)-(ln(2)+ln(1))"},
        {"int(1/t,t,-3/2,-1/2)", "ln(1/2)-ln(3/2)"},
        {"int(ln(x),x,1,2)", "2*ln(2)-1"},
        {"int(ln(x),x,2,1)", "-1-(2*ln(2)-2)"},
        {"int(ln(2*x+1),x,0,1/2)", "ln(2)-1/2"},
        {"int(ln(1-2*x),x,-1/2,0)", "-(-ln(2)+1/2)"},
        {"int(ln(3*t+1),t,0,1/3)", "2*ln(2)/3-1/3"},
        {"int(sqrt(x),x,0,4)", "16/3"},
        {"int(sqrt(x),x,4,0)", "-16/3"},
        {"int(sqrt(2*x+1),x,-1/2,3/2)", "8/3"},
        {"int(sqrt(1-2*x),x,-3/2,1/2)", "8/3"},
        {"int(sqrt(3*t+1),t,0,1)", "14/9"},
        {"int(ln(x)+ln(x+1),x,1,2)", "(2*ln(2)-2)+(3*ln(3)-2)-(-1+(2*ln(2)-1))"},
        {"limit(x^2+3*x,x,2)", "10"}, {"limit(1/(x+1),x,0)", "1"},
        {"limit(sqrt(x),x,0,1)", "0"},
        {"limit(sqrt(-x),x,0,-1)", "0"},
        {"limit(sqrt(x^2),x,0)", "0"},
        {"limit(sqrt(x^3),x,0,1)", "0"},
        {"limit(sqrt((x-2)^4*(3-x)),x,2)", "0"},
        {"limit(sqrt((2*t-1)^3),t,1/2,1)", "0"},
        {"limit(sqrt(x-x),x,0)", "0"},
        {"limit(sqrt(0),x,0)", "0"},
        {"limit(exp(sqrt(x)),x,0,1)", "1"},
        {"limit(sqrt(x)+sqrt(x^2),x,0,1)", "0"},
        {"limit((x^2-1)/(x-1),x,1)", "2"},
        {"limit(sin(x)/x,x,0)", "1"},
        {"limit(sin(3*x)/(2*x),x,0,-1)", "3/2"},
        {"limit((1-cos(x))/x^2,x,0)", "1/2"},
        {"limit((exp(x)-1-x)/x^2,x,0,1)", "1/2"},
        {"limit((sin(x)-x)/x^3,x,0)", "-1/6"},
        {"limit(sin(x)^2/x^2,x,0)", "1"},
        {"limit(sin(t-2)/(t-2),t,2)", "1"},
        {"limit(ln(1+x)/x,x,0)", "1"},
        {"limit((sqrt(1+x)-1)/x,x,0)", "1/2"},
        {"limit(sin(x)/x+cos(x),x,0)", "2"},
        {"limit(sin(x)^3/x^2,x,0)", "0"},
        {"limit((x^3-8)/(x-2),x,2)", "12"},
        {"limit((x-1)^3/(x-1)^2,x,1)", "0"},
        {"limit((x-1)^2/(x^2-2*x+1),x,1)", "1"},
        {"limit(0/x,x,0)", "0"}, {"limit(1/x-1/x,x,0)", "0"},
        {"limit((t^2-4)/(t-2),t,2,1)", "4"},
        {"limit((3*x^2+1)/(2*x^2-5),x,infinity)", "3/2"},
        {"limit((3*x^2+1)/(2*x^2-5),x,-infinity)", "3/2"},
        {"limit(1/x,x,infinity)", "0"},
        {"lim(1/x,x,-\xE2\x88\x9E)", "0"},
        {"lim((3*x^2+1)/(2*x^2-5),x,\xE2\x88\x9E)", "3/2"},
        {"limit((x-x+2)/(x+1),x,infinity)", "0"},
    };
    for (const Example &example : examples) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = example.command;
        const Command command = parse_command(arena, example.command, "x");
        CalculusBackend backend;
        backend.replies = {"0", example.answer, "0"};
        const bool identity_needed = std::string(example.command).find("int(ln(") == 0 ||
                                     std::string(example.command).find("int(sqrt(") == 0;
        const CalculusResult result = calculus_walkthrough(arena, derivation, command, Budget(),
                                                          identity_needed ? &backend : nullptr);
        const NodeId expected = parse(arena, example.answer).root;
        t.check(result.value != kNoNode && canonicalize(arena, result.value) == canonicalize(arena, expected),
                std::string("native calculus answer: ") + example.command + " got " +
                (result.value == kNoNode ? result.detail : print(arena, result.value)));
        t.check(result.status == DerivationStatus::SolvedAndVerified && derivation.all_verified_from(0),
                std::string("native calculus evidence: ") + example.command);
        invariants::Pass audit;
        std::vector<std::string> broken;
        audit.walk(arena, derivation, false, true, &broken);
        t.check(broken.empty(), std::string("native calculus rule schema: ") + example.command +
                               (broken.empty() ? "" : " " + broken.front()));
        t.check(derivation.context.original_expression == example.command,
                "nested calculus retains the original requested command");
    }
    {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        backend.replies = {"-1/6", "0"};
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "limit((sin(x)-x)/x^3,x,0)", "x"), {}, &backend);
        size_t reductions = 0;
        bool excluded = false;
        bool nested_derivative = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &entry = derivation.at(static_cast<StepId>(i));
            if (entry.rule_id == "limit.lhopital") ++reductions;
            if (entry.rule_id == "limit.rational-form") excluded = !entry.domain_restrictions.empty();
            if (entry.rule_id == "d.function" && entry.parent != kNoStep) nested_derivative = true;
        }
        t.check(result.status == DerivationStatus::SolvedAndVerified && reductions == 3 && excluded && nested_derivative,
                "third-order elementary limit retains exclusions and three nested native derivative reductions");
        t.check(!result.answer_only && result.backend_compared && result.agrees && backend.commands.size() == 2,
                "elementary native limit compares the original request independently with Giac");
    }
    {
        Arena arena;
        Derivation derivation;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "limit(sqrt(x),x,0,1)", "x"));
        bool domain_proved = false;
        bool substitution_restricted = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &entry = derivation.at(static_cast<StepId>(i));
            if (entry.rule_id == "limit.real-domain")
                domain_proved = !entry.domain_restrictions.empty() &&
                    entry.explanation_short.find("order 1") != std::string::npos;
            if (entry.rule_id == "limit.continuity")
                substitution_restricted = domain_proved && !entry.domain_restrictions.empty();
        }
        t.check(result.status == DerivationStatus::SolvedAndVerified && substitution_restricted,
                "root boundary substitution follows an exact side-domain proof and retains its condition");
    }
    {
        Arena arena;
        Derivation derivation;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "int(1/x,x,-2,-1)", "x"));
        bool negative_branch = false;
        bool positive_branch = false;
        for (size_t i = 0; i < derivation.size(); ++i) {
            for (const std::string &condition : derivation.at(static_cast<StepId>(i)).domain_restrictions) {
                negative_branch = negative_branch || condition == "(-x) > 0";
                positive_branch = positive_branch || condition == "x > 0";
            }
        }
        t.check(result.status == DerivationStatus::SolvedAndVerified && negative_branch && !positive_branch,
                "the negative logarithm branch carries its own condition on the walkthrough");
    }
    for (const auto &example : {std::pair{"limit(1/x,x,0,1)", 1},
                                std::pair{"limit(sin(x)/x^2,x,0,1)", 1},
                                std::pair{"limit(sin(x)/x^2,x,0,-1)", -1},
                                std::pair{"limit((cos(x)-1)/x^4,x,0)", -1},
                                std::pair{"limit(1/x,x,0,-1)", -1},
                                std::pair{"limit(1/x^2,x,0)", 1},
                                std::pair{"limit(-2/x^2,x,0)", -1},
                                std::pair{"limit(x^3,x,-infinity)", -1},
                                std::pair{"limit(-x^2,x,infinity)", -1},
                                std::pair{"limit(x^3/(x+1),x,-infinity)", 1}}) {
        Arena arena;
        Derivation derivation;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, example.first, "x"));
        t.check(result.infinity == example.second && result.value == kNoNode && !result.does_not_exist &&
                    result.status == DerivationStatus::SolvedAndVerified && derivation.all_verified_from(0),
                std::string("native signed infinite limit: ") + example.first);
    }
    for (const char *text : {"limit(1/x,x,0)", "limit(-1/(x-2)^3,x,2)", "limit(sin(x)/x^2,x,0)"}) {
        Arena arena;
        Derivation derivation;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"));
        t.check(result.does_not_exist && result.infinity == 0 && result.value == kNoNode &&
                    result.status == DerivationStatus::SolvedAndVerified && derivation.all_verified_from(0),
                "opposite one-sided limits prove a two-sided limit does not exist");
    }
    for (const char *text : {"int(1/x,x,-1,1)", "int(1/x^2,x,-1,1)", "int(1/x,x,0,0)",
                              "int(1/(2*x+1),x,-1,0)", "int(1/x,x,-1,0)",
                              "int(ln(x),x,0,1)", "int(ln(x),x,-2,-1)", "int(ln(x),x,0,0)",
                              "int(ln(2*x+1),x,-1,1)", "int(ln(x^2),x,1,2)",
                              "int(sqrt(x),x,-1,1)", "int(x*sin(x),x,0,1)",
                              "int(sqrt(x),x,-2,-1)", "int(sqrt(1-2*x),x,0,1)",
                              "int(sqrt(x^2+1),x,0,1)", "int(1/sqrt(x),x,0,1)",
                              "limit(1/(x-x),x,0)", "limit(0/(x-x),x,0)",
                              "limit(sin(1/x),x,0)", "limit(sqrt(x),x,0,-1)",
                              "limit(sqrt(-x),x,0,1)", "limit(exp(sqrt(x)),x,0,-1)",
                              "limit(sqrt(x),x,0)", "limit(sqrt(-x^2),x,0)",
                              "limit(sqrt(-x^2),x,0,1)", "limit(sqrt(-x^2),x,0,-1)",
                              "limit(sqrt(x^3),x,0,-1)", "limit(sqrt(x)+sqrt(-x),x,0,1)",
                              "limit(1/sqrt(x),x,0,1)", "limit(sqrt(sin(x)),x,0,1)",
                              "limit(sqrt(x^2)/x,x,0)", "limit(sqrt(-x^2)/x,x,0)",
                              "limit(sin(1/x)/x,x,0)", "limit(ln(x)/x,x,0)",
                              "limit(sin(x)/(x-x),x,0)", "limit(sin(x)/x,x,infinity)"}) {
        Arena arena;
        Derivation derivation;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"));
        t.check(result.value == kNoNode && result.status != DerivationStatus::SolvedAndVerified,
                std::string("native calculus does not invent an answer: ") + text);
    }
    {
        // The status as well as the sentence. The shell prints it verbatim on the note line at
        // nps_v4.lua:2758-2759, which is a different reader from the badge: resultClass returns at
        // its no-answer gate before any status branch, so a refusal never reaches those.
        struct Refusal { const char *command; CalculusOutcome outcome; DerivationStatus status; const char *detail; };
        const Refusal refusals[] = {
            {"limit(x^33/x^33,x,infinity)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the native calculus engine handles polynomial degrees up to 32 and this request is higher"},
            {"limit(x^40/x^40,x,infinity)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the native calculus engine handles polynomial degrees up to 32 and this request is higher"},
            {"limit(x^21/x^21,x,infinity)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the leading coefficients of this expression overflow the exact arithmetic the native calculus engine uses"},
            {"limit(x^32/x^32,x,infinity)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the leading coefficients of this expression overflow the exact arithmetic the native calculus engine uses"},
            // Both orderings, because a ceiling reached before a genuinely unsupported factor must not
            // let the operand order decide which sentence the learner reads.
            {"limit((x^33+sin(x))/x,x,infinity)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported,
             "native limits at infinity currently require a rational function"},
            {"limit((sin(x)+x^33)/x,x,infinity)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported,
             "native limits at infinity currently require a rational function"},
            {"limit(sin(x)/x,x,infinity)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported,
             "native limits at infinity currently require a rational function"},
            {"limit(exp(x)/x,x,infinity)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported,
             "native limits at infinity currently require a rational function"},
            // The finite-limit route reaches the same ceilings through its own call sites, and the
            // table named none of them until a reviewer went looking.
            {"limit(x^33/x,x,0)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the native calculus engine handles polynomial degrees up to 32 and this request is higher"},
            {"limit((x^33+x)/x,x,0)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the native calculus engine handles polynomial degrees up to 32 and this request is higher"},
            {"limit(x^33/(x-1),x,1)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
             "the native calculus engine handles polynomial degrees up to 32 and this request is higher"},
        };
        for (const Refusal &refusal : refusals) {
            Arena arena;
            Derivation derivation;
            const CalculusResult result = calculus_walkthrough(arena, derivation,
                parse_command(arena, refusal.command, "x"));
            t.check(result.value == kNoNode && result.detail == refusal.detail,
                    std::string("a ceiling is not a claim about the expression: ") + refusal.command +
                        " got " + result.detail);
            t.check(result.status == refusal.status,
                    std::string("and the status says so too, not only the sentence: ") +
                        refusal.command + " got " + derivation_status_name(result.status));
            t.check(result.outcome == refusal.outcome,
                    std::string("and the outcome names the refusal in the engine's vocabulary: ") +
                        refusal.command + " got " + calculus_outcome_name(result.outcome));
        }
        for (const char *text : {"limit(x^20/x^20,x,infinity)", "limit(x^2/x^2,x,infinity)",
                                 "limit((2*x^3+1)/(x^3+5),x,infinity)"}) {
            Arena arena;
            Derivation derivation;
            const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"));
            t.check(result.value != kNoNode && result.status == DerivationStatus::SolvedAndVerified &&
                        result.outcome == CalculusOutcome::Evaluated,
                    std::string("the engine still answers below the ceilings: ") + text);
        }
        Budget tight;
        tight.max_rewrites = 1;
        Arena arena;
        Derivation derivation;
        const CalculusResult halted = calculus_walkthrough(arena, derivation,
            parse_command(arena, "limit(x^33/x^33,x,infinity)", "x"), tight);
        t.check(halted.value == kNoNode && halted.status == DerivationStatus::ResourceLimitReached &&
                    halted.outcome == CalculusOutcome::ResourceExceeded &&
                    halted.detail == "rewrite limit",
                "a halt inside the form check is still reported as a halt, not as a degree ceiling");
    }
    for (const ResultTag tag : {ResultTag::Cancelled, ResultTag::ResourceFailure, ResultTag::Timeout}) {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        backend.interrupt = tag;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "int(ln(x),x,1,2)", "x"), Budget(), &backend);
        t.check(result.value == kNoNode && !result.answer_only && backend.commands.size() == 1 &&
                    result.status == (tag == ResultTag::Cancelled ? DerivationStatus::Cancelled
                                                                 : DerivationStatus::ResourceLimitReached) &&
                    result.outcome == (tag == ResultTag::Cancelled ? CalculusOutcome::Cancelled
                                                                  : CalculusOutcome::ResourceExceeded),
                "a terminal primitive check forbids further Giac comparison or answer-only fallback");
    }
    {
        Arena control_arena;
        Derivation control_derivation;
        const CalculusResult alone = calculus_walkthrough(control_arena, control_derivation,
            parse_command(control_arena, "int(x^2,x,0,1)", "x"));
        t.check(alone.value != kNoNode && alone.status == DerivationStatus::SolvedAndVerified,
                "the native integral answers without a backend, so a backend that gives up has a "
                "local answer to discard");
        struct Ending {
            ResultTag tag;
            size_t interrupt_from;
            bool keeps_answer;
            DerivationStatus status;
        };
        const Ending endings[] = {
            {ResultTag::ResourceFailure, 0, true, DerivationStatus::SolvedButUnchecked},
            {ResultTag::Timeout, 0, true, DerivationStatus::SolvedButUnchecked},
            {ResultTag::Cancelled, 0, false, DerivationStatus::Cancelled},
            {ResultTag::ResourceFailure, 1, true, DerivationStatus::SolvedButUnchecked},
            {ResultTag::Timeout, 1, true, DerivationStatus::SolvedButUnchecked},
            {ResultTag::Cancelled, 1, false, DerivationStatus::Cancelled},
        };
        for (const Ending &ending : endings) {
            Arena arena;
            Derivation derivation;
            CalculusBackend backend;
            backend.replies = {"1/3", "0"};
            backend.interrupt = ending.tag;
            backend.interrupt_from = ending.interrupt_from;
            const CalculusResult result = calculus_walkthrough(arena, derivation,
                parse_command(arena, "int(x^2,x,0,1)", "x"), Budget(), &backend);
            const std::string where = std::string(tag_name(ending.tag)) +
                (ending.interrupt_from == 0 ? " on the original question: " : " on the comparison: ");
            t.check((result.value != kNoNode) == ending.keeps_answer,
                    where + "the native answer " +
                        (ending.keeps_answer ? "survives" : "is withdrawn"));
            t.equal(derivation_status_name(result.status), derivation_status_name(ending.status),
                    where + "the status says what happened");
            t.check(!result.backend_compared && !result.agrees,
                    where + "no comparison is claimed");
            size_t inconclusive = 0;
            std::string method;
            std::string relation;
            for (size_t i = 0; i < derivation.size(); ++i) {
                const Step &entry = derivation.at(static_cast<StepId>(i));
                if (entry.rule_id != "calculus.check-giac")
                    continue;
                for (size_t v = 0; v < entry.verifications.size(); ++v)
                    if (entry.verifications[v].outcome == VerificationOutcome::Inconclusive)
                        ++inconclusive;
                const CheckPayload *payload = derivation.check(static_cast<StepId>(i));
                if (payload) {
                    method = payload->check_method;
                    relation = payload->expected_relation;
                }
            }
            t.equal(std::to_string(inconclusive), ending.keeps_answer ? "1" : "0",
                    where + "the record carries the backend's refusal");
            // The first site never formed a difference, so it may claim neither the method that
            // forms one nor a value the learner was supposed to see it equal.
            t.equal(method,
                    !ending.keeps_answer ? std::string()
                    : ending.interrupt_from == 0
                        ? std::string("ask Giac the same question, which did not finish")
                        : std::string("Giac evaluation followed by exact difference "
                                      "simplification, which did not finish"),
                    where + "the payload says what was actually done");
            t.equal(relation,
                    ending.keeps_answer && ending.interrupt_from == 1 ? std::string("zero")
                                                                     : std::string(),
                    where + "nothing was expected of a comparison that never ran");
            invariants::Pass audit;
            std::vector<std::string> broken;
            audit.walk(arena, derivation, !ending.keeps_answer, true, &broken);
            t.check(broken.empty(), where + "the audit is clean" +
                                        (broken.empty() ? "" : " " + broken.front()));
        }
        for (const ResultTag tag : {ResultTag::ResourceFailure, ResultTag::Timeout,
                                    ResultTag::Cancelled}) {
            Arena arena;
            Derivation derivation;
            CalculusBackend backend;
            backend.interrupt = tag;
            const CalculusResult result = calculus_walkthrough(arena, derivation,
                parse_command(arena, "limit(sin(x)/sin(2*x),x,0)", "x"), Budget(), &backend);
            const std::string where = std::string(tag_name(tag)) + " with nothing native to keep: ";
            t.check(result.value == kNoNode && !result.answer_only && backend.commands.size() == 1,
                    where + "no answer is offered and Giac is not asked again");
            t.equal(derivation_status_name(result.status),
                    derivation_status_name(tag == ResultTag::Cancelled
                                               ? DerivationStatus::Cancelled
                                               : DerivationStatus::ResourceLimitReached),
                    where + "the backend's ceiling is not reported as a claim about the expression");
            invariants::Pass audit;
            std::vector<std::string> broken;
            audit.walk(arena, derivation, true, true, &broken);
            t.check(broken.empty(), where + "the audit is clean" +
                                        (broken.empty() ? "" : " " + broken.front()));
        }
    }
    for (size_t calls : {0U, 1U, 2U}) {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        backend.replies = {"0", "2*ln(2)-1", "0"};
        Budget budget;
        budget.max_backend_calls = calls;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "int(ln(x),x,1,2)", "x"), budget, &backend);
        t.check(result.value == kNoNode && result.status == DerivationStatus::ResourceLimitReached &&
                    backend.commands.size() == calls,
                "primitive verification and final calculus comparison share the backend budget");
    }
    for (const auto &answers : {std::vector<std::string>{"1/3", "0"}, std::vector<std::string>{"9", "1"}}) {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        backend.replies = answers;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "int(x^2,x,0,1)", "x"), {}, &backend);
        const bool agreement = answers[1] == "0";
        t.check(result.backend_compared && result.agrees == agreement && backend.commands.size() == 2,
                "calculus independently compares the original bounded request");
        t.check(agreement ? result.status == DerivationStatus::SolvedAndVerified && result.value != kNoNode
                          : result.status == DerivationStatus::VerificationFailed && result.value == kNoNode,
                "calculus withholds a failed independently compared answer");
        t.check(result.outcome == (agreement ? CalculusOutcome::Evaluated
                                             : CalculusOutcome::VerificationFailed),
                "and the outcome reports the comparison rather than repeating the status");
    }
    {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        backend.replies = {"1/2"};
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "limit(sin(x)/sin(2*x),x,0)", "x"), {}, &backend);
        t.check(result.answer_only && result.value != kNoNode &&
                    result.status != DerivationStatus::SolvedAndVerified && backend.commands.size() == 1,
                "unsupported native calculus can return a distinctly labeled backend answer");
        t.check(result.outcome == CalculusOutcome::UnsupportedForm,
                "and the native refusal stays the outcome, because the native engine did not answer");
    }
    for (unsigned damaged = 0; damaged < 6; ++damaged) {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        Command command = parse_command(arena, "limit(x,x,0)", "x");
        if (damaged == 0) command.point = kNoNode;
        if (damaged == 1) command.variable = arena.integer("2");
        if (damaged == 2) command.variable_name = "y";
        if (damaged == 3) command.direction = 2;
        if (damaged == 4) command.expression = kNoNode;
        if (damaged == 5) command.kind = CommandKind::Differentiate;
        const CalculusResult result = calculus_walkthrough(arena, derivation, command, {}, &backend);
        t.check(result.status == DerivationStatus::InvalidInput && result.value == kNoNode &&
                    result.outcome == CalculusOutcome::InvalidInput &&
                    derivation.size() == 0 && backend.commands.empty(),
                "incomplete direct calculus requests never enter native or backend evaluation");
    }
    for (const char *text : {"int(x^2,x,0,1)", "int(1/x,x,-2,-1)", "int(sqrt(x),x,0,4)", "limit((x^2-1)/(x-1),x,1)",
                            "limit((sin(x)-x)/x^3,x,0)"}) {
        for (size_t limit : {size_t{0}, size_t{1}, size_t{3}}) {
            Arena arena;
            Derivation derivation;
            Budget budget;
            budget.max_steps = limit;
            const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"), budget);
            t.check(result.value == kNoNode && result.status == DerivationStatus::ResourceLimitReached &&
                        result.outcome == CalculusOutcome::ResourceExceeded,
                    "calculus shares the complete nested step budget");
        }
        Arena arena;
        Derivation derivation;
        Budget budget;
        budget.poll = [](void *) { return true; };
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"), budget);
        t.check(result.value == kNoNode && result.status == DerivationStatus::Cancelled &&
                    result.outcome == CalculusOutcome::Cancelled && derivation.size() == 0,
                "calculus cancellation before work has no answer or steps");
    }
    for (const char *text : {"limit(sqrt(x),x,0,1)", "limit(sqrt((x-2)^4),x,2)"}) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            Arena arena;
            Derivation derivation;
            CalculusBackend backend;
            Budget budget;
            if (failure == 0) budget.max_steps = 1;
            if (failure == 1) budget.max_rewrites = 3;
            if (failure == 2) budget.poll = [](void *) { return true; };
            const CalculusResult result = calculus_walkthrough(arena, derivation,
                parse_command(arena, text, "x"), budget, &backend);
            t.check(result.value == kNoNode && backend.commands.empty() &&
                    result.outcome == (failure == 2 ? CalculusOutcome::Cancelled : CalculusOutcome::ResourceExceeded) &&
                    result.status == (failure == 2 ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached),
                    "root domain proofs share cancellation and resource limits without backend fallback: " +
                    std::string(text) + " failure " + std::to_string(failure) + ": " + result.detail);
        }
    }
    // CALC-010. The tangent line and the linearization share one engine, and the difference between
    // them is whether the answer is stated as an equality.
    for (const auto &fixture : {std::pair{"tangent", false}, std::pair{"linearize", true}}) {
        const std::string text = std::string(fixture.first) + "(x^2,x,3)";
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = text;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, text, "x"));
        t.check(result.outcome == CalculusOutcome::Evaluated && result.value != kNoNode &&
                result.status == DerivationStatus::SolvedAndVerified,
                "the tangent family answers a polynomial at a rational point: " + text + ": " + result.detail);
        t.check(result.slope != kNoNode && print(arena, result.slope) == "6" &&
                result.point_value != kNoNode && print(arena, result.point_value) == "9",
                "the tangent family reports the slope and the value it built the line from: " + text);
        t.check(result.approximate == fixture.second,
                "only the linearization states its answer as an approximation: " + text);
        Rational at_point;
        Rational away;
        t.check(evaluate_rational(arena, result.value, {{"x", Rational{3, 1}}}, &at_point) &&
                at_point.num == 9 && at_point.den == 1 &&
                evaluate_rational(arena, result.value, {{"x", Rational{4, 1}}}, &away) &&
                away.num == 15 && away.den == 1,
                "the assembled line meets the curve at the point and rises by the derivative: " + text);
        // VER-002. Both point substitutions instantiate the function at one point instead of
        // rewriting it, so a claim of equivalence would be false away from the point.
        size_t substitutions = 0;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &recorded = derivation.at(static_cast<StepId>(i));
            if (recorded.rule_id != "tangent.point-value" && recorded.rule_id != "tangent.slope")
                continue;
            ++substitutions;
            t.check(recorded.claim == ClaimType::Definition,
                    "the tangent family states its point substitutions as definitions rather than "
                    "equivalences: " + recorded.rule_id + " in " + text);
        }
        t.check(substitutions == 2,
                "the tangent family records both point substitutions: " + text + " has " +
                std::to_string(substitutions));
        const std::string rendered = render_derivation(arena, derivation);
        t.check(rendered.find("tangent.check-line") != std::string::npos,
                "the tangent family records its final tangency check: " + text);
        t.check(rendered.find(fixture.second ? "tangent.linearization" : "tangent.line") != std::string::npos,
                "the assembly step names the family that was asked for: " + text);
        check_golden(t, fixture.second ? "tangent_linearization" : "tangent_line",
                     "problem: " + text + "\nresult: " + print(arena, result.value) + "\n" + rendered);
    }
    // Neighboring refusals. A point outside the domain, a value that is not exact there, and a form
    // the differentiation engine has no rule for are each refused rather than answered.
    for (const char *text : {"tangent(1/x,x,0)", "linearize(1/x,x,0)", "tangent(sqrt(x),x,2)",
                             "tangent(x^2,x,sqrt(2))"}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = text;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, text, "x"));
        t.check(result.value == kNoNode && !result.detail.empty() &&
                (result.outcome == CalculusOutcome::UnsupportedForm ||
                 result.outcome == CalculusOutcome::Refused),
                "the tangent family refuses outside its envelope: " + std::string(text) + ": " +
                std::string(calculus_outcome_name(result.outcome)));
    }
    {
        Arena arena;
        const Command missing = parse_command(arena, "tangent(x^2,x)", "x");
        t.check(missing.status == CommandStatus::Unsupported && !missing.detail.empty(),
                "a tangent request without a point is refused before any work: " + missing.detail);
    }
    for (const char *text : {"tangent(x^2,x,3)", "linearize(x^3+x,x,2)"}) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            Arena arena;
            Derivation derivation;
            Budget budget;
            if (failure == 0) budget.max_steps = 1;
            if (failure == 1) budget.max_rewrites = 2;
            if (failure == 2) budget.poll = [](void *) { return true; };
            const CalculusResult result = calculus_walkthrough(arena, derivation,
                parse_command(arena, text, "x"), budget);
            t.check(result.value == kNoNode &&
                    result.outcome == (failure == 2 ? CalculusOutcome::Cancelled
                                                    : CalculusOutcome::ResourceExceeded) &&
                    result.status == (failure == 2 ? DerivationStatus::Cancelled
                                                   : DerivationStatus::ResourceLimitReached),
                    "the tangent family stops for cancellation and for its budgets: " +
                    std::string(text) + " failure " + std::to_string(failure) + ": " + result.detail);
        }
    }
    // CALC-011. Each fixture's polynomial is read back at two points against the value it must have,
    // so a wrong coefficient fails here as well as in the recorded check.
    struct TaylorCase {
        const char *golden;
        const char *text;
        bool approximate;
        Rational at_one;
        Rational at_two;
    };
    for (const TaylorCase &fixture : {
             TaylorCase{"taylor_maclaurin_exponential", "maclaurin(exp(x),x,3)", true, {8, 3}, {19, 3}},
             TaylorCase{"taylor_reciprocal_shifted", "taylor(1/x,x,1,3)", true, {1, 1}, {0, 1}},
             TaylorCase{"taylor_polynomial_exact", "taylor(x^3,x,1,3)", false, {1, 1}, {8, 1}},
             TaylorCase{"taylor_maclaurin_sine", "maclaurin(sin(x),x,5)", true, {101, 120}, {14, 15}}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = fixture.text;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, fixture.text, "x"));
        t.check(result.outcome == CalculusOutcome::Evaluated && result.value != kNoNode &&
                result.status == DerivationStatus::SolvedAndVerified,
                "the Taylor family answers inside its envelope: " + std::string(fixture.text) + ": " + result.detail);
        Rational one, two;
        t.check(result.value != kNoNode &&
                evaluate_rational(arena, result.value, {{"x", Rational{1, 1}}}, &one) &&
                evaluate_rational(arena, result.value, {{"x", Rational{2, 1}}}, &two) &&
                one.num == fixture.at_one.num && one.den == fixture.at_one.den &&
                two.num == fixture.at_two.num && two.den == fixture.at_two.den,
                "the Taylor polynomial has the coefficients the derivatives give: " + std::string(fixture.text) +
                " is " + (result.value == kNoNode ? std::string("missing") : print(arena, result.value)));
        t.check(result.approximate == fixture.approximate && result.remainder != kNoNode,
                "the Taylor family states its remainder and calls the polynomial exact only when it is zero: " +
                std::string(fixture.text));
        size_t values = 0, remainders = 0, restrictions = 0;
        for (size_t i = 0; i < derivation.size(); ++i) {
            const Step &recorded = derivation.at(static_cast<StepId>(i));
            if (recorded.rule_id == "taylor.derivative-value") {
                ++values;
                t.check(recorded.claim == ClaimType::Definition,
                        "a derivative value at the center is a definition rather than an equivalence: " +
                        std::string(fixture.text));
            }
            if (recorded.rule_id == "taylor.remainder") {
                ++remainders;
                restrictions = derivation.restrictions_at(static_cast<StepId>(i)).size();
            }
        }
        int64_t order = 0;
        const Command parsed = parse_command(arena, fixture.text, "x");
        t.check(folded_integer(arena, parsed.order, &order) && values == static_cast<size_t>(order) + 1 &&
                remainders == 1 && restrictions == (fixture.approximate ? 2u : 0u),
                "the Taylor family records one value per order, one remainder and its hypotheses: " +
                std::string(fixture.text) + " has " + std::to_string(values) + " values and " +
                std::to_string(restrictions) + " restrictions");
        const std::string rendered = render_derivation(arena, derivation);
        t.check(rendered.find("taylor.check-polynomial") != std::string::npos,
                "the Taylor family records its final derivative check: " + std::string(fixture.text));
        check_golden(t, fixture.golden, "problem: " + std::string(fixture.text) + "\nresult: " +
                     (result.value == kNoNode ? std::string() : print(arena, result.value)) + "\nremainder: " +
                     (result.remainder == kNoNode ? std::string() : print(arena, result.remainder)) + "\n" + rendered);
    }
    // The first-order polynomial is the linearization, which is the one place two families share an answer.
    for (const char *center : {"0", "2", "-1/2"}) {
        Arena arena;
        Derivation first, second;
        const std::string taylor = std::string("taylor(x^3-2*x,x,") + center + ",1)";
        const std::string line = std::string("linearize(x^3-2*x,x,") + center + ")";
        const CalculusResult polynomial = calculus_walkthrough(arena, first, parse_command(arena, taylor, "x"));
        const CalculusResult linear = calculus_walkthrough(arena, second, parse_command(arena, line, "x"));
        bool same = polynomial.value != kNoNode && linear.value != kNoNode;
        for (int64_t at = -2; same && at <= 2; ++at) {
            Rational a, b;
            same = evaluate_rational(arena, polynomial.value, {{"x", Rational{at, 1}}}, &a) &&
                   evaluate_rational(arena, linear.value, {{"x", Rational{at, 1}}}, &b) &&
                   a.num == b.num && a.den == b.den;
        }
        t.check(same, "the order one Taylor polynomial is the linearization at the same center: " + taylor);
    }
    // Neighboring refusals, each for the reason the learner can act on.
    struct TaylorRefusal {
        const char *text;
        CalculusOutcome outcome;
        DerivationStatus status;
    };
    for (const TaylorRefusal &refusal : {
             TaylorRefusal{"taylor(sin(x),x,1,2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             TaylorRefusal{"taylor(1/x,x,0,2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             TaylorRefusal{"maclaurin(sqrt(x),x,2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             TaylorRefusal{"taylor(x^2,x,sqrt(2),2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             TaylorRefusal{"maclaurin(asin(x),x,2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             TaylorRefusal{"maclaurin(exp(x),x,20)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = refusal.text;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, refusal.text, "x"));
        t.check(result.value == kNoNode && !result.detail.empty() && result.outcome == refusal.outcome &&
                result.status == refusal.status,
                "the Taylor family refuses outside its envelope for the right reason: " + std::string(refusal.text) +
                ": " + calculus_outcome_name(result.outcome) + ": " + result.detail);
    }
    {
        Arena arena;
        Derivation derivation;
        const CalculusResult beyond = calculus_walkthrough(arena, derivation, parse_command(arena, "maclaurin(exp(x),x,20)", "x"));
        t.check(beyond.detail.find("orders up to 19") != std::string::npos && derivation.size() == 0,
                "an order past the ceiling is refused as this build's limit before any derivative is taken: " +
                beyond.detail);
    }
    {
        Arena arena;
        Derivation derivation;
        const CalculusResult ceiling = calculus_walkthrough(arena, derivation, parse_command(arena, "maclaurin(exp(x),x,19)", "x"));
        t.check(ceiling.outcome == CalculusOutcome::Evaluated && ceiling.status == DerivationStatus::SolvedAndVerified,
                "order 19 of a transcendental is inside the factorial ceiling and the default budget: " + ceiling.detail);
    }
    for (const auto &malformed : {std::pair{"taylor(x^2,x,0,-1)", CommandStatus::Invalid},
                                  std::pair{"taylor(x^2,x,0,1/2)", CommandStatus::Invalid},
                                  std::pair{"taylor(x^2,x,0)", CommandStatus::Unsupported},
                                  std::pair{"maclaurin(x^2,x)", CommandStatus::Unsupported},
                                  std::pair{"maclaurin(x^2,x,0,2)", CommandStatus::Unsupported}}) {
        Arena arena;
        const Command command = parse_command(arena, malformed.first, "x");
        t.check(command.status == malformed.second && !command.detail.empty(),
                "a malformed Taylor request is refused before any work: " + std::string(malformed.first) + ": " +
                command.detail);
    }
    for (const char *text : {"maclaurin(exp(x),x,3)", "taylor(1/x,x,1,3)"}) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            Arena arena;
            Derivation derivation;
            Budget budget;
            if (failure == 0) budget.max_steps = 2;
            if (failure == 1) budget.max_rewrites = 4;
            if (failure == 2) budget.poll = [](void *) { return true; };
            const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "x"), budget);
            t.check(result.value == kNoNode && result.remainder == kNoNode &&
                    result.outcome == (failure == 2 ? CalculusOutcome::Cancelled : CalculusOutcome::ResourceExceeded) &&
                    result.status == (failure == 2 ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached),
                    "the Taylor family stops for cancellation and for its budgets: " + std::string(text) +
                    " failure " + std::to_string(failure) + ": " + result.detail);
        }
    }
    {
        // Giac is never asked, because the family's final check is native.
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "maclaurin(exp(x),x,2)", "x"), Budget(), &backend);
        t.check(result.outcome == CalculusOutcome::Evaluated && backend.commands.empty() && !result.backend_attempted,
                "the Taylor family does not send its question to the backend");
    }
    // CALC-011 convergence. Each case names the test that has to decide it, so a series decided by
    // the wrong test fails here even when the verdict happens to be right.
    struct SeriesCase {
        const char *golden;
        const char *text;
        SeriesVerdict verdict;
        const char *test;
        bool has_sum;
        Rational sum;
    };
    for (const SeriesCase &fixture : {
             SeriesCase{"series_geometric_sum", "convergence((1/2)^n,n,0)", SeriesVerdict::ConvergesAbsolutely, "series.ratio-test", true, {2, 1}},
             SeriesCase{"series_ratio_test", "convergence(n/2^n,n,1)", SeriesVerdict::ConvergesAbsolutely, "series.ratio-test", false, {}},
             SeriesCase{"series_ratio_diverges", "convergence(3^n/n^2,n,1)", SeriesVerdict::Diverges, "series.ratio-test", false, {}},
             SeriesCase{"series_p_comparison", "convergence(1/n^2,n,1)", SeriesVerdict::ConvergesAbsolutely, "series.p-comparison", false, {}},
             SeriesCase{"series_harmonic_diverges", "convergence(1/n,n,1)", SeriesVerdict::Diverges, "series.p-comparison", false, {}},
             SeriesCase{"series_alternating_conditional", "convergence((-1)^n/n,n,1)", SeriesVerdict::ConvergesConditionally, "series.alternating-test", false, {}},
             SeriesCase{"series_divergence_test", "convergence(n/(n+1),n,1)", SeriesVerdict::Diverges, "series.divergence-test", false, {}},
             SeriesCase{"series_zero_terms", "convergence(0,n,1)", SeriesVerdict::ConvergesAbsolutely, "series.zero-terms", true, {0, 1}},
             SeriesCase{"", "convergence((-1)^n/n^2,n,1)", SeriesVerdict::ConvergesAbsolutely, "series.p-comparison", false, {}},
             SeriesCase{"", "convergence(3*(-2/3)^(n+1),n,1)", SeriesVerdict::ConvergesAbsolutely, "series.ratio-test", true, {4, 5}},
             SeriesCase{"", "convergence(1/(n^2-4),n,3)", SeriesVerdict::ConvergesAbsolutely, "series.p-comparison", false, {}},
             SeriesCase{"", "convergence(-(n^2+1)/(2*n^2),n,1)", SeriesVerdict::Diverges, "series.divergence-test", false, {}},
             SeriesCase{"", "convergence((-1)^n*n^3,n,1)", SeriesVerdict::Diverges, "series.divergence-test", false, {}}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = fixture.text;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, fixture.text, "n"));
        const std::string sum = result.value == kNoNode ? std::string() : print(arena, result.value);
        Rational observed;
        const bool sum_matches = !fixture.has_sum
            ? result.value == kNoNode
            : result.value != kNoNode && evaluate_rational(arena, result.value, {}, &observed) &&
                  observed.num == fixture.sum.num && observed.den == fixture.sum.den;
        t.check(result.outcome == CalculusOutcome::Evaluated && result.status == DerivationStatus::SolvedAndVerified &&
                result.verdict == fixture.verdict && result.test == fixture.test && sum_matches,
                "the convergence family decides " + std::string(fixture.text) + " by " + fixture.test + ": " +
                series_verdict_name(result.verdict) + " by " + result.test + " sum " + sum + ": " + result.detail);
        const std::string rendered = render_derivation(arena, derivation);
        t.check(rendered.find("series.terms-defined") != std::string::npos &&
                rendered.find("series.check-form") != std::string::npos &&
                rendered.find("series.terms-defined") < rendered.find(fixture.test),
                "the convergence family checks the terms exist before its test and records its final check: " +
                std::string(fixture.text));
        if (*fixture.golden)
            check_golden(t, fixture.golden, "problem: " + std::string(fixture.text) + "\nverdict: " +
                         series_verdict_name(result.verdict) + "\ntest: " + result.test + "\nsum: " +
                         (sum.empty() ? std::string("none") : sum) + "\n" + rendered);
    }
    struct SeriesRefusal {
        const char *text;
        CalculusOutcome outcome;
        DerivationStatus status;
    };
    for (const SeriesRefusal &refusal : {
             SeriesRefusal{"convergence(1/ln(n),n,2)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             SeriesRefusal{"convergence(sin(n)/n^2,n,1)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             SeriesRefusal{"convergence(1/n^n,n,1)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             SeriesRefusal{"convergence(0^n,n,1)", CalculusOutcome::UnsupportedForm, DerivationStatus::Unsupported},
             SeriesRefusal{"convergence(1/(n-3),n,1)", CalculusOutcome::InvalidInput, DerivationStatus::InvalidInput},
             SeriesRefusal{"convergence(1/(n^2-100),n,1)", CalculusOutcome::InvalidInput, DerivationStatus::InvalidInput},
             SeriesRefusal{"convergence((n-2)/(n-2),n,0)", CalculusOutcome::InvalidInput, DerivationStatus::InvalidInput},
             SeriesRefusal{"convergence(1/n,n,0)", CalculusOutcome::InvalidInput, DerivationStatus::InvalidInput},
             SeriesRefusal{"convergence(1/(n-5000),n,1)", CalculusOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached}}) {
        Arena arena;
        Derivation derivation;
        derivation.request.original_expression = refusal.text;
        const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, refusal.text, "n"));
        t.check(result.value == kNoNode && result.verdict == SeriesVerdict::None && !result.detail.empty() &&
                result.outcome == refusal.outcome && result.status == refusal.status,
                "the convergence family refuses for the right reason: " + std::string(refusal.text) + ": " +
                calculus_outcome_name(result.outcome) + ": " + result.detail);
    }
    {
        // The first index decides which terms exist, so the same term is fine from a later start.
        Arena arena;
        Derivation derivation;
        const CalculusResult later = calculus_walkthrough(arena, derivation, parse_command(arena, "convergence(1/(n-3),n,4)", "n"));
        t.check(later.verdict == SeriesVerdict::Diverges && later.status == DerivationStatus::SolvedAndVerified,
                "a term undefined before the first index does not stop the series: " + later.detail);
    }
    for (const auto &malformed : {std::pair{"convergence(1/n,n,1/2)", CommandStatus::Invalid},
                                  std::pair{"convergence(1/n,n)", CommandStatus::Unsupported},
                                  std::pair{"convergence(1/n,n,1,2)", CommandStatus::Unsupported}}) {
        Arena arena;
        const Command command = parse_command(arena, malformed.first, "n");
        t.check(command.status == malformed.second && !command.detail.empty(),
                "a malformed convergence request is refused before any work: " + std::string(malformed.first) + ": " +
                command.detail);
    }
    for (const char *text : {"convergence((1/2)^n,n,0)", "convergence((-1)^n/n,n,1)"}) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            Arena arena;
            Derivation derivation;
            Budget budget;
            if (failure == 0) budget.max_steps = 2;
            if (failure == 1) budget.max_rewrites = 4;
            if (failure == 2) budget.poll = [](void *) { return true; };
            const CalculusResult result = calculus_walkthrough(arena, derivation, parse_command(arena, text, "n"), budget);
            t.check(result.value == kNoNode && result.verdict == SeriesVerdict::None &&
                    result.outcome == (failure == 2 ? CalculusOutcome::Cancelled : CalculusOutcome::ResourceExceeded) &&
                    result.status == (failure == 2 ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached),
                    "the convergence family stops for cancellation and for its budgets: " + std::string(text) +
                    " failure " + std::to_string(failure) + ": " + result.detail);
        }
    }
    {
        // CALC-011 asks for both halves, so its evidence joins a verified polynomial with every test.
        Arena arena;
        Derivation polynomial_record;
        const CalculusResult polynomial = calculus_walkthrough(arena, polynomial_record,
            parse_command(arena, "maclaurin(exp(x),x,3)", "x"));
        Rational at_one;
        bool decided = polynomial.status == DerivationStatus::SolvedAndVerified && polynomial.remainder != kNoNode &&
                       evaluate_rational(arena, polynomial.value, {{"x", Rational{1, 1}}}, &at_one) &&
                       at_one.num == 8 && at_one.den == 3;
        for (const auto &series : {std::pair{"convergence(n/2^n,n,1)", "series.ratio-test"},
                                   std::pair{"convergence(n/(n+1),n,1)", "series.divergence-test"},
                                   std::pair{"convergence(1/n^2,n,1)", "series.p-comparison"},
                                   std::pair{"convergence((-1)^n/n,n,1)", "series.alternating-test"}}) {
            Derivation record;
            const CalculusResult result = calculus_walkthrough(arena, record, parse_command(arena, series.first, "n"));
            decided = decided && result.status == DerivationStatus::SolvedAndVerified && result.test == series.second;
        }
        t.evidence("CALC-011", decided,
                   "a verified Taylor polynomial with its remainder, and the ratio, divergence, "
                   "p-comparison and alternating tests each deciding a series after checking its hypotheses");
    }
    {
        Arena arena;
        Derivation derivation;
        CalculusBackend backend;
        const CalculusResult result = calculus_walkthrough(arena, derivation,
            parse_command(arena, "convergence(1/n^2,n,1)", "n"), Budget(), &backend);
        t.check(result.verdict == SeriesVerdict::ConvergesAbsolutely && backend.commands.empty() && !result.backend_attempted,
                "the convergence family does not send its question to the backend");
    }
}



}
