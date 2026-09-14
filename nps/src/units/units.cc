#include "nps/units/units.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <vector>

namespace nps {
namespace {

struct BaseUnit {
    const char *symbol;
    Dimension dimension;
    int64_t scale_num;
    int64_t scale_den;
};

Dimension length_dimension() {
    Dimension d;
    d.length = 1;
    return d;
}

Dimension mass_dimension() {
    Dimension d;
    d.mass = 1;
    return d;
}

Dimension time_dimension() {
    Dimension d;
    d.time = 1;
    return d;
}

Dimension force_dimension() {
    Dimension d;
    d.length = 1;
    d.mass = 1;
    d.time = -2;
    return d;
}

Dimension energy_dimension() {
    Dimension d;
    d.length = 2;
    d.mass = 1;
    d.time = -2;
    return d;
}

const BaseUnit *base_units(size_t *count) {
    static const BaseUnit table[] = {
        {"m", length_dimension(), 1, 1},        {"km", length_dimension(), 1000, 1},
        {"cm", length_dimension(), 1, 100},     {"mm", length_dimension(), 1, 1000},
        {"s", time_dimension(), 1, 1},          {"ms", time_dimension(), 1, 1000},
        {"min", time_dimension(), 60, 1},       {"h", time_dimension(), 3600, 1},
        {"kg", mass_dimension(), 1, 1},         {"g", mass_dimension(), 1, 1000},
        {"N", force_dimension(), 1, 1},         {"J", energy_dimension(), 1, 1},
    };
    *count = sizeof(table) / sizeof(table[0]);
    return table;
}

const BaseUnit *find_base(const std::string &symbol) {
    size_t count;
    const BaseUnit *table = base_units(&count);
    for (size_t i = 0; i < count; ++i) {
        if (symbol == table[i].symbol)
            return &table[i];
    }
    return nullptr;
}

bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string exponent_text(int64_t e) {
    return (e < 0 ? "-" : "") + magnitude_text(magnitude(e));
}

std::string with_power(const std::string &symbol, int64_t e) {
    if (e == 1)
        return symbol;
    return symbol + "^" + exponent_text(e);
}

// Applies a powered factor atomically, refusing dimension or scale overflow.
enum class ApplyUnitOutcome : uint8_t {
    Applied,
    DimensionOverflow,
    ScaleOverflow,
};

ApplyUnitOutcome apply_unit(Unit *unit, const Dimension &dimension, const Rational &scale,
                            int exponent) {
    Dimension next_dimension;
    Dimension powered_dimension;
    if (!dimension_power(dimension, exponent, &powered_dimension) ||
        !dimension_multiply(unit->dimension, powered_dimension, &next_dimension))
        return ApplyUnitOutcome::DimensionOverflow;
    Rational powered_scale;
    Rational next_scale;
    if (!rational_power(scale, exponent, &powered_scale) ||
        !rational_mul(unit->scale, powered_scale, &next_scale))
        return ApplyUnitOutcome::ScaleOverflow;
    unit->dimension = next_dimension;
    unit->scale = next_scale;
    return ApplyUnitOutcome::Applied;
}

// The unit grammar, scanned rather than matched: factors joined by a product, a division or a
// space, where a factor is a symbol or a bracketed sequence, and either may carry an exponent.
//
// Brackets are here because the calculator's own 2D editor is where units arrive from. Typing
// "5 m/s" into a math box and reading it back gives "5 ((m)/(s))", and "3 m/s^2" gives
// "3 ((m)/(s^(2)))". Skipping brackets as if they were spaces would accept both and read
// "m/(s*kg)" as m per second times kilograms, which is a wrong answer where a refusal used to be.
// A division applies to the one factor after it, and a bracketed group is one factor.
struct UnitScanner {
    UnitScanner(const std::string &t, std::string *e) : text(t), error(e) {}

    const std::string &text;
    size_t at = 0;
    std::string *error;

    void skip_blank() {
        while (at < text.size() && text[at] == ' ')
            ++at;
    }

    // The exponent after a factor, absent meaning one. The editor brackets it, so ^2 and ^(2) are
    // the same exponent and ^-1 and ^(-1) are the same one.
    bool exponent(int *out) {
        *out = 1;
        skip_blank();
        if (at >= text.size() || text[at] != '^')
            return true;
        ++at;
        skip_blank();
        const bool bracketed = at < text.size() && text[at] == '(';
        if (bracketed)
            ++at;
        bool negative = false;
        if (at < text.size() && text[at] == '-') {
            negative = true;
            ++at;
        }
        if (at >= text.size() || !is_digit(text[at])) {
            *error = "a unit exponent needs an integer after ^";
            return false;
        }
        const size_t digit_start = at;
        while (at < text.size() && is_digit(text[at]))
            ++at;
        int value = 0;
        const auto conversion =
            std::from_chars(text.data() + digit_start, text.data() + at, value);
        if (conversion.ec != std::errc() || conversion.ptr != text.data() + at || value > 99) {
            *error = "a unit exponent that large is not a unit";
            return false;
        }
        if (bracketed) {
            skip_blank();
            if (at >= text.size() || text[at] != ')') {
                *error = "an unclosed bracket in a unit exponent";
                return false;
            }
            ++at;
        }
        *out = negative ? -value : value;
        return true;
    }

    // One factor, handed back as what it contributes rather than applied here, so the caller can
    // raise it and flip its sign for a division in one place.
    bool factor(Dimension *dimension, Rational *scale, int *power) {
        skip_blank();
        if (at < text.size() && text[at] == '(') {
            ++at;
            Unit inner;
            inner.scale.num = 1;
            inner.scale.den = 1;
            if (!sequence(&inner, ')'))
                return false;
            if (at >= text.size() || text[at] != ')') {
                *error = "an unclosed bracket in a unit";
                return false;
            }
            ++at;
            *dimension = inner.dimension;
            *scale = inner.scale;
            return exponent(power);
        }
        if (at >= text.size()) {
            *error = "a unit ends with nothing after its last operator";
            return false;
        }
        if (!is_letter(text[at])) {
            *error = std::string("unexpected character in a unit: ") + text[at];
            return false;
        }
        const size_t start = at;
        while (at < text.size() && is_letter(text[at]))
            ++at;
        const std::string symbol = text.substr(start, at - start);
        const BaseUnit *base = find_base(symbol);
        if (!base) {
            *error = "unknown unit " + symbol;
            return false;
        }
        *dimension = base->dimension;
        scale->num = base->scale_num;
        scale->den = base->scale_den;
        return exponent(power);
    }

    // Factors up to stop, which is a closing bracket inside a group and nothing at the top level.
    bool sequence(Unit *unit, char stop) {
        bool any = false;
        bool dividing = false;
        bool operator_pending = false;
        while (at < text.size()) {
            skip_blank();
            if (at >= text.size() || text[at] == stop)
                break;
            if (text[at] == '*' || text[at] == '/') {
                if (operator_pending) {
                    *error = "two operators in a row in a unit";
                    return false;
                }
                if (text[at] == '/') {
                    if (!any) {
                        *error = "a unit cannot start with a division";
                        return false;
                    }
                    dividing = true;
                }
                ++at;
                operator_pending = true;
                continue;
            }
            Dimension dimension;
            Rational scale;
            int power = 1;
            if (!factor(&dimension, &scale, &power))
                return false;
            const ApplyUnitOutcome applied =
                apply_unit(unit, dimension, scale, dividing ? -power : power);
            if (applied == ApplyUnitOutcome::DimensionOverflow) {
                *error = "the unit dimension for " + text + " does not fit";
                return false;
            }
            if (applied == ApplyUnitOutcome::ScaleOverflow) {
                *error = "the conversion factor for " + text + " does not fit";
                return false;
            }
            dividing = false;
            operator_pending = false;
            any = true;
        }
        if (!any) {
            *error = "no unit given";
            return false;
        }
        // An operator with nothing after it. The loop above consumes the operator and then finds
        // the text exhausted, so without this "5 m/" reads as five metres and a wrong dimension
        // reaches the solve instead of a refusal.
        if (operator_pending) {
            *error = "a unit ends with nothing after its last operator";
            return false;
        }
        return true;
    }
};

// Precision comes from spelling: integers are exact, decimals are measured, and written zero places count.
Precision precision_of_literal(const std::string &number) {
    Precision p;
    bool point = false;
    bool started = false;
    uint16_t significant = 0;
    uint16_t zero_decimal_places = 0;
    int32_t decimal_places = 0;
    for (size_t c = 0; c < number.size(); ++c) {
        if (number[c] == '.') {
            point = true;
            continue;
        }
        if (number[c] == '-')
            continue;
        if (point)
            --decimal_places;
        if (!started && number[c] == '0') {
            if (point)
                ++zero_decimal_places;
            continue;
        }
        started = true;
        ++significant;
    }
    if (point) {
        p.kind = NumberKind::Measured;
        p.significant_digits = started ? significant
                                       : (zero_decimal_places == 0 ? 1 : zero_decimal_places);
        p.last_significant_decimal_place = decimal_places;
    }
    return p;
}

}  // namespace

bool operator==(const Dimension &a, const Dimension &b) {
    return a.length == b.length && a.mass == b.mass && a.time == b.time;
}

bool operator!=(const Dimension &a, const Dimension &b) { return !(a == b); }

bool dimension_multiply(const Dimension &a, const Dimension &b, Dimension *out) {
    int64_t length;
    int64_t mass;
    int64_t time;
    if (!add_checked(static_cast<int64_t>(a.length), static_cast<int64_t>(b.length), &length) ||
        !add_checked(static_cast<int64_t>(a.mass), static_cast<int64_t>(b.mass), &mass) ||
        !add_checked(static_cast<int64_t>(a.time), static_cast<int64_t>(b.time), &time) ||
        length < std::numeric_limits<int>::min() || length > std::numeric_limits<int>::max() ||
        mass < std::numeric_limits<int>::min() || mass > std::numeric_limits<int>::max() ||
        time < std::numeric_limits<int>::min() || time > std::numeric_limits<int>::max())
        return false;
    Dimension product;
    product.length = static_cast<int>(length);
    product.mass = static_cast<int>(mass);
    product.time = static_cast<int>(time);
    *out = product;
    return true;
}

bool dimension_power(const Dimension &a, int exponent, Dimension *out) {
    int64_t length;
    int64_t mass;
    int64_t time;
    if (!mul_checked(static_cast<int64_t>(a.length), static_cast<int64_t>(exponent), &length) ||
        !mul_checked(static_cast<int64_t>(a.mass), static_cast<int64_t>(exponent), &mass) ||
        !mul_checked(static_cast<int64_t>(a.time), static_cast<int64_t>(exponent), &time) ||
        length < std::numeric_limits<int>::min() || length > std::numeric_limits<int>::max() ||
        mass < std::numeric_limits<int>::min() || mass > std::numeric_limits<int>::max() ||
        time < std::numeric_limits<int>::min() || time > std::numeric_limits<int>::max())
        return false;
    Dimension powered;
    powered.length = static_cast<int>(length);
    powered.mass = static_cast<int>(mass);
    powered.time = static_cast<int>(time);
    *out = powered;
    return true;
}

std::string dimension_text(const Dimension &d) {
    std::string out;
    const char *names[3] = {"L", "M", "T"};
    const int powers[3] = {d.length, d.mass, d.time};
    for (int i = 0; i < 3; ++i) {
        if (powers[i] == 0)
            continue;
        if (!out.empty())
            out += " ";
        out += with_power(names[i], powers[i]);
    }
    return out.empty() ? "1" : out;
}

std::string si_unit_text(const Dimension &d) {
    const char *names[3] = {"kg", "m", "s"};
    const int powers[3] = {d.mass, d.length, d.time};
    std::string top;
    std::string bottom;
    for (int i = 0; i < 3; ++i) {
        if (powers[i] > 0) {
            if (!top.empty())
                top += " ";
            top += with_power(names[i], powers[i]);
        } else if (powers[i] < 0) {
            if (!bottom.empty())
                bottom += " ";
            bottom += with_power(names[i], -static_cast<int64_t>(powers[i]));
        }
    }
    if (top.empty() && bottom.empty())
        return "1";
    if (bottom.empty())
        return top;
    if (top.empty())
        top = "1";
    return top + "/" + bottom;
}

bool parse_unit(const std::string &text, Unit *out, std::string *error) {
    Unit unit;
    unit.text = text;
    unit.scale.num = 1;
    unit.scale.den = 1;

    UnitScanner scanner(text, error);
    // A division applies to the one factor after it, so m/s^2 is m s^-2 and m/s*s is m.
    if (!scanner.sequence(&unit, '\0'))
        return false;
    if (scanner.at != text.size()) {
        *error = std::string("unexpected character in a unit: ") + text[scanner.at];
        return false;
    }
    *out = std::move(unit);
    return true;
}

bool parse_quantity(const std::string &text, Quantity *out, std::string *error) {
    size_t i = 0;
    while (i < text.size() && text[i] == ' ')
        ++i;
    size_t start = i;
    if (i < text.size() && text[i] == '-')
        ++i;
    while (i < text.size() && (is_digit(text[i]) || text[i] == '.'))
        ++i;
    const std::string number = text.substr(start, i - start);
    Quantity q;
    if (!rational_from_text(number, &q.value)) {
        *error = number.empty() ? "a quantity starts with a number"
                                : "not a number this reads exactly: " + number;
        return false;
    }
    // Counted from the spelling rather than in rational_from_text, because it is a property of how
    // the number was written and that routine wants the value alone.
    q.precision = precision_of_literal(number);
    while (i < text.size() && text[i] == ' ')
        ++i;
    size_t end = text.size();
    while (end > i && text[end - 1] == ' ')
        --end;
    const std::string unit_text = text.substr(i, end - i);
    if (unit_text.empty()) {
        q.unit.text = "";
        q.unit.scale.num = 1;
        q.unit.scale.den = 1;
    } else if (!parse_unit(unit_text, &q.unit, error)) {
        return false;
    }
    *out = std::move(q);
    return true;
}

bool to_si(const Quantity &q, Rational *value) {
    return rational_mul(q.value, q.unit.scale, value);
}

namespace {

bool axis_index(char c, uint8_t *axis) {
    if (c == 'i')
        *axis = 0;
    else if (c == 'j')
        *axis = 1;
    else if (c == 'k')
        *axis = 2;
    else
        return false;
    return true;
}

// An axis letter is one letter. Anything else attached to it is a unit that happens to start with
// the same letter, so the term is not a term and the caller says so.
bool axis_letter_ends(const std::string &text, size_t at) {
    if (at >= text.size())
        return true;
    const char c = text[at];
    return c == ' ' || c == '+' || c == '-' || c == ')' || c == ',';
}

void skip_spaces(const std::string &text, size_t *at) {
    while (*at < text.size() && text[*at] == ' ')
        ++*at;
}

// The digits of one number, which is the spelling parse_quantity reads, without its sign.
std::string read_digits(const std::string &text, size_t *at) {
    const size_t start = *at;
    while (*at < text.size() && (is_digit(text[*at]) || text[*at] == '.'))
        ++*at;
    return text.substr(start, *at - start);
}

struct Components {
    Rational value[3];
    bool given[3] = {false, false, false};
    uint8_t highest = 0;
    Precision precision;
};

bool place(Components *c, uint8_t axis, const Rational &value, std::string *error) {
    if (c->given[axis]) {
        *error = std::string("the ") + "ijk"[axis] + " component is given twice";
        return false;
    }
    c->given[axis] = true;
    c->value[axis] = value;
    if (axis > c->highest)
        c->highest = axis;
    return true;
}

// "3 i + 4 j", "-4 j", "i + 2 k". Stops at the first thing that is not another term, which is where
// the unit starts.
bool read_terms(const std::string &text, size_t *at, Components *c, std::string *error) {
    bool any = false;
    for (;;) {
        skip_spaces(text, at);
        int sign = 1;
        if (any) {
            if (*at >= text.size() || (text[*at] != '+' && text[*at] != '-'))
                return true;
            sign = text[*at] == '-' ? -1 : 1;
            ++*at;
            skip_spaces(text, at);
        } else if (*at < text.size() && text[*at] == '-') {
            sign = -1;
            ++*at;
            skip_spaces(text, at);
        }
        const std::string number = read_digits(text, at);
        Rational value;
        value.num = 1;
        if (!number.empty()) {
            if (!rational_from_text(number, &value)) {
                *error = "not a number this reads exactly: " + number;
                return false;
            }
            c->precision = precision_combine(c->precision, precision_of_literal(number));
        }
        if (sign < 0 && !negate_fraction(value.num, value.den, &value.num, &value.den)) {
            *error = "a component does not fit an exact fraction";
            return false;
        }
        skip_spaces(text, at);
        uint8_t axis = 0;
        if (*at >= text.size() || !axis_index(text[*at], &axis) || !axis_letter_ends(text, *at + 1)) {
            *error = "a vector term is a number and then i, j or k";
            return false;
        }
        ++*at;
        if (!place(c, axis, value, error))
            return false;
        any = true;
    }
}

// "3, 4" or "3, 4, 5", already inside the brackets. Two or three, because a vector of one component
// is a scalar written strangely and one of four is not a thing this models.
bool read_tuple(const std::string &text, size_t *at, Components *c, std::string *error) {
    uint8_t axis = 0;
    for (;;) {
        skip_spaces(text, at);
        int sign = 1;
        if (*at < text.size() && text[*at] == '-') {
            sign = -1;
            ++*at;
            skip_spaces(text, at);
        }
        const std::string number = read_digits(text, at);
        Rational value;
        if (number.empty() || !rational_from_text(number, &value)) {
            *error = "a tuple component is a number";
            return false;
        }
        c->precision = precision_combine(c->precision, precision_of_literal(number));
        if (sign < 0 && !negate_fraction(value.num, value.den, &value.num, &value.den)) {
            *error = "a component does not fit an exact fraction";
            return false;
        }
        if (axis > 2) {
            *error = "a vector has two or three components, not more";
            return false;
        }
        if (!place(c, axis, value, error))
            return false;
        ++axis;
        skip_spaces(text, at);
        if (*at < text.size() && text[*at] == ',') {
            ++*at;
            continue;
        }
        if (axis < 2) {
            *error = "a vector has two or three components, not one";
            return false;
        }
        return true;
    }
}

// Which of the two bracketed forms this is, looked at rather than guessed, since both open the same
// way. A comma settles it, and so does the absence of any axis letter: "(3)" is a tuple missing a
// component rather than a term missing its axis, and saying so is the more useful of the two.
bool bracketed_tuple(const std::string &text, size_t open) {
    uint8_t axis = 0;
    for (size_t i = open + 1; i < text.size() && text[i] != ')'; ++i) {
        if (text[i] == ',')
            return true;
        if (axis_index(text[i], &axis) && axis_letter_ends(text, i + 1))
            return false;
    }
    return true;
}

}  // namespace

bool parse_vector(const std::string &text, Vector *out, std::string *error) {
    size_t at = 0;
    skip_spaces(text, &at);
    Components c;
    if (at < text.size() && text[at] == '(') {
        const size_t open = at;
        ++at;
        const bool ok = bracketed_tuple(text, open) ? read_tuple(text, &at, &c, error)
                                                    : read_terms(text, &at, &c, error);
        if (!ok)
            return false;
        skip_spaces(text, &at);
        if (at >= text.size() || text[at] != ')') {
            *error = "a bracketed vector needs its closing bracket";
            return false;
        }
        ++at;
    } else if (!read_terms(text, &at, &c, error)) {
        return false;
    }

    Vector v;
    v.x = c.value[0];
    v.y = c.value[1];
    v.z = c.value[2];
    v.rank = c.highest > 1 ? 3 : 2;
    v.frame.name = default_frame_name();
    v.precision = c.precision;
    v.unit.scale.num = 1;
    v.unit.scale.den = 1;
    skip_spaces(text, &at);
    size_t end = text.size();
    while (end > at && text[end - 1] == ' ')
        --end;
    const std::string unit_text = text.substr(at, end - at);
    if (!unit_text.empty() && !parse_unit(unit_text, &v.unit, error))
        return false;
    *out = std::move(v);
    return true;
}

Precision precision_combine(const Precision &a, const Precision &b) {
    if (a.kind == NumberKind::Exact)
        return b;
    if (b.kind == NumberKind::Exact)
        return a;
    Precision p;
    p.kind = NumberKind::Measured;
    p.significant_digits = std::min(a.significant_digits, b.significant_digits);
    p.last_significant_decimal_place =
        std::max(a.last_significant_decimal_place, b.last_significant_decimal_place);
    return p;
}

namespace {

int leading_decimal_place(const Rational &value) {
    int place = 0;
    if (!rational_leading_decimal_place(value, &place))
        return 0;
    return place;
}

bool decimal_place_unit(int32_t place, Rational *unit) {
    constexpr int32_t kLargestExactDecimalPlace = 18;
    if (place < -kLargestExactDecimalPlace || place > kLargestExactDecimalPlace)
        return false;
    return rational_power(Rational{10, 1}, place, unit);
}

// The leading decimal place of value once it is rounded at place. A rounding that carries, as 9.96
// does at the tenths place, lands one place above the exact value's leading digit.
int rounded_leading_decimal_place(const Rational &value, int32_t place) {
    const int lead = leading_decimal_place(value);
    Rational unit;
    if (value.num == 0 || place > lead || !decimal_place_unit(place, &unit))
        return lead;
    detail::Mpq reach;
    detail::Mpq half_unit;
    if (!detail::mpq_set_rational(reach.get(), value) ||
        !detail::mpq_set_rational(half_unit.get(), unit))
        return lead;
    mpq_abs(reach.get(), reach.get());
    mpq_div_2exp(half_unit.get(), half_unit.get(), 1);
    mpq_add(reach.get(), reach.get(), half_unit.get());
    const int above = lead + 1;
    const int below = -above;
    detail::Mpz power;
    detail::Mpq next_place;
    if (above >= 0) {
        detail::mpz_pow10(power.get(), static_cast<unsigned long>(above));
        mpq_set_z(next_place.get(), power.get());
    } else {
        detail::mpz_pow10(power.get(), static_cast<unsigned long>(below));
        mpq_set_ui(next_place.get(), 1, 1);
        mpq_set_den(next_place.get(), power.get());
    }
    return mpq_cmp(reach.get(), next_place.get()) >= 0 ? above : lead;
}

Precision precision_at_value(const Rational &value, Precision precision) {
    if (precision.kind == NumberKind::Exact)
        return precision;
    if (value.num == 0) {
        const int64_t digits = precision.last_significant_decimal_place < 0
                                   ? -static_cast<int64_t>(
                                         precision.last_significant_decimal_place)
                                   : 1;
        precision.significant_digits =
            digits > std::numeric_limits<uint16_t>::max()
                ? std::numeric_limits<uint16_t>::max()
                : static_cast<uint16_t>(digits);
        return precision;
    }
    const int64_t digits =
        static_cast<int64_t>(
            rounded_leading_decimal_place(value, precision.last_significant_decimal_place)) -
        static_cast<int64_t>(precision.last_significant_decimal_place) + 1;
    precision.significant_digits =
        digits <= 0 ? 1
                    : (digits > std::numeric_limits<uint16_t>::max()
                           ? std::numeric_limits<uint16_t>::max()
                           : static_cast<uint16_t>(digits));
    return precision;
}

}  // namespace

Precision precision_at_digits(const Rational &value, Precision precision) {
    if (precision.kind != NumberKind::Measured || value.num == 0)
        return precision;
    const int32_t place = static_cast<int32_t>(leading_decimal_place(value)) -
                          static_cast<int32_t>(precision.significant_digits) + 1;
    precision.last_significant_decimal_place =
        static_cast<int32_t>(rounded_leading_decimal_place(value, place)) -
        static_cast<int32_t>(precision.significant_digits) + 1;
    return precision;
}

Precision precision_product(const Rational &value, const Rational &a_value, const Precision &a,
                            const Rational &b_value, const Precision &b) {
    if ((a.kind == NumberKind::Exact && a_value.num == 0) ||
        (b.kind == NumberKind::Exact && b_value.num == 0)) {
        return Precision();
    }
    Precision precision = precision_combine(a, b);
    if (precision.kind == NumberKind::Measured && value.num != 0) {
        precision = precision_at_digits(value, precision);
    } else if (precision.kind == NumberKind::Measured) {
        const Rational *exact_factor = nullptr;
        if (a.kind == NumberKind::Measured && a_value.num == 0 && b.kind == NumberKind::Exact &&
            b_value.num != 0) {
            exact_factor = &b_value;
        } else if (b.kind == NumberKind::Measured && b_value.num == 0 &&
                   a.kind == NumberKind::Exact && a_value.num != 0) {
            exact_factor = &a_value;
        }
        if (exact_factor) {
            const int64_t shifted =
                static_cast<int64_t>(precision.last_significant_decimal_place) +
                static_cast<int64_t>(leading_decimal_place(*exact_factor));
            precision.last_significant_decimal_place = static_cast<int32_t>(std::max<int64_t>(
                std::numeric_limits<int32_t>::min(),
                std::min<int64_t>(std::numeric_limits<int32_t>::max(), shifted)));
        }
    }
    return precision;
}

Precision precision_sum(const Rational &value, const Precision &a, const Precision &b) {
    if (a.kind == NumberKind::Exact)
        return precision_at_value(value, b);
    if (b.kind == NumberKind::Exact)
        return precision_at_value(value, a);
    Precision precision;
    precision.kind = NumberKind::Measured;
    precision.last_significant_decimal_place =
        std::max(a.last_significant_decimal_place, b.last_significant_decimal_place);
    return precision_at_value(value, precision);
}

bool to_si(const Vector &v, Vector *out) {
    if (v.rank != 2 && v.rank != 3)
        return false;

    const Rational components[3] = {v.x, v.y, v.z};
    Rational converted_components[3];
    for (uint8_t axis = 0; axis < v.rank; ++axis) {
        if (!rational_mul(components[axis], v.unit.scale, &converted_components[axis]))
            return false;
    }

    Vector converted = v;
    converted.x = converted_components[0];
    converted.y = converted_components[1];
    converted.z = v.rank == 3 ? converted_components[2] : Rational();
    converted.unit.text = si_unit_text(v.unit.dimension);
    converted.unit.scale = Rational{1, 1};
    if (!rational_equal(v.unit.scale, Rational{1, 1})) {
        converted.precision = Precision();
        for (uint8_t axis = 0; axis < v.rank; ++axis) {
            const Precision component_precision =
                precision_product(converted_components[axis], components[axis], v.precision,
                                  v.unit.scale, Precision());
            converted.precision = precision_combine(converted.precision, component_precision);
        }
    }
    *out = std::move(converted);
    return true;
}

bool precision_rounded_text(const Rational &value, const Precision &precision, std::string *out) {
    if (precision.kind == NumberKind::Exact) {
        *out = rational_text(value);
        return true;
    }
    const int32_t place = precision.last_significant_decimal_place;
    Rational unit;
    if (!decimal_place_unit(place, &unit))
        return false;
    // Rounded at the declared place, half away from zero, and written with every place down to it.
    // Rounding to a digit count instead lost the last place when the rounding carried: 9.96 at the
    // tenths place is 10.0, and a two-figure rounder wrote 10.
    detail::Mpq scaled;
    detail::Mpq unit_value;
    if (!detail::mpq_set_rational(scaled.get(), value) ||
        !detail::mpq_set_rational(unit_value.get(), unit)) {
        return false;
    }
    mpq_abs(scaled.get(), scaled.get());
    mpq_div(scaled.get(), scaled.get(), unit_value.get());
    detail::Mpz rounded;
    detail::Mpz remainder;
    detail::Mpz twice_remainder;
    mpz_fdiv_qr(rounded.get(), remainder.get(), mpq_numref(scaled.get()),
                mpq_denref(scaled.get()));
    mpz_mul_2exp(twice_remainder.get(), remainder.get(), 1);
    if (mpz_cmp(twice_remainder.get(), mpq_denref(scaled.get())) >= 0)
        mpz_add_ui(rounded.get(), rounded.get(), 1);
    const bool zero = mpz_sgn(rounded.get()) == 0;
    std::string body = detail::mpz_text(rounded.get());
    if (place >= 0) {
        if (!zero)
            body.append(static_cast<size_t>(place), '0');
    } else {
        const size_t places = static_cast<size_t>(-place);
        if (body.size() <= places)
            body.insert(0, places + 1 - body.size(), '0');
        body.insert(body.size() - places, 1, '.');
    }
    *out = (value.num < 0 && !zero ? "-" : "") + body;
    return true;
}

HalfPlace precision_rounding_valid(const Rational &exact, const std::string &reported,
                                   const Precision &precision) {
    // The reported text is read straight into an exact value. Narrowing it to an int64 Rational
    // first refused numerals wider than int64 whose rounding was correct, and that refusal arrived
    // as the same false a genuine disagreement did.
    detail::Mpq reported_value;
    detail::Mpq exact_value;
    if (!detail::mpq_from_text(reported_value.get(), reported, reported.size()) ||
        !detail::mpq_set_rational(exact_value.get(), exact)) {
        return HalfPlace::Unreadable;
    }
    // The unit comes from the declared place, and is built exact rather than through a Rational,
    // so the comparison reaches every place a printer can write instead of stopping where int64
    // does. Narrowing the reported value the same way refused correct roundings once already.
    detail::Mpq unit_value;
    if (!detail::mpq_decimal_place_unit(unit_value.get(),
                                        precision.last_significant_decimal_place)) {
        return HalfPlace::Unreadable;
    }
    return detail::half_place_compare(exact_value.get(), reported_value.get(), unit_value.get());
}

bool operator==(const Frame &a, const Frame &b) { return a.name == b.name; }

bool operator!=(const Frame &a, const Frame &b) { return !(a == b); }

const char *default_frame_name() { return "lab"; }

namespace {

// The unit a vector prints with. An entered vector keeps the spelling it was typed in, and one the
// engine built has none, so it takes the SI spelling of the dimension its components are in.
std::string vector_unit_text(const Vector &v) {
    return v.unit.text.empty() ? si_unit_text(v.unit.dimension) : v.unit.text;
}

// The reason two vectors cannot be combined, or empty when they can. One place, because a frame
// silently coerced in any one operation would defeat the rule in all of them.
std::string incompatible(const Vector &a, const Vector &b, const char *operation) {
    if (a.frame != b.frame) {
        return std::string("cannot ") + operation + " vectors in frame " + a.frame.name +
               " and frame " + b.frame.name + " without an explicit basis transformation";
    }
    if (a.rank != b.rank)
        return std::string("cannot ") + operation + " a vector of a different number of components";
    if (a.unit.dimension != b.unit.dimension) {
        return std::string("cannot ") + operation + " " + dimension_text(a.unit.dimension) + " and " +
               dimension_text(b.unit.dimension);
    }
    return std::string();
}

// The result of an operation on two vectors: their shared frame and rank, and the SI unit of the
// dimension the caller worked out, since the components handed back are in SI.
Vector si_result(const Vector &a, const Vector &b, const Dimension &dimension) {
    Vector out;
    out.rank = a.rank;
    out.frame = a.frame;
    out.unit.dimension = dimension;
    out.unit.scale.num = 1;
    out.unit.scale.den = 1;
    out.precision = precision_combine(a.precision, b.precision);
    return out;
}

const char *kAxisNames[3] = {"i", "j", "k"};

}  // namespace

std::string vector_text(const Vector &v) {
    const Rational components[3] = {v.x, v.y, v.z};
    std::string body;
    size_t printed = 0;
    for (uint8_t axis = 0; axis < v.rank && axis < 3; ++axis) {
        if (components[axis].num == 0)
            continue;
        const std::string number = rational_text(components[axis]);
        const bool negative = !number.empty() && number.front() == '-';
        if (!body.empty())
            body += negative ? " - " : " + ";
        else if (negative)
            body += "-";
        body += negative ? number.substr(1) : number;
        body += " ";
        body += kAxisNames[axis];
        ++printed;
    }
    if (body.empty())
        body = "0";
    const std::string unit = vector_unit_text(v);
    if (unit.empty() || unit == "1")
        return body;
    // Parenthesised only when the unit would otherwise look like it belongs to the last term alone.
    if (printed > 1)
        return "(" + body + ") " + unit;
    return body + " " + unit;
}

bool reported_vector_text(const Vector &v, std::string *out, HalfPlace &checked) {
    checked = HalfPlace::Within;
    if (v.precision.kind == NumberKind::Exact) {
        *out = vector_text(v);
        return true;
    }
    const Rational components[3] = {v.x, v.y, v.z};
    std::string body;
    size_t printed = 0;
    for (uint8_t axis = 0; axis < v.rank && axis < 3; ++axis) {
        if (components[axis].num == 0)
            continue;
        std::string number;
        // Only the rounder produces false, and the comparison below leaves through checked, so a
        // refused rounding is never spelled as the place the exact arithmetic cannot reach.
        if (!precision_rounded_text(components[axis], v.precision, &number))
            return false;
        const HalfPlace axis_checked =
            precision_rounding_valid(components[axis], number, v.precision);
        // A disagreement on one axis outranks an inconclusive one on another, as in outcome_from.
        if (axis_checked != HalfPlace::Within &&
            (checked == HalfPlace::Within || axis_checked == HalfPlace::Outside)) {
            checked = axis_checked;
        }
        const bool negative = !number.empty() && number.front() == '-';
        if (!body.empty())
            body += negative ? " - " : " + ";
        else if (negative)
            body += "-";
        body += negative ? number.substr(1) : number;
        body += " ";
        body += kAxisNames[axis];
        ++printed;
    }
    if (body.empty())
        body = "0";
    const std::string unit = vector_unit_text(v);
    if (unit.empty() || unit == "1")
        *out = body;
    else
        *out = printed > 1 ? "(" + body + ") " + unit : body + " " + unit;
    return true;
}

bool vector_add(const Vector &a, const Vector &b, Vector *out, std::string *error) {
    const std::string why = incompatible(a, b, "add");
    if (!why.empty()) {
        *error = why;
        return false;
    }
    Vector a_si;
    Vector b_si;
    if (!to_si(a, &a_si) || !to_si(b, &b_si)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    Vector sum = si_result(a_si, b_si, a.unit.dimension);
    if (!rational_add(a_si.x, b_si.x, &sum.x) || !rational_add(a_si.y, b_si.y, &sum.y) ||
        !rational_add(a_si.z, b_si.z, &sum.z)) {
        *error = "a component sum does not fit an exact fraction";
        return false;
    }
    const Rational components[3] = {sum.x, sum.y, sum.z};
    sum.precision = Precision();
    for (uint8_t axis = 0; axis < sum.rank; ++axis) {
        sum.precision = precision_combine(
            sum.precision, precision_sum(components[axis], a_si.precision, b_si.precision));
    }
    *out = std::move(sum);
    return true;
}

bool vector_sub(const Vector &a, const Vector &b, Vector *out, std::string *error) {
    const std::string why = incompatible(a, b, "subtract");
    if (!why.empty()) {
        *error = why;
        return false;
    }
    Vector a_si;
    Vector b_si;
    if (!to_si(a, &a_si) || !to_si(b, &b_si)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    Vector difference = si_result(a_si, b_si, a.unit.dimension);
    if (!rational_sub(a_si.x, b_si.x, &difference.x) ||
        !rational_sub(a_si.y, b_si.y, &difference.y) ||
        !rational_sub(a_si.z, b_si.z, &difference.z)) {
        *error = "a component difference does not fit an exact fraction";
        return false;
    }
    const Rational components[3] = {difference.x, difference.y, difference.z};
    difference.precision = Precision();
    for (uint8_t axis = 0; axis < difference.rank; ++axis) {
        difference.precision = precision_combine(
            difference.precision,
            precision_sum(components[axis], a_si.precision, b_si.precision));
    }
    *out = std::move(difference);
    return true;
}

bool vector_scale(const Vector &v, const Quantity &s, Vector *out, std::string *error) {
    Dimension product_dimension;
    if (!dimension_multiply(v.unit.dimension, s.unit.dimension, &product_dimension)) {
        *error = "the product dimension does not fit";
        return false;
    }
    Vector vector_si;
    Rational scalar;
    if (!to_si(v, &vector_si) || !to_si(s, &scalar)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    const Precision scalar_precision =
        precision_product(scalar, s.value, s.precision, s.unit.scale, Precision());
    const Rational components[3] = {vector_si.x, vector_si.y, vector_si.z};
    Rational scaled_components[3];
    Vector scaled = si_result(vector_si, vector_si, product_dimension);
    scaled.precision = Precision();
    for (uint8_t axis = 0; axis < vector_si.rank; ++axis) {
        if (!rational_mul(components[axis], scalar, &scaled_components[axis])) {
            *error = "a scaled component does not fit an exact fraction";
            return false;
        }
        scaled.precision = precision_combine(
            scaled.precision,
            precision_product(scaled_components[axis], components[axis], vector_si.precision,
                              scalar, scalar_precision));
    }
    scaled.x = scaled_components[0];
    scaled.y = scaled_components[1];
    scaled.z = vector_si.rank == 3 ? scaled_components[2] : Rational();
    *out = std::move(scaled);
    return true;
}

bool vector_dot(const Vector &a, const Vector &b, Quantity *out, std::string *error) {
    if (a.frame != b.frame) {
        *error = "cannot take the dot product of vectors in frame " + a.frame.name +
                 " and frame " + b.frame.name + " without an explicit basis transformation";
        return false;
    }
    if (a.rank != b.rank) {
        *error = "cannot take the dot product of vectors with a different number of components";
        return false;
    }
    Dimension product_dimension;
    if (!dimension_multiply(a.unit.dimension, b.unit.dimension, &product_dimension)) {
        *error = "the product dimension does not fit";
        return false;
    }
    Vector a_si;
    Vector b_si;
    if (!to_si(a, &a_si) || !to_si(b, &b_si)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    const Rational left[3] = {a_si.x, a_si.y, a_si.z};
    const Rational right[3] = {b_si.x, b_si.y, b_si.z};
    Quantity product;
    product.unit.dimension = product_dimension;
    product.unit.scale.num = 1;
    product.unit.scale.den = 1;
    for (uint8_t axis = 0; axis < a_si.rank; ++axis) {
        Rational term;
        Rational accumulated;
        if (!rational_mul(left[axis], right[axis], &term) ||
            !rational_add(product.value, term, &accumulated)) {
            *error = "a dot product term does not fit an exact fraction";
            return false;
        }
        const Precision term_precision = precision_product(
            term, left[axis], a_si.precision, right[axis], b_si.precision);
        product.value = accumulated;
        product.precision = precision_sum(product.value, product.precision, term_precision);
    }
    *out = std::move(product);
    return true;
}

bool vector_cross(const Vector &a, const Vector &b, Vector *out, std::string *error) {
    if (a.rank != 3 || b.rank != 3) {
        *error = "the cross product is defined on three components and one of these has two";
        return false;
    }
    // Unlike a sum, the two dimensions need not match: a force crossed with a distance is a torque.
    if (a.frame != b.frame) {
        *error = "cannot cross vectors in frame " + a.frame.name + " and frame " + b.frame.name +
                 " without an explicit basis transformation";
        return false;
    }
    Dimension product_dimension;
    if (!dimension_multiply(a.unit.dimension, b.unit.dimension, &product_dimension)) {
        *error = "the product dimension does not fit";
        return false;
    }
    Vector a_si;
    Vector b_si;
    if (!to_si(a, &a_si) || !to_si(b, &b_si)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    const Rational first_left[3] = {a_si.y, a_si.z, a_si.x};
    const Rational first_right[3] = {b_si.z, b_si.x, b_si.y};
    const Rational second_left[3] = {a_si.z, a_si.x, a_si.y};
    const Rational second_right[3] = {b_si.y, b_si.z, b_si.x};
    Rational components[3];
    Vector cross = si_result(a_si, b_si, product_dimension);
    cross.precision = Precision();
    for (uint8_t axis = 0; axis < 3; ++axis) {
        Rational first;
        Rational second;
        if (!rational_mul(first_left[axis], first_right[axis], &first) ||
            !rational_mul(second_left[axis], second_right[axis], &second) ||
            !rational_sub(first, second, &components[axis])) {
            *error = "a cross product component does not fit an exact fraction";
            return false;
        }
        const Precision first_precision = precision_product(
            first, first_left[axis], a_si.precision, first_right[axis], b_si.precision);
        const Precision second_precision = precision_product(
            second, second_left[axis], a_si.precision, second_right[axis], b_si.precision);
        cross.precision = precision_combine(
            cross.precision,
            precision_sum(components[axis], first_precision, second_precision));
    }
    cross.x = components[0];
    cross.y = components[1];
    cross.z = components[2];
    *out = std::move(cross);
    return true;
}

bool vector_magnitude(const Vector &v, Quantity *out, std::string *error) {
    Vector vector_si;
    if (!to_si(v, &vector_si)) {
        *error = "a component does not fit an exact fraction after conversion";
        return false;
    }
    const Rational components[3] = {vector_si.x, vector_si.y, vector_si.z};
    Rational sum;
    Precision sum_precision;
    for (uint8_t axis = 0; axis < vector_si.rank; ++axis) {
        Rational square;
        Rational accumulated;
        if (!rational_mul(components[axis], components[axis], &square) ||
            !rational_add(sum, square, &accumulated)) {
            *error = "a squared component does not fit an exact fraction";
            return false;
        }
        const Precision square_precision = precision_product(
            square, components[axis], vector_si.precision, components[axis], vector_si.precision);
        sum = accumulated;
        sum_precision = precision_sum(sum, sum_precision, square_precision);
    }
    Quantity magnitude;
    if (!rational_sqrt_exact(sum, &magnitude.value)) {
        *error = "the magnitude of " + vector_text(v) + " is not an exact fraction";
        return false;
    }
    magnitude.unit.dimension = v.unit.dimension;
    magnitude.unit.scale.num = 1;
    magnitude.unit.scale.den = 1;
    magnitude.precision = precision_product(magnitude.value, magnitude.value, sum_precision,
                                            Rational{1, 1}, Precision());
    *out = std::move(magnitude);
    return true;
}

}  // namespace nps
