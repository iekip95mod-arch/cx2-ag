#include "nps/steps/rewrite.h"

#include <numeric>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"

namespace nps {
namespace {

const size_t kSamples = 6;
// Past this the divisor scan for the product and sum pair stops being instant on the calculator,
// and a rule that takes a visible pause is worse than one that says it will not try.
const int64_t kFactorSearchLimit = 1000000;

struct Context {
    Context(Arena &a, Derivation &d, Meter &m) : arena(a), derivation(d), meter(m) {}

    Arena &arena;
    Derivation &derivation;
    Meter &meter;
    StepId plan = kNoStep;
    bool failed = false;
    std::string detail;
    RewriteOutcome outcome = RewriteOutcome::UnsupportedForm;
};

void refuse(Context &ctx, RewriteOutcome outcome, const std::string &why) {
    ctx.failed = true;
    ctx.outcome = outcome;
    ctx.detail = why;
}

NodeId value_node(Arena &arena, const Rational &value) {
    if (value.den == 1)
        return arena.integer(integer_text(value.num));
    NodeId denominator = arena.binary(Kind::Pow, arena.integer(integer_text(value.den)),
                                      arena.unary(Kind::Neg, arena.integer("1")));
    return arena.binary(Kind::Mul, arena.integer(integer_text(value.num)), denominator);
}

bool numeric(const Arena &arena, NodeId id, Rational *out) {
    static const std::vector<SymbolValue> none;
    return evaluate_rational(arena, id, none, out);
}

// Whether the expression is already written as the number it is, which is what stops the folding
// rule from rewriting 2^(-1) into 1 * 2^(-1) and calling it an operation.
bool in_number_form(Arena &arena, NodeId id) {
    Rational value;
    if (!numeric(arena, id, &value))
        return false;
    const Node &node = arena.at(id);
    if (node.kind == Kind::Integer ||
        (node.kind == Kind::Neg && arena.at(arena.children(node)[0]).kind == Kind::Integer))
        return true;
    return value_node(arena, value) == id;
}

std::string number_text(Arena &arena, const Rational &value) {
    return print(arena, value_node(arena, value));
}

struct Term {
    Rational coefficient{1, 1};
    // The part that is not a number. kNoNode means the term is a bare number.
    NodeId rest = kNoNode;
};

NodeId product_of(Arena &arena, const std::vector<NodeId> &factors) {
    if (factors.empty())
        return kNoNode;
    return factors.size() == 1 ? factors[0] : arena.nary(Kind::Mul, factors);
}

bool term_of(Arena &arena, NodeId id, Term *out) {
    Rational value;
    if (numeric(arena, id, &value)) {
        out->coefficient = value;
        out->rest = kNoNode;
        return true;
    }
    const Node &n = arena.at(id);
    if (n.kind == Kind::Neg) {
        Term inner;
        if (!term_of(arena, arena.children(n)[0], &inner))
            return false;
        Rational negated;
        if (!negate_fraction(inner.coefficient.num, inner.coefficient.den, &negated.num,
                             &negated.den))
            return false;
        out->coefficient = negated;
        out->rest = inner.rest;
        return true;
    }
    if (n.kind == Kind::Mul) {
        Rational coefficient{1, 1};
        std::vector<NodeId> rest;
        // A product inside a product is one product, however the parser bracketed it, so 2 * (4 * x)
        // has the coefficient 8 rather than 2 and a rest of 4 * x.
        std::vector<NodeId> pending;
        for (NodeId factor : arena.children(n))
            pending.push_back(factor);
        for (size_t i = 0; i < pending.size(); ++i) {
            const NodeId factor = pending[i];
            Rational factor_value;
            if (numeric(arena, factor, &factor_value)) {
                if (!rational_mul(coefficient, factor_value, &coefficient))
                    return false;
                continue;
            }
            if (arena.at(factor).kind == Kind::Mul) {
                for (NodeId inner : arena.children(factor))
                    pending.push_back(inner);
                continue;
            }
            // A factor can carry a sign of its own, and (-x)*y is the same term as -(x*y). Without
            // this the two spellings key differently and never gather, so (-x)*y + x*y came back as
            // already in that form, which is a claim that nothing can be done about an expression
            // equal to zero. What is left under the sign goes back through pending, so a product
            // there is flattened like any other.
            if (arena.at(factor).kind == Kind::Neg) {
                Term signed_factor;
                if (!term_of(arena, factor, &signed_factor))
                    return false;
                if (!rational_mul(coefficient, signed_factor.coefficient, &coefficient))
                    return false;
                if (signed_factor.rest != kNoNode)
                    pending.push_back(signed_factor.rest);
                continue;
            }
            rest.push_back(factor);
        }
        out->coefficient = coefficient;
        out->rest = product_of(arena, rest);
        return true;
    }
    out->coefficient = Rational{1, 1};
    out->rest = id;
    return true;
}

NodeId term_node(Arena &arena, const Term &term) {
    if (term.rest == kNoNode)
        return value_node(arena, term.coefficient);
    if (term.coefficient.den == 1 && term.coefficient.num == 1)
        return term.rest;
    if (term.coefficient.den == 1 && term.coefficient.num == -1)
        return arena.unary(Kind::Neg, term.rest);
    return arena.binary(Kind::Mul, value_node(arena, term.coefficient), term.rest);
}

NodeId sum_of(Arena &arena, const std::vector<Term> &terms) {
    std::vector<NodeId> parts;
    for (const Term &term : terms) {
        if (term.coefficient.num == 0)
            continue;
        parts.push_back(term_node(arena, term));
    }
    if (parts.empty())
        return arena.integer("0");
    return parts.size() == 1 ? parts[0] : arena.nary(Kind::Add, parts);
}

// Every term of a sum, however the parser bracketed it. a + b + c arrives as one sum inside
// another, and a rule that read only the outer one would see two terms where a reader sees three.
bool terms_of(Arena &arena, NodeId id, std::vector<Term> *out) {
    if (arena.at(id).kind != Kind::Add) {
        Term one;
        if (!term_of(arena, id, &one))
            return false;
        out->push_back(one);
        return true;
    }
    for (NodeId part : arena.children(id)) {
        if (!terms_of(arena, part, out))
            return false;
    }
    return true;
}

struct Factor {
    NodeId base = kNoNode;
    int64_t exponent = 1;
};

// A term's non-numeric part as bases and whole-number exponents, which is what a common factor is
// taken over. A base with an exponent this cannot read is left with exponent one, so the common
// factor is understated rather than wrong.
void factors_of(Arena &arena, NodeId rest, std::vector<Factor> *out) {
    if (rest == kNoNode)
        return;
    const Node &n = arena.at(rest);
    if (n.kind == Kind::Mul) {
        for (NodeId factor : arena.children(n))
            factors_of(arena, factor, out);
        return;
    }
    Factor f;
    if (n.kind == Kind::Pow) {
        int64_t exponent = 0;
        if (small_integer(arena, arena.children(n)[1], &exponent) && exponent > 0) {
            f.base = arena.children(n)[0];
            f.exponent = exponent;
            out->push_back(f);
            return;
        }
    }
    f.base = rest;
    f.exponent = 1;
    out->push_back(f);
}

NodeId power_node(Arena &arena, NodeId base, int64_t exponent) {
    if (exponent == 1)
        return base;
    return arena.binary(Kind::Pow, base, arena.integer(integer_text(exponent)));
}

// What decides whether two terms are alike. The bases are put in one order and repeats are added
// up, so x * x and x^2 land on the same key and so do x * y and y * x. The canonical form cannot do
// this: it folds constants and leaves x * x as a product of two factors.
NodeId monomial_key(Arena &arena, NodeId rest, bool *combined = nullptr) {
    if (combined)
        *combined = false;
    if (rest == kNoNode)
        return kNoNode;
    std::vector<Factor> factors;
    factors_of(arena, rest, &factors);
    std::vector<Factor> merged;
    for (const Factor &f : factors) {
        bool found = false;
        for (Factor &into : merged) {
            if (into.base != f.base)
                continue;
            into.exponent += f.exponent;
            found = true;
            break;
        }
        if (!found)
            merged.push_back(f);
    }
    if (combined)
        *combined = merged.size() != factors.size();
    for (size_t i = 1; i < merged.size(); ++i) {
        for (size_t j = i; j > 0 && merged[j].base < merged[j - 1].base; --j) {
            const Factor held = merged[j];
            merged[j] = merged[j - 1];
            merged[j - 1] = held;
        }
    }
    std::vector<NodeId> parts;
    for (const Factor &f : merged)
        parts.push_back(power_node(arena, f.base, f.exponent));
    return product_of(arena, parts);
}

Step envelope(const std::string &goal, const char *rule_id, const char *rule_name,
              const std::string &why) {
    Step s;
    s.phase = "rewrite";
    s.goal = goal;
    s.rule_id = rule_id;
    s.rule_name = rule_name;
    s.explanation_short = why;
    s.claim = ClaimType::EquivalentExpression;
    return s;
}

VerificationRecord passed(const char *method, EvidenceStrength strength,
                          const std::string &detail) {
    VerificationRecord v;
    v.method = method;
    v.outcome = VerificationOutcome::Passed;
    v.strength = strength;
    v.detail = detail;
    return v;
}

bool record(Context &ctx, Step s, NodeId before, NodeId after, const std::string &action,
            const std::vector<uint32_t> &path) {
    if (!ctx.meter.step()) {
        ctx.failed = true;
        ctx.outcome = RewriteOutcome::Refused;
        ctx.detail = halt_name(ctx.meter.halt());
        return false;
    }
    // PERF-008's repeated canonical state. The arena interns, so a state already reached is the same
    // NodeId and a cycle is an equality rather than a comparison. Asked here because every pass
    // records through this one gate, and only where a rule can revisit: a pass that changes nothing
    // records nothing, so a state arriving twice is a loop rather than a rule standing still.
    if (!ctx.meter.reached(after)) {
        ctx.failed = true;
        ctx.outcome = RewriteOutcome::Refused;
        ctx.detail = halt_name(ctx.meter.halt());
        return false;
    }
    TransformationPayload p;
    p.before = before;
    p.after = after;
    p.concrete_action = action;
    p.path = path;
    p.reversible = true;
    ctx.derivation.add_transformation(ctx.plan, std::move(s), std::move(p));
    return true;
}

// What a rule did at one node. A rule with nothing to do here leaves after as kNoNode.
struct Local {
    NodeId after;
    std::string what;
    Local() : after(kNoNode) {}
};

typedef Local (*NodeRule)(Arena &arena, NodeId id, void *state);

// Which end of the tree a rule should be offered first. The difference is visible to a student, not
// an implementation detail: arithmetic wants the innermost operation, because that is the one they
// would do next by hand, while a sum rule wants the widest sum, or it gathers a nested piece and
// leaves the surrounding terms looking unfinished.
enum class Descend : uint8_t {
    InnermostFirst,
    OutermostFirst,
};

// The walk that used to be written inside fold_once and again inside distribute_once, and that the
// sum rules did not have at all. Shared rather than repeated, because a rule that never descends
// looks exactly like a rule with nothing to do: the sum rules read only the root for as long as they
// existed, and a denominator that expanded to zero came back as a successful rewrite of an undefined
// expression.
NodeId rewrite_once(Arena &arena, NodeId id, NodeRule rule, void *state, Descend order,
                    std::vector<uint32_t> *path, std::string *what) {
    if (order == Descend::OutermostFirst) {
        const Local here = rule(arena, id, state);
        if (here.after != kNoNode) {
            *what = here.what;
            return here.after;
        }
    }
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(n);
    for (size_t i = 0; i < kids.size(); ++i) {
        path->push_back(static_cast<uint32_t>(i));
        const NodeId rewritten = rewrite_once(arena, kids[i], rule, state, order, path, what);
        if (rewritten != kNoNode) {
            std::vector<NodeId> rebuilt;
            for (size_t j = 0; j < kids.size(); ++j)
                rebuilt.push_back(j == i ? rewritten : kids[j]);
            if (n.kind == Kind::Call)
                return arena.call(arena.text(id), rebuilt);
            return arena.nary(n.kind, rebuilt);
        }
        path->pop_back();
    }
    if (order == Descend::OutermostFirst)
        return kNoNode;
    const Local local = rule(arena, id, state);
    if (local.after == kNoNode)
        return kNoNode;
    *what = local.what;
    return local.after;
}

// One arithmetic operation on numbers that are already written as numbers, so what a reader sees is
// the operation they would do next by hand.
Local fold_here(Arena &arena, NodeId id, void *) {
    Local out;
    const Node &n = arena.at(id);
    if (n.kind != Kind::Add && n.kind != Kind::Mul && n.kind != Kind::Pow && n.kind != Kind::Neg)
        return out;
    if (n.kind == Kind::Neg && in_number_form(arena, id))
        return out;
    const ChildView kids = arena.children(n);
    for (size_t i = 0; i < kids.size(); ++i) {
        if (!in_number_form(arena, kids[i]))
            return out;
    }
    Rational value;
    if (!numeric(arena, id, &value))
        return out;
    const NodeId folded = value_node(arena, value);
    if (folded == kNoNode || arena.at(folded).size >= n.size)
        return out;

    const char *operation = n.kind == Kind::Add ? "Add"
                            : n.kind == Kind::Mul ? "Multiply"
                            : n.kind == Kind::Neg ? "Negate"
                                                 : "Raise";
    out.what = std::string(operation) + " " + print(arena, n.kind == Kind::Neg ? kids[0] : id) + " to get " +
               number_text(arena, value);
    out.after = folded;
    return out;
}

bool fold_arithmetic(Context &ctx, NodeId *expression, const std::string &goal) {
    Arena &a = ctx.arena;
    while (!ctx.failed) {
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RewriteOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            return false;
        }
        std::vector<uint32_t> path;
        std::string what;
        const NodeId folded =
            rewrite_once(a, *expression, fold_here, 0, Descend::InnermostFirst, &path, &what);
        if (folded == kNoNode)
            return true;

        Step s = envelope(goal, "alg.fold-arithmetic", "Work out the arithmetic",
                          "Numbers standing together are worked out first");
        s.explanation_detailed =
            "Only the numbers are touched: one operation on written numbers becomes the number it "
            "equals, and nothing else in the expression moves.";
        s.verifications.push_back(passed("exact rational arithmetic", EvidenceStrength::StructurallyValid, what));
        s.proof_obligations.push_back({"obl.alg.fold-preserves-value",
                                       "folding the constants leaves the expression's value alone"});
        if (!record(ctx, std::move(s), *expression, folded, what, path))
            return false;
        *expression = folded;
    }
    return false;
}

// State a sum rule needs to carry back out of the walk: which of two explanations fits, and whether
// it hit a wall rather than simply having nothing to do. Both are asked after every walk, so a wall
// deep in the tree refuses rather than being passed over for a shallower rewrite.
struct CollectState {
    bool numbers;
    bool overflowed;
    bool unbuildable;
    CollectState() : numbers(false), overflowed(false), unbuildable(false) {}
};

// Two terms of one sum that differ only by their coefficient, added into one. The group is chosen by
// the part of a term that is not its coefficient, so 3x and 5x go together and 3x and 5y do not.
Local collect_here(Arena &arena, NodeId id, void *state) {
    CollectState &st = *static_cast<CollectState *>(state);
    Local out;
    if (arena.at(id).kind != Kind::Add)
        return out;
    std::vector<Term> terms;
    if (!terms_of(arena, id, &terms)) {
        st.unbuildable = true;
        return out;
    }

    std::vector<NodeId> keys;
    for (const Term &term : terms)
        keys.push_back(monomial_key(arena, term.rest));

    size_t first = terms.size();
    size_t second = terms.size();
    for (size_t i = 0; i < terms.size() && first == terms.size(); ++i) {
        for (size_t j = i + 1; j < terms.size(); ++j) {
            if (keys[i] != keys[j])
                continue;
            first = i;
            second = j;
            break;
        }
    }
    if (first == terms.size())
        return out;

    Rational total;
    if (!rational_add(terms[first].coefficient, terms[second].coefficient, &total)) {
        st.overflowed = true;
        return out;
    }
    st.numbers = terms[first].rest == kNoNode;
    out.what =
        st.numbers ? "Add the numbers " + number_text(arena, terms[first].coefficient) + " and " +
                         number_text(arena, terms[second].coefficient) + " to get " +
                         number_text(arena, total)
                   : "Add the coefficients of " + print(arena, terms[first].rest) + ": " +
                         number_text(arena, terms[first].coefficient) + " and " +
                         number_text(arena, terms[second].coefficient) + " make " +
                         number_text(arena, total);

    std::vector<Term> combined;
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i == second)
            continue;
        Term next = terms[i];
        if (i == first)
            next.coefficient = total;
        combined.push_back(next);
    }
    const NodeId after = sum_of(arena, combined);
    if (after == kNoNode) {
        st.unbuildable = true;
        return out;
    }
    out.after = after;
    return out;
}

bool collect_like_terms(Context &ctx, NodeId *expression, const std::string &goal) {
    Arena &a = ctx.arena;
    while (!ctx.failed) {
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RewriteOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            return false;
        }
        CollectState st;
        std::vector<uint32_t> path;
        std::string what;
        const NodeId after = rewrite_once(a, *expression, collect_here, &st,
                                          Descend::OutermostFirst, &path, &what);
        if (st.overflowed) {
            refuse(ctx, RewriteOutcome::ResourceExceeded,
                   "the coefficients grew past what exact integer arithmetic here can hold");
            return false;
        }
        if (st.unbuildable) {
            refuse(ctx, RewriteOutcome::ResourceExceeded,
                   "a term grew past what exact integer arithmetic here can hold");
            return false;
        }
        if (after == kNoNode)
            return true;
        const bool numbers = st.numbers;

        Step s = envelope(goal, numbers ? "alg.fold-arithmetic" : "alg.collect-like-terms",
                          numbers ? "Work out the arithmetic" : "Collect like terms",
                          numbers ? "Numbers standing together are worked out first"
                                  : "Terms differing only by their coefficient add into one");
        s.explanation_detailed =
            numbers ? "Two numbers in the same sum are added, and the rest of the sum is untouched."
                    : "Two terms with the same part beside the coefficient are one term whose "
                      "coefficient is the sum of theirs, because that is the distributive law read "
                      "backwards.";
        s.verifications.push_back(passed(
            numbers ? "exact rational arithmetic" : "rule-local invariant",
            EvidenceStrength::StructurallyValid,
            numbers ? what
                    : "both terms carry the same non-numeric part, so the distributive law "
                      "gathers them"));
        s.proof_obligations.push_back(
            numbers ? ProofObligation{"obl.alg.fold-preserves-value",
                                      "folding the constants leaves the expression's value alone"}
                    : ProofObligation{"obl.alg.rule-preserves-value",
                                      "the rewritten subexpression has the value the original had"});
        if (!record(ctx, std::move(s), *expression, after, what, path))
            return false;
        *expression = after;
    }
    return !ctx.failed;
}

// A term whose coefficient is zero, removed from its sum.
Local drop_zero_here(Arena &arena, NodeId id, void *) {
    Local out;
    if (arena.at(id).kind != Kind::Add)
        return out;
    std::vector<Term> terms;
    if (!terms_of(arena, id, &terms))
        return out;

    size_t at = terms.size();
    for (size_t i = 0; i < terms.size() && at == terms.size(); ++i) {
        if (terms[i].coefficient.num == 0)
            at = i;
    }
    if (at == terms.size())
        return out;

    const std::string what =
        "Drop " + print(arena, term_node(arena, terms[at])) + ", which adds nothing";
    std::vector<Term> kept;
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i != at)
            kept.push_back(terms[i]);
    }
    const NodeId after = sum_of(arena, kept);
    if (after == kNoNode || after == id)
        return out;
    out.what = what;
    out.after = after;
    return out;
}

bool drop_zero_terms(Context &ctx, NodeId *expression, const std::string &goal) {
    Arena &a = ctx.arena;
    while (!ctx.failed) {
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RewriteOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            return false;
        }
        std::vector<uint32_t> path;
        std::string what;
        const NodeId after = rewrite_once(a, *expression, drop_zero_here, 0,
                                          Descend::OutermostFirst, &path, &what);
        if (after == kNoNode)
            return true;

        Step s = envelope(goal, "alg.drop-zero-term", "A term of zero adds nothing",
                          "Zero added to a sum leaves the sum where it was");
        s.explanation_detailed =
            "A term whose coefficient is zero contributes nothing to the sum, so removing it "
            "changes no value. It is removed before anything is factored, because a bracket built "
            "around a term of zero is true and tells a reader nothing.";
        s.verifications.push_back(passed(
            "rule-local invariant", EvidenceStrength::StructurallyValid,
            "the dropped term has coefficient zero and no other term moved"));
        s.proof_obligations.push_back({"obl.alg.rule-preserves-value",
                                       "the rewritten subexpression has the value the original had"});
        if (!record(ctx, std::move(s), *expression, after, what, path))
            return false;
        *expression = after;
    }
    return !ctx.failed;
}

// A repeated factor written as the power it is, one term to a step, so a sum reads as x^2 rather
// than x times x before its like terms are gathered.
// A repeated factor written as the power it is, one term to a step. Not gated on a sum, because a
// bare product like x*x is one term and still wants gathering.
Local gather_powers_here(Arena &arena, NodeId id, void *gather_only) {
    // The key sorts and pulls signs out as well as gathering, and simplify wants all three. A
    // caller after powers alone passes a true flag, or a bare reorder records as a gathering.
    const bool merged_only = gather_only && *static_cast<const bool *>(gather_only);
    Local out;
    std::vector<Term> terms;
    if (!terms_of(arena, id, &terms))
        return out;

    size_t at = terms.size();
    NodeId key = kNoNode;
    for (size_t i = 0; i < terms.size() && at == terms.size(); ++i) {
        bool combined = false;
        const NodeId gathered = monomial_key(arena, terms[i].rest, &combined);
        if (gathered == kNoNode || gathered == terms[i].rest || (merged_only && !combined))
            continue;
        at = i;
        key = gathered;
    }
    if (at == terms.size())
        return out;

    const std::string what = "Write " + print(arena, terms[at].rest) + " as " + print(arena, key);
    terms[at].rest = key;
    const NodeId after = sum_of(arena, terms);
    if (after == kNoNode || after == id)
        return out;
    out.what = what;
    out.after = after;
    return out;
}

bool gather_powers(Context &ctx, NodeId *expression, const std::string &goal) {
    Arena &a = ctx.arena;
    while (!ctx.failed) {
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RewriteOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            return false;
        }
        std::vector<uint32_t> path;
        std::string what;
        const NodeId after = rewrite_once(a, *expression, gather_powers_here, 0,
                                          Descend::OutermostFirst, &path, &what);
        if (after == kNoNode)
            return true;

        Step s = envelope(goal, "alg.gather-powers", "Repeated factors are a power",
                          "The same factor multiplied several times is that factor to a power");
        s.explanation_detailed =
            "Counting how many times a factor appears in the term and writing it as an exponent "
            "changes nothing about the value, and it is what lets like terms be seen as alike.";
        s.verifications.push_back(passed(
            "rule-local invariant", EvidenceStrength::StructurallyValid,
            "the exponents count the same factors the product had"));
        s.proof_obligations.push_back({"obl.alg.rule-preserves-value",
                                       "the rewritten subexpression has the value the original had"});
        if (!record(ctx, std::move(s), *expression, after, what, path))
            return false;
        *expression = after;
    }
    return !ctx.failed;
}

// One distribution of a product over a sum, or one power of a sum written as the product it stands
// for. kNoNode means there is nothing left to expand.
NodeId distribute_once(Arena &arena, NodeId id, std::vector<uint32_t> *path, std::string *what,
                       const char **rule_id, const char **rule_name) {
    const Node &n = arena.at(id);
    if (n.kind == Kind::Mul) {
        const ChildView factors = arena.children(n);
        for (size_t i = 0; i < factors.size(); ++i) {
            if (arena.at(factors[i]).kind != Kind::Add)
                continue;
            std::vector<NodeId> others;
            for (size_t j = 0; j < factors.size(); ++j) {
                if (j != i)
                    others.push_back(factors[j]);
            }
            const NodeId rest = product_of(arena, others);
            if (rest == kNoNode)
                continue;
            std::vector<NodeId> distributed;
            for (NodeId term : arena.children(factors[i]))
                distributed.push_back(arena.binary(Kind::Mul, term, rest));
            *what = "Multiply every term of " + print(arena, factors[i]) + " by " +
                    print(arena, rest);
            *rule_id = "alg.distribute";
            *rule_name = "Distributive law";
            return arena.nary(Kind::Add, distributed);
        }
    }
    if (n.kind == Kind::Pow) {
        const NodeId base = arena.children(n)[0];
        int64_t exponent = 0;
        if (arena.at(base).kind == Kind::Add && small_integer(arena, arena.children(n)[1], &exponent) &&
            exponent >= 2 && exponent <= 6) {
            std::vector<NodeId> copies;
            for (int64_t i = 0; i < exponent; ++i)
                copies.push_back(base);
            *what = "Write " + print(arena, id) + " as " + integer_text(exponent) +
                    " copies of " + print(arena, base) + " multiplied together";
            *rule_id = "alg.power-as-product";
            *rule_name = "A whole power is repeated multiplication";
            return arena.nary(Kind::Mul, copies);
        }
    }

    const ChildView kids = arena.children(n);
    for (size_t i = 0; i < kids.size(); ++i) {
        path->push_back(static_cast<uint32_t>(i));
        const NodeId rewritten = distribute_once(arena, kids[i], path, what, rule_id, rule_name);
        if (rewritten != kNoNode) {
            std::vector<NodeId> rebuilt;
            for (size_t j = 0; j < kids.size(); ++j)
                rebuilt.push_back(j == i ? rewritten : kids[j]);
            if (n.kind == Kind::Call)
                return arena.call(arena.text(id), rebuilt);
            return arena.nary(n.kind, rebuilt);
        }
        path->pop_back();
    }
    return kNoNode;
}

bool expand_products(Context &ctx, NodeId *expression, const std::string &goal) {
    Arena &a = ctx.arena;
    while (!ctx.failed) {
        if (!ctx.meter.rewrite()) {
            ctx.failed = true;
            ctx.outcome = RewriteOutcome::Refused;
            ctx.detail = halt_name(ctx.meter.halt());
            return false;
        }
        std::vector<uint32_t> path;
        std::string what;
        const char *rule_id = "";
        const char *rule_name = "";
        const NodeId next = distribute_once(a, *expression, &path, &what, &rule_id, &rule_name);
        if (next == kNoNode)
            return true;

        Step s = envelope(goal, rule_id, rule_name,
                          std::string(rule_id) == "alg.distribute"
                              ? "Every term inside the bracket is multiplied by what is outside"
                              : "A whole-number power is the base multiplied by itself that often");
        s.explanation_detailed =
            std::string(rule_id) == "alg.distribute"
                ? "A product with a sum in it is the sum of the products, term by term. Nothing is "
                  "combined yet, so each product is still visible on its own."
                : "The power is written out as a product first, so the distributive law then has "
                  "something ordinary to work on.";
        s.verifications.push_back(passed("rule-local invariant", EvidenceStrength::StructurallyValid,
                                         std::string(rule_id) == "alg.distribute"
                                             ? "the distributive law was applied to one product"
                                             : "the exponent is a whole number counting the copies"));
        s.proof_obligations.push_back({"obl.alg.rule-preserves-value",
                                       "the rewritten subexpression has the value the original had"});
        if (!record(ctx, std::move(s), *expression, next, what, path))
            return false;
        *expression = next;
    }
    return false;
}

// The expanded form, worked out without recording anything, for the factoring rule to check its own
// answer against. Bounded by the same meter, so a check cannot outrun the budget the solve has.
NodeId expanded_silently(Arena &arena, Meter &meter, NodeId id) {
    NodeId current = id;
    for (;;) {
        if (!meter.rewrite())
            return kNoNode;
        std::vector<uint32_t> path;
        std::string what;
        const char *rule_id = "";
        const char *rule_name = "";
        const NodeId next = distribute_once(arena, current, &path, &what, &rule_id, &rule_name);
        if (next == kNoNode)
            return current;
        current = next;
    }
}

// Two expressions as term maps, compared. Used to check a factorisation by multiplying it out,
// which is a proof rather than a sample, because both sides are polynomials in the same terms.
bool same_terms(Arena &arena, NodeId left, NodeId right) {
    std::vector<Term> a;
    std::vector<Term> b;
    if (left == kNoNode || right == kNoNode || arena.failed())
        return false;
    if (!terms_of(arena, left, &a) || !terms_of(arena, right, &b))
        return false;

    std::vector<NodeId> keys;
    std::vector<Rational> totals;
    for (int side = 0; side < 2; ++side) {
        const std::vector<Term> &terms = side == 0 ? a : b;
        for (const Term &term : terms) {
            const NodeId key = monomial_key(arena, term.rest);
            Rational signed_coefficient = term.coefficient;
            if (side == 1 &&
                !negate_fraction(term.coefficient.num, term.coefficient.den,
                                 &signed_coefficient.num, &signed_coefficient.den))
                return false;
            size_t at = keys.size();
            for (size_t i = 0; i < keys.size(); ++i) {
                if (keys[i] == key) {
                    at = i;
                    break;
                }
            }
            if (at == keys.size()) {
                keys.push_back(key);
                totals.push_back(Rational{0, 1});
            }
            if (!rational_add(totals[at], signed_coefficient, &totals[at]))
                return false;
        }
    }
    for (const Rational &total : totals) {
        if (total.num != 0)
            return false;
    }
    return true;
}

int64_t whole_gcd(int64_t a, int64_t b) {
    return static_cast<int64_t>(std::gcd(magnitude(a), magnitude(b)));
}

// Found and None are the two answers the search exists to give. OutOfRoom is the third thing that
// can happen to it, the product being past the ceiling this build searches to, and it is separate
// because a search that never ran is not a pair that is absent.
enum class FactorPair : uint8_t { Found, OutOfRoom, None };

// p and q with p + q = sum and p * q = product, searched over the divisors of the product. This is
// the pair a course looks for by hand, and finding it is what makes the factorisation a rule rather
// than a backend answer copied out.
FactorPair product_and_sum(int64_t sum, int64_t product, int64_t *p, int64_t *q) {
    if (magnitude(product) > static_cast<uint64_t>(kFactorSearchLimit))
        return FactorPair::OutOfRoom;
    if (product == 0) {
        *p = 0;
        *q = sum;
        return FactorPair::Found;
    }
    const int64_t limit = magnitude(product) > 0 ? static_cast<int64_t>(magnitude(product)) : 0;
    for (int64_t divisor = 1; divisor <= limit / divisor; ++divisor) {
        if (limit % divisor != 0)
            continue;
        const int64_t other = limit / divisor;
        const int64_t candidates[4][2] = {{divisor, other},
                                          {-divisor, -other},
                                          {divisor, -other},
                                          {-divisor, other}};
        for (const auto &pair : candidates) {
            int64_t their_product = 0;
            int64_t their_sum = 0;
            if (!mul_checked(pair[0], pair[1], &their_product) ||
                !add_checked(pair[0], pair[1], &their_sum))
                continue;
            if (their_product != product || their_sum != sum)
                continue;
            *p = pair[0];
            *q = pair[1];
            return FactorPair::Found;
        }
    }
    return FactorPair::None;
}

bool take_out_common_factor(Context &ctx, NodeId *expression, const std::string &goal,
                            bool *changed) {
    Arena &a = ctx.arena;
    *changed = false;
    if (a.at(*expression).kind != Kind::Add)
        return true;

    std::vector<Term> terms;
    if (!terms_of(a, *expression, &terms)) {
        refuse(ctx, RewriteOutcome::UnsupportedForm, "a term could not be read as a coefficient "
                                                     "and a rest");
        return false;
    }
    for (const Term &term : terms) {
        if (term.coefficient.den != 1) {
            refuse(ctx, RewriteOutcome::UnsupportedForm,
                   "this rule takes out a factor over whole-number coefficients, and " +
                       number_text(a, term.coefficient) + " is not one");
            return false;
        }
    }

    int64_t common = 0;
    for (const Term &term : terms)
        common = whole_gcd(common, term.coefficient.num);
    if (common == 0)
        return true;

    std::vector<Factor> shared;
    factors_of(a, terms[0].rest, &shared);
    for (size_t i = 1; i < terms.size() && !shared.empty(); ++i) {
        std::vector<Factor> theirs;
        factors_of(a, terms[i].rest, &theirs);
        std::vector<Factor> kept;
        for (const Factor &mine : shared) {
            for (const Factor &other : theirs) {
                if (other.base != mine.base)
                    continue;
                Factor f;
                f.base = mine.base;
                f.exponent = mine.exponent < other.exponent ? mine.exponent : other.exponent;
                kept.push_back(f);
                break;
            }
        }
        shared = kept;
    }

    if (common == 1 && shared.empty())
        return true;

    std::vector<NodeId> common_factors;
    if (common != 1)
        common_factors.push_back(a.integer(integer_text(common)));
    for (const Factor &f : shared)
        common_factors.push_back(power_node(a, f.base, f.exponent));
    const NodeId common_node = product_of(a, common_factors);
    if (common_node == kNoNode)
        return true;

    std::vector<Term> reduced;
    for (const Term &term : terms) {
        Term next;
        Rational divisor{common, 1};
        if (!rational_div(term.coefficient, divisor, &next.coefficient)) {
            refuse(ctx, RewriteOutcome::UnsupportedForm, "the common factor did not divide out");
            return false;
        }
        std::vector<Factor> mine;
        factors_of(a, term.rest, &mine);
        std::vector<NodeId> kept;
        for (const Factor &f : mine) {
            int64_t exponent = f.exponent;
            for (const Factor &out : shared) {
                if (out.base == f.base)
                    exponent -= out.exponent;
            }
            if (exponent > 0)
                kept.push_back(power_node(a, f.base, exponent));
        }
        next.rest = product_of(a, kept);
        reduced.push_back(next);
    }

    const NodeId inner = sum_of(a, reduced);
    const NodeId after = a.binary(Kind::Mul, common_node, inner);
    if (after == kNoNode || !same_terms(a, expanded_silently(a, ctx.meter, after),
                                        expanded_silently(a, ctx.meter, *expression))) {
        refuse(ctx, RewriteOutcome::VerificationFailed,
               "the common factor did not multiply back out to the expression it came from");
        return false;
    }

    Step s = envelope(goal, "alg.factor.common-factor", "Take out the common factor",
                      "A factor shared by every term goes outside the bracket");
    s.explanation_detailed =
        "Every term is divided by what they share and the shared part is written in front, which "
        "is the distributive law read backwards. Multiplying out again gives the terms back.";
    s.verifications.push_back(passed(
        "multiply the factors out and compare term by term",
        EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
        "the factored form expands to the same terms with the same coefficients"));
    s.proof_obligations.push_back({"obl.alg.factor-multiplies-back",
                                   "the factored form multiplies back out to the original"});
    if (!record(ctx, std::move(s), *expression, after,
                "Take out " + print(a, common_node), std::vector<uint32_t>()))
        return false;
    *expression = after;
    *changed = true;
    return true;
}

// x^2 + b x + c as two brackets, when the pair exists over the whole numbers.
bool factor_monic_quadratic(Context &ctx, NodeId *expression, const std::string &goal,
                            bool *changed) {
    Arena &a = ctx.arena;
    *changed = false;

    // The quadratic may be sitting inside the bracket a common factor left behind.
    NodeId head = kNoNode;
    NodeId target = *expression;
    if (a.at(target).kind == Kind::Mul && a.children(target).size() == 2 &&
        a.at(a.children(target)[1]).kind == Kind::Add) {
        head = a.children(target)[0];
        target = a.children(target)[1];
    }
    if (a.at(target).kind != Kind::Add)
        return true;

    std::vector<Term> terms;
    if (!terms_of(a, target, &terms))
        return true;

    NodeId variable = kNoNode;
    int64_t square = 0;
    int64_t linear = 0;
    int64_t constant = 0;
    bool have_square = false;
    for (const Term &term : terms) {
        if (term.coefficient.den != 1)
            return true;
        if (term.rest == kNoNode) {
            constant = term.coefficient.num;
            continue;
        }
        std::vector<Factor> factors;
        factors_of(a, term.rest, &factors);
        if (factors.size() != 1 || a.at(factors[0].base).kind != Kind::Symbol)
            return true;
        if (variable == kNoNode)
            variable = factors[0].base;
        if (factors[0].base != variable)
            return true;
        if (factors[0].exponent == 2) {
            square = term.coefficient.num;
            have_square = true;
        } else if (factors[0].exponent == 1) {
            linear = term.coefficient.num;
        } else {
            return true;
        }
    }
    if (!have_square || square != 1 || variable == kNoNode)
        return true;

    int64_t p = 0;
    int64_t q = 0;
    switch (product_and_sum(linear, constant, &p, &q)) {
        case FactorPair::Found: break;
        case FactorPair::None: return true;
        case FactorPair::OutOfRoom:
            refuse(ctx, RewriteOutcome::ResourceExceeded,
                   "the constant term is past the size this rule searches for a pair to, so whether "
                   "one exists was never decided");
            return false;
    }

    const NodeId first = a.binary(Kind::Add, variable, a.integer(integer_text(p)));
    const NodeId second = a.binary(Kind::Add, variable, a.integer(integer_text(q)));
    NodeId after = a.binary(Kind::Mul, first, second);
    if (head != kNoNode)
        after = a.binary(Kind::Mul, head, after);
    if (after == kNoNode || !same_terms(a, expanded_silently(a, ctx.meter, after),
                                        expanded_silently(a, ctx.meter, *expression))) {
        refuse(ctx, RewriteOutcome::VerificationFailed,
               "the two brackets did not multiply back out to the quadratic they came from");
        return false;
    }

    int64_t root = 0;
    const bool squares = linear == 0 && constant < 0 && exact_isqrt(-constant, &root);
    Step s = squares
                 ? envelope(goal, "alg.factor.difference-of-squares", "Difference of two squares",
                            "A square minus a square is the sum times the difference")
                 : envelope(goal, "alg.factor.product-and-sum", "Product and sum",
                            "Two numbers that multiply to the constant and add to the middle "
                            "coefficient give the brackets");
    s.explanation_detailed =
        squares ? "The expression is one square subtracted from another, and that always factors "
                  "into the sum of the roots times their difference."
                : "Multiplying the brackets out gives the middle coefficient as the sum of the two "
                  "numbers and the constant as their product, so finding that pair is the whole "
                  "method.";
    s.verifications.push_back(
        passed("multiply the brackets out and compare term by term",
               EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
               "the brackets expand to the same terms with the same coefficients"));
    s.proof_obligations.push_back({"obl.alg.factor-multiplies-back",
                                   "the factored form multiplies back out to the original"});
    const std::string action =
        squares ? "Write it as the sum times the difference of " + print(a, variable) + " and " +
                      integer_text(root)
                : "Use " + integer_text(p) + " and " + integer_text(q) + ", which multiply to " +
                      integer_text(constant) + " and add to " + integer_text(linear);
    if (!record(ctx, std::move(s), *expression, after, action, std::vector<uint32_t>()))
        return false;
    *expression = after;
    *changed = true;
    return true;
}

VerificationRecord backend_opinion(Arena &arena, Backend &giac, Meter &meter, NodeId difference,
                                   bool *disagreed) {
    VerificationRecord v;
    v.method = "Giac Adapter Op::IsZero on the difference";
    *disagreed = false;
    if (!meter.backend_call()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = halt_name(meter.halt());
        return v;
    }
    Adapter adapter(arena, giac);
    Request request;
    request.op = Op::IsZero;
    request.target = difference;
    const Response response = adapter.run(request);
    if (!response.usable() || response.value == kNoNode || response.value >= arena.node_count()) {
        v.outcome = VerificationOutcome::Inconclusive;
        v.detail = std::string("Giac returned ") + tag_name(response.tag) +
                   (response.detail.empty() ? "" : ": " + response.detail);
        return v;
    }
    const Node &answer = arena.at(response.value);
    if (answer.kind == Kind::Integer && answer.small_valid && answer.small == 0) {
        v.outcome = VerificationOutcome::Passed;
        v.strength = EvidenceStrength::SymbolicallyEquivalentUnderAssumptions;
        v.detail = "Giac reduced the difference to zero";
        return v;
    }
    v.outcome = VerificationOutcome::Failed;
    v.strength = EvidenceStrength::Failed;
    v.detail = "Giac reduced the difference to " + print(arena, response.value) + " rather than zero";
    *disagreed = true;
    return v;
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model, RewriteGoal goal,
                    DerivationStatus status) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "algebra.polynomial-rewrite.single-expression";
    inputs.requested_method = rewrite_goal_name(goal);
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

bool holds_decimal(const Arena &arena, NodeId id) {
    if (arena.at(id).kind == Kind::Decimal)
        return true;
    for (NodeId child : arena.children(arena.at(id))) {
        if (holds_decimal(arena, child))
            return true;
    }
    return false;
}

const char *plan_strategy(RewriteGoal goal) {
    switch (goal) {
        case RewriteGoal::Simplify: return "alg.simplify.fold-and-collect";
        case RewriteGoal::Expand: return "alg.expand.distribute-and-collect";
        case RewriteGoal::Factor: return "alg.factor.common-then-quadratic";
    }
    return "alg.simplify.fold-and-collect";
}

const char *plan_name(RewriteGoal goal) {
    switch (goal) {
        case RewriteGoal::Simplify: return "Work out the arithmetic, then gather like terms";
        case RewriteGoal::Expand: return "Multiply the brackets out, then gather like terms";
        case RewriteGoal::Factor: return "Take out what the terms share, then look for a pair";
    }
    return "Work out the arithmetic, then gather like terms";
}

}  // namespace

bool gather_repeated_factors(Arena &arena, Derivation &derivation, StepId parent, const char *phase,
                             Meter &meter, NodeId expression, NodeId *out) {
    *out = expression;
    if (expression == kNoNode || arena.failed())
        return true;

    // Probed before charged, so an expression with nothing to gather costs nothing.
    bool gather_only = true;
    NodeId current = expression;
    for (;;) {
        std::vector<uint32_t> path;
        std::string what;
        const NodeId after = rewrite_once(arena, current, gather_powers_here, &gather_only,
                                          Descend::OutermostFirst, &path, &what);
        if (after == kNoNode)
            break;
        if (!meter.rewrite())
            return false;
        current = after;
    }
    if (current == expression)
        return true;
    if (!meter.step())
        return false;

    Step s = envelope("Write the repeated factors as powers", "alg.gather-powers",
                      "Repeated factors are a power",
                      "The same factor multiplied several times is that factor to a power");
    s.phase = phase;
    s.explanation_detailed =
        "Counting how many times a factor appears and writing it as an exponent changes nothing "
        "about the value. It is what lets a rule that matches a power match a product that is one.";
    s.verifications.push_back(passed("rule-local invariant", EvidenceStrength::StructurallyValid,
                                     "the exponents count the same factors the product had"));
    s.proof_obligations.push_back({"obl.alg.rule-preserves-value",
                                   "the rewritten subexpression has the value the original had"});

    TransformationPayload payload;
    payload.before = expression;
    payload.after = current;
    payload.concrete_action = "Write each repeated factor as a power";
    payload.reversible = true;
    derivation.add_transformation(parent, std::move(s), std::move(payload));

    *out = current;
    return true;
}

const char *rewrite_goal_name(RewriteGoal g) {
    switch (g) {
        case RewriteGoal::Simplify: return "simplify";
        case RewriteGoal::Expand: return "expand";
        case RewriteGoal::Factor: return "factor";
    }
    return "unknown";
}

const char *rewrite_outcome_name(RewriteOutcome o) {
    switch (o) {
        case RewriteOutcome::Rewritten: return "rewritten";
        case RewriteOutcome::AlreadyInForm: return "already in that form";
        case RewriteOutcome::UnsupportedForm: return "unsupported form";
        case RewriteOutcome::VerificationFailed: return "verification failed";
        case RewriteOutcome::Refused: return "refused";
        case RewriteOutcome::Cancelled: return "cancelled";
        case RewriteOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

RewriteResult rewrite(Arena &arena, Derivation &derivation, NodeId expression, RewriteGoal goal,
                      const Budget &budget, Backend *giac) {
    RewriteResult result;
    if (expression == kNoNode || arena.failed()) {
        result.detail = "nothing to rewrite";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }
    if (contains_list(arena, expression)) {
        result.outcome = RewriteOutcome::UnsupportedForm;
        result.detail = "list and matrix rewriting is not supported";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }
    if (arena.at(expression).kind == Kind::Equals) {
        result.outcome = RewriteOutcome::UnsupportedForm;
        result.detail = "this rule rewrites an expression, and an equation has two of them";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }
    if (divides_by_zero(arena, expression)) {
        result.outcome = RewriteOutcome::Refused;
        result.detail = "the expression divides by zero, which has no value to rewrite";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }
    // None of the six rules below records a domain restriction, and that is correct only because none
    // of them cancels: x/x, ln(x)/ln(x) and (x^2-1)/(x-1) all come back untouched. A cancelling rule
    // added here introduces one and has to record it, or #17 is silently false from that day.
    if (has_unmeetable_condition(arena, expression)) {
        result.outcome = RewriteOutcome::Refused;
        result.detail = "the expression is undefined here, so there is nothing to rewrite";
        result.status = DerivationStatus::InvalidInput;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }
    if (holds_decimal(arena, expression)) {
        result.outcome = RewriteOutcome::UnsupportedForm;
        result.detail =
            "this rule works on exact numbers, and a decimal would have to become a fraction or "
            "stay a decimal, which is a choice it does not make";
        result.status = DerivationStatus::Unsupported;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    Meter meter(budget);
    const size_t mark = derivation.mark();

    PlanPayload plan;
    plan.strategy_id = plan_strategy(goal);
    plan.selected_strategy = plan_name(goal);
    plan.matched_problem_facts.push_back(std::string("asked to ") + rewrite_goal_name(goal) + " " +
                                         print(arena, expression));
    plan.selection_rationale =
        goal == RewriteGoal::Factor
            ? "what every term shares comes out first, because a quadratic is easier to read once "
              "its common factor is outside the bracket"
            : goal == RewriteGoal::Expand
                  ? "each bracket is multiplied out before anything is gathered, so the "
                    "distribution and the collecting stay separate steps"
                  : "the arithmetic is worked out where it stands and then terms that differ only "
                    "by their coefficient are added together";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = std::string("Rewrite ") + print(arena, expression);
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = plan_name(goal);
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short = plan_name(goal);
    register_strategy_precondition(plan, plan_step, "pre.rewrite.exact-numbers",
                                   "every number in the expression is exact", "literal inspection",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::Passed,
                                   "no decimal literal appears in the expression");
    register_strategy_precondition(plan, plan_step, "pre.rewrite.registered-rules",
                                   "every move comes from a registered rewriting rule",
                                   "registered rewriting rule dispatch",
                                   EvidenceStrength::StructurallyValid,
                                   VerificationOutcome::NotAttempted,
                                   "checked while rewriting the expression");
    if (!meter.step()) {
        derivation.rewind_to(mark);
        const bool cancelled = meter.halt() == Halt::Cancelled;
        result.outcome = cancelled ? RewriteOutcome::Cancelled : RewriteOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = cancelled ? DerivationStatus::NotRecorded
                                  : DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    Context ctx(arena, derivation, meter);
    ctx.plan = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const std::string goal_text = std::string(rewrite_goal_name(goal)) + " the expression";
    NodeId current = expression;
    meter.reached(expression);

    if (goal == RewriteGoal::Expand)
        expand_products(ctx, &current, goal_text);
    if (!ctx.failed)
        fold_arithmetic(ctx, &current, goal_text);
    if (!ctx.failed)
        drop_zero_terms(ctx, &current, goal_text);
    if (!ctx.failed)
        gather_powers(ctx, &current, goal_text);
    if (!ctx.failed)
        collect_like_terms(ctx, &current, goal_text);
    if (!ctx.failed && goal == RewriteGoal::Factor) {
        bool changed = false;
        if (take_out_common_factor(ctx, &current, goal_text, &changed) && !ctx.failed)
            factor_monic_quadratic(ctx, &current, goal_text, &changed);
    }

    if (meter.stopped()) {
        // STEP-025: a halted rewrite keeps the moves that were checked and drops the rest. The
        // precondition is settled first, or the plan reads as unchecked and takes the prefix with
        // it. Scoped to what was reached, because the moves never made prove nothing.
        derivation.complete_plan_precondition(ctx.plan, "pre.rewrite.registered-rules",
                                              VerificationOutcome::Passed,
                                              "every move made before the stop came from a "
                                              "registered rewriting rule");
        const bool cancelled = meter.halt() == Halt::Cancelled;
        const bool kept = keep_verified_prefix(derivation, mark, arena);
        result.outcome = cancelled ? RewriteOutcome::Cancelled : RewriteOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                        : kept     ? DerivationStatus::Cancelled
                                   : DerivationStatus::NotRecorded;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    if (ctx.failed || arena.failed()) {
        derivation.rewind_to(mark);
        result.outcome = ctx.outcome;
        result.detail = ctx.detail.empty() ? "the expression holds a form with no rewriting rule"
                                           : ctx.detail;
        result.status =
            result.outcome == RewriteOutcome::VerificationFailed ? DerivationStatus::VerificationFailed
            : result.outcome == RewriteOutcome::ResourceExceeded
                ? DerivationStatus::ResourceLimitReached
                : DerivationStatus::Unsupported;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    // The guard at entry cannot catch this one. At entry the denominator is unexpanded and not
    // recognisably zero, and it only becomes zero once the rules have run, which is why
    // 1/((x+1)*(x-1) - x^2 + 1) used to come back as a successful rewrite of an undefined
    // expression. Asked again on the result rather than taught to the rules that produce it, so a
    // rule added later cannot manufacture the same thing and be believed.
    //
    // The records are dropped rather than kept. Every one of them is a correct rewrite, so section
    // 15's closing paragraph has a claim on them, but criterion 8 refuses a refusal that keeps
    // transformations unless it halted. Preserving a prefix here belongs with #32, which is about
    // that contract; deferring it is a decision rather than an oversight.
    if (divides_by_zero(arena, current)) {
        derivation.rewind_to(mark);
        result.outcome = RewriteOutcome::Refused;
        result.detail = "rewriting reaches a division by zero, so the expression has no value";
        result.status = DerivationStatus::InvalidInput;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    derivation.complete_plan_precondition(ctx.plan, "pre.rewrite.registered-rules",
                                          VerificationOutcome::Passed,
                                          "every move came from a registered rewriting rule");

    if (current == expression) {
        result.outcome = RewriteOutcome::AlreadyInForm;
        result.expression = expression;
        result.detail = goal == RewriteGoal::Factor
                            ? "no factor is shared by every term and no whole-number pair "
                              "multiplies and adds to the coefficients, so this rule leaves it as "
                              "it is"
                            : "there is nothing left to work out or gather";
        result.status = DerivationStatus::SolvedAndVerified;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    const SampleAgreement agreement = agrees_on_samples(arena, expression, current, kSamples);
    bool backend_disagreed = false;
    VerificationRecord backend;
    const bool asked_backend = giac != nullptr;
    if (asked_backend) {
        const NodeId difference =
            arena.binary(Kind::Add, expression, arena.unary(Kind::Neg, current));
        backend = backend_opinion(arena, *giac, meter, difference, &backend_disagreed);
    }

    if (!meter.step()) {
        derivation.rewind_to(mark);
        const bool cancelled = meter.halt() == Halt::Cancelled;
        result.outcome = cancelled ? RewriteOutcome::Cancelled : RewriteOutcome::ResourceExceeded;
        result.detail = halt_name(meter.halt());
        result.status = cancelled ? DerivationStatus::NotRecorded
                                  : DerivationStatus::ResourceLimitReached;
        result.cost = meter.cost();
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    Step check;
    check.phase = "check";
    check.goal = "Check the rewritten expression";
    check.rule_id = "alg.rewrite.check-by-evaluation";
    check.rule_name = "Check by working both forms out";
    check.claim = ClaimType::EquivalentExpression;
    check.explanation_short = "Work the first form and the last one out at the same values";
    check.explanation_detailed =
        "Every rule used here keeps the value of the expression, so the two forms must agree "
        "wherever they are both defined. Working both out at several exact values can show that "
        "they do not, and a backend is asked for the symbolic answer when one is supplied.";
    check.proof_obligations.push_back(
        {"obl.rewrite.same-value", "the rewritten expression has the value the original had"});

    VerificationRecord sampled;
    sampled.method = "exact evaluation at " + std::to_string(kSamples) + " rational assignments";
    if (agreement.evaluated == 0) {
        sampled.outcome = VerificationOutcome::Inconclusive;
        sampled.detail = "no assignment gave both forms a value, so nothing was compared";
    } else if (agreement.agreed == agreement.evaluated) {
        sampled.outcome = VerificationOutcome::Passed;
        sampled.detail = "both forms took the same value at all " +
                         std::to_string(agreement.evaluated) +
                         " assignments that could be worked out, which is evidence of the identity "
                         "rather than a proof of it";
    } else {
        sampled.outcome = VerificationOutcome::Failed;
        sampled.detail = "the two forms differed: " + agreement.disagreement;
    }
    sampled.strength = strength_for(sampled.outcome, EvidenceStrength::NumericallyCorroborated);
    check.verifications.push_back(sampled);
    if (asked_backend)
        check.verifications.push_back(backend);

    CheckPayload payload;
    payload.target_claim =
        print(arena, expression) + " and " + print(arena, current) + " are the same expression";
    payload.check_method = asked_backend
                               ? "work both forms out exactly at several assignments and ask Giac "
                                 "whether their difference is zero"
                               : "work both forms out exactly at several assignments";
    payload.expected_relation = "the two forms take the same value";
    payload.observed_result = sampled.outcome == VerificationOutcome::Failed
                                  ? "the forms differed: " + agreement.disagreement
                                  : sampled.outcome == VerificationOutcome::Inconclusive
                                        ? "no assignment could be worked out"
                                        : "the forms agreed at all " +
                                              std::to_string(agreement.evaluated) + " assignments";
    if (asked_backend)
        payload.observed_result += ", and " + backend.detail;
    derivation.add_check(ctx.plan, std::move(check), std::move(payload));

    result.cost = meter.cost();
    if (sampled.outcome == VerificationOutcome::Failed || backend_disagreed) {
        // The record stays so the failing check can be read, and the rewritten form is withheld.
        result.outcome = RewriteOutcome::VerificationFailed;
        result.detail = sampled.outcome == VerificationOutcome::Failed
                            ? "the rewritten expression failed its own value check"
                            : "Giac and the rewritten expression disagree, so it is not offered";
        result.status = DerivationStatus::VerificationFailed;
        record_context(derivation, budget, expression, goal, result.status);
        return result;
    }

    result.outcome = RewriteOutcome::Rewritten;
    result.expression = current;
    result.status = derivation.outcome_from(mark);
    record_context(derivation, budget, expression, goal, result.status);
    return result;
}

}  // namespace nps
