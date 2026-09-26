#include "nps/steps/power.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <tuple>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/steps/rewrite.h"

namespace nps {
namespace {

constexpr size_t kValueBits = 20000;
constexpr int64_t kMaxLcm = 12;
constexpr int64_t kMaxExponent = 1024;

// A GMP rational that can be copied, for values carried through the evaluator.
class Q {
  public:
    Q() { mpq_init(v_); }
    Q(const Q &other) {
        mpq_init(v_);
        mpq_set(v_, other.v_);
    }
    Q &operator=(const Q &other) {
        mpq_set(v_, other.v_);
        return *this;
    }
    ~Q() { mpq_clear(v_); }
    mpq_ptr get() { return v_; }
    mpq_srcptr get() const { return v_; }

  private:
    mpq_t v_;
};

bool small(const Q &q) {
    return mpz_sizeinbase(mpq_numref(q.get()), 2) <= kValueBits && mpz_sizeinbase(mpq_denref(q.get()), 2) <= kValueBits;
}

bool symbol_free(const Arena &arena, NodeId id) {
    return !arena.any_node(id, [&arena](NodeId n) { return arena.at(n).kind == Kind::Symbol; });
}

// The q-th real root of value when it is rational. An even root of a negative number has none.
bool exact_root(mpq_srcptr value, unsigned long q, mpq_ptr out) {
    const int sign = mpq_sgn(value);
    if (sign < 0 && q % 2 == 0)
        return false;
    detail::Mpz num, den;
    mpz_abs(num.get(), mpq_numref(value));
    if (!mpz_root(num.get(), num.get(), q) || !mpz_root(den.get(), mpq_denref(value), q))
        return false;
    mpz_set(mpq_numref(out), num.get());
    mpz_set(mpq_denref(out), den.get());
    mpq_canonicalize(out);
    if (sign < 0)
        mpq_neg(out, out);
    return true;
}

bool raise(mpq_srcptr value, int64_t power, mpq_ptr out) {
    if (power < 0 && mpq_sgn(value) == 0)
        return false;
    const unsigned long magnitude = static_cast<unsigned long>(power < 0 ? -power : power);
    detail::Mpz num, den;
    mpz_pow_ui(num.get(), mpq_numref(value), magnitude);
    mpz_pow_ui(den.get(), mpq_denref(value), magnitude);
    mpz_set(mpq_numref(out), power < 0 ? den.get() : num.get());
    mpz_set(mpq_denref(out), power < 0 ? num.get() : den.get());
    mpq_canonicalize(out);
    return true;
}

bool constant_exponent(const Arena &arena, NodeId id, Rational *out) {
    return symbol_free(arena, id) && !arena.is_approximate(id) && evaluate_rational(arena, id, {}, out) && out->den > 0;
}

// Trial division up to this bound, beyond which a leftover factor is refused rather than guessed prime.
constexpr unsigned long kTrialLimit = 100000;
constexpr size_t kMaxTerms = 256;

// A product of real roots of distinct primes, each exponent strictly between zero and one.
using RadicalKey = std::vector<std::tuple<unsigned long, int64_t, int64_t>>;

// A value as a sum of rational multiples of distinct radical keys. Those keys are linearly
// independent over the rationals, so two values are equal exactly when their maps are.
using RadicalSum = std::map<RadicalKey, Q>;

enum class Eval { Known, NoRealValue, CannotCompute };

int64_t floor_div(int64_t num, int64_t den) {
    return num / den - ((num % den != 0) && ((num < 0) != (den < 0)) ? 1 : 0);
}

// Multiply coefficient by prime to the exponent's whole part and keep the fraction in the key.
bool carry(unsigned long prime, Rational exponent, RadicalKey *key, Q *coefficient) {
    const int64_t whole = floor_div(exponent.num, exponent.den);
    Rational fraction;
    if (!rational_sub(exponent, Rational{whole, 1}, &fraction))
        return false;
    if (whole > static_cast<int64_t>(kValueBits) || whole < -static_cast<int64_t>(kValueBits))
        return false;
    detail::Mpz power;
    mpz_ui_pow_ui(power.get(), prime, static_cast<unsigned long>(whole < 0 ? -whole : whole));
    if (whole >= 0)
        mpz_mul(mpq_numref(coefficient->get()), mpq_numref(coefficient->get()), power.get());
    else
        mpz_mul(mpq_denref(coefficient->get()), mpq_denref(coefficient->get()), power.get());
    mpq_canonicalize(coefficient->get());
    if (fraction.num != 0)
        key->emplace_back(prime, fraction.num, fraction.den);
    return small(*coefficient);
}

// The prime factors of a positive integer with their multiplicities, or false past the trial bound.
bool factor(mpz_srcptr value, std::vector<std::pair<unsigned long, unsigned long>> *out) {
    detail::Mpz rest;
    mpz_set(rest.get(), value);
    for (unsigned long d = 2; mpz_cmp_ui(rest.get(), d * d) >= 0; ++d) {
        if (d > kTrialLimit)
            return false;
        unsigned long count = 0;
        while (mpz_divisible_ui_p(rest.get(), d)) {
            mpz_divexact_ui(rest.get(), rest.get(), d);
            ++count;
        }
        if (count > 0)
            out->emplace_back(d, count);
    }
    if (mpz_cmp_ui(rest.get(), 1) > 0) {
        if (!mpz_fits_ulong_p(rest.get()))
            return false;
        out->emplace_back(mpz_get_ui(rest.get()), 1);
    }
    return true;
}

bool add_to_key(std::map<unsigned long, Rational> *exponents, unsigned long prime, const Rational &e) {
    auto found = exponents->find(prime);
    if (found == exponents->end()) {
        exponents->emplace(prime, e);
        return true;
    }
    return rational_add(found->second, e, &found->second);
}

bool settle(const std::map<unsigned long, Rational> &exponents, RadicalKey *key, Q *coefficient) {
    for (const auto &[prime, e] : exponents) {
        if (!carry(prime, e, key, coefficient))
            return false;
    }
    return true;
}

bool multiply_terms(const RadicalKey &a, const Q &ca, const RadicalKey &b, const Q &cb, RadicalKey *key, Q *coefficient) {
    std::map<unsigned long, Rational> exponents;
    for (const RadicalKey *side : {&a, &b}) {
        for (const auto &[prime, num, den] : *side) {
            if (!add_to_key(&exponents, prime, Rational{num, den}))
                return false;
        }
    }
    mpq_mul(coefficient->get(), ca.get(), cb.get());
    return settle(exponents, key, coefficient);
}

// The real power p/q of a single term, with roots of its coefficient pulled apart prime by prime.
Eval power_term(const RadicalKey &key, const Q &c, const Rational &power, RadicalSum *out) {
    const int sign = mpq_sgn(c.get());
    if (sign == 0) {
        if (power.num < 0)
            return Eval::NoRealValue;
        Q one;
        mpq_set_ui(one.get(), power.num == 0 ? 1 : 0, 1);
        out->clear();
        if (power.num == 0)
            (*out)[RadicalKey()] = one;
        return Eval::Known;
    }
    if (sign < 0 && power.den % 2 == 0)
        return Eval::NoRealValue;
    Q rooted;
    if (key.empty() && exact_root(c.get(), static_cast<unsigned long>(power.den), rooted.get())) {
        Q raised;
        if (!raise(rooted.get(), power.num, raised.get()) || !small(raised))
            return Eval::CannotCompute;
        out->clear();
        (*out)[RadicalKey()] = raised;
        return Eval::Known;
    }
    std::map<unsigned long, Rational> exponents;
    std::vector<std::pair<unsigned long, unsigned long>> top, bottom;
    detail::Mpz magnitude_num;
    mpz_abs(magnitude_num.get(), mpq_numref(c.get()));
    if (!factor(magnitude_num.get(), &top) || !factor(mpq_denref(c.get()), &bottom))
        return Eval::CannotCompute;
    for (const auto &[prime, count] : top) {
        Rational e;
        if (!rational_mul(Rational{static_cast<int64_t>(count), 1}, power, &e) || !add_to_key(&exponents, prime, e))
            return Eval::CannotCompute;
    }
    for (const auto &[prime, count] : bottom) {
        Rational e;
        if (!rational_mul(Rational{-static_cast<int64_t>(count), 1}, power, &e) || !add_to_key(&exponents, prime, e))
            return Eval::CannotCompute;
    }
    for (const auto &[prime, num, den] : key) {
        Rational e;
        if (!rational_mul(Rational{num, den}, power, &e) || !add_to_key(&exponents, prime, e))
            return Eval::CannotCompute;
    }
    Q coefficient;
    mpq_set_si(coefficient.get(), sign < 0 && power.num % 2 != 0 ? -1 : 1, 1);
    RadicalKey result;
    if (!settle(exponents, &result, &coefficient))
        return Eval::CannotCompute;
    out->clear();
    (*out)[result] = coefficient;
    return Eval::Known;
}

void drop_zeros(RadicalSum *v) {
    for (auto it = v->begin(); it != v->end();) {
        if (mpq_sgn(it->second.get()) == 0)
            it = v->erase(it);
        else
            ++it;
    }
}

bool single_term(const RadicalSum &v, RadicalKey *key, Q *c) {
    if (v.empty()) {
        key->clear();
        mpq_set_ui(c->get(), 0, 1);
        return true;
    }
    if (v.size() != 1)
        return false;
    *key = v.begin()->first;
    *c = v.begin()->second;
    return true;
}

// The exact value at one point, with rational exponents taken as real roots.
Eval evaluate(const Arena &arena, NodeId id, mpq_srcptr x, RadicalSum *out, int depth = 0) {
    if (depth > 64 || arena.is_approximate(id))
        return Eval::CannotCompute;
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(id);
    out->clear();
    switch (n.kind) {
        case Kind::Integer: {
            Q c;
            if (mpz_set_str(mpq_numref(c.get()), arena.text(id).c_str(), 10) != 0 || !small(c))
                return Eval::CannotCompute;
            (*out)[RadicalKey()] = c;
            drop_zeros(out);
            return Eval::Known;
        }
        case Kind::Symbol: {
            Q c;
            mpq_set(c.get(), x);
            (*out)[RadicalKey()] = c;
            drop_zeros(out);
            return Eval::Known;
        }
        case Kind::Add: {
            for (NodeId child : kids) {
                RadicalSum part;
                const Eval e = evaluate(arena, child, x, &part, depth + 1);
                if (e != Eval::Known)
                    return e;
                for (const auto &[key, c] : part) {
                    Q &slot = (*out)[key];
                    mpq_add(slot.get(), slot.get(), c.get());
                    if (!small(slot))
                        return Eval::CannotCompute;
                }
            }
            drop_zeros(out);
            return out->size() <= kMaxTerms ? Eval::Known : Eval::CannotCompute;
        }
        case Kind::Mul: {
            Q one;
            mpq_set_ui(one.get(), 1, 1);
            (*out)[RadicalKey()] = one;
            for (NodeId child : kids) {
                RadicalSum part;
                const Eval e = evaluate(arena, child, x, &part, depth + 1);
                if (e != Eval::Known)
                    return e;
                RadicalSum product;
                for (const auto &[ka, ca] : *out) {
                    for (const auto &[kb, cb] : part) {
                        RadicalKey key;
                        Q c;
                        if (!multiply_terms(ka, ca, kb, cb, &key, &c))
                            return Eval::CannotCompute;
                        Q &slot = product[key];
                        mpq_add(slot.get(), slot.get(), c.get());
                        if (!small(slot))
                            return Eval::CannotCompute;
                    }
                }
                drop_zeros(&product);
                if (product.size() > kMaxTerms)
                    return Eval::CannotCompute;
                *out = std::move(product);
            }
            return Eval::Known;
        }
        case Kind::Neg: {
            const Eval e = evaluate(arena, kids[0], x, out, depth + 1);
            for (auto &[key, c] : *out)
                mpq_neg(c.get(), c.get());
            return e;
        }
        case Kind::Pow: {
            Rational exponent;
            if (!constant_exponent(arena, kids[1], &exponent))
                return Eval::CannotCompute;
            RadicalSum base;
            const Eval e = evaluate(arena, kids[0], x, &base, depth + 1);
            if (e != Eval::Known)
                return e;
            RadicalKey key;
            Q c;
            if (!single_term(base, &key, &c))
                return Eval::CannotCompute;
            return power_term(key, c, exponent, out);
        }
        case Kind::Call: {
            if (kids.size() != 1)
                return Eval::CannotCompute;
            RadicalSum inner;
            const Eval e = evaluate(arena, kids[0], x, &inner, depth + 1);
            if (e != Eval::Known)
                return e;
            RadicalKey key;
            Q c;
            if (!single_term(inner, &key, &c))
                return Eval::CannotCompute;
            if (arena.text(id) == "sqrt")
                return power_term(key, c, Rational{1, 2}, out);
            if (arena.text(id) == "abs") {
                mpq_abs(c.get(), c.get());
                *out = std::move(inner);
                if (!out->empty())
                    out->begin()->second = c;
                return Eval::Known;
            }
            return Eval::CannotCompute;
        }
        default:
            return Eval::CannotCompute;
    }
}

bool same_value(const RadicalSum &a, const RadicalSum &b) {
    if (a.size() != b.size())
        return false;
    for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
        if (i->first != j->first || !mpq_equal(i->second.get(), j->second.get()))
            return false;
    }
    return true;
}

// What the check and the envelope need from a form: its one variable, the least common denominator
// of its exponents and a bound on its degree in that variable.
struct Shape {
    std::string symbol;
    int64_t lcm = 1;
    Rational degree{0, 1};
};

bool monomial(const Arena &arena, NodeId id) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Integer || n.kind == Kind::Symbol)
        return true;
    if (n.kind == Kind::Neg)
        return monomial(arena, arena.children(id)[0]);
    if (n.kind == Kind::Mul) {
        for (NodeId child : arena.children(id)) {
            if (!monomial(arena, child))
                return false;
        }
        return true;
    }
    if (n.kind == Kind::Pow)
        return monomial(arena, arena.children(id)[0]) && symbol_free(arena, arena.children(id)[1]);
    if (n.kind == Kind::Call && arena.children(id).size() == 1 &&
        (arena.text(id) == "sqrt" || arena.text(id) == "abs"))
        return monomial(arena, arena.children(id)[0]);
    return false;
}

bool read_shape(const Arena &arena, NodeId id, Shape *shape, Rational *degree, int depth = 0) {
    if (depth > 64 || arena.is_approximate(id))
        return false;
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(id);
    *degree = Rational{0, 1};
    switch (n.kind) {
        case Kind::Integer:
            return true;
        case Kind::Symbol:
            if (!shape->symbol.empty() && shape->symbol != arena.text(id))
                return false;
            shape->symbol = arena.text(id);
            *degree = Rational{1, 1};
            return true;
        case Kind::Neg:
            return read_shape(arena, kids[0], shape, degree, depth + 1);
        case Kind::Add:
        case Kind::Mul:
            for (NodeId child : kids) {
                Rational part;
                if (!read_shape(arena, child, shape, &part, depth + 1))
                    return false;
                if (n.kind == Kind::Mul) {
                    if (!rational_add(*degree, part, degree))
                        return false;
                } else if (part.num * degree->den > degree->num * part.den) {
                    *degree = part;
                }
            }
            return true;
        case Kind::Pow: {
            Rational exponent, base;
            if (!constant_exponent(arena, kids[1], &exponent) || !read_shape(arena, kids[0], shape, &base, depth + 1))
                return false;
            if (exponent.den != 1 && !monomial(arena, kids[0]))
                return false;
            if (exponent.num > kMaxExponent || exponent.num < -kMaxExponent)
                return false;
            shape->lcm = std::lcm(shape->lcm, exponent.den);
            const Rational magnitude{exponent.num < 0 ? -exponent.num : exponent.num, exponent.den};
            return shape->lcm <= kMaxLcm && rational_mul(base, magnitude, degree);
        }
        case Kind::Call: {
            if (kids.size() != 1 || (arena.text(id) != "sqrt" && arena.text(id) != "abs"))
                return false;
            Rational inner;
            if (!read_shape(arena, kids[0], shape, &inner, depth + 1))
                return false;
            if (arena.text(id) == "sqrt") {
                if (!monomial(arena, kids[0]))
                    return false;
                shape->lcm = std::lcm(shape->lcm, int64_t{2});
                return rational_mul(inner, Rational{1, 2}, degree);
            }
            *degree = inner;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

PowerReading power_equivalent(const Arena &arena, NodeId before, NodeId after, std::string *why) {
    Shape shape;
    Rational d_before, d_after;
    if (!read_shape(arena, before, &shape, &d_before) || !read_shape(arena, after, &shape, &d_after)) {
        *why = "a form is outside powers and roots of monomials in one variable";
        return PowerReading::Unreadable;
    }
    const int64_t L = 2 * shape.lcm;
    const Rational top = d_before.num * d_after.den > d_after.num * d_before.den ? d_before : d_after;
    const int64_t bound = (top.num * L + top.den - 1) / top.den;
    const int64_t needed = 2 * bound + 1;
    // Each branch x = t^L and x = -t^L makes both sides Laurent polynomials in t with at most
    // 2 bound + 1 terms, so agreeing at that many points of a branch proves them equal on it.
    bool any = false;
    for (int sign : {1, -1}) {
        int64_t agreed = 0;
        bool defined = false;
        for (int64_t j = 0; j < needed + 4 && agreed < needed; ++j) {
            Q t, x;
            RadicalSum left, right;
            mpq_set_si(t.get(), j + 2, 2);
            mpq_canonicalize(t.get());
            raise(t.get(), L, x.get());
            if (sign < 0)
                mpq_neg(x.get(), x.get());
            if (shape.symbol.empty() && sign < 0)
                break;
            const Eval old_value = evaluate(arena, before, x.get(), &left);
            if (old_value == Eval::NoRealValue)
                continue;
            if (old_value == Eval::CannotCompute) {
                *why = "the old form has a value this check cannot compute exactly";
                return PowerReading::Unreadable;
            }
            defined = true;
            const Eval new_value = evaluate(arena, after, x.get(), &right);
            if (new_value == Eval::NoRealValue) {
                *why = "the new form has no value where the old one does";
                return PowerReading::Different;
            }
            if (new_value == Eval::CannotCompute) {
                *why = "the new form has a value this check cannot compute exactly";
                return PowerReading::Unreadable;
            }
            if (!same_value(left, right)) {
                *why = "the two forms differ at a point where both have a value";
                return PowerReading::Different;
            }
            ++agreed;
        }
        if (defined && agreed < needed) {
            *why = "too few points had a value to fix the degree";
            return PowerReading::Unreadable;
        }
        any = any || defined;
    }
    if (!any) {
        *why = "the old form has no real value at any point checked";
        return PowerReading::Unreadable;
    }
    *why = "both forms agree on every branch of the old form's real domain, at more points than their difference could have roots";
    return PowerReading::Equal;
}

namespace {

struct Pass {
    const char *rule_id = nullptr;
    const char *rule_name = nullptr;
    std::string law;
    std::vector<Restriction> conditions;
};

NodeId exponent_node(Arena &arena, const Rational &value) {
    return canonical_rational(arena, value);
}

bool power_parts(const Arena &arena, NodeId id, NodeId *base, Rational *exponent) {
    if (arena.at(id).kind == Kind::Pow && constant_exponent(arena, arena.children(id)[1], exponent)) {
        *base = arena.children(id)[0];
        return true;
    }
    *base = id;
    *exponent = Rational{1, 1};
    return false;
}

NodeId as_power(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    if (arena.at(id).kind != Kind::Call || arena.text(id) != "sqrt" || arena.children(id).size() != 1)
        return kNoNode;
    *pass = Pass{"pow.root-as-power", "Write a square root as a power", "sqrt(u) = u^(1/2)", {}};
    *what = "Write " + print(arena, id) + " as a power of one half";
    return arena.binary(Kind::Pow, arena.children(id)[0], exponent_node(arena, Rational{1, 2}));
}

NodeId numeric_root(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    Rational exponent, value;
    if (arena.at(id).kind != Kind::Pow || !constant_exponent(arena, arena.children(id)[1], &exponent) ||
        exponent.den == 1 || !symbol_free(arena, arena.children(id)[0]) ||
        !evaluate_rational(arena, arena.children(id)[0], {}, &value) || value.den != 1)
        return kNoNode;
    const bool negative = value.num < 0;
    int64_t rest = negative ? -value.num : value.num;
    if (negative && exponent.den % 2 == 0)
        return kNoNode;
    int64_t out = 1;
    for (int64_t k = 2; k <= 1000; ++k) {
        int64_t power = 1;
        bool fits = true;
        for (int64_t i = 0; i < exponent.den && fits; ++i)
            fits = !__builtin_mul_overflow(power, k, &power) && power <= rest;
        if (!fits)
            break;
        while (rest % power == 0 && rest > 0) {
            rest /= power;
            out *= k;
        }
    }
    if (out == 1 && rest != 1 && !negative)
        return kNoNode;
    Rational coefficient{1, 1};
    if (!rational_power(Rational{out, 1}, exponent.num, &coefficient))
        return kNoNode;
    if (negative && exponent.num % 2 != 0)
        coefficient.num = -coefficient.num;
    *pass = Pass{"pow.numeric-root", "Take the root of a number",
                 "the largest perfect power comes out of a root, and an odd root of a negative number is negative", {}};
    NodeId result = canonical_rational(arena, coefficient);
    if (rest != 1)
        result = arena.binary(Kind::Mul, result,
                              arena.binary(Kind::Pow, arena.integer(std::to_string(rest)), exponent_node(arena, exponent)));
    *what = "Take " + print(arena, id) + " as " + print(arena, result);
    return result;
}

NodeId power_of_power(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    Rational b, a;
    NodeId inner_base = kNoNode;
    if (arena.at(id).kind != Kind::Pow || !constant_exponent(arena, arena.children(id)[1], &b) ||
        !power_parts(arena, arena.children(id)[0], &inner_base, &a) || arena.at(arena.children(id)[0]).kind != Kind::Pow)
        return kNoNode;
    Rational product;
    if (!rational_mul(a, b, &product))
        return kNoNode;
    NodeId base = inner_base;
    std::vector<Restriction> conditions;
    if (b.den != 1) {
        if (a.den == 1 && a.num % 2 == 0) {
            base = arena.call("abs", {inner_base});
        } else if (a.den == 1) {
            if (b.den % 2 == 0)
                conditions.push_back({inner_base, Condition::NonNegative});
        } else if (a.den % 2 == 0) {
            conditions.push_back({inner_base, Condition::NonNegative});
        } else {
            return kNoNode;
        }
    }
    *pass = Pass{"pow.power-of-power", "Multiply the exponents of a power of a power",
                 base != inner_base ? "(u^a)^b = |u|^(ab) when a is even and b is not whole"
                                    : "(u^a)^b = u^(ab), which needs u not negative when an even root is taken",
                 conditions};
    *what = "Multiply the exponents in " + print(arena, id);
    if (product.num == product.den)
        return base;
    return arena.binary(Kind::Pow, base, exponent_node(arena, product));
}

bool known_nonnegative(const Arena &arena, NodeId id) {
    Rational value, exponent;
    if (symbol_free(arena, id) && evaluate_rational(arena, id, {}, &value))
        return value.num >= 0;
    if (arena.at(id).kind == Kind::Call && arena.text(id) == "abs")
        return true;
    return arena.at(id).kind == Kind::Pow && constant_exponent(arena, arena.children(id)[1], &exponent) &&
           exponent.den == 1 && exponent.num % 2 == 0;
}

NodeId product_power(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    Rational exponent;
    if (arena.at(id).kind != Kind::Pow || arena.at(arena.children(id)[0]).kind != Kind::Mul ||
        !constant_exponent(arena, arena.children(id)[1], &exponent))
        return kNoNode;
    const ChildView factors = arena.children(arena.children(id)[0]);
    if (exponent.den % 2 == 0) {
        for (NodeId f : factors) {
            if (!known_nonnegative(arena, f))
                return kNoNode;
        }
    }
    std::vector<NodeId> powered;
    for (NodeId f : factors)
        powered.push_back(arena.binary(Kind::Pow, f, arena.children(id)[1]));
    *pass = Pass{"pow.product-power", "Raise each factor of a product",
                 "(uv)^r = u^r v^r, which holds for a root of even index only over factors that are not negative", {}};
    *what = "Raise each factor of " + print(arena, arena.children(id)[0]);
    return arena.nary(Kind::Mul, powered);
}

NodeId same_base(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    if (arena.at(id).kind != Kind::Mul)
        return kNoNode;
    const ChildView factors = arena.children(id);
    for (size_t i = 0; i < factors.size(); ++i) {
        NodeId base_i;
        Rational e_i;
        power_parts(arena, factors[i], &base_i, &e_i);
        if (symbol_free(arena, base_i))
            continue;
        std::vector<size_t> members{i};
        Rational sum = e_i;
        bool even_root = e_i.den % 2 == 0, negative = e_i.num < 0;
        for (size_t j = i + 1; j < factors.size(); ++j) {
            NodeId base_j;
            Rational e_j;
            power_parts(arena, factors[j], &base_j, &e_j);
            if (base_j != base_i)
                continue;
            if (!rational_add(sum, e_j, &sum))
                return kNoNode;
            even_root = even_root || e_j.den % 2 == 0;
            negative = negative || e_j.num < 0;
            members.push_back(j);
        }
        if (members.size() < 2)
            continue;
        std::vector<Restriction> conditions;
        if (even_root)
            conditions.push_back({base_i, Condition::NonNegative});
        if (negative || sum.num == 0)
            conditions.push_back({base_i, Condition::NonZero});
        std::vector<NodeId> rest;
        for (size_t k = 0; k < factors.size(); ++k) {
            if (std::find(members.begin(), members.end(), k) == members.end())
                rest.push_back(factors[k]);
        }
        const NodeId combined = sum.num == 0 ? arena.integer("1")
                                : sum.num == sum.den ? base_i
                                                     : arena.binary(Kind::Pow, base_i, exponent_node(arena, sum));
        rest.insert(rest.begin(), combined);
        *pass = Pass{"pow.same-base", "Add the exponents of a common base",
                     "u^a u^b = u^(a+b), keeping u not zero for a negative exponent and not negative for an even root",
                     conditions};
        *what = "Add the exponents of " + print(arena, base_i);
        return rest.size() == 1 ? rest[0] : arena.nary(Kind::Mul, rest);
    }
    return kNoNode;
}

NodeId first_power(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    Rational exponent;
    if (arena.at(id).kind != Kind::Pow || !constant_exponent(arena, arena.children(id)[1], &exponent) ||
        exponent.num != exponent.den)
        return kNoNode;
    *pass = Pass{"pow.first-power", "A first power is the base", "u^1 = u", {}};
    *what = "Write " + print(arena, id) + " as its base";
    return arena.children(id)[0];
}

NodeId as_root(Arena &arena, NodeId id, void *state, std::string *what) {
    Pass *pass = static_cast<Pass *>(state);
    Rational exponent;
    if (arena.at(id).kind != Kind::Pow || !constant_exponent(arena, arena.children(id)[1], &exponent) ||
        exponent.num != 1 || exponent.den != 2)
        return kNoNode;
    *pass = Pass{"pow.power-as-root", "Write a power of one half as a square root", "u^(1/2) = sqrt(u)", {}};
    *what = "Write " + print(arena, id) + " as a square root";
    return arena.call("sqrt", {arena.children(id)[0]});
}

// An even root of a negative number, found anywhere, means the expression has no real value.
bool has_no_real_value(const Arena &arena, NodeId id) {
    return arena.any_node(id, [&arena](NodeId n) {
        const Node &node = arena.at(n);
        NodeId base = kNoNode;
        int64_t index = 0;
        if (node.kind == Kind::Call && arena.text(n) == "sqrt" && arena.children(n).size() == 1) {
            base = arena.children(n)[0];
            index = 2;
        } else if (node.kind == Kind::Pow) {
            Rational exponent;
            if (constant_exponent(arena, arena.children(n)[1], &exponent)) {
                base = arena.children(n)[0];
                index = exponent.den;
            }
        }
        Rational value;
        return base != kNoNode && index % 2 == 0 && symbol_free(arena, base) &&
               evaluate_rational(arena, base, {}, &value) && value.num < 0;
    });
}

struct Run {
    Arena &arena;
    Derivation &derivation;
    NodeId input;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    RestrictionSet recorded;
    bool failed = false;
    PowerOutcome failure = PowerOutcome::OutsideEnvelope;
    std::string detail;

    Run(Arena &a, Derivation &d, NodeId e, const Budget &budget)
        : arena(a), derivation(d), input(e), meter(budget), mark(d.mark()) {}

    bool refuse(PowerOutcome outcome, std::string why) {
        if (failed)
            return false;
        failed = true;
        failure = outcome;
        detail = std::move(why);
        return false;
    }

    bool running() {
        if (failed)
            return false;
        if (arena.failed())
            return refuse(PowerOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped() || !meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? PowerOutcome::Cancelled : PowerOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        return true;
    }

    bool step() {
        if (!meter.step()) {
            running();
            return false;
        }
        return running();
    }

    VerificationRecord exact(NodeId before, NodeId after, const char *obligation) {
        std::string why;
        const PowerReading reading = power_equivalent(arena, before, after, &why);
        const VerificationOutcome outcome = reading == PowerReading::Equal ? VerificationOutcome::Passed
                                            : reading == PowerReading::Different ? VerificationOutcome::Failed
                                                                                 : VerificationOutcome::Inconclusive;
        return {"exact evaluation on each branch beyond the degree", outcome,
                strength_for(outcome, EvidenceStrength::StructurallyValid), why, obligation};
    }

    bool record(const Pass &pass, NodeId before, NodeId after, const std::string &action) {
        if (!step())
            return false;
        Step s;
        s.phase = "Simplify";
        s.goal = "Apply one power law";
        s.rule_id = pass.rule_id;
        s.rule_name = pass.rule_name;
        s.claim = ClaimType::EquivalentExpression;
        s.explanation_short = "The law used is " + pass.law + ".";
        s.explanation_detailed = pass.conditions.empty()
            ? std::string("It holds wherever the expression has a real value, so no condition is needed.")
            : std::string("It holds only under the condition recorded on this step, which the result keeps.");
        s.proof_obligations.push_back({"obl.power.same-values", "the new form has the value of the old one wherever the old one is real"});
        s.verifications.push_back(exact(before, after, "obl.power.same-values"));
        const VerificationOutcome outcome = s.verifications.back().outcome;
        const std::string why = s.verifications.back().detail;
        TransformationPayload change;
        change.before = before;
        change.after = after;
        change.reversible = pass.conditions.empty();
        change.concrete_action = action + ".";
        const StepId here = derivation.add_transformation(plan, std::move(s), std::move(change));
        for (const Restriction &r : pass.conditions)
            recorded.add(r, here);
        if (!running())
            return false;
        if (outcome != VerificationOutcome::Passed)
            return refuse(outcome == VerificationOutcome::Failed ? PowerOutcome::VerificationFailed
                                                                 : PowerOutcome::OutsideEnvelope,
                          why);
        return true;
    }

    PowerResult finish(PowerOutcome outcome, NodeId expression, std::string why) {
        PowerResult result;
        result.outcome = outcome;
        result.detail = std::move(why);
        if (arena.failed()) {
            result.outcome = PowerOutcome::ResourceExceeded;
            result.detail = status_name(arena.status());
        }
        std::vector<std::string> assumptions;
        switch (result.outcome) {
            case PowerOutcome::Rewritten:
                result.expression = expression;
                recorded.settle(arena, derivation, &assumptions);
                result.status = derivation.outcome_from(mark);
                break;
            case PowerOutcome::AlreadyInForm:
                result.expression = expression;
                result.status = DerivationStatus::SolvedAndVerified;
                break;
            case PowerOutcome::NoRealValue: result.status = DerivationStatus::InvalidInput; break;
            case PowerOutcome::OutsideEnvelope: result.status = DerivationStatus::Unsupported; break;
            case PowerOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case PowerOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case PowerOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        if (result.outcome != PowerOutcome::Rewritten && result.outcome != PowerOutcome::AlreadyInForm) {
            recorded.settle(arena, derivation, &assumptions);
            assumptions.clear();
            keep_verified_prefix(derivation, mark, arena);
        }
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = "algebra.powers-and-radicals.one-variable";
        context.requested_method = "powsimp";
        context.normalized_problem_model = input;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = input < arena.node_count() ? print(arena, input) : std::string();
        context.numeric_mode = derivation.request.numeric_mode;
        context.active_assumptions = assumptions;
        context.angle_convention = "not applicable";
        context.branch_convention = "real domain, real odd roots, even roots only of values that are not negative";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    PowerResult stopped() { return finish(failure, kNoNode, detail); }
};

}  // namespace

const char *power_outcome_name(PowerOutcome outcome) {
    switch (outcome) {
        case PowerOutcome::Rewritten: return "rewritten";
        case PowerOutcome::AlreadyInForm: return "already in form";
        case PowerOutcome::NoRealValue: return "no real value";
        case PowerOutcome::OutsideEnvelope: return "outside envelope";
        case PowerOutcome::VerificationFailed: return "verification failed";
        case PowerOutcome::Cancelled: return "cancelled";
        case PowerOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

PowerResult simplify_powers(Arena &arena, Derivation &derivation, NodeId expression, const Budget &budget) {
    Run run(arena, derivation, expression, budget);
    if (!run.running())
        return run.stopped();
    if (expression >= arena.node_count())
        return run.finish(PowerOutcome::OutsideEnvelope, kNoNode, "enter an expression");
    Shape shape;
    Rational degree;
    if (arena.any_node(expression, [&arena](NodeId n) {
            Rational e;
            return arena.at(n).kind == Kind::Pow && constant_exponent(arena, arena.children(n)[1], &e) &&
                   (e.num > kMaxExponent || e.num < -kMaxExponent);
        }))
        return run.finish(PowerOutcome::OutsideEnvelope, kNoNode, "an exponent whose numerator exceeds 1024 in size is outside the envelope");
    if (!read_shape(arena, expression, &shape, &degree))
        return run.finish(PowerOutcome::OutsideEnvelope, kNoNode,
                          "every part has to be built from numbers and one variable with sums, products, whole or "
                          "rational powers, square roots and absolute values, with every root taken of a single term");
    if (derivation.request.numeric_mode != NumericMode::Exact)
        return run.finish(PowerOutcome::OutsideEnvelope, kNoNode, "power walkthroughs require Exact mode");
    if (has_no_real_value(arena, expression))
        return run.finish(PowerOutcome::NoRealValue, kNoNode, "an even root of a negative number has no real value");

    if (!run.step())
        return run.stopped();
    Step plan;
    plan.phase = "Plan";
    plan.goal = "Simplify the powers and roots in " + print(arena, expression);
    plan.rule_id = "plan.power-laws";
    plan.rule_name = "Apply the power laws one at a time";
    plan.explanation_short = "Write roots as powers, apply one law at a time with its condition, then write square roots back.";
    plan.explanation_detailed = "Each law is checked for the condition it needs over the real numbers before it is used.";
    PlanPayload payload;
    payload.strategy_id = plan.rule_id;
    payload.selected_strategy = plan.rule_name;
    payload.matched_problem_facts.push_back(print(arena, expression));
    payload.selection_rationale = "The expression is built from numbers and one variable with powers and roots of single terms.";
    register_strategy_precondition(payload, plan, "pre.power.one-variable",
        "the expression is built from numbers and one variable with powers and roots of single terms",
        "one-variable shape reading", EvidenceStrength::StructurallyValid, VerificationOutcome::Passed, plan.goal);
    run.plan = derivation.add_plan(kNoStep, std::move(plan), std::move(payload));
    if (!run.running())
        return run.stopped();

    NodeId current = expression;
    bool changed = false;
    const SubtermRule rules[] = {as_power, numeric_root, power_of_power, product_power, same_base, first_power};
    for (int round = 0; round < 64; ++round) {
        NodeId next = kNoNode;
        Pass pass;
        std::string what;
        for (SubtermRule rule : rules) {
            next = rewrite_first_subterm(arena, current, rule, &pass, &what);
            if (next != kNoNode)
                break;
        }
        if (next == kNoNode)
            break;
        if (!run.record(pass, current, next, what))
            return run.stopped();
        current = next;
        changed = true;
    }
    for (int round = 0; round < 16; ++round) {
        Pass pass;
        std::string what;
        const NodeId next = rewrite_first_subterm(arena, current, as_root, &pass, &what);
        if (next == kNoNode)
            break;
        if (!run.record(pass, current, next, what))
            return run.stopped();
        current = next;
    }
    if (!changed || current == expression) {
        derivation.rewind_to(run.mark);
        return run.finish(PowerOutcome::AlreadyInForm, expression, "no power law simplifies the expression further");
    }

    if (!run.step())
        return run.stopped();
    VerificationRecord same = run.exact(expression, current, "obl.power.same-values");
    Step check;
    check.phase = "Check";
    check.goal = "Check the result against the expression as typed";
    check.rule_id = "pow.check-values";
    check.rule_name = "Check the result on the real domain";
    check.claim = ClaimType::EquivalentExpression;
    check.explanation_short = "Compare both at enough points of each sign to fix a difference of this degree.";
    check.explanation_detailed = "Substituting a power of t for the variable turns both sides into sums of powers of t, "
        "which agree everywhere once they agree at more points than such a sum can vanish.";
    check.proof_obligations.push_back({"obl.power.same-values", "the new form has the value of the old one wherever the old one is real"});
    const VerificationOutcome outcome = same.outcome;
    const std::string why = same.detail;
    check.verifications.push_back(std::move(same));
    CheckPayload payload_check;
    payload_check.target_claim = print(arena, current) + " equals " + print(arena, expression) + " wherever it is real";
    payload_check.check_method = "exact evaluation on each branch beyond the degree";
    payload_check.expected_relation = "equal values";
    payload_check.observed_result = why;
    derivation.add_check(run.plan, std::move(check), std::move(payload_check));
    if (!run.running())
        return run.stopped();
    if (outcome != VerificationOutcome::Passed)
        return run.finish(outcome == VerificationOutcome::Failed ? PowerOutcome::VerificationFailed
                                                                 : PowerOutcome::OutsideEnvelope,
                          kNoNode, why);
    return run.finish(PowerOutcome::Rewritten, current, "each power law applied with its real-domain condition");
}

}  // namespace nps
