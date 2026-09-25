#include "nps/core/canonical.h"

#include "nps/core/checked.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nps {
namespace {

int compare(const Arena &arena, NodeId a, NodeId b);

int compare_text(const Arena &arena, NodeId a, NodeId b) {
    const std::string &sa = arena.text(a);
    const std::string &sb = arena.text(b);
    if (sa < sb)
        return -1;
    return sa > sb ? 1 : 0;
}

struct DecimalKey {
    std::string sig;
    int64_t weight = 0;
    bool zero = true;
};

// Reduce a non-negative decimal to its significant digits and the power of ten of the first, for
// value comparison without floating point.
DecimalKey decimal_key(const std::string &s) {
    std::string digits;
    int int_count = 0;
    bool in_frac = false;
    size_t i = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            in_frac = true;
            continue;
        }
        if (c < '0' || c > '9')
            break;
        digits.push_back(c);
        if (!in_frac)
            ++int_count;
    }
    int64_t exp = 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool neg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            neg = s[i] == '-';
            ++i;
        }
        int64_t e = 0;
        // Stop well inside int64. A larger exponent only makes the number more extreme, and ties
        // fall through to a text comparison.
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
            if (e <= 100000000000000000LL)
                e = e * 10 + (s[i] - '0');
        }
        exp = neg ? -e : e;
    }
    DecimalKey k;
    size_t lead = 0;
    while (lead < digits.size() && digits[lead] == '0')
        ++lead;
    if (lead == digits.size())
        return k;
    k.zero = false;
    k.sig = digits.substr(lead);
    size_t end = k.sig.size();
    while (end > 1 && k.sig[end - 1] == '0')
        --end;
    k.sig.resize(end);
    k.weight = static_cast<int64_t>(int_count) - 1 + exp - static_cast<int64_t>(lead);
    return k;
}

int compare_decimal(const std::string &a, const std::string &b) {
    DecimalKey ka = decimal_key(a);
    DecimalKey kb = decimal_key(b);
    if (ka.zero || kb.zero) {
        if (ka.zero && kb.zero)
            return 0;
        return ka.zero ? -1 : 1;
    }
    if (ka.weight != kb.weight)
        return ka.weight < kb.weight ? -1 : 1;
    const size_t n = ka.sig.size() < kb.sig.size() ? ka.sig.size() : kb.sig.size();
    for (size_t i = 0; i < n; ++i) {
        if (ka.sig[i] != kb.sig[i])
            return ka.sig[i] < kb.sig[i] ? -1 : 1;
    }
    if (ka.sig.size() != kb.sig.size())
        return ka.sig.size() < kb.sig.size() ? -1 : 1;
    return 0;
}

int compare_numbers(const Arena &arena, NodeId a, NodeId b) {
    const Node &na = arena.at(a);
    const Node &nb = arena.at(b);
    if (na.small_valid && nb.small_valid) {
        if (na.small < nb.small)
            return -1;
        return na.small > nb.small ? 1 : 0;
    }
    const std::string &sa = arena.text(a);
    const std::string &sb = arena.text(b);
    // Decimals order by value, not text length (1.25 is smaller than 2.5 but longer as text), then
    // by text so two spellings of one number keep a fixed order.
    if (na.kind == Kind::Decimal) {
        int by_value = compare_decimal(sa, sb);
        if (by_value != 0)
            return by_value;
        return compare_text(arena, a, b);
    }
    // Big integers: sign, then digit count, then digits, which is numeric order given no point and
    // no leading zeros.
    bool na_neg = !sa.empty() && sa[0] == '-';
    bool nb_neg = !sb.empty() && sb[0] == '-';
    if (na_neg != nb_neg)
        return na_neg ? -1 : 1;
    size_t la = sa.size() - (na_neg ? 1 : 0);
    size_t lb = sb.size() - (nb_neg ? 1 : 0);
    if (la != lb)
        return (la < lb) == !na_neg ? -1 : 1;
    int by_digits = compare_text(arena, a, b);
    return na_neg ? -by_digits : by_digits;
}

int compare(const Arena &arena, NodeId a, NodeId b) {
    std::vector<std::pair<NodeId, NodeId>> pending;
    for (;;) {
        if (a != b) {
            const Node &na = arena.at(a);
            const Node &nb = arena.at(b);

            // Numbers sort ahead of everything, so a folded constant lands at the front of a sum or product
            // and a reader finds it where they expect it.
            bool a_num = na.kind == Kind::Integer || na.kind == Kind::Decimal;
            bool b_num = nb.kind == Kind::Integer || nb.kind == Kind::Decimal;
            if (a_num != b_num)
                return a_num ? -1 : 1;
            if (a_num && na.kind == nb.kind) {
                const int order = compare_numbers(arena, a, b);
                if (order != 0)
                    return order;
            } else {
                if (na.kind != nb.kind)
                    return static_cast<int>(na.kind) < static_cast<int>(nb.kind) ? -1 : 1;
                const int by_text = compare_text(arena, a, b);
                if (by_text != 0)
                    return by_text;
                const ChildView ca = arena.children(na), cb = arena.children(nb);
                if (ca.size() != cb.size())
                    return ca.size() < cb.size() ? -1 : 1;
                for (size_t i = ca.size(); i > 1; --i)
                    pending.emplace_back(ca[i - 1], cb[i - 1]);
                if (!ca.empty()) {
                    a = ca[0];
                    b = cb[0];
                    continue;
                }
            }
        }
        if (pending.empty())
            return 0;
        a = pending.back().first;
        b = pending.back().second;
        pending.pop_back();
    }
}

struct Flattener {
    Arena &arena;
    Kind kind;
    std::vector<NodeId> out;

    void add(NodeId id) {
        std::vector<NodeId> pending;
        for (;;) {
            const Node &n = arena.at(id);
            if (n.kind == kind) {
                const ChildView children = arena.children(n);
                for (size_t i = children.size(); i > 1; --i)
                    pending.push_back(children[i - 1]);
                if (!children.empty()) {
                    id = children[0];
                    continue;
                }
            } else {
                out.push_back(id);
            }
            if (pending.empty())
                return;
            id = pending.back();
            pending.pop_back();
        }
    }
};

// A factor of the form b^(-k) for small integers b and k, which is how every division by a constant
// arrives. Its contribution to a product's denominator is b^k, refused when that would not fit.
bool reciprocal_value(const Arena &arena, NodeId id, int64_t *den) {
    const Node &n = arena.at(id);
    if (n.kind != Kind::Pow)
        return false;
    const ChildView c = arena.children(n);
    const Node &base = arena.at(c[0]);
    if (base.kind != Kind::Integer || !base.small_valid)
        return false;
    // Read through small_integer rather than off the node: the parser spells a division's exponent
    // Neg(Integer) and only the canonical form has folded that to a literal, so reading the spelling
    // saw a derivative's x/2, which is not canonical, as no reciprocal at all.
    int64_t exponent;
    if (!small_integer(arena, c[1], &exponent) || exponent >= 0)
        return false;
    return integer_power(base.small, magnitude(exponent), den);
}

bool fold_sum_exact(const Arena &arena, const std::vector<NodeId> &numbers, int64_t *out) {
    detail::Mpz sum;
    detail::Mpz term;
    for (NodeId id : numbers) {
        detail::mpz_set_i64(term.get(), arena.at(id).small);
        mpz_add(sum.get(), sum.get(), term.get());
    }
    return detail::mpz_get_i64(sum.get(), out);
}

bool fold_rational_exact(const Arena &arena, const std::vector<NodeId> &numbers,
                         const std::vector<NodeId> &reciprocals, Rational *out) {
    const auto zero = std::find_if(numbers.begin(), numbers.end(),
                                   [&arena](NodeId id) { return arena.at(id).small == 0; });
    if (zero != numbers.end()) {
        for (NodeId id : reciprocals) {
            int64_t denominator;
            if (!reciprocal_value(arena, id, &denominator) || denominator == 0)
                return false;
        }
        *out = Rational{0, 1};
        return true;
    }

    detail::Mpq value;
    detail::Mpq factor;
    mpq_set_ui(value.get(), 1, 1);
    for (NodeId id : numbers) {
        detail::mpq_set_i64(factor.get(), arena.at(id).small, 1);
        mpq_mul(value.get(), value.get(), factor.get());
    }
    for (NodeId id : reciprocals) {
        int64_t denominator;
        if (!reciprocal_value(arena, id, &denominator) ||
            !detail::mpq_set_i64(factor.get(), 1, denominator)) {
            return false;
        }
        mpq_mul(value.get(), value.get(), factor.get());
    }
    return detail::mpq_get_rational(value.get(), out);
}

bool combine_checked(int64_t a, int64_t b, bool additive, int64_t *out) {
    return additive ? add_checked(a, b, out) : mul_checked(a, b, out);
}

// The constants of a chain whose exact total does not fit int64. Taken in sorted order until one
// does not fit, then any two that fit are merged until no pair does. Ending closed is what lets a
// second pass, or the printed chain read back as nested pairs, find nothing left to fold. A prefix
// that stopped at the first overflow left pairs behind it that the next pass took.
void fold_closed(std::vector<int64_t> *values, bool additive) {
    if (values->empty())
        return;
    std::sort(values->begin(), values->end());
    std::vector<int64_t> folded;
    int64_t acc = (*values)[0];
    size_t taken = 1;
    for (; taken < values->size(); ++taken) {
        int64_t next;
        if (!combine_checked(acc, (*values)[taken], additive, &next))
            break;
        acc = next;
    }
    folded.push_back(acc);
    folded.insert(folded.end(), values->begin() + static_cast<long>(taken), values->end());
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < folded.size() && !merged; ++i) {
            for (size_t j = i + 1; j < folded.size() && !merged; ++j) {
                int64_t next;
                if (!combine_checked(folded[i], folded[j], additive, &next))
                    continue;
                folded.erase(folded.begin() + static_cast<long>(j));
                folded.erase(folded.begin() + static_cast<long>(i));
                folded.insert(std::lower_bound(folded.begin(), folded.end(), next), next);
                merged = true;
            }
        }
    }
    *values = std::move(folded);
}

NodeId rebuild(Arena &arena, NodeId id, const std::vector<NodeId> &args) {
    const Node &n = arena.at(id);
    switch (n.kind) {
        case Kind::Call: return arena.call(arena.text(id), args);
        case Kind::Neg: return arena.unary(Kind::Neg, args[0]);
        case Kind::Add:
        case Kind::List:
        case Kind::Mul: return arena.nary(n.kind, args);
        default: return arena.binary(n.kind, args[0], args[1]);
    }
}

// A rational in the one spelling the canonical form uses for it, so exactify's output is already the
// shape the folder would have produced and a second pass finds nothing to do.
NodeId rational_node(Arena &arena, const Rational &value) {
    NodeId num = arena.integer(integer_text(value.num));
    if (num == kNoNode || value.den == 1)
        return num;
    NodeId den = arena.integer(integer_text(value.den));
    NodeId minus_one = arena.integer("-1");
    if (den == kNoNode || minus_one == kNoNode)
        return kNoNode;
    NodeId reciprocal = arena.binary(Kind::Pow, den, minus_one);
    if (reciprocal == kNoNode)
        return kNoNode;
    return arena.binary(Kind::Mul, num, reciprocal);
}

bool integer_value(const Arena &arena, NodeId id, int64_t *out) {
    const Node &n = arena.at(id);
    if (n.kind != Kind::Integer || !n.small_valid)
        return false;
    *out = n.small;
    return true;
}

bool rational_value(const Arena &arena, NodeId id, Rational *out) {
    int64_t numerator = 0, denominator = 1;
    if (integer_value(arena, id, &numerator)) {
        *out = {numerator, 1};
        return true;
    }
    if (arena.at(id).kind == Kind::Decimal)
        return rational_from_text(arena.text(id), out);
    if (reciprocal_value(arena, id, &denominator))
        return rational_div({1, 1}, {denominator, 1}, out);
    const ChildView children = arena.children(id);
    if (arena.at(id).kind != Kind::Mul || children.size() != 2) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (integer_value(arena, children[i], &numerator) &&
            reciprocal_value(arena, children[1 - i], &denominator))
            return rational_div({numerator, 1}, {denominator, 1}, out);
    }
    return false;
}

struct LikeTerms {
    std::vector<NodeId> factors;
    std::vector<Rational> coefficients;
    std::vector<NodeId> original;
};

// A rational constant is a like term with no factors. Decimals keep their numeric-mode spelling.
bool split_like_term(const Arena &arena, NodeId term, Rational *coefficient,
                     std::vector<NodeId> *factors) {
    const Node &node = arena.at(term);
    if (node.kind == Kind::Decimal)
        return false;
    const bool product = node.kind == Kind::Mul;
    std::vector<NodeId> parts;
    if (product) {
        for (NodeId factor : arena.children(term))
            parts.push_back(factor);
    } else {
        parts.push_back(term);
    }

    Rational value{1, 1};
    for (NodeId factor : parts) {
        const Node &part = arena.at(factor);
        int64_t integer = 0;
        int64_t denominator = 0;
        Rational next;
        if (integer_value(arena, factor, &integer)) {
            if (!rational_mul(value, Rational{integer, 1}, &next))
                return false;
            value = next;
        } else if (reciprocal_value(arena, factor, &denominator) && denominator != 0) {
            if (!rational_mul(value, Rational{1, denominator}, &next))
                return false;
            value = next;
        } else {
            // A constant too large to fold gathers as a term, but leaves a product's coefficient unread.
            if (product && (part.kind == Kind::Integer || part.kind == Kind::Decimal))
                return false;
            factors->push_back(factor);
        }
    }
    *coefficient = value;
    return true;
}

bool coefficient_sum(const std::vector<Rational> &coefficients, Rational *out) {
    detail::Mpq sum;
    detail::Mpq part;
    mpq_set_ui(sum.get(), 0, 1);
    for (const Rational &coefficient : coefficients) {
        if (!detail::mpq_set_i64(part.get(), coefficient.num, coefficient.den))
            return false;
        mpq_add(sum.get(), sum.get(), part.get());
    }
    return detail::mpq_get_rational(sum.get(), out);
}

NodeId scaled_term(Arena &arena, const Rational &coefficient,
                   const std::vector<NodeId> &base_factors) {
    if (coefficient.num == 0)
        return kNoNode;
    std::vector<NodeId> factors;
    if (coefficient.num != 1 || coefficient.den != 1) {
        const NodeId scalar = rational_node(arena, coefficient);
        if (scalar == kNoNode)
            return kNoNode;
        Flattener flat{arena, Kind::Mul, {}};
        flat.add(scalar);
        for (NodeId factor : flat.out) {
            int64_t integer;
            if (!integer_value(arena, factor, &integer) || integer != 1)
                factors.push_back(factor);
        }
    }
    factors.insert(factors.end(), base_factors.begin(), base_factors.end());
    // Rational terms totalling one leave nothing to write, and that unit is the whole term.
    if (factors.empty())
        return arena.integer("1");
    std::sort(factors.begin(), factors.end(),
              [&arena](NodeId x, NodeId y) { return compare(arena, x, y) < 0; });
    return factors.size() == 1 ? factors[0] : arena.nary(Kind::Mul, factors);
}

bool domain_total(const Arena &arena, const std::vector<NodeId> &factors) {
    std::vector<NodeId> pending = factors;
    std::unordered_set<NodeId> visited;
    while (!pending.empty()) {
        const NodeId id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second)
            continue;
        const Node &node = arena.at(id);
        const ChildView children = arena.children(node);
        switch (node.kind) {
            case Kind::Integer:
            case Kind::Decimal:
            case Kind::Symbol: break;
            case Kind::Neg:
                if (children.size() != 1)
                    return false;
                pending.push_back(children[0]);
                break;
            case Kind::Add:
            case Kind::Mul:
                if (children.empty())
                    return false;
                for (NodeId child : children)
                    pending.push_back(child);
                break;
            case Kind::Pow: {
                int64_t exponent;
                if (children.size() != 2 || !integer_value(arena, children[1], &exponent) ||
                    exponent <= 0)
                    return false;
                pending.push_back(children[0]);
                break;
            }
            case Kind::Call: {
                const std::string &name = arena.text(id);
                if (children.size() != 1 || (name != "sin" && name != "cos" && name != "exp"))
                    return false;
                pending.push_back(children[0]);
                break;
            }
            default: return false;
        }
    }
    return true;
}

bool collect_like_terms(Arena &arena, std::vector<NodeId> *terms) {
    std::vector<LikeTerms> groups;
    std::map<std::vector<NodeId>, size_t> by_factors;
    std::vector<NodeId> untouched;
    for (NodeId term : *terms) {
        Rational coefficient;
        std::vector<NodeId> factors;
        if (!split_like_term(arena, term, &coefficient, &factors)) {
            untouched.push_back(term);
            continue;
        }
        auto found = by_factors.find(factors);
        if (found == by_factors.end()) {
            by_factors.emplace(factors, groups.size());
            groups.push_back(LikeTerms{std::move(factors), {coefficient}, {term}});
        } else {
            LikeTerms &group = groups[found->second];
            group.coefficients.push_back(coefficient);
            group.original.push_back(term);
        }
    }

    for (const LikeTerms &group : groups) {
        if (group.original.size() == 1) {
            untouched.push_back(group.original[0]);
            continue;
        }
        Rational total;
        if (!coefficient_sum(group.coefficients, &total)) {
            untouched.insert(untouched.end(), group.original.begin(), group.original.end());
            continue;
        }
        if (total.num == 0 && domain_total(arena, group.factors))
            continue;
        if (total.num == 0) {
            untouched.insert(untouched.end(), group.original.begin(), group.original.end());
            continue;
        }
        const NodeId combined = scaled_term(arena, total, group.factors);
        if (combined == kNoNode)
            return false;
        untouched.push_back(combined);
    }
    *terms = std::move(untouched);
    return true;
}

// The rational coefficient of a product, spelled as the decimal it equals, with the factors that did
// not go into it handed back untouched. False for every product whose coefficient is a whole number
// or does not terminate, which is the whole of when there is nothing to write.
bool read_decimal_coefficient(const Arena &arena, const std::vector<NodeId> &factors,
                              std::string *text, std::vector<NodeId> *rest) {
    Rational value;
    value.num = 1;
    value.den = 1;
    size_t numerators = 0;
    size_t denominators = 0;
    for (size_t i = 0; i < factors.size(); ++i) {
        int64_t part;
        if (numerators == 0 && integer_value(arena, factors[i], &part)) {
            value.num = part;
            ++numerators;
        } else if (denominators == 0 && reciprocal_value(arena, factors[i], &part) && part != 0) {
            value.den = part;
            ++denominators;
        } else {
            rest->push_back(factors[i]);
        }
    }
    if (denominators == 0)
        return false;

    // rational_text writes a terminating fraction as a decimal and everything else as p/q, so the
    // point is the answer to whether this coefficient has a decimal spelling at all.
    *text = rational_text(value);
    return text->find('.') != std::string::npos;
}

}  // namespace

bool canonical_less(const Arena &arena, NodeId a, NodeId b) {
    return compare(arena, a, b) < 0;
}

namespace {

bool transformable(const Arena &arena, NodeId id) {
    if (arena.failed() || id >= arena.node_count())
        return false;
    return !arena.any_node(id, [&arena](NodeId current) {
        const Node &node = arena.at(current);
        switch (node.kind) {
            case Kind::Invalid: return true;
            case Kind::Integer:
            case Kind::Decimal:
            case Kind::Symbol: return node.child_count != 0;
            case Kind::Neg: return node.child_count != 1;
            case Kind::Add:
            case Kind::Mul:
            case Kind::Call:
            case Kind::List: return false;
            default: return node.child_count != 2;
        }
    });
}

struct CanonicalFrame {
    NodeId id = kNoNode;
    std::vector<NodeId> children;
    std::vector<NodeId> normalized;
};

bool prepare_canonical(Arena &arena, NodeId id, CanonicalFrame *frame) {
    if (id == kNoNode || arena.failed())
        return false;
    Kind kind = arena.at(id).kind;
    const bool collection = contains_list(arena, id);
    if (kind == Kind::Invalid)
        return false;
    if (kind == Kind::Neg && !collection) {
        const NodeId minus_one = arena.integer("-1");
        if (minus_one == kNoNode)
            return false;
        id = arena.nary(Kind::Mul, {minus_one, arena.children(id)[0]});
        if (id == kNoNode)
            return false;
        kind = Kind::Mul;
    }
    frame->id = id;
    if ((kind == Kind::Add || kind == Kind::Mul) && !collection) {
        Flattener raw{arena, kind, {}};
        for (NodeId child : arena.children(id))
            raw.add(child);
        frame->children = std::move(raw.out);
    } else {
        for (NodeId child : arena.children(id))
            frame->children.push_back(child);
    }
    frame->normalized.reserve(frame->children.size());
    return true;
}

NodeId canonical_node(Arena &arena, NodeId id, const std::vector<NodeId> &normalized,
                      NodeId *rewrite) {
    if (id == kNoNode || arena.failed())
        return kNoNode;

    const Kind kind = arena.at(id).kind;
    // Scalar identities do not apply to unimplemented collection algebra.
    if (contains_list(arena, id)) {
        return rebuild(arena, id, normalized);
    }
    switch (kind) {
        // An id past the end of the arena, which the guard above catches as kNoNode but not when a
        // caller invents one. There is nothing to canonicalize, so it stays a miss.
        case Kind::Invalid:
        case Kind::List:
            return kNoNode;
        case Kind::Integer: {
            // The text is the interning key, so 007 and 7 are two nodes until the zeros go.
            const std::string &text = arena.text(id);
            size_t lead = 0;
            while (lead + 1 < text.size() && text[lead] == '0')
                ++lead;
            return lead == 0 ? id : arena.integer(text.substr(lead));
        }
        case Kind::Decimal:
        case Kind::Symbol:
            return id;

        case Kind::Neg: return kNoNode;

        case Kind::Add:
        case Kind::Mul: {
            // Same-kind descendants are gathered before anything is canonicalized, so the constants
            // a chain holds reach the fold as one multiset however it was nested. The printer writes
            // a chain flat and the parser reads it left nested, so a fold that saw the nesting did
            // not survive its own print.
            Flattener flat{arena, kind, {}};
            for (NodeId child : normalized)
                flat.add(child);

            const bool additive = kind == Kind::Add;

            // Sorting makes the maximal-prefix fallback independent of input order.
            std::vector<NodeId> numbers;
            std::vector<NodeId> reciprocals;
            std::vector<NodeId> rest;
            for (NodeId a : flat.out) {
                const Node &n = arena.at(a);
                int64_t den;
                if (n.kind == Kind::Integer && n.small_valid)
                    numbers.push_back(a);
                else if (!additive && reciprocal_value(arena, a, &den))
                    reciprocals.push_back(a);
                else
                    rest.push_back(a);
            }
            std::sort(numbers.begin(), numbers.end(),
                      [&arena](NodeId x, NodeId y) { return compare(arena, x, y) < 0; });
            std::sort(reciprocals.begin(), reciprocals.end(),
                      [&arena](NodeId x, NodeId y) { return compare(arena, x, y) < 0; });

            // Normalize an undefined zero product before bottom-up folding can discard siblings.
            if (!additive) {
                const auto zero = std::find_if(numbers.begin(), numbers.end(), [&arena](NodeId a) {
                    return arena.at(a).small == 0;
                });
                if (zero != numbers.end()) {
                    const auto zero_reciprocal =
                        std::find_if(reciprocals.begin(), reciprocals.end(), [&arena](NodeId a) {
                            int64_t denominator;
                            return reciprocal_value(arena, a, &denominator) && denominator == 0;
                        });
                    if (zero_reciprocal != reciprocals.end()) {
                        std::vector<NodeId> undefined;
                        undefined.reserve(reciprocals.size() + 1);
                        undefined.push_back(*zero);
                        for (NodeId reciprocal : reciprocals) {
                            int64_t denominator;
                            if (reciprocal_value(arena, reciprocal, &denominator) && denominator == 0)
                                undefined.push_back(reciprocal);
                        }
                        return arena.nary(Kind::Mul, undefined);
                    }
                }
            }

            // Prefer a full exact fold, then fall back to a closed fold of what does not fit.
            // A product also folds its constant reciprocals, so 3 * x^2 * 3^-1 and x^2 are one
            // form: that is the comparison the integrator's derivative check needs, and a rational
            // coefficient in lowest terms is the shape a reader expects anyway.
            // A full arena returns kNoNode from any of the constructions below. Each is kept out of
            // the sort that follows, which reads arena.at on every element and would index past the
            // node table at kNoNode.
            const auto push_integer = [&arena, &rest](int64_t value) {
                const NodeId id = arena.integer(integer_text(value));
                if (id == kNoNode)
                    return false;
                rest.push_back(id);
                return true;
            };
            const auto push_reciprocal = [&arena, &rest](int64_t denominator) {
                const NodeId den_id = arena.integer(integer_text(denominator));
                const NodeId minus_one = arena.integer("-1");
                if (den_id == kNoNode || minus_one == kNoNode)
                    return false;
                const NodeId reciprocal = arena.binary(Kind::Pow, den_id, minus_one);
                if (reciprocal == kNoNode)
                    return false;
                rest.push_back(reciprocal);
                return true;
            };
            std::vector<int64_t> values;
            values.reserve(numbers.size());
            for (NodeId a : numbers)
                values.push_back(arena.at(a).small);
            if (additive) {
                int64_t total = 0;
                if (fold_sum_exact(arena, numbers, &total)) {
                    values.assign(1, total);
                } else {
                    fold_closed(&values, true);
                }
                for (int64_t value : values) {
                    if (value == 0 && values.size() == 1)
                        continue;
                    if (!push_integer(value))
                        return kNoNode;
                }
            } else {
                Rational total;
                if (fold_rational_exact(arena, numbers, reciprocals, &total)) {
                    if (!numbers.empty() && total.num == 0)
                        return arena.integer("0");
                    if (total.num != 1 && !push_integer(total.num))
                        return kNoNode;
                    if (total.den != 1 && !push_reciprocal(total.den))
                        return kNoNode;
                } else {
                    // Integers fold with integers and reciprocals with reciprocals, never across.
                    // A rational the two had formed would print as two constants that the next pass
                    // could pair differently, and the closed fold would no longer be closed.
                    std::vector<int64_t> denominators;
                    for (NodeId a : reciprocals) {
                        int64_t denominator;
                        if (reciprocal_value(arena, a, &denominator) && denominator != 0)
                            denominators.push_back(denominator);
                        else
                            rest.push_back(a);
                    }
                    fold_closed(&values, false);
                    fold_closed(&denominators, false);
                    for (int64_t value : values) {
                        if (value == 1 && values.size() == 1)
                            continue;
                        if (!push_integer(value))
                            return kNoNode;
                    }
                    for (int64_t denominator : denominators) {
                        if (!push_reciprocal(denominator))
                            return kNoNode;
                    }
                }
            }

            if (additive && !collect_like_terms(arena, &rest))
                return kNoNode;

            // Everything sorts together, the folded constants included. Giving the fold a reserved
            // place at the front put it out of order with the leftovers, and the next pass moved it.
            std::sort(rest.begin(), rest.end(),
                      [&arena](NodeId x, NodeId y) { return compare(arena, x, y) < 0; });

            std::vector<NodeId> args;
            args.reserve(rest.size());
            for (NodeId a : rest)
                args.push_back(a);

            if (args.empty())
                return arena.integer(additive ? "0" : "1");
            if (args.size() == 1)
                return args[0];
            return arena.nary(kind, args);
        }

        // Each side is canonicalized and the relation is rebuilt as itself. That is what keeps an
        // approximation an approximation: normalising the two sides says nothing about the claim
        // between them, and rebuilding under any other kind would quietly upgrade "about equal" to
        // "equal".
        case Kind::Pow:
        case Kind::Equals:
        case Kind::Assign:
        case Kind::Approx:
        case Kind::Identity:
        case Kind::Less:
        case Kind::LessEqual:
        case Kind::Greater:
        case Kind::GreaterEqual:
        case Kind::Call: {
            const std::vector<NodeId> &args = normalized;
            if (kind == Kind::Call) {
                Rational argument, value;
                if (args.size() == 1 && rational_value(arena, args[0], &argument) &&
                    evaluate_rational_function(arena.text(id), argument, &value))
                    return rational_node(arena, value);
                return arena.call(arena.text(id), args);
            }
            if (kind == Kind::Pow) {
                // x^1 is x, and 1 to any integer power is 1. Without the first, the power rule's
                // 2 * x^1 and a typed 2 * x were two forms of one expression.
                const Node &base = arena.at(args[0]);
                const Node &exponent = arena.at(args[1]);
                if (exponent.kind == Kind::Integer && exponent.small_valid && exponent.small == 1)
                    return args[0];
                if (base.kind == Kind::Mul && exponent.kind == Kind::Integer && exponent.small_valid) {
                    const bool odd = exponent.small % 2 != 0;
                    const ChildView factors = arena.children(args[0]);
                    for (size_t i = 0; i < factors.size(); ++i) {
                        const Node &factor = arena.at(factors[i]);
                        if (factor.kind != Kind::Integer || !factor.small_valid || factor.small >= 0 ||
                            negate_overflows(factor.small))
                            continue;
                        const int64_t magnitude = -factor.small;
                        std::vector<NodeId> positive;
                        positive.reserve(factors.size());
                        for (NodeId child : factors)
                            positive.push_back(child);
                        positive[i] = arena.integer(integer_text(magnitude));
                        const NodeId positive_base = arena.nary(Kind::Mul, positive);
                        const NodeId power = arena.binary(Kind::Pow, positive_base, args[1]);
                        *rewrite = odd ? arena.unary(Kind::Neg, power) : power;
                        return kNoNode;
                    }
                }
                if (base.kind == Kind::Integer && base.small_valid && base.small == 1 &&
                    exponent.kind == Kind::Integer)
                    return args[0];
                // A constant to a positive constant power is its value while that fits. Zero to
                // the zero stays written, since it has no value to fold to.
                int64_t value;
                if (base.kind == Kind::Integer && base.small_valid && exponent.kind == Kind::Integer &&
                    exponent.small_valid && exponent.small > 0 &&
                    integer_power(base.small, exponent.small, &value))
                    return arena.integer(integer_text(value));
                // A negative power of a constant is a whole number only when the base is minus one,
                // and then it is the sign of the exponent. Any other base would need a fraction the
                // node cannot hold, so it stays written.
                if (base.kind == Kind::Integer && base.small_valid && base.small == -1 &&
                    exponent.kind == Kind::Integer && exponent.small_valid && exponent.small < 0)
                    return arena.integer(exponent.small % 2 == 0 ? "1" : "-1");
                // (a^m)^n is a^(m*n) for integer m and n, whatever a is. Without it a rational
                // coefficient spelled as 2^-1 could not be inverted: the integrator builds
                // (2^-1)^-1 to divide by one half, and its own derivative check then compared two
                // spellings of the same number and refused the answer it had just computed.
                if (base.kind == Kind::Pow && exponent.kind == Kind::Integer && exponent.small_valid) {
                    const Node &inner = arena.at(arena.children(args[0])[1]);
                    if (inner.kind == Kind::Integer && inner.small_valid &&
                        !mul_overflows(inner.small, exponent.small)) {
                        *rewrite = arena.binary(Kind::Pow, arena.children(args[0])[0],
                                                arena.integer(integer_text(inner.small * exponent.small)));
                        return kNoNode;
                    }
                }
            }
            return arena.nary(kind, args);
        }
    }
    return kNoNode;
}

}

CanonicalRefusal canonical_refusal(const Arena &arena) {
    if (arena.failed() && resource_status(arena.status()))
        return CanonicalRefusal::ResourceLimit;
    return CanonicalRefusal::Unsupported;
}

std::string canonical_refusal_message(const Arena &arena) {
    if (canonical_refusal(arena) == CanonicalRefusal::Unsupported)
        return "this expression has no canonical form in StepCAS";
    // The status is carried because naming the limit without naming which one sent a reader to
    // shorten an expression whose size was never the problem.
    return std::string("the expression outgrew the limits while being put in canonical form: ") +
           status_name(arena.status());
}

NodeId canonicalize(Arena &arena, NodeId id) {
    if (!transformable(arena, id))
        return kNoNode;
    CanonicalFrame first;
    if (!prepare_canonical(arena, id, &first))
        return kNoNode;
    std::vector<CanonicalFrame> pending;
    pending.push_back(std::move(first));
    while (!pending.empty()) {
        CanonicalFrame &frame = pending.back();
        if (frame.normalized.size() < frame.children.size()) {
            const NodeId child = frame.children[frame.normalized.size()];
            CanonicalFrame next;
            if (!prepare_canonical(arena, child, &next))
                return kNoNode;
            pending.push_back(std::move(next));
            continue;
        }
        NodeId rewrite = kNoNode;
        const NodeId normalized = canonical_node(arena, frame.id, frame.normalized, &rewrite);
        if (rewrite != kNoNode) {
            CanonicalFrame next;
            if (!prepare_canonical(arena, rewrite, &next))
                return kNoNode;
            frame = std::move(next);
            continue;
        }
        if (normalized == kNoNode)
            return kNoNode;
        pending.pop_back();
        if (pending.empty())
            return normalized;
        pending.back().normalized.push_back(normalized);
    }
    return kNoNode;
}

bool folded_integer(Arena &arena, NodeId id, int64_t *out) {
    if (small_integer(arena, id, out))
        return true;
    const NodeId folded = canonicalize(arena, id);
    return folded != kNoNode && small_integer(arena, folded, out);
}

bool divides_by_zero(Arena &arena, NodeId id) {
    if (has_undefined_form(arena, id))
        return true;
    const NodeId folded = canonicalize(arena, id);
    return folded != kNoNode && has_undefined_form(arena, folded);
}

namespace {

NodeId rest_key(Arena &arena, const std::vector<NodeId> &factors) {
    if (factors.empty())
        return kNoNode;
    if (factors.size() == 1)
        return factors[0];
    return arena.nary(Kind::Mul, factors);
}

// Whether a canonical sum cancels once terms differing only by a whole-number coefficient are
// gathered. It decides y - y, 2y - y - y and sin(y) - sin(y). It declines a coefficient that is not
// a whole number, anything needing powers gathered first such as x*x - x^2, and (x+1) - (x+1),
// where canonicalize flattens the bare bracket into the outer sum while the negated one stays inside
// a product, so the two stop looking alike. Declining reads as Unknown, which allows: refusing a
// divisor that might be fine would break working solves, and missing one is only today's behaviour.
bool sums_to_zero(Arena &arena, NodeId id) {
    std::vector<NodeId> keys;
    std::vector<int64_t> totals;
    for (NodeId term : arena.children(arena.at(id))) {
        int64_t coefficient = 1;
        NodeId key = term;
        const Node &t = arena.at(term);
        if (t.kind == Kind::Integer) {
            if (!integer_value(arena, term, &coefficient))
                return false;
            key = kNoNode;
        } else if (t.kind == Kind::Mul) {
            std::vector<NodeId> others;
            bool taken = false;
            for (NodeId factor : arena.children(t)) {
                int64_t value;
                if (!taken && integer_value(arena, factor, &value)) {
                    coefficient = value;
                    taken = true;
                } else {
                    others.push_back(factor);
                }
            }
            key = rest_key(arena, others);
            if (key == kNoNode)
                return false;
        }
        size_t at = keys.size();
        for (size_t i = 0; i < keys.size() && at == keys.size(); ++i) {
            const bool same = keys[i] == kNoNode ? key == kNoNode
                                                 : key != kNoNode && compare(arena, keys[i], key) == 0;
            if (same)
                at = i;
        }
        if (at == keys.size()) {
            keys.push_back(key);
            totals.push_back(0);
        }
        if (add_overflows(totals[at], coefficient))
            return false;
        totals[at] += coefficient;
    }
    for (size_t i = 0; i < totals.size(); ++i) {
        if (totals[i] != 0)
            return false;
    }
    return true;
}

Sign sign_of(Arena &arena, NodeId id) {
    const NodeId folded = canonicalize(arena, id);
    if (folded == kNoNode)
        return Sign::Unknown;
    const Node &n = arena.at(folded);
    if (n.kind == Kind::Integer || n.kind == Kind::Decimal)
        return literal_sign(arena, folded);
    if (n.kind == Kind::Mul) {
        bool negative = false;
        bool unreadable = false;
        for (NodeId factor : arena.children(n)) {
            const Sign sign = sign_of(arena, factor);
            if (sign == Sign::Zero)
                return Sign::Zero;
            if (sign == Sign::Unknown)
                unreadable = true;
            negative = negative != (sign == Sign::Negative);
        }
        if (unreadable)
            return Sign::Unknown;
        return negative ? Sign::Negative : Sign::Positive;
    }
    if (n.kind == Kind::Pow) {
        const ChildView children = arena.children(n);
        int64_t exponent;
        if (children.size() == 2 && folded_integer(arena, children[1], &exponent) && exponent > 0 &&
            sign_of(arena, children[0]) == Sign::Zero)
            return Sign::Zero;
        return Sign::Unknown;
    }
    // Coefficient shapes the collector leaves untouched still need this exact zero fallback.
    if (n.kind == Kind::Add && sums_to_zero(arena, folded))
        return Sign::Zero;
    return Sign::Unknown;
}

// Whether the form alone puts the expression away from zero, so asking it to be non-zero constrains
// nothing. A reciprocal is never zero where it has a value, and a product is non-zero exactly when
// every factor is. A sum is not here and must not be: 1/a + 1/b vanishes at a = -b, which is the
// lens equation, so that one stays open and is stated.
bool settles_nonzero(Arena &arena, NodeId id) {
    const NodeId folded = canonicalize(arena, id);
    if (folded == kNoNode)
        return false;
    const Node &n = arena.at(folded);
    const ChildView children = arena.children(n);
    if (n.kind == Kind::Pow && children.size() == 2) {
        int64_t exponent;
        return folded_integer(arena, children[1], &exponent) && exponent < 0;
    }
    if (n.kind == Kind::Mul && children.size() > 0) {
        for (size_t i = 0; i < children.size(); ++i) {
            if (decide(arena, children[i], Condition::NonZero) != Decision::Holds)
                return false;
        }
        return true;
    }
    return false;
}

void collect(Arena &arena, NodeId id, std::vector<Restriction> *out, bool *unmeetable);

void require(Arena &arena, NodeId subject, Condition condition, std::vector<Restriction> *out,
             bool *unmeetable) {
    switch (decide(arena, subject, condition)) {
        case Decision::Holds: return;
        case Decision::Fails: *unmeetable = true; return;
        case Decision::Unknown: break;
    }
    merge_restriction(out, Restriction{subject, condition});
}

void collect(Arena &arena, NodeId id, std::vector<Restriction> *out, bool *unmeetable) {
    if (id == kNoNode)
        return;
    const Node &n = arena.at(id);
    const ChildView children = arena.children(n);
    if (n.kind == Kind::Pow && children.size() == 2) {
        int64_t exponent;
        if (folded_integer(arena, children[1], &exponent) && exponent < 0)
            require(arena, children[0], Condition::NonZero, out, unmeetable);
    }
    if (n.kind == Kind::Call && children.size() == 1) {
        const std::string &name = arena.text(id);
        if (name == "ln" || name == "log")
            require(arena, children[0], Condition::Positive, out, unmeetable);
        else if (name == "sqrt")
            require(arena, children[0], Condition::NonNegative, out, unmeetable);
    }
    for (size_t i = 0; i < children.size(); ++i)
        collect(arena, children[i], out, unmeetable);
}

}  // namespace

Decision decide(Arena &arena, NodeId id, Condition condition) {
    if (condition == Condition::NonZero && settles_nonzero(arena, id))
        return Decision::Holds;
    const Sign sign = sign_of(arena, id);
    if (sign == Sign::Unknown)
        return Decision::Unknown;
    switch (condition) {
        case Condition::NonZero:
            return sign == Sign::Zero ? Decision::Fails : Decision::Holds;
        case Condition::Positive:
            return sign == Sign::Positive ? Decision::Holds : Decision::Fails;
        case Condition::NonNegative:
            return sign == Sign::Negative ? Decision::Fails : Decision::Holds;
    }
    return Decision::Unknown;
}

std::vector<Restriction> restrictions_of(Arena &arena, NodeId id) {
    std::vector<Restriction> found;
    bool unmeetable = false;
    collect(arena, id, &found, &unmeetable);
    return found;
}

bool has_unmeetable_condition(Arena &arena, NodeId id) {
    std::vector<Restriction> found;
    bool unmeetable = false;
    collect(arena, id, &found, &unmeetable);
    return unmeetable;
}

void merge_restriction(std::vector<Restriction> *into, const Restriction &r) {
    for (size_t i = 0; i < into->size(); ++i) {
        if (subsumes((*into)[i], r))
            return;
    }
    for (size_t i = into->size(); i > 0; --i) {
        if (subsumes(r, (*into)[i - 1]))
            into->erase(into->begin() + static_cast<long>(i - 1));
    }
    into->push_back(r);
}

bool subsumes(const Restriction &held, const Restriction &candidate) {
    if (held.subject != candidate.subject)
        return false;
    if (held.condition == candidate.condition)
        return true;
    return held.condition == Condition::Positive && candidate.condition == Condition::NonZero;
}

bool has_decimal(const Arena &arena, NodeId id) {
    return arena.any_node(id, [&arena](NodeId current) {
        return arena.at(current).kind == Kind::Decimal;
    });
}

bool has_decimal_exponent(const Arena &arena, NodeId id) {
    if (id == kNoNode || id >= arena.node_count())
        return false;
    struct ExponentVisit {
        NodeId id;
        bool inside;
    };
    std::vector<ExponentVisit> pending{{id, false}};
    // A shared node can occur both inside and outside an exponent.
    std::unordered_set<NodeId> outside_exponent;
    std::unordered_set<NodeId> inside_exponent;
    while (!pending.empty()) {
        const ExponentVisit visit = pending.back();
        pending.pop_back();
        auto &visited = visit.inside ? inside_exponent : outside_exponent;
        if (!visited.insert(visit.id).second)
            continue;
        const Node &node = arena.at(visit.id);
        if (visit.inside && node.kind == Kind::Decimal)
            return true;
        const ChildView children = arena.children(node);
        for (size_t i = children.size(); i > 0; --i) {
            const bool inside = visit.inside ||
                                (node.kind == Kind::Pow && children.size() == 2 && i == 2);
            pending.push_back({children[i - 1], inside});
        }
    }
    return false;
}

namespace {

struct ConversionFrame {
    NodeId id;
    std::vector<NodeId> children;
    std::vector<NodeId> converted;
    size_t next_child = 0;
    NodeId value = kNoNode;
    bool product = false;
};

bool prepare_conversion(Arena &arena, NodeId id, bool to_decimal, ConversionFrame *frame) {
    frame->id = id;
    const Node &node = arena.at(id);
    if (!to_decimal && node.kind == Kind::Decimal) {
        Rational value;
        if (!rational_from_text(arena.text(id), &value))
            return false;
        frame->value = rational_node(arena, value);
        return frame->value != kNoNode;
    }
    if (node.child_count == 0) {
        frame->value = id;
        return true;
    }
    const ChildView children = arena.children(node);
    if (to_decimal && (node.kind == Kind::Mul || node.kind == Kind::Pow) &&
        !contains_list(arena, id)) {
        std::vector<NodeId> factors;
        if (node.kind == Kind::Mul) {
            for (NodeId child : children)
                factors.push_back(child);
        } else {
            factors.push_back(id);
        }
        std::string text;
        std::vector<NodeId> rest;
        if (read_decimal_coefficient(arena, factors, &text, &rest)) {
            const NodeId written = arena.decimal(text);
            if (written == kNoNode)
                return false;
            if (rest.empty()) {
                frame->value = written;
            } else {
                frame->children = std::move(rest);
                frame->converted.push_back(written);
                frame->product = true;
            }
            return true;
        }
    }
    for (NodeId child : children)
        frame->children.push_back(child);
    frame->converted.reserve(frame->children.size());
    return true;
}

NodeId convert_numeric(Arena &arena, NodeId id, bool to_decimal) {
    if (!transformable(arena, id))
        return kNoNode;
    ConversionFrame first;
    if (!prepare_conversion(arena, id, to_decimal, &first))
        return kNoNode;
    std::vector<ConversionFrame> pending;
    pending.push_back(std::move(first));
    std::unordered_map<NodeId, NodeId> completed;
    while (!pending.empty()) {
        ConversionFrame &frame = pending.back();
        if (frame.next_child < frame.children.size()) {
            const NodeId child = frame.children[frame.next_child];
            const auto found = completed.find(child);
            if (found == completed.end()) {
                ConversionFrame next;
                if (!prepare_conversion(arena, child, to_decimal, &next))
                    return kNoNode;
                pending.push_back(std::move(next));
                continue;
            }
            frame.converted.push_back(found->second);
            ++frame.next_child;
            continue;
        }
        NodeId converted = frame.value;
        if (converted == kNoNode) {
            converted = frame.product ? arena.nary(Kind::Mul, frame.converted)
                                      : (frame.children == frame.converted ? frame.id
                                           : rebuild(arena, frame.id, frame.converted));
        }
        if (converted == kNoNode)
            return kNoNode;
        completed.emplace(frame.id, converted);
        pending.pop_back();
        if (pending.empty())
            return converted;
    }
    return kNoNode;
}

}

NodeId exactify(Arena &arena, NodeId id) {
    return convert_numeric(arena, id, false);
}

NodeId decimalize(Arena &arena, NodeId id) {
    return convert_numeric(arena, id, true);
}

const char *exactness_name(Exactness e) {
    switch (e) {
        case Exactness::NotALiteral: return "not a literal";
        case Exactness::ExactRational: return "exact rational";
        case Exactness::ExactConstant: return "exact constant";
        case Exactness::Measured: return "measured";
        case Exactness::Approximate: return "approximate";
    }
    return "not a literal";
}

Exactness exactness(const Arena &arena, NodeId id) {
    if (id == kNoNode)
        return Exactness::NotALiteral;
    const Node &n = arena.at(id);
    switch (n.kind) {
        case Kind::Integer: return Exactness::ExactRational;
        case Kind::Decimal:
            // Asked before the spelling, because a backend's float and a typed decimal are the same
            // Kind and only the mark tells them apart.
            return arena.is_approximate(id) ? Exactness::Approximate : Exactness::Measured;
        case Kind::Symbol:
            // pi and nothing else. The Giac bridge special cases pi alone (giac_typed.cc:260), so
            // naming e a constant here would leave the two disagreeing about what is exact.
            return arena.text(id) == "pi" ? Exactness::ExactConstant : Exactness::NotALiteral;
        default: return Exactness::NotALiteral;
    }
}

bool inexact(Exactness e) {
    return e == Exactness::Measured || e == Exactness::Approximate;
}

std::string restriction_text(const Arena &arena, const Restriction &r) {
    std::string subject = print(arena, r.subject);
    switch (r.condition) {
        case Condition::NonZero: return subject + " is not zero";
        case Condition::Positive: return subject + " > 0";
        case Condition::NonNegative: return subject + " >= 0";
    }
    return subject;
}

}  // namespace nps
