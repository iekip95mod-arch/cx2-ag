#include "nps/steps/trig.h"

#include <algorithm>
#include <map>
#include <numeric>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/steps/rewrite.h"

namespace nps {
namespace {

constexpr int kMaxFrequency = 64;
constexpr int64_t kMaxPower = 8;

struct Complex {
    Rational re{0, 1};
    Rational im{0, 1};
};

bool complex_add(const Complex &a, const Complex &b, Complex *out) {
    return rational_add(a.re, b.re, &out->re) && rational_add(a.im, b.im, &out->im);
}

bool complex_mul(const Complex &a, const Complex &b, Complex *out) {
    Rational rr, ii, ri, ir;
    if (!rational_mul(a.re, b.re, &rr) || !rational_mul(a.im, b.im, &ii) || !rational_mul(a.re, b.im, &ri) ||
        !rational_mul(a.im, b.re, &ir))
        return false;
    Complex result;
    if (!rational_sub(rr, ii, &result.re) || !rational_add(ri, ir, &result.im))
        return false;
    *out = result;
    return true;
}

bool complex_zero(const Complex &c) {
    return c.re.num == 0 && c.im.num == 0;
}

// A trigonometric polynomial as a Laurent polynomial in z_v = e^(i base_v), one exponent per variable.
using Laurent = std::map<std::vector<int>, Complex>;

bool laurent_add(const Laurent &a, const Laurent &b, Laurent *out) {
    Laurent sum = a;
    for (const auto &[key, value] : b) {
        Complex next;
        if (!complex_add(sum[key], value, &next))
            return false;
        sum[key] = next;
    }
    for (auto it = sum.begin(); it != sum.end();)
        it = complex_zero(it->second) ? sum.erase(it) : std::next(it);
    *out = std::move(sum);
    return true;
}

bool laurent_mul(const Laurent &a, const Laurent &b, Laurent *out) {
    Laurent product;
    for (const auto &[left_key, left] : a) {
        for (const auto &[right_key, right] : b) {
            std::vector<int> key(left_key.size());
            for (size_t i = 0; i < key.size(); ++i) {
                key[i] = left_key[i] + right_key[i];
                if (key[i] > kMaxFrequency || key[i] < -kMaxFrequency)
                    return false;
            }
            Complex term, next;
            if (!complex_mul(left, right, &term) || !complex_add(product[key], term, &next))
                return false;
            product[key] = next;
        }
    }
    for (auto it = product.begin(); it != product.end();)
        it = complex_zero(it->second) ? product.erase(it) : std::next(it);
    *out = std::move(product);
    return true;
}

bool symbol_free(const Arena &arena, NodeId id) {
    return !arena.any_node(id, [&arena](NodeId n) { return arena.at(n).kind == Kind::Symbol; });
}

bool is_trig_call(const Arena &arena, NodeId id) {
    const Node &n = arena.at(id);
    return n.kind == Kind::Call && arena.children(id).size() == 1 &&
           (arena.text(id) == "sin" || arena.text(id) == "cos");
}

// An angle as rational coefficients of its variables, with no constant term.
bool read_angle(const Arena &arena, NodeId id, std::map<std::string, Rational> *out, int depth = 0) {
    if (depth > 32 || arena.is_approximate(id))
        return false;
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(id);
    switch (n.kind) {
        case Kind::Symbol: {
            Rational &slot = (*out)[arena.text(id)];
            return rational_add(slot, Rational{1, 1}, &slot);
        }
        case Kind::Add:
            for (NodeId child : kids) {
                if (!read_angle(arena, child, out, depth + 1))
                    return false;
            }
            return true;
        case Kind::Neg: {
            std::map<std::string, Rational> inner;
            if (!read_angle(arena, kids[0], &inner, depth + 1))
                return false;
            for (const auto &[name, value] : inner) {
                Rational &slot = (*out)[name];
                if (!rational_sub(slot, value, &slot))
                    return false;
            }
            return true;
        }
        case Kind::Mul: {
            Rational scale{1, 1};
            NodeId carrier = kNoNode;
            for (NodeId child : kids) {
                Rational value;
                if (symbol_free(arena, child)) {
                    if (!evaluate_rational(arena, child, {}, &value) || !rational_mul(scale, value, &scale))
                        return false;
                } else if (carrier != kNoNode) {
                    return false;
                } else {
                    carrier = child;
                }
            }
            if (carrier == kNoNode)
                return false;
            std::map<std::string, Rational> inner;
            if (!read_angle(arena, carrier, &inner, depth + 1))
                return false;
            for (const auto &[name, value] : inner) {
                Rational scaled;
                Rational &slot = (*out)[name];
                if (!rational_mul(value, scale, &scaled) || !rational_add(slot, scaled, &slot))
                    return false;
            }
            return true;
        }
        default:
            return false;
    }
}

struct Basis {
    std::vector<std::string> names;
    std::vector<int64_t> denominators;

    size_t index(const std::string &name) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name)
                return i;
        }
        names.push_back(name);
        denominators.push_back(1);
        return names.size() - 1;
    }
};

// Every variable an angle uses, with the least common denominator of its coefficients, so each angle
// is a whole multiple of a base angle per variable.
bool gather_basis(const Arena &arena, NodeId id, Basis *basis) {
    bool ok = true;
    arena.any_node(id, [&](NodeId n) {
        if (!ok || !is_trig_call(arena, n))
            return false;
        std::map<std::string, Rational> angle;
        if (!read_angle(arena, arena.children(n)[0], &angle)) {
            ok = false;
            return true;
        }
        for (const auto &[name, value] : angle) {
            const size_t at = basis->index(name);
            basis->denominators[at] = std::lcm(basis->denominators[at], value.den);
            if (basis->denominators[at] > kMaxFrequency)
                ok = false;
        }
        return false;
    });
    return ok;
}

bool to_laurent(const Arena &arena, NodeId id, const Basis &basis, Laurent *out, int depth = 0) {
    if (depth > 64 || arena.is_approximate(id))
        return false;
    const std::vector<int> zero(basis.names.size(), 0);
    out->clear();
    if (symbol_free(arena, id)) {
        Rational value;
        if (!evaluate_rational(arena, id, {}, &value))
            return false;
        if (value.num != 0)
            (*out)[zero] = Complex{value, {0, 1}};
        return true;
    }
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(id);
    switch (n.kind) {
        case Kind::Add: {
            for (NodeId child : kids) {
                Laurent part;
                if (!to_laurent(arena, child, basis, &part, depth + 1) || !laurent_add(*out, part, out))
                    return false;
            }
            return true;
        }
        case Kind::Neg: {
            Laurent inner;
            if (!to_laurent(arena, kids[0], basis, &inner, depth + 1))
                return false;
            Laurent minus{{zero, Complex{{-1, 1}, {0, 1}}}};
            return laurent_mul(inner, minus, out);
        }
        case Kind::Mul: {
            (*out)[zero] = Complex{{1, 1}, {0, 1}};
            for (NodeId child : kids) {
                Laurent part;
                if (!to_laurent(arena, child, basis, &part, depth + 1) || !laurent_mul(*out, part, out))
                    return false;
            }
            return true;
        }
        case Kind::Pow: {
            int64_t exponent = 0;
            if (!small_integer(arena, kids[1], &exponent) || exponent < 0 || exponent > kMaxPower)
                return false;
            Laurent base;
            if (!to_laurent(arena, kids[0], basis, &base, depth + 1))
                return false;
            (*out)[zero] = Complex{{1, 1}, {0, 1}};
            for (int64_t i = 0; i < exponent; ++i) {
                if (!laurent_mul(*out, base, out))
                    return false;
            }
            return true;
        }
        case Kind::Call: {
            if (!is_trig_call(arena, id))
                return false;
            std::map<std::string, Rational> angle;
            if (!read_angle(arena, kids[0], &angle))
                return false;
            std::vector<int> up = zero, down = zero;
            for (const auto &[name, value] : angle) {
                size_t at = 0;
                while (at < basis.names.size() && basis.names[at] != name)
                    ++at;
                if (at == basis.names.size())
                    return false;
                Rational scaled;
                if (!rational_mul(value, Rational{basis.denominators[at], 1}, &scaled) || scaled.den != 1 ||
                    scaled.num > kMaxFrequency || scaled.num < -kMaxFrequency)
                    return false;
                up[at] = static_cast<int>(scaled.num);
                down[at] = -static_cast<int>(scaled.num);
            }
            if (arena.text(id) == "cos") {
                (*out)[up] = Complex{{1, 2}, {0, 1}};
                Complex next;
                if (!complex_add((*out)[down], Complex{{1, 2}, {0, 1}}, &next))
                    return false;
                (*out)[down] = next;
            } else {
                (*out)[up] = Complex{{0, 1}, {-1, 2}};
                Complex next;
                if (!complex_add((*out)[down], Complex{{0, 1}, {1, 2}}, &next))
                    return false;
                (*out)[down] = next;
            }
            for (auto it = out->begin(); it != out->end();)
                it = complex_zero(it->second) ? out->erase(it) : std::next(it);
            return true;
        }
        default:
            return false;
    }
}

bool readable(const Arena &arena, NodeId id, Basis *basis, Laurent *out) {
    return gather_basis(arena, id, basis) && to_laurent(arena, id, *basis, out);
}

bool mentions_trig(const Arena &arena, NodeId id) {
    return arena.any_node(id, [&arena](NodeId n) { return is_trig_call(arena, n); });
}

}  // namespace

TrigReading trig_equivalent(const Arena &arena, NodeId left, NodeId right, std::string *why) {
    Basis basis;
    Laurent a, b;
    if (!gather_basis(arena, left, &basis) || !gather_basis(arena, right, &basis) ||
        !to_laurent(arena, left, basis, &a) || !to_laurent(arena, right, basis, &b)) {
        *why = "a form is outside sums and products of sines and cosines of rational multiples of the variables";
        return TrigReading::Unreadable;
    }
    if (a.size() != b.size()) {
        *why = "the two sides have different exponential forms";
        return TrigReading::Different;
    }
    for (auto it = a.begin(), jt = b.begin(); it != a.end(); ++it, ++jt) {
        if (it->first != jt->first || !rational_equal(it->second.re, jt->second.re) ||
            !rational_equal(it->second.im, jt->second.im)) {
            *why = "the two sides have different exponential forms";
            return TrigReading::Different;
        }
    }
    *why = "both sides have the same exponential form, coefficient for coefficient";
    return TrigReading::Equal;
}

namespace {

// Which identity a rule applied, for the step that records it.
struct Applied {
    const char *rule_id = nullptr;
    const char *rule_name = nullptr;
    const char *identity = nullptr;
};

NodeId call1(Arena &arena, const char *name, NodeId argument) {
    return arena.call(name, {argument});
}

NodeId square(Arena &arena, NodeId id) {
    return arena.binary(Kind::Pow, id, arena.integer("2"));
}

NodeId half(Arena &arena) {
    return arena.binary(Kind::Pow, arena.integer("2"), arena.integer("-1"));
}

bool integer_multiple(const Arena &arena, NodeId argument, int64_t *k, NodeId *rest) {
    if (arena.at(argument).kind != Kind::Mul || arena.children(argument).size() != 2)
        return false;
    for (size_t i = 0; i < 2; ++i) {
        const NodeId factor = arena.children(argument)[i];
        if (symbol_free(arena, factor) && small_integer(arena, factor, k)) {
            *rest = arena.children(argument)[1 - i];
            return true;
        }
    }
    return false;
}

NodeId expand_here(Arena &arena, NodeId id, void *state, std::string *what) {
    Applied *applied = static_cast<Applied *>(state);
    if (!is_trig_call(arena, id))
        return kNoNode;
    const bool sine = arena.text(id) == "sin";
    const NodeId arg = arena.children(id)[0];
    const Node &a = arena.at(arg);
    if (a.kind == Kind::Neg) {
        const NodeId inner = arena.children(arg)[0];
        *applied = sine ? Applied{"trig.odd", "Sine is odd", "sin(-a) = -sin(a)"}
                        : Applied{"trig.even", "Cosine is even", "cos(-a) = cos(a)"};
        *what = std::string("Use ") + applied->identity;
        return sine ? arena.unary(Kind::Neg, call1(arena, "sin", inner)) : call1(arena, "cos", inner);
    }
    int64_t k = 0;
    NodeId rest = kNoNode;
    if (integer_multiple(arena, arg, &k, &rest)) {
        if (k < 0) {
            const NodeId positive = k == -1 ? rest : arena.binary(Kind::Mul, arena.integer(std::to_string(-k)), rest);
            *applied = sine ? Applied{"trig.odd", "Sine is odd", "sin(-a) = -sin(a)"}
                            : Applied{"trig.even", "Cosine is even", "cos(-a) = cos(a)"};
            *what = std::string("Use ") + applied->identity;
            return sine ? arena.unary(Kind::Neg, call1(arena, "sin", positive)) : call1(arena, "cos", positive);
        }
        if (k == 2) {
            *applied = sine ? Applied{"trig.double-angle", "Double-angle identity", "sin(2a) = 2 sin(a) cos(a)"}
                            : Applied{"trig.double-angle", "Double-angle identity", "cos(2a) = cos(a)^2 - sin(a)^2"};
            *what = std::string("Use ") + applied->identity;
            if (sine)
                return arena.nary(Kind::Mul, {arena.integer("2"), call1(arena, "sin", rest), call1(arena, "cos", rest)});
            return arena.binary(Kind::Add, square(arena, call1(arena, "cos", rest)),
                                arena.unary(Kind::Neg, square(arena, call1(arena, "sin", rest))));
        }
        if (k > 2 && k <= 6) {
            const NodeId most = k == 2 ? rest : arena.binary(Kind::Mul, arena.integer(std::to_string(k - 1)), rest);
            *applied = Applied{"trig.split-multiple", "Split a multiple angle", "k a = (k - 1) a + a"};
            *what = "Write " + print(arena, arg) + " as " + print(arena, most) + " + " + print(arena, rest);
            return call1(arena, sine ? "sin" : "cos", arena.binary(Kind::Add, most, rest));
        }
        return kNoNode;
    }
    if (a.kind == Kind::Add && arena.children(arg).size() >= 2) {
        const ChildView terms = arena.children(arg);
        const NodeId first = terms[0];
        std::vector<NodeId> others;
        for (size_t i = 1; i < terms.size(); ++i)
            others.push_back(terms[i]);
        NodeId second = others.size() == 1 ? others[0] : arena.nary(Kind::Add, others);
        bool difference = false;
        if (others.size() == 1 && arena.at(second).kind == Kind::Neg) {
            second = arena.children(second)[0];
            difference = true;
        }
        const NodeId sa = call1(arena, "sin", first), ca = call1(arena, "cos", first);
        const NodeId sb = call1(arena, "sin", second), cb = call1(arena, "cos", second);
        if (sine) {
            *applied = difference ? Applied{"trig.angle-sum", "Angle-difference identity", "sin(a - b) = sin(a) cos(b) - cos(a) sin(b)"}
                                  : Applied{"trig.angle-sum", "Angle-sum identity", "sin(a + b) = sin(a) cos(b) + cos(a) sin(b)"};
            *what = std::string("Use ") + applied->identity;
            const NodeId right = arena.binary(Kind::Mul, ca, sb);
            return arena.binary(Kind::Add, arena.binary(Kind::Mul, sa, cb),
                                difference ? arena.unary(Kind::Neg, right) : right);
        }
        *applied = difference ? Applied{"trig.angle-sum", "Angle-difference identity", "cos(a - b) = cos(a) cos(b) + sin(a) sin(b)"}
                              : Applied{"trig.angle-sum", "Angle-sum identity", "cos(a + b) = cos(a) cos(b) - sin(a) sin(b)"};
        *what = std::string("Use ") + applied->identity;
        const NodeId right = arena.binary(Kind::Mul, sa, sb);
        return arena.binary(Kind::Add, arena.binary(Kind::Mul, ca, cb),
                            difference ? right : arena.unary(Kind::Neg, right));
    }
    return kNoNode;
}

// A term c sin(a)^2 or c cos(a)^2, read as its coefficient node, its function and its angle.
bool squared_trig(const Arena &arena, NodeId term, NodeId *coefficient, std::string *name, NodeId *angle) {
    NodeId power = term;
    *coefficient = kNoNode;
    if (arena.at(term).kind == Kind::Mul && arena.children(term).size() == 2) {
        const NodeId a = arena.children(term)[0], b = arena.children(term)[1];
        if (symbol_free(arena, a)) {
            *coefficient = a;
            power = b;
        } else if (symbol_free(arena, b)) {
            *coefficient = b;
            power = a;
        } else {
            return false;
        }
    }
    int64_t exponent = 0;
    if (arena.at(power).kind != Kind::Pow || !small_integer(arena, arena.children(power)[1], &exponent) ||
        exponent != 2 || !is_trig_call(arena, arena.children(power)[0]))
        return false;
    *name = arena.text(arena.children(power)[0]);
    *angle = arena.children(arena.children(power)[0])[0];
    return true;
}

NodeId pythagorean_here(Arena &arena, NodeId id, void *state, std::string *what) {
    Applied *applied = static_cast<Applied *>(state);
    if (arena.at(id).kind != Kind::Add)
        return kNoNode;
    const ChildView terms = arena.children(id);
    for (size_t i = 0; i < terms.size(); ++i) {
        NodeId ci, ai;
        std::string ni;
        if (!squared_trig(arena, terms[i], &ci, &ni, &ai) || ni != "sin")
            continue;
        for (size_t j = 0; j < terms.size(); ++j) {
            NodeId cj, aj;
            std::string nj;
            if (j == i || !squared_trig(arena, terms[j], &cj, &nj, &aj) || nj != "cos" || aj != ai || cj != ci)
                continue;
            std::vector<NodeId> rest;
            for (size_t k = 0; k < terms.size(); ++k) {
                if (k != i && k != j)
                    rest.push_back(terms[k]);
            }
            rest.push_back(ci == kNoNode ? arena.integer("1") : ci);
            *applied = Applied{"trig.pythagorean", "Pythagorean identity", "sin(a)^2 + cos(a)^2 = 1"};
            *what = "Use sin(a)^2 + cos(a)^2 = 1 with a = " + print(arena, ai);
            return rest.size() == 1 ? rest[0] : arena.nary(Kind::Add, rest);
        }
    }
    return kNoNode;
}

NodeId product_here(Arena &arena, NodeId id, void *state, std::string *what) {
    Applied *applied = static_cast<Applied *>(state);
    if (arena.at(id).kind != Kind::Mul)
        return kNoNode;
    const ChildView factors = arena.children(id);
    for (size_t i = 0; i < factors.size(); ++i) {
        if (!is_trig_call(arena, factors[i]) || arena.text(factors[i]) != "sin")
            continue;
        for (size_t j = 0; j < factors.size(); ++j) {
            if (!is_trig_call(arena, factors[j]) || arena.text(factors[j]) != "cos" ||
                arena.children(factors[j])[0] != arena.children(factors[i])[0])
                continue;
            const NodeId angle = arena.children(factors[i])[0];
            std::vector<NodeId> rest;
            for (size_t k = 0; k < factors.size(); ++k) {
                if (k != i && k != j)
                    rest.push_back(factors[k]);
            }
            rest.push_back(half(arena));
            rest.push_back(call1(arena, "sin", arena.binary(Kind::Mul, arena.integer("2"), angle)));
            *applied = Applied{"trig.double-angle-product", "Double-angle identity read backwards",
                               "sin(a) cos(a) = sin(2a)/2"};
            *what = std::string("Use ") + applied->identity;
            return arena.nary(Kind::Mul, rest);
        }
    }
    return kNoNode;
}

NodeId half_angle_here(Arena &arena, NodeId id, void *state, std::string *what) {
    Applied *applied = static_cast<Applied *>(state);
    int64_t exponent = 0;
    if (arena.at(id).kind != Kind::Pow || !small_integer(arena, arena.children(id)[1], &exponent) || exponent != 2 ||
        !is_trig_call(arena, arena.children(id)[0]))
        return kNoNode;
    const NodeId call = arena.children(id)[0];
    const bool sine = arena.text(call) == "sin";
    const NodeId doubled = call1(arena, "cos", arena.binary(Kind::Mul, arena.integer("2"), arena.children(call)[0]));
    *applied = sine ? Applied{"trig.half-angle", "Half-angle identity", "sin(a)^2 = (1 - cos(2a))/2"}
                    : Applied{"trig.half-angle", "Half-angle identity", "cos(a)^2 = (1 + cos(2a))/2"};
    *what = std::string("Use ") + applied->identity;
    return arena.binary(Kind::Mul, half(arena),
                        arena.binary(Kind::Add, arena.integer("1"), sine ? arena.unary(Kind::Neg, doubled) : doubled));
}

// What collecting refuses before anything is recorded: a power above two, and a product of trig
// factors that are not a sine and cosine of one angle, which would need product-to-sum.
const char *collect_refusal(const Arena &arena, NodeId id) {
    const char *why = nullptr;
    arena.any_node(id, [&](NodeId n) {
        const Node &node = arena.at(n);
        if (node.kind == Kind::Pow) {
            int64_t exponent = 0;
            if (mentions_trig(arena, arena.children(n)[0]) &&
                (!small_integer(arena, arena.children(n)[1], &exponent) || exponent > 2 || exponent < 0)) {
                why = "a power of a sine or cosine above two needs the identities applied more than once, which is outside the envelope";
                return true;
            }
        }
        if (node.kind == Kind::Mul) {
            std::vector<NodeId> trig;
            int degree = 0;
            for (NodeId f : arena.children(n)) {
                if (is_trig_call(arena, f)) {
                    trig.push_back(arena.children(f)[0]);
                    ++degree;
                } else if (arena.at(f).kind == Kind::Pow && is_trig_call(arena, arena.children(f)[0])) {
                    trig.push_back(arena.children(arena.children(f)[0])[0]);
                    degree += 2;
                } else if (mentions_trig(arena, f)) {
                    degree += 2;
                }
            }
            bool same = true;
            for (NodeId angle : trig)
                same = same && angle == trig.front();
            if (degree > 2 || !same) {
                why = "a product of sines and cosines of different angles, or of more than two of them, needs the product-to-sum identity, which is outside the envelope";
                return true;
            }
        }
        return false;
    });
    return why;
}

struct Run {
    Arena &arena;
    Derivation &derivation;
    NodeId input;
    TrigGoal goal;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    bool failed = false;
    TrigOutcome failure = TrigOutcome::OutsideEnvelope;
    std::string detail;

    Run(Arena &a, Derivation &d, NodeId e, TrigGoal g, const Budget &budget)
        : arena(a), derivation(d), input(e), goal(g), meter(budget), mark(d.mark()) {}

    bool refuse(TrigOutcome outcome, std::string why) {
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
            return refuse(TrigOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped() || !meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? TrigOutcome::Cancelled : TrigOutcome::ResourceExceeded,
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
        const TrigReading reading = trig_equivalent(arena, before, after, &why);
        const VerificationOutcome outcome = reading == TrigReading::Equal ? VerificationOutcome::Passed
                                            : reading == TrigReading::Different ? VerificationOutcome::Failed
                                                                                : VerificationOutcome::Inconclusive;
        return {"exact exponential normal form", outcome, strength_for(outcome, EvidenceStrength::StructurallyValid),
                why, obligation};
    }

    bool record(const Applied &applied, NodeId before, NodeId after, const std::string &action) {
        if (!step())
            return false;
        Step s;
        s.phase = goal == TrigGoal::Expand ? "Expand" : "Collect";
        s.goal = goal == TrigGoal::Expand ? "Expand one sine or cosine" : "Collect one product or square";
        s.rule_id = applied.rule_id;
        s.rule_name = applied.rule_name;
        s.claim = ClaimType::EquivalentExpression;
        s.explanation_short = std::string("The identity used is ") + applied.identity + ".";
        s.explanation_detailed = "It holds for every angle, so the expression keeps its value.";
        s.proof_obligations.push_back({"obl.trig.identity-holds", "the rewritten expression equals the one before it"});
        s.verifications.push_back(exact(before, after, "obl.trig.identity-holds"));
        const bool passed = s.verifications.back().outcome == VerificationOutcome::Passed;
        const std::string why = s.verifications.back().detail;
        TransformationPayload change;
        change.before = before;
        change.after = after;
        change.reversible = true;
        change.concrete_action = action + ".";
        derivation.add_transformation(plan, std::move(s), std::move(change));
        if (!running())
            return false;
        if (!passed)
            return refuse(TrigOutcome::VerificationFailed, why);
        return true;
    }

    TrigResult finish(TrigOutcome outcome, NodeId expression, std::string why) {
        TrigResult result;
        result.outcome = outcome;
        result.detail = std::move(why);
        if (arena.failed()) {
            result.outcome = TrigOutcome::ResourceExceeded;
            result.detail = status_name(arena.status());
        }
        switch (result.outcome) {
            case TrigOutcome::Rewritten:
                result.expression = expression;
                result.status = derivation.outcome_from(mark);
                break;
            case TrigOutcome::AlreadyInForm:
                result.expression = expression;
                result.status = DerivationStatus::SolvedAndVerified;
                break;
            case TrigOutcome::NotTrigonometric:
            case TrigOutcome::OutsideEnvelope: result.status = DerivationStatus::Unsupported; break;
            case TrigOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case TrigOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case TrigOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        if (result.outcome != TrigOutcome::Rewritten && result.outcome != TrigOutcome::AlreadyInForm)
            keep_verified_prefix(derivation, mark, arena);
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = "algebra.trigonometric-identities";
        context.requested_method = goal == TrigGoal::Expand ? "texpand" : "tcollect";
        context.normalized_problem_model = input;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = input < arena.node_count() ? print(arena, input) : std::string();
        context.numeric_mode = derivation.request.numeric_mode;
        context.angle_convention = "either, the identities hold in radians and in degrees";
        context.branch_convention = "real domain";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    TrigResult stopped() { return finish(failure, kNoNode, detail); }

    // The collected form read off the exponential form: a constant, then a cosine and a sine for each
    // frequency, with the first nonzero multiple of each angle made positive.
    NodeId rendered(const Laurent &form, const Basis &basis) {
        std::vector<NodeId> terms;
        const std::vector<int> zero(basis.names.size(), 0);
        std::vector<std::vector<int>> done;
        for (const auto &[key, value] : form) {
            if (key == zero) {
                if (value.im.num != 0)
                    return kNoNode;
                terms.push_back(canonical_rational(arena, value.re));
                continue;
            }
            size_t lead = 0;
            while (key[lead] == 0)
                ++lead;
            std::vector<int> positive = key, negative = key;
            for (int &f : negative)
                f = -f;
            if (key[lead] < 0)
                std::swap(positive, negative);
            if (std::find(done.begin(), done.end(), positive) != done.end())
                continue;
            done.push_back(positive);
            const auto p = form.find(positive), n = form.find(negative);
            const Complex cp = p == form.end() ? Complex{} : p->second;
            const Complex cn = n == form.end() ? Complex{} : n->second;
            Rational a_re, a_im, b_re, b_im;
            if (!rational_add(cp.re, cn.re, &a_re) || !rational_add(cp.im, cn.im, &a_im) ||
                !rational_sub(cn.im, cp.im, &b_re) || !rational_sub(cp.re, cn.re, &b_im) || a_im.num != 0 || b_im.num != 0)
                return kNoNode;
            std::vector<NodeId> parts;
            for (size_t v = 0; v < positive.size(); ++v) {
                if (positive[v] == 0)
                    continue;
                const Rational coefficient{positive[v], basis.denominators[v]};
                Rational reduced;
                if (!rational_mul(coefficient, Rational{1, 1}, &reduced))
                    return kNoNode;
                const NodeId symbol = arena.symbol(basis.names[v]);
                parts.push_back(reduced.num == 1 && reduced.den == 1 ? symbol
                                : arena.binary(Kind::Mul, canonical_rational(arena, reduced), symbol));
            }
            const NodeId angle = parts.size() == 1 ? parts[0] : arena.nary(Kind::Add, parts);
            if (a_re.num != 0) {
                const NodeId c = call1(arena, "cos", angle);
                terms.push_back(a_re.num == 1 && a_re.den == 1 ? c : arena.binary(Kind::Mul, canonical_rational(arena, a_re), c));
            }
            if (b_re.num != 0) {
                const NodeId s = call1(arena, "sin", angle);
                terms.push_back(b_re.num == 1 && b_re.den == 1 ? s : arena.binary(Kind::Mul, canonical_rational(arena, b_re), s));
            }
        }
        if (terms.empty())
            return arena.integer("0");
        return terms.size() == 1 ? terms[0] : arena.nary(Kind::Add, terms);
    }
};

}  // namespace

const char *trig_outcome_name(TrigOutcome outcome) {
    switch (outcome) {
        case TrigOutcome::Rewritten: return "rewritten";
        case TrigOutcome::AlreadyInForm: return "already in form";
        case TrigOutcome::NotTrigonometric: return "not trigonometric";
        case TrigOutcome::OutsideEnvelope: return "outside envelope";
        case TrigOutcome::VerificationFailed: return "verification failed";
        case TrigOutcome::Cancelled: return "cancelled";
        case TrigOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

TrigResult trig_rewrite(Arena &arena, Derivation &derivation, NodeId expression, TrigGoal goal,
                        const Budget &budget) {
    Run run(arena, derivation, expression, goal, budget);
    if (!run.running())
        return run.stopped();
    if (expression >= arena.node_count())
        return run.finish(TrigOutcome::OutsideEnvelope, kNoNode, "enter an expression");
    const bool any_trig = arena.any_node(expression, [&arena](NodeId n) {
        if (arena.at(n).kind != Kind::Call)
            return false;
        const std::string &name = arena.text(n);
        return name == "sin" || name == "cos" || name == "tan" || name == "sec" || name == "csc" || name == "cot";
    });
    if (!any_trig)
        return run.finish(TrigOutcome::NotTrigonometric, kNoNode, "the expression has no sine or cosine to rewrite");
    Basis basis;
    Laurent form;
    if (!readable(arena, expression, &basis, &form))
        return run.finish(TrigOutcome::OutsideEnvelope, kNoNode,
                          "every part has to be a sum or product of sines and cosines of rational multiples of the "
                          "variables, with no constant inside an angle, no variable outside sin or cos and no tan");
    if (goal == TrigGoal::Collect) {
        if (const char *why = collect_refusal(arena, expression))
            return run.finish(TrigOutcome::OutsideEnvelope, kNoNode, why);
    }

    const bool expand = goal == TrigGoal::Expand;
    if (!run.step())
        return run.stopped();
    Step plan;
    plan.phase = "Plan";
    plan.goal = (expand ? "Expand " : "Collect ") + print(arena, expression);
    plan.rule_id = expand ? "plan.trig-expand" : "plan.trig-collect";
    plan.rule_name = expand ? "Expand sums and multiples of angles" : "Reduce squares and products to single angles";
    plan.explanation_short = expand ? "Use the angle-sum and double-angle identities until every angle is a single variable."
                                    : "Use the Pythagorean, half-angle and double-angle identities, then collect like terms.";
    plan.explanation_detailed = "Each step names the identity it uses. Every step and the result are checked exactly.";
    PlanPayload payload;
    payload.strategy_id = plan.rule_id;
    payload.selected_strategy = plan.rule_name;
    payload.matched_problem_facts.push_back(print(arena, expression));
    payload.selection_rationale = "The expression is a polynomial in sines and cosines of rational multiples of its variables.";
    register_strategy_precondition(payload, plan, "pre.trig.polynomial-in-sin-cos",
        "the expression is a polynomial in sines and cosines of rational multiples of its variables with rational coefficients",
        "exact exponential normal form", EvidenceStrength::StructurallyValid, VerificationOutcome::Passed, plan.goal);
    run.plan = derivation.add_plan(kNoStep, std::move(plan), std::move(payload));
    if (!run.running())
        return run.stopped();

    NodeId current = expression;
    bool changed = false;
    for (int round = 0; round < 64; ++round) {
        Applied applied;
        std::string what;
        NodeId next = kNoNode;
        if (expand) {
            next = rewrite_first_subterm(arena, current, expand_here, &applied, &what);
        } else {
            next = rewrite_first_subterm(arena, current, pythagorean_here, &applied, &what);
            if (next == kNoNode)
                next = rewrite_first_subterm(arena, current, product_here, &applied, &what);
            if (next == kNoNode)
                next = rewrite_first_subterm(arena, current, half_angle_here, &applied, &what);
        }
        if (next == kNoNode)
            break;
        if (!run.record(applied, current, next, what))
            return run.stopped();
        current = next;
        changed = true;
    }

    if (!expand) {
        Basis final_basis;
        Laurent final_form;
        if (!readable(arena, current, &final_basis, &final_form))
            return run.finish(TrigOutcome::VerificationFailed, kNoNode, "the collected expression left the envelope");
        const NodeId collected = run.rendered(final_form, final_basis);
        if (collected == kNoNode)
            return run.finish(TrigOutcome::VerificationFailed, kNoNode, "the collected form has a nonreal coefficient");
        if (collected != current) {
            const Applied collect{"trig.collect", "Collect like terms", "like terms combine"};
            if (!run.record(collect, current, collected, "Collect like terms to get " + print(arena, collected)))
                return run.stopped();
            current = collected;
            changed = true;
        }
    }

    if (!changed) {
        derivation.rewind_to(run.mark);
        return run.finish(TrigOutcome::AlreadyInForm, expression,
                          expand ? "no sine or cosine of a sum or a whole multiple is left to expand"
                                 : "no square or product of the same angle is left to collect");
    }

    if (!run.step())
        return run.stopped();
    VerificationRecord same = run.exact(expression, current, "obl.trig.identity-holds");
    Step check;
    check.phase = "Check";
    check.goal = "Check the result against the expression as typed";
    check.rule_id = "trig.check-identity";
    check.rule_name = "Check the result is the same function";
    check.claim = ClaimType::EquivalentExpression;
    check.explanation_short = "Write both as sums of powers of e to the i times the angle and compare every coefficient.";
    check.explanation_detailed = "Two trigonometric polynomials are the same function exactly when their exponential forms match.";
    check.proof_obligations.push_back({"obl.trig.identity-holds", "the rewritten expression equals the one before it"});
    const VerificationOutcome outcome = same.outcome;
    const std::string why = same.detail;
    check.verifications.push_back(std::move(same));
    CheckPayload payload_check;
    payload_check.target_claim = print(arena, current) + " equals " + print(arena, expression);
    payload_check.check_method = "exact exponential normal form";
    payload_check.expected_relation = "identical exponential forms";
    payload_check.observed_result = why;
    derivation.add_check(run.plan, std::move(check), std::move(payload_check));
    if (!run.running())
        return run.stopped();
    if (outcome != VerificationOutcome::Passed)
        return run.finish(outcome == VerificationOutcome::Failed ? TrigOutcome::VerificationFailed
                                                                 : TrigOutcome::ResourceExceeded,
                          kNoNode, why);
    return run.finish(TrigOutcome::Rewritten, current,
                      expand ? "every angle is a single variable" : "squares and products reduced and like terms collected");
}

}  // namespace nps
