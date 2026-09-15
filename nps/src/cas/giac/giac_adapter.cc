#include "nps/cas/giac_adapter.h"

#include "nps/core/canonical.h"
#include "nps/core/matrix.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

namespace nps {
namespace {

std::string trimmed(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r'))
        --e;
    return s.substr(b, e - b);
}

bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Giac's messages are whole words in the answer text. Match them as words, so a user symbol that
// merely contains the letters, like undef_val or timeout2, is not read as a message about it.
bool mentions(const std::string &text, const char *phrase) {
    const size_t len = std::char_traits<char>::length(phrase);
    size_t pos = text.find(phrase);
    while (pos != std::string::npos) {
        const bool before_ok = pos == 0 || !is_ident_char(text[pos - 1]);
        const size_t after = pos + len;
        const bool after_ok = after >= text.size() || !is_ident_char(text[after]);
        if (before_ok && after_ok)
            return true;
        pos = text.find(phrase, pos + len);
    }
    return false;
}

bool scalar_root(const Arena &arena, NodeId id, NodeId unknown, NodeId target) {
    if (id == kNoNode || id >= arena.node_count())
        return false;
    const Node &node = arena.at(id);
    if (node.kind == Kind::Symbol) {
        const std::string &name = arena.text(id);
        return id != unknown && name != "i" && name != "infinity" && name != "inf" &&
               name != "undef" && (name == "pi" || depends_on(arena, target, id));
    }
    if (node.kind == Kind::Call) {
        const std::string &name = arena.text(id);
        if (name == "when" || name == "piecewise" || name == "rootof" || name == "list")
            return false;
    } else if (node.kind != Kind::Integer && node.kind != Kind::Decimal &&
               node.kind != Kind::Add && node.kind != Kind::Mul && node.kind != Kind::Pow &&
               node.kind != Kind::Neg) {
        return false;
    }
    for (NodeId child : arena.children(node))
        if (!scalar_root(arena, child, unknown, target))
            return false;
    return !has_undefined_form(arena, id);
}

}  // namespace

const char *tag_name(ResultTag t) {
    switch (t) {
        case ResultTag::Exact: return "exact";
        case ResultTag::Conditional: return "conditional";
        case ResultTag::Unevaluated: return "unevaluated";
        case ResultTag::Approximate: return "approximate";
        case ResultTag::Timeout: return "timeout";
        case ResultTag::ResourceFailure: return "resource failure";
        case ResultTag::BackendError: return "backend error";
        case ResultTag::MalformedResult: return "malformed result";
        case ResultTag::UnsupportedOperation: return "unsupported operation";
        case ResultTag::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char *result_form_name(ResultForm f) {
    switch (f) {
        case ResultForm::NoResult: return "no result";
        case ResultForm::ElementaryClosedForm: return "elementary closed form";
        case ResultForm::UnevaluatedExactForm: return "unevaluated exact form";
        case ResultForm::NumericalApproximation: return "numerical approximation";
        case ResultForm::UnsupportedSymbolicForm: return "unsupported symbolic form";
    }
    return "no result";
}

ResultForm result_form(const Response &r) {
    if (r.tag == ResultTag::Unevaluated)
        return ResultForm::UnevaluatedExactForm;
    if (r.tag == ResultTag::UnsupportedOperation)
        return ResultForm::UnsupportedSymbolicForm;
    if (!r.usable())
        return ResultForm::NoResult;
    return r.tag == ResultTag::Approximate ? ResultForm::NumericalApproximation
                                           : ResultForm::ElementaryClosedForm;
}

const char *op_name(Op op) {
    switch (op) {
        case Op::Simplify: return "simplify";
        case Op::Expand: return "expand";
        case Op::Factor: return "factor";
        case Op::Solve: return "solve";
        case Op::Differentiate: return "differentiate";
        case Op::Integrate: return "integrate";
        case Op::Limit: return "limit";
        case Op::Substitute: return "substitute";
        case Op::Approximate: return "approximate";
        case Op::IsZero: return "is_zero";
        case Op::Dot: return "dot";
        case Op::Cross: return "cross";
        case Op::Norm: return "norm";
        case Op::Sin: return "sin";
        case Op::Cos: return "cos";
        case Op::Atan2: return "atan2";
        case Op::Ref: return "ref";
        case Op::Rref: return "rref";
    }
    return "unknown";
}

ResultTag matrix_request_status(const Arena &arena, const Request &request, std::string *why) {
    if (arena.failed()) {
        *why = "the matrix request arena has exhausted its resources";
        return ResultTag::ResourceFailure;
    }
    if ((request.op != Op::Ref && request.op != Op::Rref) ||
        request.variable != kNoNode || request.replacement != kNoNode ||
        request.point != kNoNode || request.argument != kNoNode ||
        request.lower != kNoNode || request.upper != kNoNode || request.direction != 0 ||
        !request.target_components.empty() || !request.argument_components.empty()) {
        *why = "ref and rref take one matrix without options";
        return ResultTag::UnsupportedOperation;
    }
    const auto matrix = MatrixView::from(arena, request.target);
    if (!matrix || matrix->rows() > 4 || matrix->columns() > 6) {
        *why = "ref and rref require a nonempty rectangular matrix of at most 4 by 6";
        return ResultTag::UnsupportedOperation;
    }
    if (!read_exact_matrix(arena, request.target)) {
        *why = "the matrix requires exact rational arithmetic within checked integer bounds and no approximate provenance";
        return ResultTag::UnsupportedOperation;
    }
    return ResultTag::Exact;
}

namespace {
bool approximate_matrix_cell(const Arena &arena, NodeId cell) {
    const Node &node = arena.at(cell);
    if (node.kind == Kind::Decimal) {
        Rational value;
        return rational_from_text(arena.text(cell), &value);
    }
    return node.kind == Kind::Neg && arena.children(node).size() == 1 &&
           approximate_matrix_cell(arena, arena.children(node)[0]);
}
}

ResultTag matrix_result_status(const Arena &arena, const Request &request, NodeId root,
                               std::string *why) {
    if (arena.failed()) {
        *why = "the matrix result exhausted the shared arena limits";
        return ResultTag::ResourceFailure;
    }
    const auto input = MatrixView::from(arena, request.target);
    const auto output = MatrixView::from(arena, root);
    if (!input || !output || output->rows() != input->rows() ||
        output->columns() != input->columns()) {
        *why = "the backend matrix dimensions do not match the request";
        return ResultTag::MalformedResult;
    }
    if (read_exact_matrix(arena, root))
        return ResultTag::Exact;
    for (size_t row = 0; row < output->rows(); ++row) {
        for (size_t column = 0; column < output->columns(); ++column) {
            const NodeId cell = output->cell(row, column);
            Rational value;
            if (read_matrix_rational(arena, cell, &value))
                continue;
            if (approximate_matrix_cell(arena, cell)) {
                continue;
            }
            *why = "the backend matrix contains an unsupported rational cell";
            return ResultTag::UnsupportedOperation;
        }
    }
    return ResultTag::Approximate;
}

bool Adapter::build_command(const Request &request, std::string *out, std::string *why) const {
    if ((request.lower != kNoNode || request.upper != kNoNode) &&
        (request.op != Op::Integrate || request.lower == kNoNode || request.upper == kNoNode)) {
        *why = "only integration accepts bounds and both bounds are required";
        return false;
    }
    if (request.direction < -1 || request.direction > 1 ||
        (request.direction != 0 && request.op != Op::Limit)) {
        *why = "only limits accept a direction, which must be -1, 0 or 1";
        return false;
    }
    if (request.op == Op::Ref || request.op == Op::Rref) {
        *out = std::string(op_name(request.op)) + "(" + print_giac(arena_, request.target) + ")";
        return true;
    }
    for (NodeId expression : {request.target, request.variable, request.replacement,
                              request.point, request.argument, request.lower, request.upper}) {
        if (contains_list(arena_, expression)) {
            *why = "this backend operation requires scalar expressions";
            return false;
        }
    }
    if (request.op == Op::Dot || request.op == Op::Cross || request.op == Op::Norm) {
        const size_t rank = request.target_components.size();
        if (rank != 2 && rank != 3) {
            *why = std::string(op_name(request.op)) + " needs a vector with two or three components";
            return false;
        }
        if (request.op == Op::Cross && rank != 3) {
            *why = "cross needs two vectors with three components";
            return false;
        }
        if (request.op != Op::Norm && request.argument_components.size() != rank) {
            *why = std::string(op_name(request.op)) + " needs two vectors of the same rank";
            return false;
        }
        if (request.op == Op::Norm && !request.argument_components.empty()) {
            *why = "norm needs one vector";
            return false;
        }
        for (NodeId component : request.target_components) {
            if (component == kNoNode) {
                *why = "a vector component is missing";
                return false;
            }
            if (contains_list(arena_, component)) {
                *why = "vector components must be scalar expressions";
                return false;
            }
        }
        for (NodeId component : request.argument_components) {
            if (component == kNoNode) {
                *why = "a vector component is missing";
                return false;
            }
            if (contains_list(arena_, component)) {
                *why = "vector components must be scalar expressions";
                return false;
            }
        }

        const auto component_list = [this](const std::vector<NodeId> &components) {
            std::string text = "[";
            for (size_t i = 0; i < components.size(); ++i) {
                const std::string component = print_giac(arena_, components[i]);
                if (component.empty())
                    return std::string();
                if (i)
                    text += ",";
                text += component;
            }
            return text + "]";
        };
        const std::string first = component_list(request.target_components);
        if (first.empty()) {
            *why = "a vector component has no faithful Giac syntax";
            return false;
        }
        if (request.op == Op::Norm) {
            *out = "l2norm(" + first + ")";
        } else {
            const std::string second = component_list(request.argument_components);
            if (second.empty()) {
                *why = "a vector component has no faithful Giac syntax";
                return false;
            }
            const char *fn = request.op == Op::Dot ? "dotprod" : "cross";
            *out = std::string(fn) + "(" + first + "," + second + ")";
        }
        return true;
    }
    if (request.target == kNoNode) {
        *why = "no target expression";
        return false;
    }
    const std::string target = print_giac(arena_, request.target);
    if (target.empty()) {
        *why = "the target expression has no faithful Giac syntax";
        return false;
    }

    switch (request.op) {
        case Op::Simplify: *out = "simplify(" + target + ")"; return true;
        case Op::Expand: *out = "expand(" + target + ")"; return true;
        case Op::Factor: *out = "factor(" + target + ")"; return true;
        case Op::Approximate: *out = "evalf(" + target + ")"; return true;
        case Op::IsZero: *out = "simplify(" + target + ")"; return true;
        case Op::Sin: *out = "sin(" + target + ")"; return true;
        case Op::Cos: *out = "cos(" + target + ")"; return true;
        case Op::Atan2: {
            if (request.argument == kNoNode) {
                *why = "atan2 needs y and x components";
                return false;
            }
            const std::string argument = print_giac(arena_, request.argument);
            if (argument.empty()) {
                *why = "the argument has no faithful Giac syntax";
                return false;
            }
            *out = "atan2(" + target + "," + argument + ")";
            return true;
        }
        case Op::Dot:
        case Op::Cross:
        case Op::Norm: break;

        case Op::Solve:
        case Op::Differentiate:
        case Op::Integrate: {
            if (request.variable == kNoNode) {
                *why = std::string(op_name(request.op)) + " needs a variable";
                return false;
            }
            if (arena_.at(request.variable).kind != Kind::Symbol) {
                *why = "the variable has to be a symbol";
                return false;
            }
            const std::string var = print_giac(arena_, request.variable);
            const char *fn = request.op == Op::Solve            ? "solve"
                             : request.op == Op::Differentiate  ? "diff"
                                                                : "integrate";
            *out = std::string(fn) + "(" + target + "," + var + ")";
            if (request.op == Op::Integrate && request.lower != kNoNode) {
                const std::string lower = print_giac(arena_, request.lower);
                const std::string upper = print_giac(arena_, request.upper);
                if (lower.empty() || upper.empty()) {
                    *why = "the integral bounds have no faithful Giac syntax";
                    return false;
                }
                *out = std::string(fn) + "(" + target + "," + var + "," + lower + "," + upper + ")";
            }
            return true;
        }

        case Op::Limit: {
            if (request.variable == kNoNode || request.point == kNoNode) {
                *why = "limit needs a variable and a point";
                return false;
            }
            if (arena_.at(request.variable).kind != Kind::Symbol) {
                *why = "the variable has to be a symbol";
                return false;
            }
            const std::string point = print_giac(arena_, request.point);
            if (point.empty()) {
                *why = "the limit point has no faithful Giac syntax";
                return false;
            }
            *out = "limit(" + target + "," + print_giac(arena_, request.variable) + "," + point + ")";
            if (request.direction != 0)
                *out = "limit(" + target + "," + print_giac(arena_, request.variable) + "," + point +
                       "," + std::to_string(request.direction) + ")";
            return true;
        }

        case Op::Substitute: {
            if (request.variable == kNoNode || request.replacement == kNoNode) {
                *why = "substitute needs a variable and a replacement";
                return false;
            }
            if (arena_.at(request.variable).kind != Kind::Symbol) {
                *why = "the variable has to be a symbol";
                return false;
            }
            const std::string replacement = print_giac(arena_, request.replacement);
            if (replacement.empty()) {
                *why = "the replacement has no faithful Giac syntax";
                return false;
            }
            *out = "subst(" + target + "," + print_giac(arena_, request.variable) + "=" + replacement + ")";
            return true;
        }
        case Op::Ref:
        case Op::Rref:
            break;
    }
    *why = "operation is not in the allowlist";
    return false;
}

bool Adapter::has_decimal(NodeId id) const {
    return nps::has_decimal(arena_, id);
}

// Unwrap only a delimiter pair enclosing the complete backend result.
static bool list_body(const std::string &text, std::string *body, bool allow_typed_list = false) {
    const size_t first = allow_typed_list && text.compare(0, 5, "list[") == 0 ? 4 : 0;
    if (text.size() < first + 2)
        return false;
    const char open = text[first];
    const char close = text[text.size() - 1];
    if (!((open == '[' && close == ']') || (open == '{' && close == '}')))
        return false;
    std::string closers;
    for (size_t i = first; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[' || c == '{' || c == '(')
            closers.push_back(c == '[' ? ']' : c == '{' ? '}' : ')');
        else if (c == ']' || c == '}' || c == ')') {
            if (closers.empty() || closers.back() != c)
                return false;
            closers.pop_back();
        }
        if (closers.empty() && i + 1 != text.size())
            return false;
    }
    if (!closers.empty())
        return false;
    body->assign(text, first + 1, text.size() - first - 2);
    return true;
}

static size_t top_level_elements(const std::string &body) {
    if (body.empty())
        return 0;
    size_t depth = 0;
    size_t elements = 1;
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '[' || c == '{' || c == '(') {
            ++depth;
        } else if (c == ']' || c == '}' || c == ')') {
            if (depth)
                --depth;
        } else if (c == ',' && depth == 0) {
            ++elements;
        }
    }
    return elements;
}

static bool split_top_level(const std::string &body, std::vector<std::string> *elements) {
    elements->clear();
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '[' || c == '{' || c == '(') {
            ++depth;
        } else if (c == ']' || c == '}' || c == ')') {
            if (depth == 0)
                return false;
            --depth;
        } else if (c == ',' && depth == 0) {
            elements->push_back(trimmed(body.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (depth != 0)
        return false;
    elements->push_back(trimmed(body.substr(start)));
    return true;
}

Response Adapter::interpret(const Request &request, const std::string &raw) {
    Response r;
    r.raw = raw;

    const std::string text = trimmed(raw);
    if (text.empty()) {
        r.tag = ResultTag::MalformedResult;
        r.detail = "the backend answered with nothing";
        return r;
    }

    // Giac reports its own trouble in the result string rather than out of band, so these are read
    // before any attempt to parse. Order matters: a timeout message also contains "Error".
    if (mentions(text, "Time limit") || mentions(text, "timeout") || mentions(text, "Timeout")) {
        r.tag = ResultTag::Timeout;
        r.detail = text;
        note_backend_reply(r.tag, r.detail);
        return r;
    }
    // The learner asked to stop, which is its own terminal condition rather than the backend
    // failing. Giac's other interruption message names a stack overflow as an alternative cause and
    // cannot say which happened, so a message wearing both readings is decided by the caller's
    // keypad poll: it says the learner asked, or nothing does and the resource reading stands.
    const bool interruption =
        mentions(text, "user interruption") || mentions(text, "Interrupted by user");
    const bool resource = mentions(text, "Not enough memory") || mentions(text, "stack overflow") ||
                          mentions(text, "Recursion");
    if (resource && !(interruption && backend_.stop_requested())) {
        r.tag = ResultTag::ResourceFailure;
        r.detail = text;
        note_backend_reply(r.tag, r.detail);
        return r;
    }
    if (interruption) {
        r.tag = ResultTag::Cancelled;
        r.detail = text;
        note_backend_reply(r.tag, r.detail);
        return r;
    }
    if (mentions(text, "Error") || mentions(text, "error:") || mentions(text, "Invalid")) {
        r.tag = ResultTag::BackendError;
        r.detail = text;
        return r;
    }
    if (mentions(text, "Unable to") || mentions(text, "undef")) {
        r.tag = ResultTag::Unevaluated;
        r.detail = text;
        return r;
    }

    if (request.op == Op::Ref || request.op == Op::Rref) {
        r.shape = ResultShape::Matrix;
        const ParseResult parsed = parse(arena_, text);
        if (!parsed.ok()) {
            r.tag = parsed.status == Status::SyntaxError ? ResultTag::MalformedResult
                                                        : ResultTag::ResourceFailure;
            r.detail = "the backend matrix could not be parsed within the shared limits";
            return r;
        }
        r.value = parsed.root;
        r.tag = has_decimal(r.value) ? ResultTag::Approximate : ResultTag::Exact;
        return r;
    }

    if (request.op == Op::Cross) {
        r.shape = ResultShape::Vector;
        std::string vector_text = text;
        std::string body;
        std::vector<std::string> elements;
        // Real Giac returns Cross as [[x,y,z]], so peel singleton list wrappers.
        while (list_body(vector_text, &body) && split_top_level(body, &elements) &&
               elements.size() == 1) {
            vector_text = elements[0];
        }
        if (!list_body(vector_text, &body) || !split_top_level(body, &elements) ||
            elements.size() != 3) {
            r.tag = ResultTag::MalformedResult;
            r.detail = "cross returned something other than a three component vector";
            return r;
        }
        bool approximate = false;
        for (const std::string &element : elements) {
            ParseResult parsed = parse(arena_, element);
            if (!parsed.ok()) {
                r.values.clear();
                r.tag = resource_status(parsed.status) ? ResultTag::ResourceFailure
                                                       : ResultTag::MalformedResult;
                r.detail = std::string(status_name(parsed.status)) + ": " + parsed.message;
                return r;
            }
            approximate = approximate || has_decimal(parsed.root);
            r.values.push_back(parsed.root);
        }
        r.tag = approximate ? ResultTag::Approximate : ResultTag::Exact;
        return r;
    }

    if (request.op == Op::Solve) {
        r.shape = ResultShape::FiniteSolutions;
        if (text.size() > arena_.limits().max_input_bytes) {
            r.tag = ResultTag::ResourceFailure;
            r.detail = "the solution collection exceeds the shared input limit";
            return r;
        }
        std::string inner = text;
        std::string body;
        std::vector<std::string> elements;
        while (list_body(inner, &body, true)) {
            if (trimmed(body).empty()) {
                r.tag = ResultTag::Exact;
                return r;
            }
            if (!split_top_level(body, &elements)) {
                r.tag = ResultTag::MalformedResult;
                r.detail = "the solution collection has unbalanced delimiters";
                return r;
            }
            std::string nested;
            if (elements.size() != 1 || !list_body(elements[0], &nested, true))
                break;
            inner = elements[0];
        }
        if (elements.empty())
            elements.push_back(inner);
        if (elements.size() > arena_.limits().max_nodes) {
            r.tag = ResultTag::ResourceFailure;
            r.detail = "the solution collection exceeds the shared node limit";
            return r;
        }
        bool approximate = false;
        for (const std::string &element : elements) {
            const ParseResult parsed = parse(arena_, element);
            if (!parsed.ok()) {
                r.values.clear();
                // The parse already answered this. The arena never fails on a depth refusal.
                r.tag = resource_status(parsed.status) ? ResultTag::ResourceFailure
                                                       : ResultTag::MalformedResult;
                r.detail = std::string(status_name(parsed.status)) + ": " + parsed.message;
                return r;
            }
            approximate = approximate || has_decimal(parsed.root);
            r.values.push_back(parsed.root);
        }
        r.tag = approximate ? ResultTag::Approximate : ResultTag::Exact;
        return r;
    }

    // Scalar operations peel singleton wrappers without choosing from a wider collection.
    std::string inner = text;
    std::string body;
    while (list_body(inner, &body)) {
        const std::string trimmed_body = trimmed(body);
        const size_t elements = top_level_elements(trimmed_body);
        if (elements != 1) {
            r.tag = ResultTag::UnsupportedOperation;
            r.detail = elements == 0
                           ? "the backend returned an empty solution set"
                           : "the backend returned more than one solution and this carries one";
            return r;
        }
        inner = trimmed_body;
    }

    ParseResult parsed = parse(arena_, inner);
    if (!parsed.ok()) {
        // Section 12.1: text that will not become the expected typed representation never enters a
        // derivation state. Refusing here is the enforcement of that, not a convenience.
        r.tag = resource_status(parsed.status) ? ResultTag::ResourceFailure
                                               : ResultTag::MalformedResult;
        r.detail = std::string(status_name(parsed.status)) + ": " + parsed.message;
        return r;
    }

    r.value = parsed.root;
    // A decimal anywhere in the answer means the backend approximated, whatever was asked, and the
    // caller has to know that before it puts the value in a step. Walk the result, not the arena:
    // the arena holds every expression this session has parsed, so scanning it marks a result
    // approximate because something unrelated earlier had a decimal in it.
    r.tag = has_decimal(parsed.root) ? ResultTag::Approximate : ResultTag::Exact;
    return r;
}

Response Adapter::run(const Request &request) {
    return run(request, nullptr);
}

Response Adapter::matrix_steps(const Request &request, MatrixRowSink &sink) {
    return run(request, &sink);
}

// A terminal resource status is not a state to call a backend from, and the backend that reported
// one is still the backend the next engine in the same request is handed.
Response Adapter::run(const Request &request, MatrixRowSink *sink) {
    if (backend_.terminal()) {
        Response latched;
        latched.tag = backend_.terminal_tag();
        latched.detail = backend_.terminal_detail();
        return latched;
    }
    return dispatch(request, sink);
}

// Only what the backend said about itself. Dispatch also answers ResourceFailure wherever it refuses
// a reply on our own limits rather than on Giac's report, including our parser running out of room
// reading it, and latching those would retire a working backend over an arena's trouble.
void Adapter::note_backend_reply(ResultTag tag, const std::string &detail) {
    if (tag == ResultTag::Cancelled || tag == ResultTag::ResourceFailure ||
        tag == ResultTag::Timeout)
        backend_.latch_terminal(tag, detail);
}

Response Adapter::dispatch(const Request &request, MatrixRowSink *sink) {
    Response r;
    if (arena_.failed()) {
        r.tag = ResultTag::ResourceFailure;
        r.detail = status_name(arena_.status());
        return r;
    }
    std::string command;
    std::string why;
    const bool matrix_op = request.op == Op::Ref || request.op == Op::Rref;
    if (sink && !matrix_op) {
        r.tag = ResultTag::UnsupportedOperation;
        r.detail = "row steps require a Ref or Rref matrix request";
        return r;
    }
    if (matrix_op) {
        r.shape = ResultShape::Matrix;
        r.tag = matrix_request_status(arena_, request, &r.detail);
        if (r.tag != ResultTag::Exact)
            return r;
    }
    // Checked before the typed path too, because the allowlist and the argument rules are the
    // adapter's and must not become two lists that can disagree.
    if (!build_command(request, &command, &why)) {
        r.tag = ResultTag::UnsupportedOperation;
        r.detail = why;
        return r;
    }

    TypedResult typed;
    if (sink ? backend_.matrix_steps(request, arena_, *sink, &typed) :
               backend_.typed(request, arena_, &typed)) {
        ++call_count_;
        r.tag = typed.tag;
        r.shape = typed.shape;
        r.value = typed.value;
        r.values = typed.values;
        r.detail = typed.detail;
        if (typed.from_backend)
            note_backend_reply(r.tag, r.detail);
    } else if (sink) {
        r.tag = ResultTag::BackendError;
        r.detail = "the backend does not provide matrix row steps";
        return r;
    } else {
        std::string out;
        std::string error;
        ++call_count_;
        if (!backend_.eval(command, &out, &error)) {
            r.tag = ResultTag::BackendError;
            r.detail = error.empty() ? "the backend refused the call" : error;
            r.raw = out;
            return r;
        }
        r = interpret(request, out);
    }

    if (matrix_op) {
        if (r.tag == ResultTag::Exact || r.tag == ResultTag::Approximate) {
            if (r.shape != ResultShape::Matrix || !r.values.empty()) {
                r.tag = ResultTag::MalformedResult;
                r.detail = "the backend did not return a matrix in one ordered row-list AST";
            } else {
                const ResultTag checked = matrix_result_status(arena_, request, r.value, &r.detail);
                if (checked != ResultTag::Exact)
                    r.tag = checked;
            }
        }
        if (r.tag != ResultTag::Exact && r.tag != ResultTag::Approximate) {
            r.value = kNoNode;
            r.values.clear();
        }
    } else if (r.usable() || (r.shape == ResultShape::Matrix &&
                             (r.tag == ResultTag::Exact || r.tag == ResultTag::Approximate ||
                              r.tag == ResultTag::Conditional))) {
        bool collection = r.shape == ResultShape::Matrix || contains_list(arena_, r.value);
        for (NodeId value : r.values)
            collection = collection || contains_list(arena_, value);
        if (collection) {
            r.tag = ResultTag::MalformedResult;
            r.detail = "the backend returned a collection where a scalar expression was required";
            r.value = kNoNode;
            r.values.clear();
        }
    }
    if (request.op == Op::Solve && r.usable()) {
        if (r.shape != ResultShape::FiniteSolutions || r.value != kNoNode) {
            r.tag = ResultTag::MalformedResult;
            r.detail = "solve did not return a finite solution collection";
        } else if (r.values.size() > arena_.limits().max_nodes || arena_.failed()) {
            r.tag = ResultTag::ResourceFailure;
            r.detail = "the solution collection exceeds the shared node limit";
        } else {
            for (NodeId root : r.values) {
                if (!scalar_root(arena_, root, request.variable, request.target)) {
                    r.tag = ResultTag::UnsupportedOperation;
                    r.detail = "the solution collection contains an unsupported or unbounded root";
                    break;
                }
            }
        }
    }
    if (request.op == Op::Solve && !r.usable()) {
        r.value = kNoNode;
        r.values.clear();
    }

    // MATH-009. A decimal the engine returned is its approximation, on either path.
    mark_decimals(r.value);
    for (NodeId v : r.values)
        mark_decimals(v);
    return r;
}

void Adapter::mark_decimals(NodeId id) {
    // A typed backend's reference is unchecked data, so it is ranged rather than trusted.
    if (id == kNoNode || id >= arena_.node_count())
        return;
    arena_.any_node(id, [this](NodeId current) {
        if (arena_.at(current).kind == Kind::Decimal)
            arena_.mark_approximate(current);
        return false;
    });
}

}  // namespace nps
