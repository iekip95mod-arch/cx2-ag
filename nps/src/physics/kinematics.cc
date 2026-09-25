#include "nps/physics/kinematics.h"

#include "nps/core/context.h"
#include "nps/steps/linear.h"
#include "nps/steps/quadratic.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "algebra_first.h"

namespace nps {
namespace {

// The plan step carries the link and the context carries the summary, so this is named once rather
// than spelled twice. The step is what makes it true, and a summary that drifts from it is worse
// than no summary.
const char kConstantAcceleration[] = "acceleration is constant";

struct SymbolInfo {
    const char *symbol;
    const char *name;
    Dimension dimension;
};

Dimension dim(int length, int time) {
    Dimension d;
    d.length = length;
    d.time = time;
    return d;
}

const SymbolInfo *symbols(size_t *count) {
    static const SymbolInfo table[] = {
        {"v0", "initial velocity", dim(1, -1)}, {"v", "final velocity", dim(1, -1)},
        {"a", "acceleration", dim(1, -2)},      {"t", "time", dim(0, 1)},
        {"x", "displacement", dim(1, 0)},
    };
    *count = sizeof(table) / sizeof(table[0]);
    return table;
}

const SymbolInfo *find_symbol(const std::string &symbol) {
    size_t count;
    const SymbolInfo *table = symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (symbol == table[i].symbol)
            return &table[i];
    }
    return nullptr;
}

// The equations themselves and nothing more: which one can be solved for which symbol is the
// linear solver's verdict, not a table's.
struct Equation {
    const char *name;
    const char *text;
    const char *symbols[4];
};

const Equation *equations(size_t *count) {
    static const Equation table[] = {
        {"velocity from acceleration and time", "v = v0 + a*t", {"v", "v0", "a", "t"}},
        {"displacement from initial velocity, acceleration and time", "x = v0*t + (1/2)*a*t^2",
         {"x", "v0", "a", "t"}},
        {"velocity squared from acceleration and displacement", "v^2 = v0^2 + 2*a*x",
         {"v", "v0", "a", "x"}},
        {"displacement from the average velocity", "x = (1/2)*(v0 + v)*t", {"x", "v0", "v", "t"}},
    };
    *count = sizeof(table) / sizeof(table[0]);
    return table;
}

bool equation_has(const Equation &e, const std::string &symbol) {
    for (int i = 0; i < 4; ++i) {
        if (symbol == e.symbols[i])
            return true;
    }
    return false;
}

bool names_contain(const std::vector<std::string> &names, const std::string &s) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == s)
            return true;
    }
    return false;
}

// Which rule isolated the target. The equations are degree two in t and in either velocity, so an
// engine that stops at degree one cannot reach half of them, and which one answered decides what
// the hop still has to record.
enum class Engine : uint8_t { Linear, SquareRoot, Formula };

// One equation solved for one quantity. A route is a sequence of these ending at the unknown, and
// each hop's answer is a known quantity for the hops after it.
struct Hop {
    const Equation *equation = nullptr;
    std::string produces;
    NodeId symbolic = kNoNode;
    NodeId numeric = kNoNode;
    NodeId value = kNoNode;
    Engine engine = Engine::Linear;
    SolveOutcome solve_outcome = SolveOutcome::Refused;
    DerivationStatus refusal_status = DerivationStatus::NotRecorded;
    // Why each equation this hop did not use was not used, in the solver's own words where the
    // solver was the one that refused.
    std::vector<std::string> alternatives;
};

std::string trimmed(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
        --e;
    return s.substr(b, e - b);
}

bool starts_with(const std::string &s, const char *prefix) {
    size_t n = 0;
    while (prefix[n])
        ++n;
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

// The symbol as the user may spell it: d for displacement as well as x.
std::string canonical_symbol(const std::string &s) {
    if (s == "d")
        return "x";
    return s;
}

struct Context {
    Context(Arena &a, Derivation &d, Meter &m, Backend *g) : arena(a), derivation(d), meter(m), giac(g) {}
    Arena &arena;
    Derivation &derivation;
    Meter &meter;
    Backend *giac;
    // Set when a step budget or a cancellation stops the work, our own or the solver's.
    bool halted = false;
    bool cancelled = false;
    // A nested solve counts against its own meter, so ours can still read as running after one has
    // stopped. What it reported is kept here to be reported on rather than replaced by a guess.
    std::string halt_detail;
    Cost halt_cost;
};

Step envelope(const std::string &goal, const char *rule_id, const std::string &rule_name,
              const std::string &why) {
    Step s;
    s.phase = "solve";
    s.goal = goal;
    s.rule_id = rule_id;
    s.rule_name = rule_name;
    s.explanation_short = why;
    s.claim = ClaimType::SolutionSetPreserved;
    return s;
}

VerificationRecord verified(const char *method, const std::string &detail,
                           EvidenceStrength passing, bool passed) {
    VerificationRecord v;
    v.method = method;
    v.outcome = passed ? VerificationOutcome::Passed : VerificationOutcome::Failed;
    v.strength = strength_for(v.outcome, passing);
    v.detail = detail;
    return v;
}

void record(Context &ctx, StepId parent, Step s, NodeId before, NodeId after,
            const std::string &action) {
    if (!ctx.meter.step()) {
        ctx.halted = true;
        return;
    }
    TransformationPayload p;
    p.before = before;
    p.after = after;
    p.concrete_action = action;
    p.reversible = true;
    ctx.derivation.add_transformation(parent, std::move(s), std::move(p));
}

// A value as the linear solver reads it: an integer, or a numerator times a reciprocal.
NodeId rational_node(Arena &arena, const Rational &r) {
    if (r.den == 1)
        return arena.integer(integer_text(r.num));
    NodeId n = arena.integer(integer_text(r.num));
    NodeId d = arena.integer(integer_text(r.den));
    return arena.binary(Kind::Mul, n, arena.binary(Kind::Pow, d, arena.integer("-1")));
}

// The reverse, for the solver's answer: an integer, a negation, or a numerator times a
// reciprocal, in either spelling of the exponent.
bool rational_of_node(const Arena &arena, NodeId id, Rational *out) {
    const Node &n = arena.at(id);
    int64_t value;
    if (n.kind == Kind::Integer || n.kind == Kind::Neg) {
        if (!small_integer(arena, id, &value)) {
            if (n.kind != Kind::Neg)
                return false;
            Rational inner;
            Rational zero;
            return rational_of_node(arena, arena.children(n)[0], &inner) &&
                   rational_sub(zero, inner, out);
        }
        out->num = value;
        out->den = 1;
        return true;
    }
    const ChildView c = arena.children(n);
    if (n.kind != Kind::Mul || c.size() != 2)
        return false;
    Rational num;
    if (!rational_of_node(arena, c[0], &num) || num.den != 1)
        return false;
    const Node &recip = arena.at(c[1]);
    const ChildView rc = arena.children(recip);
    int64_t den, exponent;
    if (recip.kind != Kind::Pow || !small_integer(arena, rc[0], &den) ||
        !small_integer(arena, rc[1], &exponent) || exponent != -1 || den == 0)
        return false;
    out->num = num.num;
    out->den = den;
    return normalise(&out->num, &out->den);
}

// The expression with each symbol replaced by its value node. Symbols are interned, so this is a
// rebuild rather than a search.
NodeId substitute(Arena &arena, NodeId id, const std::vector<std::string> &names,
                  const std::vector<NodeId> &values) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Symbol) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (arena.text(id) == names[i])
                return values[i];
        }
        return id;
    }
    const ChildView kids = arena.children(n);
    if (kids.empty())
        return id;
    std::vector<NodeId> args;
    args.reserve(kids.size());
    for (NodeId a : kids)
        args.push_back(substitute(arena, a, names, values));
    if (n.kind == Kind::Call)
        return arena.call(arena.text(id), args);
    return arena.nary(n.kind, args);
}

// Everything the search needs to try a hop, in one place so the recursion carries one reference.
struct Search {
    Search(Arena &a, Derivation &d, const Equation *t, size_t c, const Budget &b, Meter &m,
           const std::vector<std::string> &known_names, const std::vector<NodeId> &known_values)
        : arena(a), derivation(d), table(t), count(c), budget(b), meter(m), names(known_names),
          values(known_values), used(c, false) {}
    Arena &arena;
    // The real derivation, whose memory of solved runs each probe shares so the route's equations
    // are solved once.
    Derivation &derivation;
    const Equation *table;
    size_t count;
    const Budget &budget;
    Meter &meter;
    // The symbols with a value, and the values, growing as hops close. A hop's answer is a known
    // quantity for every hop after it.
    std::vector<std::string> names;
    std::vector<NodeId> values;
    std::vector<bool> used;
    bool halted = false;
    bool cancelled = false;
    std::string halt_detail;
    // Only what a probe spent on its own meter. Ours is read directly, so copying it here doubles it.
    Cost halt_cost;
    // The quantity actually asked for, and why each equation failed to reach it. A deeper recursion
    // explains a branch the reader never asked about, so only the outermost level's reasons are
    // kept and they are what a refusal reports.
    std::string goal;
    std::vector<std::string> goal_reasons;
    bool has_answer_candidate = false;
    Hop answer_candidate;
    std::string answer_candidate_reason;
};

// What the linear solver made of one equation. A halt stops the route search and does not stop the
// labelling of an equation the search has already passed over, so which one it is belongs to the
// caller rather than to a flag here.
enum class Probe : uint8_t {
    Solved,
    NoSolution,
    Refused,
    Halted,
};

enum class RouteOutcome : uint8_t {
    NotFound,
    Complete,
    Contradiction,
};

// What the engines between them made of one equation, in the shape the route search reads. The
// order they are tried in lives here alone: the route search and the real solve have to agree about
// which rule answers an equation, and a second copy of the order is a second thing to keep in step.
struct Attempt {
    Engine engine = Engine::Linear;
    SolveOutcome outcome = SolveOutcome::Refused;
    std::vector<NodeId> solutions;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

void charge_attempt(Attempt *attempt, const Cost &spent) {
    attempt->cost.steps += spent.steps;
    attempt->cost.rewrites += spent.rewrites;
    attempt->cost.branches += spent.branches;
    attempt->cost.backend_calls += spent.backend_calls;
    attempt->cost.replayed += spent.replayed;
}

// A quadratic outcome said in the vocabulary the route search already speaks, so one switch handles
// every engine rather than each caller learning two enums.
SolveOutcome as_solve_outcome(QuadraticOutcome outcome) {
    switch (outcome) {
        case QuadraticOutcome::Solved: return SolveOutcome::Solved;
        case QuadraticOutcome::NoRealSolution: return SolveOutcome::NoSolution;
        case QuadraticOutcome::NotAnEquation: return SolveOutcome::NotAnEquation;
        case QuadraticOutcome::Cancelled: return SolveOutcome::Cancelled;
        case QuadraticOutcome::ResourceExceeded: return SolveOutcome::ResourceExceeded;
        case QuadraticOutcome::NotPureQuadratic:
        case QuadraticOutcome::OutsideEnvelope:
        case QuadraticOutcome::Refused: break;
    }
    return SolveOutcome::Refused;
}

Attempt isolate(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                const Budget &budget) {
    Attempt out;
    const SolveResult linear = solve_linear(arena, derivation, equation, unknown, budget);
    out.outcome = linear.outcome;
    out.detail = linear.detail;
    out.status = linear.status;
    charge_attempt(&out, linear.cost);
    if (linear.outcome == SolveOutcome::Solved) {
        out.solutions.push_back(linear.solution);
        return out;
    }
    // Degree one is the only thing the linear rule refuses that another rule here can take. Every
    // other refusal is about the equation rather than about the rule, so it stays the last word.
    if (linear.outcome != SolveOutcome::NotLinear)
        return out;

    const QuadraticResult square = solve_by_square_root(arena, derivation, equation, unknown, budget);
    charge_attempt(&out, square.cost);
    if (square.outcome != QuadraticOutcome::NotPureQuadratic) {
        out.engine = Engine::SquareRoot;
        out.outcome = as_solve_outcome(square.outcome);
        out.detail = square.detail;
        out.status = square.status;
        out.solutions = square.solutions;
        return out;
    }

    const QuadraticResult formula = solve_quadratic(arena, derivation, equation, unknown, budget);
    charge_attempt(&out, formula.cost);
    if (formula.outcome == QuadraticOutcome::NotPureQuadratic)
        return out;
    out.engine = Engine::Formula;
    out.outcome = as_solve_outcome(formula.outcome);
    out.detail = formula.detail;
    out.status = formula.status;
    out.solutions = formula.solutions;
    return out;
}

// A root as a reader expects to see it, 4 or -22/3 rather than the arena's spelling of a reciprocal.
std::string node_value_text(const Arena &arena, NodeId id) {
    Rational value;
    return rational_of_node(arena, id, &value) ? rational_text(value) : print(arena, id);
}

bool known_value(const Arena &arena, const std::vector<std::string> &names,
                 const std::vector<NodeId> &values, const std::string &symbol, Rational *out) {
    for (size_t i = 0; i < names.size() && i < values.size(); ++i) {
        if (names[i] == symbol)
            return rational_of_node(arena, values[i], out);
    }
    return false;
}

// Which of an equation's roots the problem is asking about, and the physical reason for it. An
// undecided answer is an answer: two admissible values mean the motion really does reach the stated
// condition twice and the problem has not said which, so nothing is picked and the refusal says so.
struct RootChoice {
    bool decided = false;
    size_t index = 0;
    std::string assumption;
    std::string why;
};

std::string roots_text(const std::string &target, const std::vector<Rational> &roots,
                       const std::vector<size_t> &which) {
    std::string out;
    for (size_t i = 0; i < which.size(); ++i)
        out += (out.empty() ? "" : " and ") + target + " = " + rational_text(roots[which[i]]);
    return out;
}

std::string every_root(const std::string &target, const std::vector<Rational> &roots) {
    std::vector<size_t> all(roots.size());
    for (size_t i = 0; i < all.size(); ++i)
        all[i] = i;
    return roots_text(target, roots, all);
}

RootChoice choose_physical_root(const Arena &arena, const std::string &target,
                                const std::vector<std::string> &names,
                                const std::vector<NodeId> &values,
                                const std::vector<Rational> &roots) {
    RootChoice out;
    std::string assumption;
    bool keep_non_negative = true;
    if (target == "t") {
        assumption = "a time measured from the start of the interval is not negative";
    } else if (target == "v" || target == "v0") {
        // v = v0 + a*t over an interval with t >= 0, so the other velocity and the acceleration fix
        // this one's sign when they agree, and nothing here fixes it when they do not.
        const std::string other = target == "v" ? "v0" : "v";
        Rational other_value;
        Rational acceleration;
        if (!known_value(arena, names, values, other, &other_value) ||
            !known_value(arena, names, values, "a", &acceleration)) {
            out.why = "the sign of " + target + " is not fixed by what is given, so neither root is "
                                                "the one the problem means";
            return out;
        }
        const bool acceleration_forward =
            target == "v" ? acceleration.num >= 0 : acceleration.num <= 0;
        const bool acceleration_backward =
            target == "v" ? acceleration.num <= 0 : acceleration.num >= 0;
        const std::string relation =
            target == "v" ? "v = v0 + a*t" : "v0 = v - a*t";
        const std::string acceleration_sign_forward =
            target == "v" ? "a are both non-negative" : "a is non-positive";
        const std::string acceleration_sign_backward =
            target == "v" ? "a are both non-positive" : "a is non-negative";
        if (other_value.num >= 0 && acceleration_forward) {
            keep_non_negative = true;
            assumption = (target == "v" ? "v0 and " : "v is non-negative and ") +
                         acceleration_sign_forward + ", so " + relation +
                         " is not negative for t >= 0";
        } else if (other_value.num <= 0 && acceleration_backward) {
            keep_non_negative = false;
            assumption = (target == "v" ? "v0 and " : "v is non-positive and ") +
                         acceleration_sign_backward + ", so " + relation +
                         " is not positive for t >= 0";
        } else if (roots.size() == 1) {
            // One root is the whole solution set, so there is no sign to choose between.
            out.decided = true;
            out.assumption = "the equation has a single root, so the sign of " + target +
                             " is not a choice";
            return out;
        } else {
            out.why = every_root(target, roots) + " both satisfy this, and the signs of " + other +
                      " and a do not say which one " + relation + " gives";
            return out;
        }
    } else {
        out.why = "nothing here says which root of " + target + " the problem means";
        return out;
    }

    std::vector<size_t> admissible;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (keep_non_negative ? roots[i].num >= 0 : roots[i].num <= 0)
            admissible.push_back(i);
    }
    if (admissible.size() == 1) {
        out.decided = true;
        out.index = admissible[0];
        out.assumption = assumption;
        return out;
    }
    if (admissible.empty()) {
        out.why = every_root(target, roots) + " is ruled out, because " + assumption;
        return out;
    }
    out.why = roots_text(target, roots, admissible) +
              " both satisfy this and the stated condition, and the problem does not say which is "
              "meant";
    return out;
}

// One equation offered to the linear solver with the given knowns substituted in. The solver is
// what decides whether the equation is usable, not a table: it is the engine that would have to do
// the isolation, so a quadratic or a square root is refused by the thing that can see it. The knowns
// are an argument because the two callers ask about different moments: the search asks about now,
// and the labelling asks about before this level took a route.
Probe offer(Search &s, const std::vector<std::string> &names, const std::vector<NodeId> &values,
            const Equation &e, const std::string &target, Hop *hop, std::string *reason) {
    // Candidate probes count as rewrites because they do not add derivation steps.
    if (!s.meter.rewrite()) {
        s.cancelled = s.meter.halt() == Halt::Cancelled;
        s.halt_detail = s.cancelled ? "cancelled while searching for a route"
                                    : "the search for a route ran out of budget";
        *reason = s.halt_detail;
        return Probe::Halted;
    }
    ParseResult parsed = parse(s.arena, e.text);
    if (!parsed.ok()) {
        *reason = "the equation table did not parse";
        return Probe::Refused;
    }
    NodeId candidate = substitute(s.arena, parsed.root, names, values);
    hop->equation = &e;
    hop->produces = target;
    hop->symbolic = parsed.root;
    hop->numeric = candidate;
    Derivation scratch;
    scratch.share_runs(s.derivation);
    const Attempt probe = isolate(s.arena, scratch, candidate, s.arena.symbol(target), s.budget);
    hop->refusal_status = probe.status;
    hop->solve_outcome = probe.outcome;
    hop->engine = probe.engine;
    if (probe.outcome == SolveOutcome::Cancelled || probe.outcome == SolveOutcome::ResourceExceeded) {
        s.cancelled = probe.outcome == SolveOutcome::Cancelled;
        s.halt_detail = probe.detail;
        s.halt_cost = probe.cost;
        *reason = probe.detail;
        return Probe::Halted;
    }
    if (probe.outcome == SolveOutcome::NoSolution) {
        *reason = probe.detail;
        return Probe::NoSolution;
    }
    if (probe.outcome != SolveOutcome::Solved) {
        *reason = probe.detail;
        return Probe::Refused;
    }
    if (probe.engine == Engine::Linear) {
        hop->value = probe.solutions.empty() ? kNoNode : probe.solutions[0];
        return Probe::Solved;
    }
    // The algebra gives every value that satisfies the equation. Which of them the problem is about
    // is a question about the motion, and one this route cannot take without an answer to.
    std::vector<Rational> roots;
    for (size_t i = 0; i < probe.solutions.size(); ++i) {
        Rational root;
        if (!rational_of_node(s.arena, probe.solutions[i], &root)) {
            *reason = "a root came back in a form this route cannot read as an exact value";
            hop->solve_outcome = SolveOutcome::Refused;
            hop->refusal_status = DerivationStatus::Unsupported;
            return Probe::Refused;
        }
        roots.push_back(root);
    }
    const RootChoice chosen = choose_physical_root(s.arena, target, names, values, roots);
    if (!chosen.decided) {
        *reason = chosen.why;
        // The algebra solved it and the physics did not, so the hop is refused rather than left
        // reading as solved with a refusal reason beside it.
        hop->solve_outcome = SolveOutcome::Refused;
        hop->refusal_status = DerivationStatus::Unsupported;
        return Probe::Refused;
    }
    hop->value = probe.solutions[chosen.index];
    return Probe::Solved;
}

// The route search's use of it, where a halt ends the search.
Probe try_equation(Search &s, const Equation &e, const std::string &target, Hop *hop,
                   std::string *reason) {
    const Probe p = offer(s, s.names, s.values, e, target, hop, reason);
    if (p == Probe::Halted)
        s.halted = true;
    return p;
}

// What is in this equation that reaching target would still need. Empty means the equation can be
// solved for target straight away, given what is known now.
std::vector<std::string> missing_for(const Search &s, const Equation &e, const std::string &target) {
    std::vector<std::string> missing;
    for (int j = 0; j < 4; ++j) {
        const std::string sym = e.symbols[j];
        if (sym != target && !names_contain(s.names, sym))
            missing.push_back(sym);
    }
    return missing;
}

std::string joined(const std::vector<std::string> &v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
        out += (out.empty() ? "" : ", ") + v[i];
    return out;
}

// Backward chaining from the unknown, which is section 15.2's search and the one Andes and
// solution_tracer both arrive at. An equation is used at most once on a path, so the depth is
// bounded by the table, and a branch is kept only when every quantity it needs is either given or
// produced by an earlier hop. The caller deepens one hop at a time, so a one-hop answer always wins
// over a two-hop one and the shortest derivation is the one recorded.
RouteOutcome chain(Search &s, const std::string &target, size_t limit, std::vector<Hop> *route) {
    // The limit is on the whole route rather than on this branch, so two missing quantities cannot
    // each spend it. Counting the hops already committed is what makes the depth the caller asked
    // for the depth it gets, and what makes a shorter route win.
    if (route->size() >= limit || s.halted)
        return RouteOutcome::NotFound;
    const size_t hops_left = limit - route->size();
    // What an equation the search passed over is judged against: the givens, plus whatever the route
    // produced on the way, and never the target itself. Substituting the target would leave nothing
    // to solve for and make every alternative look refused.
    std::vector<std::string> known_names = s.names;
    std::vector<NodeId> known_values = s.values;
    // Why each equation was passed over. The hop that succeeds carries these away as its
    // alternatives, and the outermost failure reports them as the refusal.
    std::vector<std::string> reasons(s.count);
    size_t taken = s.count;
    for (size_t i = 0; i < s.count && !s.halted; ++i) {
        const Equation &e = s.table[i];
        if (s.used[i]) {
            reasons[i] = "already used earlier on this route";
            continue;
        }
        if (!equation_has(e, target)) {
            reasons[i] = "does not contain " + target;
            continue;
        }
        const std::vector<std::string> missing = missing_for(s, e, target);
        // Each missing quantity costs a hop of its own, so a branch that cannot fit in what is left
        // is dropped before any solving is attempted. This is the dead-end filter.
        if (missing.size() >= hops_left) {
            reasons[i] = "needs " + joined(missing) + ", which is not given";
            continue;
        }
        if (taken != s.count) {
            // Already have a route. This one is still classified, because a plan that shows only
            // the equation it used has not shown the reader that the others were considered.
            if (!missing.empty()) {
                reasons[i] = "also reachable, and longer";
                continue;
            }
            // Every quantity it needs is known, so whether it would also have worked is the
            // solver's answer rather than the table's guess.
            Hop unused;
            std::string why;
            switch (offer(s, known_names, known_values, e, target, &unused, &why)) {
                case Probe::Solved: reasons[i] = "also applicable, not needed"; break;
                case Probe::NoSolution:
                case Probe::Refused: reasons[i] = why; break;
                case Probe::Halted: reasons[i] = "not tried, the search budget was spent"; break;
            }
            continue;
        }
        // Everything the recursion may touch, so a branch that fails leaves no trace. The used
        // flags matter as much as the names: a sub-hop that succeeded before a later one failed has
        // marked its equation, and leaving that mark would hide it from every branch tried after.
        const std::vector<bool> used_before = s.used;
        const size_t names_before = s.names.size();
        const size_t route_before = route->size();
        s.used[i] = true;
        RouteOutcome reached = RouteOutcome::Complete;
        // One less than the limit, because this level's own hop is not in the route yet and has to
        // be paid for. Siblings share what is left, so two missing quantities cannot each spend it.
        for (size_t m = 0; m < missing.size() && reached == RouteOutcome::Complete; ++m)
            reached = chain(s, missing[m], limit - 1, route);
        if (reached == RouteOutcome::Contradiction)
            return RouteOutcome::Contradiction;
        Hop hop;
        std::string reason;
        const Probe probe = reached == RouteOutcome::Complete
                                ? try_equation(s, e, target, &hop, &reason)
                                : Probe::Refused;
        if (probe == Probe::Solved || probe == Probe::NoSolution) {
            taken = i;
            route->push_back(hop);
            if (probe == Probe::NoSolution)
                return RouteOutcome::Contradiction;
            known_names = s.names;
            known_values = s.values;
            s.names.push_back(target);
            s.values.push_back(hop.value);
            continue;
        }
        if (reached == RouteOutcome::Complete && target == s.goal && missing.empty() &&
            hop.refusal_status == DerivationStatus::Unsupported && !s.has_answer_candidate) {
            s.has_answer_candidate = true;
            s.answer_candidate = hop;
            s.answer_candidate_reason = reason;
        }
        s.used = used_before;
        s.names.resize(names_before);
        s.values.resize(names_before);
        route->resize(route_before);
        // A failure inside the recursion is reported as what was wanted, not as the fact that a
        // search happened. "needs v0, and nothing reaches it" is the sentence a reader can act on.
        reasons[i] = reason.empty() ? "needs " + joined(missing) + ", and no equation reaches " +
                                          (missing.size() == 1 ? missing[0] : std::string("them"))
                                    : reason;
    }
    if (taken != s.count) {
        std::vector<std::string> alternatives;
        for (size_t i = 0; i < s.count; ++i) {
            if (i != taken)
                alternatives.push_back(std::string(s.table[i].text) + ": " + reasons[i]);
        }
        route->back().alternatives = std::move(alternatives);
        return RouteOutcome::Complete;
    }
    // Only the outermost call's reasons are worth reporting. A deeper one explains a branch the
    // reader never asked about.
    if (target == s.goal) {
        s.goal_reasons.clear();
        for (size_t i = 0; i < s.count; ++i)
            s.goal_reasons.push_back(std::string(s.table[i].text) + ": " + reasons[i]);
    }
    return RouteOutcome::NotFound;
}

// Deepening one hop at a time. kMaxHops is a bound on the search rather than a physics claim: with
// four equations over five quantities every SUVAT problem that is solvable at all closes in two, and
// the third is headroom for the families that come next.
const size_t kMaxHops = 3;

RouteOutcome find_route(Search &s, const std::string &target, std::vector<Hop> *route) {
    s.goal = target;
    for (size_t hops = 1; hops <= kMaxHops && !s.halted; ++hops) {
        route->clear();
        const RouteOutcome outcome = chain(s, target, hops, route);
        if (outcome != RouteOutcome::NotFound)
            return outcome;
    }
    return RouteOutcome::NotFound;
}

// The dimension of an expression over the kinematics symbols. A sum of unlike dimensions is the
// mismatch the check is looking for, and this reports it rather than picking one.
bool dimension_of(const Arena &arena, NodeId id, Dimension *out, std::string *why) {
    const Node &n = arena.at(id);
    switch (n.kind) {
        case Kind::Integer:
        case Kind::Decimal:
            *out = Dimension();
            return true;
        case Kind::Symbol: {
            const SymbolInfo *info = find_symbol(arena.text(id));
            if (!info) {
                *why = "no dimension is known for " + arena.text(id);
                return false;
            }
            *out = info->dimension;
            return true;
        }
        case Kind::Neg:
            return dimension_of(arena, arena.children(n)[0], out, why);
        case Kind::Add: {
            Dimension first;
            bool have = false;
            for (NodeId a : arena.children(n)) {
                Dimension d;
                if (!dimension_of(arena, a, &d, why))
                    return false;
                if (have && d != first) {
                    *why = "a sum of " + dimension_text(first) + " and " + dimension_text(d);
                    return false;
                }
                first = d;
                have = true;
            }
            *out = first;
            return true;
        }
        case Kind::Mul: {
            Dimension acc;
            for (NodeId a : arena.children(n)) {
                Dimension d;
                if (!dimension_of(arena, a, &d, why))
                    return false;
                Dimension product;
                if (!dimension_multiply(acc, d, &product)) {
                    *why = "the product dimension does not fit";
                    return false;
                }
                acc = product;
            }
            *out = acc;
            return true;
        }
        case Kind::Pow: {
            Dimension base;
            int64_t exponent;
            if (!dimension_of(arena, arena.children(n)[0], &base, why))
                return false;
            if (!small_integer(arena, arena.children(n)[1], &exponent) || exponent < -8 ||
                exponent > 8) {
                *why = "a power with an exponent that is not a small integer";
                return false;
            }
            if (!dimension_power(base, static_cast<int>(exponent), out)) {
                *why = "the powered dimension does not fit";
                return false;
            }
            return true;
        }
        default:
            *why = "a form with no dimension rule";
            return false;
    }
}

// A measured given is shown with the figures it was written with, so a step that says 1.0 m/s has
// two figures does not print it as 1. Exact arithmetic keeps the value either way.
std::string value_text(const Known &k) {
    std::string number = rational_text(k.quantity.value);
    if (k.quantity.precision.kind == NumberKind::Measured) {
        std::string as_written;
        if (rounded_text(k.quantity.value, k.quantity.precision.significant_digits, &as_written))
            number = as_written;
    }
    return number + (k.quantity.unit.text.empty() ? "" : " " + k.quantity.unit.text);
}

std::string quantity_text(const Known &k) { return k.symbol + " = " + value_text(k); }

// The problem as parse_kinematics would read it back, which is what makes a solve replayable: the
// chosen equation says which route was taken, never which givens produced it.
std::string problem_statement(const KinematicsProblem &problem) {
    std::string text = "find " + problem.unknown;
    for (const Known &k : problem.knowns)
        text += "; " + quantity_text(k);
    return text;
}

void record_context(Derivation &derivation, const Budget &budget, const KinematicsProblem &problem,
                    NodeId model, DerivationStatus status) {
    ContextInputs inputs;
    // Not normalized_expression: the format reserves that for the printed model and refuses a blob
    // where the two disagree. A structured family usually has no user text here, so the statement it
    // would have been typed as is the most faithful thing this engine can record when none arrives.
    inputs.original_expression = derivation.request.original_expression.empty()
                                     ? problem_statement(problem)
                                     : derivation.request.original_expression;
    inputs.application_version = application_version();
    inputs.problem_family_id = "physics.kinematics.constant-acceleration.one-dimension";
    inputs.requested_method = "select an equation, substitute in SI, solve as a linear equation";
    inputs.normalized_problem_model = model;
    inputs.active_assumptions.push_back(kConstantAcceleration);
    inputs.active_assumptions.push_back("motion is along one axis, positive in the chosen direction");
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain";
    inputs.unit_policy = "exact integers, decimals as written, every quantity converted to SI "
                         "before the equation is used; the answer is reported to the fewest "
                         "significant figures among the measured givens, and nothing is rounded "
                         "before then";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

KinematicsResult invalid(const std::string &detail) {
    KinematicsResult r;
    r.outcome = KinematicsOutcome::InvalidInput;
    r.detail = detail;
    r.status = DerivationStatus::InvalidInput;
    return r;
}

// Giac rearranges the symbolic equation for the unknown, and Giac then checks that its form with
// the values in equals the value the linear solver found. Without a backend the step is absent
// and the plan says so, since a step that could not be checked is worse than no step.
bool giac_rearrangement(Context &ctx, StepId plan_id, NodeId symbolic, NodeId unknown_symbol,
                        const std::vector<std::string> &names, const std::vector<NodeId> &values,
                        NodeId solver_value, NodeId *isolated, bool *disagreed) {
    Arena &arena = ctx.arena;
    Adapter adapter(arena, *ctx.giac);
    Request req;
    req.op = Op::Solve;
    req.target = symbolic;
    req.variable = unknown_symbol;
    const std::string name = arena.text(unknown_symbol);
    // Asked before the request goes out, since a count taken afterwards cannot refuse anything.
    if (!ctx.meter.backend_call()) {
        ctx.halted = true;
        return false;
    }
    Response r = adapter.run(req);
    const NodeId rearranged = r.single_value();
    if (!r.usable() || r.tag != ResultTag::Exact || rearranged == kNoNode) {
        // A backend that was asked and declined is not the same as no backend, and without this the
        // two look identical: a solve with no rearrangement step and nothing saying why.
        if (!ctx.meter.step()) {
            ctx.halted = true;
            return false;
        }
        Step s;
        s.phase = "check";
        s.goal = "Rearrange for " + name + " symbolically";
        s.claim = ClaimType::NoClaim;
        s.explanation_short =
            "The symbolic engine was asked to rearrange the equation and did not give an exact form";
        s.explanation_detailed =
            "The answer below comes from the linear solver alone. An independent rearrangement is "
            "what would have cross-checked it, so this solve has one check fewer than usual.";
        VerificationRecord v;
        v.method = "backend solve";
        v.outcome = VerificationOutcome::NotAttempted;
        v.detail = "Giac answered " + std::string(tag_name(r.tag));
        if (r.usable() && rearranged == kNoNode)
            v.detail += " but an isolated formula requires exactly one solution";
        s.verifications.push_back(v);
        CheckPayload c;
        c.target_claim = "Giac's rearrangement agrees with the linear solver's value";
        c.check_method = "backend solve, compared by backend is_zero";
        c.expected_relation = "an exact rearrangement to compare against";
        c.observed_result = v.detail;
        ctx.derivation.add_check(plan_id, std::move(s), std::move(c));
        return false;
    }

    NodeId with_values = substitute(arena, rearranged, names, values);
    Request zero;
    zero.op = Op::IsZero;
    zero.target = arena.binary(Kind::Add, with_values,
                               arena.binary(Kind::Mul, arena.integer("-1"), solver_value));
    // Building the comparison can exhaust the arena, and a terminal resource status is not a state
    // to call a backend from. Without this the second request went out carrying kNoNode.
    if (arena.failed())
        return false;
    if (!ctx.meter.backend_call()) {
        ctx.halted = true;
        return false;
    }
    Response z = adapter.run(zero);
    bool agrees = false;
    VerificationOutcome compared = VerificationOutcome::Inconclusive;
    std::string observed;
    if (z.usable()) {
        int64_t small;
        agrees = small_integer(arena, z.value, &small) && small == 0;
        // Agreement corroborates rather than proves, because Giac wrote the form it is being asked
        // about, which is the call differentiate.cc:651-659 makes for VER-004.
        compared = agrees ? VerificationOutcome::Inconclusive : VerificationOutcome::Failed;
        observed = agrees ? "Giac agrees, but Giac was also asked for part of the answer it is "
                            "checking, so this corroborates the result rather than proving it"
                          : "Giac reports a difference";
    } else {
        observed = "Giac could not compare the two: " + std::string(tag_name(z.tag));
    }

    Step s = envelope("Rearrange for " + name, "kin.rearrange", "Rearrange symbolically",
                      "Solve the equation for " + name + " while it is still symbolic");
    s.explanation_detailed =
        "Working with symbols first shows which quantities the answer depends on and keeps the "
        "algebra separate from the arithmetic. Giac does the rearranging, and its form is checked "
        "against the value the linear solver reaches below.";
    // The schema declares this obligation for kin.rearrange, and a rule that never raises what its
    // schema declares is what VER-016 calls a record nobody can audit.
    s.proof_obligations.push_back(
        {"obl.kinematics.symbolic-isolation",
         "the backend's isolated form has the solutions the symbolic equation had"});
    // Nothing independent checks a rearrangement the backend produced, so the best this can record
    // is corroboration, which is what criterion 4 now accepts and the derivation status reports.
    VerificationRecord v;
    v.method = "backend solve, checked by backend is_zero";
    v.outcome = compared;
    v.strength = agrees ? EvidenceStrength::SymbolicallyEquivalentUnderAssumptions
                        : strength_for(compared, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
    v.detail = observed;
    s.verifications.push_back(std::move(v));
    record(ctx, plan_id, std::move(s), symbolic, arena.binary(Kind::Equals, unknown_symbol, rearranged),
           "Giac: solve(" + print(arena, symbolic) + ", " + name + ")");
    *isolated = rearranged;
    // Only a comparison that ran and came back non-zero is a disagreement. One that never came back
    // leaves the record inconclusive, which outcome_from reads as solved but unchecked.
    *disagreed = compared == VerificationOutcome::Failed;
    return true;
}

KinematicsResult solve_body(Context &ctx, const KinematicsProblem &problem, const Budget &budget,
                            NodeId *model) {
    Arena &arena = ctx.arena;
    Derivation &derivation = ctx.derivation;
    KinematicsResult result;

    const SymbolInfo *target = find_symbol(problem.unknown);
    if (!target)
        return invalid("no such quantity to find: " + problem.unknown +
                       " (the symbols are v0, v, a, t and x)");
    for (size_t i = 0; i < problem.knowns.size(); ++i) {
        const Known &k = problem.knowns[i];
        const SymbolInfo *info = find_symbol(k.symbol);
        if (!info)
            return invalid("no such quantity: " + k.symbol + " (the symbols are v0, v, a, t and x)");
        if (k.symbol == problem.unknown)
            return invalid(k.symbol + " is both known and the unknown");
        if (k.quantity.unit.dimension != info->dimension) {
            result.outcome = KinematicsOutcome::DimensionMismatch;
            result.detail = quantity_text(k) + ": " + info->name + " has dimension " +
                            dimension_text(info->dimension) + ", and " +
                            (k.quantity.unit.text.empty() ? std::string("a bare number")
                                                          : k.quantity.unit.text) +
                            " has " + dimension_text(k.quantity.unit.dimension);
            result.status = DerivationStatus::InvalidInput;
            return result;
        }
    }
    NodeId unknown_symbol = arena.symbol(problem.unknown);
    *model = unknown_symbol;

    // Every known in SI, as the value node the solver will read.
    std::vector<std::string> names;
    std::vector<NodeId> values;
    std::vector<Known> si_knowns;
    std::string conversions;
    for (size_t i = 0; i < problem.knowns.size(); ++i) {
        const Known &k = problem.knowns[i];
        Rational si;
        if (!to_si(k.quantity, &si)) {
            result.detail = "converting " + quantity_text(k) + " to SI does not fit exact arithmetic";
            result.status = DerivationStatus::ResourceLimitReached;
            result.outcome = KinematicsOutcome::ResourceExceeded;
            return result;
        }
        Known converted = k;
        converted.quantity.value = si;
        converted.quantity.precision = precision_product(
            si, k.quantity.value, k.quantity.precision, k.quantity.unit.scale, Precision());
        converted.quantity.unit.text = si_unit_text(k.quantity.unit.dimension);
        converted.quantity.unit.scale.num = 1;
        converted.quantity.unit.scale.den = 1;
        si_knowns.push_back(converted);
        if (!(k.quantity.unit.scale.num == 1 && k.quantity.unit.scale.den == 1)) {
            if (!conversions.empty())
                conversions += ", ";
            conversions += quantity_text(k) + " = " + value_text(converted);
        }
        names.push_back(k.symbol);
        values.push_back(rational_node(arena, si));
    }

    // The planner, section 15.2. Backward chaining from the unknown, deepened a hop at a time, so a
    // problem answered by one equation still is, and one that needs an intermediate quantity now
    // gets it instead of a refusal. Each candidate is offered to the linear solver on a scratch
    // derivation, so a quadratic or a square root is refused by the engine that would have to do
    // the isolation rather than by a table that guesses.
    size_t count;
    const Equation *table = equations(&count);
    Search search(arena, ctx.derivation, table, count, budget, ctx.meter, names, values);
    std::vector<Hop> route;
    const RouteOutcome route_outcome = find_route(search, problem.unknown, &route);
    if (search.halted) {
        ctx.halted = true;
        ctx.cancelled = search.cancelled;
        ctx.halt_detail = search.halt_detail;
        ctx.halt_cost = search.halt_cost;
        return result;
    }
    if (route_outcome == RouteOutcome::NotFound) {
        result.outcome = KinematicsOutcome::NoApplicableEquation;
        result.detail = "no constant-acceleration equation reaches " + problem.unknown +
                        " from what is given";
        for (size_t i = 0; i < search.goal_reasons.size(); ++i)
            result.detail += "; " + search.goal_reasons[i];
        result.status = DerivationStatus::Unsupported;
        if (search.has_answer_candidate) {
            const Hop &candidate = search.answer_candidate;
            const ChildView sides = arena.children(candidate.symbolic);
            Dimension left_dim, right_dim;
            std::string dimension_detail;
            const bool dimensions_ok =
                sides.size() == 2 && dimension_of(arena, sides[0], &left_dim, &dimension_detail) &&
                dimension_of(arena, sides[1], &right_dim, &dimension_detail) && left_dim == right_dim;
            if (dimensions_ok) {
                result.unknown = unknown_symbol;
                result.equation = candidate.symbolic;
                result.substituted = candidate.numeric;
                result.unit_text = si_unit_text(target->dimension);
                result.answer_candidate_detail =
                    "backend candidate " + std::string(candidate.equation->text) +
                    " is fully specified and dimensionally valid, but the local solver refused it: " +
                    search.answer_candidate_reason;
            }
        }
        return result;
    }

    // The last hop is the one that produces the unknown. The earlier ones produce the quantities it
    // needed, and each is recorded in full rather than folded into the answer.
    const Hop &final_hop = route.back();
    const Equation *chosen = final_hop.equation;
    NodeId symbolic = final_hop.symbolic;
    *model = symbolic;
    const bool recursive_contradiction =
        route_outcome == RouteOutcome::Contradiction && final_hop.produces != problem.unknown;

    // What the search passed over. On one hop that is the other three equations and why not, which
    // is what it has always been. On a route it is per hop, labelled with the quantity being looked
    // for, because "already used earlier on this route" means nothing without knowing which hop
    // said it.
    std::vector<std::string> alternatives;
    for (size_t i = 0; i < route.size(); ++i) {
        for (size_t a = 0; a < route[i].alternatives.size(); ++a) {
            alternatives.push_back(route.size() == 1
                                       ? route[i].alternatives[a]
                                       : "for " + route[i].produces + ": " + route[i].alternatives[a]);
        }
    }

    // The route in one line, "x = v0*t + (1/2)*a*t^2 for v0, then v = v0 + a*t for v", which is what
    // a reader needs before the steps make sense.
    std::string route_text;
    for (size_t i = 0; i < route.size(); ++i) {
        if (i)
            route_text += ", then ";
        route_text += std::string(route[i].equation->text) + " for " + route[i].produces;
    }

    PlanPayload plan;
    plan.strategy_id = "physics.kinematics.constant-acceleration";
    plan.selected_strategy = "Constant acceleration in one dimension";
    for (size_t i = 0; i < problem.knowns.size(); ++i) {
        std::string fact = quantity_text(problem.knowns[i]);
        if (const SymbolInfo *given = find_symbol(problem.knowns[i].symbol))
            fact += ", the " + std::string(given->name);
        plan.matched_problem_facts.push_back(fact);
    }
    plan.matched_problem_facts.push_back("find " + problem.unknown + ", the " + target->name);
    plan.alternatives_considered = alternatives;
    if (recursive_contradiction) {
        plan.selection_rationale = "while deriving " + final_hop.produces + " on the way to " +
                                   problem.unknown + ", " + route_text +
                                   " reduces to a contradiction, so the given data have no solution";
    } else if (route.size() == 1) {
        plan.selection_rationale =
            std::string("of the four constant-acceleration equations, ") + chosen->text +
            " is the first that has " + problem.unknown +
            " with every other quantity known and " +
            (final_hop.engine == Engine::Linear
                 ? "is linear in it once the values are in"
                 : final_hop.engine == Engine::SquareRoot
                       ? "leaves its square against constants once the values are in"
                       : "leaves a quadratic in it once the values are in");
    } else {
        // A route worth explaining, because the reader can see that no single equation would do.
        std::string intermediates;
        for (size_t i = 0; i + 1 < route.size(); ++i)
            intermediates += (intermediates.empty() ? "" : ", ") + route[i].produces;
        plan.selection_rationale = "no single constant-acceleration equation reaches " +
                                   problem.unknown + " from what is given, so " + intermediates +
                                   " is found first and then used: " + route_text;
    }
    if (!ctx.giac)
        plan.selection_rationale += "; no symbolic engine is attached, so the rearrangement is "
                                    "shown with the values already in";

    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Find " + problem.unknown + ", the " + target->name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Constant acceleration in one dimension";
    plan_step.claim = ClaimType::NoClaim;
    if (recursive_contradiction) {
        plan_step.explanation_short = "Use " + route_text + " while finding " + problem.unknown +
                                      "; its contradiction means the givens admit no solution";
    } else {
        plan_step.explanation_short =
            route.size() == 1
                ? std::string("Use ") + chosen->text +
                      ": put the values in SI into it and solve for " + problem.unknown
                : std::string("Use ") + route_text + ", each with the values in SI";
    }
    plan_step.assumptions_before.push_back(kConstantAcceleration);
    register_strategy_precondition(
        plan, plan_step, "pre.kinematics.constant-acceleration",
        "acceleration is constant over the interval", "problem-family model validation",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        "the selected equation table is the constant-acceleration family");
    register_strategy_precondition(
        plan, plan_step, "pre.kinematics.one-axis",
        "motion is along one axis with one positive direction", "problem-family model validation",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        "the typed problem uses the one-dimensional kinematics family");
    register_strategy_precondition(
        plan, plan_step, "pre.kinematics.route-applicable",
        "every selected equation contains its target and only quantities known by that hop",
        "exact route search through the solving rules", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed, route_text);
    if (!ctx.meter.step()) {
        ctx.halted = true;
        return result;
    }
    StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    if (!conversions.empty()) {
        Step s = envelope("Convert every quantity to SI units", "kin.convert-units",
                          "Unit conversion",
                          "The equation holds in any one consistent system, and SI is the one used here");
        s.explanation_detailed =
            "A value in km/h or minutes has to be scaled before it can sit beside one in m/s or "
            "seconds. Each conversion multiplies by the exact factor between the units.";
        s.verifications.push_back(verified("rule-local invariant",
                                           "each factor is the unit table's exact scale to SI",
                                           EvidenceStrength::StructurallyValid, true));
        s.proof_obligations.push_back(
            {"obl.kinematics.conversion-preserves-solutions",
             "converting a quantity to SI leaves the equation's solutions alone"});
        record(ctx, plan_id, std::move(s), symbolic, symbolic, conversions);
        if (ctx.halted)
            return result;
    }

    // One pass per hop. Each is a whole solve of its own: the dimensions checked symbolically, the
    // known values put in, and the linear solver's own derivation underneath. The answer joins the
    // known quantities, so the next hop substitutes it the same way it substitutes a given.
    NodeId isolated = kNoNode;
    NodeId numeric = kNoNode;
    SolveResult solved;
    Precision result_precision;
    for (size_t h = 0; h < route.size(); ++h) {
        const Hop &hop = route[h];
        const SymbolInfo *produced = find_symbol(hop.produces);
        NodeId hop_symbol = arena.symbol(hop.produces);

        // Dimensions are checked on the symbolic equation, before any number is in it: the two
        // sides have to agree whatever the values turn out to be.
        Dimension left_dim, right_dim;
        std::string why;
        const bool dimensions_ok =
            dimension_of(arena, arena.children(hop.symbolic)[0], &left_dim, &why) &&
            dimension_of(arena, arena.children(hop.symbolic)[1], &right_dim, &why) &&
            left_dim == right_dim;
        if (dimensions_ok)
            why = dimension_text(left_dim) + " on both sides";
        else if (why.empty())
            why = dimension_text(left_dim) + " against " + dimension_text(right_dim);
        {
            if (!ctx.meter.step()) {
                ctx.halted = true;
                return result;
            }
            Step s;
            s.phase = "check";
            s.goal = "Check the dimensions";
            s.rule_id = "physics.kinematics.check-dimensions";
            s.rule_name = "Dimensional analysis";
            s.claim = ClaimType::SolutionSetPreserved;
            s.explanation_short = "Both sides of the equation must have the same dimension";
            s.explanation_detailed =
                "Each symbol carries a dimension. Multiplying adds exponents, dividing subtracts "
                "them, and a sum only makes sense between equal dimensions. An equation whose sides "
                "disagree is being misused, however the arithmetic comes out.";
            s.proof_obligations.push_back(
                {"obl.kinematics.dimensions-agree",
                 "dimension of the left side equals dimension of the right side"});
            s.verifications.push_back(verified("dimensional analysis", why,
                                               EvidenceStrength::DimensionallyValid,
                                               dimensions_ok));
            CheckPayload c;
            c.target_claim = std::string(hop.equation->text) + " is dimensionally consistent";
            c.check_method = "dimensional analysis of both sides";
            c.expected_relation = "equal dimensions";
            c.observed_result = why;
            derivation.add_check(plan_id, std::move(s), std::move(c));
        }
        if (!dimensions_ok) {
            result.outcome = KinematicsOutcome::DimensionMismatch;
            result.detail = "the equation's sides differ in dimension, so no value is offered";
            result.status = DerivationStatus::VerificationFailed;
            return result;
        }

        bool giac_disagreed = false;
        // A no-solution answer has no scalar value for a backend rearrangement to corroborate, and
        // neither does a hop with more than one root: the rearrangement is a single expression and
        // comparing it against one of a pair would report a disagreement that is not one.
        if (ctx.giac && hop.engine == Engine::Linear && hop.solve_outcome == SolveOutcome::Solved) {
            NodeId hop_isolated = kNoNode;
            giac_rearrangement(ctx, plan_id, hop.symbolic, hop_symbol, names, values, hop.value,
                               &hop_isolated, &giac_disagreed);
            if (ctx.halted)
                return result;
            if (h + 1 == route.size() && hop_isolated != kNoNode)
                isolated = hop_isolated;
            if (giac_disagreed) {
                result.outcome = KinematicsOutcome::VerificationFailed;
                result.detail =
                    "Giac's rearrangement and the linear solver disagree, so no value is offered";
                result.status = DerivationStatus::VerificationFailed;
                return result;
            }
        }

        // The algebra before the arithmetic, and without a backend: the unknown is isolated while
        // the equation is still symbolic, and ALG-007's moves are the steps that say how. Recorded
        // after the backend's own check so a contradicted rearrangement still leaves before any
        // move is on the page, and before the substitution so the shape stays algebra then numbers.
        // ALG-007 undoes one operation at a time, which is degree one, so a hop another engine
        // answered has no isolation of this kind to record and its own rule records the algebra.
        if (hop.engine == Engine::Linear && hop.solve_outcome == SolveOutcome::Solved) {
            const physics::IsolationRecord isolation = physics::record_symbolic_isolation(
                arena, derivation, plan_id, hop.symbolic, hop_symbol, budget, ctx.meter);
            if (isolation.halted) {
                ctx.halted = true;
                return result;
            }
            if (isolation.recorded && h + 1 == route.size() && isolated == kNoNode)
                isolated = isolation.isolated;
        }

        // Substituting against the names known now, not against the problem's givens: on a later
        // hop that set includes what the earlier hops produced.
        numeric = substitute(arena, hop.symbolic, names, values);
        {
            std::string action = "Substitute";
            for (size_t i = 0; i < si_knowns.size(); ++i)
                action += (i ? ", " : " ") + quantity_text(si_knowns[i]);
            Step s = envelope("Substitute the known values", "kin.substitute", "Substitution",
                              "Replace each known symbol by its value in SI units");
            s.explanation_detailed =
                "What remains is an equation in " + hop.produces +
                " alone, with numbers everywhere else, which the " +
                (hop.engine == Engine::Linear ? "linear solver" : "degree-two rule below") +
                " takes from here.";
            s.verifications.push_back(verified(
                "rule-local invariant", "every symbol but " + hop.produces + " has a known value",
                EvidenceStrength::StructurallyValid, true));
            s.proof_obligations.push_back(
                {"obl.kinematics.substitution-preserves-solutions",
                 "putting a known quantity in place of its symbol leaves the equation's solutions "
                 "alone"});
            record(ctx, plan_id, std::move(s), hop.symbolic, numeric, action);
            if (ctx.halted)
                return result;
        }

        // The engine that isolates and evaluates, with its own steps and its own substitution check.
        // Which one that is was settled by the route search, and asking again here would let the
        // record show a rule the search never offered the equation to.
        const size_t solve_start = derivation.mark();
        const Attempt worked =
            isolate(arena, derivation, numeric, hop_symbol, remaining_budget(budget, ctx.meter));
        solved = SolveResult();
        solved.outcome = worked.outcome;
        solved.detail = worked.detail;
        solved.status = worked.status;
        solved.cost = worked.cost;
        RootChoice picked;
        if (worked.outcome == SolveOutcome::Solved) {
            if (worked.engine == Engine::Linear) {
                solved.solution = worked.solutions.empty() ? kNoNode : worked.solutions[0];
            } else {
                std::vector<Rational> roots;
                bool readable = true;
                for (size_t r = 0; r < worked.solutions.size(); ++r) {
                    Rational root;
                    if (!rational_of_node(arena, worked.solutions[r], &root)) {
                        readable = false;
                        break;
                    }
                    roots.push_back(root);
                }
                picked = readable ? choose_physical_root(arena, hop.produces, names, values, roots)
                                  : RootChoice();
                if (picked.decided)
                    solved.solution = worked.solutions[picked.index];
                else {
                    solved.outcome = SolveOutcome::Refused;
                    solved.status = DerivationStatus::Unsupported;
                    solved.detail = picked.why.empty()
                                        ? "a root came back in a form this route cannot read as an "
                                          "exact value"
                                        : picked.why;
                }
            }
        }
        derivation.adopt_roots_since(solve_start, plan_id);
        // The search decided which rule would answer this equation and the record has to be of that
        // rule. A divergence means the two runs saw different things, which is a fault to report
        // rather than a record to publish.
        if (worked.engine != hop.engine) {
            result.outcome = KinematicsOutcome::VerificationFailed;
            result.detail = "the route search and the recorded solve used different rules";
            result.status = DerivationStatus::VerificationFailed;
            return result;
        }
        result.cost.replayed += solved.cost.replayed;
        // Charged to our meter, not the result alone, so the next hop's remainder knows this spend.
        const bool afforded = charge(ctx.meter, solved.cost);
        if (!afforded || solved.outcome == SolveOutcome::Cancelled ||
            solved.outcome == SolveOutcome::ResourceExceeded) {
            ctx.halted = true;
            ctx.cancelled = solved.outcome == SolveOutcome::Cancelled;
            ctx.halt_detail = solved.detail;
            return result;
        }
        if (solved.outcome == SolveOutcome::NoSolution) {
            result.outcome = KinematicsOutcome::NoSolution;
            result.detail = solved.detail;
            result.status = solved.status;
            return result;
        }
        if (solved.outcome != SolveOutcome::Solved) {
            result.outcome = KinematicsOutcome::NoApplicableEquation;
            result.detail = solved.detail;
            result.status = solved.status;
            return result;
        }

        Precision hop_precision;
        Rational solved_value;
        if (!rational_of_node(arena, solved.solution, &solved_value)) {
            result.outcome = KinematicsOutcome::VerificationFailed;
            result.detail = "the answer did not come back as an exact value";
            result.status = DerivationStatus::VerificationFailed;
            return result;
        }
        if (worked.engine == Engine::Linear) {
            std::vector<LinearKnown> precision_knowns;
            precision_knowns.reserve(si_knowns.size());
            for (const Known &known : si_knowns) {
                LinearKnown binding;
                binding.symbol = known.symbol;
                binding.value = known.quantity.value;
                binding.precision = known.quantity.precision;
                precision_knowns.push_back(binding);
            }
            Rational precision_value;
            if (!linear_solution_precision(arena, hop.symbolic, hop_symbol, precision_knowns,
                                           &precision_value, &hop_precision) ||
                !rational_equal(precision_value, solved_value)) {
                result.outcome = KinematicsOutcome::VerificationFailed;
                result.detail = "the exact solve and its precision evaluation disagree";
                result.status = DerivationStatus::VerificationFailed;
                return result;
            }
        } else {
            // A root keeps the figures of the quantities it came from, which is the same fewest
            // rule a product follows. The walk linear_solution_precision does is degree one, so it
            // has nothing to say here and the givens are what the count comes from.
            Precision combined;
            for (const Known &known : si_knowns)
                combined = precision_combine(combined, known.quantity.precision);
            hop_precision = precision_at_digits(solved_value, combined);

            // The algebra gave every value that satisfies the equation. Which one the problem is
            // about is a fact about the motion, so it is recorded as its own move with the reason
            // on it rather than settled by which root happened to be written first.
            if (!ctx.meter.step()) {
                ctx.halted = true;
                return result;
            }
            std::string rejected;
            for (size_t r = 0; r < worked.solutions.size(); ++r) {
                if (r == picked.index)
                    continue;
                rejected += (rejected.empty() ? "" : ", ") + hop.produces + " = " +
                            node_value_text(arena, worked.solutions[r]);
            }
            const std::string kept =
                hop.produces + " = " + node_value_text(arena, worked.solutions[picked.index]);
            Step s;
            s.phase = "solve";
            s.goal = "Choose the root this problem asks for";
            s.rule_id = "kin.select-physical-root";
            s.rule_name = "Physical root selection";
            s.claim = ClaimType::SolutionSetNarrowed;
            s.explanation_short = kept + ", because " + picked.assumption;
            s.explanation_detailed =
                "The equation is satisfied by every root the algebra found. Which of them the "
                "problem is about is a question about the motion rather than about the equation, "
                "so the condition that decides it is stated here and the roots it rules out are "
                "named rather than quietly dropped.";
            s.assumptions_after.push_back(picked.assumption);
            s.proof_obligations.push_back(
                {"obl.kinematics.selected-root-is-admissible",
                 "the value reported is the only root the stated condition allows"});
            s.verifications.push_back(verified(
                "exact comparison against the stated condition",
                rejected.empty() ? "the only root satisfies " + picked.assumption
                                 : "of the roots found, only " + kept + " satisfies " +
                                       picked.assumption,
                EvidenceStrength::StructurallyValid, true));
            TransformationPayload p;
            p.before = numeric;
            p.after = arena.binary(Kind::Equals, hop_symbol, worked.solutions[picked.index]);
            std::string action = "Keep ";
            action += kept;
            if (!rejected.empty()) {
                action += " and reject ";
                action += rejected;
            }
            action += ", since ";
            action += picked.assumption;
            p.concrete_action = std::move(action);
            p.reversible = false;
            derivation.add_transformation(plan_id, std::move(s), std::move(p));
        }
        result_precision = hop_precision;

        // An intermediate answer becomes a known quantity, and is shown as one so the substitution
        // on the next hop is not a value appearing from nowhere.
        if (h + 1 < route.size()) {
            names.push_back(hop.produces);
            values.push_back(solved.solution);
            Rational intermediate;
            Known found;
            found.symbol = hop.produces;
            found.quantity.value = rational_of_node(arena, solved.solution, &intermediate)
                                       ? intermediate
                                       : Rational();
            found.quantity.precision = hop_precision;
            found.quantity.unit.text = produced ? si_unit_text(produced->dimension) : std::string();
            si_knowns.push_back(found);
        }
    }

    Rational value;
    const bool exact_value = rational_of_node(arena, solved.solution, &value);
    result.outcome = KinematicsOutcome::Solved;
    result.value = solved.solution;
    result.value_text = exact_value ? rational_text(value) : print(arena, solved.solution);
    result.precision = result_precision;

    // The one rounding in the whole solve, and it is the last thing that happens. Every step above
    // holds the exact value, so the record shows what was computed as well as what is reported.
    std::string reported;
    // The rounding is shown only when it can be checked. A displayed transformation with no passing
    // verification is what MVP criterion 4 forbids, and the exact value is the right thing to fall
    // back to, since it is what was computed. The check that decides which of the two happens is
    // recorded either way, so an answer that kept its exact form says why.
    const uint16_t reported_digits = result_precision.significant_digits;
    if (exact_value && result_precision.kind == NumberKind::Measured && reported_digits > 0 &&
        precision_rounded_text(value, result_precision, &reported) &&
        reported != result.value_text) {
        const std::string exact_text = result.value_text;
        const HalfPlace checked = precision_rounding_valid(value, reported, result_precision);
        const bool within = checked == HalfPlace::Within;
        const std::string figures = integer_text(reported_digits);
        Step s;
        s.phase = "report";
        s.goal = "Report " + problem.unknown + " to " + figures + " significant figures";
        s.rule_id = "kin.significant-figures";
        s.rule_name = "Significant figures";
        s.claim = ClaimType::NoClaim;
        s.explanation_short =
            "Sums follow their least precise decimal place, while products and quotients follow "
            "their fewest significant figures";
        s.explanation_detailed =
            "A result cannot be more precise than what it was measured from. The value above is "
            "exact and stays exact in the record, since rounding partway through a calculation "
            "loses figures the final rounding cannot get back.";
        s.proof_obligations.push_back(
            {"obl.kinematics.rounding-within-half-place",
             "the reported value is within half a unit in the last place of the exact one"});
        // Three answers, three records. A rounding the comparison measured and rejected is a
        // verdict and reads as Failed, while one whose text it could not read back was never
        // compared and reads as Inconclusive. Collapsing those was what made the old sentence true
        // whichever had happened.
        VerificationRecord half_place;
        half_place.method = "exact comparison against the unrounded value";
        switch (checked) {
            case HalfPlace::Within:
                half_place.outcome = VerificationOutcome::Passed;
                half_place.detail =
                    reported + " is within half a unit in the last place of " + exact_text;
                break;
            case HalfPlace::Outside:
                half_place.outcome = VerificationOutcome::Failed;
                half_place.detail = reported + " is further than half a unit in its last place from " +
                                    exact_text;
                break;
            case HalfPlace::Unreadable:
                half_place.outcome = VerificationOutcome::Inconclusive;
                half_place.detail = reported + " could not be read back as a decimal to compare "
                                               "against " +
                                    exact_text;
                break;
        }
        half_place.strength = strength_for(half_place.outcome, EvidenceStrength::CandidateChecked);
        s.verifications.push_back(half_place);
        if (!ctx.meter.step()) {
            ctx.halted = true;
            return result;
        }
        if (within) {
            TransformationPayload p;
            p.before = solved.solution;
            // The value as written rather than as a fraction: the step says it reports 0.33 and the
            // node it points at should say the same thing. A rounding with no point left is a whole
            // number, and Decimal is for the ones that kept one.
            p.after = reported.find('.') == std::string::npos ? arena.integer(reported)
                                                              : arena.decimal(reported);
            p.concrete_action = "Report " + result.value_text + " as " + reported;
            p.reversible = false;
            // A root of its own rather than a child of the plan, because the steps render in the
            // order they were added: under the plan it would read as rounding the answer before
            // finding it.
            derivation.add_transformation(kNoStep, std::move(s), std::move(p));
            result.value_text = reported;
        } else {
            // The answer keeps its exact form, which is correct but carries more figures than the
            // data supports, so the record holds the attempt rather than leaving the reader to
            // notice a rounding that is not there.
            CheckPayload check;
            check.target_claim =
                "the reported value is within half a unit in the last place of the exact one";
            check.check_method = "read the rounded text back and compare it against the exact value";
            check.expected_relation = "the difference is at most half a unit in the last place";
            check.observed_result =
                checked == HalfPlace::Outside
                    ? reported + " differs from " + exact_text + " by more than half a unit in its "
                                                                 "last place"
                    : reported + " could not be read back as a decimal, so it was never compared "
                                 "against " +
                          exact_text;
            derivation.add_check(kNoStep, std::move(s), std::move(check));
            if (checked == HalfPlace::Outside) {
                // A measured disagreement refuses, like the two verification failures above it in
                // this function and like the four other families. The check stays as the record of
                // what was compared, and nothing is offered beside it. Only the unreadable arm
                // degrades, because there the comparison never ran.
                result.outcome = KinematicsOutcome::VerificationFailed;
                result.detail = reported + " differs from " + exact_text +
                                " by more than half a unit in its last place, so no value is offered";
                result.status = DerivationStatus::VerificationFailed;
                result.value = kNoNode;
                result.value_text.clear();
                return result;
            }
        }
    }
    result.unit_text = si_unit_text(target->dimension);
    result.unknown = unknown_symbol;
    result.equation = symbolic;
    result.isolated = isolated;
    result.substituted = numeric;
    return result;
}

}  // namespace

const char *kinematics_outcome_name(KinematicsOutcome o) {
    switch (o) {
        case KinematicsOutcome::Solved: return "solved";
        case KinematicsOutcome::NoSolution: return "no solution";
        case KinematicsOutcome::InvalidInput: return "invalid input";
        case KinematicsOutcome::NoApplicableEquation: return "no applicable equation";
        case KinematicsOutcome::DimensionMismatch: return "dimension mismatch";
        case KinematicsOutcome::VerificationFailed: return "verification failed";
        case KinematicsOutcome::Refused: return "refused";
        case KinematicsOutcome::Cancelled: return "cancelled";
        case KinematicsOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

bool parse_kinematics(const std::string &text, KinematicsProblem *out, std::string *error) {
    KinematicsProblem p;
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i <= text.size(); ++i) {
        const bool end = i == text.size();
        const char c = end ? ';' : text[i];
        if (c == ';' || c == ',' || c == '\n') {
            const std::string part = trimmed(current);
            if (!part.empty())
                parts.push_back(part);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (parts.empty()) {
        *error = "nothing to work on: give the unknown and the known quantities";
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string &part = parts[i];
        if (starts_with(part, "find ") || starts_with(part, "unknown ") ||
            starts_with(part, "solve for ")) {
            const size_t skip = starts_with(part, "solve for ") ? 10 : part.find(' ') + 1;
            p.unknown = canonical_symbol(trimmed(part.substr(skip)));
            continue;
        }
        size_t eq = part.find('=');
        if (eq == std::string::npos) {
            *error = "could not read \"" + part + "\": expected symbol = value unit, or find symbol";
            return false;
        }
        const std::string symbol = canonical_symbol(trimmed(part.substr(0, eq)));
        const std::string value = trimmed(part.substr(eq + 1));
        if (symbol.empty()) {
            *error = "a value with no symbol before the = in \"" + part + "\"";
            return false;
        }
        if (value == "?") {
            p.unknown = symbol;
            continue;
        }
        Known k;
        k.symbol = symbol;
        std::string why;
        if (!parse_quantity(value, &k.quantity, &why)) {
            *error = "could not read ";
            error->append(symbol).append(" = ").append(value).append(": ").append(why);
            return false;
        }
        for (size_t j = 0; j < p.knowns.size(); ++j) {
            if (p.knowns[j].symbol == symbol) {
                *error = symbol + " is given twice";
                return false;
            }
        }
        p.knowns.push_back(k);
    }
    if (p.unknown.empty()) {
        *error = "no unknown: say find v, or v = ?";
        return false;
    }
    *out = std::move(p);
    return true;
}

KinematicsResult solve_kinematics(Arena &arena, Derivation &derivation,
                                  const KinematicsProblem &problem, const Budget &budget,
                                  Backend *giac) {
    Meter meter(budget);
    Context ctx(arena, derivation, meter, giac);
    const size_t mark = derivation.mark();
    NodeId model = kNoNode;
    KinematicsResult result = solve_body(ctx, problem, budget, &model);

    // STEP-025: a halted solve keeps the run of records that were checked. Nothing is settled on the
    // plan first, unlike the calculus engines: all three kinematics preconditions are proved before
    // the plan is recorded, so a plan that exists is already a checked one.
    if (meter.stopped() || ctx.halted) {
        const bool cancelled = ctx.cancelled || meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        KinematicsResult halted;
        halted.outcome = cancelled ? KinematicsOutcome::Cancelled : KinematicsOutcome::ResourceExceeded;
        // The reason belongs to whichever meter stopped: ours, or the nested solve's, which
        // already put it in its detail.
        halted.detail = meter.stopped() ? halt_name(meter.halt()) : ctx.halt_detail;
        halted.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        halted.cost = meter.cost();
        halted.cost.rewrites += ctx.halt_cost.rewrites;
        halted.cost.steps += ctx.halt_cost.steps;
        halted.cost.backend_calls += ctx.halt_cost.backend_calls;
        halted.cost.replayed += ctx.halt_cost.replayed;
        record_context(derivation, budget, problem, model, halted.status);
        return halted;
    }

    if (result.outcome == KinematicsOutcome::Solved ||
        result.outcome == KinematicsOutcome::NoSolution) {
        result.status = derivation.outcome_from(mark);
    }
    result.cost.rewrites += meter.cost().rewrites;
    result.cost.steps += meter.cost().steps;
    result.cost.backend_calls += meter.cost().backend_calls;
    record_context(derivation, budget, problem, model, result.status);
    return result;
}

const char *coupled_kinematics_outcome_name(CoupledKinematicsOutcome outcome) {
    switch (outcome) {
        case CoupledKinematicsOutcome::Solved: return "solved";
        case CoupledKinematicsOutcome::FirstUnsolved: return "first body unsolved";
        case CoupledKinematicsOutcome::SecondUnsolved: return "second body unsolved";
        case CoupledKinematicsOutcome::InvalidInput: return "invalid input";
        case CoupledKinematicsOutcome::Cancelled: return "cancelled";
        case CoupledKinematicsOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

// Two solves of the same engine, one Derivation, joined by a handover step in between: the first
// body's answer is declared a known of the second body's problem rather than reimplementing the
// join as a third kind of solve.
CoupledKinematicsResult solve_coupled_kinematics(Arena &arena, Derivation &derivation,
                                                 const CoupledKinematicsProblem &problem,
                                                 const Budget &budget, Backend *giac) {
    CoupledKinematicsResult result;
    const SymbolInfo *handover_symbol = find_symbol(problem.coupled_known);
    if (problem.first.name.empty() || problem.second.name.empty() ||
        problem.coupled_known.empty() || handover_symbol == nullptr) {
        result.outcome = CoupledKinematicsOutcome::InvalidInput;
        result.detail =
            "a coupled problem needs both bodies named and a known kinematics symbol to hand over";
        return result;
    }

    Meter meter(budget);
    result.first = solve_kinematics(arena, derivation, problem.first.problem,
                                    remaining_budget(budget, meter), giac);
    const bool first_afforded = charge(meter, result.first.cost);
    if (!first_afforded || meter.stopped()) {
        result.outcome = meter.halt() == Halt::Cancelled ? CoupledKinematicsOutcome::Cancelled
                                                          : CoupledKinematicsOutcome::ResourceExceeded;
        result.detail = "the budget ran out solving " + problem.first.name;
        result.cost = meter.cost();
        return result;
    }
    if (result.first.outcome != KinematicsOutcome::Solved) {
        result.outcome = CoupledKinematicsOutcome::FirstUnsolved;
        result.detail = problem.first.name + "'s derivation did not solve: " + result.first.detail;
        result.cost = meter.cost();
        return result;
    }

    Rational handover_value;
    if (!rational_of_node(arena, result.first.value, &handover_value)) {
        result.outcome = CoupledKinematicsOutcome::FirstUnsolved;
        result.detail = problem.first.name + "'s answer has no exact rational value to hand over";
        result.cost = meter.cost();
        return result;
    }

    if (!meter.step()) {
        result.outcome = CoupledKinematicsOutcome::ResourceExceeded;
        result.detail = "the budget ran out recording the handover";
        result.cost = meter.cost();
        return result;
    }
    {
        Step s;
        s.phase = "solve";
        s.goal = "Carry " + problem.coupled_known + " from " + problem.first.name + " into " +
                problem.second.name;
        s.rule_id = "kin.coupled.handover";
        s.rule_name = "Handover between bodies";
        s.claim = ClaimType::NoClaim;
        s.explanation_short = problem.first.name + "'s solved " + problem.coupled_known +
                              " becomes a known of " + problem.second.name + "'s problem";
        s.explanation_detailed =
            "The two bodies share this quantity over the same interval. " + problem.first.name +
            "'s derivation computed it above; " + problem.second.name +
            "'s derivation below takes it as given rather than solving for it again.";
        s.verifications.push_back(verified(
            "rule-local invariant",
            problem.coupled_known + " = " + result.first.value_text + " " + result.first.unit_text +
                ", computed in " + problem.first.name + "'s derivation",
            EvidenceStrength::StructurallyValid, true));
        TransformationPayload p;
        p.before = result.first.value;
        p.after = result.first.value;
        p.concrete_action = "Declare " + problem.coupled_known + " = " + result.first.value_text +
                            " " + result.first.unit_text + " from " + problem.first.name;
        p.reversible = false;
        derivation.add_transformation(kNoStep, std::move(s), std::move(p));
    }

    KinematicsProblem second = problem.second.problem;
    Known handover;
    handover.symbol = problem.coupled_known;
    handover.quantity.value = handover_value;
    handover.quantity.precision = result.first.precision;
    handover.quantity.unit.text = result.first.unit_text;
    handover.quantity.unit.dimension = handover_symbol->dimension;
    handover.quantity.unit.scale.num = 1;
    handover.quantity.unit.scale.den = 1;
    second.knowns.push_back(handover);

    result.second = solve_kinematics(arena, derivation, second, remaining_budget(budget, meter), giac);
    const bool second_afforded = charge(meter, result.second.cost);
    result.cost = meter.cost();
    if (!second_afforded || meter.stopped()) {
        result.outcome = meter.halt() == Halt::Cancelled ? CoupledKinematicsOutcome::Cancelled
                                                          : CoupledKinematicsOutcome::ResourceExceeded;
        result.detail = "the budget ran out solving " + problem.second.name;
        return result;
    }
    if (result.second.outcome != KinematicsOutcome::Solved) {
        result.outcome = CoupledKinematicsOutcome::SecondUnsolved;
        result.detail = problem.second.name + "'s derivation did not solve: " + result.second.detail;
        return result;
    }

    result.outcome = CoupledKinematicsOutcome::Solved;
    return result;
}

}  // namespace nps
