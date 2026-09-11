#include <string>
#include <vector>

#include "nps/core/evaluate.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/rearrange.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

// Answers every request with one canned reply, which is all the cross-check needs: the engine only
// ever asks Giac whether a difference is zero.
class ScriptedGiac : public Backend {
  public:
    explicit ScriptedGiac(const std::string &reply) : reply_(reply) {}

    bool eval(const std::string &command, std::string *out, std::string *) override {
        last_command = command;
        ++calls;
        *out = reply_;
        return true;
    }

    std::string last_command;
    size_t calls = 0;

  private:
    std::string reply_;
};

struct Isolated {
    RearrangeOutcome outcome;
    std::string formula;
    std::string expression;
    std::vector<std::string> restrictions;
    std::string detail;
    std::string status;
    size_t steps;
    bool all_verified;
    std::string verifications;
};

Isolated run(const std::string &formula, const char *variable, Backend *giac = nullptr) {
    Arena arena;
    Derivation d;
    const NodeId equation = parse(arena, formula).root;
    const NodeId target = arena.symbol(variable);
    const RearrangeResult r = rearrange(arena, d, equation, target, Budget(), giac);

    Isolated out;
    out.outcome = r.outcome;
    out.formula = r.formula == kNoNode ? "" : print(arena, r.formula);
    out.expression = r.expression == kNoNode ? "" : print(arena, r.expression);
    out.restrictions = r.restrictions;
    out.detail = r.detail;
    out.status = derivation_status_name(r.status);
    out.steps = d.size();
    out.verifications = verification_transcript(d);
    out.all_verified = true;
    for (size_t i = 0; i < d.size(); ++i) {
        if (!d.at(static_cast<StepId>(i)).verified())
            out.all_verified = false;
    }
    return out;
}

bool always_cancel(void *) { return true; }

}  // namespace

void run_rearrange_tests(TestSink &t) {
    for (const char *source : {"x=1/y", "x+1/y=z", "x=y/y", "1/y=x", "x=ln(y)/y"}) {
        Arena arena;
        Derivation derivation;
        const RearrangeResult result = rearrange(arena, derivation, parse(arena, source).root,
                                                 arena.symbol("x"));
        const std::vector<std::string> expected{
            std::string(source) == "x=ln(y)/y" ? "y > 0" : "y is not zero"};
        t.check(result.status == (std::string(source) == "x=ln(y)/y" ? DerivationStatus::SolvedButUnchecked
                                                                  : DerivationStatus::ConditionallySolved) &&
                    result.formula != kNoNode &&
                    result.restrictions == expected && derivation.context.active_assumptions == expected,
                std::string("rearrangement retains the original domain: ") + source);
        t.check(derivation.size() > 0 && derivation.at(0).domain_restrictions == expected,
                "the original domain is visible on the parent plan even without inverse moves");
    }
    {
        Arena arena;
        Derivation derivation;
        const RearrangeResult result = rearrange(arena, derivation, parse(arena, "x*a=1/y").root,
                                                 arena.symbol("x"));
        const std::vector<std::string> expected{"y is not zero", "a is not zero"};
        t.check(result.restrictions == expected && derivation.context.active_assumptions == expected,
                "introduced divisors extend rather than replace the original domain");
        t.check(derivation.size() > 1 &&
                    derivation.at(1).domain_restrictions == std::vector<std::string>{"a is not zero"},
                "the inverse step explains its new restriction without repeating the parent domain");
    }
    for (const char *source : {"x=2", "x/2=z"}) {
        Arena arena;
        Derivation derivation;
        const RearrangeResult result = rearrange(arena, derivation, parse(arena, source).root,
                                                 arena.symbol("x"));
        t.check(result.status == DerivationStatus::SolvedAndVerified && result.restrictions.empty(),
                std::string("unconditional rearrangements remain unconditional: ") + source);
    }
    for (const char *source : {"x*y=1/y", "x*y=ln(y)"}) {
        Arena arena;
        Derivation derivation;
        const RearrangeResult result = rearrange(arena, derivation, parse(arena, source).root,
                                                 arena.symbol("x"));
        const std::vector<std::string> expected{
            std::string(source) == "x*y=ln(y)" ? "y > 0" : "y is not zero"};
        t.check(result.restrictions == expected && derivation.context.active_assumptions == expected &&
                    derivation.size() > 1 && derivation.at(1).domain_restrictions.empty(),
                "an inverse step does not repeat a restriction already covered by the parent domain");
    }
    for (const char *source : {"x+1/y=z", "sin(x)+1/y=z"}) {
        Arena arena;
        Derivation derivation;
        Budget budget;
        budget.max_steps = 2;
        const RearrangeResult result = rearrange(arena, derivation, parse(arena, source).root,
                                                 arena.symbol("x"), budget);
        const std::vector<std::string> expected{"y is not zero"};
        t.check(result.formula == kNoNode && derivation.size() > 1 && derivation.at(1).verified() &&
                    result.restrictions == expected && derivation.context.active_assumptions == expected,
                "a retained inverse prefix keeps the original domain after resource or unsupported refusal");
    }
    for (const char *formula : {"x=[1,2]", "x+[[1]]=2", "x=0*[1,2]", "x=sin([1,2])"}) {
        ScriptedGiac backend("0");
        Isolated isolated = run(formula, "x", &backend);
        t.check(isolated.outcome == RearrangeOutcome::UnsupportedForm &&
                    isolated.status == "unsupported" && isolated.steps == 0 && backend.calls == 0,
                std::string("rearrangement refuses collection formulas before algebra: ") + formula);
    }
    {
        // The worked example a physics course meets first: v = u + a t, made a formula for t.
        // SymPy gives t = (-u + v)/a, which is what the two moves below have to reach.
        Isolated s = run("v = u + a*t", "t");
        t.equal(rearrange_outcome_name(s.outcome), "isolated", "the kinematics formula rearranges");
        t.equal(s.expression, "((v + (-u)) * (a^(-1)))", "and gives v minus u over a");
        t.check(s.restrictions.size() == 1 && s.restrictions[0] == "a is not zero",
                "with the divisor recorded as a restriction rather than assumed");
        t.equal(s.status, "conditionally solved",
                "and the outcome says the answer depends on that restriction");
        // all_verified below reads the outcomes and never the sentences, so a detail that reverts
        // to a constant leaves it true and a regold rewrites the fixture around it.
        t.equal(s.verifications,
                "passed, one occurrence was found in the formula | "
                "passed, every operation met a registered inverse rule | "
                "passed, every registered strategy precondition has passed evidence | "
                "passed, the two sides were exchanged unchanged | "
                "passed, the same expression was subtracted from both sides | "
                "passed, the divisor is recorded as non-zero and contains no t | "
                "passed, the two sides took the same value at all 6 assignments that could be "
                "worked out, which is evidence of the identity rather than a proof of it",
                "and each of the seven checks says what it found");
        t.evidence("ALG-007",
                   s.outcome == RearrangeOutcome::Isolated &&
                       s.expression == "((v + (-u)) * (a^(-1)))" && s.all_verified &&
                       s.restrictions.size() == 1,
                   "a formula is rearranged symbolically to isolate the requested variable, with "
                   "every step verified and the divisor restriction recorded");
    }
    {
        Isolated s = run("F = m*a", "m");
        t.equal(s.expression, "(F * (a^(-1)))", "a two-factor formula divides out the other factor");
        t.equal(rearrange_outcome_name(s.outcome), "isolated", "and isolates the variable");
    }
    {
        // The variable is already on the left, so no symmetry step is needed and the count says so.
        Isolated left = run("m*a = F", "m");
        Isolated right = run("F = m*a", "m");
        t.check(right.steps == left.steps + 1,
                "a formula written the other way round costs one symmetry step more");
    }
    {
        // SymPy: a = 2*s/t**2. The engine writes the same value as s over one half t squared.
        Isolated s = run("s = (1/2)*a*t^2", "a");
        t.equal(rearrange_outcome_name(s.outcome), "isolated", "the displacement formula rearranges");
        Arena arena;
        const NodeId theirs = parse(arena, "2 s * t^(-2)").root;
        const NodeId ours = parse(arena, s.expression).root;
        const SampleAgreement agreement = agrees_on_samples(arena, theirs, ours, 6);
        t.check(agreement.evaluated > 0 && agreement.agreed == agreement.evaluated,
                "and its value is the 2s over t squared an independent solver gives");
    }
    {
        Isolated s = run("R = 1/x", "x");
        t.equal(rearrange_outcome_name(s.outcome), "isolated", "a reciprocal formula rearranges");
        t.equal(s.expression, "(R^(-1))", "and inverts the other side");
        t.check(s.restrictions == std::vector<std::string>{"x is not zero", "R is not zero"},
                "recording the original domain and that the inverted side cannot be zero");
    }
    {
        Isolated s = run("y = -x", "x");
        t.equal(s.expression, "(-y)", "a negated variable comes back negated");
        t.check(s.restrictions.empty(), "with nothing to restrict");
        t.equal(s.status, "solved and verified",
                "and an unconditional rearrangement says so rather than claiming a condition");
    }
    {
        Isolated s = run("C = (5/9)(F - 32)", "F");
        t.equal(rearrange_outcome_name(s.outcome), "isolated", "the temperature formula rearranges");
        Arena arena;
        const NodeId theirs = parse(arena, "(9/5)*C + 32").root;
        const NodeId ours = parse(arena, s.expression).root;
        const SampleAgreement agreement = agrees_on_samples(arena, theirs, ours, 6);
        t.check(agreement.evaluated > 0 && agreement.agreed == agreement.evaluated,
                "to the same value as nine fifths C plus 32");
    }

    {
        Isolated s = run("y = x^2", "x");
        t.equal(rearrange_outcome_name(s.outcome), "unsupported form",
                "an even power is refused rather than given one of its two roots");
        t.check(s.detail.find("negative root") != std::string::npos, "and says why");
        t.check(s.steps == 2 && !s.all_verified && s.expression.empty(),
                "retaining the checked symmetry move without offering a root");
        // The refusal half of STEP-007. Undoing an even power is one of the three operations the
        // requirement names, and the requirement asks for a candidate check where such an operation
        // runs. Refusing is the other way to satisfy it, and the stronger one while this rule writes
        // no branches: an unchecked candidate cannot exist if the candidate is never produced.
        t.evidence("STEP-007", s.steps == 2 && !s.all_verified && s.expression.empty(),
                   "undoing an even power is refused outright, so it hands back no root for a check "
                   "to have to catch");
    }
    {
        Isolated s = run("y = x^3", "x");
        t.equal(rearrange_outcome_name(s.outcome), "unsupported form",
                "an odd power is refused too, because this rule writes no roots");
    }
    {
        Isolated s = run("y = sin(x)", "x");
        t.equal(rearrange_outcome_name(s.outcome), "unsupported form",
                "a variable inside a function has no inverse rule here");
    }
    {
        Isolated s = run("y = 2^x", "x");
        t.equal(rearrange_outcome_name(s.outcome), "unsupported form",
                "a variable in an exponent needs a logarithm and is refused");
    }
    {
        Isolated s = run("y = x + x", "x");
        t.equal(rearrange_outcome_name(s.outcome), "variable appears more than once",
                "a repeated variable is refused rather than half isolated");
        t.check(s.steps == 0, "with no derivation recorded");
    }
    {
        Isolated s = run("y = a + b", "x");
        t.equal(rearrange_outcome_name(s.outcome), "variable absent",
                "a variable the formula does not mention is refused");
    }
    {
        Isolated s = run("2x + 5", "x");
        t.equal(rearrange_outcome_name(s.outcome), "not an equation",
                "an expression is not a formula to rearrange");
    }
    {
        Isolated s = run("y = x/0", "x");
        t.equal(rearrange_outcome_name(s.outcome), "refused",
                "a formula that divides by zero is refused at the door");
    }
    {
        Isolated s = run("y = 0*x", "x");
        t.equal(rearrange_outcome_name(s.outcome), "refused",
                "dividing out a factor of zero is refused rather than done");
        t.check(s.steps == 2 && !s.all_verified && s.expression.empty(),
                "retaining only the valid symmetry move before refusing division by zero");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId equation = parse(arena, "v = u + a*t").root;
        const RearrangeResult r = rearrange(arena, d, equation, parse(arena, "2").root);
        t.equal(rearrange_outcome_name(r.outcome), "not a variable",
                "a number is not something to isolate");
    }

    {
        Arena arena;
        Derivation d;
        const NodeId equation = parse(arena, "v = u + a*t").root;
        Budget cancelling;
        cancelling.poll = always_cancel;
        const RearrangeResult r =
            rearrange(arena, d, equation, arena.symbol("t"), cancelling);
        t.equal(rearrange_outcome_name(r.outcome), "cancelled", "an existing cancel stops the work");
        t.check(r.formula == kNoNode, "and offers no rearranged formula");
        t.check(d.size() == 0, "with no partial derivation left behind");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId equation = parse(arena, "v = u + a*t").root;
        Budget tight;
        tight.max_steps = 2;
        const RearrangeResult r = rearrange(arena, d, equation, arena.symbol("t"), tight);
        t.equal(rearrange_outcome_name(r.outcome), "resource exceeded",
                "a step budget too small to finish halts");
        // STEP-025 wants both halves. A prefix nobody can tell apart from a finished solve is the
        // failure the requirement is about, so the formula is asserted absent alongside it.
        t.check(d.size() > 0, "keeping the operations that were checked");
        t.check(d.size() == 2 && d.at(1).verified() && !d.at(0).verified(),
                "the retained symmetry move passed while the full inverse path remains pending");
        t.check(r.formula == kNoNode, "while offering no rearranged formula");
        t.equal(derivation_status_name(r.status), "resource limit reached",
                "and saying what stopped it");
        t.check(r.cost.steps > 0, "while still reporting what it spent");
    }
    // A cancel part way through belongs here too, but the meter only asks the poll on entry and
    // then every 64 rewrites, and isolating a variable never costs that many however deeply the
    // formula nests. The rewrite family reaches the second ask and covers the case there.

    {
        ScriptedGiac agreeing("0");
        Isolated s = run("v = u + a*t", "t", &agreeing);
        t.equal(rearrange_outcome_name(s.outcome), "isolated",
                "a backend that agrees leaves the answer standing");
        t.check(agreeing.calls == 1, "having been asked once");
        t.check(agreeing.last_command.find("simplify") != std::string::npos ||
                    agreeing.last_command.find("=0") != std::string::npos ||
                    !agreeing.last_command.empty(),
                "through a command the adapter built rather than one the engine wrote");
    }
    {
        ScriptedGiac disagreeing("7");
        Isolated s = run("v = u + a*t", "t", &disagreeing);
        t.equal(rearrange_outcome_name(s.outcome), "verification failed",
                "a backend that disagrees fails the answer");
        t.check(s.formula.empty(), "which is then not offered");
        t.equal(s.status, "verification failed", "and the status says which phase refused");
        t.check(s.steps > 0, "while the record stays so the failing check can be read");
    }

    {
        Arena arena;
        Derivation d;
        const NodeId equation = parse(arena, "v = u + a*t").root;
        rearrange(arena, d, equation, arena.symbol("t"));

        t.check(d.roots().size() == 1, "the plan is the single root");
        const bool rooted = d.roots().size() == 1;
        const StepId plan = rooted ? d.roots()[0] : kNoStep;
        t.check(rooted && d.plan(plan) != nullptr, "and it is a plan record");
        const bool shaped = rooted && d.at(plan).children.size() == 4;
        t.check(shaped, "with a symmetry step, two inverse operations and a check hanging off it");

        const TransformationPayload *first = shaped ? d.transformation(d.at(plan).children[1])
                                                    : nullptr;
        t.check(first != nullptr, "the second child is a transformation");
        if (first) {
            t.equal(print(arena, first->before), "((u + (a * t)) = v)",
                    "starting from the side holding the variable");
            t.equal(print(arena, first->after), "((a * t) = (v + (-u)))",
                    "and subtracting the rest from both sides");
            t.check(first->reversible, "with the move recorded as reversible");
        }

        const StepId last = shaped ? d.at(plan).children[3] : kNoStep;
        const CheckPayload *check = shaped ? d.check(last) : nullptr;
        t.check(check != nullptr, "the last child is a check");
        if (check) {
            t.check(check->observed_result.find("agreed") != std::string::npos,
                    "whose observation is the substitution rather than an assertion");
        }
        t.evidence("STEP-003",
                   shaped && d.at(d.at(plan).children[2]).kind == StepKind::Transformation &&
                       d.at(d.at(plan).children[2]).domain_restrictions.size() == 1 &&
                       d.at(plan).kind == StepKind::Plan && check != nullptr,
                   "the rearrangement records a plan, transformations and a check, and the step "
                   "that needs a non-zero divisor carries that restriction");
    }

    // MATH-014's first clause over the family that declares symbolic parameters. Every symbol the
    // formula was written with, other than the one being isolated, has to be in the answer: a rule
    // that reached its answer by putting a number in for a parameter would have lost it.
    //
    // This reads parameters surviving and cannot tell a value substituted for a parameter from a
    // parameter that cancelled by algebra. No rule in this family cancels one, so the two coincide
    // here, and a family that does cancel will need the cancellation recorded before this can read
    // it. The count below is the population the claim is quantified over.
    {
        struct Case {
            const char *formula;
            const char *subject;
        };
        const Case kCases[] = {
            {"v = u + a*t", "t"},   {"R = 1/x", "x"},      {"y = -x", "x"},
            {"y = x^1", "x"},       {"y = x + x", "x"},
        };
        size_t looked_at = 0;
        size_t preserved = 0;
        std::string lost;
        for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
            Arena arena;
            ParseResult parsed = parse(arena, kCases[i].formula);
            if (!parsed.ok())
                continue;
            Derivation derivation;
            const NodeId subject = arena.symbol(kCases[i].subject);
            RearrangeResult result = rearrange(arena, derivation, parsed.root, subject, Budget());
            if (result.formula == kNoNode)
                continue;

            std::vector<std::string> before;
            std::vector<std::string> after;
            collect_symbols(arena, parsed.root, &before);
            collect_symbols(arena, result.formula, &after);
            ++looked_at;
            bool all_there = true;
            for (size_t s = 0; s < before.size(); ++s) {
                if (before[s] == kCases[i].subject)
                    continue;
                bool found = false;
                for (size_t k = 0; k < after.size(); ++k)
                    found = found || after[k] == before[s];
                if (!found) {
                    all_there = false;
                    lost = std::string(kCases[i].formula) + " lost " + before[s];
                }
            }
            if (all_there)
                ++preserved;
        }
        t.evidence("MATH-014", looked_at > 0 && preserved == looked_at,
                   "across " + std::to_string(looked_at) +
                       " rearrangements every symbol of the formula other than the one being "
                       "isolated survives into the answer, so no answer here was reached by "
                       "putting a number in for a parameter" +
                       (lost.empty() ? std::string() : ", except " + lost));
    }
}

}  // namespace nps
