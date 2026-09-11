#include "property/fuzz.h"

#include <cstdio>

#include "nps/core/ast.h"
#include "nps/core/canonical.h"
#include "nps/steps/derivation.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/linear.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

namespace nps {
namespace {

// Host only, and deliberately not in src/: nothing here has to cross the device toolchain.
//
// A case is generated as a tree and only then spelled, rather than generated as text. That is what
// makes the twin property possible: one tree written two ways has to reach one canonical form, and
// a text generator has no way to know that two strings were meant to say the same thing.

const size_t kFailuresKept = 3;
const size_t kRewriteNodeCap = 512;
const size_t kStepsPerNode = 16;
const size_t kStepSlack = 64;

// splitmix64, written out rather than taken from <random>, because a seed has to name the same case
// on every host and the standard distributions are not specified down to the bit.
class Rng {
  public:
    explicit Rng(uint64_t seed) : state_(seed) {}

    uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    uint32_t below(uint32_t n) { return n == 0 ? 0 : static_cast<uint32_t>(next() % n); }
    uint32_t between(uint32_t lo, uint32_t hi) { return lo + below(hi - lo + 1); }
    bool chance(uint32_t percent) { return below(100) < percent; }

  private:
    uint64_t state_;
};

enum class Shape : uint8_t {
    Number,
    Symbol,
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Neg,
    Call,
    Relation,
};

struct GenNode {
    Shape shape = Shape::Number;
    std::string text;
    std::vector<uint32_t> args;
};

struct Tree {
    std::vector<GenNode> nodes;
    uint32_t root = 0;

    uint32_t add(const GenNode &n) {
        nodes.push_back(n);
        return static_cast<uint32_t>(nodes.size() - 1);
    }
};

const char *const kSymbols[] = {"x", "y", "z", "a", "b", "t", "n", "u_1", "e", "pi"};
const uint32_t kSymbolCount = 10;

const char *const kFunctions[] = {"sin",  "cos", "tan", "exp",    "ln",
                                  "sqrt", "max", "f",   "arctan", "nosuchfn"};
const uint32_t kFunctionCount = 10;

const char *const kRelations[] = {"=", "<", "<=", ">", ">="};
const uint32_t kRelationCount = 5;

// The int64 edges, because that is where exact folding gives up and where a printed constant stops
// reading back as the node it came from.
const char *const kEdgeNumbers[] = {
    "0",
    "1",
    "9223372036854775806",
    "9223372036854775807",
    "9223372036854775808",
    "9223372036854775809",
    "18446744073709551615",
    "18446744073709551616",
    "99999999999999999999999999",
};
const uint32_t kEdgeNumberCount = 9;

const char kJunkBytes[] = {'$',    '@',    '#',  '!',    '~',    '%',    '&',    '|', '?',
                           ';',    ':',    '\\', '"',    '\'',   '[',    ']',    '{', '}',
                           '`',    '\x01', '\t', '\x7f', '\xc3', '\xa9', '\xff', 0};
const uint32_t kJunkByteCount = 26;

std::string digit_run(Rng &rng, uint32_t length) {
    std::string s;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t d = rng.below(10);
        if (i == 0 && length > 1 && d == 0)
            d = 1 + rng.below(9);
        s.push_back(static_cast<char>('0' + d));
    }
    return s;
}

std::string gen_number(Rng &rng, bool edgey) {
    if (edgey && rng.chance(60))
        return kEdgeNumbers[rng.below(kEdgeNumberCount)];
    switch (rng.below(12)) {
        case 0:
        case 1:
        case 2:
        case 3:
            return digit_run(rng, rng.between(1, 2));
        case 4:
            return digit_run(rng, rng.between(3, 25));
        case 5:
            return kEdgeNumbers[rng.below(kEdgeNumberCount)];
        case 6:
        case 7:
            return digit_run(rng, rng.between(1, 3)) + "." + digit_run(rng, rng.between(1, 4));
        case 8:
            return digit_run(rng, 1) + "e" + digit_run(rng, rng.between(1, 2));
        case 9:
            return digit_run(rng, 1) + "." + digit_run(rng, 1) + "e-" + digit_run(rng, 1);
        case 10:
            return digit_run(rng, rng.between(1, 2)) + ".";
        default:
            return "." + digit_run(rng, rng.between(1, 3));
    }
}

struct Builder {
    Tree &tree;
    Rng &rng;
    int budget;
    int depth_cap;
    bool edge_numbers;
    bool allow_relation;

    uint32_t leaf() {
        GenNode n;
        if (rng.chance(45)) {
            n.shape = Shape::Number;
            n.text = gen_number(rng, edge_numbers);
        } else {
            n.shape = Shape::Symbol;
            n.text = kSymbols[rng.below(kSymbolCount)];
        }
        return tree.add(n);
    }

    uint32_t number_leaf(const std::string &text) {
        GenNode n;
        n.shape = Shape::Number;
        n.text = text;
        return tree.add(n);
    }

    uint32_t exponent(int depth) {
        switch (rng.below(8)) {
            case 0:
            case 1:
            case 2:
                return number_leaf(digit_run(rng, rng.between(1, 2)));
            case 3:
            case 4: {
                GenNode n;
                n.shape = Shape::Neg;
                n.args.push_back(number_leaf(digit_run(rng, 1)));
                return tree.add(n);
            }
            case 5:
                return number_leaf(kEdgeNumbers[rng.below(kEdgeNumberCount)]);
            default:
                return build(depth + 1);
        }
    }

    void kids(GenNode &n, int depth, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            n.args.push_back(build(depth + 1));
    }

    uint32_t build(int depth) {
        if (budget <= 1 || depth >= depth_cap)
            return leaf();
        --budget;

        GenNode n;
        uint32_t roll = rng.below(100);
        if (roll < 18)
            return leaf();
        if (roll < 34) {
            n.shape = Shape::Add;
            kids(n, depth, rng.between(2, 4));
        } else if (roll < 44) {
            n.shape = Shape::Sub;
            kids(n, depth, 2);
        } else if (roll < 60) {
            n.shape = Shape::Mul;
            kids(n, depth, rng.between(2, 3));
        } else if (roll < 68) {
            n.shape = Shape::Div;
            kids(n, depth, 2);
        } else if (roll < 79) {
            n.shape = Shape::Pow;
            n.args.push_back(build(depth + 1));
            n.args.push_back(exponent(depth));
        } else if (roll < 86) {
            n.shape = Shape::Neg;
            n.args.push_back(build(depth + 1));
        } else if (roll < 97 || !allow_relation) {
            n.shape = Shape::Call;
            n.text = kFunctions[rng.below(kFunctionCount)];
            kids(n, depth, rng.below(4));
        } else {
            n.shape = Shape::Relation;
            n.text = kRelations[rng.below(kRelationCount)];
            kids(n, depth, 2);
        }
        return tree.add(n);
    }

    uint32_t chain(uint32_t levels) {
        uint32_t id = leaf();
        for (uint32_t i = 0; i < levels; ++i) {
            GenNode n;
            switch (rng.below(4)) {
                case 0:
                    n.shape = Shape::Neg;
                    n.args.push_back(id);
                    break;
                case 1:
                    n.shape = Shape::Call;
                    n.text = kFunctions[rng.below(kFunctionCount)];
                    n.args.push_back(id);
                    break;
                case 2:
                    n.shape = Shape::Add;
                    n.args.push_back(id);
                    n.args.push_back(leaf());
                    break;
                default:
                    n.shape = Shape::Pow;
                    n.args.push_back(id);
                    n.args.push_back(number_leaf(digit_run(rng, 1)));
                    break;
            }
            id = tree.add(n);
        }
        return id;
    }

    uint32_t wide(uint32_t terms) {
        GenNode n;
        n.shape = rng.chance(50) ? Shape::Add : Shape::Mul;
        for (uint32_t i = 0; i < terms; ++i)
            n.args.push_back(leaf());
        return tree.add(n);
    }
};

// Every flag here has to be meaning preserving, so two styles of one tree stay one expression:
// a - b and a + (-b) parse to the same node, and so do a / b and a * b^-1.
struct Style {
    bool reverse = false;
    bool nest_right = false;
    bool sub_as_neg = false;
    bool div_as_pow = false;
    bool paren_all = false;
    bool tight = false;
    bool juxtapose = false;
};

Style pick_style(Rng &rng) {
    Style s;
    s.reverse = rng.chance(35);
    s.nest_right = rng.chance(30);
    s.sub_as_neg = rng.chance(35);
    s.div_as_pow = rng.chance(35);
    s.paren_all = rng.chance(25);
    s.tight = rng.chance(30);
    s.juxtapose = rng.chance(40);
    return s;
}

int precedence(Shape s) {
    switch (s) {
        case Shape::Relation: return 1;
        case Shape::Add:
        case Shape::Sub: return 2;
        case Shape::Mul:
        case Shape::Div: return 3;
        case Shape::Neg: return 4;
        case Shape::Pow: return 5;
        case Shape::Number:
        case Shape::Symbol:
        case Shape::Call: return 6;
    }
    return 6;
}

struct Renderer {
    const Tree &tree;
    Style style;
    std::string out;

    Renderer(const Tree &t, const Style &s) : tree(t), style(s) {}

    void spaced(const char *text) {
        if (style.tight) {
            out += text;
        } else {
            out += ' ';
            out += text;
            out += ' ';
        }
    }

    static bool composite(Shape s) { return s != Shape::Number && s != Shape::Symbol; }

    bool juxtaposable(uint32_t left, uint32_t right) const {
        if (tree.nodes[left].shape != Shape::Number)
            return false;
        const GenNode &r = tree.nodes[right];
        if (r.shape != Shape::Symbol && r.shape != Shape::Call)
            return false;
        // 2 times e is not 2e: the lexer reads the e as the start of an exponent, so .21e+78 is one
        // literal rather than a product and a sum. Every other initial letter is safe.
        return r.text.empty() || (r.text[0] != 'e' && r.text[0] != 'E');
    }

    void render(uint32_t id, int parent_prec, bool right_of_pow) {
        const GenNode &n = tree.nodes[id];
        const int prec = precedence(n.shape);
        // Over parenthesising is always safe, so equal precedence takes brackets rather than a
        // case per associativity. The exception is the exponent, where right association means an
        // equal precedence child needs none and the parser's power() accepts a bare prefix minus.
        bool parens;
        if (style.paren_all)
            parens = composite(n.shape);
        else if (right_of_pow)
            parens = n.shape != Shape::Neg && prec < parent_prec;
        else
            parens = prec <= parent_prec;

        if (parens)
            out += '(';
        emit(n, prec);
        if (parens)
            out += ')';
    }

    void render_list(const GenNode &n, int prec, const char *sym) {
        std::vector<uint32_t> args = n.args;
        if (style.reverse) {
            for (size_t i = 0, j = args.size(); i + 1 < j; ++i, --j) {
                uint32_t t = args[i];
                args[i] = args[j - 1];
                args[j - 1] = t;
            }
        }
        if (style.nest_right && args.size() > 2) {
            render(args[0], prec, false);
            spaced(sym);
            out += '(';
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1)
                    spaced(sym);
                render(args[i], prec, false);
            }
            out += ')';
            return;
        }
        for (size_t i = 0; i < args.size(); ++i) {
            if (i)
                spaced(sym);
            render(args[i], prec, false);
        }
    }

    void emit(const GenNode &n, int prec) {
        switch (n.shape) {
            case Shape::Number:
            case Shape::Symbol:
                out += n.text;
                return;

            case Shape::Neg:
                out += '-';
                render(n.args[0], prec, false);
                return;

            case Shape::Add:
                render_list(n, prec, "+");
                return;

            case Shape::Mul:
                if (style.juxtapose && n.args.size() == 2 && juxtaposable(n.args[0], n.args[1])) {
                    render(n.args[0], prec, false);
                    render(n.args[1], prec, false);
                    return;
                }
                render_list(n, prec, "*");
                return;

            case Shape::Sub:
                render(n.args[0], prec, false);
                if (style.sub_as_neg) {
                    spaced("+");
                    out += '-';
                    render(n.args[1], precedence(Shape::Neg), false);
                } else {
                    spaced("-");
                    render(n.args[1], prec, false);
                }
                return;

            case Shape::Div:
                render(n.args[0], prec, false);
                if (style.div_as_pow) {
                    spaced("*");
                    render(n.args[1], precedence(Shape::Pow), false);
                    out += "^-1";
                } else {
                    spaced("/");
                    render(n.args[1], prec, false);
                }
                return;

            case Shape::Pow:
                render(n.args[0], prec, false);
                out += '^';
                render(n.args[1], prec, true);
                return;

            case Shape::Call:
                out += n.text;
                out += '(';
                for (size_t i = 0; i < n.args.size(); ++i) {
                    if (i) {
                        out += ',';
                        if (!style.tight)
                            out += ' ';
                    }
                    render(n.args[i], 0, false);
                }
                out += ')';
                return;

            case Shape::Relation:
                render(n.args[0], prec, false);
                spaced(n.text.c_str());
                render(n.args[1], prec, false);
                return;
        }
    }
};

std::string render_tree(const Tree &tree, uint32_t root, const Style &style) {
    Renderer r(tree, style);
    r.render(root, 0, false);
    return r.out;
}

// Most cases run against the shipped budgets, and the rest against a tightened one so each refusal
// path is reached often enough that the run demonstrates it rather than assuming it.
Limits pick_limits(Rng &rng) {
    Limits l;
    switch (rng.below(10)) {
        case 0: l.max_depth = rng.between(2, 64); break;
        case 1: l.max_nodes = rng.between(3, 64); break;
        case 2: l.max_input_bytes = rng.between(1, 256); break;
        default: break;
    }
    return l;
}

std::string splice_junk(Rng &rng, const std::string &src) {
    std::string s = src;
    uint32_t count = rng.between(1, 4);
    for (uint32_t i = 0; i < count; ++i) {
        size_t at = s.empty() ? 0 : rng.below(static_cast<uint32_t>(s.size()) + 1);
        char c = kJunkBytes[rng.below(kJunkByteCount)];
        if (rng.chance(30) && !s.empty())
            s[at == s.size() ? s.size() - 1 : at] = c;
        else
            s.insert(at, 1, c);
    }
    return s;
}

std::string truncate_or_bend(Rng &rng, const std::string &src) {
    std::string s = src;
    if (s.empty())
        return s;
    switch (rng.below(5)) {
        case 0:
            s.erase(rng.below(static_cast<uint32_t>(s.size())));
            return s;
        case 1:
            s.erase(rng.below(static_cast<uint32_t>(s.size())), 1);
            return s;
        case 2: {
            size_t at = rng.below(static_cast<uint32_t>(s.size()));
            s.insert(at, 1, s[at]);
            return s;
        }
        case 3: {
            const char *tail[] = {"+", "*", "^", "(", ")", ",", "="};
            s += tail[rng.below(7)];
            return s;
        }
        default:
            s.insert(rng.below(static_cast<uint32_t>(s.size()) + 1), 1, '(');
            return s;
    }
}

void build_case(Rng &rng, FuzzCase &c) {
    Tree tree;
    Builder b{tree, rng, 0, 0, false, true};

    uint32_t profile = rng.below(100);
    uint32_t root;

    if (profile < 44) {
        c.profile = "ordinary";
        b.budget = static_cast<int>(rng.between(3, 40));
        b.depth_cap = static_cast<int>(rng.between(2, 8));
        root = b.build(0);
    } else if (profile < 54) {
        c.profile = "deep";
        b.budget = 4096;
        b.depth_cap = 4096;
        root = b.chain(rng.between(50, 80));
    } else if (profile < 64) {
        c.profile = "wide";
        b.budget = 4096;
        b.depth_cap = 2;
        root = b.wide(rng.between(10, 300));
    } else if (profile < 74) {
        c.profile = "numeric";
        b.edge_numbers = true;
        b.budget = static_cast<int>(rng.between(3, 30));
        b.depth_cap = static_cast<int>(rng.between(2, 6));
        root = b.build(0);
    } else if (profile < 80) {
        c.profile = "chained";
        b.allow_relation = false;
        b.budget = static_cast<int>(rng.between(2, 10));
        b.depth_cap = 3;
        Style style = pick_style(rng);
        std::string s = render_tree(tree, b.build(0), style);
        uint32_t parts = rng.between(2, 3);
        for (uint32_t i = 0; i < parts; ++i) {
            s += style.tight ? "" : " ";
            s += kRelations[rng.below(kRelationCount)];
            s += style.tight ? "" : " ";
            s += render_tree(tree, b.build(0), style);
        }
        c.input = s;
        return;
    } else if (profile < 92) {
        c.profile = "junk";
        b.budget = static_cast<int>(rng.between(2, 20));
        b.depth_cap = static_cast<int>(rng.between(2, 5));
        c.input = splice_junk(rng, render_tree(tree, b.build(0), pick_style(rng)));
        return;
    } else {
        c.profile = "bent";
        b.budget = static_cast<int>(rng.between(2, 20));
        b.depth_cap = static_cast<int>(rng.between(2, 5));
        c.input = truncate_or_bend(rng, render_tree(tree, b.build(0), pick_style(rng)));
        return;
    }

    c.input = render_tree(tree, root, pick_style(rng));
    c.twin = render_tree(tree, root, pick_style(rng));
}

// The canonical properties run under these rather than the case's own, because they are about the
// canonical form and not about the budgets. The budgets are checked separately, against the limits
// the case was actually given.
Limits generous_limits() {
    Limits l;
    l.max_depth = 256;
    l.max_nodes = 1u << 20;
    l.max_input_bytes = 1u << 20;
    return l;
}

std::string number_text(unsigned long long v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%llu", v);
    return std::string(buf);
}

void fail(FuzzReport &report, const FuzzCase &c, Property property, const std::string &detail) {
    ++report.failed;
    // Kept per property rather than in encounter order, so a rare class is still shown when a
    // common one is failing thousands of times.
    if (++report.by_property[static_cast<size_t>(property)] > kFailuresKept)
        return;
    FuzzFailure f;
    f.seed = c.seed;
    f.property = property;
    f.profile = c.profile;
    f.input = c.input;
    f.detail = detail;
    report.failures.push_back(f);
}

void count_status(FuzzReport &report, Status s) {
    switch (s) {
        case Status::Ok: ++report.parsed; return;
        case Status::DepthExceeded: ++report.depth_exceeded; return;
        case Status::SizeExceeded: ++report.size_exceeded; return;
        case Status::InputTooLong: ++report.input_too_long; return;
        case Status::SyntaxError: ++report.syntax_error; return;
    }
}

void check_round_trip(const FuzzCase &c, const Arena &arena, NodeId root, FuzzReport &report) {
    std::string once = print(arena, root);
    // The printed form is fully parenthesised, so it is routinely longer than what was typed. The
    // byte cap is an input policy rather than a property of the printer, so only that one is lifted.
    Limits l = c.limits;
    if (l.max_input_bytes < once.size())
        l.max_input_bytes = once.size();

    Arena second(l);
    ParseResult again = parse(second, once);
    if (!again.ok()) {
        fail(report, c, Property::RoundTrip,
             std::string("the printed form does not reparse (") + status_name(again.status) +
                 ")\n      printed " + fuzz_escape(once));
        return;
    }
    std::string twice = print(second, again.root);
    if (twice != once)
        fail(report, c, Property::RoundTrip,
             "print is not a fixed point\n      once  " + fuzz_escape(once) + "\n      twice " +
                 fuzz_escape(twice));
}

void check_rewrite(const FuzzCase &c, Arena &arena, NodeId root, FuzzReport &report) {
    // Every recorded step prints the subexpression it names, and the derivative rules test whether
    // each factor depends on the variable by walking it, so both are quadratic in the expression.
    // Past this size the run stops being a fuzz loop and becomes a benchmark.
    if (arena.node_count() > kRewriteNodeCap)
        return;

    NodeId unknown = arena.symbol("x");
    if (unknown == kNoNode || arena.failed())
        return;
    ++report.rewrites;

    {
        const size_t nodes = arena.node_count();
        const size_t budget = kStepsPerNode * nodes + kStepSlack;
        Derivation d;
        DiffResult result = differentiate(arena, d, root, unknown);
        if (d.size() > budget)
            fail(report, c, Property::RewriteBudget,
                 "differentiate recorded " + number_text(d.size()) + " steps for " +
                     number_text(nodes) + " nodes");
        if (result.outcome == DiffOutcome::Differentiated &&
            (result.derivative == kNoNode || result.derivative >= arena.node_count()))
            fail(report, c, Property::RewriteResult, "a differentiated outcome carries no derivative");
        if (result.outcome != DiffOutcome::Differentiated && result.derivative != kNoNode)
            fail(report, c, Property::RewriteResult, "a refused derivative carries a result anyway");
    }

    {
        const size_t nodes = arena.node_count();
        const size_t budget = kStepsPerNode * nodes + kStepSlack;
        Derivation d;
        SolveResult result = solve_linear(arena, d, root, unknown);
        if (d.size() > budget)
            fail(report, c, Property::RewriteBudget,
                 "solve_linear recorded " + number_text(d.size()) + " steps for " +
                     number_text(nodes) + " nodes");
        if (result.outcome == SolveOutcome::Solved &&
            (result.solution == kNoNode || result.solution >= arena.node_count()))
            fail(report, c, Property::RewriteResult, "a solved outcome carries no solution");
    }

    if (arena.node_count() > c.limits.max_nodes)
        fail(report, c, Property::NodeBudget,
             "a rewrite pushed the arena to " + number_text(arena.node_count()) +
                 " nodes, past its limit of " + number_text(c.limits.max_nodes));
}

// A completed fold leaves at most one integer constant under an associative node, so two of them is
// the signature of a fold that stopped at the int64 edge, which src/canonical.h documents as a
// boundary rather than a defect. Only the twin property uses this: it excludes the region rather
// than the property, so a case that folds inside an int64 and still fails is a real defect and
// still fails.
bool fold_refused(const Arena &arena, NodeId id) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Add || n.kind == Kind::Mul) {
        size_t constants = 0;
        for (NodeId a : arena.children(n)) {
            const Node &child = arena.at(a);
            if (child.kind == Kind::Integer && child.small_valid)
                ++constants;
        }
        if (constants > 1)
            return true;
    }
    for (NodeId a : arena.children(n)) {
        if (fold_refused(arena, a))
            return true;
    }
    return false;
}

void check_canonical(const FuzzCase &c, FuzzReport &report) {
    Arena arena(generous_limits());
    ParseResult r = parse(arena, c.input);
    if (!r.ok())
        return;

    NodeId first = canonicalize(arena, r.root);
    if (first == kNoNode || arena.failed())
        return;

    // Idempotence is scoped to the fold boundary and the round trip below is not, because a
    // canonical form has to survive being written down and read back whatever its constants are.
    // Neither of the two below is scoped to the fold boundary. Once folding took a maximal prefix of
    // the sorted constants they both held there as well, so excluding the region would only hide a
    // regression. Measured: unscoped, both are clean over 50,000 cases where the twin property is
    // not.
    NodeId settled = canonicalize(arena, first);
    if (settled != first) {
        fail(report, c, Property::CanonicalIdempotent,
             "canonicalising a canonical form changes it\n      once  " +
                 fuzz_escape(print(arena, first)) + "\n      twice " +
                 fuzz_escape(print(arena, settled)));
        return;
    }

    std::string text = print(arena, first);
    ParseResult again = parse(arena, text);
    if (!again.ok()) {
        fail(report, c, Property::CanonicalRoundTrip,
             std::string("a canonical form does not reparse (") + status_name(again.status) +
                 ")\n      canonical " + fuzz_escape(text));
        return;
    }
    NodeId reparsed = canonicalize(arena, again.root);
    if (reparsed != first)
        fail(report, c, Property::CanonicalRoundTrip,
             "the canonical form changes across a print and reparse\n      once  " +
                 fuzz_escape(text) + "\n      twice " + fuzz_escape(print(arena, reparsed)));
}

void check_twin(const FuzzCase &c, FuzzReport &report) {
    if (c.twin.empty())
        return;

    Arena arena(generous_limits());
    ParseResult a = parse(arena, c.input);
    ParseResult b = parse(arena, c.twin);
    if (a.ok() != b.ok()) {
        fail(report, c, Property::TwinParse,
             std::string("one spelling parses and the other does not\n      twin  ") +
                 fuzz_escape(c.twin) + "\n      input " +
                 (a.ok() ? "ok" : status_name(a.status)) + ", twin " +
                 (b.ok() ? "ok" : status_name(b.status)));
        return;
    }
    if (!a.ok())
        return;

    NodeId ca = canonicalize(arena, a.root);
    NodeId cb = canonicalize(arena, b.root);
    if (ca == kNoNode || cb == kNoNode || arena.failed())
        return;
    // The one property the boundary still reaches. Two spellings of one sum can fold different
    // prefixes when a child folded before its parent overflowed, so the grouping depends on how the
    // sum was written even though nothing wrong was computed.
    if (fold_refused(arena, ca) || fold_refused(arena, cb)) {
        ++report.fold_limited;
        return;
    }

    ++report.twins;
    if (ca != cb)
        fail(report, c, Property::TwinCanonical,
             "two spellings of one expression canonicalise differently\n      twin  " +
                 fuzz_escape(c.twin) + "\n      once  " + fuzz_escape(print(arena, ca)) +
                 "\n      twice " + fuzz_escape(print(arena, cb)));
}

void check_case(const FuzzCase &c, FuzzReport &report) {
    Arena arena(c.limits);
    ParseResult r = parse(arena, c.input);
    count_status(report, r.status);

    if ((r.status == Status::Ok) != (r.root != kNoNode))
        fail(report, c, Property::ParseStatus,
             std::string("status ") + status_name(r.status) + " does not match the root");
    if (r.status != Status::Ok && r.message.empty())
        fail(report, c, Property::ParseStatus, "a refusal carries no message");
    if (r.offset > c.input.size())
        fail(report, c, Property::ParseStatus,
             "the failure offset " + number_text(r.offset) + " is past the end of the input");
    if (arena.node_count() > c.limits.max_nodes)
        fail(report, c, Property::NodeBudget,
             "the parse left " + number_text(arena.node_count()) +
                 " nodes, past the limit of " + number_text(c.limits.max_nodes));

    if (r.ok()) {
        check_round_trip(c, arena, r.root, report);
        check_rewrite(c, arena, r.root, report);
    }
    check_canonical(c, report);
    check_twin(c, report);
}

uint64_t case_seed(uint64_t seed, size_t index) {
    uint64_t z = seed + (static_cast<uint64_t>(index) + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

}  // namespace

const char *property_name(Property p) {
    switch (p) {
        case Property::ParseStatus: return "parse-status";
        case Property::NodeBudget: return "node-budget";
        case Property::RoundTrip: return "round-trip";
        case Property::RewriteBudget: return "rewrite-budget";
        case Property::RewriteResult: return "rewrite-result";
        case Property::CanonicalIdempotent: return "canonical-idempotent";
        case Property::CanonicalRoundTrip: return "canonical-round-trip";
        case Property::TwinParse: return "twin-parse";
        case Property::TwinCanonical: return "twin-canonical";
    }
    return "unknown";
}

FuzzCase fuzz_case(uint64_t seed) {
    Rng rng(seed);
    FuzzCase c;
    c.seed = seed;
    c.limits = pick_limits(rng);
    build_case(rng, c);
    return c;
}

std::string fuzz_escape(const std::string &s) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == '\\') {
            out += "\\\\";
            continue;
        }
        if (ch < 0x20 || ch >= 0x7F) {
            out += "\\x";
            out.push_back(kHex[ch >> 4]);
            out.push_back(kHex[ch & 0x0F]);
            continue;
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string fuzz_describe(const FuzzFailure &f) {
    std::string out =
        std::string(property_name(f.property)) + ", " + f.profile + ", seed " + number_text(f.seed);
    out += "\n      input " + fuzz_escape(f.input);
    out += "\n      " + f.detail;
    return out;
}

FuzzReport fuzz_run(uint64_t seed, size_t cases, bool progress) {
    FuzzReport report;
    report.seed = seed;
    for (size_t i = 0; i < cases; ++i) {
        FuzzCase c = fuzz_case(case_seed(seed, i));
        check_case(c, report);
        ++report.cases;
        if (progress && (i + 1) % 1000 == 0) {
            fprintf(stderr, "  %zu/%zu, %zu failures\n", i + 1, cases, report.failed);
            fflush(stderr);
        }
    }
    return report;
}

void run_fuzz_tests(TestSink &t) {
    FuzzReport r = fuzz_run(1, 400);
    for (size_t i = 0; i < r.failures.size(); ++i)
        t.check(false, "fuzz: " + fuzz_describe(r.failures[i]));
    t.check(r.failed == 0, "the short fuzz pass finds nothing");

    // A generator that stopped producing a shape would make the run faster and the suite weaker, so
    // the shapes are asserted rather than assumed.
    t.check(r.parsed > 0, "the short fuzz pass parses something");
    t.check(r.syntax_error > 0, "the short fuzz pass reaches a syntax error");
    t.check(r.depth_exceeded > 0, "the short fuzz pass reaches the depth limit");
    t.check(r.size_exceeded > 0, "the short fuzz pass reaches the node limit");
    t.check(r.input_too_long > 0, "the short fuzz pass reaches the input limit");
    t.check(r.twins > 0, "the short fuzz pass compares two spellings of one expression");
    t.check(r.rewrites > 0, "the short fuzz pass runs the rewrite path");
}

}  // namespace nps
