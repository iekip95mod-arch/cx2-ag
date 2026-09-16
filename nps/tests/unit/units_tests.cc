#include <limits>
#include <string>

#include "nps/units/units.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Precision at_place(int32_t place) {
    Precision precision;
    precision.kind = NumberKind::Measured;
    precision.last_significant_decimal_place = place;
    return precision;
}

std::string unit_dimension(const std::string &text) {
    Unit u;
    std::string why;
    if (!parse_unit(text, &u, &why))
        return "refused: " + why;
    return dimension_text(u.dimension);
}

std::string unit_scale(const std::string &text) {
    Unit u;
    std::string why;
    if (!parse_unit(text, &u, &why))
        return "refused: " + why;
    return rational_text(u.scale);
}

std::string si_value(const std::string &text) {
    Quantity q;
    std::string why;
    if (!parse_quantity(text, &q, &why))
        return "refused: " + why;
    Rational v;
    if (!to_si(q, &v))
        return "overflow";
    return rational_text(v) + " " + si_unit_text(q.unit.dimension);
}

// The dimension of a spelling, for asserting that its SI spelling parses back to the same thing.
Dimension parse_dimension(const std::string &text) {
    Unit u;
    std::string why;
    if (!parse_unit(text, &u, &why))
        return Dimension();
    return u.dimension;
}

std::string number(const std::string &text) {
    Rational r;
    if (!rational_from_text(text, &r))
        return "refused";
    return rational_text(r);
}

// "exact" or the digit count, so a wrong count reads as a number rather than as a boolean.
std::string figures(const std::string &text) {
    Quantity q;
    std::string why;
    if (!parse_quantity(text, &q, &why))
        return "refused: " + why;
    if (q.precision.kind == NumberKind::Exact)
        return "exact";
    return magnitude_text(q.precision.significant_digits);
}

std::string last_place(const std::string &text) {
    Quantity q;
    std::string why;
    if (!parse_quantity(text, &q, &why))
        return "refused: " + why;
    if (q.precision.kind == NumberKind::Exact)
        return "exact";
    return std::to_string(q.precision.last_significant_decimal_place);
}

// Do a Precision's two fields describe the same number? The leading place a count implies is
// place + digits - 1, and the rounded text shows where its leading digit actually sits. Read off
// the text rather than recomputed from the value, so this cannot agree with the printer by sharing
// its arithmetic.
bool fields_agree(const std::string &text, const Precision &precision) {
    if (precision.kind != NumberKind::Measured)
        return true;
    size_t first = 0;
    while (first < text.size() && (text[first] == '-' || text[first] == '0' || text[first] == '.'))
        ++first;
    if (first == text.size())
        return true;  // A value that rounded away to zero shows no leading digit to place.
    const size_t point = text.find('.');
    // Places left of the point count up from 0, places right of it count down from -1.
    const int leading = point == std::string::npos || first < point
                            ? static_cast<int>((point == std::string::npos ? text.size() : point) -
                                               first) - 1
                            : -static_cast<int>(first - point);
    const int implied = precision.last_significant_decimal_place +
                        static_cast<int>(precision.significant_digits) - 1;
    return leading == implied;
}

std::string rounded(const std::string &text, unsigned digits) {
    Rational r;
    if (!rational_from_text(text, &r))
        return "refused";
    std::string out;
    if (!rounded_text(r, digits, &out))
        return "out of range";
    return out;
}

// A vector as a test wants to write one: components, a unit spelling and the default frame. An empty
// spelling leaves it dimensionless, where parse_unit would refuse and leave the scale at zero.
Vector vector_of(const std::string &unit_text, int64_t x, int64_t y) {
    Vector v;
    v.x.num = x;
    v.y.num = y;
    v.rank = 2;
    v.frame.name = default_frame_name();
    v.unit.scale.num = 1;
    v.unit.scale.den = 1;
    std::string why;
    if (!unit_text.empty())
        parse_unit(unit_text, &v.unit, &why);
    return v;
}

Vector vector_of(const std::string &unit_text, int64_t x, int64_t y, int64_t z) {
    Vector v = vector_of(unit_text, x, y);
    v.z.num = z;
    v.rank = 3;
    return v;
}

std::string quantity_si_text(const Quantity &q) {
    const std::string unit = si_unit_text(q.unit.dimension);
    return unit == "1" ? rational_text(q.value) : rational_text(q.value) + " " + unit;
}

std::string added(const Vector &a, const Vector &b) {
    Vector sum;
    std::string why;
    if (!vector_add(a, b, &sum, &why))
        return "refused: " + why;
    return vector_text(sum);
}

std::string subtracted(const Vector &a, const Vector &b) {
    Vector difference;
    std::string why;
    if (!vector_sub(a, b, &difference, &why))
        return "refused: " + why;
    return vector_text(difference);
}

std::string dotted(const Vector &a, const Vector &b) {
    Quantity product;
    std::string why;
    if (!vector_dot(a, b, &product, &why))
        return "refused: " + why;
    return quantity_si_text(product);
}

std::string crossed(const Vector &a, const Vector &b) {
    Vector product;
    std::string why;
    if (!vector_cross(a, b, &product, &why))
        return "refused: " + why;
    return vector_text(product);
}

std::string magnitude_of(const Vector &v) {
    Quantity m;
    std::string why;
    if (!vector_magnitude(v, &m, &why))
        return "refused: " + why;
    return quantity_si_text(m);
}

std::string scaled(const Vector &v, const std::string &scalar) {
    Quantity s;
    std::string why;
    if (!parse_quantity(scalar, &s, &why))
        return "refused: " + why;
    Vector out;
    if (!vector_scale(v, s, &out, &why))
        return "refused: " + why;
    return vector_text(out);
}

// Read a vector and print it back, so a test states one string and reads one. The tuple form is
// never echoed, which is the point of checking it this way round.
std::string reread(const std::string &text) {
    Vector v;
    std::string why;
    if (!parse_vector(text, &v, &why))
        return "refused: " + why;
    return vector_text(v);
}

std::string read_rank(const std::string &text) {
    Vector v;
    std::string why;
    if (!parse_vector(text, &v, &why))
        return "refused: " + why;
    return magnitude_text(v.rank);
}

std::string precision_text(const Precision &p) {
    return p.kind == NumberKind::Exact ? "exact" : magnitude_text(p.significant_digits);
}

Precision measured(uint16_t digits) {
    Precision p;
    p.kind = NumberKind::Measured;
    p.significant_digits = digits;
    p.last_significant_decimal_place = 1 - static_cast<int32_t>(digits);
    return p;
}

}  // namespace

void run_units_tests(TestSink &t) {
    t.equal(number("5"), "5", "an integer reads back as itself");
    t.equal(number("-3"), "-3", "with its sign");
    t.equal(number("2.50"), "2.5", "a decimal is read exactly and printed without trailing zeros");
    t.equal(number("0.125"), "0.125", "a decimal that terminates in three places");
    t.equal(number("1e3"), "refused", "an exponent is not read");
    t.equal(number("1.2.3"), "refused", "two points are not a number");
    t.equal(number(""), "refused", "nor is nothing");
    {
        Rational third;
        third.num = 1;
        third.den = 3;
        t.equal(rational_text(third), "1/3", "a non-terminating value prints as a fraction");
        Rational half;
        half.num = -9;
        half.den = 2;
        t.equal(rational_text(half), "-4.5", "a negative terminating one as a decimal");
    }
    {
        const int64_t maximum = std::numeric_limits<int64_t>::max();
        const int64_t minimum = std::numeric_limits<int64_t>::min();
        int64_t checked = 19;
        t.check(add_checked(maximum, -1, &checked) && checked == maximum - 1,
                "checked addition accepts a representable boundary result");
        checked = 19;
        t.check(!add_checked(maximum, 1, &checked) && checked == 19,
                "checked addition refuses overflow without publishing it");
        checked = 23;
        t.check(!mul_checked(minimum, -1, &checked) && checked == 23,
                "checked multiplication refuses signed overflow without publishing it");

        Rational cancelled;
        t.check(rational_mul(Rational{maximum, 2}, Rational{2, maximum}, &cancelled) &&
                    cancelled.num == 1 && cancelled.den == 1,
                "exact multiplication reduces before applying the int64 result boundary");
        t.check(rational_equal(Rational{1, 2}, Rational{2, 4}),
                "GMP equality compares exact values rather than fraction storage");
        t.check(!rational_equal(Rational{1, 0}, Rational{1, 0}),
                "invalid fractions are not equal values");
        Rational untouched{17, 19};
        t.check(!rational_add(Rational{maximum, 1}, Rational{1, 1}, &untouched) &&
                    untouched.num == 17 && untouched.den == 19,
                "a final exact result outside int64 is refused without publishing it");
        t.check(integer_power(-2, 63, &checked) && checked == minimum,
                "GMP integer power reaches the signed minimum exactly");
        checked = 23;
        t.check(!integer_power(2, 63, &checked) && checked == 23,
                "GMP integer power refuses a positive result outside int64");
        t.check(integer_power(-1, std::numeric_limits<uint64_t>::max(), &checked) && checked == -1,
                "a vast unit-magnitude power is bounded and exact");
        Rational powered;
        t.check(rational_power(Rational{-1, 2}, -3, &powered) && powered.num == -8 &&
                    powered.den == 1,
                "GMP rational power handles an exact negative exponent");
        powered = Rational{17, 19};
        t.check(!rational_power(Rational{0, 1}, -1, &powered) && powered.num == 17 &&
                    powered.den == 19,
                "a reciprocal of zero is refused without publishing it");
        int leading_place = 77;
        t.check(rational_leading_decimal_place(Rational{maximum, 1}, &leading_place) &&
                    leading_place == 18,
                "the largest rational numerator is in decimal place eighteen");
        t.check(rational_leading_decimal_place(Rational{1, maximum}, &leading_place) &&
                    leading_place == -19,
                "a reciprocal near the int64 boundary is in decimal place negative nineteen");
        leading_place = 77;
        t.check(!rational_leading_decimal_place(Rational{1, 0}, &leading_place) &&
                    leading_place == 77,
                "an invalid rational has no decimal place and leaves the output unchanged");

        int64_t numerator = 2;
        int64_t denominator = minimum;
        t.check(normalise(&numerator, &denominator) && numerator == -1 &&
                    denominator == INT64_C(4611686018427387904),
                "normalization accepts a negative minimum denominator when reduction fits");
        numerator = 1;
        denominator = minimum;
        t.check(!normalise(&numerator, &denominator) && numerator == 1 && denominator == minimum,
                "normalization refuses a canonical denominator outside int64");

        int64_t root = -1;
        t.check(exact_isqrt(INT64_C(9223372030926249001), &root) && root == 3037000499,
                "the largest int64 square is recognized exactly");
        root = -1;
        t.check(!exact_isqrt(maximum, &root) && root == -1,
                "a neighboring non-square is refused without publishing a root");
        Rational square_root;
        t.check(rational_sqrt_exact(Rational{4, 16}, &square_root) && square_root.num == 1 &&
                    square_root.den == 2,
                "an exact rational square root is canonicalized");
        t.equal(rational_text(Rational{minimum, 1}), "-9223372036854775808",
                "the minimum int64 converts to decimal without negating it in C++");
        Rational boundary{17, 19};
        t.check(rational_from_text("9223372036854775807.0", &boundary) &&
                    boundary.num == maximum && boundary.den == 1,
                "a decimal boundary is accepted after exact canonicalization");
        t.check(rational_from_text("-9223372036854775808", &boundary) &&
                    boundary.num == minimum && boundary.den == 1,
                "the signed minimum parses exactly");
        t.check(rational_from_text("-9223372036854775808.0", &boundary) &&
                    boundary.num == minimum && boundary.den == 1,
                "the signed decimal minimum canonicalizes before the fit check");
        boundary = Rational{17, 19};
        t.check(!rational_from_text("9223372036854775808.0", &boundary) &&
                    boundary.num == 17 && boundary.den == 19,
                "an out-of-range canonical value is refused without publishing it");
        const std::string oversized_literal(Limits().max_input_bytes + 1, '0');
        t.check(!rational_from_text(oversized_literal, &boundary) && boundary.num == 17 &&
                    boundary.den == 19,
                "an oversized exact literal is refused before GMP allocation grows");
    }

    // PRD section 13.1: an integer literal is a count and a decimal literal is a measurement.
    t.equal(figures("20 m"), "exact", "an integer is exact and limits nothing");
    t.equal(figures("-3 m/s^2"), "exact", "sign included");
    t.equal(figures("20.0 m"), "3", "the same number written with a point is measured to three");
    t.equal(figures("2.5 s"), "2", "two figures");
    t.equal(figures("-9.8 m/s^2"), "2", "a negative value counts its digits, not its sign");
    t.equal(figures("0.0450 m"), "3", "leading zeros place the point, trailing ones are measured");
    t.equal(figures("100.0 m"), "4", "zeros inside a measured number count");
    t.equal(figures("0 m/s"), "exact", "an integer zero remains exact");
    t.equal(figures("-0 m/s"), "exact", "a signed integer zero remains exact");
    t.equal(figures("0.0 m/s"), "1", "one written decimal place makes zero measured");
    t.equal(figures("-0.0 m/s"), "1", "a sign does not change measured-zero precision");
    t.equal(figures("0.00 m/s"), "2", "written decimal places retain zero precision");
    t.equal(figures("-0.00 m/s"), "2", "signed decimal zeros retain every written place");
    t.equal(figures(".0 m/s"), "1", "a parser-supported leading point retains one place");
    t.equal(figures("0. m/s"), "1", "a parser-supported trailing point marks measured zero");
    t.equal(last_place("20.0 m"), "-1", "20.0 records the tenths place");
    t.equal(last_place("0.0450 m"), "-4", "0.0450 records the ten-thousandths place");
    t.equal(last_place("0. m"), "0", "a trailing point records the units place");
    t.check(figures("0e0 m/s").find("refused:") == 0,
            "scientific notation remains outside the exact quantity grammar");

    t.equal(rounded("24.5", 2), "25", "a half rounds away from zero");
    t.equal(rounded("-24.5", 2), "-25", "in both directions");
    t.equal(rounded("24.4", 2), "24", "and below a half rounds down");
    t.equal(rounded("9.99", 2), "10", "a carry out of the last digit moves the point");
    t.equal(rounded("12345", 2), "12000", "digits beyond the count become zeros");
    t.equal(rounded("5", 3), "5.00", "and a short value is padded to the count");
    t.equal(rounded("0.045", 2), "0.045", "leading zeros are not digits");
    t.equal(rounded("0", 3), "0", "zero is zero at any count");
    {
        Quantity twenty;
        Quantity twenty_four;
        std::string why;
        t.check(parse_quantity("20.0", &twenty, &why) &&
                    parse_quantity("24.0", &twenty_four, &why),
                "measured subtraction inputs parse");
        Rational difference;
        t.check(rational_sub(twenty.value, twenty_four.value, &difference),
                "measured subtraction stays exact internally");
        const Precision precision =
            precision_sum(difference, twenty.precision, twenty_four.precision);
        std::string report;
        t.check(precision.kind == NumberKind::Measured && precision.significant_digits == 2 &&
                    precision.last_significant_decimal_place == -1 &&
                    precision_rounded_text(difference, precision, &report) &&
                    report == "-4.0",
                "20.0 minus 24.0 reports -4.0 at the shared tenths place");

        Quantity nineteen_ninety_nine;
        t.check(parse_quantity("19.99", &nineteen_ninety_nine, &why),
                "a finer subtraction input parses");
        Rational below_place;
        t.check(rational_sub(twenty.value, nineteen_ninety_nine.value, &below_place),
                "near cancellation stays exact internally");
        const Precision below_place_precision =
            precision_sum(below_place, twenty.precision, nineteen_ninety_nine.precision);
        t.check(precision_rounded_text(below_place, below_place_precision, &report) &&
                    report == "0.0" &&
                    precision_rounding_valid(below_place, report, below_place_precision) ==
                        HalfPlace::Within,
                "a difference below the limiting tenths place reports zero at that place");

        Quantity two_fifty;
        Quantity three;
        t.check(parse_quantity("2.50", &two_fifty, &why) && parse_quantity("3.0", &three, &why),
                "measured multiplication inputs parse");
        Rational product;
        t.check(rational_mul(two_fifty.value, three.value, &product),
                "measured multiplication stays exact internally");
        const Precision product_precision =
            precision_product(product, two_fifty.value, two_fifty.precision, three.value,
                              three.precision);
        t.check(product_precision.significant_digits == 2 &&
                    product_precision.last_significant_decimal_place == -1,
                "multiplication keeps the fewer significant-figure count");

        Precision maximum_precision;
        maximum_precision.kind = NumberKind::Measured;
        maximum_precision.significant_digits = std::numeric_limits<uint16_t>::max();
        const Precision maximum_product =
            precision_product(Rational{1, 1}, Rational{1, 1}, maximum_precision,
                              Rational{1, 1}, Precision());
        t.check(maximum_product.significant_digits == std::numeric_limits<uint16_t>::max() &&
                    maximum_product.last_significant_decimal_place == -65534,
                "the largest significant-digit count does not narrow when locating its last place");

        const Precision exact_zero_product =
            precision_product(Rational{0, 1}, Rational{0, 1}, Precision(), two_fifty.value,
                              two_fifty.precision);
        t.check(exact_zero_product.kind == NumberKind::Exact,
                "an exact zero factor makes the product exact");
        Quantity measured_zero;
        t.check(parse_quantity("0.00", &measured_zero, &why), "a measured zero parses");
        const Precision measured_zero_product =
            precision_product(Rational{0, 1}, measured_zero.value, measured_zero.precision,
                              Rational{2, 1}, Precision());
        t.check(measured_zero_product.kind == NumberKind::Measured &&
                    measured_zero_product.significant_digits == 2,
                "a measured zero remains measured when multiplied by an exact nonzero value");

        Precision decimal_place;
        decimal_place.kind = NumberKind::Measured;
        decimal_place.last_significant_decimal_place = -18;
        t.check(precision_rounded_text(Rational{1, INT64_C(1000000000000000000)}, decimal_place,
                                       &report) &&
                    report == "0.000000000000000001",
                "the smallest supported negative decimal place rounds exactly");
        // The comparison reaches past the printer, so nothing is published it cannot read back.
        t.check(precision_rounding_valid(Rational{1, INT64_C(1000000000000000000)}, report,
                                         decimal_place) == HalfPlace::Within,
                "and the comparison reaches that place too rather than calling it unreadable");
        decimal_place.last_significant_decimal_place = 18;
        t.check(precision_rounded_text(Rational{std::numeric_limits<int64_t>::max(), 1},
                                       decimal_place, &report) &&
                    report == "9000000000000000000",
                "the largest supported positive decimal place rounds exactly");
        t.check(precision_rounding_valid(Rational{std::numeric_limits<int64_t>::max(), 1}, report,
                                         decimal_place) == HalfPlace::Within,
                "and the comparison reaches that place too");
        decimal_place.last_significant_decimal_place = -19;
        report = "unchanged";
        t.check(!precision_rounded_text(Rational{1, 1}, decimal_place, &report) &&
                    report == "unchanged",
                "an unsupported negative decimal place is refused without publishing text");
        decimal_place.last_significant_decimal_place = 19;
        t.check(!precision_rounded_text(Rational{1, 1}, decimal_place, &report) &&
                    report == "unchanged",
                "an unsupported positive decimal place is refused without publishing text");

        // A rounding that carries into a new leading digit. 5.0 + 4.96 is 9.96 exactly and the sum
        // is good to the tenths place, so its report is 10.0 with three figures. The printer used to
        // hand the exact value's digit count to the significant-figure rounder, which wrote 10 and
        // dropped the place the sum was entitled to, and the sum's own count said two.
        Quantity five;
        Quantity four_ninety_six;
        t.check(parse_quantity("5.0", &five, &why) &&
                    parse_quantity("4.96", &four_ninety_six, &why),
                "carrying sum inputs parse");
        Rational carried;
        t.check(rational_add(five.value, four_ninety_six.value, &carried),
                "the carrying sum stays exact internally");
        const Precision carried_precision =
            precision_sum(carried, five.precision, four_ninety_six.precision);
        t.check(carried_precision.last_significant_decimal_place == -1 &&
                    carried_precision.significant_digits == 3,
                "a sum that rounds up into a new leading digit counts that digit");
        t.check(precision_rounded_text(carried, carried_precision, &report) && report == "10.0",
                "and reports 10.0 at the tenths place rather than 10");
        Precision hundredths;
        hundredths.kind = NumberKind::Measured;
        hundredths.last_significant_decimal_place = -2;
        Rational near_tenth;
        t.check(rational_from_text("0.096", &near_tenth) &&
                    precision_rounded_text(near_tenth, hundredths, &report) && report == "0.10",
                "a carry below one keeps its written hundredths place");
        Rational near_one;
        t.check(rational_from_text("0.996", &near_one) &&
                    precision_rounded_text(near_one, hundredths, &report) && report == "1.00",
                "and a carry across one keeps both places");
        Rational negative_carry;
        t.check(rational_from_text("-9.96", &negative_carry) &&
                    precision_rounded_text(negative_carry, carried_precision, &report) &&
                    report == "-10.0",
                "a negative carry keeps its sign and its place");

        // The same carry on the product side, where the figure count is the given and the place is
        // derived. 2.0 times 4.98 is 9.96 to two figures, which is 10 with its last figure in the
        // units place, not 9.96 in the tenths.
        Quantity two_point_zero;
        Quantity four_ninety_eight;
        t.check(parse_quantity("2.0", &two_point_zero, &why) &&
                    parse_quantity("4.98", &four_ninety_eight, &why),
                "carrying product inputs parse");
        Rational carried_product;
        t.check(rational_mul(two_point_zero.value, four_ninety_eight.value, &carried_product),
                "the carrying product stays exact internally");
        const Precision carried_product_precision =
            precision_product(carried_product, two_point_zero.value, two_point_zero.precision,
                              four_ninety_eight.value, four_ninety_eight.precision);
        t.check(carried_product_precision.significant_digits == 2 &&
                    carried_product_precision.last_significant_decimal_place == 0,
                "a product that rounds up into a new leading digit moves its last place up too");
        t.check(precision_rounded_text(carried_product, carried_product_precision, &report) &&
                    report == "10",
                "and reports 10 at two figures");
        // Rounding at the place rather than to a digit count also reaches a numeral wider than
        // int64, which the comparison beside it already reads. This was refused as an overflow.
        Rational widest{std::numeric_limits<int64_t>::max(), 1};
        t.check(precision_rounded_text(widest, carried_precision, &report) &&
                    report == "9223372036854775807.0" &&
                    precision_rounding_valid(widest, report, carried_precision) ==
                        HalfPlace::Within,
                "the largest int64 at the tenths place prints exactly and passes its own check");

        // precision_at_digits is the other direction from precision_sum: the figure count is fixed
        // and the place follows the value, which is what a combined or copied precision needs
        // before it travels with an answer it was not computed from.
        Precision two_figures;
        two_figures.kind = NumberKind::Measured;
        two_figures.significant_digits = 2;
        two_figures.last_significant_decimal_place = -1;
        Rational carrying;
        t.check(rational_from_text("9.96", &carrying) &&
                    precision_at_digits(carrying, two_figures).last_significant_decimal_place == 0,
                "a value that carries at its figure count lands its last figure a place up");
        Rational not_carrying;
        t.check(rational_from_text("9.94", &not_carrying) &&
                    precision_at_digits(not_carrying, two_figures)
                            .last_significant_decimal_place == -1,
                "and one that does not carry keeps the place its figures reach");
        t.check(precision_at_digits(Rational{9960, 1}, Precision{NumberKind::Measured, 3, -2})
                        .last_significant_decimal_place == 1,
                "a place that disagrees with the value is replaced rather than trusted");
        Precision exact_kind;
        exact_kind.kind = NumberKind::Exact;
        exact_kind.last_significant_decimal_place = -7;
        t.check(precision_at_digits(carrying, exact_kind).last_significant_decimal_place == -7 &&
                    precision_at_digits(Rational{0, 1}, two_figures)
                            .last_significant_decimal_place == -1,
                "an exact precision and a zero value are both returned untouched");

        // The two Precision fields against each other, over a sweep rather than at a chosen value.
        // core-1 was a carry making them disagree and nothing in the tree asked whether they ever
        // do, which is how it survived. significant_digits and last_significant_decimal_place say
        // the same thing twice: the leading place a count implies is place + digits - 1, and that
        // has to be the leading place of the value once it is rounded there.
        {
            size_t sum_disagreements = 0;
            size_t product_disagreements = 0;
            size_t examined = 0;
            for (int64_t num = -400; num <= 400; ++num) {
                for (int64_t den = 1; den <= 12; ++den) {
                    for (int32_t place = -4; place <= 2; ++place) {
                        const Rational value{num, den};
                        if (num == 0)
                            continue;
                        ++examined;
                        Precision declared;
                        declared.kind = NumberKind::Measured;
                        declared.last_significant_decimal_place = place;
                        declared.significant_digits = 3;

                        // precision_at_value through precision_sum, which is the public way in.
                        const Precision summed = precision_sum(value, declared, declared);
                        std::string text;
                        if (precision_rounded_text(value, summed, &text) &&
                            !fields_agree(text, summed))
                            ++sum_disagreements;

                        // And the product direction, where the count is the given.
                        const Precision multiplied =
                            precision_product(value, value, declared, Rational{1, 1}, declared);
                        if (precision_rounded_text(value, multiplied, &text) &&
                            !fields_agree(text, multiplied))
                            ++product_disagreements;
                    }
                }
            }
            // Shown to discriminate, not assumed to: on this grid at 0c55208 the sum direction
            // reports 180 disagreements, the first being -399/4 at place 0 printing -100 with a
            // count of 2, which implies a leading digit in the tens where the text shows hundreds.
            // The product direction reports 0 at 0c55208 as well as here, because precision_product
            // derives the place from the count and the two therefore agree even when both are
            // wrong. So that half is a consistency guard against a future change, not a check with
            // a failure behind it, and it should not be read as proven coverage of the product
            // rule. What covers that is the carry case above, which pins the value.
            t.check(examined > 20000, "the field agreement sweep covers the grid it claims to");
            t.check(sum_disagreements == 0,
                    "precision_sum leaves the figure count and the decimal place describing the "
                    "same number");
            t.check(product_disagreements == 0,
                    "and precision_product's two fields stay consistent with each other");
        }

        // Held in int64 this difference overflowed and the right rounding read as a failed check.
        Rational fine;
        fine.num = 1;
        fine.den = 11442889;
        Precision fine_precision;
        fine_precision.kind = NumberKind::Measured;
        fine_precision.last_significant_decimal_place = -12;
        t.check(precision_rounded_text(fine, fine_precision, &report) &&
                    report == "0.000000087391" &&
                    precision_rounding_valid(fine, report, fine_precision) == HalfPlace::Within,
                "a rounding at the twelfth decimal place passes its own check");
        t.check(precision_rounding_valid(fine, "0.000000087392", fine_precision) ==
                        HalfPlace::Outside &&
                    precision_rounding_valid(fine, "0.000000087390", fine_precision) ==
                        HalfPlace::Outside,
                "and the places either side of it are rejected as measured disagreements rather "
                "than as roundings nothing could read");
        t.check(precision_rounding_valid(fine, "eight point seven", fine_precision) ==
                    HalfPlace::Unreadable,
                "while text that is not a decimal is a comparison that never ran");
    }
    {
        Rational third;
        third.num = 1;
        third.den = 3;
        std::string out;
        t.check(rounded_text(third, 3, &out) && out == "0.333",
                "a non-terminating value rounds where printing it exactly cannot");
        t.check(!rounded_text(third, 0, &out), "no digits is not a number of digits");
        t.check(!rounded_text(third, 19, &out), "and more digits than int64 holds is refused");

        // The check the derivation displays beside the rounding, so it has to fail on a wrong one
        // as well as pass on a right one, and it has to say which of the two it did.
        t.check(precision_rounding_valid(third, "0.333", at_place(-3)) == HalfPlace::Within,
                "0.333 is within half a place of 1/3");
        t.check(precision_rounding_valid(third, "0.33", at_place(-2)) == HalfPlace::Within,
                "and so is 0.33 at the hundredth");
        t.check(precision_rounding_valid(third, "0.34", at_place(-2)) == HalfPlace::Outside,
                "0.34 is measured and rejected");
        t.check(precision_rounding_valid(third, "0.4", at_place(-1)) == HalfPlace::Outside,
                "and so is 0.4 at the tenth");

        // A rounding wider than int64 whose text is a perfectly good decimal. The comparison used
        // to narrow the text back to an int64 Rational, so a correct rounding came back as the same
        // false a wrong one did, and the engine could not check a value it could not spell.
        Rational ceiling;
        ceiling.num = std::numeric_limits<int64_t>::max();
        ceiling.den = 1;
        t.check(precision_rounding_valid(ceiling, "9223372036854775810", at_place(1)) ==
                    HalfPlace::Within,
                "a rounding one place wider than int64 is compared rather than refused");
        t.check(precision_rounding_valid(ceiling, "9223372036854775910", at_place(1)) ==
                    HalfPlace::Outside,
                "and a wrong one that wide is still caught");

        // The same narrowing one field over. The unit was a Rational too, so a place past the
        // eighteenth came back unreadable even though the printer had just written one.
        Rational thirtieth;
        thirtieth.num = 1;
        thirtieth.den = 30;
        t.check(precision_rounding_valid(thirtieth, "0.0333333333333333333", at_place(-19)) ==
                    HalfPlace::Within,
                "a rounding finer than a Rational unit reaches is compared rather than refused");
        t.check(precision_rounding_valid(thirtieth, "0.0333333333333333334", at_place(-19)) ==
                    HalfPlace::Outside,
                "and a wrong one that fine is still caught");
        t.check(precision_rounding_valid(thirtieth, "0.0333333333333333333", at_place(-2000)) ==
                    HalfPlace::Unreadable,
                "while a place past the widest unit this builds is a comparison that never ran");

        // A carry leaves the declared place one finer than the text, and the place the value was
        // rounded at is the one the check has to use.
        Rational carried;
        carried.num = INT64_C(900000000000000000);
        carried.den = INT64_C(9000000000000000001);
        t.check(precision_rounding_valid(carried, "0.100000000000000000", at_place(-19)) ==
                    HalfPlace::Within,
                "a carry past the eighteenth place is checked at the place it rounded at");

        // The three ways there is nothing to compare, told apart from a disagreement.
        t.check(precision_rounding_valid(third, "", at_place(-2)) == HalfPlace::Unreadable,
                "empty text is not a rounding that disagreed");
        t.check(precision_rounding_valid(third, "one third", at_place(-2)) == HalfPlace::Unreadable,
                "nor is text that is not a numeral");
        t.check(precision_rounding_valid(third, "0.1.2", at_place(-2)) == HalfPlace::Unreadable,
                "nor is a numeral with two points");

        // Zero has no figures to count, and the declared place answers for it either way.
        t.check(precision_rounding_valid(Rational{0, 1}, "0", at_place(-2)) == HalfPlace::Within,
                "a measured zero reported as 0 is within half of any place");
        t.check(precision_rounding_valid(third, "0", at_place(-3)) == HalfPlace::Outside,
                "while one third reported as 0 at the thousandth is a disagreement rather than a "
                "numeral nothing could read");

        // A report coarser than the measurement earned, which agrees with itself at its own place.
        Rational coarse;
        coarse.num = 1234;
        coarse.den = 10000;
        t.check(precision_rounding_valid(coarse, "0.123", at_place(-3)) == HalfPlace::Within,
                "0.1234 declared to the thousandth reports as 0.123");
        t.check(precision_rounding_valid(coarse, "0.1", at_place(-3)) == HalfPlace::Outside,
                "and a report that silently dropped two of those places is rejected");
        t.check(precision_rounding_valid(coarse, "0.2", at_place(-3)) == HalfPlace::Outside,
                "as is one that dropped them and rounded the wrong way");
    }

    t.equal(unit_dimension("m"), "L", "metre is a length");
    t.equal(unit_dimension("s"), "T", "second is a time");
    t.equal(unit_dimension("kg"), "M", "kilogram is a mass");
    t.equal(unit_dimension("N"), "L M T^-2", "newton carries the derived force dimension");
    t.equal(unit_dimension("J"), "L^2 M T^-2", "joule carries the derived energy dimension");
    t.evidence("PHYS-013",
               unit_dimension("m") == "L" && unit_dimension("s") == "T" &&
                   unit_dimension("kg") == "M" && unit_dimension("N") == "L M T^-2" &&
                   unit_dimension("J") == "L^2 M T^-2" && unit_scale("km") == "1000" &&
                   unit_scale("cm") == "0.01" && unit_scale("mm") == "0.001" &&
                   unit_scale("ms") == "0.001" && unit_scale("min") == "60" &&
                   unit_scale("h") == "3600" && unit_scale("g") == "0.001",
               "the MVP SI base, derived and prefixed unit set is recognized with exact scales");
    t.equal(unit_dimension("m/s"), "L T^-1", "a quotient subtracts exponents");
    t.equal(unit_dimension("m/s^2"), "L T^-2", "and a power on the divisor scales them");
    t.equal(unit_dimension("kg*m/s^2"), "L M T^-2", "a product adds them");
    t.equal(unit_dimension("kg m/s^2"), "L M T^-2", "with a space as the product too");
    t.equal(unit_dimension("m/s/s"), "L T^-2", "each division applies to the one factor after it");
    t.equal(unit_dimension("m/s*s"), "L", "so a division does not swallow the rest");
    t.equal(unit_dimension("s^-1"), "T^-1", "a negative exponent is read");
    t.equal(unit_dimension("furlong"), "refused: unknown unit furlong", "an unknown unit names itself");
    t.equal(unit_dimension("/s"), "refused: a unit cannot start with a division", "a leading division is refused");
    t.equal(unit_dimension("m^"), "refused: a unit exponent needs an integer after ^", "a bare caret is refused");
    t.equal(unit_dimension("m^100"), "refused: a unit exponent that large is not a unit",
            "a unit exponent above the domain cap is refused");
    t.equal(unit_dimension("m^999999999999999999999999"),
            "refused: a unit exponent that large is not a unit",
            "an exponent outside the integer representation is refused");
    t.equal(unit_dimension("m2"), "refused: unexpected character in a unit: 2", "a digit without a caret is refused");
    t.equal(unit_dimension(""), "refused: no unit given", "an empty unit is refused");

    // Units arrive from the calculator's 2D editor, which brackets what it hands back: typing
    // "5 m/s" into a math box and reading it out gives "5 ((m)/(s))". Brackets group, they are not
    // noise to be skipped, which is what the last two of these are here to hold.
    t.equal(unit_dimension("((m)/(s))"), "L T^-1", "the 2D editor's spelling of m/s");
    t.equal(unit_dimension("((m)/(s^(2)))"), "L T^-2", "and of m/s^2, exponent bracketed too");
    t.equal(unit_dimension("(m)"), "L", "a bracketed single factor");
    t.equal(unit_dimension("((((m))))"), "L", "however deeply it is nested");
    t.equal(unit_dimension("(m/s)^2"), "L^2 T^-2", "an exponent applies to the whole group");
    t.equal(unit_dimension("(m/s)^-1"), "L^-1 T", "including a negative one");
    t.equal(unit_dimension("m/(s*kg)"), "L M^-1 T^-1",
            "a division applies to the whole group after it, not just its first factor");
    t.equal(unit_dimension("m/s*kg"), "L M T^-1", "which is not what the same factors unbracketed mean");
    // A trailing operator. The scanner had the words for this and could not reach them: the loop
    // consumed the operator, found the text exhausted and returned success, so "5 m/" read as five
    // metres and a wrong dimension reached the solve where a refusal belonged.
    t.equal(unit_dimension("m/"), "refused: a unit ends with nothing after its last operator",
            "a unit ending in a division is refused");
    t.equal(unit_dimension("m*"), "refused: a unit ends with nothing after its last operator",
            "and one ending in a multiplication");
    t.equal(unit_dimension("m//s"), "refused: two operators in a row in a unit",
            "and a doubled division, whose first operator has no factor after it");
    t.equal(unit_dimension("m/s"), "L T^-1", "while an ordinary division still reads");
    t.equal(unit_dimension("(m"), "refused: an unclosed bracket in a unit", "an unclosed group is refused");
    t.equal(unit_dimension("m)"), "refused: unexpected character in a unit: )",
            "and so is a stray closing bracket");
    t.equal(unit_dimension("()"), "refused: no unit given", "an empty group is refused");
    t.equal(unit_dimension("m^(2"), "refused: an unclosed bracket in a unit exponent",
            "an unclosed exponent bracket is refused");
    const std::string nested_power = "(((((m^99)^99)^99)^99)^99)";
    t.equal(unit_dimension(nested_power),
            "refused: the unit dimension for " + nested_power + " does not fit",
            "nested powers that exceed the dimension representation are refused before multiplying");

    t.equal(unit_scale("km"), "1000", "a kilometre is a thousand metres");
    t.equal(unit_scale("cm"), "0.01", "a centimetre a hundredth");
    t.equal(unit_scale("km/h"), "5/18", "km/h to m/s is the exact 5/18");
    t.equal(unit_scale("min"), "60", "a minute is sixty seconds");
    t.equal(unit_scale("N"), "1", "newton is exact in SI base units");
    t.equal(unit_scale("J"), "1", "joule is exact in SI base units");
    t.equal(unit_scale("km/h^2"), "1/12960", "a divisor's power applies to the scale as well");
    t.equal(unit_scale("((km)/(h))"), "5/18", "the 2D editor's km/h scales the same as the plain one");
    t.equal(unit_scale("(km/h)^2"), "25/324", "a group's exponent raises its scale as well");
    t.equal(unit_scale("(km/h)^-1"), "3.6",
            "a negative group exponent raises the reciprocal scale exactly");
    t.equal(unit_scale("cm^9"), "0.000000000000000001",
            "a powered fractional scale reaches its exact denominator boundary");
    t.equal(unit_scale("cm^-9"), "1000000000000000000",
            "a negative power reaches its exact numerator boundary");
    {
        Unit unchanged;
        unchanged.text = "sentinel";
        unchanged.dimension.mass = 7;
        unchanged.scale = Rational{17, 19};
        const Unit sentinel = unchanged;
        std::string why;
        t.check(!parse_unit("cm^-10", &unchanged, &why) &&
                    why == "the conversion factor for cm^-10 does not fit",
                "a powered scale beyond the rational boundary is refused");
        t.check(unchanged.text == sentinel.text && unchanged.dimension == sentinel.dimension &&
                    rational_equal(unchanged.scale, sentinel.scale),
                "a failed powered scale leaves the published unit unchanged");
    }

    t.equal(si_value("5 m/s"), "5 m/s", "a value already in SI is unchanged");
    t.equal(si_value("18 km/h"), "5 m/s", "18 km/h is exactly 5 m/s");
    t.equal(si_value("2.5 km"), "2500 m", "a decimal value scales exactly");
    t.equal(si_value("-3 m/s^2"), "-3 m/s^2", "a negative acceleration keeps its sign");
    t.equal(si_value("90 min"), "5400 s", "minutes become seconds");
    t.equal(si_value("3 N"), "3 kg m/s^2", "newtons convert exactly to SI base units");
    t.equal(si_value("2 J"), "2 kg m^2/s^2", "joules convert exactly to SI base units");
    t.equal(si_value("4"), "4 1", "a bare number is dimensionless");
    t.equal(si_value("  7   cm "), "0.07 m", "surrounding spaces are ignored");
    t.equal(si_value("m/s"), "refused: a quantity starts with a number", "a unit with no number is refused");
    t.equal(si_value("5 parsec"), "refused: unknown unit parsec", "and an unknown unit still names itself");

    Dimension force;
    force.mass = 1;
    force.length = 1;
    force.time = -2;
    t.equal(si_unit_text(force), "kg m/s^2", "an SI spelling puts the negative powers under the line");
    Dimension none;
    t.equal(si_unit_text(none), "1", "a pure number spells as one");
    t.equal(dimension_text(none), "1", "in dimension form too");
    Dimension per_second;
    per_second.time = -1;
    t.equal(si_unit_text(per_second), "1/s", "a pure reciprocal gets a one on top");
    Dimension minimum_power;
    minimum_power.length = std::numeric_limits<int>::min();
    t.equal(dimension_text(minimum_power),
            "L^-" + magnitude_text(magnitude(static_cast<int64_t>(minimum_power.length))),
            "the minimum representable dimension exponent prints without signed overflow");
    t.equal(si_unit_text(minimum_power),
            "1/m^" + magnitude_text(magnitude(static_cast<int64_t>(minimum_power.length))),
            "the minimum exponent also prints safely as an SI denominator");
    {
        Unit first;
        Unit second;
        std::string why;
        bool first_ok = parse_unit("N", &first, &why);
        bool second_ok = first_ok && parse_unit(first.text, &second, &why);
        t.check(second_ok && first.dimension == second.dimension &&
                    first.scale.num == second.scale.num && first.scale.den == second.scale.den,
                "a parsed newton spelling round trips without changing its typed unit");
        first_ok = parse_unit("J", &first, &why);
        second_ok = first_ok && parse_unit(first.text, &second, &why);
        t.check(second_ok && first.dimension == second.dimension &&
                    first.scale.num == second.scale.num && first.scale.den == second.scale.den,
                "a parsed joule spelling round trips without changing its typed unit");
    }
    {
        Dimension largest;
        largest.length = std::numeric_limits<int>::max();
        Dimension one;
        one.length = 1;
        Dimension untouched;
        untouched.mass = 7;
        const Dimension sentinel = untouched;
        t.check(!dimension_multiply(largest, one, &untouched),
                "dimension multiplication refuses positive exponent overflow");
        t.check(untouched == sentinel,
                "failed dimension multiplication does not publish a partial dimension");

        Dimension smallest;
        smallest.time = std::numeric_limits<int>::min();
        t.check(!dimension_power(smallest, -1, &untouched),
                "dimension powers refuse the unrepresentable negation of the minimum exponent");
        t.check(untouched == sentinel,
                "failed dimension exponentiation does not publish a partial dimension");
        t.check(dimension_power(smallest, 0, &untouched) && untouched == Dimension(),
                "a zero power remains dimensionless at the representation boundary");
    }

    t.begin_group("vectors");
    t.equal(vector_text(vector_of("m/s", 3, 4)), "(3 i + 4 j) m/s",
            "a vector prints in unit-vector form with the unit once at the end");
    t.equal(vector_text(vector_of("", 3, 4)), "3 i + 4 j",
            "a dimensionless one has no unit and no brackets");
    t.equal(vector_text(vector_of("m/s", 3, -4)), "(3 i - 4 j) m/s",
            "a negative component is subtracted rather than written plus minus");
    t.equal(vector_text(vector_of("m", 0, 5)), "5 j m",
            "a zero component is dropped, and one term needs no brackets");
    t.equal(vector_text(vector_of("m", -5, 0)), "-5 i m",
            "a leading negative keeps its sign against the number");
    t.equal(vector_text(vector_of("m", 0, 0)), "0 m", "an all-zero vector still prints something");
    t.equal(vector_text(vector_of("m", 1, 0, 2)), "(1 i + 2 k) m",
            "a three component vector skips the middle axis when it is zero");
    t.equal(vector_text(vector_of("m", 3, 4, 0)), "(3 i + 4 j) m",
            "and drops a zero k rather than printing it");
    {
        const Vector minimum_component =
            vector_of("m", 1, std::numeric_limits<int64_t>::min());
        const std::string expected = "(1 i - 9223372036854775808 j) m";
        std::string reported;
        HalfPlace exact_checked = HalfPlace::Unreadable;
        const bool report_ok = reported_vector_text(minimum_component, &reported, exact_checked);
        t.check(vector_text(minimum_component) == expected && report_ok && reported == expected,
                "exact vector formatting handles a later minimum numerator without signed negation");
    }
    {
        // The printer's comparison handed back rather than folded into the bool a refusal shares.
        Vector measured = vector_of("m/s", 3, 4);
        measured.precision.kind = NumberKind::Measured;
        measured.precision.last_significant_decimal_place = -1;
        std::string reported;
        HalfPlace checked = HalfPlace::Unreadable;
        t.check(reported_vector_text(measured, &reported, checked) && reported == "(3.0 i + 4.0 j) m/s",
                "a measured vector still gets its rounded spelling");
        t.check(checked == HalfPlace::Within,
                "and the comparison that allowed it comes back with the text, so the caller is not "
                "left inferring it from a bool that also means the arithmetic ran out of room");
    }

    {
        // Density rounds by digit count and checks at the declared place, so the two have to agree
        // wherever the printer can write. A swept pair rather than a row, because the places that
        // came apart were the ones nobody picks by hand.
        const Rational candidates[] = {
            Rational{1, 3},
            Rational{2, 3},
            Rational{1, 7},
            Rational{1, 30},
            Rational{1, 300},
            Rational{1, 3000},
            Rational{1, 30000},
            Rational{1, 300000},
            Rational{1, 3000000},
            Rational{1, 30000000},
            Rational{996, 100},
            Rational{997, 1000},
            Rational{1, 2},
            Rational{7, 2},
            Rational{1, 1000000},
            Rational{INT64_C(9223372036854775807), 7},
            Rational{1, INT64_C(1000000000000)},
        };
        int unchecked = 0;
        int compared = 0;
        for (const Rational &candidate : candidates) {
            for (unsigned digits = 1; digits <= 18; ++digits) {
                std::string reported;
                if (!rounded_text(candidate, digits, &reported))
                    continue;
                if (reported == rational_text(candidate))
                    continue;
                Precision precision;
                precision.kind = NumberKind::Measured;
                precision.significant_digits = static_cast<uint16_t>(digits);
                precision = precision_at_digits(candidate, precision);
                ++compared;
                if (precision_rounding_valid(candidate, reported, precision) != HalfPlace::Within)
                    ++unchecked;
            }
        }
        t.check(compared > 200 && unchecked == 0,
                "every rounding the digit printer writes passes the check at its declared place");
    }

    t.equal(added(vector_of("m", 3, 4), vector_of("m", 1, 2)), "(4 i + 6 j) m",
            "vectors in one frame add component by component");
    t.equal(added(vector_of("km", 1, 0), vector_of("m", 500, 0)), "1500 i m",
            "and are converted to SI first, so km and m add without either being reinterpreted");
    t.equal(subtracted(vector_of("m", 3, 4), vector_of("m", 1, 2)), "(2 i + 2 j) m",
            "subtraction is the same rule with the sign flipped");
    t.equal(subtracted(vector_of("m", 3, 4), vector_of("m", 3, 4)), "0 m",
            "a vector minus itself is the zero vector, printed rather than empty");
    {
        Vector measured_velocity;
        Vector exact_reference;
        std::string why;
        const bool parsed = parse_vector("(1.0, 2.0) km/s", &measured_velocity, &why) &&
                            parse_vector("(300, 400) m/s", &exact_reference, &why);
        t.check(parsed, "mixed measured vector inputs parse");

        Vector measured_si;
        const bool converted = parsed && to_si(measured_velocity, &measured_si);
        t.check(converted && measured_si.x.num == 1000 && measured_si.x.den == 1 &&
                    measured_si.y.num == 2000 && measured_si.y.den == 1,
                "vector SI conversion scales every component exactly");
        t.check(converted && measured_si.precision.kind == NumberKind::Measured &&
                    measured_si.precision.significant_digits == 2 &&
                    measured_si.precision.last_significant_decimal_place == 2,
                "vector SI conversion moves shared precision from tenths of km/s to hundreds of m/s");

        Vector difference;
        const bool subtracted = converted && vector_sub(measured_si, exact_reference, &difference, &why);
        t.check(subtracted && difference.x.num == 700 && difference.x.den == 1 &&
                    difference.y.num == 1600 && difference.y.den == 1,
                "mixed-unit vector subtraction retains exact SI components");
        t.check(subtracted && difference.precision.kind == NumberKind::Measured &&
                    difference.precision.significant_digits == 1 &&
                    difference.precision.last_significant_decimal_place == 2,
                "the shared result precision conservatively combines both component differences");
        std::string horizontal;
        std::string vertical;
        t.check(subtracted && precision_rounded_text(difference.x, difference.precision, &horizontal) &&
                    precision_rounded_text(difference.y, difference.precision, &vertical) &&
                    horizontal == "700" && vertical == "1600",
                "mixed-unit measured components report at the converted hundreds place");

        Vector measured_zero;
        Vector zero_si;
        t.check(parse_vector("(0.0, 0.0) km/s", &measured_zero, &why) &&
                    to_si(measured_zero, &zero_si) &&
                    zero_si.precision.kind == NumberKind::Measured &&
                    zero_si.precision.significant_digits == 1 &&
                    zero_si.precision.last_significant_decimal_place == 2,
                "an exact-zero component still transports its measured place through unit scaling");
    }
    {
        Vector too_large = vector_of("km", std::numeric_limits<int64_t>::max(), 0);
        Vector untouched = vector_of("s", 17, 19);
        untouched.precision = measured(3);
        const Vector sentinel = untouched;
        t.check(!to_si(too_large, &untouched), "vector SI conversion refuses exact overflow");
        t.check(rational_equal(untouched.x, sentinel.x) && rational_equal(untouched.y, sentinel.y) &&
                    untouched.rank == sentinel.rank && untouched.frame == sentinel.frame &&
                    untouched.unit.text == sentinel.unit.text &&
                    untouched.unit.dimension == sentinel.unit.dimension &&
                    rational_equal(untouched.unit.scale, sentinel.unit.scale) &&
                    untouched.precision.kind == sentinel.precision.kind &&
                    untouched.precision.significant_digits == sentinel.precision.significant_digits &&
                    untouched.precision.last_significant_decimal_place ==
                        sentinel.precision.last_significant_decimal_place,
                 "a refused vector conversion leaves its output unchanged");
    }
    {
        Vector velocity;
        Quantity duration;
        Vector displacement;
        std::string why;
        const bool scaled = parse_vector("(1.0, 2.0) km/s", &velocity, &why) &&
                            parse_quantity("2.0 min", &duration, &why) &&
                            vector_scale(velocity, duration, &displacement, &why);
        t.check(scaled && displacement.x.num == 120000 && displacement.x.den == 1 &&
                    displacement.y.num == 240000 && displacement.y.den == 1,
                "vector scaling retains exact SI components through two non-SI unit scales");
        t.check(scaled && displacement.precision.kind == NumberKind::Measured &&
                    displacement.precision.significant_digits == 2 &&
                    displacement.precision.last_significant_decimal_place == 4,
                "vector scaling transports both operand precisions before multiplying components");

        Vector length;
        Vector direction;
        Quantity dot;
        const bool dotted = parse_vector("(1.0, 2.0) km", &length, &why) &&
                            parse_vector("(3.0, 4.0) m", &direction, &why) &&
                            vector_dot(length, direction, &dot, &why);
        t.check(dotted && dot.value.num == 11000 && dot.value.den == 1,
                "a mixed-unit dot product retains its exact accumulated SI value");
        t.check(dotted && dot.precision.kind == NumberKind::Measured &&
                    dot.precision.significant_digits == 3 &&
                    dot.precision.last_significant_decimal_place == 2,
                "dot-product precision follows each product term and their sum");

        Vector left;
        Vector right;
        Vector cross;
        const bool crossed = parse_vector("(1.0, 2.0, 3.0) km", &left, &why) &&
                             parse_vector("(4.0, 5.0, 6.0) m", &right, &why) &&
                             vector_cross(left, right, &cross, &why);
        t.check(crossed && cross.x.num == -3000 && cross.x.den == 1 && cross.y.num == 6000 &&
                    cross.y.den == 1 && cross.z.num == -3000 && cross.z.den == 1,
                "a mixed-unit cross product retains exact SI component differences");
        t.check(crossed && cross.precision.kind == NumberKind::Measured &&
                    cross.precision.significant_digits == 1 &&
                    cross.precision.last_significant_decimal_place == 3,
                "cross-product precision combines both terms of every component conservatively");

        Vector pythagorean;
        Quantity magnitude;
        const bool measured_magnitude = parse_vector("(6.0, 8.0) km", &pythagorean, &why) &&
                                        vector_magnitude(pythagorean, &magnitude, &why);
        t.check(measured_magnitude && magnitude.value.num == 10000 && magnitude.value.den == 1,
                "a measured magnitude retains its exact SI square root");
        t.check(measured_magnitude && magnitude.precision.kind == NumberKind::Measured &&
                    magnitude.precision.significant_digits == 3 &&
                    magnitude.precision.last_significant_decimal_place == 2,
                "magnitude precision follows the squared component sum before the exact root");
    }
    {
        Vector measured_zero;
        Quantity exact_scale;
        Vector scaled_zero;
        Vector exact_direction;
        Quantity zero_dot;
        Quantity zero_magnitude;
        std::string why;
        const bool parsed = parse_vector("(0.0, 0.0) km", &measured_zero, &why) &&
                            parse_quantity("2", &exact_scale, &why) &&
                            parse_vector("(3, 4) m", &exact_direction, &why);
        t.check(parsed && vector_scale(measured_zero, exact_scale, &scaled_zero, &why) &&
                    scaled_zero.x.num == 0 && scaled_zero.y.num == 0 &&
                    scaled_zero.precision.kind == NumberKind::Measured &&
                    scaled_zero.precision.significant_digits == 1 &&
                    scaled_zero.precision.last_significant_decimal_place == 2,
                "scaling measured-zero components keeps their converted hundreds place");
        t.check(parsed && vector_dot(measured_zero, exact_direction, &zero_dot, &why) &&
                    zero_dot.value.num == 0 && zero_dot.precision.kind == NumberKind::Measured &&
                    zero_dot.precision.significant_digits == 1 &&
                    zero_dot.precision.last_significant_decimal_place == 2,
                "dot-product accumulation keeps a measured zero measured after SI conversion");
        t.check(parsed && vector_magnitude(measured_zero, &zero_magnitude, &why) &&
                    zero_magnitude.value.num == 0 &&
                    zero_magnitude.precision.kind == NumberKind::Measured &&
                    zero_magnitude.precision.significant_digits == 1 &&
                    zero_magnitude.precision.last_significant_decimal_place == 2,
                "an exact-zero magnitude retains the vector's converted measured place");

        Vector crossed_length;
        Vector crossed_force;
        Vector aligned_force;
        Quantity crossed_dot;
        Quantity offset_dot;
        const bool crossed = parse_vector("200.0 i + 0.0 j cm", &crossed_length, &why) &&
                             parse_vector("0.0 i + 15.0 j N", &crossed_force, &why) &&
                             parse_vector("15.0 i + 30.0 j N", &aligned_force, &why);
        t.check(crossed && vector_dot(crossed_length, crossed_force, &crossed_dot, &why) &&
                    crossed_dot.value.num == 0 &&
                    crossed_dot.precision.kind == NumberKind::Measured &&
                    crossed_dot.precision.significant_digits == 1 &&
                    crossed_dot.precision.last_significant_decimal_place == -1,
                "a dot product that cancels to zero takes its place from the terms that canceled");
        t.check(crossed && vector_dot(crossed_length, aligned_force, &offset_dot, &why) &&
                    offset_dot.value.num == 30 && offset_dot.value.den == 1 &&
                    offset_dot.precision.kind == NumberKind::Measured &&
                    offset_dot.precision.significant_digits == 3 &&
                    offset_dot.precision.last_significant_decimal_place == -1,
                "the same zero component leaves a nonzero fold claiming its three figures");
        Vector nearer_length;
        Quantity nearer_dot;
        t.check(parse_vector("20.0 i + 0.0 j cm", &nearer_length, &why) && crossed &&
                    vector_dot(nearer_length, crossed_force, &nearer_dot, &why) &&
                    nearer_dot.value.num == 0 &&
                    nearer_dot.precision.kind == NumberKind::Measured &&
                    nearer_dot.precision.significant_digits == 2 &&
                    nearer_dot.precision.last_significant_decimal_place == -2,
                "a zero fold whose terms agree still takes its place from how large the other factors are");

        Vector parallel;
        Vector zero_cross;
        t.check(parse_vector("(2.0, 4.0, 6.0) m", &parallel, &why) &&
                    parse_vector("(1.0, 2.0, 3.0) km", &exact_direction, &why) &&
                    vector_cross(exact_direction, parallel, &zero_cross, &why) &&
                    zero_cross.x.num == 0 && zero_cross.y.num == 0 && zero_cross.z.num == 0 &&
                    zero_cross.precision.kind == NumberKind::Measured &&
                    zero_cross.precision.significant_digits == 1 &&
                    zero_cross.precision.last_significant_decimal_place == 3,
                "cross-product cancellation retains the coarsest measured zero component");
    }
    {
        Vector with_zero;
        Vector without_zero;
        std::string why;
        t.check(parse_vector("36.0 i + 0.0 j m", &with_zero, &why) &&
                    with_zero.precision.kind == NumberKind::Measured &&
                    with_zero.precision.significant_digits == 3 &&
                    with_zero.precision.last_significant_decimal_place == -1,
                "a written zero component declares its place and claims no significant figures");
        t.check(parse_vector("36.0 i + 1.0 j m", &without_zero, &why) &&
                    without_zero.precision.significant_digits == 2,
                "a component that does claim two figures still wins the fewest-figures rule");

        Vector finer;
        std::string finer_text;
        HalfPlace finer_checked = HalfPlace::Within;
        t.check(parse_vector("36.000 i + 0.0 j m", &finer, &why) &&
                    finer.precision.significant_digits == 5 &&
                    finer.precision.last_significant_decimal_place == -3 &&
                    reported_vector_text(finer, &finer_text, finer_checked) &&
                    finer_text == "36.000 i m",
                "the zero's coarser place does not blunt the component that will be printed");

        Vector converted;
        Quantity elapsed;
        Vector drift;
        std::string reported;
        HalfPlace checked = HalfPlace::Within;
        t.check(parse_vector("36.0 i + 0.0 j km/h", &with_zero, &why) &&
                    to_si(with_zero, &converted) && parse_quantity("4.00 s", &elapsed, &why) &&
                    vector_scale(converted, elapsed, &drift, &why) &&
                    reported_vector_text(drift, &reported, checked) && reported == "40.0 i m",
                "and the figures it did not throw away survive conversion into the reported text");

        Vector left;
        Vector right;
        Vector difference;
        t.check(parse_vector("1.50 i + 2.50 j m", &left, &why) &&
                    parse_vector("1.50 i + 1.25 j m", &right, &why) &&
                    vector_sub(left, right, &difference, &why) && difference.x.num == 0 &&
                    difference.precision.significant_digits == 3 &&
                    difference.precision.last_significant_decimal_place == -2,
                "a component that cancels to zero claims no figures either, so the other axis keeps "
                "all three");

        Vector along_one_axis;
        Quantity reach;
        t.check(parse_vector("6.0 i + 0.0 j km", &along_one_axis, &why) &&
                    vector_magnitude(along_one_axis, &reach, &why) && reach.value.num == 6000 &&
                    reach.precision.significant_digits == 2 &&
                    reach.precision.last_significant_decimal_place == 2,
                "a magnitude reached through a squared zero keeps the figures the other axis wrote");
    }

    Vector rotated = vector_of("m", 1, 0);
    rotated.frame.name = "ramp";
    t.equal(added(vector_of("m", 3, 4), rotated),
            "refused: cannot add vectors in frame lab and frame ramp without an explicit basis "
            "transformation",
            "an implicit frame conversion is refused, which section 11.2 forbids outright");
    t.equal(dotted(vector_of("m", 3, 4), rotated),
            "refused: cannot take the dot product of vectors in frame lab and frame ramp without an "
            "explicit basis transformation",
            "and the dot product refuses the same way rather than quietly agreeing");
    t.equal(added(vector_of("m", 3, 4), vector_of("s", 1, 2)), "refused: cannot add L and T",
            "adding a length to a time is refused by dimension");
    t.equal(added(vector_of("m", 3, 4), vector_of("m", 1, 2, 3)),
            "refused: cannot add a vector of a different number of components",
            "and a plane vector does not add to a space one");

    t.equal(dotted(vector_of("m", 3, 4), vector_of("m", 2, 1)), "10 m^2",
            "the dot product is a scalar whose dimension is the product of the two");
    t.equal(dotted(vector_of("m", 3, 4), vector_of("", 1, 0)), "3 m",
            "a dot with a dimensionless direction keeps the dimension it had");
    t.equal(dotted(vector_of("m", 1, 0), vector_of("m", 0, 1)), "0 m^2",
            "perpendicular components give zero");

    t.equal(crossed(vector_of("m", 1, 0, 0), vector_of("m", 0, 1, 0)), "1 k m^2",
            "i cross j is k");
    t.equal(crossed(vector_of("m", 0, 1, 0), vector_of("m", 1, 0, 0)), "-1 k m^2",
            "and the other way round is its negative");
    t.equal(crossed(vector_of("m", 1, 0, 0), vector_of("kg m/s^2", 0, 2, 0)), "2 k kg m^2/s^2",
            "the two dimensions need not match, since a distance crossed with a force is a torque");
    t.equal(crossed(vector_of("m", 1, 0), vector_of("m", 0, 1)),
            "refused: the cross product is defined on three components and one of these has two",
            "and two components are not enough for it");

    t.equal(magnitude_of(vector_of("m", 3, 4)), "5 m", "a magnitude that is exact is given exactly");
    t.equal(magnitude_of(vector_of("m", 3, 4, 12)), "13 m", "in three components too");
    t.equal(magnitude_of(vector_of("m", 1, 1)),
            "refused: the magnitude of (1 i + 1 j) m is not an exact fraction",
            "and one that is irrational is refused rather than quietly rounded");

    t.equal(scaled(vector_of("m/s", 3, 4), "2 s"), "(6 i + 8 j) m",
            "a scalar multiplies every component and the dimensions multiply once");
    t.equal(scaled(vector_of("m/s", 3, 4), "-1"), "(-3 i - 4 j) m/s",
            "and a negative scalar reverses it");
    {
        Vector largest = vector_of("", 1, 0);
        largest.unit.dimension.length = std::numeric_limits<int>::max();
        Vector one = vector_of("", 1, 0);
        one.unit.dimension.length = 1;
        t.equal(dotted(largest, one), "refused: the product dimension does not fit",
                "a dot product propagates dimension overflow as a refusal");

        Vector largest_three = vector_of("", 1, 0, 0);
        largest_three.unit.dimension.length = std::numeric_limits<int>::max();
        Vector one_three = vector_of("", 0, 1, 0);
        one_three.unit.dimension.length = 1;
        t.equal(crossed(largest_three, one_three), "refused: the product dimension does not fit",
                "a cross product propagates dimension overflow as a refusal");

        Quantity scalar;
        scalar.value.num = 1;
        scalar.unit.scale.num = 1;
        scalar.unit.scale.den = 1;
        scalar.unit.dimension.length = 1;
        Vector scaled_output;
        std::string why;
        t.check(!vector_scale(largest, scalar, &scaled_output, &why) &&
                    why == "the product dimension does not fit",
                "vector scaling propagates dimension overflow as a refusal");
    }
    {
        const int64_t maximum = std::numeric_limits<int64_t>::max();
        const Vector too_large = vector_of("", maximum, 1);
        Quantity two;
        Quantity quantity_sentinel;
        std::string why;
        const bool parsed = parse_quantity("2", &two, &why) &&
                            parse_quantity("17.0 s", &quantity_sentinel, &why);
        Vector vector_sentinel = vector_of("s", 17, 19, 23);
        vector_sentinel.frame.name = "sentinel";
        vector_sentinel.precision = measured(3);

        Vector vector_output = vector_sentinel;
        t.check(parsed && !vector_scale(too_large, two, &vector_output, &why) &&
                    rational_equal(vector_output.x, vector_sentinel.x) &&
                    rational_equal(vector_output.y, vector_sentinel.y) &&
                    rational_equal(vector_output.z, vector_sentinel.z) &&
                    vector_output.rank == vector_sentinel.rank &&
                    vector_output.frame == vector_sentinel.frame &&
                    vector_output.unit.text == vector_sentinel.unit.text &&
                    vector_output.unit.dimension == vector_sentinel.unit.dimension &&
                    rational_equal(vector_output.unit.scale, vector_sentinel.unit.scale) &&
                    vector_output.precision.kind == vector_sentinel.precision.kind &&
                    vector_output.precision.significant_digits ==
                        vector_sentinel.precision.significant_digits &&
                    vector_output.precision.last_significant_decimal_place ==
                        vector_sentinel.precision.last_significant_decimal_place,
                "scaled-component overflow leaves the complete vector output unchanged");

        Quantity quantity_output = quantity_sentinel;
        const Vector double_first = vector_of("", 2, 1);
        t.check(parsed && !vector_dot(too_large, double_first, &quantity_output, &why) &&
                    rational_equal(quantity_output.value, quantity_sentinel.value) &&
                    quantity_output.unit.text == quantity_sentinel.unit.text &&
                    quantity_output.unit.dimension == quantity_sentinel.unit.dimension &&
                    rational_equal(quantity_output.unit.scale, quantity_sentinel.unit.scale) &&
                    quantity_output.precision.kind == quantity_sentinel.precision.kind &&
                    quantity_output.precision.significant_digits ==
                        quantity_sentinel.precision.significant_digits &&
                    quantity_output.precision.last_significant_decimal_place ==
                        quantity_sentinel.precision.last_significant_decimal_place,
                "dot-product term overflow leaves the complete quantity output unchanged");

        Vector cross_left = vector_of("", maximum, 0, 0);
        Vector cross_right = vector_of("", 0, 2, 0);
        vector_output = vector_sentinel;
        t.check(!vector_cross(cross_left, cross_right, &vector_output, &why) &&
                    rational_equal(vector_output.x, vector_sentinel.x) &&
                    rational_equal(vector_output.y, vector_sentinel.y) &&
                    rational_equal(vector_output.z, vector_sentinel.z) &&
                    vector_output.rank == vector_sentinel.rank &&
                    vector_output.frame == vector_sentinel.frame &&
                    vector_output.unit.text == vector_sentinel.unit.text &&
                    vector_output.unit.dimension == vector_sentinel.unit.dimension &&
                    rational_equal(vector_output.unit.scale, vector_sentinel.unit.scale) &&
                    vector_output.precision.kind == vector_sentinel.precision.kind &&
                    vector_output.precision.significant_digits ==
                        vector_sentinel.precision.significant_digits &&
                    vector_output.precision.last_significant_decimal_place ==
                        vector_sentinel.precision.last_significant_decimal_place,
                "late cross-product overflow leaves the complete vector output unchanged");

        quantity_output = quantity_sentinel;
        t.check(!vector_magnitude(too_large, &quantity_output, &why) &&
                    rational_equal(quantity_output.value, quantity_sentinel.value) &&
                    quantity_output.unit.text == quantity_sentinel.unit.text &&
                    quantity_output.unit.dimension == quantity_sentinel.unit.dimension &&
                    rational_equal(quantity_output.unit.scale, quantity_sentinel.unit.scale) &&
                    quantity_output.precision.kind == quantity_sentinel.precision.kind &&
                    quantity_output.precision.significant_digits ==
                        quantity_sentinel.precision.significant_digits &&
                    quantity_output.precision.last_significant_decimal_place ==
                        quantity_sentinel.precision.last_significant_decimal_place,
                "magnitude square overflow leaves the complete quantity output unchanged");
    }

    Vector one_figure = vector_of("m", 3, 4);
    one_figure.precision = measured(2);
    Vector two_figures = vector_of("m", 1, 2);
    two_figures.precision = measured(3);
    Vector sum;
    std::string why;
    t.check(vector_add(one_figure, two_figures, &sum, &why), "two measured vectors add");
    t.equal(precision_text(sum.precision), "2",
            "and the sum is entitled to the fewer of the two figure counts");
    t.check(vector_add(one_figure, vector_of("m", 1, 2), &sum, &why),
            "a measured vector adds to an exact one");
    t.equal(precision_text(sum.precision), "2", "which limits nothing, so the count is unchanged");
    t.equal(precision_text(precision_combine(Precision(), Precision())), "exact",
            "two exact values stay exact");

    t.equal(reread("3 i + 4 j m/s"), "(3 i + 4 j) m/s", "unit-vector entry reads back as it went in");
    t.equal(reread("(3, 4) m/s"), "(3 i + 4 j) m/s",
            "and the tuple form gives the same vector, printed in unit-vector form rather than echoed");
    t.equal(reread("(3, 4, 5) m"), "(3 i + 4 j + 5 k) m", "a three component tuple reaches k");
    t.equal(reread("(3 i + 4 j) m/s"), "(3 i + 4 j) m/s", "the printed form is itself readable");
    t.equal(reread("3 i - 4 j m/s"), "(3 i - 4 j) m/s", "a minus between terms is a negative one");
    t.equal(reread("-4 j m"), "-4 j m", "a leading minus applies to the term it is on");
    t.equal(reread("(-3, -4) m"), "(-3 i - 4 j) m", "a tuple carries its signs too");
    t.equal(reread("i + 2 j"), "1 i + 2 j", "a term with no number is one of that axis");
    t.equal(reread("4 k m"), "4 k m", "a k term on its own is still a vector");
    t.equal(read_rank("4 k m"), "3", "and it has three components, since k is the third");
    t.equal(read_rank("3 i + 4 j m"), "2", "where i and j alone stay in the plane");
    t.equal(reread("2 i + 3 i m"), "refused: the i component is given twice",
            "a repeated axis is refused rather than added to itself");
    t.equal(reread("3 i + 4"), "refused: a vector term is a number and then i, j or k",
            "a term with no axis is not a term");
    t.equal(reread("3 min"), "refused: a vector term is a number and then i, j or k",
            "and a unit that starts with an axis letter is not one either");
    t.equal(reread("(3) m"), "refused: a vector has two or three components, not one",
            "one component is a scalar written strangely");
    t.equal(reread("(1, 2, 3, 4) m"), "refused: a vector has two or three components, not more",
            "and four is not a thing this models");
    t.equal(reread("(3, 4 m"), "refused: a bracketed vector needs its closing bracket",
            "an unclosed bracket is refused before the unit is even looked at");
    t.equal(reread("3 i + 4 j parsec"), "refused: unknown unit parsec",
            "the unit is read by the same table as a scalar, and names what it did not know");

    Vector measured_entry;
    std::string why_measured;
    t.check(parse_vector("3.0 i + 4.00 j m", &measured_entry, &why_measured),
            "a vector with decimal components parses");
    t.equal(precision_text(measured_entry.precision), "2",
            "and takes the fewest figures among them, the same rule a scalar follows");
    Vector counted;
    t.check(parse_vector("3 i + 4 j m", &counted, &why_measured), "one written without points too");
    t.equal(precision_text(counted.precision), "exact", "which is a count and limits nothing");
    t.equal(counted.frame.name, default_frame_name(),
            "an entered vector is in the problem's frame, never in no frame at all");

    // PHYS-012 and PHYS-013's additional-units clause: charge, potential, resistance and
    // capacitance are not expressible in length, mass and time alone.
    t.equal(unit_dimension("A"), "I", "current is a base dimension of its own");
    t.equal(unit_dimension("C"), "T I", "a coulomb is an ampere second");
    t.equal(unit_dimension("V"), "L^2 M T^-3 I^-1", "a volt is energy per unit charge");
    t.equal(unit_dimension("ohm"), "L^2 M T^-3 I^-2", "an ohm is volts per ampere");
    t.equal(unit_dimension("W"), "L^2 M T^-3", "a watt is joules per second and carries no current");
    t.equal(unit_dimension("F"), "L^-2 M^-1 T^4 I^2", "a farad is coulombs per volt");
    t.equal(unit_dimension("V/A"), "L^2 M T^-3 I^-2",
            "and the ohm agrees with the quotient it is defined as");
    t.equal(unit_dimension("J/C"), "L^2 M T^-3 I^-1", "as the volt agrees with joules per coulomb");
    t.equal(unit_dimension("V*A"), "L^2 M T^-3", "and the watt with volt amperes");
    t.equal(unit_scale("kohm"), "1000", "the prefixed spellings scale to the SI unit exactly");
    t.equal(unit_scale("mA"), "0.001", "including the ones below it");
    t.equal(unit_scale("uC"), "0.000001", "and the microcoulomb a charge problem is written in");
    t.equal(si_value("2.5 kohm"), "2500 kg m^2/(s^3 A^2)",
            "a resistance converts to its SI unit and reports it");
    t.equal(si_value("12 mV"), "0.012 kg m^2/(s^3 A)", "as a potential does");
    // A division applies to the one factor after it, so a denominator of two factors has to be
    // bracketed or the SI spelling reads back as multiplied by the second one.
    t.equal(unit_dimension(si_unit_text(parse_dimension("ohm"))), "L^2 M T^-3 I^-2",
            "and the SI spelling of an ohm parses back to the dimension it came from");
    t.equal(unit_dimension(si_unit_text(parse_dimension("V"))), "L^2 M T^-3 I^-1",
            "as the spelling of a volt does");
    t.equal(unit_dimension(si_unit_text(parse_dimension("m/s^2"))), "L T^-2",
            "while a single denominator factor stays unbracketed and still round-trips");
    t.equal(si_unit_text(parse_dimension("s^-1")), "1/s",
            "a dimension with nothing above the line is spelled with a one there");
    t.equal(unit_dimension("1/s"), "T^-1",
            "and the parser reads that spelling back rather than refusing what it wrote");
    t.equal(unit_dimension("1"), "1", "as it does a bare one, which is the dimensionless spelling");
    t.equal(unit_dimension(si_unit_text(parse_dimension("s^-1 A^-1"))), "T^-1 I^-1",
            "a bracketed denominator under a one round-trips too");
    t.equal(unit_dimension("12"), "refused: unexpected character in a unit: 2",
            "and a number that is not one is still not a unit");
}

}  // namespace nps
