#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "nps/cas/giac_adapter.h"
#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/core/capability_manifest.h"
#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/linear.h"
#include "unit/adapter_tests.h"

namespace nps {
void run_catch_up_tests(TestSink &sink);
void run_integrity_tests(TestSink &sink);
void run_native_menu_tests(TestSink &sink);
void run_task_tests(TestSink &sink);
void run_solve_task_tests(TestSink &sink);
void run_command_tests(TestSink &sink);
void run_calculus_tests(TestSink &sink);
void run_ui_canvas_tests(TestSink &sink);
void run_integer_tests(TestSink &sink);
void run_matrix_row_tests(TestSink &sink);
void run_matrix_form_tests(TestSink &sink);
void run_matrix_tests(TestSink &sink);
}

using namespace nps;

namespace {

TestSink sink;

struct PollState {
    size_t calls = 0;
};

bool cancel_on_second_poll(void *context) {
    PollState *state = static_cast<PollState *>(context);
    return ++state->calls == 2;
}

// One fixed reply, so the adapter's boundary can be driven without Giac.
class ReplyingBackend : public Backend {
  public:
    explicit ReplyingBackend(const std::string &reply) : reply_(reply) {}

    bool eval(const std::string &, std::string *out, std::string *) override {
        *out = reply_;
        return true;
    }

  private:
    std::string reply_;
};

void check(bool cond, const std::string &what) { sink.check(cond, what); }

bool check_and(bool cond, const std::string &what) {
    sink.check(cond, what);
    return cond;
}

void equal(const std::string &got, const std::string &want, const std::string &what) {
    sink.equal(got, want, what);
}

std::string parse_print(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    if (!r.ok())
        return std::string("<") + status_name(r.status) + ": " + r.message + ">";
    return print(arena, r.root);
}

std::string parse_giac(const std::string &src) {
    Arena arena;
    ParseResult r = parse(arena, src);
    if (!r.ok())
        return std::string("<") + status_name(r.status) + ">";
    return print_giac(arena, r.root);
}

bool deep_printing() {
    Limits limits;
    limits.max_depth = 4096;
    Arena arena(limits);
    NodeId nested = arena.symbol("x");
    std::string expected;
    for (size_t depth = 0; depth < 2048; ++depth) {
        nested = arena.call("f", {nested});
        expected += "f(";
    }
    expected += "x";
    expected.append(2048, ')');
    return !arena.failed() && print(arena, nested) == expected &&
           print_giac(arena, nested) == expected;
}

void test_shapes() {
    {
        Arena arena;
        const NodeId x = arena.symbol("x");
        const NodeId invalid = arena.nary(Kind::Invalid, {});
        const NodeId nested = arena.binary(Kind::Add, x, invalid);
        const NodeId valid = arena.binary(Kind::Add, x, x);
        check(!arena.failed() && nested != kNoNode, "invalid printer fixture reaches the emitter");
        for (auto printer : {print, print_giac, print_math}) {
            equal(printer(arena, x), "x", "printers preserve a valid symbol");
            equal(printer(arena, valid), printer == print_giac ? "(x+x)" : "(x + x)",
                  "printers preserve valid operands and their existing spelling");
            for (NodeId id : {kNoNode, static_cast<NodeId>(arena.node_count()),
                              kNoNode - 1, invalid})
                check(printer(arena, id).empty(), "printers refuse invalid roots with empty output");
            check(printer(arena, nested).empty(),
                  "printers refuse an invalid operand without returning partial text");
        }
    }
#if defined(__unix__) || defined(__APPLE__)
    pthread_attr_t attributes;
    const int initialized = pthread_attr_init(&attributes);
    check(initialized == 0, "printer depth worker attributes initialize");
    if (initialized == 0) {
        const int sized = pthread_attr_setstacksize(&attributes, 128 * 1024);
        check(sized == 0, "printer depth worker uses a bounded native stack");
        if (sized == 0) {
            bool passed = false;
            pthread_t worker;
            const int started = pthread_create(&worker, &attributes, [](void *context) -> void * {
                *static_cast<bool *>(context) = deep_printing();
                return nullptr;
            }, &passed);
            check(started == 0, "printer depth worker starts");
            if (started == 0) {
                const int joined = pthread_join(worker, nullptr);
                check(joined == 0 && passed, "both printers preserve accepted expression depth");
            }
        }
        check(pthread_attr_destroy(&attributes) == 0, "printer worker attributes release");
    }
#else
    check(deep_printing(), "both printers preserve accepted expression depth");
#endif
    {
        Arena arena;
        const NodeId x = arena.symbol("x");
        for (Kind kind : {Kind::Neg, Kind::Pow, Kind::Equals, Kind::Assign, Kind::Approx,
                          Kind::Identity, Kind::Less, Kind::LessEqual, Kind::Greater,
                          Kind::GreaterEqual}) {
            for (size_t arity = 0; arity <= 3; ++arity) {
                if (arity == (kind == Kind::Neg ? 1 : 2))
                    continue;
                const NodeId malformed = arena.nary(kind, std::vector<NodeId>(arity, x));
                const NodeId nested = arena.call("f", {malformed});
                check(print(arena, malformed).empty() && print_giac(arena, malformed).empty() &&
                          print(arena, nested).empty() && print_giac(arena, nested).empty(),
                      "printers refuse malformed operands without returning a partial expression");
            }
        }
    }
    equal(parse_print("1"), "1", "a bare integer");
    equal(parse_print("x"), "x", "a bare symbol");
    equal(parse_print("1.5"), "1.5", "a decimal keeps its text");
    equal(parse_print("1e3"), "1e3", "an exponent belongs to the literal");
    equal(parse_print("\xE2\x88\x92" "1"), "(-1)", "TI minus admits negative constants");
    equal(parse_print("2\xE2\x88\x92" "3"), "(2 + (-3))", "TI subtraction retains the ordinary AST");
    equal(parse_print("\xE2\x88\x92" "x^2"), "(-(x^2))", "TI unary minus preserves power precedence");
    equal(parse_print("x^\xE2\x88\x92" "1"), "(x^(-1))", "TI minus admits negative powers");
    equal(parse_print("1e\xE2\x88\x92" "3"), "1e-3", "TI exponent minus stays inside the decimal literal");
    equal(parse_print("2.50E\xE2\x88\x92" "12"), "2.50E-12", "TI exponent normalization retains mantissa precision");
    equal(parse_print("\xE2\x88\x92" "99999999999999999999999999"), "-99999999999999999999999999",
          "TI minus preserves wide integer spelling");
    {
        Arena arena;
        const ParseResult malformed = parse(arena, "2\xE2\x88\x92@");
        check(!malformed.ok() && malformed.offset == 4, "TI minus preserves original byte offsets on errors");
    }
    for (const char *invalid : {"\xE2\x88", "\xE2\x88\x92", "1e\xE2\x88\x92",
                               "1\xE2\x80\x93" "2", "1\xE2\x80\x94" "2"}) {
        Arena arena;
        check(!parse(arena, invalid).ok(), "minus admission preserves malformed input and unrelated dash refusals");
    }
    {
        Limits tight;
        tight.max_input_bytes = 3;
        Arena arena(tight);
        check(parse(arena, "\xE2\x88\x92" "1").status == Status::InputTooLong && arena.node_count() == 0,
              "TI minus retains the original input byte budget before parsing");
    }
    equal(parse_print("2 + 3"), "(2 + 3)", "addition");
    equal(parse_print("2 - 3"), "(2 + (-3))", "subtraction is addition of a negation");
    equal(parse_print("2 * 3"), "(2 * 3)", "multiplication");
    equal(parse_print("2 / 3"), "(2 * (3^(-1)))", "division is multiplication by a power");
    equal(parse_print("2x"), "(2 * x)", "juxtaposition after a number");
    equal(parse_print("2(3)"), "(2 * 3)", "juxtaposition of a number and a group");
    equal(parse_print("x^2"), "(x^2)", "a power");
    equal(parse_print("-x^2"), "(-(x^2))", "unary minus binds looser than a power");
    equal(parse_print("x^-1"), "(x^(-1))", "a negative exponent");
    equal(parse_print("2x^-1"), "(2 * (x^(-1)))", "a negative exponent after juxtaposition");
    equal(parse_print("2(-3)"), "(2 * (-3))", "juxtaposition with a negative group");
    equal(parse_print("-2x"), "((-2) * x)", "juxtaposition after a negated number");
    equal(parse_print("-2sin(x)"), "((-2) * sin(x))", "juxtaposition of a negated number and a call");
    equal(parse_print("-2x+6"), "(((-2) * x) + 6)", "a negated leading coefficient in an equation side");
    equal(parse_print("-99999999999999999999999999"), "-99999999999999999999999999",
          "a wide signed integer stays an integer");
    equal(parse_print("--99999999999999999999999999"), "99999999999999999999999999",
          "double negation toggles a wide integer sign");
    equal(parse_print("--7"), "(-(-7))", "double negation keeps the ordinary small AST shape");
    equal(parse_print("x - 99999999999999999999999999"),
          "(x + -99999999999999999999999999)", "subtraction preserves a wide signed integer");
    equal(parse_print("-9223372036854775808^2"), "(-(9223372036854775808^2))",
          "unary minus still binds outside a power at the signed boundary");
    equal(parse_print("(-9223372036854775808)^2"), "((-9223372036854775808)^2)",
          "parentheses keep the signed boundary as a power base");
    equal(parse_print("x^2^3"), "(x^(2^3))", "powers are right associative");
    equal(parse_print("2 + 3 * 4"), "(2 + (3 * 4))", "multiplication binds tighter than addition");
    equal(parse_print("(2 + 3) * 4"), "((2 + 3) * 4)", "parentheses override precedence");
    equal(parse_print("sin(x)"), "sin(x)", "a call");
    equal(parse_print("max(1, 2)"), "max(1, 2)", "a call with two arguments");
    equal(parse_print("2x + 5 = 13"), "(((2 * x) + 5) = 13)", "the PRD's worked example");
    equal(parse_print("x^2*sin(x)"), "((x^2) * sin(x))", "the milestone 0 expression");
}

void test_integer_primitives() {
    equal(integer_text(std::numeric_limits<int64_t>::min()), "-9223372036854775808",
          "the standard formatter handles the signed minimum");
    equal(integer_text(std::numeric_limits<int64_t>::max()), "9223372036854775807",
          "the standard formatter handles the signed maximum");

    Arena arena;
    const NodeId minimum = arena.integer("-9223372036854775808");
    const NodeId maximum = arena.integer("9223372036854775807");
    const NodeId below = arena.integer("-9223372036854775809");
    const NodeId above = arena.integer("9223372036854775808");
    check(arena.at(minimum).small_valid &&
              arena.at(minimum).small == std::numeric_limits<int64_t>::min(),
          "the standard parser accepts the signed minimum");
    check(arena.at(maximum).small_valid &&
              arena.at(maximum).small == std::numeric_limits<int64_t>::max(),
          "the standard parser accepts the signed maximum");
    check(!arena.at(below).small_valid && !arena.at(above).small_valid,
          "the standard parser refuses integers outside the representation");
}

void test_giac_spelling() {
    equal(parse_giac("x^2*sin(x)"), "((x)^(2)*sin(x))", "giac spelling of the milestone expression");
    equal(parse_giac("2 - 3"), "(2+(-(3)))", "giac spelling of a subtraction");
    equal(parse_giac("2x + 5 = 13"), "(((2*x)+5))=(13)", "giac spelling of an equation");
}

void test_round_trip() {
    const char *cases[] = {
        "1", "x", "1.5", "2 + 3", "2 - 3", "2 * 3", "2 / 3", "2x", "x^2", "-x^2", "x^-1",
        "x^2^3", "2 + 3 * 4", "(2 + 3) * 4", "sin(x)", "max(1, 2)", "2x + 5 = 13",
        "x^2*sin(x)", "a + b + c", "a * b * c", "sin(cos(tan(x)))", "3 <= x",
        "-9223372036854775808", "-0009223372036854775808", "x^-9223372036854775808",
        "-00099999999999999999999999", "x^-99999999999999999999999999",
    };
    for (const char *src : cases) {
        std::string once = parse_print(src);
        std::string twice = parse_print(once);
        equal(twice, once, std::string("round trip is a fixed point for ") + src);
    }
}

void check_canonical_round_trip(const char *source, const char *expected, const char *what) {
    Arena arena;
    ParseResult parsed = parse(arena, source);
    check(parsed.ok(), std::string(what) + " parses");
    if (!parsed.ok())
        return;

    NodeId canonical = canonicalize(arena, parsed.root);
    check(canonical != kNoNode, std::string(what) + " canonicalizes");
    if (canonical == kNoNode)
        return;

    const std::string text = print(arena, canonical);
    equal(text, expected, std::string(what) + " prints exactly");
    ParseResult reparsed = parse(arena, text);
    check(reparsed.ok(), std::string(what) + " reparses");
    if (!reparsed.ok())
        return;

    NodeId round_trip = canonicalize(arena, reparsed.root);
    check(round_trip == canonical, std::string(what) + " keeps its canonical AST across a round trip");
}

void test_canonical_signed_integers() {
    check_canonical_round_trip("(-8)^21", "-9223372036854775808", "the signed-minimum power");
    check_canonical_round_trip("-00099999999999999999999999", "-00099999999999999999999999",
                               "a wide signed integer with leading zeros");

    Arena arena;
    ParseResult parsed = parse(arena, "--99999999999999999999999999");
    check(parsed.ok() && arena.node_count() == 2,
          "wide double negation reuses its two exact integer spellings");
}

void test_interning() {
    Arena arena;
    ParseResult r = parse(arena, "sin(x) + sin(x)");
    check(r.ok(), "an interning case parses");
    if (r.ok()) {
        const ChildView sum = arena.children(r.root);
        check(sum.size() == 2, "the sum has two operands");
        if (sum.size() == 2)
            check(sum[0] == sum[1],
                  "equal subtrees are the same node, so structural equality is index equality");
    }
}

void test_reference_stability() {
    Arena arena;
    NodeId first = arena.integer("1");
    const Node &held = arena.at(first);
    NodeId last = kNoNode;
    for (int i = 2; i < 2000; ++i)
        last = arena.integer(integer_text(i));
    check(!arena.failed() && last != kNoNode, "two thousand nodes fit under the default limits");
    check(&arena.at(first) == &held, "a node keeps its address while the arena grows");
    check(held.kind == Kind::Integer && held.small_valid && held.small == 1,
          "and the reference still reads what it held");
}

void test_limits() {
    {
        Arena arena;
        const NodeId x = arena.symbol("x");
        NodeId shared = x;
        for (size_t depth = 0; depth < 31; ++depth)
            shared = arena.binary(Kind::Add, shared, shared);
        const uint32_t maximum = std::numeric_limits<uint32_t>::max();
        check(!arena.failed() && arena.at(shared).size == maximum,
              "a shared graph can reach the largest recorded expression size");
        for (NodeId parent : {arena.unary(Kind::Neg, shared),
                              arena.binary(Kind::Mul, shared, x),
                              arena.call("f", {shared}),
                              arena.call("f", {shared, shared})}) {
            check(parent != kNoNode && arena.at(parent).size == maximum,
                  "expression size saturates instead of wrapping when parents grow");
        }
        const NodeId pair = arena.binary(Kind::Add, x, x);
        check(arena.at(x).size == 1 && arena.at(pair).size == 3 &&
                  arena.at(arena.call("f", {pair, pair})).size == 7,
              "ordinary expression sizes count repeated children exactly");
        check(arena.binary(Kind::Add, shared, shared) ==
                  arena.binary(Kind::Add, shared, shared) && !arena.failed(),
              "saturated size preserves interning and structural admission");
    }
    {
        Limits tight;
        tight.max_depth = 4;
        Arena arena(tight);
        ParseResult r = parse(arena, "((((((1))))))+2*3^4^5");
        check(r.status == Status::DepthExceeded, "a deep expression is refused for depth");
    }
    {
        // Parentheses build no node, so the node-depth check never sees them. Only the parser's own
        // recursion guard refuses this, which is what stops a deep nest overflowing the stack.
        Limits tight;
        tight.max_depth = 4;
        Arena arena(tight);
        std::string deep(64, '(');
        deep += "1";
        deep.append(64, ')');
        ParseResult r = parse(arena, deep);
        check(r.status == Status::DepthExceeded, "deep parentheses are refused before the stack overflows");
    }
    {
        Limits tight;
        tight.max_nodes = 8;
        Arena arena(tight);
        ParseResult r = parse(arena, "1+2+3+4+5+6+7+8+9+10+11+12");
        check(r.status == Status::SizeExceeded, "a wide expression is refused for size");
    }
    {
        Limits tight;
        tight.max_input_bytes = 4;
        Arena arena(tight);
        ParseResult r = parse(arena, "1 + 2 + 3");
        check(r.status == Status::InputTooLong, "an over long input is refused before parsing");
        check(!arena.failed() && arena.integer("1") != kNoNode,
              "and the arena it was offered to stays usable, since it holds nothing it should not");
    }
    {
        // The adapter parses Giac's reply into the caller's arena. A reply too deep to accept used to
        // mark that arena failed, and everything the caller built afterwards came back as kNoNode.
        Limits tight;
        tight.max_depth = 4;
        Arena arena(tight);
        std::string deep(64, '(');
        deep += "1";
        deep.append(64, ')');
        ParseResult r = parse(arena, deep);
        check(r.status == Status::DepthExceeded && !r.message.empty(),
              "deep parentheses are refused with a message");
        check(!arena.failed() && arena.integer("1") != kNoNode,
              "and the parser's own recursion guard does not fail the arena");
    }
    {
        Limits tight;
        tight.max_nodes = 1;
        Arena arena(tight);
        NodeId first = arena.integer("1");
        NodeId again = arena.integer("1");
        check(first != kNoNode && again == first && !arena.failed(),
              "a full arena still returns a node it already holds");
        check(arena.integer("2") == kNoNode && arena.status() == Status::SizeExceeded,
              "and refuses only a new one");
    }
}

void test_budgets() {
    Budget formatted;
    formatted.max_rewrites = 0;
    formatted.max_steps = 42;
    formatted.max_branches = 7;
    formatted.max_backend_calls = 999;
    equal(budget_policy(formatted), "rewrites<=0 steps<=42 branches<=7 backend<=999",
          "the standard formatter records every budget value");

    PollState state;
    Budget budget;
    budget.poll = cancel_on_second_poll;
    budget.poll_context = &state;
    Meter meter(budget);
    check(!meter.stopped() && state.calls == 1, "a meter polls once when work starts");

    bool running = true;
    for (size_t i = 0; i < 63; ++i)
        running = meter.rewrite() && running;
    check(running && state.calls == 1, "the rewrite poll keeps its 64 operation stride");
    check(!meter.rewrite() && meter.halt() == Halt::Cancelled,
          "the stride poll observes a later cancellation");
    check(state.calls == 2 && meter.cost().rewrites == 64,
          "and accounts for the rewrite that observed it");

    Budget shared;
    shared.max_rewrites = 2;
    shared.max_steps = 2;
    shared.max_branches = 2;
    shared.max_backend_calls = 2;
    Meter parent(shared);
    check(parent.rewrite() && parent.step() && parent.branch() && parent.backend_call(),
          "a parent can spend one unit from every nested-work limit");
    const Budget nested = remaining_budget(shared, parent);
    check(nested.max_rewrites == 1 && nested.max_steps == 1 && nested.max_branches == 1 &&
              nested.max_backend_calls == 1,
          "a nested budget receives the remainder of all four work limits");

    Cost nested_cost;
    nested_cost.rewrites = 1;
    nested_cost.steps = 1;
    nested_cost.branches = 1;
    nested_cost.backend_calls = 1;
    check(charge(parent, nested_cost), "nested work fitting every remainder is charged");
    const Cost combined = parent.cost();
    check(combined.rewrites == 2 && combined.steps == 2 && combined.branches == 2 &&
              combined.backend_calls == 2,
          "charging combines all four nested work counters");

    Budget one_branch;
    one_branch.max_branches = 1;
    Meter exhausted(one_branch);
    Cost two_branches;
    two_branches.branches = 2;
    check(!charge(exhausted, two_branches) && exhausted.halt() == Halt::BranchLimit &&
              exhausted.branches() == 2,
          "charging branch work preserves the meter's terminal limit semantics");
}

void test_rejections() {
    struct Case {
        const char *src;
        const char *what;
    };
    const Case cases[] = {
        {"", "empty input"},
        {"1 +", "a trailing operator"},
        {"(1", "an unclosed parenthesis"},
        {"1)", "a stray closing parenthesis"},
        {"x y", "two names side by side"},
        {"2 3", "two numbers side by side"},
        {"-2 3", "two numbers side by side after a negation"},
        {"1.2.3", "a split decimal literal"},
        {"-1.2.3", "a split decimal literal after a negation"},
        {"1 = 2 = 3", "a chained relation"},
        {"sin(", "an unfinished call"},
        {"1 $ 2", "an unknown character"},
    };
    for (const Case &c : cases) {
        Arena arena;
        ParseResult r = parse(arena, c.src);
        check(!r.ok(), std::string("rejects ") + c.what);
    }
}

// A field the manifest carries but leaves blank is not a field it contains, and a null one crashes
// whoever reads it, so both are failures here.
bool filled(const char *text) { return text != nullptr && *text != '\0'; }

// PLAT-009 names six things the manifest must contain, so all six are checked, and every element of
// each list rather than its length. A count alone would pass a manifest listing four modules with
// nothing in them.
//
// The full six-field check also lives in tests/target/luax_host.lua, and that is where PLAT-009 is
// actually evidenced, because the manifest this binary links has the release target list compiled
// out. The check below therefore proves five of the six here and tags nothing, which is deliberate:
// see the gate at the end. luax_host is a ctest of its own, not the sanitizer-only custom target an
// earlier version of this comment described, and it appends its result to the same evidence file.
void test_plat009_manifest() {
    // Every field below is filled today, so the predicate would look identical if it returned true
    // for everything. These three are what say it does not.
    check(!filled(nullptr) && !filled("") && filled("x"),
          "a blank or missing manifest field does not count as filled");

    const CapabilityManifest m = capability_manifest();
    bool complete = true;

    complete = check_and(filled(m.id) && filled(m.artifact) && filled(m.stepcas_version),
                         "PLAT-009 the manifest names its id, artifact and StepCAS version") &&
               complete;

    // The host build compiles the release target list out, on purpose: it is not a release artifact
    // and a manifest claiming to support calculator models it was never built for would be worse
    // than one claiming none. So every target present is checked, and their absence is not a
    // failure here. It does mean the host cannot evidence this clause, which is why nothing is
    // tagged below unless the list is there.
    bool targets = true;
    for (size_t i = 0; i < m.supported_target_count; ++i) {
        const SupportedTarget &t = m.supported_targets[i];
        targets = targets && filled(t.calculator_model) && filled(t.os_version) &&
                  filled(t.ndl_version);
    }
    complete = check_and(targets,
                         "PLAT-009 every supported target names its model, OS and Ndl version") &&
               complete;

    complete = check_and(filled(m.symbolic_backend.name) && filled(m.symbolic_backend.version) &&
                             filled(m.symbolic_backend.interface_id),
                         "PLAT-009 the symbolic backend names its version and interface") &&
               complete;

    bool modules = m.installed_module_count > 0;
    for (size_t i = 0; i < m.installed_module_count; ++i)
        modules = modules && filled(m.installed_modules[i].kind) && filled(m.installed_modules[i].id);
    complete = check_and(modules, "PLAT-009 every installed module names its kind and id") && complete;

    bool schemas = m.schema_version_count > 0;
    for (size_t i = 0; i < m.schema_version_count; ++i)
        schemas = schemas && filled(m.schema_versions[i].id) && m.schema_versions[i].version > 0;
    complete = check_and(schemas, "PLAT-009 every schema is named and versioned") && complete;

    bool identifiers = m.integrity_identifier_count > 0;
    for (size_t i = 0; i < m.integrity_identifier_count; ++i) {
        const IntegrityIdentifier &g = m.integrity_identifiers[i];
        identifiers = identifiers && filled(g.component) && filled(g.scheme) && filled(g.value);
    }
    complete = check_and(identifiers,
                         "PLAT-009 every integrity identifier names its component, scheme and value") &&
               complete;

    // Five of PLAT-009's six items hold on any build. The sixth, the supported model and OS and
    // Ndl combinations, is compiled out of the host, so a host run proves five of six and the
    // requirement is left unevidenced rather than tagged on a partial reading.
    //
    // This branch is therefore unreachable and is kept as the check rather than as the evidence. No
    // build reaches it: run_tests.cc is compiled only into nps_host, which sits in the non-cross
    // branch of CMakeLists.txt, and nps_host links nps_manifest_host, whose release_targets argument
    // is the literal 0. An earlier comment here said a release build tags it, which described a
    // build that does not exist. luax_host reads a manifest that does carry the list and tags it.
    if (m.supported_target_count > 0) {
        sink.evidence("PLAT-009", complete,
                      "the build publishes a capability manifest carrying its StepCAS version, its "
                      "supported model and OS and Ndl combinations, its symbolic backend version "
                      "and interface, its installed solver and content modules, its schema versions "
                      "and its integrity identifiers, with every element of every list filled "
                      "rather than merely present");
    }
}

// MATH-001 lists ten categories by name, so the check names all ten rather than sampling. Each is
// parsed and its printed form compared, which is what "into an internal abstract syntax tree" means
// here: the shape survives, so the tree holds the thing rather than a reading of it.
void test_math001_categories() {
    struct Category {
        const char *what;
        const char *src;
        const char *shape;
    };
    const Category cases[] = {
        {"integers", "42", "42"},
        {"exact rational numbers", "2/3", "(2 * (3^(-1)))"},
        {"decimals", "1.5", "1.5"},
        {"variables", "x", "x"},
        {"functions", "sin(x)", "sin(x)"},
        {"powers", "x^2", "(x^2)"},
        {"radicals", "sqrt(x)", "sqrt(x)"},
        {"constants", "pi", "pi"},
        {"equations", "2x + 5 = 13", "(((2 * x) + 5) = 13)"},
        {"inequalities", "3 <= x", "(3 <= x)"},
    };
    bool all = true;
    for (const Category &c : cases) {
        const bool ok = parse_print(c.src) == c.shape;
        all = all && ok;
        equal(parse_print(c.src), c.shape, std::string("MATH-001 parses ") + c.what);
    }
    // The typed pi is normalised to the name, so both spellings reach the same node.
    Arena arena;
    ParseResult typed = parse(arena, "\xCF\x80");
    const bool same_constant = typed.ok() && print(arena, typed.root) == "pi";
    check(same_constant, "the typed constant reaches the same node as its spelled name");
    const auto infinity = parse(arena, "\xE2\x88\x9E");
    check(infinity.ok() && print(arena, infinity.root) == "infinity" &&
              is_identifier("\xE2\x88\x9E") && normalize_identifier("\xE2\x88\x9E") == "infinity",
          "TI infinity spelling shares parser and identifier normalization");
    Limits tight;
    tight.max_input_bytes = 3;
    check(is_identifier("abc", tight.max_input_bytes) &&
              !is_identifier("abcd", tight.max_input_bytes) &&
              is_identifier(std::string(Limits{}.max_input_bytes, 'x')) &&
              !is_identifier(std::string(Limits{}.max_input_bytes + 1, 'x')),
          "identifier validation retains names at the byte limit and rejects longer names");
    check(!parse(arena, "\xE2\x88").ok(), "an incomplete infinity code point is rejected");
    sink.evidence("MATH-001", all && same_constant,
                  "integers, exact rationals, decimals, variables, functions, powers, radicals, "
                  "constants, equations and inequalities each parse into the AST with their shape "
                  "intact");
}

// MATH-004. The four relations are one AST kind apart, and the point of the requirement is that
// nothing downstream can confuse them, so each part is asked separately rather than inferred from
// the parse: they parse apart, they print apart, they survive canonicalization as themselves, the
// solver takes only the one that is an equation, and the two Giac cannot express are refused rather
// than sent.
void test_math004_relations() {
    struct Relation {
        const char *what;
        const char *src;
        const char *shape;
    };
    const Relation cases[] = {
        {"assignment", "x := 3", "(x := 3)"},
        {"equality", "x = 3", "(x = 3)"},
        {"approximation", "x ~= 3", "(x ~= 3)"},
        {"identity", "x == 3", "(x == 3)"},
    };
    bool distinct = true;
    std::set<std::string> shapes;
    for (const Relation &r : cases) {
        const bool ok = parse_print(r.src) == r.shape;
        distinct = distinct && ok;
        equal(parse_print(r.src), r.shape, std::string("MATH-004 parses ") + r.what);
        shapes.insert(parse_print(r.src));
        // Criterion 14 replays a stored context by printing it and parsing it back, so a relation
        // that did not survive its own printed form would come back as a different claim.
        equal(parse_print(parse_print(r.src)), r.shape,
              std::string("and ") + r.what + " round trips through its printed form");
    }
    check(shapes.size() == 4, "MATH-004: the four relations print four different ways");

    // Canonicalization normalises the two sides and must leave the claim between them alone.
    // Equalizing an approximation here is the specific failure the requirement guards against, so
    // it is asked of an expression whose sides do change.
    const auto canonical_shape = [](const char *src) {
        Arena a;
        ParseResult p = parse(a, src);
        if (!p.ok())
            return std::string("did not parse");
        NodeId c = canonicalize(a, p.root);
        return c == kNoNode ? std::string("did not canonicalize") : print(a, c);
    };
    // Asked as a comparison rather than against a literal, because the interesting claim is that
    // the relation is the only thing that differs. Canonicalization collects both sides the same
    // way whichever relation sits between them, so an approximation coming back equalized would
    // show up here as the two agreeing.
    equal(canonical_shape("2x + x ~= 3"), "((3 * x) ~= 3)",
          "MATH-004: canonicalizing an approximation keeps it an approximation");
    equal(canonical_shape("2x + x == 3"), "((3 * x) == 3)",
          "and canonicalizing an identity keeps it an identity");
    check(canonical_shape("2x + x ~= 3") != canonical_shape("2x + x = 3"),
          "so an approximation and the equality written over the same sides stay apart");

    // Chained relations were refused before this change and the guard had no test. Both spellings
    // are asked, because adding tokens to the opening switch without adding them to the chain
    // switch would accept a := b := c silently.
    // The message is asserted, not just the refusal. Removing these tokens from the chain switch
    // still refuses, because the trailing token fails the top-level parse, so a test that only
    // checked ok() would pass either way and prove nothing about the guard.
    Arena chained;
    const auto why_refused = [&chained](const char *src) {
        ParseResult p = parse(chained, src);
        return p.ok() ? std::string("accepted") : p.message;
    };
    equal(why_refused("a := b := c"), "chained relations are not accepted, use one at a time",
          "MATH-004: a chained assignment is refused as a chained relation");
    equal(why_refused("a = b ~= c"), "chained relations are not accepted, use one at a time",
          "and so is an approximation chained onto an equality");
    equal(why_refused("a = b = c"), "chained relations are not accepted, use one at a time",
          "which was true of plain equality all along and had no test until now");

    // The solver takes an equation and nothing else. Assignment binds a name rather than stating a
    // constraint, so solving one would answer a question that was not asked.
    Arena solving;
    ParseResult assignment = parse(solving, "2x := 8");
    check(assignment.ok(), "an assignment parses");
    Derivation d;
    SolveResult solved = solve_linear(solving, d, assignment.root, solving.symbol("x"));
    equal(solve_outcome_name(solved.outcome), "not an equation",
          "MATH-004: the linear solver refuses an assignment rather than solving it");

    // Giac has no faithful syntax for either, so the text path emits nothing and the bridge reports
    // that rather than sending an empty command.
    Arena giac;
    ParseResult approx = parse(giac, "x ~= 3");
    ParseResult equals = parse(giac, "x = 3");
    const bool refused = approx.ok() && print_giac(giac, approx.root).empty();
    const bool sent = equals.ok() && !print_giac(giac, equals.root).empty();
    check(refused && sent,
          "MATH-004: an approximation is not rendered for Giac while an equation still is");

    sink.evidence("MATH-004", distinct && shapes.size() == 4 && refused && sent,
                  "assignment, equality, approximation and identity parse to four different AST "
                  "kinds that print apart, survive canonicalization as themselves, and are told "
                  "apart downstream: the solver takes only the equation and the two Giac cannot "
                  "express are refused rather than sent");
}

void test_math009_exactness() {
    Arena arena;
    const auto category = [&arena](const char *src) {
        ParseResult p = parse(arena, src);
        return std::string(exactness_name(p.ok() ? exactness(arena, p.root) : Exactness::NotALiteral));
    };

    equal(category("7"), "exact rational", "MATH-009: an integer literal is exact");
    equal(category("1.5"), "measured", "MATH-009: a written decimal is a measurement");
    equal(category("pi"), "exact constant", "MATH-009: pi is an exact constant");
    equal(category("\xCF\x80"), "exact constant",
          "MATH-009: and so is the same constant typed as its own glyph");
    equal(category("x"), "not a literal", "MATH-009: an ordinary symbol is no literal at all");
    equal(category("e"), "not a literal",
          "MATH-009: e is left an ordinary symbol, because the Giac bridge treats it as one and a "
          "constant only this side knew about would put the two out of step");
    equal(category("-7"), "not a literal",
          "MATH-009: a negation is a node above the literal rather than a literal itself");
    equal(category("2 + 3"), "not a literal", "MATH-009: nor is a sum of two of them");

    // The approximate mark is written only where the adapter hands back an engine's reply.
    Arena backend;
    const NodeId user_typed = backend.decimal("2.7182818284590451");
    ReplyingBackend engine("1.4142135623730951");
    Adapter adapter(backend, engine);
    Request approximate;
    approximate.op = Op::Approximate;
    approximate.target = parse(backend, "sqrt(2)").root;
    const Response reply = adapter.run(approximate);
    const NodeId from_giac = reply.value;
    check(reply.tag == ResultTag::Approximate && from_giac != kNoNode &&
              backend.at(from_giac).kind == Kind::Decimal,
          "MATH-009: an evalf reply comes back as a decimal literal tagged approximate");
    equal(std::string(exactness_name(exactness(backend, from_giac))), "approximate",
          "MATH-009: a decimal the backend returned reads as approximate, however many digits it "
          "has, because digit counting would be a guess at where a number came from");
    equal(std::string(exactness_name(exactness(backend, user_typed))), "measured",
          "MATH-009: a decimal the user typed in the same arena stays a measurement, so the mark "
          "followed the reply and not the kind");

    // Hash consing means provenance can only ever attach to an identity, so a later literal spelled
    // the same is the same node and inherits the mark. Asserted rather than left implicit: the
    // direction is what makes it safe, since approximate claims less exactness than measured.
    const NodeId typed_later = backend.decimal("1.4142135623730951");
    check(typed_later == from_giac &&
              exactness(backend, typed_later) == Exactness::Approximate,
          "MATH-009: a decimal spelled like a marked one shares its node and its mark, which "
          "understates exactness rather than overstating it");
    check(inexact(Exactness::Measured) && inexact(Exactness::Approximate) &&
              !inexact(Exactness::ExactRational) && !inexact(Exactness::ExactConstant) &&
              !inexact(Exactness::NotALiteral),
          "MATH-009: inexact names the two categories a rule may not quietly discard");

    // The promotion the numeric mode runs is where provenance is genuinely lost, and this pins it
    // as a known loss rather than leaving it to be discovered. num.decimal-to-rational records the
    // step, so the derivation still holds what the expression no longer does.
    Arena promoted;
    ParseResult half = parse(promoted, "0.5");
    const NodeId exact = exactify(promoted, half.root);
    const bool before_measured = exactness(promoted, half.root) == Exactness::Measured;
    const bool after_exact = exact != kNoNode && !inexact(exactness(promoted, exact));

    sink.evidence("MATH-009",
                  category("7") == "exact rational" && category("1.5") == "measured" &&
                      category("pi") == "exact constant" &&
                      exactness(backend, from_giac) == Exactness::Approximate && before_measured &&
                      after_exact,
                  "every numeric literal answers for where its value came from: an integer is an "
                  "exact rational, pi an exact constant, a written decimal a measurement and a "
                  "double the backend returned an approximation, told apart by a mark because the "
                  "last two share a kind. Reading a decimal exactly does drop the category, which "
                  "is why that promotion is a recorded step rather than a silent rewrite");
}

void test_error_positions() {
    Arena arena;
    ParseResult r = parse(arena, "2 + * 3");
    check(!r.ok(), "a misplaced operator is rejected");
    check(r.offset == 4, "the failure names the offset of the offending token");
    check(!r.message.empty(), "the failure carries a message");
    sink.evidence("UI-006", !r.ok() && r.offset == 4 &&
                                    r.message == "expected a number, a name or a parenthesis",
                  "invalid input identifies the offending location and carries an explanation");
}

// The evidence the run produced, for the traceability report. Written only when the build asks for
// it by name, so two trees running the same binary cannot overwrite each other's.
void write_evidence(const TestSink &s) {
    const char *path = getenv("NPS_EVIDENCE");
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("evidence: could not write %s\n", path);
        return;
    }
    for (const std::string &g : s.groups_run)
        fprintf(f, "group\t%s\n", g.c_str());
    // Every family id this run stamped, which is what lets the coverage join ask after a family
    // rather than after a test group that some other family already claims.
    for (const std::string &family : family_census())
        fprintf(f, "family\t%s\n", family.c_str());
    for (const Evidence &e : s.evidence_records)
        fprintf(f, "evidence\t%s\t%s\t%s\t%s\n", e.requirement.c_str(), e.passed ? "pass" : "fail",
                e.group.c_str(), e.what.c_str());
    fclose(f);
}

int main_body() {
    test_shapes();
    test_integer_primitives();
    test_giac_spelling();
    test_round_trip();
    test_canonical_signed_integers();
    test_interning();
    test_reference_stability();
    test_limits();
    test_budgets();
    test_rejections();
    test_math001_categories();
    test_math004_relations();
    test_math009_exactness();
    test_plat009_manifest();
    test_error_positions();
    // The group name is set here and nowhere else, so a test body cannot label its evidence as
    // something it is not.
    sink.begin_group("adapter");
    run_adapter_tests(sink);
    sink.begin_group("canonical");
    run_canonical_tests(sink);
    sink.begin_group("task");
    run_task_tests(sink);
    sink.begin_group("solve_task");
    run_solve_task_tests(sink);
    sink.begin_group("derivation");
    run_derivation_tests(sink);
    sink.begin_group("linear");
    run_linear_tests(sink);
    sink.begin_group("quadratic");
    run_quadratic_tests(sink);
    sink.begin_group("rearrange");
    run_rearrange_tests(sink);
    sink.begin_group("rewrite");
    run_rewrite_tests(sink);
    sink.begin_group("command");
    run_command_tests(sink);
    sink.begin_group("calculus");
    run_calculus_tests(sink);
    sink.begin_group("ui canvas");
    run_ui_canvas_tests(sink);
    sink.begin_group("integer");
    run_integer_tests(sink);
    sink.begin_group("matrix row");
    run_matrix_row_tests(sink);
    sink.begin_group("matrix form");
    run_matrix_form_tests(sink);
    sink.begin_group("matrix");
    run_matrix_tests(sink);
    sink.begin_group("differentiate");
    run_differentiate_tests(sink);
    sink.begin_group("integrate");
    run_integrate_tests(sink);
    sink.begin_group("integrity");
    run_integrity_tests(sink);
    sink.begin_group("native menu");
    run_native_menu_tests(sink);
    sink.begin_group("units");
    run_units_tests(sink);
    sink.begin_group("catch up");
    run_catch_up_tests(sink);
    sink.begin_group("density");
    run_density_tests(sink);
    sink.begin_group("modern");
    run_modern_tests(sink);
    sink.begin_group("relativity");
    run_relativity_tests(sink);
    sink.begin_group("gravitation");
    run_gravitation_tests(sink);
    sink.begin_group("oscillation");
    run_oscillation_tests(sink);
    sink.begin_group("circular motion");
    run_circular_motion_tests(sink);
    sink.begin_group("kinematics");
    run_kinematics_tests(sink);
    sink.begin_group("planar kinematics");
    run_planar_kinematics_tests(sink);
    sink.begin_group("position motion");
    run_position_motion_tests(sink);
    sink.begin_group("graph integration");
    run_graph_integration_tests(sink);
    sink.begin_group("optics");
    run_optics_tests(sink);
    sink.begin_group("relative motion");
    run_relative_motion_tests(sink);
    sink.begin_group("unit conversion");
    run_unit_conversion_tests(sink);
    sink.begin_group("vector addition");
    run_vector_addition_tests(sink);
    sink.begin_group("vector components");
    run_vector_components_tests(sink);
    sink.begin_group("vector cross product");
    run_vector_cross_tests(sink);
    sink.begin_group("scalar product");
    run_scalar_product_tests(sink);
    sink.begin_group("forces");
    run_forces_tests(sink);
    sink.begin_group("ranking");
    run_ranking_tests(sink);
    sink.begin_group("work");
    run_work_tests(sink);
    sink.begin_group("context");
    run_context_tests(sink);
    sink.begin_group("fuzz");
    run_fuzz_tests(sink);
    sink.begin_group("golden");
    run_golden_tests(sink);

    write_evidence(sink);
    // A total is one number, and two platforms reporting different totals from the same sources say
    // nothing about where they parted. Set NPS_GROUP_COUNTS and the tally says which group.
    if (const char *tally = std::getenv("NPS_GROUP_COUNTS"); tally && tally[0] != '\0') {
        // Fifteen test functions run before the first group opens and belong to none of them, which
        // is a couple of hundred checks the per-group lines cannot see. Without this the tally adds
        // up to less than the total and the gap looks like an error in the tally.
        if (!sink.group_opened_at.empty())
            printf("nps group: (before the first group) %d\n", sink.group_opened_at[0]);
        for (size_t i = 0; i < sink.groups_run.size(); ++i) {
            const int next = i + 1 < sink.group_opened_at.size()
                                 ? sink.group_opened_at[i + 1]
                                 : sink.checks;
            printf("nps group: %s %d\n", sink.groups_run[i].c_str(),
                   next - sink.group_opened_at[i]);
        }
    }
    // One step finer than the group tally, for when the group is known and the sentence is not.
    if (const char *labels = std::getenv("NPS_CHECK_LABELS"); labels && labels[0] != '\0') {
        for (const auto &entry : sink.label_counts)
            printf("nps label: %d\t%s\t%s\n", entry.second, entry.first.first.c_str(),
                   entry.first.second.c_str());
    }
    printf("nps: %d checks, %zu failed\n", sink.checks, sink.failures.size());
    for (const std::string &f : sink.failures)
        printf("  FAIL %s\n", f.c_str());
    return sink.failures.empty() ? 0 : 1;
}

}  // namespace

int main() { return main_body(); }
