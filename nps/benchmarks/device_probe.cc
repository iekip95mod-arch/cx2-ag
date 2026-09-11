// The first StepCAS code that runs on the calculator. It exercises the parser and both printers on
// the target and writes what it found, because a cross build that only compiles proves nothing
// about ARM: the size fields, the interning key packing and the recursion in the printer all behave
// differently if anything about the ABI is not what the host assumed.
//
// It does not touch Giac. That keeps it a standalone ndl program, which can be launched with
// exec and cannot take a Lua session down with it if it faults.

#include <cstdio>
#include <cstring>

#include <SDL/SDL.h>
#include <os.h>

#include "nps/core/capability_manifest.h"
#include "nps/core/canonical.h"
#include "nps/core/matrix.h"
#include "nps/cas/giac_adapter.h"
#include "nps/platform/nspire/measurement.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/linear.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

using namespace nps;

namespace {

const char *kOutput = "/documents/ndl/nps_bench.txt.tns";

// Stands in for luagiac so the adapter's own path runs here too. It answers one question with a
// known good string, which checks that a backend reply becomes an AST on the device rather than
// only on the host.
class CannedBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *out, std::string *error) override {
        (void)error;
        last = command;
        *out = "2*x*sin(x)+x^2*cos(x)";
        return true;
    }
    std::string last;
};

class SharedBackend : public Backend {
  public:
    explicit SharedBackend(NodeId root) : root_(root) {}
    bool eval(const std::string &, std::string *, std::string *) override { return false; }
    bool typed(const Request &, Arena &, TypedResult *reply) override {
        reply->tag = ResultTag::Approximate;
        reply->value = root_;
        return true;
    }

  private:
    NodeId root_;
};

void report(FILE *f, const char *label, const std::string &value) {
    fprintf(f, "%s = %s\n", label, value.c_str());
}

void check(FILE *f, const char *label, bool passed, ProbeCounts *counts) {
    fprintf(f, "%s = %s\n", label, passed ? "yes" : "no");
    counts->record(passed);
}

}  // namespace

int main() {
    FILE *f = fopen(kOutput, "wb");
    if (!f)
        return 1;

    ProbeReport measurements(f);
    ProbeCounts counts;
    measurements.text("schema", "nps-m0-probe-v1");
    fprintf(f, "start\n");
    {
        Arena malformed_arena;
        const NodeId numeral = malformed_arena.decimal("0.5");
        for (Kind kind : {Kind::Neg, Kind::Pow, Kind::Equals, Kind::Assign, Kind::Approx,
                          Kind::Identity, Kind::Less, Kind::LessEqual, Kind::Greater,
                          Kind::GreaterEqual}) {
            for (size_t arity : {0u, 1u, 2u, 3u}) {
                if (arity == (kind == Kind::Neg ? 1u : 2u))
                    continue;
                const NodeId malformed = malformed_arena.nary(kind, std::vector<NodeId>(arity, numeral));
                check(f, "malformed operator refused",
                      canonicalize(malformed_arena, malformed) == kNoNode &&
                          exactify(malformed_arena, malformed) == kNoNode &&
                          decimalize(malformed_arena, malformed) == kNoNode &&
                          !malformed_arena.failed(), &counts);
            }
        }
    }
    {
        Arena scalar_arena;
        const NodeId one = scalar_arena.integer("1");
        for (size_t arity : {0u, 2u, 3u}) {
            const NodeId malformed = scalar_arena.nary(Kind::Neg, std::vector<NodeId>(arity, one));
            int64_t integer = 99;
            check(f, "integer reader rejects malformed negation",
                  !small_integer(scalar_arena, malformed, &integer) && integer == 99, &counts);
        }
    }
    const CapabilityManifest manifest = capability_manifest();
    report(f, "manifest", manifest.id);
    report(f, "artifact", manifest.artifact);
    measurements.text("build.application", manifest.stepcas_version);
    measurements.text("build.manifest_id", manifest.id);
    measurements.text("build.artifact", manifest.artifact);
    measurements.number("target.hardware_type", nl_hwtype());
    measurements.number("target.hardware_subtype", nl_hwsubtype());
    measurements.number("target.ndl_revision", nl_ndl_rev());
    measurements.number("target.started_from_startup", static_cast<unsigned>(nl_isstartup()));
    measurements.text("timing.source", "SDL_GetTicks");
    measurements.text("timing.tick_unit", "millisecond");
    measurements.number("timing.frequency", 1000, "ticks_per_second");
    measurements.text("arena.allocation_model", "monotonic, operation end equals peak");

    const bool timer_ready = SDL_Init(0) == 0;
    measurements.text("timing.initialized", timer_ready ? "yes" : "no");
    counts.record(timer_ready);
    if (!timer_ready) {
        measurements.text("timing.error", SDL_GetError());
        measurements.summary(counts);
        fclose(f);
        return 1;
    }

    for (bool to_decimal : {false, true}) {
        for (bool deep : {false, true}) {
            Limits limits;
            limits.max_depth = 1024;
            limits.max_nodes = 4096;
            Arena conversion_arena(limits);
            const NodeId half = conversion_arena.decimal("0.5");
            const NodeId fraction = conversion_arena.binary(Kind::Mul, conversion_arena.integer("1"),
                conversion_arena.binary(Kind::Pow, conversion_arena.integer("2"),
                                         conversion_arena.integer("-1")));
            NodeId expression = to_decimal ? fraction : half;
            NodeId expected = to_decimal ? half : fraction;
            for (size_t depth = 0; depth < (deep ? 512 : 40); ++depth) {
                expression = deep ? conversion_arena.call("f", {expression})
                                  : conversion_arena.binary(Kind::Add, expression, expression);
                expected = deep ? conversion_arena.call("f", {expected})
                                : conversion_arena.binary(Kind::Add, expected, expected);
            }
            const uint32_t started = SDL_GetTicks();
            const NodeId converted = to_decimal ? decimalize(conversion_arena, expression)
                                                : exactify(conversion_arena, expression);
            check(f, deep ? "deep numeric conversion" : "shared numeric conversion",
                  expected != kNoNode && converted == expected && !conversion_arena.failed(), &counts);
            measurements.number("conversion.elapsed", SDL_GetTicks() - started, "milliseconds");
        }
    }
    for (size_t depth : {18u, 40u, 512u}) {
        Limits limits;
        limits.max_depth = 1024;
        Arena matrix_arena(limits);
        NodeId expression = matrix_arena.integer("0");
        for (size_t level = 0; level < depth; ++level)
            expression = depth == 512 ? matrix_arena.unary(Kind::Neg, expression)
                                      : matrix_arena.binary(Kind::Add, expression, expression);
        Rational value;
        const uint32_t started = SDL_GetTicks();
        check(f, "matrix cell remains exact",
              expression != kNoNode && read_matrix_rational(matrix_arena, expression, &value) &&
                  value.num == 0 && value.den == 1 && !matrix_arena.failed(), &counts);
        measurements.number("matrix.depth", depth);
        measurements.number("matrix.elapsed", SDL_GetTicks() - started, "milliseconds");
        check(f, "matrix reader accepts the exact cell",
              read_exact_matrix(matrix_arena, matrix_arena.list({matrix_arena.list({expression})})),
              &counts);
    }
    for (size_t depth : {18u, 40u, 512u}) {
        Limits limits;
        limits.max_depth = 1024;
        Arena reply_arena(limits);
        const NodeId decimal = reply_arena.decimal("0.5");
        NodeId shared = decimal;
        for (size_t level = 0; level < depth; ++level)
            shared = depth == 512 ? reply_arena.call("f", {shared})
                                  : reply_arena.call("f", {shared, shared});
        SharedBackend backend(shared);
        Adapter adapter(reply_arena, backend);
        Request request;
        request.op = Op::Simplify;
        request.target = reply_arena.integer("1");
        const uint32_t started = SDL_GetTicks();
        const Response response = adapter.run(request);
        check(f, "shared backend decimals retain provenance",
              response.usable() && response.value == shared && reply_arena.is_approximate(decimal),
              &counts);
        measurements.number("adapter.depth", depth);
        measurements.number("adapter.shared.elapsed", SDL_GetTicks() - started, "milliseconds");
    }
    measurements.headroom("before", measure_contiguous_headroom());
    fprintf(f, "installed modules = %u\n", static_cast<unsigned>(manifest.installed_module_count));
    for (size_t i = 0; i < manifest.installed_module_count; ++i)
        report(f, manifest.installed_modules[i].kind, manifest.installed_modules[i].id);
    fprintf(f, "sizeof(Node) = %u\n", static_cast<unsigned>(sizeof(Node)));
    measurements.number("arena.node_size", sizeof(Node), "bytes");

    Arena arena;
    ParseResult parsed = parse(arena, "x^2*sin(x)");
    if (!parsed.ok()) {
        counts.record(false);
        fprintf(f, "parse failed: %s at %u\n", status_name(parsed.status),
                static_cast<unsigned>(parsed.offset));
        measurements.summary(counts);
        SDL_Quit();
        fclose(f);
        return 1;
    }
    counts.record(true);

    report(f, "canonical", print(arena, parsed.root));
    report(f, "giac", print_giac(arena, parsed.root));
    fprintf(f, "nodes = %u depth = %u\n", static_cast<unsigned>(arena.node_count()),
            static_cast<unsigned>(arena.at(parsed.root).depth));

    ParseResult again = parse(arena, print(arena, parsed.root));
    check(f, "round trip is a fixed point", again.ok() && again.root == parsed.root, &counts);

    ParseResult refused = parse(arena, "1 = 2 = 3");
    check(f, "chained relation refused", !refused.ok(), &counts);

    CannedBackend backend;
    Adapter adapter(arena, backend);
    Request req;
    req.op = Op::Differentiate;
    req.target = parsed.root;
    req.variable = parse(arena, "x").root;
    Response r = adapter.run(req);
    report(f, "command", backend.last);
    report(f, "tag", tag_name(r.tag));
    if (r.usable())
        report(f, "value", print(arena, r.value));
    check(f, "adapter answer usable", r.usable(), &counts);

    {
        ParseResult a = parse(arena, "sin(x)*x^2 + 1");
        ParseResult b = parse(arena, "1 + x^2*sin(x)");
        NodeId ca = canonicalize(arena, a.root);
        NodeId cb = canonicalize(arena, b.root);
        report(f, "canonical form", print(arena, ca));
        check(f, "two spellings agree", ca != kNoNode && ca == cb, &counts);

        ParseResult big = parse(arena, "999999999999999999 + 999999999999999999");
        check(f, "wide integer folds",
              big.ok() && print(arena, canonicalize(arena, big.root)) == "1999999999999999998",
              &counts);
    }

    {
        Limits tight;
        tight.max_nodes = 8;
        Arena small(tight);
        ParseResult over = parse(small, "1+2+3+4+5+6+7+8+9+10+11+12");
        const bool limit_fired = over.status == Status::SizeExceeded;
        fprintf(f, "size limit fires = %s (%s)\n",
                limit_fired ? "yes" : "no", status_name(over.status));
        counts.record(limit_fired);
    }

    {
        // Milestone 1's exit condition in miniature: a linear equation solved through steps that
        // can be browsed, not a bare answer.
        Arena solve_arena;
        Derivation d;
        ParseResult equation = parse(solve_arena, "2x + 5 = 13");
        NodeId unknown = solve_arena.symbol("x");
        const uint32_t started = SDL_GetTicks();
        SolveResult sr;
        if (equation.ok() && unknown != kNoNode)
            sr = solve_linear(solve_arena, d, equation.root, unknown);
        const uint32_t elapsed = SDL_GetTicks() - started;
        const ArenaPeak arena_peak{solve_arena.node_count(), solve_arena.child_slot_count()};
        fprintf(f, "solve outcome = %s\n", solve_outcome_name(sr.outcome));
        if (sr.solution != kNoNode)
            report(f, "solution", print(solve_arena, sr.solution));
        fprintf(f, "steps = %u\n", static_cast<unsigned>(d.size()));
        for (size_t i = 0; i < d.size(); ++i) {
            const Step &s = d.at(static_cast<StepId>(i));
            fprintf(f, "  [%s] %s | %s | verified=%s\n", step_kind_name(s.kind), s.goal.c_str(),
                    s.explanation_short.c_str(), s.verified() ? "yes" : "no");
        }
        OperationMeasurement operation;
        operation.name = "linear_solve";
        operation.outcome = solve_outcome_name(sr.outcome);
        operation.iterations = 1;
        operation.elapsed_ticks = elapsed;
        operation.elapsed_milliseconds = elapsed;
        operation.arena_peak = arena_peak;
        operation.derivation_steps = d.size();
        operation.cost = sr.cost;
        operation.success = sr.outcome == SolveOutcome::Solved;
        measurements.operation(operation);
        counts.record(operation.success);

        Derivation refused;
        ParseResult quadratic = parse(solve_arena, "x^2 = 4");
        SolveResult qr;
        if (quadratic.ok())
            qr = solve_linear(solve_arena, refused, quadratic.root, unknown);
        fprintf(f, "quadratic refused = %s\n", solve_outcome_name(qr.outcome));
        counts.record(qr.outcome == SolveOutcome::NotLinear && refused.size() == 0);
    }

    {
        // Milestone 2's headline: the derivative of x squared sin x, by rule, with the steps that
        // produced it. The adapter answer above is Giac's; this one is ours.
        Arena diff_arena;
        Derivation d;
        ParseResult expression = parse(diff_arena, "x^2*sin(x)");
        NodeId variable = diff_arena.symbol("x");
        const uint32_t started = SDL_GetTicks();
        DiffResult dr;
        if (expression.ok() && variable != kNoNode)
            dr = differentiate(diff_arena, d, expression.root, variable);
        const uint32_t elapsed = SDL_GetTicks() - started;
        const ArenaPeak arena_peak{diff_arena.node_count(), diff_arena.child_slot_count()};
        fprintf(f, "diff outcome = %s\n", diff_outcome_name(dr.outcome));
        if (dr.derivative != kNoNode) {
            report(f, "derivative", print(diff_arena, dr.derivative));
            report(f, "derivative canonical",
                   print(diff_arena, canonicalize(diff_arena, dr.derivative)));
        }
        fprintf(f, "diff steps = %u\n", static_cast<unsigned>(d.size()));
        for (size_t i = 0; i < d.size(); ++i) {
            const Step &s = d.at(static_cast<StepId>(i));
            fprintf(f, "  [%s] %s | %s\n", step_kind_name(s.kind),
                    s.rule_name.empty() ? "plan" : s.rule_name.c_str(), s.goal.c_str());
        }
        OperationMeasurement operation;
        operation.name = "differentiate";
        operation.outcome = diff_outcome_name(dr.outcome);
        operation.iterations = 1;
        operation.elapsed_ticks = elapsed;
        operation.elapsed_milliseconds = elapsed;
        operation.arena_peak = arena_peak;
        operation.derivation_steps = d.size();
        operation.cost = dr.cost;
        operation.success = dr.outcome == DiffOutcome::Differentiated;
        measurements.operation(operation);
        counts.record(operation.success);

        Derivation no_rule;
        ParseResult hard = parse(diff_arena, "x^x");
        DiffResult hr;
        if (hard.ok())
            hr = differentiate(diff_arena, no_rule, hard.root, variable);
        fprintf(f, "variable exponent refused = %s\n", diff_outcome_name(hr.outcome));
        counts.record(hr.outcome == DiffOutcome::UnsupportedForm && no_rule.size() == 0);
    }

    {
        // The bug-hunt fixes, exercised on the device rather than only on the host.
        // The recursion guard sits at three times max_depth, so this has to pass 192 frames.
        std::string deep(200, '(');
        deep += "1";
        deep.append(200, ')');
        ParseResult dp = parse(arena, deep);
        const bool depth_refused = dp.status == Status::DepthExceeded;
        fprintf(f, "deep parens refused = %s (%s)\n",
                depth_refused ? "yes" : "no", status_name(dp.status));
        counts.record(depth_refused);

        report(f, "decimal order", print(arena, canonicalize(arena, parse(arena, "2.5 + 1.25").root)));

        Derivation dd;
        NodeId huge = parse(arena, "x + (-1)^9223372036854775807 = 0").root;
        SolveResult hs = solve_linear(arena, dd, huge, arena.symbol("x"));
        fprintf(f, "huge exponent solves = %s", solve_outcome_name(hs.outcome));
        if (hs.solution != kNoNode)
            fprintf(f, " x=%s", print(arena, hs.solution).c_str());
        fprintf(f, "\n");
        counts.record(hs.outcome == SolveOutcome::Solved && hs.solution != kNoNode);

        Limits tiny;
        tiny.max_nodes = 5;
        Arena ta(tiny);
        NodeId fold = canonicalize(ta, parse(ta, "x + 2 + 3").root);
        check(f, "fold exhaustion clean", fold == kNoNode, &counts);
    }

    {
        // The second review's fixes, on the target's 32 bit ARM rather than only on the host.
        Derivation d;
        NodeId eq = parse(arena, "x + 4611686018427387904*(-2) = 0").root;
        SolveResult sr = solve_linear(arena, d, eq, arena.symbol("x"));
        fprintf(f, "int64 minimum refused = %s (%s)\n",
                sr.outcome == SolveOutcome::Solved ? "no" : "yes", solve_outcome_name(sr.outcome));
        counts.record(sr.outcome != SolveOutcome::Solved);

        report(f, "leading zeros", print(arena, canonicalize(arena, parse(arena, "007 + x^007").root)));

        Derivation neg;
        differentiate(arena, neg, parse(arena, "-sin(x)").root, arena.symbol("x"));
        check(f, "negation records a step", neg.size() == 3, &counts);

        Derivation partial;
        differentiate(arena, partial, parse(arena, "x^x + sin(x)").root, arena.symbol("x"));
        check(f, "refusal leaves no steps", partial.size() == 0, &counts);

        Arena spare;
        parse(spare, std::string(5000, '1'));
        check(f, "long input leaves the arena usable", spare.integer("1") != kNoNode, &counts);
    }

    measurements.headroom("after", measure_contiguous_headroom());
    measurements.summary(counts);
    measurements.text("status", counts.failures == 0 ? "pass" : "fail");
    fprintf(f, "done\n");
    const bool write_ok = measurements.ok() && std::ferror(f) == 0;
    SDL_Quit();
    const bool close_ok = fclose(f) == 0;
    return write_ok && close_ok && counts.failures == 0 ? 0 : 1;
}
