#ifndef NPS_UNITS_H
#define NPS_UNITS_H

#include <cstdint>
#include <span>
#include <string>

#include "nps/core/rational.h"

namespace nps {

// PRD section 13 and 14: quantities carry dimensions, and a derivation checks them rather than
// trusting the numbers. Three base dimensions cover the kinematics vertical slice; the others
// join when a family needs them, which is the same rule as everything else here. Electric current
// joined for PHYS-012, because charge, potential and resistance are not expressible without it.
// Temperature and amount of substance joined for PHYS-018, for heat capacities and the gas constant.
struct Dimension {
    int length = 0;
    int mass = 0;
    int time = 0;
    int current = 0;
    int temperature = 0;
    int amount = 0;
};

bool operator==(const Dimension &a, const Dimension &b);
bool operator!=(const Dimension &a, const Dimension &b);

// One place that knows how many base dimensions there are, so a fifth one is a single edit here
// rather than parallel lists that drift apart. A producer that writes a dimension out loops over
// these rather than naming the fields, so a new base dimension cannot be dropped silently.
constexpr int kDimensionCount = 6;
void dimension_powers(const Dimension &d, int (&out)[kDimensionCount]);

// False leaves out unchanged when a dimension exponent would not fit.
bool dimension_multiply(const Dimension &a, const Dimension &b, Dimension *out);
bool dimension_power(const Dimension &a, int exponent, Dimension *out);

// "L T^-1", or "1" for a pure number.
std::string dimension_text(const Dimension &d);

// The SI spelling of a dimension: "m/s", "m/s^2", "kg m/s^2", "1" for a pure number.
std::string si_unit_text(const Dimension &d);

// The quantity a dimension measures, such as force, or null when no quantity here is named for it.
const char *quantity_name(const Dimension &d);

// A unit as typed, with its dimension and its scale to the SI unit of that dimension.
struct Unit {
    std::string text;
    Dimension dimension;
    Rational scale;
};

// Reads a unit spelling: a symbol from the table, optionally raised to an integer power, joined
// by * or a space for a product and / for a quotient, as in m/s^2, km/h or kg*m/s^2. The table
// is the whole vocabulary; anything else is refused with the word that was not understood.
bool parse_unit(const std::string &text, Unit *out, std::string *error);

// Decimal measurements carry both significant digits and their last decimal place.
enum class NumberKind : uint8_t {
    Exact,
    Measured,
};

// PHYS-020: an uncertainty is kept as its exact square, and every state but Known says why there is none.
enum class UncertaintyState : uint8_t {
    None,
    Known,
    Unstated,
    NotPropagated,
    TooLarge,
};

const char *uncertainty_state_name(UncertaintyState state);

struct Precision {
    NumberKind kind = NumberKind::Exact;
    uint16_t significant_digits = 0;
    int32_t last_significant_decimal_place = 0;
    UncertaintyState uncertainty = UncertaintyState::None;
    Rational variance;
};

// Whether a Precision states what it carries: an exact value states no uncertainty and no variance,
// a known one is a measurement with a variance of zero or more, and every other state carries none.
bool precision_consistent(const Precision &precision);

struct Quantity {
    Rational value;
    Unit unit;
    Precision precision;
};

// "5 m/s", "2.5 km/h", "-3 m/s^2", or a bare number for a pure number. A literal written with a
// decimal point is measured, and its significant digits are counted from the first non-zero one, so
// 0.0450 is three and 20.0 is three.
bool parse_quantity(const std::string &text, Quantity *out, std::string *error);

// Also reads "2.50 +/- 0.02 m" or the plus-minus sign, which parse_quantity refuses rather than drops.
bool parse_quantity_with_uncertainty(const std::string &text, Quantity *out, std::string *error);

// The value in the SI unit of its dimension. False when the conversion overflows.
bool to_si(const Quantity &q, Rational *value);

// The whole quantity in SI: the value by the exact scale, the figures it was written with, and the
// variance by that scale squared. False when the conversion overflows.
bool to_si(const Quantity &q, Quantity *out);

// Products and quotients use the fewest significant digits among measured operands.
Precision precision_combine(const Precision &a, const Precision &b);

// The place that agrees with a figure count already fixed, which is what a combined or copied
// precision carries when it is attached to a value it was not computed from. precision_sum is the
// other direction, deriving the count from a declared place. Both account for a rounding that
// carries into a new leading digit, so 9.96 at two figures ends in the units place and not the
// tenths its operands were written to.
Precision precision_at_digits(const Rational &value, Precision precision);

// The figure count that agrees with a place already fixed, which is precision_at_digits read the
// other way round. A place left of the value's leading digit leaves one figure rather than none.
Precision precision_at_value(const Rational &value, Precision precision);

// Each carries an uncertainty through its own partials only when value really is that operation.
Precision precision_product(const Rational &value, const Rational &a_value, const Precision &a,
                            const Rational &b_value, const Precision &b);
Precision precision_quotient(const Rational &value, const Rational &a_value, const Precision &a,
                             const Rational &b_value, const Precision &b);
Precision precision_power(const Rational &value, const Rational &base_value, const Precision &base,
                          int64_t exponent);
Precision precision_sum(const Rational &value, const Precision &a, const Precision &b);

struct UncertaintyTerm {
    Rational sensitivity;
    const Precision *precision;
};

// First order over distinct inputs: each sensitivity squared times that input's variance, summed.
// A sum of zero has no root to report, so it comes back not propagated rather than as a known zero.
// Only the uncertainty and its variance are written, so out arrives carrying the figures it keeps.
void propagate_uncertainty(std::span<const UncertaintyTerm> terms, Precision *out);

// The root rounded up to two significant figures, and the decimal place of its last one. False for a
// zero variance, which has no significant figure to round up to.
bool uncertainty_text(const Precision &precision, std::string *out, int32_t *place);

// What a reported uncertainty was found to be against the variance behind it. Named for what
// happened rather than for what a caller should do about it, the same way HalfPlace is.
enum class UncertaintyRounding : uint8_t {
    Smallest,
    TooSmall,
    NotSmallest,
    NotTwoFigures,
    Unreadable,
};

const char *uncertainty_rounding_name(UncertaintyRounding outcome);

// Reads the reported text back and judges whether it is the smallest two-significant-figure value in
// place whose square covers the exact variance. Shares no path with uncertainty_text's integer root.
UncertaintyRounding uncertainty_rounding_valid(const Precision &precision,
                                               const std::string &reported, int32_t place);

bool precision_rounded_text(const Rational &value, const Precision &precision, std::string *out);
HalfPlace precision_rounding_valid(const Rational &exact, const std::string &reported,
                                   const Precision &precision);

// PRD section 11. A vector is components in a named frame, and the frame is part of the value rather
// than something a caller is trusted to remember: section 14 rejects a design that "treats vectors as
// unframed lists" outright. Every operation below refuses a frame it was not given, because section
// 11.2's last line forbids an implicit conversion and a rotation nobody asked for is exactly that.
struct Frame {
    std::string name;
};

bool operator==(const Frame &a, const Frame &b);
bool operator!=(const Frame &a, const Frame &b);

// The frame a problem states its quantities in when it names no other. Every vector gets one, so
// there is no such thing here as a vector whose frame is unknown.
const char *default_frame_name();

// Components in one unit, which is how a textbook writes a vector and how the components stay
// addable. Rank 2 leaves z unread rather than absent, so the arithmetic has one shape.
struct Vector {
    Rational x;
    Rational y;
    Rational z;
    uint8_t rank = 2;
    Frame frame;
    Unit unit;
    Precision precision;
};

// Reads a vector in either of the two forms a person writes: "3 i + 4 j m/s" in unit-vector form,
// or "(3, 4) m/s" as an ordered tuple. A term with no number is one, so "i + 2 j" is a direction. A
// repeated axis is refused rather than added to itself. The tuple form is read and never written
// back, since which axis a number belongs to should not depend on counting commas.
bool parse_vector(const std::string &text, Vector *out, std::string *error);

// Converts every active component and its shared measured precision to SI.
bool to_si(const Vector &v, Vector *out);

// "3 i + 4 j", and "(3 i + 4 j) m/s" once a unit is on it. A zero component is dropped, a negative
// one is written "- 4 j" rather than "+ -4 j", and an all-zero vector is "0" so that something is
// printed. The unit is the vector's own spelling, so the numbers and the unit always agree.
std::string vector_text(const Vector &v);
// Final measured report, skipping exact-zero components. False is a place the exact arithmetic
// cannot reach, and checked is the half-place comparison, taken by reference so a caller cannot
// leave the rounding unjudged or receive a rounding refusal as the arithmetic one.
bool reported_vector_text(const Vector &v, std::string *out, HalfPlace &checked);

// Addition and subtraction in compatible frames, section 11.2's first operation. The two have to
// agree on the frame, the dimension and the rank. Disagreeing on any of them is an error with a
// reason rather than a coercion.
bool vector_add(const Vector &a, const Vector &b, Vector *out, std::string *error);
bool vector_sub(const Vector &a, const Vector &b, Vector *out, std::string *error);

// A scalar times a vector. The dimensions multiply and the frame is the vector's.
bool vector_scale(const Vector &v, const Quantity &s, Vector *out, std::string *error);

// The dot product, whose result is a scalar with the product of the two dimensions. Frames must
// agree: a dot product across frames is meaningless without a stated transformation.
bool vector_dot(const Vector &a, const Vector &b, Quantity *out, std::string *error);

// The cross product, which needs three components on both sides because it is only defined there.
bool vector_cross(const Vector &a, const Vector &b, Vector *out, std::string *error);

// The magnitude, when the square root is exact. Most are not, and this refuses rather than rounding
// silently, so a caller that needs a decimal has to ask for one and say so in its step.
bool vector_magnitude(const Vector &v, Quantity *out, std::string *error);

}  // namespace nps

#endif
