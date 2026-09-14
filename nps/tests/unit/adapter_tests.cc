#include "unit/adapter_tests.h"

#include <string>
#include <vector>

#include "nps/cas/giac_adapter.h"
#include "nps/cas/matrix_events.h"
#include "nps/core/matrix.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

namespace nps {
namespace {

// Answers whatever it was told to, and keeps the command so a test can assert what the adapter
// actually sent. That is the half of the contract a real Giac would hide.
class ScriptedBackend : public Backend {
  public:
    explicit ScriptedBackend(const std::string &reply, bool succeed = true)
        : reply_(reply), succeed_(succeed) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        last_command = command;
        if (!succeed_) {
            *error = "the fake backend was told to fail";
            return false;
        }
        *out = reply_;
        return true;
    }

    std::string last_command;

  private:
    std::string reply_;
    bool succeed_;
};

class CountingRowSink : public MatrixRowSink {
  public:
    bool row(NodeId, NodeId, const MatrixRowOperation &) override {
        ++rows;
        return true;
    }
    size_t rows = 0;
};

NodeId must_parse(Arena &arena, const std::string &src) {
    ParseResult r = parse(arena, src);
    return r.root;
}

class TypedRoots : public Backend {
  public:
    explicit TypedRoots(std::vector<std::string> roots) : roots_(std::move(roots)) {}
    bool eval(const std::string &, std::string *, std::string *) override {
        ++string_calls;
        return false;
    }
    bool typed(const Request &, Arena &arena, TypedResult *out) override {
        ++typed_calls;
        out->tag = tag;
        out->from_backend = !ours;
        out->shape = shape;
        for (const std::string &root : roots_)
            out->values.push_back(must_parse(arena, root));
        if (invalid_node)
            out->values.push_back(kNoNode);
        return true;
    }
    ResultTag tag = ResultTag::Exact;
    ResultShape shape = ResultShape::FiniteSolutions;
    bool invalid_node = false;
    bool ours = false;
    size_t string_calls = 0;
    size_t typed_calls = 0;

  private:
    std::vector<std::string> roots_;
};

class TypedValue : public Backend {
  public:
    TypedValue(NodeId value, ResultShape shape) : value_(value), shape_(shape) {}
    bool eval(const std::string &, std::string *, std::string *) override { return false; }
    bool typed(const Request &, Arena &arena, TypedResult *out) override {
        out->tag = ResultTag::Exact;
        out->shape = shape_;
        if (shape_ == ResultShape::Scalar)
            out->value = value_;
        else
            out->values = {value_, arena.integer("1"), arena.integer("2")};
        return true;
    }

  private:
    NodeId value_;
    ResultShape shape_;
};

class TypedMatrix : public Backend {
  public:
    explicit TypedMatrix(std::string reply) : reply_(std::move(reply)) {}
    bool eval(const std::string &, std::string *, std::string *) override {
        ++string_calls;
        return false;
    }
    bool typed(const Request &, Arena &arena, TypedResult *out) override {
        ++typed_calls;
        out->tag = tag;
        out->shape = shape;
        out->value = missing ? kNoNode : must_parse(arena, reply_);
        if (approximate_container != 0) {
            const NodeId marked = approximate_container == 1 ? out->value :
                                  arena.children(out->value)[approximate_container - 2];
            arena.mark_approximate(marked);
        }
        if (extra_values)
            out->values.push_back(arena.integer("1"));
        return true;
    }
    ResultTag tag = ResultTag::Exact;
    ResultShape shape = ResultShape::Matrix;
    bool missing = false;
    bool extra_values = false;
    size_t approximate_container = 0;
    size_t typed_calls = 0;
    size_t string_calls = 0;

  private:
    std::string reply_;
};

void matrix_adapter_tests(TestSink &t) {
    t.check(static_cast<unsigned>(Op::Atan2) == 15 && static_cast<unsigned>(Op::Ref) == 16 &&
                static_cast<unsigned>(Op::Rref) == 17 &&
                static_cast<unsigned>(ResultShape::FiniteSolutions) == 2 &&
                static_cast<unsigned>(ResultShape::Matrix) == 3,
            "matrix enum additions preserve all previous identifiers");
    for (const auto &c : std::vector<std::pair<std::string, Rational>>{
             {"0", {0, 1}}, {"-7", {-7, 1}}, {"2/3", {2, 3}}, {"1+1/2", {3, 2}},
             {"(-2)^3", {-8, 1}}, {"1/(-4)", {-1, 4}}}) {
        Arena arena;
        Rational value{91, 17};
        t.check(read_matrix_rational(arena, must_parse(arena, c.first), &value) &&
                    value.num == c.second.num && value.den == c.second.den,
                "shared matrix reader computes exact rational " + c.first);
    }
    for (const char *cell : {"x", "pi", "i", "0.5", "0*0.5", "f(1)", "1=1",
                             "[1]", "0^(-1)", "1/(1-1)", "2^(1/2)", "2^64"}) {
        Arena arena;
        Rational value{91, 17};
        t.check(!read_matrix_rational(arena, must_parse(arena, cell), &value) &&
                    value.num == 91 && value.den == 17,
                std::string("shared matrix reader refuses without fabricating a value: ") + cell);
    }
    {
        Arena arena;
        const NodeId integer = arena.integer("1");
        arena.mark_approximate(integer);
        const NodeId cell = arena.binary(Kind::Add, integer, arena.integer("2"));
        Rational value{91, 17};
        t.check(!read_matrix_rational(arena, cell, &value),
                "matrix reader rejects approximate provenance hidden in exact syntax");
        const NodeId invalid_power = arena.nary(Kind::Pow, {integer});
        t.check(!read_matrix_rational(arena, invalid_power, &value) &&
                    !read_matrix_rational(arena, kNoNode, &value),
                "matrix reader refuses invalid arity and missing cells");
    }
    {
        Arena arena;
        TypedMatrix backend("1");
        const Response response = Adapter(arena, backend).run(
            Request{Op::Simplify, arena.integer("1")});
        t.check(response.tag == ResultTag::MalformedResult && !response.usable(),
                "scalar operation refuses the matrix result tag even around a scalar node");
    }
    {
        Limits limits;
        limits.max_nodes = 3;
        Arena arena(limits);
        const NodeId matrix = must_parse(arena, "[[1]]");
        arena.integer("2");
        TypedMatrix backend("[[1]]");
        Adapter adapter(arena, backend);
        const Response response = adapter.run(Request{Op::Ref, matrix});
        t.check(response.tag == ResultTag::ResourceFailure && adapter.call_count() == 0 &&
                    backend.typed_calls == 0,
                "matrix admission stops before backend work after arena resource failure");
    }
    const Op operations[] = {Op::Ref, Op::Rref};
    for (Op op : operations) {
        for (size_t container = 0; container < 3; ++container) {
            Arena arena;
            const NodeId input = must_parse(arena, "[[2,4],[3,5]]");
            arena.mark_approximate(container == 0 ? input : arena.children(input)[container - 1]);
            const MatrixView matrix = *MatrixView::from(arena, input);
            bool exact_cells = true;
            for (size_t row = 0; row < matrix.rows(); ++row) {
                for (size_t column = 0; column < matrix.columns(); ++column) {
                    Rational cell;
                    exact_cells = exact_cells && read_matrix_rational(arena, matrix.cell(row, column), &cell);
                }
            }
            t.check(exact_cells, "request provenance fixtures mark only the matrix or one row identity");
            Request request;
            request.op = op;
            request.target = input;
            std::string why;
            t.check(matrix_request_status(arena, request, &why) == ResultTag::UnsupportedOperation,
                    "matrix admission rejects root and every row provenance despite exact cells");
            for (bool typed : {false, true}) {
                TypedMatrix typed_backend("[[1,2],[0,1]]");
                ScriptedBackend string_backend("[[1,2],[0,1]]");
                Backend &backend = typed ? static_cast<Backend &>(typed_backend) :
                                           static_cast<Backend &>(string_backend);
                Adapter adapter(arena, backend);
                const Response reply = adapter.run(request);
                t.check(reply.tag == ResultTag::UnsupportedOperation && !reply.usable() &&
                            reply.value == kNoNode && reply.values.empty() && adapter.call_count() == 0 &&
                            typed_backend.typed_calls == 0 && typed_backend.string_calls == 0 &&
                            string_backend.last_command.empty(),
                        "approximate matrix containers stop before either backend path");
            }
        }
        for (size_t container = 0; container < 3; ++container) {
            Arena arena;
            Request request;
            request.op = op;
            request.target = must_parse(arena, "[[2,4],[3,5]]");
            const NodeId output = must_parse(arena, "[[1,2],[0,1]]");
            const NodeId marked = container == 0 ? output : arena.children(output)[container - 1];
            arena.mark_approximate(marked);
            const MatrixView matrix = *MatrixView::from(arena, output);
            bool exact_cells = true;
            for (size_t row = 0; row < matrix.rows(); ++row) {
                for (size_t column = 0; column < matrix.columns(); ++column) {
                    Rational cell;
                    exact_cells = exact_cells && read_matrix_rational(arena, matrix.cell(row, column), &cell);
                }
            }
            std::string why;
            t.check(exact_cells && matrix_request_status(arena, request, &why) == ResultTag::Exact,
                    "output-only provenance fixtures leave the input and every cell exact");
            t.check(matrix_result_status(arena, request, output, &why) == ResultTag::Approximate,
                    "root and every row provenance prevent an exact matrix output tag");
            TypedMatrix backend("[[1,2],[0,1]]");
            backend.approximate_container = container + 1;
            Adapter adapter(arena, backend);
            const Response reply = adapter.run(request);
            t.check(reply.tag == ResultTag::Approximate && reply.usable() && reply.value == output &&
                        reply.shape == ResultShape::Matrix && reply.values.empty() &&
                        reply.single_value() == kNoNode && arena.is_approximate(marked) &&
                        adapter.call_count() == 1 && backend.typed_calls == 1 && backend.string_calls == 0,
                    "typed matrix outputs retain container provenance and their matrix shape");
        }
    }
    struct MatrixCase {
        const char *input;
        const char *reply;
        size_t rows;
        size_t columns;
    };
    const MatrixCase cases[] = {
        {"[[2]]", "[[1]]", 1, 1},
        {"[[0,2]]", "[[0,1]]", 1, 2},
        {"[[0],[2]]", "[[1],[0]]", 2, 1},
        {"[[1/2,-2/3,1],[0,3,4]]", "[[1,0,34/9],[0,1,4/3]]", 2, 3},
        {"[[1+1,2^-1],[0,3]]", "[[1,0],[0,1]]", 2, 2},
        {"[[1,0,0,0,2,3],[0,1,0,0,4,5],[0,0,1,0,6,7],[0,0,0,1,8,9]]",
         "[[1,0,0,0,2,3],[0,1,0,0,4,5],[0,0,1,0,6,7],[0,0,0,1,8,9]]", 4, 6},
    };
    for (Op op : operations) {
        for (const MatrixCase &c : cases) {
            for (bool typed : {false, true}) {
                Arena arena;
                Request request{op, must_parse(arena, c.input)};
                ScriptedBackend string_backend(c.reply);
                TypedMatrix typed_backend(c.reply);
                Backend &backend = typed ? static_cast<Backend &>(typed_backend)
                                         : static_cast<Backend &>(string_backend);
                Adapter adapter(arena, backend);
                const Response response = adapter.run(request);
                const auto matrix = MatrixView::from(arena, response.value);
                const std::string label = std::string("matrix ") + (typed ? "typed " : "string ") + c.input;
                t.check(response.tag == ResultTag::Exact && response.usable(), label + " is exact");
                t.check(response.shape == ResultShape::Matrix && matrix &&
                            matrix->rows() == c.rows && matrix->columns() == c.columns,
                        label + " preserves both dimensions");
                t.check(response.single_value() == kNoNode && response.values.empty(),
                        label + " cannot be extracted as a scalar or flat vector");
                if (matrix)
                    t.equal(print_giac(arena, response.value), print_giac(arena, must_parse(arena, c.reply)),
                            label + " preserves every ordered cell");
                t.check(adapter.call_count() == 1 && (!typed || typed_backend.string_calls == 0),
                        label + " dispatches once");
                if (!typed)
                    t.equal(string_backend.last_command,
                            std::string(op == operations[0] ? "ref(" : "rref(") +
                                print_giac(arena, request.target) + ")",
                            label + " uses the fixed operation command");
            }
        }
        for (const char *input : {"1", "[]", "[[]]", "[1,2]", "[[1],[2,3]]", "[[[1]]]",
                                 "[[x]]", "[[pi]]", "[[i]]", "[[sin(0)]]", "[[1=1]]",
                                 "[[1.0]]", "[[0*0.1]]", "[[1/0]]", "[[2^(1/2)]]",
                                 "[[9223372036854775808]]",
                                 "[[1],[2],[3],[4],[5]]", "[[1,2,3,4,5,6,7]]"}) {
            Arena arena;
            TypedMatrix backend("[[1]]");
            Adapter adapter(arena, backend);
            const Response response = adapter.run(Request{op, must_parse(arena, input)});
            t.check(response.tag == ResultTag::UnsupportedOperation && !response.usable() &&
                        adapter.call_count() == 0 && backend.typed_calls == 0 && backend.string_calls == 0,
                    std::string("matrix rejects before backend: ") + input);
        }
        {
            Arena arena;
            ScriptedBackend backend("[[1]]");
            Adapter adapter(arena, backend);
            CountingRowSink sink;
            const Response response = adapter.matrix_steps(Request{op, must_parse(arena, "[[1]]")},
                                                           sink);
            t.check(response.tag == ResultTag::BackendError && adapter.call_count() == 0 &&
                        sink.rows == 0 && backend.last_command.empty(),
                    "a backend with no row-step path is not counted as a call it never received");
        }
        for (size_t slot = 0; slot < 6; ++slot) {
            Arena arena;
            Request request{op, must_parse(arena, "[[1]]")};
            const NodeId extra = arena.integer("1");
            if (slot == 0) request.variable = extra;
            if (slot == 1) request.replacement = extra;
            if (slot == 2) request.point = extra;
            if (slot == 3) request.argument = extra;
            if (slot == 4) request.target_components.push_back(extra);
            if (slot == 5) request.argument_components.push_back(extra);
            TypedMatrix backend("[[1]]");
            Adapter adapter(arena, backend);
            t.check(adapter.run(request).tag == ResultTag::UnsupportedOperation &&
                        adapter.call_count() == 0 && backend.typed_calls == 0,
                    "matrix rejects every unsupported option slot before dispatch");
        }
        for (const char *reply : {"1", "[1]", "[]", "[[]]", "[[[1]]]", "[[1,2]]",
                                 "[[1],[2]]", "[[1],[2,3]]", "[[1],[2],[3],[4],[5]]"}) {
            for (bool typed : {false, true}) {
                Arena arena;
                ScriptedBackend string_backend(reply);
                TypedMatrix typed_backend(reply);
                Backend &backend = typed ? static_cast<Backend &>(typed_backend)
                                         : static_cast<Backend &>(string_backend);
                const Response response = Adapter(arena, backend).run(
                    Request{op, must_parse(arena, "[[1]]")});
                t.check(response.tag == ResultTag::MalformedResult && !response.usable() &&
                            response.value == kNoNode && response.single_value() == kNoNode,
                        std::string("matrix refuses changed rank/dimensions: ") + reply);
            }
        }
        for (const char *reply : {"[[x]]", "[[i]]", "[[sqrt(2)]]", "[[when(x,1,0)]]",
                                 "[[1=1]]", "[[1/0]]", "[[9223372036854775808]]"}) {
            for (bool typed : {false, true}) {
                Arena arena;
                ScriptedBackend string_backend(reply);
                TypedMatrix typed_backend(reply);
                Backend &backend = typed ? static_cast<Backend &>(typed_backend)
                                         : static_cast<Backend &>(string_backend);
                const Response response = Adapter(arena, backend).run(
                    Request{op, must_parse(arena, "[[1]]")});
                t.check(response.tag == ResultTag::UnsupportedOperation && !response.usable() &&
                            response.value == kNoNode,
                        std::string("matrix refuses unsupported cells: ") + reply);
            }
        }
        for (bool typed : {false, true}) {
            Arena arena;
            ScriptedBackend string_backend("[[0.5]]");
            TypedMatrix typed_backend("[[0.5]]");
            Backend &backend = typed ? static_cast<Backend &>(typed_backend)
                                     : static_cast<Backend &>(string_backend);
            const Response response = Adapter(arena, backend).run(
                Request{op, must_parse(arena, "[[1]]")});
            const auto matrix = MatrixView::from(arena, response.value);
            t.check(response.tag == ResultTag::Approximate && response.usable() && matrix &&
                        arena.is_approximate(matrix->cell(0, 0)) &&
                        response.single_value() == kNoNode,
                    "matrix decimal output retains approximate provenance without scalarization");
        }
    }
    for (ResultShape shape : {ResultShape::Scalar, ResultShape::Vector, ResultShape::FiniteSolutions}) {
        Arena arena;
        TypedMatrix backend("[[1]]");
        backend.shape = shape;
        const Response response = Adapter(arena, backend).run(
            Request{operations[0], must_parse(arena, "[[1]]")});
        t.check(response.tag == ResultTag::MalformedResult && response.value == kNoNode,
                "matrix operation rejects a typed result carrying the wrong shape tag");
    }
    for (bool missing : {false, true}) {
        Arena arena;
        TypedMatrix backend("[[1]]");
        backend.missing = missing;
        backend.extra_values = !missing;
        const Response response = Adapter(arena, backend).run(
            Request{operations[0], must_parse(arena, "[[1]]")});
        t.check(response.tag == ResultTag::MalformedResult && response.value == kNoNode &&
                    response.values.empty(), "matrix rejects incomplete or split authoritative storage");
    }
    for (ResultTag tag : {ResultTag::Conditional, ResultTag::Timeout, ResultTag::ResourceFailure,
                          ResultTag::BackendError, ResultTag::UnsupportedOperation}) {
        Arena arena;
        TypedMatrix backend("[[1]]");
        backend.tag = tag;
        const Response response = Adapter(arena, backend).run(
            Request{operations[0], must_parse(arena, "[[1]]")});
        t.check(response.tag == tag && !response.usable() && response.value == kNoNode,
                "matrix keeps typed refusal class and clears unqualified values");
    }
    {
        Limits limits;
        limits.max_input_bytes = 32;
        Arena arena(limits);
        ScriptedBackend backend("[[11111111111111111111111111111111111111]]");
        const Response response = Adapter(arena, backend).run(
            Request{operations[0], must_parse(arena, "[[1]]")});
        t.check(response.tag == ResultTag::ResourceFailure && !response.usable(),
                "matrix response input limit is a resource failure");
    }
}

}  // namespace

void run_adapter_tests(TestSink &t) {
    for (const ResultTag tag : {ResultTag::Cancelled, ResultTag::ResourceFailure,
                                ResultTag::Timeout}) {
        Arena arena;
        TypedRoots backend({"1"});
        backend.tag = tag;
        Request request{Op::IsZero};
        request.target = arena.integer("0");
        // A second Adapter, because one request reaches Giac through several engines and each one
        // builds its own. The latch has to outlive the adapter that saw the tag or it holds nothing.
        const Response first = Adapter(arena, backend).run(request);
        const size_t reached = backend.string_calls + backend.typed_calls;
        const Response again = Adapter(arena, backend).run(request);
        t.check(first.tag == tag && again.tag == tag &&
                    backend.string_calls + backend.typed_calls == reached,
                std::string("a backend that reported ") + tag_name(tag) +
                    " is not asked a second time in the same request");
    }
    {
        Arena arena;
        TypedRoots backend({"1"});
        Request request{Op::IsZero};
        request.target = arena.integer("0");
        Adapter(arena, backend).run(request);
        const size_t reached = backend.string_calls + backend.typed_calls;
        Adapter(arena, backend).run(request);
        t.check(backend.string_calls + backend.typed_calls > reached,
                "a backend that answered normally is still asked again");
    }
    {
        // The typed tag is a shared channel: giac_typed.cc:1100-1104 answers ResourceFailure for our
        // own node limit with a healthy arena and a backend that answered. from_backend is how that
        // says so, and without it one oversized solution set retires Giac for the whole request.
        Arena arena;
        TypedRoots backend({"1"});
        backend.tag = ResultTag::ResourceFailure;
        backend.ours = true;
        Request request{Op::IsZero};
        request.target = arena.integer("0");
        const Response refused = Adapter(arena, backend).run(request);
        const size_t reached = backend.string_calls + backend.typed_calls;
        backend.tag = ResultTag::Exact;
        backend.ours = false;
        const Response after = Adapter(arena, backend).run(request);
        t.check(refused.tag == ResultTag::ResourceFailure &&
                    backend.string_calls + backend.typed_calls > reached &&
                    after.tag != ResultTag::ResourceFailure,
                "our own limit refusing a reply does not retire the backend that gave it");
    }
    {
        // Our own limits refusing to hold a reply is a resource failure too, and it belongs to the
        // arena rather than to Giac. Two arenas over one backend is the only shape that tells them
        // apart, so a one-arena test would pass with the latch reading either.
        Arena spent;
        Arena healthy;
        TypedRoots backend({"1"});
        Request refused{Op::IsZero};
        refused.target = spent.integer("0");
        spent.fail(Status::SizeExceeded);
        const size_t before = backend.string_calls + backend.typed_calls;
        const Response arena_said = Adapter(spent, backend).run(refused);
        const size_t reached = backend.string_calls + backend.typed_calls;
        Request asked{Op::IsZero};
        asked.target = healthy.integer("0");
        const Response after = Adapter(healthy, backend).run(asked);
        t.check(arena_said.tag == ResultTag::ResourceFailure && reached == before,
                "a failed arena refuses without troubling the backend");
        t.check(backend.string_calls + backend.typed_calls > reached &&
                    after.tag != ResultTag::ResourceFailure,
                "and a healthy arena still gets a real answer from the same backend");
    }
    {
        Arena arena;
        ScriptedBackend backend("1/3");
        Adapter adapter(arena, backend);
        Request request{Op::Integrate};
        request.target = must_parse(arena, "x^2");
        request.variable = arena.symbol("x");
        request.lower = arena.integer("0");
        request.upper = arena.integer("1");
        const Response response = adapter.run(request);
        t.check(response.usable() && backend.last_command == "integrate((x)^(2),x,0,1)",
                "definite integration sends both bounds to Giac: " + backend.last_command);
        request.upper = kNoNode;
        const size_t calls = adapter.call_count();
        t.check(adapter.run(request).tag == ResultTag::UnsupportedOperation && adapter.call_count() == calls,
                "an incomplete integral interval is refused before backend work");
    }
    for (int direction : {-1, 0, 1, 2}) {
        Arena arena;
        ScriptedBackend backend("1");
        Adapter adapter(arena, backend);
        Request request{Op::Limit};
        request.target = arena.symbol("x");
        request.variable = request.target;
        request.point = arena.integer("1");
        request.direction = direction;
        const Response response = adapter.run(request);
        t.check(direction == 2 ? response.tag == ResultTag::UnsupportedOperation && adapter.call_count() == 0
                              : response.usable() && backend.last_command ==
                                    "limit(x,x,1" + (direction == 0 ? std::string() : "," + std::to_string(direction)) + ")",
                "Giac limit requests preserve and validate their direction");
    }
    for (bool deep : {false, true}) {
        for (ResultShape shape : {ResultShape::Scalar, ResultShape::Vector}) {
            Limits limits;
            limits.max_depth = 4096;
            Arena arena(limits);
            const NodeId half = arena.decimal("0.5");
            const NodeId quarter = arena.decimal("0.25");
            const NodeId unrelated = arena.decimal("0.125");
            NodeId shared = arena.symbol("x");
            for (size_t depth = 0; depth < (deep ? 2048 : 40); ++depth)
                shared = deep ? arena.call("f", {shared}) : arena.call("f", {shared, shared});
            const NodeId root = arena.call("g", {shared, half, quarter});
            TypedValue backend(root, shape);
            Adapter adapter(arena, backend);
            Request request;
            request.op = shape == ResultShape::Scalar ? Op::Simplify : Op::Cross;
            request.target = arena.integer("1");
            if (shape == ResultShape::Vector) {
                request.target_components = {arena.integer("1"), arena.integer("2"), arena.integer("3")};
                request.argument_components = request.target_components;
            }
            const Response response = adapter.run(request);
            const NodeId returned = shape == ResultShape::Scalar ? response.value
                                       : (response.values.empty() ? kNoNode : response.values[0]);
            t.check(root != kNoNode && response.usable() && returned == root && !arena.failed() &&
                        adapter.call_count() == 1,
                    "backend provenance scanning handles deep and shared scalar and vector replies");
            t.check(arena.is_approximate(half) && arena.is_approximate(quarter) &&
                        !arena.is_approximate(unrelated) && !arena.is_approximate(shared),
                    "backend provenance visits every returned decimal and leaves unrelated nodes alone");
        }
    }
    matrix_adapter_tests(t);
    for (const auto &sample : std::vector<std::pair<std::string, std::vector<Rational>>>{
             {"list[]", {}}, {"list[ ]", {}}, {"list[0]", {{0, 1}}},
             {"list[4]", {{4, 1}}}, {"list[7/3]", {{7, 3}}},
             {"list[-2,2]", {{-2, 1}, {2, 1}}},
             {"list[-1/2,1/2]", {{-1, 2}, {1, 2}}},
             {" \tlist[ 1/2, -1/2 ]\n", {{1, 2}, {-1, 2}}},
             {"list[2,-2,2]", {{2, 1}, {-2, 1}, {2, 1}}},
             {"list[[4]]", {{4, 1}}}, {"[list[-2,2]]", {{-2, 1}, {2, 1}}}}) {
        Arena arena;
        ScriptedBackend backend(sample.first);
        Adapter adapter(arena, backend);
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = adapter.run(request);
        t.check(reply.tag == ResultTag::Exact && reply.usable() &&
                    reply.shape == ResultShape::FiniteSolutions && reply.value == kNoNode &&
                    reply.values.size() == sample.second.size(),
                "Giac typed-list spelling preserves the finite solution collection: " + sample.first);
        bool exact_roots = reply.values.size() == sample.second.size();
        for (size_t index = 0; exact_roots && index < sample.second.size(); ++index) {
            Rational root;
            exact_roots = evaluate_rational(arena, reply.values[index], {}, &root) &&
                          root.num == sample.second[index].num && root.den == sample.second[index].den;
        }
        t.check(exact_roots, "Giac typed-list roots retain exact value, order and duplicates: " + sample.first);
        t.check(reply.raw == sample.first && adapter.call_count() == 1 &&
                    (reply.single_value() != kNoNode) == (sample.second.size() == 1),
                "typed-list interpretation preserves raw output and scalar extraction rules");
    }
    for (const std::string &raw : {"list[1", "list[1}", "list{1}", "list(1)", "list [1]",
                                   "List[1]", "mylist[1]", "listlist[1]", "list[1]+list[2]",
                                   "list[1]x", "list[1],[2]", "list[1];2", "list[1,,2]",
                                   "list[,1]", "list[1,]", "list[(1]]", "list[{1)]",
                                   "list[[1)]", "list[[1],[2]]", "list[1,[2]]", "list[1,list[2]]",
                                   "list[(1+[2])]", "list[1,0*[2]]", "list[1,sin([2])]",
                                   "list[2,x]", "list[2,x=2]", "list[2,i]", "list[2,infinity]",
                                   "list[2,n_1*pi]", "list[2,when(x>0,1,2)]", "list[2,1/0]"}) {
        Arena arena;
        ScriptedBackend backend(raw);
        Adapter adapter(arena, backend);
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = adapter.run(request);
        t.check(!reply.usable() && reply.value == kNoNode && reply.values.empty() &&
                    reply.single_value() == kNoNode,
                "malformed typed lists and unsupported roots expose no partial solution: " + raw);
    }
    for (Op op : {Op::Simplify, Op::Cross, Op::Ref, Op::Rref}) {
        Arena arena;
        ScriptedBackend backend(op == Op::Cross ? "list[1,2,3]" :
                                op == Op::Simplify ? "list[1]" : "list[[1]]");
        Request request;
        request.op = op;
        request.target = op == Op::Ref || op == Op::Rref ? must_parse(arena, "[[1]]") : arena.integer("1");
        if (op == Op::Cross) {
            request.target_components = {arena.integer("1"), arena.integer("2"), arena.integer("3")};
            request.argument_components = request.target_components;
        }
        const Response reply = Adapter(arena, backend).run(request);
        t.check(!reply.usable() && reply.value == kNoNode && reply.values.empty(),
                "typed-list solution spelling does not broaden other operation result shapes");
    }
    for (size_t input_limit : {size_t{12}, size_t{11}}) {
        Limits limits;
        limits.max_input_bytes = input_limit;
        Arena arena(limits);
        ScriptedBackend backend("list[123456]");
        Request request;
        request.op = Op::Solve;
        request.target = request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(input_limit == 12 ? reply.tag == ResultTag::Exact && reply.values.size() == 1 :
                                   reply.tag == ResultTag::ResourceFailure && reply.values.empty(),
                "the shared input limit includes the typed-list prefix and delimiters");
    }
    {
        Limits limits;
        limits.max_nodes = 16;
        Arena arena(limits);
        ScriptedBackend backend("list[2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2]");
        Request request;
        request.op = Op::Solve;
        request.target = request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(reply.tag == ResultTag::ResourceFailure && reply.values.empty(),
                "duplicate roots in typed-list text cannot evade the collection node limit");
    }
    {
        Limits limits;
        limits.max_nodes = 3;
        Arena arena(limits);
        ScriptedBackend backend("list[1,2,3]");
        Request request;
        request.op = Op::Solve;
        request.target = request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(reply.tag == ResultTag::ResourceFailure && reply.values.empty() && arena.failed(),
                "typed-list root parsing discards a partial collection after Arena exhaustion");
    }
    for (const char *expression : {"x:=1", "f(x:=1)", "x~=1", "x==1"}) {
        Arena arena;
        const NodeId untranslated = must_parse(arena, expression);
        t.check(untranslated != kNoNode && print_giac(arena, untranslated).empty(),
                std::string("the backend cannot faithfully translate: ") + expression);
        for (NodeId Request::*field : {&Request::target, &Request::replacement,
                                      &Request::point, &Request::argument}) {
            ScriptedBackend backend("1");
            Adapter adapter(arena, backend);
            Request request;
            request.op = field == &Request::replacement ? Op::Substitute
                         : field == &Request::point ? Op::Limit
                         : field == &Request::argument ? Op::Atan2 : Op::Simplify;
            request.target = request.variable = arena.symbol("x");
            request.*field = untranslated;
            const Response reply = adapter.run(request);
            t.check(reply.tag == ResultTag::UnsupportedOperation && adapter.call_count() == 0 &&
                        backend.last_command.empty(),
                    std::string(op_name(request.op)) + " refuses an untranslatable operand before backend work");
        }
        for (bool in_argument : {false, true}) {
            ScriptedBackend backend("1");
            Adapter adapter(arena, backend);
            Request request;
            request.op = Op::Dot;
            request.target_components = {arena.integer("1"), arena.integer("2")};
            request.argument_components = request.target_components;
            (in_argument ? request.argument_components : request.target_components)[0] = untranslated;
            const Response reply = adapter.run(request);
            t.check(reply.tag == ResultTag::UnsupportedOperation && adapter.call_count() == 0 &&
                        backend.last_command.empty(),
                    "an untranslatable vector component cannot become an empty operand");
        }
    }
    for (const char *expression : {"[1,2]", "[[1]]", "0*[1,2]", "sin([1,2])"}) {
        Arena arena;
        const ParseResult collection = parse(arena, expression);
        t.check(collection.ok(), std::string("collection guard input parses: ") + expression);
        if (!collection.ok())
            continue;
        for (Op op : {Op::Simplify, Op::Expand, Op::Factor, Op::Solve, Op::Differentiate,
                      Op::Integrate, Op::Limit, Op::Substitute, Op::Approximate, Op::IsZero,
                      Op::Sin, Op::Cos, Op::Atan2}) {
            ScriptedBackend backend("1");
            Adapter adapter(arena, backend);
            Request request;
            request.op = op;
            request.target = collection.root;
            request.variable = arena.symbol("x");
            request.replacement = request.point = request.argument = arena.integer("1");
            const Response reply = adapter.run(request);
            t.check(reply.tag == ResultTag::UnsupportedOperation && !reply.usable() &&
                        adapter.call_count() == 0 && backend.last_command.empty(),
                    std::string(op_name(op)) + " refuses a collection before backend work: " + expression);
        }
        for (NodeId Request::*field : {&Request::replacement, &Request::point, &Request::argument}) {
            ScriptedBackend backend("1");
            Adapter adapter(arena, backend);
            Request request;
            request.op = field == &Request::replacement ? Op::Substitute
                         : field == &Request::point ? Op::Limit : Op::Atan2;
            request.target = request.variable = arena.symbol("x");
            request.*field = collection.root;
            const Response reply = adapter.run(request);
            t.check(reply.tag == ResultTag::UnsupportedOperation && adapter.call_count() == 0,
                    std::string(op_name(request.op)) + " refuses collection arguments before backend work");
        }
        for (Op op : {Op::Dot, Op::Cross, Op::Norm}) {
            for (bool in_argument : {false, true}) {
                if (op == Op::Norm && in_argument)
                    continue;
                ScriptedBackend backend("1");
                Adapter adapter(arena, backend);
                Request request;
                request.op = op;
                request.target_components = {arena.integer("1"), arena.integer("2"), arena.integer("3")};
                if (op != Op::Norm)
                    request.argument_components = request.target_components;
                (in_argument ? request.argument_components : request.target_components)[0] = collection.root;
                const Response reply = adapter.run(request);
                t.check(reply.tag == ResultTag::UnsupportedOperation && adapter.call_count() == 0,
                        std::string(op_name(op)) + " requires scalar vector components");
            }
        }
        for (ResultShape shape : {ResultShape::Scalar, ResultShape::Vector, ResultShape::FiniteSolutions}) {
            TypedValue backend(collection.root, shape);
            Request request;
            request.op = shape == ResultShape::FiniteSolutions ? Op::Solve
                         : shape == ResultShape::Vector ? Op::Cross : Op::Simplify;
            request.target = request.variable = arena.symbol("x");
            if (shape == ResultShape::Vector) {
                request.target_components = {arena.integer("1"), arena.integer("2"), arena.integer("3")};
                request.argument_components = request.target_components;
            }
            const Response reply = Adapter(arena, backend).run(request);
            t.check(reply.tag == ResultTag::MalformedResult && !reply.usable() &&
                        reply.value == kNoNode && reply.values.empty(),
                    "a typed scalar or scalar component cannot contain a list");
        }
    }
    for (const std::vector<std::string> &roots : std::vector<std::vector<std::string>>{
             {}, {"0"}, {"-2", "2"}, {"2", "-2", "2"}}) {
        Arena arena;
        TypedRoots backend(roots);
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(reply.usable() && reply.shape == ResultShape::FiniteSolutions &&
                    reply.values.size() == roots.size() && reply.value == kNoNode &&
                    backend.string_calls == 0,
                "typed solve collections preserve every entry without string fallback");
        t.check((reply.single_value() != kNoNode) == (roots.size() == 1),
                "typed solution extraction requires exactly one root");
    }
    for (const std::string &root : {"x", "x=2", "i", "when(x>0,2,3)", "1/0", "2*pi*n_1"}) {
        Arena arena;
        TypedRoots backend({"2", root});
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(!reply.usable() && reply.values.empty(),
                "typed partial roots are discarded when any root is unsupported: " + root);
    }
    for (ResultTag tag : {ResultTag::Conditional, ResultTag::BackendError, ResultTag::ResourceFailure}) {
        Arena arena;
        TypedRoots backend({"2"});
        backend.tag = tag;
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(!reply.usable() && reply.values.empty() && reply.single_value() == kNoNode,
                "an incomplete typed solution collection cannot expose a scalar answer");
    }
    {
        Arena arena;
        TypedRoots backend({"2"});
        backend.invalid_node = true;
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        Response reply = Adapter(arena, backend).run(request);
        t.check(!reply.usable() && reply.values.empty(), "an invalid typed root is never indexed");
        backend.invalid_node = false;
        backend.shape = ResultShape::Vector;
        reply = Adapter(arena, backend).run(request);
        t.check(reply.tag == ResultTag::MalformedResult && reply.values.empty(),
                "a vector cannot be relabelled as a solution set by a consumer");
    }
    {
        Limits limits;
        limits.max_nodes = 16;
        Arena arena(limits);
        TypedRoots backend(std::vector<std::string>(17, "2"));
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response reply = Adapter(arena, backend).run(request);
        t.check(reply.tag == ResultTag::ResourceFailure && reply.values.empty(),
                "duplicate typed roots cannot evade the collection resource limit");
    }
    for (const auto &sample : std::vector<std::pair<std::string, size_t>>{
             {"[]", 0}, {"[ ]", 0}, {"[[]]", 0}, {"[0]", 1}, {"[[0]]", 1},
             {"[-2,2]", 2}, {"[[-2,2]]", 2}, {"[2,-2,2]", 3},
             {"[1/2,-1/2]", 2}, {"2", 1}}) {
        Arena arena;
        ScriptedBackend backend(sample.first);
        Adapter adapter(arena, backend);
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response response = adapter.run(request);
        t.check(response.tag == ResultTag::Exact && response.usable() &&
                    response.values.size() == sample.second && response.value == kNoNode,
                "solve retains the complete finite collection " + sample.first);
        if (response.values.size() == 3) {
            t.equal(print(arena, response.values[0]), "2", "root conversion preserves order");
            t.equal(print(arena, response.values[2]), "2", "root conversion preserves duplicates");
        }
    }
    for (const std::string &reply : {"[x]", "[x=2]", "[i,-i]", "[infinity]", "[n_1*pi]"}) {
        Arena arena;
        ScriptedBackend backend(reply);
        Adapter adapter(arena, backend);
        Request request;
        request.op = Op::Solve;
        request.target = must_parse(arena, "x^2=4");
        request.variable = arena.symbol("x");
        const Response response = adapter.run(request);
        t.check(!response.usable() && response.values.empty(),
                "an unsupported root cannot be a finite real solution " + reply);
    }
    {
        Arena arena;
        ScriptedBackend backend("2*x*sin(x)+x^2*cos(x)");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Differentiate;
        req.target = must_parse(arena, "x^2*sin(x)");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(backend.last_command, "diff(((x)^(2)*sin(x)),x)",
                "the adapter builds the command from typed parts");
        t.equal(tag_name(r.tag), "exact", "a clean symbolic answer is exact");
        t.check(r.usable(), "an exact answer is usable");
        t.equal(print(arena, r.value), "(((2 * x) * sin(x)) + ((x^2) * cos(x)))",
                "the answer comes back as an AST, not as text");
    }

    {
        // Measured on the handheld: Giac answers solve((((2*x)+5))=(13),x) with [[4]], and the
        // parser refused the brackets, so a correct answer was thrown away as malformed and solve
        // was never actually cross-checked.
        Arena arena;
        ScriptedBackend backend("[[4]]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "2x + 5 = 13");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "a one element solution list is that solution");
        t.equal(print(arena, r.single_value()), "4", "the one root remains available to scalar consumers");
    }

    {
        Arena arena;
        ScriptedBackend backend("[[x = 1, y = 2]]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "2x + 5 = 13");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation",
                "relations requiring unsupported solution structure are refused");
    }

    {
        Arena arena;
        ScriptedBackend backend("[]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "x + 1 = x");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "an empty solution set is an exact collection");
        t.check(r.usable() && r.shape == ResultShape::FiniteSolutions, "the empty set has an explicit shape");
        t.check(r.single_value() == kNoNode, "the empty set is not a scalar zero");
    }

    {
        Arena arena;
        ScriptedBackend backend("[-2,2]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "x^2 = 4");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "both solutions are retained");
        t.check(r.usable() && r.values.size() == 2 && r.single_value() == kNoNode,
                "a two-root collection cannot masquerade as one scalar");
    }

    {
        Arena arena;
        ScriptedBackend backend("[max(1, 2)]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "2x + 5 = 13");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact",
                "a comma inside the one solution is not a second solution");
    }

    {
        Arena arena;
        ScriptedBackend backend("[7]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "2x + 5 = 13");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "a single element list still peels to its element");
        t.equal(print(arena, r.single_value()), "7", "and returns that element to scalar consumers");
    }

    {
        // The outer brackets are not one enclosing list here, they close and reopen, so peeling them
        // would splice across the plus. It is malformed, not a two-solution set.
        Arena arena;
        ScriptedBackend backend("[1,2]+[3]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Solve;
        req.target = must_parse(arena, "2x + 5 = 13");
        req.variable = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "malformed result", "a sum of lists is malformed, not two solutions");
    }

    {
        Arena arena;
        ScriptedBackend backend("undef_val");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "undef inside a longer name is not the undefined sentinel");
    }

    {
        Arena arena;
        ScriptedBackend backend("undef");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unevaluated", "a bare undef is the undefined sentinel");
    }

    {
        // The same rule for every message word: a user symbol spelt like one is an answer.
        Arena arena;
        ScriptedBackend backend("timeout2+Invalid_x+Errors");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "message words inside longer names are not messages");
    }

    {
        Arena arena;
        ScriptedBackend backend("Error: x is not a valid argument");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");

        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "backend error", "a message word on its own is the message");
    }

    {
        Arena arena;
        ScriptedBackend backend("1.4142135");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Approximate;
        req.target = must_parse(arena, "2^(1/2)");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "approximate", "a decimal in the answer means approximate");
        t.check(r.usable(), "an approximate answer is usable but labelled");
    }

    {
        // The decimal check must look at the answer, not at everything the arena has ever held.
        Arena arena;
        must_parse(arena, "3.14");
        ScriptedBackend backend("2*x");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Differentiate;
        req.target = must_parse(arena, "x^2");
        req.variable = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact",
                "an unrelated decimal parsed earlier does not make this answer approximate");
    }

    {
        Arena arena;
        ScriptedBackend backend("Error: Bad Argument Value");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "backend error", "giac's own error text is a backend error");
        t.check(!r.usable(), "a backend error carries no value");
    }

    {
        Arena arena;
        ScriptedBackend backend("Time limit exceeded");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Integrate;
        req.target = must_parse(arena, "sin(x^2)");
        req.variable = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "timeout", "a time limit is a timeout, not a generic error");
        t.evidence("VER-014", r.tag == ResultTag::Timeout && !r.usable(),
                   "a backend timeout cannot become a usable verification value");
    }

    {
        Arena arena;
        ScriptedBackend backend("Not enough memory");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Factor;
        req.target = must_parse(arena, "x^2 + 2x + 1");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "resource failure", "an out of memory reply is a resource failure");
        t.evidence("VER-014", r.tag == ResultTag::ResourceFailure && !r.usable(),
                   "a backend resource failure cannot become a usable verification value");
    }

    {
        // The learner pressed escape while Giac was working. Giac reports that in the result string
        // the same way it reports a time limit, so the adapter has to read it there.
        Arena arena;
        ScriptedBackend backend("GIAC_ERROR: Stopped by user interruption.");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Integrate;
        req.target = must_parse(arena, "sin(x^2)");
        req.variable = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "cancelled",
                "a user interruption is a cancellation, not a malformed result");
        t.check(!r.usable(), "a cancelled reply carries no value");
        t.check(backend.terminal() && backend.terminal_tag() == ResultTag::Cancelled,
                "a cancelled backend is retired for the rest of the request");
    }

    {
        // Giac's other spelling names two causes and cannot say which, so it keeps the resource
        // reading it already had rather than claiming the learner asked to stop.
        Arena arena;
        ScriptedBackend backend("GIAC_ERROR: Stopped by user interruption or stack overflow.");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Integrate;
        req.target = must_parse(arena, "sin(x^2)");
        req.variable = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "resource failure",
                "an interruption giac cannot separate from a stack overflow stays a resource failure");
    }

    {
        Arena arena;
        ScriptedBackend backend("Unable to integrate");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Integrate;
        req.target = must_parse(arena, "exp(x^2)");
        req.variable = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unevaluated", "a refusal to evaluate is not a failure");
        t.evidence("VER-014", !r.usable(), "an unevaluated answer cannot pass verification");
    }

    {
        // MATH-015. A reader seeing only the tag reads a refusal and a missing backend as one nothing.
        Arena arena;
        ScriptedBackend exact("2*x");
        Adapter exact_adapter(arena, exact);
        Request differentiate;
        differentiate.op = Op::Differentiate;
        differentiate.target = must_parse(arena, "x^2");
        differentiate.variable = must_parse(arena, "x");
        const ResultForm elementary = result_form(exact_adapter.run(differentiate));

        ScriptedBackend decimal("1.4142135");
        Adapter decimal_adapter(arena, decimal);
        Request approximate;
        approximate.op = Op::Approximate;
        approximate.target = must_parse(arena, "2^(1/2)");
        const ResultForm numerical = result_form(decimal_adapter.run(approximate));

        ScriptedBackend refused("Unable to integrate");
        Adapter refused_adapter(arena, refused);
        Request integrate;
        integrate.op = Op::Integrate;
        integrate.target = must_parse(arena, "exp(x^2)");
        integrate.variable = must_parse(arena, "x");
        const ResultForm unevaluated = result_form(refused_adapter.run(integrate));

        ScriptedBackend foreign("[y]");
        Adapter foreign_adapter(arena, foreign);
        Request solve;
        solve.op = Op::Solve;
        solve.target = must_parse(arena, "x^2 = 2");
        solve.variable = must_parse(arena, "x");
        const Response wider = foreign_adapter.run(solve);
        const ResultForm unsupported = result_form(wider);

        ScriptedBackend broken("Error: Bad Argument Value");
        Adapter broken_adapter(arena, broken);
        Request simplify;
        simplify.op = Op::Simplify;
        simplify.target = must_parse(arena, "x");
        const ResultForm none = result_form(broken_adapter.run(simplify));

        t.equal(result_form_name(elementary), "elementary closed form",
                "an exact answer the engine can print is an elementary closed form");
        t.equal(result_form_name(none), "no result",
                "a backend error names no form, because no answer came back");
        t.equal(tag_name(wider.tag), "unsupported operation",
                "an unbounded root is refused rather than reported");
        t.evidence("MATH-015",
                   unevaluated == ResultForm::UnevaluatedExactForm &&
                       numerical == ResultForm::NumericalApproximation &&
                       unsupported == ResultForm::UnsupportedSymbolicForm &&
                       elementary != unevaluated && elementary != numerical &&
                       elementary != unsupported && unevaluated != numerical &&
                       unevaluated != unsupported && numerical != unsupported,
                   "unevaluated exact form, explicit numerical approximation and unsupported "
                   "operation are three distinct named result forms, each produced by the adapter "
                   "rather than supplied to it; no catalogued family declares a supported "
                   "special-function form, so the requirement's fourth form has nothing to name");
    }

    {
        Arena arena;
        ScriptedBackend backend("this ( is not ) an expression");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "malformed result",
                "text that will not parse never becomes a value");
        t.evidence("VER-014", !r.usable(), "a malformed answer cannot pass verification");
    }

    {
        Arena arena;
        ScriptedBackend backend("", false);
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Simplify;
        req.target = must_parse(arena, "x");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "backend error", "a backend that refuses the call reports so");
        t.evidence("VER-014", !r.usable(), "an unavailable backend cannot pass verification");
    }

    {
        Arena arena;
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Differentiate;
        req.target = must_parse(arena, "x^2");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation",
                "differentiate without a variable is refused before any call");
        t.check(adapter.call_count() == 0, "a refused request never reaches the backend");
    }

    {
        Arena arena;
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Differentiate;
        req.target = must_parse(arena, "x^2");
        req.variable = must_parse(arena, "2");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation",
                "a variable slot that is not a symbol is refused");
    }

    {
        Arena arena;
        ScriptedBackend backend("x^2/2");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Substitute;
        req.target = must_parse(arena, "y");
        req.variable = must_parse(arena, "y");
        req.replacement = must_parse(arena, "x^2/2");
        Response r = adapter.run(req);
        t.equal(backend.last_command, "subst(y,y=((x)^(2)*(2)^((-(1)))))",
                "substitute names the variable and the replacement, both printed by us");
    }

    {
        Arena arena;
        ScriptedBackend backend("11");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Dot;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "2")};
        req.argument_components = {must_parse(arena, "3"), must_parse(arena, "4")};
        Response r = adapter.run(req);
        t.equal(backend.last_command, "dotprod([1,2],[3,4])", "dot uses Giac's allowlisted operation");
        t.equal(print(arena, r.value), "11", "dot returns a scalar AST");
    }

    {
        Arena arena;
        ScriptedBackend backend("[0,0,1]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0"),
                                 must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(backend.last_command, "cross([1,0,0],[0,1,0])", "cross sends component lists to Giac");
        t.check(r.usable() && r.values.size() == 3, "a three component list becomes a usable vector");
        t.equal(print(arena, r.values[2]), "1", "the converted vector retains component order");

        ScriptedBackend zero_backend("0");
        Adapter zero_adapter(arena, zero_backend);
        Request check;
        check.op = Op::IsZero;
        check.target = arena.binary(Kind::Add, r.values[2], arena.unary(Kind::Neg, must_parse(arena, "1")));
        Response checked = zero_adapter.run(check);
        t.equal(tag_name(checked.tag), "exact", "a returned component can be cross checked with is_zero");
        t.equal(print(arena, checked.value), "0", "the cross check carries the backend's zero");
    }

    {
        Arena arena;
        ScriptedBackend backend("5");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Norm;
        req.target_components = {must_parse(arena, "3"), must_parse(arena, "4")};
        Response r = adapter.run(req);
        t.equal(backend.last_command, "l2norm([3,4])", "norm uses Giac's l2norm");
        t.equal(print(arena, r.value), "5", "norm returns a scalar AST");
    }

    {
        Arena arena;
        ScriptedBackend backend("[0.0,0,1]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0"),
                                 must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "approximate", "a decimal vector component marks the whole reply approximate");
    }

    {
        Arena arena;
        ScriptedBackend backend("[[0,0,1]]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0"),
                                 must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "a backend singleton wrapper around a cross vector is peeled");
        t.check(r.values.size() == 3, "the wrapped cross reply retains all three components");
    }

    {
        Arena arena;
        ScriptedBackend backend("[0,1]");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0"),
                                 must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "malformed result", "a wrong-rank cross reply is refused");
        t.check(!r.usable(), "a wrong-rank reply carries no vector");
    }

    {
        Arena arena;
        ScriptedBackend backend("[0,0,1");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0"),
                                 must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "malformed result", "an unclosed list reply is refused");
    }

    {
        Arena arena;
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cross;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation", "cross refuses a two component request");
        t.check(adapter.call_count() == 0, "a wrong-rank request never reaches Giac");
    }

    {
        Arena arena;
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Dot;
        req.target_components = {must_parse(arena, "1"), must_parse(arena, "0")};
        req.argument_components = {must_parse(arena, "0"), must_parse(arena, "1"),
                                   must_parse(arena, "0")};
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation", "dot refuses vectors of different rank");
        t.check(adapter.call_count() == 0, "a mismatched dot request never reaches Giac");
    }

    {
        Arena arena;
        ScriptedBackend backend("1/2");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Sin;
        req.target = must_parse(arena, "pi/6");
        Response r = adapter.run(req);
        t.equal(backend.last_command, "sin((pi*(6)^((-(1)))))", "sine stays inside the Giac allowlist");
        t.equal(print(arena, r.value), "(1 * (2^(-1)))", "sine returns a scalar AST");
    }

    {
        Arena arena;
        ScriptedBackend backend("sqrt(3)/2");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cos;
        req.target = must_parse(arena, "pi/6");
        Response r = adapter.run(req);
        t.equal(backend.last_command, "cos((pi*(6)^((-(1)))))", "cosine stays inside the Giac allowlist");
        t.check(r.usable(), "cosine returns a usable symbolic scalar");
    }

    {
        Arena arena;
        ScriptedBackend backend("1/2");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Cos;
        req.target = must_parse(arena, "pi/3");
        Response r = adapter.run(req);
        t.equal(backend.last_command, "cos((pi*(3)^((-(1)))))",
                "the differential cosine case preserves exact pi");
        t.equal(print(arena, r.value), "(1 * (2^(-1)))",
                "cosine of exact pi over three stays exact");
    }

    {
        Arena arena;
        ScriptedBackend backend("pi/4");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Atan2;
        req.target = must_parse(arena, "1");
        req.argument = must_parse(arena, "1");
        Response r = adapter.run(req);
        t.equal(backend.last_command, "atan2(1,1)", "atan2 carries y and x as two allowlisted arguments");
        t.check(r.usable(), "atan2 returns a usable exact angle expression");
    }

    {
        Arena arena;
        ScriptedBackend backend("π/4");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Atan2;
        req.target = must_parse(arena, "1");
        req.argument = must_parse(arena, "1");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "exact", "the real Giac Unicode pi reply remains exact");
        t.equal(print(arena, r.value), "(pi * (4^(-1)))",
                "the real Giac atan2 reply normalizes to the exact pi expression");
    }

    {
        Arena arena;
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request req;
        req.op = Op::Atan2;
        req.target = must_parse(arena, "1");
        Response r = adapter.run(req);
        t.equal(tag_name(r.tag), "unsupported operation", "atan2 refuses a missing x component");
        t.check(adapter.call_count() == 0, "an incomplete atan2 request never reaches Giac");
    }

    {
        // A reply the local limits cannot hold is StepCAS out of room, not a bad backend answer.
        std::string deep = "x";
        for (int i = 0; i < 200; ++i)
            deep = "(" + deep + "+1)";
        std::string tags;
        for (Op op : {Op::Simplify, Op::Solve, Op::Cross}) {
            Limits limits;
            limits.max_nodes = 40;
            limits.max_depth = 12;
            Arena arena(limits);
            ScriptedBackend backend(op == Op::Cross ? "[" + deep + "," + deep + "," + deep + "]"
                                                    : deep);
            Adapter adapter(arena, backend);
            Request request;
            request.op = op;
            request.target = arena.integer("1");
            request.variable = arena.symbol("x");
            if (op == Op::Cross) {
                request.target_components = {arena.integer("1"), arena.integer("2"),
                                             arena.integer("3")};
                request.argument_components = request.target_components;
            }
            const Response r = adapter.run(request);
            tags += (tags.empty() ? "" : " | ") + std::string(op_name(op)) + " " +
                    tag_name(r.tag) + " " + (arena.failed() ? "arena failed" : "arena ok");
        }
        t.equal(tags,
                "simplify resource failure arena ok | solve resource failure arena ok | "
                "cross resource failure arena ok",
                "every parse branch reports a local limit as a resource failure rather than "
                "blaming the backend, and none of them needs the arena to have failed to say so");
    }

    {
        // No request goes out once the arena is terminal, and that belongs where one is authorized.
        Arena arena;
        const NodeId target = arena.symbol("x");
        arena.fail(Status::SizeExceeded);
        ScriptedBackend backend("0");
        Adapter adapter(arena, backend);
        Request request;
        request.op = Op::IsZero;
        request.target = target;
        const Response r = adapter.run(request);
        t.check(backend.last_command.empty() && adapter.call_count() == 0,
                "a failed arena sends no command, whatever the call site remembered to check");
        t.equal(tag_name(r.tag), "resource failure",
                "and the refusal is named as the local limit it is");
        t.equal(r.detail, "size exceeded", "with the arena's own status as the detail");
    }
}

}  // namespace nps
