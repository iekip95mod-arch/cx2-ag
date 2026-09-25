#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "nps/core/evaluate.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/rewrite.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

class ScriptedGiac : public Backend {
  public:
    explicit ScriptedGiac(const std::string &reply) : reply_(reply) {}

    bool eval(const std::string &, std::string *out, std::string *) override {
        ++calls;
        *out = reply_;
        return true;
    }

    size_t calls = 0;

  private:
    std::string reply_;
};

struct Rewritten {
    RewriteOutcome outcome;
    std::string expression;
    std::string detail;
    std::string status;
    size_t steps;
    bool all_verified;
    std::string verifications;
    Cost cost;
};

Rewritten run(const std::string &source, RewriteGoal goal, Backend *giac = nullptr,
              const Budget &budget = Budget()) {
    Arena arena;
    Derivation d;
    const NodeId expression = parse(arena, source).root;
    const RewriteResult r = rewrite(arena, d, expression, goal, budget, giac);

    Rewritten out;
    out.outcome = r.outcome;
    out.expression = r.expression == kNoNode ? "" : print(arena, r.expression);
    out.detail = r.detail;
    out.status = derivation_status_name(r.status);
    out.steps = d.size();
    out.cost = r.cost;
    out.verifications = verification_transcript(d);
    out.all_verified = true;
    for (size_t i = 0; i < d.size(); ++i) {
        if (!d.at(static_cast<StepId>(i)).verified())
            out.all_verified = false;
    }
    return out;
}

// Whether two written forms are the same expression, worked out rather than compared as text. The
// engine's own printer spells a product one way and a reader another, and a test that pinned the
// spelling would fail on a change that means nothing.
bool same_value(const std::string &left, const std::string &right) {
    Arena arena;
    const NodeId a = parse(arena, left).root;
    const NodeId b = parse(arena, right).root;
    if (a == kNoNode || b == kNoNode)
        return false;
    const SampleAgreement agreement = agrees_on_samples(arena, a, b, 6);
    return agreement.evaluated > 0 && agreement.agreed == agreement.evaluated;
}

bool bounded_evaluation() {
    for (Kind kind : {Kind::Neg, Kind::Add, Kind::Mul, Kind::Pow, Kind::Call, Kind::List}) {
        Limits limits;
        limits.max_depth = 4096;
        Arena arena(limits);
        const NodeId x = arena.symbol("x");
        const NodeId zero = arena.integer("0");
        const NodeId one = arena.integer("1");
        NodeId nested = x;
        for (size_t depth = 0; depth < 2048; ++depth) {
            if (kind == Kind::Neg)
                nested = arena.unary(kind, nested);
            else if (kind == Kind::Call)
                nested = arena.call("f", {nested});
            else if (kind == Kind::List)
                nested = arena.list({nested});
            else
                nested = arena.binary(kind, nested, kind == Kind::Add ? zero : one);
        }
        if (arena.failed())
            return false;
        Rational value;
        const bool evaluated = evaluate_rational(arena, nested, {{"x", {3, 2}}}, &value);
        if (kind == Kind::Call || kind == Kind::List) {
            if (evaluated)
                return false;
        } else if (!evaluated || !rational_equal(value, {3, 2})) {
            return false;
        }
        std::vector<std::string> names{"z", "z"};
        collect_symbols(arena, nested, &names);
        if (names != std::vector<std::string>{"x", "z"})
            return false;
    }
    Arena malformed;
    const NodeId two = malformed.integer("2");
    for (Kind kind : {Kind::Neg, Kind::Pow}) {
        for (size_t arity = 0; arity <= 3; ++arity) {
            if (arity == (kind == Kind::Neg ? 1 : 2))
                continue;
            const NodeId expression = malformed.nary(kind, std::vector<NodeId>(arity, two));
            Rational value{7, 3};
            if (evaluate_rational(malformed, expression, {}, &value) ||
                value.num != 7 || value.den != 3)
                return false;
        }
    }
    Arena shared;
    NodeId sum = shared.symbol("x");
    for (size_t depth = 0; depth < 40; ++depth)
        sum = shared.binary(Kind::Add, sum, sum);
    Rational value;
    if (shared.failed() || !evaluate_rational(shared, sum, {{"x", {1, 1}}}, &value) ||
        !rational_equal(value, {int64_t{1} << 40, 1}))
        return false;
    std::vector<std::string> names;
    collect_symbols(shared, sum, &names);
    if (names != std::vector<std::string>{"x"})
        return false;
    return evaluate_rational(shared, sum, {{"x", {0, 1}}}, &value) && value.num == 0;
}

bool always_cancel(void *) { return true; }

// Budget::poll carries no state of its own, so the countdown lives beside it. Set it before the run
// that uses it.
size_t cancel_countdown = 0;

bool cancel_after_working(void *) {
    if (cancel_countdown == 0)
        return true;
    --cancel_countdown;
    return false;
}

// How many transformations of one rule the derivation holds, which is how a test says the working
// was shown rather than only that the answer arrived.
size_t rule_count(const Derivation &d, const char *rule_id) {
    size_t found = 0;
    for (size_t i = 0; i < d.size(); ++i) {
        if (d.at(static_cast<StepId>(i)).rule_id == rule_id)
            ++found;
    }
    return found;
}

}  // namespace

void run_rewrite_tests(TestSink &t) {
    for (const auto &fixture : {std::pair{"sqrt(0)", Rational{0, 1}},
                               std::pair{"sqrt(81/100)", Rational{9, 10}},
                               std::pair{"sqrt(9223372030926249001)", Rational{3037000499, 1}},
                               std::pair{"sqrt(sqrt(81))", Rational{3, 1}},
                               std::pair{"sqrt(x^2)", Rational{3, 2}},
                               std::pair{"exp(sqrt(0))+cos(0)+sin(0)+ln(1)", Rational{2, 1}}}) {
        Arena arena;
        Rational value{7, 3};
        t.check(evaluate_rational(arena, parse(arena, fixture.first).root, {{"x", {-3, 2}}}, &value) &&
                rational_equal(value, fixture.second),
                "shared evaluation simplifies exact elementary constants: " + std::string(fixture.first));
    }
    for (const char *expression : {"sqrt(2)", "sqrt(-1)", "sqrt(1/2)", "ln(0)", "ln(-1)",
                                  "sin(1)", "cos(1)", "exp(1)", "f(0)", "sqrt()", "sqrt(1,4)"}) {
        Arena arena;
        Rational value{7, 3};
        t.check(!evaluate_rational(arena, parse(arena, expression).root, {}, &value) &&
                value.num == 7 && value.den == 3,
                "shared evaluation withholds nonrational or invalid elementary values: " + std::string(expression));
    }
    {
        // The limit reading holds only while everything reduced here is continuous where it returns.
        const std::string warning =
            ", so the limit reading in nps/tests/step_invariants.h must be re-justified";
        struct HeadCase { const char *name; Rational argument; bool reduces; };
        const HeadCase heads[] = {
            {"sqrt", {0, 1}, true},  {"sqrt", {4, 1}, true},   {"sqrt", {9, 4}, true},
            {"sqrt", {2, 1}, false}, {"sqrt", {-1, 1}, false}, {"sin", {0, 1}, true},
            {"sin", {1, 1}, false},  {"cos", {0, 1}, true},    {"cos", {1, 1}, false},
            {"exp", {0, 1}, true},   {"exp", {1, 1}, false},   {"ln", {1, 1}, true},
            {"ln", {0, 1}, false},
        };
        std::string moved;
        for (const HeadCase &head : heads) {
            Rational value{7, 3};
            if (evaluate_rational_function(head.name, head.argument, &value) != head.reduces)
                moved += (moved.empty() ? "" : " | ") + std::string(head.name) + " at " +
                         rational_text(head.argument);
        }
        for (const char *name : {"abs", "sign", "sgn", "floor", "ceil", "round", "trunc", "frac",
                                 "mod", "piecewise", "min", "max", "step", "tan", "log"}) {
            for (const Rational &argument : {Rational{0, 1}, Rational{1, 1}, Rational{-1, 1},
                                             Rational{1, 2}, Rational{3, 2}, Rational{-3, 2}}) {
                Rational value{7, 3};
                if (evaluate_rational_function(name, argument, &value))
                    moved += (moved.empty() ? "" : " | ") + std::string(name) + " at " +
                             rational_text(argument);
            }
        }
        t.equal(moved, "",
                "evaluate_rational reduces the same call heads at the same arguments as before" +
                    warning);

        struct KindCase { Kind kind; const char *name; bool reduces; };
        const KindCase kinds[] = {
            {Kind::Integer, "Integer", true},  {Kind::Decimal, "Decimal", true},
            {Kind::Symbol, "Symbol", true},    {Kind::Add, "Add", true},
            {Kind::Mul, "Mul", true},          {Kind::Pow, "Pow", true},
            {Kind::Neg, "Neg", true},          {Kind::Call, "Call", true},
            {Kind::Equals, "Equals", false},   {Kind::Assign, "Assign", false},
            {Kind::Approx, "Approx", false},   {Kind::Identity, "Identity", false},
            {Kind::Less, "Less", false},       {Kind::LessEqual, "LessEqual", false},
            {Kind::Greater, "Greater", false}, {Kind::GreaterEqual, "GreaterEqual", false},
            {Kind::Invalid, "Invalid", false}, {Kind::List, "List", false},
        };
        std::string changed;
        // Every byte, so a kind appended after List and then reduced here is caught as well.
        for (size_t raw = 0; raw < 256; ++raw) {
            const Kind kind = static_cast<Kind>(raw);
            bool expected = false;
            std::string name = "the undeclared kind " + integer_text(static_cast<int64_t>(raw));
            for (const KindCase &entry : kinds) {
                if (entry.kind != kind)
                    continue;
                expected = entry.reduces;
                name = entry.name;
            }
            Arena arena;
            const NodeId four = arena.integer("4");
            NodeId node = kNoNode;
            switch (kind) {
                case Kind::Integer: node = four; break;
                case Kind::Decimal: node = arena.decimal("2.5"); break;
                case Kind::Symbol: node = arena.symbol("x"); break;
                case Kind::Neg: node = arena.unary(Kind::Neg, four); break;
                case Kind::Call: node = arena.call("sqrt", {four}); break;
                case Kind::List: node = arena.list({four, four}); break;
                default: node = arena.binary(kind, four, four); break;
            }
            Rational value{7, 3};
            const bool reduced =
                !arena.failed() && evaluate_rational(arena, node, {{"x", {1, 2}}}, &value);
            if (reduced != expected)
                changed += (changed.empty() ? "" : " | ") + name;
        }
        t.equal(changed, "", "evaluate_rational reduces the same node kinds as before" + warning);
    }
#if defined(__unix__) || defined(__APPLE__)
    pthread_attr_t attributes;
    const int initialized = pthread_attr_init(&attributes);
    t.check(initialized == 0, "evaluation depth worker attributes initialize");
    if (initialized == 0) {
        const int sized = pthread_attr_setstacksize(&attributes, 128 * 1024);
        t.check(sized == 0, "evaluation depth worker uses a bounded native stack");
        if (sized == 0) {
            bool passed = false;
            pthread_t worker;
            const int started = pthread_create(&worker, &attributes, [](void *context) -> void * {
                *static_cast<bool *>(context) = bounded_evaluation();
                return nullptr;
            }, &passed);
            t.check(started == 0, "evaluation depth worker starts");
            if (started == 0) {
                const int joined = pthread_join(worker, nullptr);
                t.check(joined == 0 && passed,
                        "evaluation and symbol collection handle deep and shared expressions");
            }
        }
        t.check(pthread_attr_destroy(&attributes) == 0, "evaluation worker attributes release");
    }
#else
    t.check(bounded_evaluation(), "evaluation and symbol collection handle deep and shared expressions");
#endif
    {
        struct Case {
            const char *source;
            const char *wanted;
        };
        for (const Case &c : {Case{"0^-(-1)", "0"}, Case{"0^(-(-2))", "0"}, Case{"--7", "7"},
                             Case{"001+2", "3"}, Case{"-2+3", "1"}, Case{"(-2)^2", "4"}}) {
            const Rewritten solved = run(c.source, RewriteGoal::Simplify);
            t.check(solved.outcome == RewriteOutcome::Rewritten &&
                        solved.expression == c.wanted && solved.steps > 0 && solved.all_verified,
                    std::string(c.source) + " completes its arithmetic: " +
                        rewrite_outcome_name(solved.outcome) + ", " + solved.expression + ", " +
                        solved.detail);
        }
        const Rewritten literal = run("-2", RewriteGoal::Simplify);
        t.check(literal.outcome == RewriteOutcome::AlreadyInForm,
                "a written negative number receives no invented arithmetic step");
        const Rewritten negated = run("--1", RewriteGoal::Simplify);
        t.check(negated.verifications.find("Negate (-1) to get 1") != std::string::npos,
                "the negation explanation names the operand whose sign is changed");
    }
    for (RewriteGoal goal : {RewriteGoal::Simplify, RewriteGoal::Expand, RewriteGoal::Factor}) {
        for (const char *source : {"[1,2]", "[[1]]", "0*[1,2]", "[1]^0", "f([1])",
                                   "1+[2]", "[0.5]", "x*[1]+x*[1]"}) {
            ScriptedGiac backend("0");
            const Rewritten result = run(source, goal, &backend);
            t.check(result.outcome == RewriteOutcome::UnsupportedForm &&
                        result.status == "unsupported" && result.expression.empty() &&
                        result.steps == 0 && backend.calls == 0,
                    std::string(rewrite_goal_name(goal)) + " refuses scalar rewriting of " + source);
        }
    }
    {
        // A denominator that is zero, but not until the rules have run. The guard at entry sees an
        // unexpanded product and cannot know, so this used to come back as a successful rewrite of
        // 1/0 with an answer offered.
        Rewritten s = run("1/((x+1)*(x-1) - x^2 + 1)", RewriteGoal::Expand);
        t.equal(rewrite_outcome_name(s.outcome), "refused",
                "a denominator that expands to zero is refused");
        t.equal(s.status, "invalid input", "and says the expression has no value rather than an answer");
        t.check(s.expression.empty(), "with nothing offered as a result");
        t.check(s.steps == 0, "and no walkthrough left standing behind a refusal");
    }
    {
        // The same shape with a denominator that survives, so the guard above is not simply refusing
        // everything that has a bracket under a division line.
        Rewritten s = run("1/((x+1)*(x-1) + 1)", RewriteGoal::Expand);
        t.equal(rewrite_outcome_name(s.outcome), "rewritten",
                "a denominator that expands to something non-zero is still rewritten");
        t.check(same_value(s.expression, "1/(x^2)"), "and the bracket below the line is worked out");
    }
    {
        // Why the two above behave differently: the rules now reach inside a denominator at all.
        Rewritten s = run("1/(x - x + 1)", RewriteGoal::Expand);
        t.equal(rewrite_outcome_name(s.outcome), "rewritten",
                "a sum inside a denominator is no longer reported as already in form");
        t.check(same_value(s.expression, "1"), "and it is gathered to what it equals");
    }
    {
        // A call, where same_value cannot answer: it samples at rational points and the evaluator
        // does not value sin at any of them, which is the same gap that leaves a transcendental
        // answer unchecked. Compared by structure instead, in one arena, where interning makes two
        // identical forms the same node whatever the printer spells them as.
        Rewritten s = run("sin(x - x + 2)", RewriteGoal::Expand);
        Arena shared;
        t.check(parse(shared, s.expression).root == parse(shared, "sin(2)").root,
                "the descent reaches inside a function call too");
    }
    {
        // The order the descent takes, which a student reads. Offering the rule at the widest sum
        // first keeps the gathered result flat; trying children first gathers a nested piece and
        // leaves the surrounding terms looking unfinished.
        Rewritten s = run("(x + 1)*(x + 2)", RewriteGoal::Expand);
        t.check(same_value(s.expression, "x^2 + 3*x + 2"), "expanding still reaches the gathered form");
        t.check(s.expression.find("* 1)") == std::string::npos,
                "with no unfinished multiplication by one left in it");
    }
    {
        // PERF-008's rewrite budget against the descent, which multiplies how many nodes each pass
        // visits. Measured rather than assumed: the headline case above is the deepest in this file.
        Rewritten s = run("1/((x+1)*(x-1) + 1)", RewriteGoal::Expand);
        t.check(s.cost.rewrites < 128,
                "descending into a denominator costs a small fraction of the rewrite budget");
    }
    {
        Rewritten s = run("2 + 3*4", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "rewritten", "arithmetic is worked out");
        t.equal(s.expression, "14", "and gives the value");
        t.check(s.steps == 4, "as a plan, two operations and a check");
        // all_verified below reads the outcomes and never the sentences, so a detail that reverts
        // to a constant leaves it true and a regold rewrites the fixture around it.
        t.equal(s.verifications,
                "passed, no decimal literal appears in the expression | "
                "passed, every move came from a registered rewriting rule | "
                "passed, every registered strategy precondition has passed evidence | "
                "passed, Multiply (3 * 4) to get 12 | "
                "passed, Add (2 + 12) to get 14 | "
                "passed, both forms took the same value at all 6 assignments that could be worked "
                "out, which is evidence of the identity rather than a proof of it",
                "and each of the six checks names the arithmetic it did");
        t.evidence("ALG-001",
                   s.outcome == RewriteOutcome::Rewritten && s.expression == "14" &&
                       s.steps == 4 && s.all_verified,
                   "an arithmetic expression is simplified with each operation shown as its own "
                   "verified step");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "3x + 5x").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Simplify);
        t.equal(print(arena, r.expression), "(8 * x)", "like terms add their coefficients");
        t.check(rule_count(d, "alg.collect-like-terms") == 1,
                "through one collecting step rather than silently");
    }
    {
        Rewritten s = run("3x + 5x + -2x", RewriteGoal::Simplify);
        t.equal(s.expression, "(6 * x)", "three like terms collect a pair at a time");
    }
    {
        // The sign written into a factor rather than in front of the term. Both spell the same
        // term, and until they keyed the same the engine said "already in that form" about an
        // expression equal to zero.
        Rewritten s = run("(-x)*y + x*y", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "rewritten",
                "a sign inside a factor does not hide a term from its twin");
        t.equal(s.expression, "0", "so the two cancel to nothing");
    }
    {
        Rewritten s = run("x*y + (-x)*y", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "and it reads the same written the other way round");
    }
    {
        Rewritten s = run("(-2*x)*y + 2*x*y", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "a coefficient beside the sign is pulled out with it");
    }
    {
        Rewritten s = run("(-x)*y + 3*x*y", RewriteGoal::Simplify);
        t.equal(s.expression, "(2 * (x * y))",
                "and a pair that does not cancel still gathers to one term");
    }
    {
        Rewritten s = run("(a*b)*c - a*(b*c)", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "a bracketed product and the same product regrouped are one term");
    }
    {
        Rewritten s = run("(2*a*b)*c - a*(2*b*c)", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "and they still meet with a coefficient inside each grouping");
    }
    {
        Rewritten s = run("x*y - y*x", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "factors written in the other order are the same term");
    }
    {
        Rewritten s = run("(a*b)*c - c*(b*a)", RewriteGoal::Simplify);
        t.equal(s.expression, "0", "regrouped and reordered at once is still one term");
    }
    {
        // The sign has to survive being pulled out, not merely be removed.
        Rewritten s = run("(-x)*y", RewriteGoal::Simplify);
        t.equal(s.expression, "(-(x * y))", "one such term on its own keeps its sign");
    }
    {
        Rewritten s = run("(x - x)*y", RewriteGoal::Expand);
        t.equal(s.expression, "0", "and a bracket that is zero takes the product with it");
    }
    {
        Rewritten s = run("2x + 3y + 4x", RewriteGoal::Simplify);
        t.check(same_value(s.expression, "6x + 3y"), "unlike terms stay apart while like ones join");
    }
    {
        Rewritten s = run("x + 3 + -3", RewriteGoal::Simplify);
        t.equal(s.expression, "x", "a constant that cancels leaves nothing behind");
    }
    {
        Rewritten s = run("x*x", RewriteGoal::Simplify);
        t.equal(s.expression, "(x^2)", "a repeated factor becomes a power");
    }
    {
        Rewritten s = run("x", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "already in that form",
                "an expression with nothing to do says so rather than inventing a step");
        t.check(s.steps == 1, "leaving only the plan");
    }
    {
        Rewritten s = run("9223372036854775807x + x", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "resource exceeded",
                "coefficients that outgrow exact integer arithmetic report capacity exhaustion");
        t.check(s.detail.find("coefficients grew") != std::string::npos,
                "reaching the coefficient-addition refusal is explicit");
        t.equal(s.status, "resource limit reached",
                "and says it ran out of arithmetic rather than out of rules");
        t.check(s.expression.empty() && s.steps == 0, "offering no answer and no partial record");
    }
    {
        Rewritten s =
            run("9223372036854775807*2*x + y", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "resource exceeded",
                "an unreadable oversized term reports capacity exhaustion");
        t.check(s.detail.find("a term grew") != std::string::npos,
                "reaching the term-construction refusal is explicit");
        t.equal(s.status, "resource limit reached",
                "and does not turn exhausted arithmetic into an unsupported form");
        t.check(s.expression.empty() && s.steps == 0, "offering no answer and no partial record");
    }

    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "(x + 1)(x + 2)").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Expand);
        const std::string got = print(arena, r.expression);
        t.equal(rewrite_outcome_name(r.outcome), "rewritten", "a product of sums expands");
        t.check(same_value(got, "x^2 + 3x + 2"),
                "to the x squared plus three x plus two an independent solver gives");
        t.check(rule_count(d, "alg.distribute") >= 2,
                "with each distribution shown rather than one leap");
        t.check(rule_count(d, "alg.collect-like-terms") >= 1, "and the middle terms gathered");
        t.evidence("ALG-002",
                   r.outcome == RewriteOutcome::Rewritten && same_value(got, "x^2 + 3x + 2") &&
                       rule_count(d, "alg.distribute") >= 2 &&
                       rule_count(d, "alg.collect-like-terms") >= 1,
                   "a polynomial product is expanded and collected through named distribution and "
                   "like-term rules");
    }
    {
        Rewritten s = run("(x + 1)^2", RewriteGoal::Expand);
        t.check(same_value(s.expression, "x^2 + 2x + 1"), "a squared bracket expands");
    }
    {
        Rewritten s = run("2(x + 3)", RewriteGoal::Expand);
        t.check(same_value(s.expression, "2x + 6"), "a number multiplies into a bracket");
    }
    {
        Rewritten s = run("(x + 1)(x + -1)", RewriteGoal::Expand);
        t.check(same_value(s.expression, "x^2 - 1"), "the middle terms cancel to nothing");
    }
    {
        Rewritten s = run("x + 1", RewriteGoal::Expand);
        t.equal(rewrite_outcome_name(s.outcome), "already in that form",
                "a sum with no product to distribute is already expanded");
    }

    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2 + 3x + 2").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Factor);
        const std::string got = print(arena, r.expression);
        t.equal(rewrite_outcome_name(r.outcome), "rewritten", "a quadratic factors");
        t.check(same_value(got, "(x + 1)(x + 2)"), "into the two brackets that multiply back to it");
        t.check(rule_count(d, "alg.factor.product-and-sum") == 1,
                "by the product and sum rule rather than by an unnamed leap");
        t.evidence("ALG-002",
                   r.outcome == RewriteOutcome::Rewritten && same_value(got, "(x + 1)(x + 2)") &&
                       rule_count(d, "alg.factor.product-and-sum") == 1,
                   "a quadratic is factored by the named product and sum rule and checked by "
                   "multiplying the brackets back out");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "x^2 + -4").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Factor);
        t.check(same_value(print(arena, r.expression), "(x + 2)(x - 2)"),
                "a difference of squares factors");
        t.check(rule_count(d, "alg.factor.difference-of-squares") == 1,
                "under the name a course gives it rather than as a generic quadratic");
    }
    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "2x^2 + 8x + 6").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Factor);
        // SymPy factors this as 2*(x + 1)*(x + 3).
        t.check(same_value(print(arena, r.expression), "2(x + 1)(x + 3)"),
                "a common factor comes out before the quadratic is factored");
        t.check(rule_count(d, "alg.factor.common-factor") == 1, "as a step of its own");
        t.check(rule_count(d, "alg.factor.product-and-sum") == 1, "followed by the pair");
    }
    {
        Rewritten s = run("3x + 6", RewriteGoal::Factor);
        t.check(same_value(s.expression, "3(x + 2)"), "a linear sum gives up its common factor");
    }
    {
        Rewritten s = run("x^2 + x", RewriteGoal::Factor);
        t.check(same_value(s.expression, "x*(x + 1)"), "and so does a shared variable");
    }
    {
        // A term of zero has to go before anything is factored: leaving it in makes the pair rule
        // read the constant as zero and offer a bracket of (x + 0), which is true and useless.
        Rewritten s = run("x^2 + 4x + 0", RewriteGoal::Factor);
        t.check(same_value(s.expression, "x*(x + 4)"), "a written zero does not become a bracket");
        t.check(s.expression.find("+ 0") == std::string::npos,
                "and no bracket carries a term of zero");
    }
    {
        Rewritten s = run("x^2 + 0*x + 4", RewriteGoal::Simplify);
        t.check(same_value(s.expression, "x^2 + 4"), "a term with a zero coefficient goes too");
        t.check(s.expression.find("0") == std::string::npos, "and leaves no zero behind");
    }
    {
        Rewritten s = run("0 + 0", RewriteGoal::Simplify);
        t.check(s.outcome == RewriteOutcome::Rewritten || s.outcome == RewriteOutcome::AlreadyInForm,
                "a sum of nothing but zeros is still answered");
        t.check(same_value(s.expression, "0"), "and the answer is zero");
    }
    {
        Rewritten s = run("x^2 + x + 1", RewriteGoal::Factor);
        t.equal(rewrite_outcome_name(s.outcome), "already in that form",
                "a quadratic with no whole-number pair is left alone rather than forced");
        t.check(s.detail.find("no whole-number pair") != std::string::npos,
                "and says what was looked for");
    }
    {
        Rewritten s = run("2x^2 + 3x + 1", RewriteGoal::Factor);
        t.equal(rewrite_outcome_name(s.outcome), "already in that form",
                "a quadratic this rule cannot make monic is refused rather than half factored");
    }
    {
        // The ceiling and the answer either side of it. A constant one past the size this rule
        // searches to leaves the question open, and saying the expression is already factored would
        // answer it, with a status calling the answer verified.
        Rewritten ceiling = run("x^2 + x + 1000001", RewriteGoal::Factor);
        t.equal(rewrite_outcome_name(ceiling.outcome), "resource exceeded",
                "a constant past the search ceiling is the search never having run");
        t.equal(ceiling.status, "resource limit reached",
                "and the record does not call an undecided question verified");

        Rewritten absent = run("x^2 + x + 999999", RewriteGoal::Factor);
        t.equal(rewrite_outcome_name(absent.outcome), "already in that form",
                "while a constant inside the ceiling with no pair is still left alone");
        t.equal(absent.status, "solved and verified",
                "because that search did run and came back empty");

        Rewritten found = run("x^2 + 3x + 2", RewriteGoal::Factor);
        t.equal(rewrite_outcome_name(found.outcome), "rewritten",
                "and a pair inside the ceiling is still found");
    }

    {
        Rewritten s = run("1.5 + 2", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "unsupported form",
                "a decimal is refused rather than turned into a fraction");
        t.check(s.steps == 0, "with no derivation recorded");
    }
    {
        Rewritten s = run("x = 1", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "unsupported form",
                "an equation has two expressions and is not one");
    }
    {
        Rewritten s = run("x/0", RewriteGoal::Simplify);
        t.equal(rewrite_outcome_name(s.outcome), "refused",
                "an expression that divides by zero is refused at the door");
    }
    {
        Budget cancelling;
        cancelling.poll = always_cancel;
        Rewritten s = run("2 + 3*4", RewriteGoal::Simplify, nullptr, cancelling);
        t.equal(rewrite_outcome_name(s.outcome), "cancelled", "an existing cancel stops the work");
        t.check(s.steps == 0, "with no partial derivation left behind");
    }
    {
        Budget tight;
        tight.max_steps = 2;
        Rewritten s = run("2 + 3*4", RewriteGoal::Simplify, nullptr, tight);
        t.equal(rewrite_outcome_name(s.outcome), "resource exceeded",
                "a step budget too small to finish halts");
        // STEP-025 wants both halves. A prefix nobody can tell apart from a finished solve is the
        // failure the requirement is about, so the answer is asserted absent alongside it.
        t.check(s.steps > 0, "keeping the moves that were checked");
        t.check(s.all_verified, "each of which passed");
        t.check(s.expression.empty(), "while answering nothing for the goal it did not reach");
        t.equal(s.status, "resource limit reached", "and saying what stopped it");
        t.check(s.cost.steps > 0, "while reporting what it spent");
    }
    {
        // The cancel above fires before any work, so it proves nothing about what a cancel keeps.
        // This one fires part way through, which is the state PERF-013 asks to be restorable.
        cancel_countdown = 1;
        Budget late;
        late.poll = cancel_after_working;
        Rewritten s = run("(x + 1)(x + 2)(x + 3)(x + 4)(x + 5)", RewriteGoal::Expand, nullptr, late);
        t.equal(rewrite_outcome_name(s.outcome), "cancelled", "a cancel part way through stops it");
        t.check(s.steps > 0, "keeping the moves that were checked");
        t.check(s.all_verified, "each of which passed");
        t.check(s.expression.empty(), "while answering nothing for the goal it did not reach");
        t.equal(s.status, "cancelled",
                "and saying the user stopped it rather than that it got part of the way");
        t.evidence("PERF-013",
                   s.outcome == RewriteOutcome::Cancelled && s.steps > 0 && s.all_verified &&
                       s.expression.empty() && s.status == "cancelled",
                   "a cancellation part way through a rewrite keeps every step that was checked, "
                   "offers no answer for the goal it did not reach and says the user stopped it, "
                   "which is the last checked state the requirement asks to be restorable");
    }

    {
        ScriptedGiac agreeing("0");
        Rewritten s = run("(x + 1)(x + 2)", RewriteGoal::Expand, &agreeing);
        t.equal(rewrite_outcome_name(s.outcome), "rewritten",
                "a backend that agrees leaves the answer standing");
        t.check(agreeing.calls == 1, "having been asked once");
    }
    {
        Arena arena;
        Derivation d;
        ScriptedGiac agreeing("0");
        const NodeId expression = parse(arena, "(x + 1)(x + 2)").root;
        const RewriteResult r = rewrite(arena, d, expression, RewriteGoal::Expand, Budget(),
                                        &agreeing);
        size_t rule_local = 0;
        size_t cross_checked = 0;
        for (size_t i = 0; i < d.size(); ++i) {
            const Step &s = d.at(static_cast<StepId>(i));
            for (size_t j = 0; j < s.verifications.size(); ++j) {
                if (s.verifications[j].outcome != VerificationOutcome::Passed)
                    continue;
                if (s.verifications[j].method.find("rule-local") != std::string::npos)
                    ++rule_local;
                if (s.verifications[j].detail.find("Giac") != std::string::npos)
                    ++cross_checked;
            }
        }
        const bool assumptions_recorded = d.context.angle_convention == "radians" &&
                                          d.context.branch_convention == "real domain";
        t.check(rule_local >= 3, "every transformation of an expansion carries a rule-local check");
        t.check(cross_checked == 1, "and the answer is cross-checked once by symbolic simplification");
        t.check(assumptions_recorded, "under the conventions the context records");

        // The "when available" half. Without a backend the rule-local checks stand alone and the
        // answer is still offered, so the cross-check is an addition rather than a precondition.
        Derivation alone;
        Arena bare;
        const RewriteResult without =
            rewrite(bare, alone, parse(bare, "(x + 1)(x + 2)").root, RewriteGoal::Expand);
        size_t rule_local_alone = 0;
        size_t cross_checked_alone = 0;
        for (size_t i = 0; i < alone.size(); ++i) {
            const Step &s = alone.at(static_cast<StepId>(i));
            for (size_t j = 0; j < s.verifications.size(); ++j) {
                if (s.verifications[j].outcome != VerificationOutcome::Passed)
                    continue;
                if (s.verifications[j].method.find("rule-local") != std::string::npos)
                    ++rule_local_alone;
                if (s.verifications[j].detail.find("Giac") != std::string::npos)
                    ++cross_checked_alone;
            }
        }
        t.check(without.outcome == RewriteOutcome::Rewritten && rule_local_alone >= 3,
                "and with no backend to ask, the rule-local checks carry the answer on their own");
        // Counting zero here is what stops the count above from being incidental: it is the backend
        // that the one cross-check comes from, rather than some other check that mentions Giac.
        t.check(cross_checked_alone == 0, "with no cross-check to be had and none claimed");

        t.evidence("VER-001",
                   r.outcome == RewriteOutcome::Rewritten && rule_local >= 3 &&
                       cross_checked == 1 && assumptions_recorded &&
                       without.outcome == RewriteOutcome::Rewritten && rule_local_alone >= 3,
                   "an algebraic identity transformation is validated by rule-local invariants on "
                   "every step and cross-checked once by symbolic simplification under the recorded "
                   "angle and branch conventions, and stands on the invariants alone when no "
                   "backend is available");
    }
    {
        ScriptedGiac disagreeing("x");
        Rewritten s = run("(x + 1)(x + 2)", RewriteGoal::Expand, &disagreeing);
        t.equal(rewrite_outcome_name(s.outcome), "verification failed",
                "a backend that disagrees fails the answer");
        t.check(s.expression.empty(), "which is then not offered");
        t.check(s.steps > 0, "while the record stays so the failing check can be read");
    }

    {
        Arena arena;
        Derivation d;
        const NodeId expression = parse(arena, "2 + 3*4").root;
        rewrite(arena, d, expression, RewriteGoal::Simplify);
        const bool rooted = d.roots().size() == 1;
        const StepId plan = rooted ? d.roots()[0] : kNoStep;
        const bool shaped = rooted && d.at(plan).children.size() == 3;
        t.check(shaped, "the plan carries the two operations and the check");
        const TransformationPayload *first = shaped ? d.transformation(d.at(plan).children[0])
                                                    : nullptr;
        t.check(first != nullptr, "the first child is a transformation");
        if (first) {
            t.equal(print(arena, first->before), "(2 + (3 * 4))", "starting from the expression");
            t.equal(print(arena, first->after), "(2 + 12)", "with only the inner product worked out");
            t.check(first->path.size() == 1 && first->path[0] == 1,
                    "and the path naming which part was rewritten");
        }
        // No requirement tag here on purpose. This proves the trace is fine-grained, which is a
        // premise of STEP-016 rather than the requirement itself: that one is about a presentation
        // level aggregating the trace, and there is no presentation layer in this engine to test.
        t.check(shaped && first != nullptr && print(arena, first->after) == "(2 + 12)",
                "each arithmetic operation is its own step rather than one simplified answer");
    }
}

}  // namespace nps
