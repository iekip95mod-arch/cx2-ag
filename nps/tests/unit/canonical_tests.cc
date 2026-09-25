#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "nps/core/canonical.h"
#include "nps/core/evaluate.h"
#include "nps/core/matrix.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

std::string canon(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    if (!r.ok())
        return std::string("<") + status_name(r.status) + ">";
    NodeId c = canonicalize(arena, r.root);
    if (c == kNoNode)
        return "<canonicalize failed>";
    return print(arena, c);
}

bool same(const std::string &a, const std::string &b) {
    Arena arena;
    ParseResult ra = parse(arena, a);
    ParseResult rb = parse(arena, b);
    if (!ra.ok() || !rb.ok())
        return false;
    NodeId ca = canonicalize(arena, ra.root);
    NodeId cb = canonicalize(arena, rb.root);
    return ca != kNoNode && ca == cb;
}

std::string exact_form(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    if (!r.ok())
        return std::string("<") + status_name(r.status) + ">";
    NodeId e = exactify(arena, r.root);
    if (e == kNoNode)
        return "<refused>";
    return print(arena, e);
}

// Every decimal-mode answer takes this route: exactify, do the arithmetic, write the coefficient
// back. Testing the three together is what catches a pair that each work and do not compose.
std::string round_trip(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    if (!r.ok())
        return std::string("<") + status_name(r.status) + ">";
    NodeId e = exactify(arena, r.root);
    if (e == kNoNode)
        return "<refused>";
    NodeId c = canonicalize(arena, e);
    if (c == kNoNode)
        return "<canonicalize failed>";
    NodeId d = decimalize(arena, c);
    if (d == kNoNode)
        return "<decimalize failed>";
    return print(arena, d);
}

bool deep_canonical_shapes() {
    for (Kind kind : {Kind::Neg, Kind::Add, Kind::Mul, Kind::Call, Kind::List, Kind::Pow}) {
        Limits limits;
        limits.max_depth = 4096;
        limits.max_nodes = 30000;
        Arena arena(limits);
        const NodeId x = arena.symbol("x");
        const NodeId one = arena.integer("1");
        const NodeId zero = arena.integer("0");
        NodeId nested = x;
        for (size_t depth = 0; depth < 2048; ++depth) {
            if (kind == Kind::Call)
                nested = arena.call("f", {nested});
            else if (kind == Kind::List)
                nested = arena.list({nested});
            else if (kind == Kind::Neg)
                nested = arena.unary(kind, nested);
            else
                nested = arena.binary(kind, nested, kind == Kind::Add ? zero : one);
        }
        if (nested == kNoNode || arena.failed())
            return false;
        const NodeId normalized = canonicalize(arena, nested);
        const NodeId expected = kind == Kind::Call || kind == Kind::List ? nested : x;
        if (normalized != expected || arena.failed() || canonicalize(arena, normalized) != normalized)
            return false;
        if (kind == Kind::Call || kind == Kind::List) {
            NodeId other = arena.symbol("y");
            for (size_t depth = 0; depth < 2048; ++depth)
                other = kind == Kind::Call ? arena.call("f", {other}) : arena.list({other});
            if (!canonical_less(arena, normalized, other) || canonical_less(arena, other, normalized))
                return false;
        }
    }
    return true;
}

bool bounded_expression_scans() {
    Arena arena;
    const NodeId x = arena.symbol("x");
    const NodeId absent = arena.symbol("y");
    NodeId shared = x;
    for (size_t depth = 0; depth < 40; ++depth)
        shared = arena.binary(Kind::Add, shared, shared);
    size_t visits = 0;
    if (arena.any_node(shared, [&visits](NodeId) { ++visits; return false; }) || visits != 41)
        return false;
    visits = 0;
    if (!arena.any_node(shared, [&visits](NodeId) { ++visits; return true; }) || visits != 1)
        return false;
    for (NodeId invalid : {kNoNode, static_cast<NodeId>(arena.node_count())}) {
        if (arena.any_node(invalid, [&visits](NodeId) { ++visits; return true; }) ||
            visits != 1 || depends_on(arena, invalid, invalid) ||
            has_undefined_form(arena, invalid) || has_decimal(arena, invalid) ||
            has_decimal_exponent(arena, invalid))
            return false;
    }
    if (arena.failed() || depends_on(arena, shared, absent) ||
        has_decimal_exponent(arena, arena.binary(Kind::Pow, shared, shared)) ||
        has_undefined_form(arena, shared) || has_decimal(arena, shared) ||
        has_decimal_exponent(arena, shared) || !depends_on(arena, shared, x))
        return false;

    const NodeId decimal = arena.decimal("0.5");
    const NodeId integer = arena.integer("2");
    const NodeId decimal_tree = arena.call("f", {decimal});
    const NodeId shared_exponent = arena.binary(Kind::Pow, decimal_tree, decimal_tree);
    if (!has_decimal(arena, arena.binary(Kind::Add, shared, decimal)) ||
        has_decimal_exponent(arena, arena.nary(Kind::Pow, {decimal})) ||
        !has_decimal_exponent(arena, shared_exponent) ||
        has_decimal_exponent(arena, arena.binary(Kind::Pow, decimal_tree, integer)) ||
        !has_decimal_exponent(arena, arena.binary(Kind::Add, decimal_tree, shared_exponent)) ||
        !has_decimal_exponent(arena, arena.binary(Kind::Add, shared_exponent, decimal_tree)))
        return false;

    const NodeId undefined = arena.binary(Kind::Pow, arena.integer("0"), arena.integer("-1"));
    return has_undefined_form(arena, arena.binary(Kind::Add, shared, undefined)) &&
           has_undefined_form(arena, arena.binary(Kind::Add, undefined, shared));
}

bool numeric_conversion_shapes(bool deep) {
    for (bool to_decimal : {false, true}) {
        Limits limits;
        limits.max_depth = 4096;
        limits.max_nodes = 30000;
        Arena arena(limits);
        const auto convert = [&](NodeId id) {
            return to_decimal ? decimalize(arena, id) : exactify(arena, id);
        };
        for (bool changes : {false, true}) {
            const NodeId half = arena.decimal("0.5");
            const NodeId fraction = arena.binary(Kind::Mul, arena.integer("1"),
                arena.binary(Kind::Pow, arena.integer("2"), arena.integer("-1")));
            NodeId expression = changes ? (to_decimal ? fraction : half) : arena.symbol("x");
            NodeId expected = changes ? (to_decimal ? half : fraction) : expression;
            for (size_t depth = 0; depth < (deep ? 2048 : 40); ++depth) {
                expression = deep ? arena.call("f", {expression})
                                  : arena.binary(Kind::Add, expression, expression);
                expected = deep ? arena.call("f", {expected})
                                : arena.binary(Kind::Add, expected, expected);
            }
            if (expression == kNoNode || expected == kNoNode ||
                convert(expression) != expected || arena.failed() || convert(expected) != expected)
                return false;
        }
    }
    return true;
}

bool holds_a_decimal(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    return r.ok() && has_decimal(arena, r.root);
}

}  // namespace

void run_canonical_tests(TestSink &t) {
    for (const auto &sample : std::vector<std::pair<std::string, std::string>>{
             {"2^-1", "(1 / 2)"}, {"1*2^-1", "(1 / 2)"},
             {"x*y^-1", "(x / y)"}, {"x^-1*y", "((1 / x) * y)"},
             {"(x*y)*z^-1", "((x * y) / z)"},
             {"x*(y*z)^-1", "(x / (y * z))"},
             {"x^-1*y^-1", "((1 / x) / y)"},
             {"(-2)^-1", "(1 / (-2))"}, {"x^-2", "(x^(-2))"},
             {"0*0^-1", "(0 / 0)"}, {"x^-1*x", "((1 / x) * x)"},
             {"[1/2,1/3]", "[(1 / 2), (1 / 3)]"},
             {"sin(x/2)", "sin((x / 2))"}, {"x/2=y/3", "((x / 2) = (y / 3))"},
             {"0.50*x", "(0.50 * x)"}}) {
        Arena arena;
        const auto parsed = parse(arena, sample.first);
        t.check(parsed.ok(), "math display fixture parses");
        const auto stored = print(arena, parsed.root);
        const auto nodes = arena.node_count();
        const auto display = print_math(arena, parsed.root);
        t.equal(display, sample.second, "math display uses division without cancelling or reordering factors");
        t.check(arena.node_count() == nodes && print(arena, parsed.root) == stored,
                "math display leaves the arena and stored expression unchanged");
        const auto reparsed = parse(arena, display);
        Rational source_value, displayed_value;
        const bool same_constant = reparsed.ok() && evaluate_rational(arena, parsed.root, {}, &source_value) &&
                                   evaluate_rational(arena, reparsed.root, {}, &displayed_value) &&
                                   source_value.num == displayed_value.num && source_value.den == displayed_value.den;
        t.check(reparsed.ok() && (same_constant || canonicalize(arena, parsed.root) == canonicalize(arena, reparsed.root)),
                "display division retains canonical structure or the exact constant value");
    }
    {
        Arena arena;
        const NodeId inverse = arena.binary(Kind::Pow, arena.symbol("z"), arena.integer("-1"));
        const NodeId product = arena.nary(Kind::Mul, {arena.symbol("x"), arena.symbol("y"), inverse});
        t.equal(print_math(arena, product), "((x * y) / z)", "n-ary products keep the numerator grouped");
    }
    {
        Limits limits;
        limits.max_depth = 4096;
        limits.max_nodes = 10000;
        Arena arena(limits);
        NodeId nested = arena.symbol("x");
        const NodeId minus_one = arena.integer("-1");
        for (size_t depth = 0; depth < 2048; ++depth)
            nested = arena.binary(Kind::Pow, nested, minus_one);
        t.check(nested != kNoNode && print_math(arena, nested).size() == 2048 * 6 + 1,
                "deep reciprocal display uses the existing iterative printer");
    }
    for (const auto &fixture : {std::pair{"sqrt(0)+x", "x"},
                               std::pair{"sqrt(81/100)+x", "9/10+x"},
                               std::pair{"sqrt(sqrt(81))+x", "3+x"},
                               std::pair{"exp(sin(0))+ln(1)+cos(0)+x", "2+x"}}) {
        t.check(canon(fixture.first) == canon(fixture.second),
                "canonical forms simplify exact calls within symbolic expressions: " + std::string(fixture.first));
    }
    {
        Arena arena;
        const NodeId one = arena.integer("1");
        for (size_t arity : {0u, 2u, 3u}) {
            int64_t integer = 99;
            const NodeId malformed = arena.nary(Kind::Neg, std::vector<NodeId>(arity, one));
            t.check(!small_integer(arena, malformed, &integer) && integer == 99 && !arena.failed(),
                    "the integer reader refuses malformed negation without changing its output");
        }
        int64_t integer = 99;
        t.check(small_integer(arena, arena.unary(Kind::Neg, one), &integer) && integer == -1,
                "the integer reader still accepts a unary negative literal");
    }
    for (bool to_decimal : {false, true}) {
        Arena arena;
        const auto convert = [&](NodeId id) {
            return to_decimal ? decimalize(arena, id) : exactify(arena, id);
        };
        for (NodeId invalid : {kNoNode, static_cast<NodeId>(arena.node_count())})
            t.check(convert(invalid) == kNoNode, "numeric conversion refuses invalid roots");
        const NodeId numeral = to_decimal ? arena.integer("2") : arena.decimal("0.5");
        for (Kind kind : {Kind::Neg, Kind::Pow, Kind::Equals, Kind::Assign, Kind::Approx,
                          Kind::Identity, Kind::Less, Kind::LessEqual, Kind::Greater,
                          Kind::GreaterEqual}) {
            for (size_t arity : {0u, 1u, 2u, 3u}) {
                if (arity == (kind == Kind::Neg ? 1u : 2u))
                    continue;
                const NodeId malformed = arena.nary(kind, std::vector<NodeId>(arity, numeral));
                t.check(convert(malformed) == kNoNode && !arena.failed(),
                        "numeric conversion refuses malformed operator arity");
                t.check(canonicalize(arena, malformed) == kNoNode && !arena.failed(),
                        "canonicalization refuses malformed operator arity");
                for (NodeId nested : {arena.call("f", {malformed}), arena.list({malformed}),
                                      arena.binary(Kind::Mul, numeral, malformed)}) {
                    t.check(convert(nested) == kNoNode && canonicalize(arena, nested) == kNoNode &&
                                !arena.failed(),
                            "numeric transformations refuse malformed descendants before rewriting");
                }
            }
        }
    }
    t.check(numeric_conversion_shapes(false), "numeric conversions preserve shared expression structure");
    t.check(bounded_expression_scans(), "shared expression scans finish and retain their predicates");
#if defined(__unix__) || defined(__APPLE__)
    pthread_attr_t attributes;
    const int initialized = pthread_attr_init(&attributes);
    t.check(initialized == 0, "the canonical depth check initializes its worker attributes");
    if (initialized == 0) {
        const int sized = pthread_attr_setstacksize(&attributes, 128 * 1024);
        t.check(sized == 0, "the canonical depth check selects a bounded native stack");
        if (sized == 0) {
            bool passed = false;
            pthread_t worker;
            const int started = pthread_create(&worker, &attributes, [](void *context) -> void * {
                *static_cast<bool *>(context) = deep_canonical_shapes() && numeric_conversion_shapes(true);
                return nullptr;
            }, &passed);
            t.check(started == 0, "the canonical depth check starts its worker");
            if (started == 0) {
                const int joined = pthread_join(worker, nullptr);
                t.check(joined == 0 && passed,
                        "accepted canonical depth works on a bounded native stack");
            }
        }
        t.check(pthread_attr_destroy(&attributes) == 0,
                "the canonical depth check releases its worker attributes");
    }
#else
    t.check(deep_canonical_shapes(), "accepted canonical depth preserves every expression shape");
#endif
    for (const char *source : {"[]", "[1]", "[3,1,2]", "[[1,2],[3,4]]",
                               "[[-1/2,0],[2,3/4]]", "ref([[2,4],[0,1]])"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        t.check(parsed.ok(), std::string("ordered list syntax parses: ") + source);
        if (!parsed.ok())
            continue;
        const std::string printed = print(arena, parsed.root);
        const ParseResult reparsed = parse(arena, printed);
        t.check(reparsed.ok() && reparsed.root == parsed.root,
                std::string("list display spelling preserves structure: ") + source);
        const std::string translated = print_giac(arena, parsed.root);
        const ParseResult backend = parse(arena, translated);
        t.check(backend.ok() &&
                    canonicalize(arena, backend.root) == canonicalize(arena, parsed.root),
                std::string("list backend spelling preserves structure: ") + source);
    }
    for (const char *source : {"[x:=1]", "[1,x~=1]", "[[x==1]]", "f([x:=1])"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        t.check(parsed.ok(), std::string("a relation inside a list retains its syntax: ") + source);
        t.equal(print_giac(arena, parsed.root), "",
                std::string("untranslatable list cells refuse the complete backend expression: ") + source);
    }
    for (bool call : {false, true}) {
        for (bool has_prefix : {false, true}) {
            Arena arena;
            if (has_prefix)
                arena.integer("3");
            const NodeId unbuilt = static_cast<NodeId>(arena.node_count());
            const NodeId refused = call ? arena.call("f", {unbuilt}) : arena.list({unbuilt});
            t.check(refused == kNoNode && arena.status() == Status::SyntaxError &&
                        arena.integer("4") == kNoNode,
                    "unbuilt children are refused before either shared composite constructor reads them");
        }
    }
    t.equal(canon("[[2+1,1],[4,0]]"), "[[3, 1], [4, 0]]",
            "canonical matrices preserve row and column order");
    t.equal(canon("[[2]]"), "[[2]]", "canonical matrices retain singleton dimensions");
    t.check(static_cast<unsigned>(Kind::Integer) == 0 &&
                static_cast<unsigned>(Kind::GreaterEqual) == 15 &&
                static_cast<unsigned>(Kind::Invalid) == 16 &&
                static_cast<unsigned>(Kind::List) == 17,
            "adding lists preserves existing node kind numbers");
    for (const char *source : {"[", "[1", "[1,]", "[,1]", "[1,,2]", "[1;2]",
                               "[[1],2", "[1]]", "[] []", "2[1]"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        t.check(parsed.status == Status::SyntaxError && parsed.root == kNoNode,
                std::string("malformed list syntax is refused: ") + source);
    }
    {
        struct Case {
            const char *source;
            const char *expected;
        };
        const Case cases[] = {
            {"0*[1,2]", "(0 * [1, 2])"},
            {"1*[2+1]", "(1 * [3])"},
            {"[1,2]^0", "([1, 2]^0)"},
            {"1^[2]", "(1^[2])"},
            {"-[2+1]", "(-[3])"},
            {"0*[1]+x", "((0 * [1]) + x)"},
            {"f([[2+1]])", "f([[3]])"},
            {"0*[1]=0", "((0 * [1]) = 0)"},
            {"[[2+1]]+[[3]]", "([[3]] + [[3]])"},
        };
        for (const Case &item : cases) {
            t.equal(canon(item.source), item.expected,
                    std::string("scalar identities preserve unsupported list operations: ") + item.source);
        }
        t.equal(exact_form("[[0.5,2.0],[0.25,3]]"),
                "[[(1 * (2^-1)), 2], [(1 * (4^-1)), 3]]",
                "exactifying matrix cells preserves all rows and columns");
        t.equal(round_trip("[[0.5],[0.25]]"), "[[0.5], [0.25]]",
                "decimalizing matrix cells preserves singleton row widths");
    }
    {
        Arena arena;
        const NodeId matrix = parse(arena, "[[2,1,3],[4,0,5]]").root;
        const auto view = MatrixView::from(arena, matrix);
        t.check(view && view->root() == matrix && view->rows() == 2 && view->columns() == 3,
                "matrix dimensions come from the ordered row lists");
        if (view) {
            t.check(view->cell(0, 1) == arena.integer("1") &&
                        view->cell(1, 0) == arena.integer("4"),
                    "matrix cells retain their existing arena identities");
            t.check(view->cell(2, 0) == kNoNode && view->cell(0, 3) == kNoNode,
                    "matrix access outside either dimension is refused");
            const size_t before = arena.node_count();
            for (size_t index = 0; index < 1100; ++index)
                arena.call("f", {arena.integer(std::to_string(index))});
            t.check(arena.node_count() > before && view->cell(1, 2) == arena.integer("5"),
                    "matrix views survive growth of the arena node and child storage");
        }
        const NodeId singleton = parse(arena, "[[2]]").root;
        const auto one = MatrixView::from(arena, singleton);
        t.check(one && one->rows() == 1 && one->columns() == 1 &&
                    one->cell(0, 0) == arena.integer("2"),
                "a one by one matrix does not collapse into a scalar");
        for (const char *source : {"2", "[]", "[[]]", "[1,2]", "[[1],[2,3]]",
                                   "[[1],2]", "[[[1]]]", "[[f([1])]]", "[[0*[1]]]"}) {
            const NodeId shape = parse(arena, source).root;
            t.check(shape != kNoNode && !MatrixView::from(arena, shape),
                    std::string("a nonrectangular or nested collection is not a matrix view: ") + source);
        }
        const NodeId invalid_cell = arena.nary(Kind::Invalid, {});
        const NodeId invalid_matrix = arena.list({arena.list({invalid_cell})});
        t.check(!MatrixView::from(arena, invalid_matrix),
                "an invalid node cannot serve as a matrix cell");
        t.check(!MatrixView::from(arena, kNoNode) &&
                    !MatrixView::from(arena, static_cast<NodeId>(arena.node_count())),
                "matrix views refuse absent and unbuilt nodes");
    }
    {
        Arena arena;
        const NodeId scalar = arena.call("f", {arena.integer("1")});
        const NodeId list = arena.list({scalar});
        const NodeId nested = arena.binary(Kind::Equals, arena.integer("0"),
                                          arena.call("g", {list}));
        t.check(!contains_list(arena, scalar) && contains_list(arena, list) &&
                    contains_list(arena, nested) && !contains_list(arena, kNoNode),
                "the shared list predicate sees collections beneath scalar syntax");
        NodeId shared = scalar;
        for (size_t index = 0; index < 40; ++index)
            shared = arena.call("pair", {shared, shared});
        t.check(!contains_list(arena, shared),
                "shared scalar subtrees remain scalar without expanding the DAG");
        const NodeId nested_shared = arena.call("pair", {shared, list});
        t.check(contains_list(arena, nested_shared),
                "a list in a shared DAG is retained by the owning node fact");
        t.check(arena.list({scalar}) == list &&
                    arena.list({scalar, arena.integer("2")}) !=
                        arena.list({arena.integer("2"), scalar}),
                "list interning preserves element order and repeated identities");
    }
    {
        Limits limits;
        limits.max_depth = 4;
        Arena arena(limits);
        t.check(parse(arena, "[[[1]]]").ok(), "nested lists obey the shared depth boundary");
        const ParseResult deep = parse(arena, "[[[[1]]]]");
        t.check(deep.status == Status::DepthExceeded && arena.failed() &&
                    arena.list({}) == kNoNode,
                "excessively nested lists preserve sticky arena failure");
        Limits width;
        width.max_nodes = 3;
        Arena bounded(width);
        const ParseResult large = parse(bounded, "[1,2,3]");
        t.check(large.status == Status::SizeExceeded && large.root == kNoNode &&
                    !MatrixView::from(bounded, 0),
                "list construction uses the shared node limit");
    }
    {
        // A decimal literal is a fraction over a power of ten, so reading it as one is exact.
        t.equal(exact_form("0.5"), "(1 * (2^-1))", "a half is read as the rational it names");
        t.equal(exact_form("2.0"), "2", "a decimal that is a whole number loses the point");
        t.equal(exact_form("-14.0"), "(-14)", "and a negative one keeps its sign");
        t.equal(exact_form("0.000250"), "(1 * (4000^-1))",
                "a small measurement is read to its lowest terms");
        t.equal(exact_form("sin(0.5*x)"), "sin(((1 * (2^-1)) * x))",
                "a decimal buried inside a call is reached");
        t.equal(exact_form("2*x + 1"), "((2 * x) + 1)",
                "an expression with no decimal in it comes back as it went in");
        t.equal(exact_form("1.2345678901234567890123"), "<refused>",
                "a literal wider than the rational is refused rather than narrowed");

        t.check(holds_a_decimal("sin(0.5*x)"), "a decimal anywhere in the tree is found");
        t.check(!holds_a_decimal("sin(x/2)"), "and a written fraction is not one");

        t.equal(round_trip("0.5*x"), "(0.5 * x)", "a coefficient survives the round trip");
        t.equal(round_trip("sin(0.5*x)"), "sin((0.5 * x))", "and so does one inside a call");
        t.equal(round_trip("0.25*x + 3"), "(3 + (0.25 * x))",
                "the whole number beside it stays a whole number");
        t.equal(round_trip("0.000250"), "0.00025",
                "a trailing zero is significant-figure information the rational never held");
        // Where the two directions genuinely do not compose, and where they must not pretend to.
        t.equal(round_trip("x/3"), "(x * (3^-1))",
                "a third has no decimal spelling, so it is left as a third");
        t.equal(round_trip("2*x + 1"), "(1 + (2 * x))",
                "and an expression with no fraction in it is untouched");
    }
    {
        struct Case {
            const char *text;
            Decision nonzero;
            Decision positive;
            Decision nonnegative;
        };
        const Case cases[] = {
            {"0e0", Decision::Fails, Decision::Fails, Decision::Holds},
            {"0e3", Decision::Fails, Decision::Fails, Decision::Holds},
            {"0.00E-500", Decision::Fails, Decision::Fails, Decision::Holds},
            {"-0.0", Decision::Fails, Decision::Fails, Decision::Holds},
            {"-0e+500", Decision::Fails, Decision::Fails, Decision::Holds},
            {"-0.000E-999999999999999999", Decision::Fails, Decision::Fails, Decision::Holds},
            {"1e-999999999999999999", Decision::Holds, Decision::Holds, Decision::Holds},
            {"-1e999999999999999999", Decision::Holds, Decision::Fails, Decision::Fails},
        };
        for (const Case &c : cases) {
            Arena arena;
            const NodeId literal = arena.decimal(c.text);
            t.check(decide(arena, literal, Condition::NonZero) == c.nonzero &&
                        decide(arena, literal, Condition::Positive) == c.positive &&
                        decide(arena, literal, Condition::NonNegative) == c.nonnegative &&
                        !arena.failed(),
                    std::string(c.text) + " has the sign of its mantissa");
        }
    }
    {
        Arena arena;
        for (const char *text : {"", "+", "-", ".", "0e", "0e+", "0e--1", "0.0.0",
                                 "1x", "1e2x"}) {
            t.check(literal_sign(arena, arena.decimal(text)) == Sign::Unknown,
                    std::string("malformed numeral has no sign: ") + text);
        }
        for (const char *text : {"-9223372036854775808", "-999999999999999999999999"}) {
            const NodeId integer = arena.integer(text);
            t.check(literal_sign(arena, integer) == Sign::Negative &&
                        literal_sign(arena, arena.unary(Kind::Neg, integer)) == Sign::Positive,
                    std::string(text) + " keeps its sign without narrowing or negation overflow");
        }
        const NodeId x = arena.symbol("x");
        t.check(literal_sign(arena, x) == Sign::Unknown &&
                    literal_sign(arena, arena.unary(Kind::Neg, x)) == Sign::Unknown &&
                    literal_sign(arena, arena.nary(Kind::Neg, {})) == Sign::Unknown &&
                    literal_sign(arena, arena.nary(Kind::Neg, {x, x})) == Sign::Unknown &&
                    literal_sign(arena, kNoNode) == Sign::Unknown,
                "symbolic and malformed operands do not establish a literal sign");
    }
    t.equal(canon("x"), "x", "an atom is already canonical");
    t.check(same("π/4", "pi/4"), "Giac's Unicode pi parses as the exact pi symbol");
    t.check(same("2π", "2*pi"), "a coefficient next to Unicode pi keeps implicit multiplication");
    // A power of a power, which is how a reciprocal of a rational coefficient arrives. Without
    // these the integrator computed a correct antiderivative and then refused it, because its own
    // derivative check compared two spellings of the same number.
    t.equal(canon("(2^-1)^-1"), "2", "a reciprocal of a reciprocal folds back to the number");
    t.equal(canon("(x^2)^3"), "(x^6)", "a symbolic power of a power multiplies its exponents");
    t.equal(canon("(x^2)^-1"), "(x^-2)", "a negative outer exponent multiplies too");
    t.check(same("1/x^2", "x^-2"), "the two spellings of a reciprocal square reach one form");
    for (const auto &forms : {std::pair{"(-x)^-1", "-x^-1"},
                             std::pair{"(-2*x)^-1", "-(2*x)^-1"},
                             std::pair{"(-(x+1))^-1", "-(x+1)^-1"},
                             std::pair{"(-x)^-2", "x^-2"},
                             std::pair{"(-x)^3", "-x^3"},
                             std::pair{"(-x)^2", "x^2"},
                             std::pair{"(-x)^-9223372036854775808", "x^-9223372036854775808"}})
        t.check(same(forms.first, forms.second), "integer powers normalize the sign of a product base");
    t.check(!same("(-x)^(1/2)", "x^(1/2)"), "fractional powers preserve their real branch");
    t.check(canon("(-9223372036854775808*x)^-1") != "<canonicalize failed>",
            "a coefficient whose magnitude does not fit stays representable");
    t.equal(canon("(-1)^-1"), "-1", "an odd negative power of minus one is minus one");
    t.equal(canon("(-1)^-2"), "1", "an even negative power of minus one is one");
    t.equal(canon("(2^-1)^-3"), "8", "a rational base folds through a wider exponent");
    // A division by zero is not a value, and before this the engines handed one back as a checked
    // answer: 1/0 differentiated to 0 with status SolvedAndVerified, and 2/0 + x gave 1.
    {
        struct Case {
            const char *src;
            bool undefined;
        };
        const Case cases[] = {
            {"x/0", true},   {"1/0", true},      {"0^-1", true},    {"x/(1-1)", true},
            {"2/0 + x", true}, {"sin(1/0)", true}, {"x/2", false},  {"x^-1", false},
            {"0^0", false},  {"x^0", false},     {"0/x", false},    {"0*x^-1", false},
            {"1/(0e3)", true}, {"1/(0.00E-500)", true}, {"1/(-0.0)", true},
            {"0.0^(-1.5)", true}, {"0^-(-1)", false}, {"0^(-(-2))", false},
            {"0^-x", false}, {"0^(-0.0)", false}, {"0e3^(-1)", true},
        };
        for (const Case &c : cases) {
            Arena arena;
            ParseResult p = parse(arena, c.src);
            t.check(p.ok(), std::string(c.src) + " parses");
            if (!p.ok())
                continue;
            t.check(divides_by_zero(arena, p.root) == c.undefined,
                    std::string(c.src) + (c.undefined ? " divides by zero" : " does not"));
        }
    }
    {
        // What a form needs before it has a value. A literal settles the condition and says nothing,
        // a symbol leaves it open and is recorded, and a literal that settles it false is not a
        // restriction at all: nobody can meet 0 > 0, so the expression has no value.
        struct Case {
            const char *src;
            const char *wanted;
            bool unmeetable;
        };
        const Case cases[] = {
            {"ln(x)", "x > 0", false},
            {"ln(a)", "a > 0", false},
            {"ln(2*x)", "(2 * x) > 0", false},
            {"ln(5)", "", false},
            {"ln(0)", "", true},
            {"ln(-2)", "", true},
            {"ln(0e3)", "", true}, {"log(0.00E-500)", "", true},
            {"ln(-0.0)", "", true}, {"ln(-1.5)", "", true},
            {"ln(-1e999999999999999999)", "", true},
            {"ln(1e-999999999999999999)", "", false},
            {"sqrt(x)", "x >= 0", false},
            {"sqrt(4)", "", false},
            {"sqrt(-1)", "", true},
            {"sqrt(-0.0)", "", false}, {"sqrt(-1.5)", "", true},
            {"x/a", "a is not zero", false},
            {"x/(2*a)", "(2 * a) is not zero", false},
            {"x/2", "", false},
            {"x/(-2)", "", false},
            {"x/(0.5)", "", false},
            {"x/(1-1)", "", true},
            // The same divisor written with a symbol. Numbers folded here long before symbols did,
            // so this pair is the whole of the gap: both are zero, and only one used to be seen.
            {"x/(y-y)", "", true},
            {"x/(2*y - y - y)", "", true},
            {"x/(sin(y) - sin(y))", "", true},
            // A zero factor buried in a product or under a power, which a literal test could not see.
            {"x/((y-y)*z)", "", true},
            {"x/(z*(y-y))", "", true},
            {"x/((y-y)^2)", "", true},
            {"x/(y^2)", "(y^2) is not zero", false},
            // Not zero, and must stay allowed: the coefficients gather to one rather than to none.
            // The condition is stated on the sum as written, because nothing here collects it to y.
            {"x/(2*y - y)", "((2 * y) + (-y)) is not zero", false},
            // Positive says everything non-zero says, so the weaker one is not repeated beside it.
            {"ln(x)/x", "x > 0", false},
            {"sin(x)", "", false},
            // A reciprocal is never zero, so the outer condition is settled and only the base's is
            // worth stating. A product of reciprocals is settled the same way, factor by factor.
            {"1/(1/k)", "k is not zero", false},
            {"1/((a^-1)*(b^-1))", "a is not zero; b is not zero", false},
            // The lens equation, and parallel resistors. A sum of reciprocals is zero at a = -b, so
            // this one is genuinely open and must survive whatever the reciprocal rule settles.
            {"1/(1/a + 1/b)",
             "((1 * (a^(-1))) + (1 * (b^(-1)))) is not zero; a is not zero; b is not zero", false},
            // Neither factor is settled, so the product is not either and the condition stands.
            {"1/(a*b)", "(a * b) is not zero", false},
        };
        for (const Case &c : cases) {
            Arena arena;
            ParseResult p = parse(arena, c.src);
            t.check(p.ok(), std::string(c.src) + " parses");
            if (!p.ok())
                continue;
            std::string joined;
            for (const Restriction &r : restrictions_of(arena, p.root))
                joined += (joined.empty() ? "" : "; ") + restriction_text(arena, r);
            t.equal(joined, c.wanted, std::string(c.src) + " needs what its form does not settle");
            t.check(has_unmeetable_condition(arena, p.root) == c.unmeetable,
                    std::string(c.src) + (c.unmeetable ? " has no value" : " has a value"));
        }
    }
    t.equal(canon("1 + 2"), "3", "integer addition folds");
    t.equal(canon("2 * 3"), "6", "integer multiplication folds");
    t.equal(canon("x + 0"), "x", "an additive identity drops out");
    t.equal(canon("x * 1"), "x", "a multiplicative identity drops out");
    t.equal(canon("x * 0"), "0", "a zero factor collapses the product");
    t.equal(canon("(a + b) + c"), "(a + b + c)", "nested sums flatten into one");
    t.equal(canon("a + (b + c)"), "(a + b + c)", "and flatten the same way from the right");
    t.equal(canon("-x"), "(-1 * x)", "negation becomes a factor of minus one");
    t.equal(canon("2 - 3"), "-1", "a subtraction of larger from smaller folds to a negative");
    t.equal(canon("1 + x + 2"), "(3 + x)", "constants gather at the front of a sum");
    t.equal(canon("2*x + 3*x"), "(5 * x)", "whole-number coefficients of one term gather");
    t.equal(canon("x + x"), "(2 * x)", "an implicit unit coefficient gathers too");
    t.equal(canon("2*x - x"), "x", "like terms cancel down to one copy");
    t.equal(canon("2*x*y + 3*y*x"), "(5 * x * y)",
            "products with the same symbolic factors gather independent of factor order");
    t.equal(canon("pi/3 + pi/3"), "(2 * pi * (3^-1))",
            "rational coefficients of one symbolic term gather exactly");
    t.equal(canon("1/3 + 1/3"), "(2 * (3^-1))",
            "pure rational terms gather the same way a symbolic one does");
    t.equal(canon("1/3 + 1/6"), "(2^-1)", "unlike denominators gather over a common one");
    t.equal(canon("1 + 1/3"), "(4 * (3^-1))", "a whole number gathers with a fraction");
    t.equal(canon("1/3 + 2/3"), "1", "rational terms that total one gather to the integer");
    t.equal(canon("1/3 - 1/3"), "0", "rational terms that cancel leave nothing behind");
    t.equal(canon("pi*(-2/3 - 2/3) + 16*pi/3"), "(4 * pi)",
            "a symbolic factor over a folded rational sum gathers with its like term");
    t.equal(canon("0.5 + 0.5"), "(0.5 + 0.5)",
            "decimal constants keep their numeric-mode spelling and stay separate");
    t.equal(canon("99999999999999999999 + 99999999999999999999"),
            "(2 * 99999999999999999999)",
            "an integer too large to fold gathers as a like term of its own");
    t.equal(canon("20000000000000000000 + 20000000000000000000 + 20000000000000000000"),
            "(3 * 20000000000000000000)",
            "three copies of an unfoldable integer gather into one coefficient");
    t.equal(canon("x + 20000000000000000000 + 20000000000000000000"),
            "(x + (2 * 20000000000000000000))",
            "an unfoldable integer gathers beside a symbolic term");
    t.check(same("20000000000000000000 + 20000000000000000000", "2*20000000000000000000"),
            "a doubled unfoldable integer stays canonically identical to its written double");
    t.equal(canon("x*20000000000000000000 + x*20000000000000000000"),
            "((20000000000000000000 * x) + (20000000000000000000 * x))",
            "an unfoldable integer inside a product keeps that product out of the collection");
    t.equal(canon("0.5*x + 0.5*x"), "((0.5 * x) + (0.5 * x))",
            "decimal coefficients keep their numeric-mode spelling and stay separate");
    t.equal(canon("2*x + 3*y"), "((2 * x) + (3 * y))",
            "terms with different symbolic factors stay separate");
    t.equal(canon("x + (x^2)"), "(x + (x^2))",
            "different powers are not treated as like terms");
    for (const char *source : {"0^-1 - 0^-1", "x^-1 - x^-1", "log(x) - log(x)",
                               "sqrt(-1) - sqrt(-1)", "x^(1/2) - x^(1/2)",
                               "tan(x) - tan(x)", "asin(x) - asin(x)"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        const NodeId once = parsed.ok() ? canonicalize(arena, parsed.root) : kNoNode;
        const NodeId twice = once != kNoNode ? canonicalize(arena, once) : kNoNode;
        t.check(once != kNoNode && once == twice && arena.at(once).kind == Kind::Add,
                std::string(source) + " keeps its domain-sensitive factor at a fixed point");
    }
    {
        Arena arena;
        const ParseResult reciprocal = parse(arena, "0^-1 - 0^-1");
        const NodeId canonical = reciprocal.ok() ? canonicalize(arena, reciprocal.root) : kNoNode;
        t.check(canonical != kNoNode && divides_by_zero(arena, canonical),
                "cancelling undefined reciprocals remain visibly undefined");

        const ParseResult logarithm = parse(arena, "log(x) - log(x)");
        const NodeId log_canonical = logarithm.ok() ? canonicalize(arena, logarithm.root) : kNoNode;
        t.check(log_canonical != kNoNode && !restrictions_of(arena, log_canonical).empty(),
                "cancelling logarithms retain their positive-argument restriction");

        const ParseResult root = parse(arena, "sqrt(-1) - sqrt(-1)");
        const NodeId root_canonical = root.ok() ? canonicalize(arena, root.root) : kNoNode;
        t.check(root_canonical != kNoNode && has_unmeetable_condition(arena, root_canonical),
                "cancelling invalid square roots remain outside the real domain");
    }
    for (const char *source : {"x - x", "sin(x) - sin(x)"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        const NodeId once = parsed.ok() ? canonicalize(arena, parsed.root) : kNoNode;
        const NodeId twice = once != kNoNode ? canonicalize(arena, once) : kNoNode;
        t.check(once != kNoNode && once == twice && print(arena, once) == "0",
                std::string(source) + " cancels as a total expression at a fixed point");
    }
    for (const char *source : {"x/3 + x/6", "x/2 + x/3 - x/3"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, source);
        const NodeId once = parsed.ok() ? canonicalize(arena, parsed.root) : kNoNode;
        const NodeId twice = once != kNoNode ? canonicalize(arena, once) : kNoNode;
        t.check(once != kNoNode && once == twice,
                std::string(source) + " reaches its canonical form in one pass");
    }
    t.equal(canon("x/3 + x/6"), "(x * (2^-1))",
            "a unit numerator is omitted from a gathered rational coefficient");
    t.equal(canon("x/2 + x/3 - x/3"), "(x * (2^-1))",
            "cancelled thirds leave the existing half coefficient");
    t.equal(canon("9223372036854775807*x + x"),
            "(x + (9223372036854775807 * x))",
            "an unrepresentable coefficient sum preserves its terms");
    t.equal(canon("9223372036854775807*x + 2*x - 2*x"),
            "(9223372036854775807 * x)",
            "a wide intermediate coefficient sum can still cancel to a representable total");
    for (size_t bound : {size_t{3}, size_t{4}}) {
        Limits limits;
        limits.max_nodes = bound;
        Arena arena(limits);
        const ParseResult parsed = parse(arena, "x+x");
        const NodeId canonical = parsed.ok() ? canonicalize(arena, parsed.root) : kNoNode;
        if (bound == 3) {
            t.check(canonical == kNoNode && arena.status() == Status::SizeExceeded,
                    "like-term collection reports the node limit it reaches");
        } else {
            t.check(canonical != kNoNode && print(arena, canonical) == "(2 * x)",
                    "one additional node is enough to collect two symbols");
        }
    }
    t.equal(canon("-9223372036854775807 + -2 + 2"), "-9223372036854775807",
            "an overflowing prefix folds when the full exact sum fits");
    t.equal(canon("9223372036854775807 + 2"), "(2 + 9223372036854775807)",
            "a failed exact sum leaves the maximal-prefix fallback intact");
    t.equal(canon("2.5 + 1.25"), "(1.25 + 2.5)", "decimals sort by value, not text length");
    t.equal(canon("0.5 + 0.125 + 0.25"), "(0.125 + 0.25 + 0.5)", "shorter-valued decimals sort first");
    t.equal(canon("007"), "7", "leading zeros go, because the text is the interning key");
    t.equal(canon("x^007"), "(x^7)", "wherever the integer sits");
    t.equal(canon("000"), "0", "and a zero keeps one digit");
    t.check(same("0009223372036854775808", "9223372036854775808"),
            "an integer past int64 loses its leading zeros too");

    // Rational coefficients. The integrator checks its answer by differentiating it, and x^3/3
    // differentiated is 3 * x^2 * 3^-1, which has to be the same form as x^2.
    t.equal(canon("x^1"), "x", "a first power is the base");
    t.equal(canon("2 * x^1"), "(2 * x)", "wherever it sits");
    t.equal(canon("1^5"), "1", "one to an integer power is one");
    t.equal(canon("6/4"), "(3 * (2^-1))", "a constant fraction reduces to lowest terms");
    t.equal(canon("x/1"), "x", "a denominator of one drops out");
    t.equal(canon("3 * x^2 * 3^-1"), "(x^2)", "a coefficient cancels its own reciprocal");
    t.equal(canon("4 * x / 6"), "(2 * x * (3^-1))", "and a partial cancellation reduces");
    t.equal(canon("1/(-2)"), "(-1 * (2^-1))", "a negative denominator gives its sign up");
    t.equal(canon("x / 2^2"), "(x * (4^-1))", "a power in the denominator is its value");
    t.equal(canon("0 / 7"), "0", "a zero numerator is zero");
    t.equal(canon("0 / 0"), "(0 * (0^-1))", "an undefined zero reciprocal is not erased");
    t.equal(canon("x / 0"), "(x * (0^-1))", "a zero denominator is left alone");
    t.check(same("2*x/4", "x/2"), "two spellings of a half agree");
    t.check(same("3*x^2*3^-1", "x^2"), "the derivative of a cubic over three is the square");
    t.check(same("x^3 * 3^-1 * 3", "3 * 3^-1 * x^3"), "however the factors are ordered");
    t.check(!same("x/2", "x/3"), "different fractions stay different");
    t.check(same("6/4", "3/2"), "and equal fractions agree whatever their spelling");
    // The full fold must see cancellation beyond an overflowing prefix.
    t.equal(canon("9223372036854775807 * 2 / 2"), "9223372036854775807",
            "a reciprocal that cancels a folded factor lets the leftover fold");
    t.equal(canon("9223372036854775807 * 2 / 3"), "(2 * 9223372036854775807 * (3^-1))",
            "and one that does not leaves the product unfolded");
    t.equal(canon("3999999999 * 4000000000 / 2"), "7999999998000000000",
            "an overflowing cross product folds when its reduced exact value fits");
    t.equal(canon("2 * 9223372036854775807 / 3 / 9223372036854775807"),
            "(2 * (3^-1))", "whole-product cancellation survives an overflowing prefix");
    t.equal(canon("4 * 9223372036854775807 / 3"),
            "(4 * 9223372036854775807 * (3^-1))",
            "a failed exact rational fold leaves the prefix fallback intact");

    // Constants whose exact product does not fit int64. The fold has to be a function of the
    // multiset alone: canonicalizing its own output, or the printed chain read back as nested
    // pairs, must land on the same form. Found by the fuzzer at 100000 cases, seeds 3, 2 and 1234.
    {
        // A prefix fold took the huge negative first, flipped its sign against the -1, and the
        // small constants it had blocked folded on the next pass.
        const std::string once = canon("-(-8399309064073782222 * (f * 79 * 5) * 4131)");
        t.equal(canon(once), once, "canonicalizing a canonical overflowing product changes nothing");
        t.equal(once, "(1631745 * 8399309064073782222 * f)",
                "and the small constants fold whatever blocked them");
    }
    {
        // The printer writes a chain flat and the parser reads it left nested, so a fold that
        // depends on nesting does not survive its own print.
        const std::string once = canon("-((9223372036854775806 * 44) * -9223372036854775808 * -a)");
        t.equal(canon(once), once, "an overflowing product survives a print and reparse");
        t.equal(once, "(-9223372036854775808 * 44 * 9223372036854775806 * a)",
                "with the pair of minus ones folded away");
    }
    {
        // A zero reciprocal sorted first and stopped the prefix, so 9^-2 folded to 81^-1 only when
        // an inner grouping had folded it before the zero arrived.
        t.check(same("0^-1 * x * 9^-2", "0^-1 * (x * 9^-2)"),
                "a reciprocal folds the same way whether or not an undefined one sits beside it");
        t.equal(canon("0^-1 * x * 9^-2"), "(x * (0^-1) * (81^-1))",
                "and the undefined reciprocal stays written while the other folds");
    }

    // Huge powers must stay bounded while unit-magnitude bases remain exact.
    t.equal(canon("0^9223372036854775807"), "0", "zero to a vast power folds without a loop");
    t.equal(canon("1^9223372036854775807"), "1", "and so does one");
    t.equal(canon("(-1)^9223372036854775807"), "-1", "and minus one, by parity");
    t.equal(canon("(-1)^9223372036854775806"), "1", "on both sides of it");
    t.equal(canon("2^62"), "4611686018427387904", "a base that can overflow still folds while it fits");
    t.equal(canon("2^64"), "(2^64)", "and is left written when it cannot");
    t.equal(canon("x * (-1)^-9223372036854775808"), "x",
            "the signed-minimum reciprocal exponent uses its unsigned magnitude");
    t.equal(canon("x * 2^-9223372036854775808"),
            "(x * (2^-9223372036854775808))",
            "an unrepresentable signed-minimum power is refused without unbounded work");

    {
        // A full arena returns kNoNode for the folded constant. The fold has to drop it rather than
        // sort it, since the comparator would index the node table at kNoNode.
        Limits tight;
        tight.max_nodes = 5;
        Arena arena(tight);
        ParseResult r = parse(arena, "x + 2 + 3");
        t.check(r.ok(), "the sum parses within the node budget");
        NodeId c = canonicalize(arena, r.root);
        t.check(c == kNoNode, "canonicalize fails cleanly when the fold exhausts the arena");
    }

    t.check(same("a + b", "b + a"), "addition is order insensitive");
    t.check(same("a * b", "b * a"), "multiplication is order insensitive");
    t.check(same("(a + b) + c", "a + (b + c)"), "addition reassociates");
    t.check(same("2 * x", "x * 2"), "a constant factor sorts to the front either way");
    t.check(same("x + x", "x + x"), "the trivial case still holds");
    t.check(!same("a - b", "b - a"), "subtraction is not order insensitive");
    t.check(!same("a / b", "b / a"), "division is not order insensitive");
    t.check(same("a - b", "a + (-b)"), "subtraction and adding a negation agree");
    t.check(same("a / b", "a * b^-1"), "division and a negative power agree");

    {
        // The whole point of interning plus a canonical form: equality is one comparison, not a
        // walk, and it has to survive the two expressions being written differently.
        Arena arena;
        ParseResult a = parse(arena, "sin(x)*x^2 + 1");
        ParseResult b = parse(arena, "1 + x^2*sin(x)");
        NodeId ca = canonicalize(arena, a.root);
        NodeId cb = canonicalize(arena, b.root);
        t.check(ca == cb, "two spellings of one expression reach the same node id");
    }

    {
        // Folding must stop rather than wrap. A literal cannot overflow an int64 on its own, since
        // small_valid caps at 18 digits, so it takes a sum of ten of the largest that fits.
        Arena arena;
        const char *big = "999999999999999999";
        std::string src = big;
        for (int i = 1; i < 10; ++i)
            src += std::string(" + ") + big;
        ParseResult r = parse(arena, src);
        NodeId c = canonicalize(arena, r.root);
        std::string out = print(arena, c);
        // The leftover sorts with the other numbers rather than trailing the folded total, because
        // folding now runs in sorted order so that the answer cannot depend on the input order.
        t.equal(out, "(999999999999999999 + 8999999999999999991)",
                "a fold that would overflow keeps the last term instead of wrapping");
    }

    {
        // The guard used to multiply and then check the result, which is undefined for a product
        // that overflows. At -O2 the compiler is entitled to assume it did not happen and delete
        // the check, and it did: this folded to a wrapped negative number.
        Arena arena;
        ParseResult r = parse(arena, "9223372036854775807 * 2");
        std::string out = print(arena, canonicalize(arena, r.root));
        t.equal(out, "(2 * 9223372036854775807)", "a product past an int64 is left unfolded");
    }

    {
        Arena arena;
        ParseResult r = parse(arena, "3037000500 * 3037000500");
        std::string out = print(arena, canonicalize(arena, r.root));
        t.equal(out, "(3037000500 * 3037000500)",
                "and so is a square that just crosses the boundary");
    }

    {
        // The property the 18 digit cap used to break: a long sum has to reach the same canonical
        // form however it was bracketed. It did not, because a folded 19 digit result stopped being
        // foldable and the leftovers depended on the shape of the tree.
        Arena arena;
        const char *big = "999999999999999999";
        std::string left = big, right = big;
        for (int i = 1; i < 6; ++i) {
            left = "(" + left + " + " + big + ")";
            right = "(" + std::string(big) + " + " + right + ")";
        }
        ParseResult a = parse(arena, left);
        ParseResult b = parse(arena, right);
        NodeId ca = canonicalize(arena, a.root);
        NodeId cb = canonicalize(arena, b.root);
        t.check(ca != kNoNode && ca == cb,
                "a long sum canonicalises the same however it is bracketed");
    }

    {
        Arena arena;
        ParseResult r = parse(arena, "x^2*sin(x)");
        NodeId c = canonicalize(arena, r.root);
        ParseResult again = parse(arena, print(arena, c));
        NodeId c2 = canonicalize(arena, again.root);
        t.check(c == c2, "canonical form survives a print and reparse");
    }

    // kNoNode from canonicalize is two different answers, and only the arena says which. A shape the
    // canonicalizer does not handle leaves the arena clean, so a caller that reads kNoNode as a limit
    // reports a size that was never reached.
    {
        Arena arena;
        ParseResult r = parse(arena, "x");
        const NodeId malformed = arena.nary(Kind::Pow, {r.root});
        t.check(malformed != kNoNode, "a one-child power is held by the arena");
        t.check(canonicalize(arena, malformed) == kNoNode,
                "a shape outside the node grammar has no canonical form");
        t.check(!arena.failed(),
                "that refusal leaves the arena unfailed, so it is not a limit");
        t.check(canonical_refusal(arena) == CanonicalRefusal::Unsupported,
                "the refusal reads as unsupported rather than as a limit");
    }

    // The other answer, for the same return value. Here the arena really did run out, and it records
    // a resource status, which is what tells the two apart.
    {
        // The bound that parses the input and then starves canonicalization is a property of the
        // fixture rather than a number worth writing down, so it is searched for.
        bool starved = false;
        for (size_t bound = 4; bound < 64 && !starved; ++bound) {
            Limits limits;
            limits.max_nodes = bound;
            Arena arena(limits);
            ParseResult r = parse(arena, "-(a + b + c) * (d + e + f)");
            if (!r.ok())
                continue;
            if (canonicalize(arena, r.root) != kNoNode)
                continue;
            starved = arena.failed() && resource_status(arena.status());
        }
        t.check(starved,
                "an arena that runs out during canonical form records a resource status");
    }

    // The two answers read back through the function the bridge asks, which is the thing that has to
    // tell them apart. A mapping that always said limit would pass every check above.
    {
        Arena clean;
        ParseResult r = parse(clean, "x");
        t.check(canonicalize(clean, clean.nary(Kind::Pow, {r.root})) == kNoNode,
                "the unsupported shape still refuses");
        t.check(canonical_refusal(clean) == CanonicalRefusal::Unsupported,
                "an unfailed arena names an unsupported form");

        Limits limits;
        limits.max_nodes = 8;
        Arena starved(limits);
        // Distinct spellings, because the arena interns and one repeated node never costs a second.
        for (size_t i = 0; i < limits.max_nodes + 4 && !starved.failed(); ++i)
            starved.integer(std::to_string(i));
        t.check(starved.failed() && resource_status(starved.status()),
                "the starved arena records a resource status");
        t.check(canonical_refusal(starved) == CanonicalRefusal::ResourceLimit,
                "a failed arena names the limit it hit");
    }

    {
        Arena arena;
        ParseResult r = parse(arena, "(0 * x) * (y * 0^-1)");
        NodeId c = canonicalize(arena, r.root);
        std::string text = print(arena, c);
        ParseResult again = parse(arena, text);
        NodeId c2 = canonicalize(arena, again.root);
        t.equal(text, "(0 * (0^-1))",
                "an undefined zero reciprocal absorbs the rest of its product");
        t.check(c == c2, "that blocked product survives a print and reparse");
    }

}

}  // namespace nps
