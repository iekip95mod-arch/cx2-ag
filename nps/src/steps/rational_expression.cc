#include "nps/steps/rational_expression.h"

#include <algorithm>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"

namespace nps {

Poly::Poly(size_t count) : c_(count) {
    for (__mpq_struct &q : c_)
        mpq_init(&q);
}

Poly::Poly(const Poly &other) : c_(other.c_.size()) {
    for (size_t i = 0; i < c_.size(); ++i) {
        mpq_init(&c_[i]);
        mpq_set(&c_[i], &other.c_[i]);
    }
}

Poly::Poly(Poly &&other) noexcept : c_(std::move(other.c_)) {
    other.c_.clear();
}

Poly &Poly::operator=(Poly other) noexcept {
    swap(other);
    return *this;
}

Poly::~Poly() {
    for (__mpq_struct &q : c_)
        mpq_clear(&q);
}

Poly Poly::constant(int64_t value) {
    Poly out(1);
    mpq_set_si(out.at(0), value, 1);
    out.trim();
    return out;
}

Poly Poly::variable() {
    Poly out(2);
    mpq_set_ui(out.at(1), 1, 1);
    return out;
}

void Poly::trim() {
    while (!c_.empty() && mpq_sgn(&c_.back()) == 0) {
        mpq_clear(&c_.back());
        c_.pop_back();
    }
}

bool Poly::bounded() const {
    for (const __mpq_struct &q : c_) {
        if (mpz_sizeinbase(mpq_numref(&q), 2) > kPolyCoefficientBits ||
            mpz_sizeinbase(mpq_denref(&q), 2) > kPolyCoefficientBits)
            return false;
    }
    return static_cast<int>(c_.size()) - 1 <= 2 * kPolyMaxDegree;
}

bool poly_add(const Poly &a, const Poly &b, Poly *out) {
    Poly sum(std::max(a.size(), b.size()));
    for (size_t i = 0; i < sum.size(); ++i) {
        if (i < a.size())
            mpq_add(sum.at(i), sum.at(i), a.at(i));
        if (i < b.size())
            mpq_add(sum.at(i), sum.at(i), b.at(i));
    }
    sum.trim();
    if (!sum.bounded())
        return false;
    *out = std::move(sum);
    return true;
}

bool poly_sub(const Poly &a, const Poly &b, Poly *out) {
    Poly difference(std::max(a.size(), b.size()));
    for (size_t i = 0; i < difference.size(); ++i) {
        if (i < a.size())
            mpq_add(difference.at(i), difference.at(i), a.at(i));
        if (i < b.size())
            mpq_sub(difference.at(i), difference.at(i), b.at(i));
    }
    difference.trim();
    if (!difference.bounded())
        return false;
    *out = std::move(difference);
    return true;
}

bool poly_mul(const Poly &a, const Poly &b, Poly *out) {
    if (a.zero() || b.zero()) {
        *out = Poly();
        return true;
    }
    Poly product(a.size() + b.size() - 1);
    detail::Mpq term;
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) {
            mpq_mul(term.get(), a.at(i), b.at(j));
            mpq_add(product.at(i + j), product.at(i + j), term.get());
        }
    }
    product.trim();
    if (!product.bounded())
        return false;
    *out = std::move(product);
    return true;
}

bool poly_divide(const Poly &a, const Poly &b, Poly *quotient, Poly *remainder) {
    if (b.zero())
        return false;
    Poly rest = a;
    Poly q(a.degree() >= b.degree() ? static_cast<size_t>(a.degree() - b.degree() + 1) : 0);
    detail::Mpq factor, term;
    const mpq_srcptr lead = b.at(static_cast<size_t>(b.degree()));
    while (!rest.zero() && rest.degree() >= b.degree()) {
        const size_t shift = static_cast<size_t>(rest.degree() - b.degree());
        mpq_div(factor.get(), rest.at(static_cast<size_t>(rest.degree())), lead);
        mpq_set(q.at(shift), factor.get());
        for (size_t i = 0; i < b.size(); ++i) {
            mpq_mul(term.get(), factor.get(), b.at(i));
            mpq_sub(rest.at(i + shift), rest.at(i + shift), term.get());
        }
        rest.trim();
    }
    q.trim();
    if (!q.bounded() || !rest.bounded())
        return false;
    *quotient = std::move(q);
    *remainder = std::move(rest);
    return true;
}

bool poly_gcd(const Poly &a, const Poly &b, Poly *out) {
    if (a.zero() && b.zero())
        return false;
    Poly x = a, y = b;
    while (!y.zero()) {
        Poly q, r;
        if (!poly_divide(x, y, &q, &r))
            return false;
        x = std::move(y);
        y = std::move(r);
    }
    detail::Mpq lead;
    mpq_set(lead.get(), x.at(static_cast<size_t>(x.degree())));
    for (size_t i = 0; i < x.size(); ++i)
        mpq_div(x.at(i), x.at(i), lead.get());
    *out = std::move(x);
    return true;
}

void poly_evaluate(const Poly &p, mpq_srcptr at, mpq_ptr out) {
    mpq_set_ui(out, 0, 1);
    for (size_t i = p.size(); i-- > 0;) {
        mpq_mul(out, out, at);
        mpq_add(out, out, p.at(i));
    }
}

bool poly_derivative(const Poly &p, Poly *out) {
    if (p.degree() < 1) {
        *out = Poly();
        return true;
    }
    Poly d(p.size() - 1);
    detail::Mpq power;
    for (size_t i = 1; i < p.size(); ++i) {
        mpq_set_ui(power.get(), static_cast<unsigned long>(i), 1);
        mpq_mul(d.at(i - 1), p.at(i), power.get());
    }
    d.trim();
    *out = std::move(d);
    return true;
}

bool poly_equal(const Poly &a, const Poly &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!mpq_equal(a.at(i), b.at(i)))
            return false;
    }
    return true;
}

namespace {

constexpr unsigned long kRootCoefficientLimit = 1000000;

// Every positive divisor of value, which the caller has already bounded.
std::vector<unsigned long> divisors(unsigned long value) {
    std::vector<unsigned long> out;
    for (unsigned long d = 1; d * d <= value; ++d) {
        if (value % d != 0)
            continue;
        out.push_back(d);
        if (d != value / d)
            out.push_back(value / d);
    }
    return out;
}

}  // namespace

RootSearch poly_rational_roots(const Poly &p, Meter &meter, std::vector<Rational> *roots,
                               std::vector<int> *multiplicities, Poly *remainder) {
    roots->clear();
    multiplicities->clear();
    Poly rest = p;
    rest.trim();
    // Zero is found by its own factor of x, so the constant term the search divides is nonzero.
    int zero_count = 0;
    while (rest.degree() >= 1 && mpq_sgn(rest.at(0)) == 0) {
        Poly shifted(rest.size() - 1);
        for (size_t i = 1; i < rest.size(); ++i)
            mpq_set(shifted.at(i - 1), rest.at(i));
        rest = std::move(shifted);
        ++zero_count;
    }
    if (zero_count > 0) {
        roots->push_back(Rational{0, 1});
        multiplicities->push_back(zero_count);
    }
    while (rest.degree() >= 1) {
        detail::Mpz scale, content;
        mpz_set_ui(scale.get(), 1);
        for (size_t i = 0; i < rest.size(); ++i)
            mpz_lcm(scale.get(), scale.get(), mpq_denref(rest.at(i)));
        detail::Mpz low, high, value;
        mpz_mul(low.get(), mpq_numref(rest.at(0)), scale.get());
        mpz_divexact(low.get(), low.get(), mpq_denref(rest.at(0)));
        const size_t top = static_cast<size_t>(rest.degree());
        mpz_mul(high.get(), mpq_numref(rest.at(top)), scale.get());
        mpz_divexact(high.get(), high.get(), mpq_denref(rest.at(top)));
        mpz_abs(low.get(), low.get());
        mpz_abs(high.get(), high.get());
        if (mpz_cmp_ui(low.get(), kRootCoefficientLimit) > 0 || mpz_cmp_ui(high.get(), kRootCoefficientLimit) > 0)
            return RootSearch::OutOfRange;
        bool found = false;
        detail::Mpq candidate, at;
        for (unsigned long num : divisors(mpz_get_ui(low.get()))) {
            for (unsigned long den : divisors(mpz_get_ui(high.get()))) {
                for (int sign = 1; sign >= -1 && !found; sign -= 2) {
                    if (!meter.rewrite())
                        return RootSearch::Halted;
                    mpq_set_si(candidate.get(), sign * static_cast<long>(num), den);
                    mpq_canonicalize(candidate.get());
                    poly_evaluate(rest, candidate.get(), at.get());
                    if (mpq_sgn(at.get()) != 0)
                        continue;
                    Rational root;
                    if (!detail::mpq_get_rational(candidate.get(), &root))
                        return RootSearch::OutOfRange;
                    Poly factor(2);
                    mpq_neg(factor.at(0), candidate.get());
                    mpq_set_ui(factor.at(1), 1, 1);
                    int count = 0;
                    while (rest.degree() >= 1) {
                        Poly q, r;
                        if (!poly_divide(rest, factor, &q, &r) || !r.zero())
                            break;
                        rest = std::move(q);
                        ++count;
                    }
                    roots->push_back(root);
                    multiplicities->push_back(count);
                    found = true;
                }
                if (found)
                    break;
            }
            if (found)
                break;
        }
        if (!found)
            break;
    }
    *remainder = std::move(rest);
    return RootSearch::Found;
}

namespace {

enum class Read : uint8_t { Ok, NotRational, OtherSymbol, Inexact, TooHigh, DivideByZero, TooLarge, Halted };

struct Fraction {
    Poly num;
    Poly den;
};

struct Reader {
    const Arena &arena;
    NodeId variable;
    Meter &meter;
    // Every polynomial that appears as a denominator, which is where excluded values come from.
    std::vector<Poly> denominators;

    Read constant_of(NodeId id, mpq_ptr out, size_t depth) {
        Fraction f;
        const Read read = fraction(id, &f, depth);
        if (read != Read::Ok)
            return read;
        if (f.num.degree() > 0 || f.den.degree() > 0)
            return Read::TooHigh;
        mpq_set_ui(out, 0, 1);
        if (!f.num.zero())
            mpq_div(out, f.num.at(0), f.den.at(0));
        return Read::Ok;
    }

    Read fraction(NodeId id, Fraction *out, size_t depth) {
        if (!meter.rewrite())
            return Read::Halted;
        if (depth > Limits().max_depth)
            return Read::TooLarge;
        if (arena.is_approximate(id))
            return Read::Inexact;
        const Node &node = arena.at(id);
        const ChildView kids = arena.children(id);
        out->den = Poly::constant(1);
        switch (node.kind) {
            case Kind::Integer: {
                Poly value(1);
                if (mpz_set_str(mpq_numref(value.at(0)), arena.text(id).c_str(), 10) != 0)
                    return Read::NotRational;
                value.trim();
                if (!value.bounded())
                    return Read::TooLarge;
                out->num = std::move(value);
                return Read::Ok;
            }
            case Kind::Decimal:
                return Read::Inexact;
            case Kind::Symbol:
                if (arena.text(id) != arena.text(variable))
                    return Read::OtherSymbol;
                out->num = Poly::variable();
                return Read::Ok;
            case Kind::Neg: {
                if (kids.size() != 1)
                    return Read::NotRational;
                const Read read = fraction(kids[0], out, depth + 1);
                if (read != Read::Ok)
                    return read;
                return poly_sub(Poly(), out->num, &out->num) ? Read::Ok : Read::TooLarge;
            }
            case Kind::Add: {
                out->num = Poly();
                for (NodeId child : kids) {
                    Fraction part;
                    const Read read = fraction(child, &part, depth + 1);
                    if (read != Read::Ok)
                        return read;
                    if (out->num.degree() + part.den.degree() > kPolyMaxDegree ||
                        part.num.degree() + out->den.degree() > kPolyMaxDegree ||
                        out->den.degree() + part.den.degree() > kPolyMaxDegree)
                        return Read::TooHigh;
                    Poly left, right, den;
                    if (!poly_mul(out->num, part.den, &left) || !poly_mul(part.num, out->den, &right) ||
                        !poly_add(left, right, &out->num) || !poly_mul(out->den, part.den, &den))
                        return Read::TooLarge;
                    out->den = std::move(den);
                }
                return Read::Ok;
            }
            case Kind::Mul: {
                out->num = Poly::constant(1);
                for (NodeId child : kids) {
                    Fraction part;
                    const Read read = fraction(child, &part, depth + 1);
                    if (read != Read::Ok)
                        return read;
                    if (out->num.degree() + part.num.degree() > kPolyMaxDegree ||
                        out->den.degree() + part.den.degree() > kPolyMaxDegree)
                        return Read::TooHigh;
                    Poly num, den;
                    if (!poly_mul(out->num, part.num, &num) || !poly_mul(out->den, part.den, &den))
                        return Read::TooLarge;
                    out->num = std::move(num);
                    out->den = std::move(den);
                }
                return Read::Ok;
            }
            case Kind::Pow: {
                if (kids.size() != 2)
                    return Read::NotRational;
                int64_t exponent = 0;
                if (arena.is_approximate(kids[1]) || !small_integer(arena, kids[1], &exponent))
                    return depends_on(arena, id, variable) ? Read::NotRational : Read::Inexact;
                Fraction base;
                const Read read = fraction(kids[0], &base, depth + 1);
                if (read != Read::Ok)
                    return read;
                const bool constant = base.num.degree() <= 0 && base.den.degree() <= 0;
                const int64_t magnitude = exponent < 0 ? -exponent : exponent;
                if (!constant && magnitude * std::max(base.num.degree(), base.den.degree()) > kPolyMaxDegree)
                    return Read::TooHigh;
                if (constant && magnitude > 4096)
                    return Read::TooLarge;
                if (exponent < 0) {
                    if (base.num.zero())
                        return Read::DivideByZero;
                    if (base.num.degree() >= 1)
                        denominators.push_back(base.num);
                    std::swap(base.num, base.den);
                }
                out->num = Poly::constant(1);
                for (int64_t i = 0; i < magnitude; ++i) {
                    Poly num, den;
                    if (!poly_mul(out->num, base.num, &num) || !poly_mul(out->den, base.den, &den))
                        return Read::TooLarge;
                    out->num = std::move(num);
                    out->den = std::move(den);
                }
                return Read::Ok;
            }
            default:
                return depends_on(arena, id, variable) ? Read::NotRational : Read::Inexact;
        }
    }
};

const char *read_refusal(Read read);

struct Run {
    Arena &arena;
    Derivation &derivation;
    NodeId input;
    NodeId variable;
    RationalGoal goal;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    RestrictionSet recorded;
    bool failed = false;
    RationalOutcome failure = RationalOutcome::OutsideEnvelope;
    std::string detail;

    Run(Arena &a, Derivation &d, NodeId e, NodeId v, RationalGoal g, const Budget &budget)
        : arena(a), derivation(d), input(e), variable(v), goal(g), meter(budget), mark(d.mark()) {}

    bool refuse(RationalOutcome outcome, std::string why) {
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
            return refuse(RationalOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped() || !meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? RationalOutcome::Cancelled
                                                          : RationalOutcome::ResourceExceeded,
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

    // A coefficient narrowed to the int64 rational a node is built from.
    NodeId number(mpq_srcptr value) {
        Rational narrow;
        if (!detail::mpq_get_rational(value, &narrow)) {
            refuse(RationalOutcome::ResourceExceeded, "a coefficient is too large to write as an exact fraction");
            return kNoNode;
        }
        return canonical_rational(arena, narrow);
    }

    NodeId poly_node(const Poly &p) {
        if (p.zero())
            return arena.integer("0");
        std::vector<NodeId> terms;
        for (size_t i = p.size(); i-- > 0;) {
            if (mpq_sgn(p.at(i)) == 0)
                continue;
            NodeId power = i == 0 ? kNoNode : i == 1 ? variable
                                                     : arena.binary(Kind::Pow, variable, arena.integer(std::to_string(i)));
            NodeId coefficient = number(p.at(i));
            if (coefficient == kNoNode)
                return kNoNode;
            if (power == kNoNode)
                terms.push_back(coefficient);
            else if (mpq_cmp_ui(p.at(i), 1, 1) == 0)
                terms.push_back(power);
            else
                terms.push_back(arena.binary(Kind::Mul, coefficient, power));
        }
        return terms.size() == 1 ? terms[0] : arena.nary(Kind::Add, terms);
    }

    // num over den with den made monic, so a constant denominator folds into the numerator.
    NodeId fraction_node(const Poly &num, const Poly &den) {
        Poly n = num, d = den;
        detail::Mpq lead;
        mpq_set(lead.get(), d.at(static_cast<size_t>(d.degree())));
        for (size_t i = 0; i < n.size(); ++i)
            mpq_div(n.at(i), n.at(i), lead.get());
        for (size_t i = 0; i < d.size(); ++i)
            mpq_div(d.at(i), d.at(i), lead.get());
        const NodeId top = poly_node(n);
        if (top == kNoNode || d.degree() == 0)
            return top;
        const NodeId bottom = poly_node(d);
        if (bottom == kNoNode)
            return kNoNode;
        return arena.binary(Kind::Mul, top, arena.binary(Kind::Pow, bottom, arena.integer("-1")));
    }

    // Two rational functions whose cross-multiplied difference has degree at most bound agree
    // everywhere once they agree at bound + 1 points where both have a value.
    VerificationRecord identity(NodeId before, NodeId after, int bound, const char *obligation) {
        VerificationRecord record{"exact evaluation at more points than the cross-multiplied degree",
                                  VerificationOutcome::Inconclusive, EvidenceStrength::Unsupported,
                                  "too few points had a value on both sides", obligation};
        const std::string name = arena.text(variable);
        int agreed = 0;
        for (int j = 0; j < bound + 1 + 2 * kPolyMaxDegree + 8 && agreed <= bound; ++j) {
            const Rational at{2 * j + 1, 3};
            Rational left, right;
            if (!evaluate_rational(arena, before, {{name, at}}, &left) ||
                !evaluate_rational(arena, after, {{name, at}}, &right))
                continue;
            if (!rational_equal(left, right)) {
                record.outcome = VerificationOutcome::Failed;
                record.strength = EvidenceStrength::Failed;
                record.detail = "the two sides differ at " + name + " = " + std::to_string(at.num) + "/3";
                return record;
            }
            ++agreed;
        }
        if (agreed > bound) {
            record.outcome = VerificationOutcome::Passed;
            record.strength = EvidenceStrength::StructurallyValid;
            record.detail = "both sides agree at " + std::to_string(agreed) +
                            " points, more than the degree " + std::to_string(bound) +
                            " their cross-multiplied difference can have, read from the polynomials";
        }
        return record;
    }

    NodeId linear_factor(const Rational &root) {
        if (root.num == 0)
            return variable;
        return arena.nary(Kind::Add, {variable, canonical_rational(arena, Rational{-root.num, root.den})});
    }

    // Long division when the fraction is improper, then one term A over (x - r) for each root, each
    // A found by the cover-up rule and checked against the numerator at that root.
    NodeId partial_fractions(NodeId before, const Poly &top, const Poly &bottom, const std::vector<Rational> &roots) {
        Poly num = top, den = bottom;
        detail::Mpq lead;
        mpq_set(lead.get(), den.at(static_cast<size_t>(den.degree())));
        for (size_t i = 0; i < num.size(); ++i)
            mpq_div(num.at(i), num.at(i), lead.get());
        for (size_t i = 0; i < den.size(); ++i)
            mpq_div(den.at(i), den.at(i), lead.get());
        Poly quotient, remainder;
        if (!poly_divide(num, den, &quotient, &remainder)) {
            refuse(RationalOutcome::ResourceExceeded, read_refusal(Read::TooLarge));
            return kNoNode;
        }
        NodeId current = before;
        NodeId polynomial_part = kNoNode;
        if (!quotient.zero()) {
            Poly product, back;
            const bool exact = poly_mul(quotient, den, &product) && poly_add(product, remainder, &back) &&
                               poly_equal(back, num);
            polynomial_part = poly_node(quotient);
            const NodeId proper = remainder.zero() ? kNoNode : fraction_node(remainder, den);
            if (polynomial_part == kNoNode || (!remainder.zero() && proper == kNoNode))
                return kNoNode;
            const NodeId after = proper == kNoNode ? polynomial_part : arena.nary(Kind::Add, {polynomial_part, proper});
            if (!step())
                return kNoNode;
            Step s;
            s.phase = "Divide";
            s.goal = "Divide out the polynomial part";
            s.rule_id = "pf.divide";
            s.rule_name = "Divide the numerator by the denominator";
            s.claim = ClaimType::EquivalentExpression;
            s.explanation_short = "The numerator's degree is not below the denominator's, so divide first.";
            s.explanation_detailed = "The quotient is the polynomial part and the remainder over the same denominator is what gets split.";
            s.proof_obligations.push_back({"obl.rational.exact-division",
                "the quotient times the denominator plus the remainder is the numerator"});
            s.verifications.push_back({"exact polynomial division", exact ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                exact ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed,
                exact ? "the quotient times the denominator plus the remainder gives the numerator exactly"
                      : "the division does not reproduce the numerator", "obl.rational.exact-division"});
            TransformationPayload change;
            change.before = current;
            change.after = after;
            change.reversible = true;
            change.concrete_action = "Divide to get " + print(arena, after) + ".";
            derivation.add_transformation(plan, std::move(s), std::move(change));
            if (!running())
                return kNoNode;
            if (!exact) {
                refuse(RationalOutcome::VerificationFailed, "the division does not reproduce the numerator");
                return kNoNode;
            }
            current = after;
            if (remainder.zero())
                return after;
        }

        std::vector<NodeId> terms;
        if (polynomial_part != kNoNode)
            terms.push_back(polynomial_part);
        detail::Mpq at, top_value, coefficient, product, gap;
        for (size_t i = 0; i < roots.size(); ++i) {
            detail::mpq_set_rational(at.get(), roots[i]);
            poly_evaluate(remainder, at.get(), top_value.get());
            mpq_set_ui(product.get(), 1, 1);
            for (size_t j = 0; j < roots.size(); ++j) {
                if (j == i)
                    continue;
                detail::mpq_set_rational(gap.get(), roots[j]);
                mpq_sub(gap.get(), at.get(), gap.get());
                mpq_mul(product.get(), product.get(), gap.get());
            }
            Poly derivative;
            poly_derivative(den, &derivative);
            detail::Mpq slope, recomputed;
            poly_evaluate(derivative, at.get(), slope.get());
            if (mpq_sgn(slope.get()) == 0) {
                refuse(RationalOutcome::VerificationFailed, "a root of the denominator is repeated");
                return kNoNode;
            }
            mpq_div(coefficient.get(), top_value.get(), slope.get());
            mpq_mul(recomputed.get(), coefficient.get(), product.get());
            const bool covered = mpq_equal(recomputed.get(), top_value.get());
            const NodeId a_node = number(coefficient.get());
            const NodeId factor = linear_factor(roots[i]);
            if (a_node == kNoNode || !step())
                return kNoNode;
            const NodeId root_node = canonical_rational(arena, roots[i]);
            Step s;
            s.phase = "Split";
            s.goal = "Find the numerator over " + print(arena, factor);
            s.rule_id = "pf.cover-up";
            s.rule_name = "Cover up the factor and evaluate at its root";
            s.claim = ClaimType::Definition;
            s.explanation_short = "Cover up " + print(arena, factor) + " and put " + arena.text(variable) + " = " +
                                  print(arena, root_node) + " into what is left.";
            s.explanation_detailed = "Every other term vanishes at this root, so the numerator of this term is the "
                                     "remainder divided by the other factors there.";
            s.proof_obligations.push_back({"obl.rational.cover-up",
                "the coefficient times the other factors at the root equals the remainder at the root"});
            s.verifications.push_back({"exact evaluation at the root", covered ? VerificationOutcome::Passed : VerificationOutcome::Failed,
                covered ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed,
                covered ? "the coefficient times the other factors at the root gives the remainder there exactly"
                        : "the coefficient does not reproduce the remainder at the root", "obl.rational.cover-up"});
            CheckPayload check;
            check.target_claim = "The numerator over " + print(arena, factor) + " is " + print(arena, a_node);
            check.check_method = "exact evaluation at the root";
            check.expected_relation = "the coefficient times the other factors equals the remainder at the root";
            check.observed_result = print(arena, a_node);
            derivation.add_check(plan, std::move(s), std::move(check));
            if (!running())
                return kNoNode;
            if (!covered) {
                refuse(RationalOutcome::VerificationFailed, "a cover-up coefficient does not reproduce the remainder");
                return kNoNode;
            }
            if (mpq_sgn(coefficient.get()) != 0)
                terms.push_back(arena.binary(Kind::Mul, a_node, arena.binary(Kind::Pow, factor, arena.integer("-1"))));
        }
        const NodeId split = terms.empty() ? arena.integer("0") : terms.size() == 1 ? terms[0] : arena.nary(Kind::Add, terms);
        if (!step())
            return kNoNode;
        Step s;
        s.phase = "Split";
        s.goal = "Write the sum of partial fractions";
        s.rule_id = "pf.decompose";
        s.rule_name = "Write one fraction for each linear factor";
        s.claim = ClaimType::EquivalentExpression;
        s.explanation_short = "Add the fractions found for each factor.";
        s.explanation_detailed = "The denominators are the linear factors and the numerators are the numbers the cover-up gave.";
        s.proof_obligations.push_back({"obl.rational.same-values",
            "the new form has the value of the old one wherever both are defined"});
        s.verifications.push_back(identity(current, split, 2 * kPolyMaxDegree, "obl.rational.same-values"));
        const bool passed = s.verifications.back().outcome == VerificationOutcome::Passed;
        const std::string why = s.verifications.back().detail;
        TransformationPayload change;
        change.before = current;
        change.after = split;
        change.reversible = true;
        change.concrete_action = "Write " + print(arena, split) + ".";
        derivation.add_transformation(plan, std::move(s), std::move(change));
        if (!running())
            return kNoNode;
        if (!passed) {
            refuse(RationalOutcome::VerificationFailed, why);
            return kNoNode;
        }
        return split;
    }

    RationalResult finish(RationalOutcome outcome, NodeId expression, std::string why) {
        RationalResult result;
        result.outcome = outcome;
        result.detail = std::move(why);
        if (arena.failed()) {
            result.outcome = RationalOutcome::ResourceExceeded;
            result.detail = status_name(arena.status());
        }
        std::vector<std::string> assumptions;
        switch (result.outcome) {
            case RationalOutcome::Rewritten:
                result.expression = expression;
                recorded.settle(arena, derivation, &assumptions);
                result.status = derivation.outcome_from(mark);
                break;
            case RationalOutcome::NotRational:
            case RationalOutcome::OutsideEnvelope: result.status = DerivationStatus::Unsupported; break;
            case RationalOutcome::InvalidInput: result.status = DerivationStatus::InvalidInput; break;
            case RationalOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case RationalOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case RationalOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        if (result.outcome != RationalOutcome::Rewritten) {
            recorded.settle(arena, derivation, &assumptions);
            assumptions.clear();
            keep_verified_prefix(derivation, mark, arena);
        }
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = goal == RationalGoal::Normal ? "algebra.rational-expression.single-variable"
                                                                 : "algebra.partial-fractions.linear-factors";
        context.requested_method = goal == RationalGoal::Normal ? "normal" : "partial fractions";
        context.normalized_problem_model = input;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = input < arena.node_count() ? print(arena, input) : std::string();
        context.numeric_mode = derivation.request.numeric_mode;
        context.active_assumptions = assumptions;
        context.angle_convention = "not applicable";
        context.branch_convention = "real domain, excluded values kept";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    RationalResult stopped() { return finish(failure, kNoNode, detail); }
};

const char *read_refusal(Read read) {
    switch (read) {
        case Read::NotRational: return "the expression is not a quotient of polynomials in the variable";
        case Read::OtherSymbol: return "the expression has a symbol other than the variable";
        case Read::Inexact: return "a coefficient is not an exact rational number";
        case Read::TooHigh: return "a degree is above the envelope of 12";
        case Read::DivideByZero: return "a denominator is identically zero, so the expression has no value";
        case Read::TooLarge: return "a coefficient is beyond the exact arithmetic bound";
        case Read::Halted:
        case Read::Ok: break;
    }
    return "the expression could not be read";
}

size_t product_factors(const Arena &arena, NodeId id) {
    if (arena.at(id).kind != Kind::Mul)
        return 1;
    size_t count = 0;
    for (NodeId child : arena.children(id))
        count += product_factors(arena, child);
    return count;
}

// Which rule names the move from the typed expression to one fraction.
const char *combine_rule(const Arena &arena, NodeId id, const char **name) {
    if (arena.at(id).kind == Kind::Add) {
        *name = "Write over a common denominator";
        return "rat.common-denominator";
    }
    if (arena.at(id).kind == Kind::Mul && product_factors(arena, id) > 2) {
        *name = "Multiply numerators and denominators";
        return "rat.multiply";
    }
    *name = "Expand the numerator and the denominator";
    return "rat.single-fraction";
}

}  // namespace

const char *rational_outcome_name(RationalOutcome outcome) {
    switch (outcome) {
        case RationalOutcome::Rewritten: return "rewritten";
        case RationalOutcome::NotRational: return "not rational";
        case RationalOutcome::InvalidInput: return "invalid input";
        case RationalOutcome::OutsideEnvelope: return "outside envelope";
        case RationalOutcome::VerificationFailed: return "verification failed";
        case RationalOutcome::Cancelled: return "cancelled";
        case RationalOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

RationalResult rational_expression(Arena &arena, Derivation &derivation, NodeId expression,
                                   NodeId variable, RationalGoal goal, const Budget &budget) {
    Run run(arena, derivation, expression, variable, goal, budget);
    if (!run.running())
        return run.stopped();
    if (expression >= arena.node_count() || variable >= arena.node_count() ||
        arena.at(variable).kind != Kind::Symbol)
        return run.finish(RationalOutcome::InvalidInput, kNoNode, "enter an expression and a variable");
    if (contains_list(arena, expression))
        return run.finish(RationalOutcome::NotRational, kNoNode, "lists are not rational expressions");
    if (derivation.request.numeric_mode != NumericMode::Exact)
        return run.finish(RationalOutcome::OutsideEnvelope, kNoNode, "rational expression walkthroughs require Exact mode");

    Reader reader{arena, variable, run.meter, {}};
    Fraction whole;
    const Read read = reader.fraction(expression, &whole, 0);
    if (read == Read::Halted) {
        run.running();
        return run.stopped();
    }
    if (read != Read::Ok) {
        const RationalOutcome outcome = read == Read::NotRational ? RationalOutcome::NotRational :
            read == Read::DivideByZero ? RationalOutcome::InvalidInput :
            read == Read::TooLarge ? RationalOutcome::ResourceExceeded : RationalOutcome::OutsideEnvelope;
        return run.finish(outcome, kNoNode, read_refusal(read));
    }
    if (whole.den.zero())
        return run.finish(RationalOutcome::InvalidInput, kNoNode, read_refusal(Read::DivideByZero));
    const std::string name = arena.text(variable);

    // Partial fractions over linear factors needs a denominator that splits into distinct rational
    // roots. Asked before anything is recorded, so a refusal leaves no half-built record.
    std::vector<Rational> linear_roots;
    if (goal == RationalGoal::PartialFractions && !whole.num.zero()) {
        Poly shared, reduced_den, rest, left;
        if (!poly_gcd(whole.num, whole.den, &shared) || !poly_divide(whole.den, shared, &reduced_den, &rest))
            return run.finish(RationalOutcome::ResourceExceeded, kNoNode, read_refusal(Read::TooLarge));
        std::vector<int> multiplicities;
        const RootSearch search = poly_rational_roots(reduced_den, run.meter, &linear_roots, &multiplicities, &left);
        if (search == RootSearch::Halted) {
            run.running();
            return run.stopped();
        }
        if (search == RootSearch::OutOfRange)
            return run.finish(RationalOutcome::OutsideEnvelope, kNoNode,
                              "the denominator's coefficients are too large to search for rational roots");
        if (left.degree() >= 1)
            return run.finish(RationalOutcome::OutsideEnvelope, kNoNode,
                              "the denominator has a factor with no rational root, such as an irreducible quadratic, "
                              "which partial fractions over linear factors cannot split");
        if (std::any_of(multiplicities.begin(), multiplicities.end(), [](int m) { return m > 1; }))
            return run.finish(RationalOutcome::OutsideEnvelope, kNoNode,
                              "the denominator has a repeated factor, which needs a term for each power of it");
    }

    if (!run.step())
        return run.stopped();
    Step plan;
    plan.phase = "Plan";
    const bool partial = goal == RationalGoal::PartialFractions;
    plan.goal = "Write " + print(arena, expression) + (partial ? " as partial fractions" : " as one reduced fraction");
    plan.rule_id = partial ? "plan.rational-partial-fractions" : "plan.rational-normal";
    plan.rule_name = partial ? "Reduce, divide, then split over the linear factors"
                             : "Combine into one fraction and cancel common factors";
    plan.explanation_short = partial
        ? "Reduce to one fraction, divide out any polynomial part, then give each linear factor its own fraction."
        : "Note the excluded values, write one fraction, then cancel what the top and bottom share.";
    plan.explanation_detailed = "A value that makes any denominator zero is excluded from the start, and it stays "
        "excluded even when the factor that caused it cancels.";
    PlanPayload payload;
    payload.strategy_id = plan.rule_id;
    payload.selected_strategy = plan.rule_name;
    payload.matched_problem_facts.push_back(print(arena, expression));
    payload.selection_rationale = "Every part is a polynomial in " + name + " with exact rational coefficients, or a quotient of such.";
    if (partial)
        register_strategy_precondition(payload, plan, "pre.rational.distinct-linear-factors",
            "the reduced denominator is a product of distinct linear factors with rational roots",
            "rational root search", EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
            std::to_string(linear_roots.size()) + " distinct rational roots and nothing left over");
    register_strategy_precondition(payload, plan, "pre.rational.one-variable",
        "every part is a polynomial in the variable with exact rational coefficients and degree at most 12, or a quotient of such",
        "exact rational function reading", EvidenceStrength::StructurallyValid, VerificationOutcome::Passed, plan.goal);
    run.plan = derivation.add_plan(kNoStep, std::move(plan), std::move(payload));
    if (!run.running())
        return run.stopped();

    // Every root a denominator has is an excluded value, found before anything is cancelled.
    if (!reader.denominators.empty()) {
        if (!run.step())
            return run.stopped();
        std::vector<Rational> excluded;
        std::vector<Restriction> restrictions;
        bool every_root_zeroes = true;
        for (const Poly &den : reader.denominators) {
            std::vector<Rational> roots;
            std::vector<int> multiplicities;
            Poly rest;
            const RootSearch search = poly_rational_roots(den, run.meter, &roots, &multiplicities, &rest);
            if (search == RootSearch::Halted) {
                run.running();
                return run.stopped();
            }
            if (search == RootSearch::OutOfRange) {
                rest = den;
                roots.clear();
            }
            detail::Mpq at, value;
            for (const Rational &root : roots) {
                detail::mpq_set_rational(at.get(), root);
                poly_evaluate(den, at.get(), value.get());
                every_root_zeroes = every_root_zeroes && mpq_sgn(value.get()) == 0;
                if (std::none_of(excluded.begin(), excluded.end(),
                                 [&root](const Rational &seen) { return rational_equal(seen, root); })) {
                    excluded.push_back(root);
                    const NodeId subject = arena.nary(Kind::Add, {variable, canonical_rational(arena, Rational{-root.num, root.den})});
                    restrictions.push_back({root.num == 0 ? variable : subject, Condition::NonZero});
                }
            }
            if (rest.degree() >= 1) {
                const NodeId subject = run.poly_node(rest);
                if (subject == kNoNode)
                    return run.stopped();
                restrictions.push_back({subject, Condition::NonZero});
            }
        }
        std::string listed;
        for (const Restriction &r : restrictions) {
            if (!listed.empty())
                listed += ", ";
            listed += restriction_text(arena, r);
        }
        Step step;
        step.phase = "Domain";
        step.goal = "Find the values the expression excludes";
        step.rule_id = "rat.excluded-values";
        step.rule_name = "Exclude the zeros of every denominator";
        step.claim = ClaimType::Definition;
        step.explanation_short = "The expression has no value where a denominator is zero: " + listed + ".";
        step.explanation_detailed = "These conditions hold for every later form, including one where the factor has cancelled.";
        step.proof_obligations.push_back({"obl.rational.excluded-values",
            "every excluded value makes one of the denominators zero"});
        step.verifications.push_back({"exact denominator evaluation",
            every_root_zeroes ? VerificationOutcome::Passed : VerificationOutcome::Failed,
            every_root_zeroes ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed,
            every_root_zeroes ? "each excluded value makes its denominator exactly zero"
                              : "an excluded value does not make its denominator zero",
            "obl.rational.excluded-values"});
        CheckPayload check;
        check.target_claim = "The expression is defined exactly when " + listed;
        check.check_method = "exact denominator evaluation";
        check.expected_relation = "each excluded value is a zero of a denominator";
        check.observed_result = listed;
        const StepId here = derivation.add_check(run.plan, std::move(step), std::move(check));
        for (const Restriction &r : restrictions)
            run.recorded.add(r, here);
        if (!run.running())
            return run.stopped();
        if (!every_root_zeroes)
            return run.finish(RationalOutcome::VerificationFailed, kNoNode, "an excluded value does not zero its denominator");
    }

    const NodeId combined = run.fraction_node(whole.num, whole.den);
    if (combined == kNoNode)
        return run.stopped();
    const int whole_degree = std::max(whole.num.degree(), whole.den.degree());
    if (combined != expression) {
        if (!run.step())
            return run.stopped();
        const char *rule_name = nullptr;
        const char *rule_id = combine_rule(arena, expression, &rule_name);
        Step step;
        step.phase = "Combine";
        step.goal = "Write the expression as one fraction";
        step.rule_id = rule_id;
        step.rule_name = rule_name;
        step.claim = ClaimType::EquivalentExpression;
        const std::string rule(rule_id);
        step.explanation_short = rule == "rat.common-denominator"
            ? "Write every term over the same denominator, then add the numerators."
            : rule == "rat.multiply" ? "Multiply the numerators together and the denominators together."
                                     : "Expand the numerator and the denominator.";
        step.explanation_detailed = "A sum needs every term over the same denominator before the numerators add. "
            "A product multiplies the numerators together and the denominators together.";
        step.proof_obligations.push_back({"obl.rational.same-values",
            "the new form has the value of the old one wherever both are defined"});
        step.verifications.push_back(run.identity(expression, combined, 2 * kPolyMaxDegree, "obl.rational.same-values"));
        const bool passed = step.verifications.back().outcome == VerificationOutcome::Passed;
        const std::string why = step.verifications.back().detail;
        TransformationPayload change;
        change.before = expression;
        change.after = combined;
        change.reversible = true;
        change.concrete_action = "Write " + print(arena, combined) + ".";
        derivation.add_transformation(run.plan, std::move(step), std::move(change));
        if (!run.running())
            return run.stopped();
        if (!passed)
            return run.finish(RationalOutcome::VerificationFailed, kNoNode, why);
    }

    Poly common;
    if (!poly_gcd(whole.num.zero() ? whole.den : whole.num, whole.den, &common))
        return run.finish(RationalOutcome::ResourceExceeded, kNoNode, read_refusal(Read::TooLarge));
    Poly num = whole.num, den = whole.den;
    NodeId result = combined;
    if (!whole.num.zero() && common.degree() >= 1) {
        Poly num_rest, den_rest;
        Poly reduced_num, reduced_den;
        if (!poly_divide(whole.num, common, &reduced_num, &num_rest) ||
            !poly_divide(whole.den, common, &reduced_den, &den_rest))
            return run.finish(RationalOutcome::ResourceExceeded, kNoNode, read_refusal(Read::TooLarge));
        Poly back_num, back_den;
        const bool exact = num_rest.zero() && den_rest.zero() &&
                           poly_mul(reduced_num, common, &back_num) && poly_mul(reduced_den, common, &back_den) &&
                           poly_equal(back_num, whole.num) && poly_equal(back_den, whole.den);
        num = std::move(reduced_num);
        den = std::move(reduced_den);
        result = run.fraction_node(num, den);
        const NodeId factor = run.poly_node(common);
        if (result == kNoNode || factor == kNoNode)
            return run.stopped();
        if (!run.step())
            return run.stopped();
        Step step;
        step.phase = "Cancel";
        step.goal = "Cancel the factor the numerator and denominator share";
        step.rule_id = "rat.cancel-common-factor";
        step.rule_name = "Cancel a common factor";
        step.claim = ClaimType::EquivalentExpression;
        step.explanation_short = "Divide the top and the bottom by " + print(arena, factor) + ".";
        step.explanation_detailed = "Where " + print(arena, factor) + " is zero the original expression has no value, "
            "so those values stay excluded even though the factor is gone from the result.";
        step.proof_obligations.push_back({"obl.rational.exact-cancellation",
            "the numerator and the denominator are each the reduced one times the cancelled factor"});
        step.verifications.push_back({"exact polynomial division", exact ? VerificationOutcome::Passed : VerificationOutcome::Failed,
            exact ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed,
            exact ? "multiplying back by the cancelled factor returns the numerator and the denominator exactly"
                  : "the cancelled factor does not divide both exactly",
            "obl.rational.exact-cancellation"});
        TransformationPayload change;
        change.before = combined;
        change.after = result;
        change.reversible = false;
        change.concrete_action = "Cancel " + print(arena, factor) + " to get " + print(arena, result) + ".";
        derivation.add_transformation(run.plan, std::move(step), std::move(change));
        if (!run.running())
            return run.stopped();
        if (!exact)
            return run.finish(RationalOutcome::VerificationFailed, kNoNode, "the cancelled factor does not divide exactly");
    }

    if (partial && den.degree() >= 1 && !num.zero()) {
        const NodeId decomposed = run.partial_fractions(result, num, den, linear_roots);
        if (decomposed == kNoNode)
            return run.stopped();
        result = decomposed;
    }

    if (!run.step())
        return run.stopped();
    const int bound = whole_degree + std::max(num.degree(), den.degree());
    VerificationRecord same = run.identity(expression, result, bound, "obl.rational.same-values");
    Step check;
    check.phase = "Check";
    check.goal = "Check the result against the expression as typed";
    check.rule_id = "rat.check-equivalent";
    check.rule_name = "Check the result has the same values";
    check.claim = ClaimType::EquivalentExpression;
    check.explanation_short = "Evaluate both at enough points to fix a rational function of this degree.";
    check.explanation_detailed = "Two fractions of polynomials that agree at more points than the degree of their "
        "cross-multiplied difference are the same function wherever both are defined.";
    check.proof_obligations.push_back({"obl.rational.same-values",
        "the new form has the value of the old one wherever both are defined"});
    const VerificationOutcome outcome = same.outcome;
    const std::string why = same.detail;
    check.verifications.push_back(std::move(same));
    CheckPayload payload_check;
    payload_check.target_claim = print(arena, result) + " equals " + print(arena, expression) + " wherever both are defined";
    payload_check.check_method = "exact evaluation at more points than the cross-multiplied degree";
    payload_check.expected_relation = "equal values";
    payload_check.observed_result = why;
    derivation.add_check(run.plan, std::move(check), std::move(payload_check));
    if (!run.running())
        return run.stopped();
    if (outcome != VerificationOutcome::Passed)
        return run.finish(outcome == VerificationOutcome::Failed ? RationalOutcome::VerificationFailed
                                                                 : RationalOutcome::ResourceExceeded,
                          kNoNode, why);
    return run.finish(RationalOutcome::Rewritten, result,
                      partial ? "partial fractions over the linear factors with every excluded value kept"
                              : "one reduced fraction with every excluded value kept");
}

}  // namespace nps
