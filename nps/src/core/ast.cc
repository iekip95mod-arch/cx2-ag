#include "nps/core/ast.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace nps {

const char *status_name(Status s) {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::DepthExceeded: return "depth exceeded";
        case Status::SizeExceeded: return "size exceeded";
        case Status::InputTooLong: return "input too long";
        case Status::SyntaxError: return "syntax error";
    }
    return "unknown";
}

bool resource_status(Status s) {
    return s == Status::DepthExceeded || s == Status::SizeExceeded || s == Status::InputTooLong;
}

uint32_t Arena::intern(const std::string &s) {
    auto it = string_ids_.find(s);
    if (it != string_ids_.end())
        return it->second;
    uint32_t id = static_cast<uint32_t>(strings_.size());
    strings_.push_back(s);
    string_ids_.emplace(s, id);
    return id;
}

static void append_u32(std::string &out, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (i * 8)) & 0xFFu));
}

NodeId Arena::add_node(Node n, const std::vector<NodeId> &args) {
    if (failed())
        return kNoNode;

    std::string key;
    key.push_back(static_cast<char>(n.kind));
    append_u32(key, n.text);
    n.has_list = n.kind == Kind::List;
    for (NodeId a : args) {
        append_u32(key, a);
        n.has_list = n.has_list || nodes_[a].has_list;
        n.size += std::min(nodes_[a].size, std::numeric_limits<uint32_t>::max() - n.size);
    }

    // A node already held costs nothing, so the limits apply to a new one only, and so does the
    // pool: the children are appended below the lookup rather than above it.
    auto it = interned_.find(key);
    if (it != interned_.end())
        return it->second;

    if (n.depth > limits_.max_depth) {
        fail(Status::DepthExceeded);
        return kNoNode;
    }
    if (nodes_.size() >= limits_.max_nodes) {
        fail(Status::SizeExceeded);
        return kNoNode;
    }

    n.child_offset = static_cast<uint32_t>(children_.size());
    n.child_count = static_cast<uint32_t>(args.size());
    children_.insert(children_.end(), args.begin(), args.end());

    NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(n);
    interned_.emplace(std::move(key), id);
    return id;
}

static bool parse_small(const std::string &digits, int64_t *out) {
    int64_t value = 0;
    const auto conversion = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (conversion.ec != std::errc() || conversion.ptr != digits.data() + digits.size())
        return false;
    *out = value;
    return true;
}

bool contains_list(const Arena &arena, NodeId id) {
    return arena.at(id).has_list;
}

Sign literal_sign(const Arena &arena, NodeId id) {
    bool negative = false;
    while (arena.at(id).kind == Kind::Neg) {
        const ChildView children = arena.children(id);
        if (children.size() != 1)
            return Sign::Unknown;
        negative = !negative;
        id = children[0];
    }
    const Node &node = arena.at(id);
    if (node.kind != Kind::Integer && node.kind != Kind::Decimal)
        return Sign::Unknown;
    if (node.kind == Kind::Integer && node.small_valid) {
        if (node.small == 0)
            return Sign::Zero;
        return negative != (node.small < 0) ? Sign::Negative : Sign::Positive;
    }

    const std::string &text = arena.text(id);
    size_t at = 0;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        negative = negative != (text[at] == '-');
        ++at;
    }
    bool zero = true;
    bool saw_digit = false;
    bool saw_point = false;
    for (; at < text.size(); ++at) {
        const char digit = text[at];
        if (digit >= '0' && digit <= '9') {
            saw_digit = true;
            zero = zero && digit == '0';
        } else if (node.kind == Kind::Decimal && digit == '.' && !saw_point) {
            saw_point = true;
        } else {
            break;
        }
    }
    if (!saw_digit)
        return Sign::Unknown;
    if (node.kind == Kind::Decimal && at < text.size() &&
        (text[at] == 'e' || text[at] == 'E')) {
        ++at;
        if (at < text.size() && (text[at] == '-' || text[at] == '+'))
            ++at;
        const size_t exponent_begin = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9')
            ++at;
        if (at == exponent_begin)
            return Sign::Unknown;
    }
    if (at != text.size())
        return Sign::Unknown;
    return zero ? Sign::Zero : (negative ? Sign::Negative : Sign::Positive);
}

bool small_integer(const Arena &arena, NodeId id, int64_t *out) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Integer && n.small_valid && n.small != INT64_MIN) {
        *out = n.small;
        return true;
    }
    if (n.kind == Kind::Neg && n.child_count == 1) {
        const Node &inner = arena.at(arena.children(n)[0]);
        if (inner.kind == Kind::Integer && inner.small_valid && inner.small != INT64_MIN) {
            *out = -inner.small;
            return true;
        }
    }
    return false;
}

bool depends_on(const Arena &arena, NodeId id, NodeId variable) {
    return arena.any_node(id, [variable](NodeId current) { return current == variable; });
}

bool angle_dependent(const Arena &arena, NodeId id, NodeId variable) {
    static constexpr std::string_view names[] = {"sin",  "cos",  "tan",  "sec",  "csc",  "cot",  "asin",
                                                 "acos", "atan", "asec", "acsc", "acot", "atan2"};
    return arena.any_node(id, [&arena, variable](NodeId current) {
        if (arena.at(current).kind != Kind::Call ||
            (variable != kNoNode && !depends_on(arena, current, variable))) return false;
        const std::string &name = arena.text(current);
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    });
}

bool has_undefined_form(const Arena &arena, NodeId id) {
    return arena.any_node(id, [&arena](NodeId current) {
        const Node &node = arena.at(current);
        const ChildView children = arena.children(node);
        return node.kind == Kind::Pow && children.size() == 2 &&
               literal_sign(arena, children[0]) == Sign::Zero &&
               literal_sign(arena, children[1]) == Sign::Negative;
    });
}

std::string integer_text(int64_t v) {
    char digits[32];
    const auto conversion = std::to_chars(digits, digits + sizeof digits, v);
    if (conversion.ec != std::errc())
        return std::string();
    return std::string(digits, conversion.ptr);
}

NodeId Arena::integer(const std::string &digits) {
    Node n;
    n.kind = Kind::Integer;
    n.text = intern(digits);
    n.small = 0;
    n.small_valid = parse_small(digits, &n.small);
    n.depth = 1;
    n.size = 1;
    return add_node(n, std::vector<NodeId>());
}

NodeId Arena::decimal(const std::string &text) {
    Node n;
    n.kind = Kind::Decimal;
    n.text = intern(text);
    n.small = 0;
    n.small_valid = false;
    n.depth = 1;
    n.size = 1;
    return add_node(n, std::vector<NodeId>());
}

NodeId Arena::symbol(const std::string &name) {
    Node n;
    n.kind = Kind::Symbol;
    n.text = intern(name);
    n.small = 0;
    n.small_valid = false;
    n.depth = 1;
    n.size = 1;
    return add_node(n, std::vector<NodeId>());
}

void Arena::mark_approximate(NodeId id) {
    if (id == kNoNode || id >= nodes_.size())
        return;
    if (approximate_.size() <= id)
        approximate_.resize(id + 1, false);
    approximate_[id] = true;
}

bool Arena::is_approximate(NodeId id) const {
    return id != kNoNode && id < approximate_.size() && approximate_[id];
}

NodeId Arena::nary(Kind kind, const std::vector<NodeId> &args) {
    if (failed())
        return kNoNode;
    for (NodeId a : args) {
        if (a >= nodes_.size()) {
            fail(Status::SyntaxError);
            return kNoNode;
        }
    }

    Node n;
    n.kind = kind;
    n.text = intern("");
    n.small = 0;
    n.small_valid = false;
    n.depth = 1;
    n.size = 1;
    for (NodeId a : args) {
        const Node &child = nodes_[a];
        if (child.depth + 1 > n.depth)
            n.depth = child.depth + 1;
    }
    return add_node(n, args);
}

NodeId Arena::list(const std::vector<NodeId> &items) {
    return nary(Kind::List, items);
}

NodeId Arena::unary(Kind kind, NodeId a) {
    return nary(kind, std::vector<NodeId>{a});
}

NodeId Arena::binary(Kind kind, NodeId a, NodeId b) {
    return nary(kind, std::vector<NodeId>{a, b});
}

NodeId Arena::call(const std::string &name, const std::vector<NodeId> &args) {
    if (failed())
        return kNoNode;
    for (NodeId a : args) {
        if (a >= nodes_.size()) {
            fail(Status::SyntaxError);
            return kNoNode;
        }
    }

    Node n;
    n.kind = Kind::Call;
    n.text = intern(name);
    n.small = 0;
    n.small_valid = false;
    n.depth = 1;
    n.size = 1;
    for (NodeId a : args) {
        const Node &child = nodes_[a];
        if (child.depth + 1 > n.depth)
            n.depth = child.depth + 1;
    }
    return add_node(n, args);
}

}  // namespace nps
