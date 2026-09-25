#ifndef NPS_CANONICAL_H
#define NPS_CANONICAL_H

#include "nps/core/ast.h"

namespace nps {

// A second form of the same expression, for comparison only. The parsed tree is kept as written,
// because PRD section 12.1 wants the AST lossless enough to preserve meaningful grouping and user
// intent, and a derivation that silently reorders what the user typed has already lost the thing it
// is meant to explain.
//
// Two expressions are equal under this form when their canonical ids are equal, which is one
// comparison rather than a walk, because the arena interns.
//
// Integer constants fold, a product's constant reciprocals fold with them into one fraction in
// lowest terms, x^1 is x, and that is the whole of the arithmetic it does.
//
// One boundary, and it is a limit rather than a defect. Integer constants fold from the smallest
// upwards while the running total fits in an int64, and the rest are left as they are. Folding runs
// bottom up, so a sub-product that folded before its parent overflowed cannot be taken apart again:
// 25*53*79*B and (25*53)*79*B reach different canonical forms once B is large enough to overflow.
// That is the whole cost, measured rather than assumed: the form stays idempotent and stays stable
// across a print and a reparse, and only two differently bracketed spellings of one expression can
// disagree. The failure is always a false negative, two equal expressions comparing unequal, never
// the reverse. Real equivalence is the verifier's job with Giac and its bignums behind it.
NodeId canonicalize(Arena &arena, NodeId id);

// canonicalize answers kNoNode for a limit the arena hit and for a form it has no canonical spelling
// for, and the caller cannot tell those apart from the return value alone. The arena is what knows,
// so the reading of it lives here beside the function that refused rather than in each caller.
enum class CanonicalRefusal { Unsupported, ResourceLimit };
CanonicalRefusal canonical_refusal(const Arena &arena);

// The wording each of those two answers gets, here rather than at the bridge, because an arena can
// be starved on purpose here and no input to the bridge entry point reaches either arm.
std::string canonical_refusal_message(const Arena &arena);

// Ordering over canonical nodes. Exposed because the rule engine needs the same order the canonical
// form was built with, and two orders that disagree would make equality depend on who asked.
bool canonical_less(const Arena &arena, NodeId a, NodeId b);

// Whether the expression divides by zero once its arithmetic is folded, so x/(1-1) is caught as
// well as x/0. Every engine asks this at its entry: a division by zero is not a value, and handing
// one back as a checked answer is what MVP criterion 11 exists to prevent.
bool divides_by_zero(Arena &arena, NodeId id);

// small_integer, but reading an exponent that is only arithmetically an integer: x^(1+1) and
// x^(4/2) are both x squared, and a rule that reads the exponent structurally refuses them. Folds
// first and reads the result, so the rule envelope is stated over the value rather than the
// spelling.
bool folded_integer(Arena &arena, NodeId id, int64_t *out);

// What a subexpression's form requires of it before the surrounding expression has a value: a
// denominator must not be zero, a logarithm's argument must be positive, an even root's must not be
// negative.
enum class Condition { NonZero, Positive, NonNegative };

// Whether the form settles the condition. Only Unknown is worth recording. Holds needs no saying,
// and Fails is an expression with no value rather than a condition a reader could meet.
enum class Decision { Holds, Fails, Unknown };

struct Restriction {
    NodeId subject = kNoNode;
    Condition condition = Condition::NonZero;
};

// Folds the arithmetic before reading it, so 1-1 settles a condition the same way 0 does.
Decision decide(Arena &arena, NodeId id, Condition condition);

// Whether one restriction already says everything another would, so a step does not state both.
// Positive covers non-zero over the same subject and that is the whole of the order: a quotient with
// a logarithm over it meets both and reads as one condition, while non-negative and non-zero say
// genuinely different things and are left alone.
bool subsumes(const Restriction &held, const Restriction &candidate);

// Add one to a set, dropping whatever it covers and skipping it when the set already covers it.
// Both directions, because which of a pair arrives first is an accident of the walk: a rule sees
// the reciprocal in what it consumes and the logarithm in what it produces.
void merge_restriction(std::vector<Restriction> *into, const Restriction &r);

// Every condition an expression needs that its own form does not settle, each once, in the order
// met. This is the whole of what a predicate can decide: a restriction here is a property of the
// expression as written. A modelling assumption is not, so the ones physics records by hand about
// active intervals and start times stay hand-written and correctly so.
// Nothing derived a restriction before this: assume_nonzero, the only prior one, never fired once.
std::vector<Restriction> restrictions_of(Arena &arena, NodeId id);

// True when some condition the expression needs is settled false, so no value exists to hand back.
// divides_by_zero is the division-shaped half of this question, kept separate because its refusal
// names division and every engine already asks it at entry.
bool has_unmeetable_condition(Arena &arena, NodeId id);

std::string restriction_text(const Arena &arena, const Restriction &r);

// Whether a decimal literal occurs anywhere. What an engine asks to find out whether the numeric
// mode it is running under makes any difference to this input.
bool has_decimal(const Arena &arena, NodeId id);

// Whether any exponent holds a decimal. Reading one as exact would hand an exact power rule the
// measured two in x^(1.0+1.0), so promotion asks this first and refuses rather than launders.
bool has_decimal_exponent(const Arena &arena, NodeId id);

// The same expression with every decimal literal replaced by the rational it names exactly: 0.5
// becomes (1 * (2^-1)) and 2.0 becomes 2. Nothing is approximated and nothing is rounded, because a
// decimal literal already is a fraction over a power of ten. kNoNode when a literal carries more
// digits than the rational can hold, which is a refusal rather than a silent narrowing.
NodeId exactify(Arena &arena, NodeId id);

// The other direction, for reporting an answer once the arithmetic is done. A rational whose
// denominator is only twos and fives is written back as the decimal it equals, and one that would
// not terminate is left alone: (1 * (2^-1)) becomes 0.5 and a third stays a third.
NodeId decimalize(Arena &arena, NodeId id);

// MATH-009's four categories, plus the answer for a node that is not a literal at all.
enum class Exactness : uint8_t {
    NotALiteral,
    ExactRational,
    ExactConstant,
    Measured,
    Approximate,
};

const char *exactness_name(Exactness e);

// Where one node's value came from, read from its kind and its spelling. Named in one place because
// four sites already agree on this mapping without any of them stating it.
Exactness exactness(const Arena &arena, NodeId id);

// Whether the category is one a rule may not quietly discard. True of a measurement and of an
// approximation, false of the two exact ones and of a node that holds no literal.
bool inexact(Exactness e);

}  // namespace nps

#endif
