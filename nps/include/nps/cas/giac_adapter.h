#ifndef NPS_GIAC_ADAPTER_H
#define NPS_GIAC_ADAPTER_H

#include <string>
#include <vector>

#include "nps/core/ast.h"

namespace nps {

class MatrixRowSink;

// PRD section 12.1: every adapter response is tagged as exactly one of these and the caller has to
// handle each. The point of the list is that "it did not work" is several different situations and
// a derivation has to record which one.
enum class ResultTag : uint8_t {
    Exact,
    Conditional,
    Unevaluated,
    Approximate,
    Timeout,
    ResourceFailure,
    BackendError,
    MalformedResult,
    UnsupportedOperation,
    Cancelled,
};

const char *tag_name(ResultTag t);

// PRD section MATH-015: what kind of answer came back, which the tag alone does not say.
enum class ResultForm : uint8_t {
    NoResult,
    ElementaryClosedForm,
    UnevaluatedExactForm,
    NumericalApproximation,
    UnsupportedSymbolicForm,
};

const char *result_form_name(ResultForm f);

// The allowlist. Solver modules name an operation from this enum; they never hand the adapter a
// command string, which is what section 12.3 forbids.
enum class Op : uint8_t {
    Simplify,
    Expand,
    Factor,
    Solve,
    Differentiate,
    Integrate,
    Limit,
    Substitute,
    Approximate,
    IsZero,
    Dot,
    Cross,
    Norm,
    Sin,
    Cos,
    Atan2,
    Ref,
    Rref,
};

const char *op_name(Op op);

// FiniteSolutions stores every root in values, including the exact empty set.
enum class ResultShape : uint8_t { Scalar, Vector, FiniteSolutions, Matrix };

struct Response {
    ResultTag tag = ResultTag::BackendError;
    ResultShape shape = ResultShape::Scalar;
    NodeId value = kNoNode;
    std::vector<NodeId> values;
    std::string raw;
    std::string detail;

    // A matrix keeps its row lists in value and never yields a scalar.
    NodeId single_value() const {
        if (!usable())
            return kNoNode;
        if (shape == ResultShape::Scalar)
            return value;
        return shape == ResultShape::FiniteSolutions && values.size() == 1 ? values[0] : kNoNode;
    }

    bool usable() const {
        if (shape == ResultShape::Matrix)
            return value != kNoNode && values.empty() &&
                   (tag == ResultTag::Exact || tag == ResultTag::Approximate);
        if (shape == ResultShape::FiniteSolutions)
            return tag == ResultTag::Exact || tag == ResultTag::Approximate;
        return (shape == ResultShape::Scalar ? value != kNoNode : !values.empty()) &&
               (tag == ResultTag::Exact || tag == ResultTag::Conditional ||
                tag == ResultTag::Approximate);
    }
};

// Derived rather than stored, so a response cannot carry a form its tag contradicts.
ResultForm result_form(const Response &r);

struct Request {
    Op op;
    NodeId target = kNoNode;
    NodeId variable = kNoNode;
    NodeId replacement = kNoNode;
    NodeId point = kNoNode;
    NodeId argument = kNoNode;
    std::vector<NodeId> target_components;
    std::vector<NodeId> argument_components;
    NodeId lower = kNoNode;
    NodeId upper = kNoNode;
    int direction = 0;
};

// Exact admits the request. Other tags refuse it before backend work.
ResultTag matrix_request_status(const Arena &arena, const Request &request, std::string *why);
ResultTag matrix_result_status(const Arena &arena, const Request &request, NodeId matrix,
                               std::string *why);

// What a backend that speaks the engine's own types answers with. Separate from Response because a
// typed backend fills in the value and the tag itself, having seen the result as data rather than
// as prose, and never has a raw string to report.
struct TypedResult {
    ResultTag tag = ResultTag::BackendError;
    ResultShape shape = ResultShape::Scalar;
    NodeId value = kNoNode;
    std::vector<NodeId> values;
    std::string detail;
    // False where the tag is our limits refusing an answer the backend gave perfectly well, which
    // reads the same as the backend giving up and must not retire it. Adapter::dispatch makes the
    // same refusal for a solution collection over the node limit, on the side of the conversion that
    // never latched, so the two now agree.
    // True by default because this struct exists to relay what the backend said, not because either
    // default is safe: a forgotten mark retires a working backend, and a forgotten one the other way
    // shows a timed-out Giac's answer to a simpler follow-up in place of the native result.
    bool from_backend = true;
};

// What the device supplies through luagiac and the host supplies from a script. Keeping it abstract
// is what lets the adapter's contract be tested without a calculator, which is most of it.
class Backend {
  public:
    virtual ~Backend() {}
    // This is the one place text reaches a symbolic engine, so the command must come from
    // build_command below and never from a caller. Giac evaluates what it is given, including file
    // operations, which is why section 17 bans forwarding user input here.
    // Returns false for a backend level failure, with why in *error. A true return means the
    // backend answered, not that the answer is any good.
    virtual bool eval(const std::string &command, std::string *out, std::string *error) = 0;

    // A backend linked into this program can take the request as data and give the answer back as
    // data, with no command text built on the way in and none parsed on the way out. False means
    // this backend has no such path and the adapter falls back to eval, which is what a backend
    // reached over a Lua call or a script has to do.
    virtual bool typed(const Request &request, Arena &arena, TypedResult *out) {
        (void)request;
        (void)arena;
        (void)out;
        return false;
    }

    virtual bool matrix_steps(const Request &request, Arena &arena, MatrixRowSink &sink,
                              TypedResult *out) {
        (void)request;
        (void)arena;
        (void)sink;
        (void)out;
        return false;
    }

    // One request reaches Giac through several engines and a fresh Adapter each time, so the one
    // object all of them share is where "it already gave up" has to live. A backend is constructed
    // per bridge call, at lua_module.cc:491 and its siblings, so the latch dies with the request.
    bool terminal() const { return terminal_tag_ != ResultTag::Exact; }
    ResultTag terminal_tag() const { return terminal_tag_; }
    const std::string &terminal_detail() const { return terminal_detail_; }

    void latch_terminal(ResultTag tag, const std::string &detail) {
        if (terminal())
            return;
        terminal_tag_ = tag;
        terminal_detail_ = detail;
    }

    // Giac's in_eval spelling names an interruption and a stack overflow as alternatives and cannot
    // say which one it was, so the adapter reading that text is one fact short. The caller that
    // polls the keypad holds that fact and lends it here. Same object as the terminal latch, so it
    // reaches every adapter the request builds.
    void watch_stop(bool (*poll)(void *), void *context) {
        stop_poll_ = poll;
        stop_poll_context_ = context;
    }

    // Sticky, because one request reaches Giac through several adapters and a key released between
    // two of them must not give the same stop two different readings.
    bool stop_requested() {
        if (!stop_seen_ && stop_poll_ && stop_poll_(stop_poll_context_))
            stop_seen_ = true;
        return stop_seen_;
    }

  private:
    ResultTag terminal_tag_ = ResultTag::Exact;
    std::string terminal_detail_;
    bool (*stop_poll_)(void *) = 0;
    void *stop_poll_context_ = 0;
    bool stop_seen_ = false;
};

class Adapter {
  public:
    Adapter(Arena &arena, Backend &backend) : arena_(arena), backend_(backend) {}

    Response run(const Request &request);
    Response matrix_steps(const Request &request, MatrixRowSink &sink);

    size_t call_count() const { return call_count_; }

  private:
    Response run(const Request &request, MatrixRowSink *sink);
    Response dispatch(const Request &request, MatrixRowSink *sink);
    void note_backend_reply(ResultTag tag, const std::string &detail);
    bool build_command(const Request &request, std::string *out, std::string *why) const;
    Response interpret(const Request &request, const std::string &raw);
    bool has_decimal(NodeId id) const;
    void mark_decimals(NodeId id);

    Arena &arena_;
    Backend &backend_;
    size_t call_count_ = 0;
};

}  // namespace nps

#endif
