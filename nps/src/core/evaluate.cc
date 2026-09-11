#include "nps/core/evaluate.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace nps {
namespace {

bool value_of_symbol(const std::string &name, const std::vector<SymbolValue> &values,
                     Rational *out) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].symbol == name) {
            *out = values[i].value;
            return true;
        }
    }
    return false;
}

// The assignment for one sample. Within a sample every symbol gets a different value, because two
// symbols sharing one would make x + y and 2 * x agree at it, and every third sample is fractional
// so a rule that only holds for integers cannot hide.
Rational sample_value(size_t symbol_index, size_t symbol_count, size_t sample) {
    Rational value;
    value.num = static_cast<int64_t>(2 + symbol_index + sample * (symbol_count + 1));
    value.den = (sample % 3 == 2) ? 2 : 1;
    return value;
}

}  // namespace

bool evaluate_rational_function(const std::string &name, const Rational &argument, Rational *out) {
    if (name == "sqrt") return rational_sqrt_exact(argument, out);
    if ((name == "sin" && rational_equal(argument, {0, 1})) ||
        (name == "ln" && rational_equal(argument, {1, 1}))) {
        *out = {0, 1};
        return true;
    }
    if ((name == "cos" || name == "exp") && rational_equal(argument, {0, 1})) {
        *out = {1, 1};
        return true;
    }
    return false;
}

bool evaluate_rational(const Arena &arena, NodeId id, const std::vector<SymbolValue> &values,
                       Rational *out) {
    if (id == kNoNode || id >= arena.node_count())
        return false;

    struct Frame {
        NodeId id;
        size_t next_child = 0;
        Rational value{0, 1};
    };
    std::vector<Frame> pending{{id}};
    std::unordered_map<NodeId, Rational> completed;
    while (!pending.empty()) {
        Frame &frame = pending.back();
        const Node &node = arena.at(frame.id);
        switch (node.kind) {
            case Kind::Integer:
                if (!node.small_valid)
                    return false;
                frame.value = {node.small, 1};
                break;
            case Kind::Decimal:
                if (!rational_from_text(arena.text(frame.id), &frame.value))
                    return false;
                break;
            case Kind::Symbol:
                if (!value_of_symbol(arena.text(frame.id), values, &frame.value))
                    return false;
                break;
            case Kind::Add:
            case Kind::Mul:
            case Kind::Neg:
            case Kind::Call:
            case Kind::Pow: {
                if (node.kind == Kind::Mul && frame.next_child == 0)
                    frame.value = {1, 1};
                const ChildView children = arena.children(node);
                if (((node.kind == Kind::Neg || node.kind == Kind::Call) && children.size() != 1) ||
                    (node.kind == Kind::Pow && children.size() != 2))
                    return false;
                if (frame.next_child < children.size()) {
                    const NodeId child = children[frame.next_child];
                    const auto found = completed.find(child);
                    if (found == completed.end()) {
                        pending.push_back({child});
                        continue;
                    }
                    const Rational operand = found->second;
                    if (node.kind == Kind::Add) {
                        if (!rational_add(frame.value, operand, &frame.value))
                            return false;
                    } else if (node.kind == Kind::Mul) {
                        if (!rational_mul(frame.value, operand, &frame.value))
                            return false;
                    } else if (node.kind == Kind::Neg) {
                        if (!negate_fraction(operand.num, operand.den,
                                             &frame.value.num, &frame.value.den))
                            return false;
                    } else if (node.kind == Kind::Call) {
                        if (!evaluate_rational_function(arena.text(frame.id), operand, &frame.value))
                            return false;
                    } else if (frame.next_child == 0) {
                        frame.value = operand;
                    } else if (operand.den != 1 ||
                               !rational_power(frame.value, operand.num, &frame.value)) {
                        return false;
                    }
                    ++frame.next_child;
                    continue;
                }
                break;
            }
            default:
                return false;
        }
        const Rational value = frame.value;
        completed.emplace(frame.id, value);
        pending.pop_back();
        if (pending.empty()) {
            *out = value;
            return true;
        }
    }
    return false;
}

namespace {

void gather_symbols(const Arena &arena, NodeId id, std::vector<std::string> *out) {
    std::vector<NodeId> pending{id};
    std::unordered_set<NodeId> visited;
    while (!pending.empty()) {
        const NodeId current = pending.back();
        pending.pop_back();
        if (current == kNoNode || current >= arena.node_count() || !visited.insert(current).second)
            continue;
        const Node &node = arena.at(current);
        if (node.kind == Kind::Symbol)
            out->push_back(arena.text(current));
        for (NodeId child : arena.children(node))
            pending.push_back(child);
    }
}

}  // namespace

void collect_symbols(const Arena &arena, NodeId id, std::vector<std::string> *out) {
    gather_symbols(arena, id, out);
    std::sort(out->begin(), out->end());
    out->erase(std::unique(out->begin(), out->end()), out->end());
}

SampleAgreement agrees_on_samples(const Arena &arena, NodeId left, NodeId right, size_t samples) {
    SampleAgreement result;
    if (left == kNoNode || right == kNoNode || arena.failed())
        return result;

    std::vector<std::string> names;
    collect_symbols(arena, left, &names);
    collect_symbols(arena, right, &names);

    for (size_t sample = 0; sample < samples; ++sample) {
        std::vector<SymbolValue> assignment;
        for (size_t i = 0; i < names.size(); ++i) {
            SymbolValue value;
            value.symbol = names[i];
            value.value = sample_value(i, names.size(), sample);
            assignment.push_back(value);
        }

        Rational a;
        Rational b;
        if (!evaluate_rational(arena, left, assignment, &a) ||
            !evaluate_rational(arena, right, assignment, &b))
            continue;
        ++result.evaluated;
        if (rational_equal(a, b)) {
            ++result.agreed;
            continue;
        }
        if (result.disagreement.empty()) {
            result.disagreement = rational_text(a) + " against " + rational_text(b);
            for (size_t i = 0; i < assignment.size(); ++i) {
                result.disagreement += i == 0 ? " at " : ", ";
                result.disagreement += assignment[i].symbol + " = " +
                                       rational_text(assignment[i].value);
            }
        }
    }
    return result;
}

}  // namespace nps
