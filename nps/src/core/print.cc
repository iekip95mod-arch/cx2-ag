#include "nps/core/print.h"

#include <vector>

namespace nps {
namespace {

const char *relation_op(Kind k, bool giac) {
    switch (k) {
        // These spellings are what the parser reads back, which criterion 14 needs: a context is
        // stored as printed text and replayed through the parser, so a relation that printed as
        // anything else would come back a different kind.
        case Kind::Equals: return giac ? ")=(" : " = ";
        case Kind::Assign: return giac ? nullptr : " := ";
        case Kind::Approx: return giac ? nullptr : " ~= ";
        case Kind::Identity: return giac ? nullptr : " == ";
        case Kind::Less: return giac ? ")<(" : " < ";
        case Kind::LessEqual: return giac ? ")<=(" : " <= ";
        case Kind::Greater: return giac ? ")>(" : " > ";
        case Kind::GreaterEqual: return giac ? ")>=(" : " >= ";
        default: return " ? ";
    }
}

// A folded constant carries its sign in its own text, and unary minus binds looser than a power, so
// -7^23 reads back as -(7^23). Only the base of a power is affected: everywhere else the minus
// applies to the literal either way.
bool negative_literal(const Arena &arena, NodeId id) {
    const Node &n = arena.at(id);
    if (n.kind != Kind::Integer && n.kind != Kind::Decimal)
        return false;
    const std::string &text = arena.text(id);
    return !text.empty() && text[0] == '-';
}

struct PrintPart {
    NodeId id;
    const char *text = nullptr;
};

NodeId denominator(const Arena &arena, NodeId id) {
    if (arena.at(id).kind != Kind::Pow) return kNoNode;
    const auto children = arena.children(id);
    int64_t exponent;
    return children.size() == 2 && small_integer(arena, children[1], &exponent) && exponent == -1
               ? children[0] : kNoNode;
}

bool emit(const Arena &arena, NodeId id, std::string &out, bool giac, bool math = false) {
    std::vector<PrintPart> pending{{id}};
    while (!pending.empty()) {
        const PrintPart part = pending.back();
        pending.pop_back();
        if (part.text) {
            out += part.text;
            continue;
        }
        const Node &node = arena.at(part.id);
        const ChildView children = arena.children(node);
        switch (node.kind) {
            case Kind::Invalid:
                return false;
            case Kind::Integer:
            case Kind::Decimal:
            case Kind::Symbol:
                out += arena.text(part.id);
                break;
            case Kind::Interval: {
                const std::string &ends = arena.text(part.id);
                if (children.size() != 2 || ends.size() != 2)
                    return false;
                if (giac) {
                    // Giac only spells a closed interval, so an open end has no faithful form to send.
                    if (ends != "[]")
                        return false;
                    out += "(";
                    pending.push_back({kNoNode, ")"});
                    pending.push_back({children[1]});
                    pending.push_back({kNoNode, ")..("});
                    pending.push_back({children[0]});
                    break;
                }
                out += ends[0];
                pending.push_back({kNoNode, ends[1] == ']' ? "]" : ")"});
                pending.push_back({children[1]});
                pending.push_back({kNoNode, ".."});
                pending.push_back({children[0]});
                break;
            }
            case Kind::Neg:
                if (children.size() != 1)
                    return false;
                out += giac ? "(-(" : "(-";
                pending.push_back({kNoNode, giac ? "))" : ")"});
                pending.push_back({children[0]});
                break;
            case Kind::Add:
            case Kind::Mul:
            case Kind::List:
            case Kind::Call: {
                if (node.kind == Kind::Call)
                    out += arena.text(part.id);
                out += node.kind == Kind::List ? "[" : "(";
                pending.push_back({kNoNode, node.kind == Kind::List ? "]" : ")"});
                const char *separator = node.kind == Kind::Add ? (giac ? "+" : " + ")
                                        : node.kind == Kind::Mul ? (giac ? "*" : " * ")
                                                                : (giac ? "," : ", ");
                size_t count = children.size();
                const NodeId divisor = math && node.kind == Kind::Mul && count > 1
                                           ? denominator(arena, children[count - 1]) : kNoNode;
                if (divisor != kNoNode) {
                    --count;
                    pending.push_back({divisor});
                    pending.push_back({kNoNode, " / "});
                    if (count > 1) {
                        out += "(";
                        pending.push_back({kNoNode, ")"});
                    }
                }
                for (size_t i = count; i > 0; --i) {
                    pending.push_back({children[i - 1]});
                    if (i > 1)
                        pending.push_back({kNoNode, separator});
                }
                break;
            }
            case Kind::Pow: {
                if (children.size() != 2)
                    return false;
                const NodeId divisor = math ? denominator(arena, part.id) : kNoNode;
                if (divisor != kNoNode) {
                    out += "(1 / ";
                    pending.push_back({kNoNode, ")"});
                    pending.push_back({divisor});
                    break;
                }
                const bool bracket_base = !giac && negative_literal(arena, children[0]);
                out += "(";
                if (bracket_base)
                    out += "(";
                pending.push_back({kNoNode, ")"});
                pending.push_back({children[1]});
                pending.push_back({kNoNode, giac ? ")^(" : "^"});
                if (bracket_base)
                    pending.push_back({kNoNode, ")"});
                pending.push_back({children[0]});
                break;
            }
            case Kind::Equals:
            case Kind::Assign:
            case Kind::Approx:
            case Kind::Identity:
            case Kind::Less:
            case Kind::LessEqual:
            case Kind::Greater:
            case Kind::GreaterEqual: {
                const char *separator = relation_op(node.kind, giac);
                if (!separator || children.size() != 2)
                    return false;
                out += "(";
                pending.push_back({kNoNode, ")"});
                pending.push_back({children[1]});
                pending.push_back({kNoNode, separator});
                pending.push_back({children[0]});
                break;
            }
        }
    }
    return true;
}

}  // namespace

std::string print(const Arena &arena, NodeId id) {
    std::string out;
    if (id == kNoNode)
        return out;
    return emit(arena, id, out, false) ? out : std::string();
}

std::string print_giac(const Arena &arena, NodeId id) {
    std::string out;
    if (id == kNoNode)
        return out;
    return emit(arena, id, out, true) ? out : std::string();
}

std::string print_math(const Arena &arena, NodeId id) {
    std::string out;
    if (id == kNoNode) return out;
    return emit(arena, id, out, false, true) ? out : std::string();
}

}  // namespace nps
