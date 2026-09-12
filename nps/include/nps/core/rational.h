#ifndef NPS_RATIONAL_H
#define NPS_RATIONAL_H

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>

#include <gmp.h>

#include "nps/core/budgets.h"
#include "nps/core/checked.h"

static_assert(__GNU_MP_VERSION == 6 && __GNU_MP_VERSION_MINOR == 3 &&
              __GNU_MP_VERSION_PATCHLEVEL == 0,
              "StepCAS requires GMP 6.3.0");

namespace nps {

inline uint64_t magnitude(int64_t value) {
    return value < 0 ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
}

struct Rational {
    int64_t num = 0;
    int64_t den = 1;
};

namespace detail {

class Mpz final {
  public:
    Mpz() { mpz_init(value_); }
    ~Mpz() { mpz_clear(value_); }

    Mpz(const Mpz &) = delete;
    Mpz &operator=(const Mpz &) = delete;

    mpz_ptr get() { return value_; }
    mpz_srcptr get() const { return value_; }

  private:
    mpz_t value_;
};

class Mpq final {
  public:
    Mpq() { mpq_init(value_); }
    ~Mpq() { mpq_clear(value_); }

    Mpq(const Mpq &) = delete;
    Mpq &operator=(const Mpq &) = delete;

    mpq_ptr get() { return value_; }
    mpq_srcptr get() const { return value_; }

  private:
    mpq_t value_;
};

inline void mpz_set_u64(mpz_ptr out, uint64_t value) {
    mpz_import(out, 1, 1, sizeof(value), 0, 0, &value);
}

inline void mpz_set_i64(mpz_ptr out, int64_t value) {
    mpz_set_u64(out, magnitude(value));
    if (value < 0)
        mpz_neg(out, out);
}

inline bool mpz_get_i64(mpz_srcptr value, int64_t *out) {
    const bool negative = mpz_sgn(value) < 0;
    Mpz absolute;
    Mpz bound;
    mpz_abs(absolute.get(), value);
    mpz_set_u64(bound.get(), negative ? uint64_t{1} << 63
                                      : static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    if (mpz_cmp(absolute.get(), bound.get()) > 0)
        return false;

    uint64_t word = 0;
    size_t count = 0;
    mpz_export(&word, &count, 1, sizeof(word), 0, 0, absolute.get());
    if (count > 1)
        return false;
    if (!negative) {
        *out = static_cast<int64_t>(word);
        return true;
    }
    if (word == (uint64_t{1} << 63)) {
        *out = std::numeric_limits<int64_t>::min();
        return true;
    }
    *out = -static_cast<int64_t>(word);
    return true;
}

inline bool mpq_set_i64(mpq_ptr out, int64_t num, int64_t den) {
    if (den == 0)
        return false;
    mpz_set_i64(mpq_numref(out), num);
    mpz_set_i64(mpq_denref(out), den);
    mpq_canonicalize(out);
    return true;
}

inline bool mpq_get_fraction(mpq_srcptr value, int64_t *num, int64_t *den) {
    int64_t next_num = 0;
    int64_t next_den = 0;
    if (!mpz_get_i64(mpq_numref(value), &next_num) ||
        !mpz_get_i64(mpq_denref(value), &next_den)) {
        return false;
    }
    *num = next_num;
    *den = next_den;
    return true;
}

inline bool mpq_set_rational(mpq_ptr out, const Rational &value) {
    return mpq_set_i64(out, value.num, value.den);
}

inline bool mpq_get_rational(mpq_srcptr value, Rational *out) {
    return mpq_get_fraction(value, &out->num, &out->den);
}

inline std::string mpz_text(mpz_srcptr value) {
    std::array<char, 64> buffer{};
    if (mpz_sizeinbase(value, 10) + 2 > buffer.size())
        return {};
    mpz_get_str(buffer.data(), 10, value);
    return buffer.data();
}

inline void mpz_pow10(mpz_ptr out, unsigned long exponent) { mpz_ui_pow_ui(out, 10, exponent); }

inline bool mpz_pow_bounded(mpz_ptr out, mpz_srcptr base, uint64_t exponent) {
    if (exponent <= 63) {
        mpz_pow_ui(out, base, static_cast<unsigned long>(exponent));
        return true;
    }
    if (mpz_cmpabs_ui(base, 1) > 0)
        return false;
    if (mpz_sgn(base) == 0) {
        mpz_set_ui(out, 0);
    } else {
        mpz_set_si(out, mpz_sgn(base) < 0 && exponent % 2 != 0 ? -1 : 1);
    }
    return true;
}

inline int mpq_leading_decimal_place(mpq_srcptr value) {
    if (mpq_sgn(value) == 0)
        return 0;

    Mpz absolute;
    mpz_abs(absolute.get(), mpq_numref(value));
    const std::string numerator_text = mpz_text(absolute.get());
    const std::string denominator_text = mpz_text(mpq_denref(value));
    int lead = static_cast<int>(numerator_text.size()) - static_cast<int>(denominator_text.size());

    Mpz scale;
    Mpz comparison;
    if (lead >= 0) {
        mpz_pow10(scale.get(), static_cast<unsigned long>(lead));
        mpz_mul(comparison.get(), mpq_denref(value), scale.get());
        if (mpz_cmp(absolute.get(), comparison.get()) < 0)
            --lead;
    } else {
        mpz_pow10(scale.get(), static_cast<unsigned long>(-lead));
        mpz_mul(comparison.get(), absolute.get(), scale.get());
        if (mpz_cmp(comparison.get(), mpq_denref(value)) < 0)
            --lead;
    }
    return lead;
}

}

inline bool normalise(int64_t *num, int64_t *den) {
    detail::Mpq value;
    if (!detail::mpq_set_i64(value.get(), *num, *den))
        return false;
    return detail::mpq_get_fraction(value.get(), num, den);
}

inline bool negate_fraction(int64_t num, int64_t den, int64_t *out_num, int64_t *out_den) {
    detail::Mpq value;
    detail::Mpq negated;
    if (!detail::mpq_set_i64(value.get(), num, den))
        return false;
    mpq_neg(negated.get(), value.get());
    return detail::mpq_get_fraction(negated.get(), out_num, out_den);
}

inline bool add_fraction(int64_t a_num, int64_t a_den, int64_t b_num, int64_t b_den,
                         int64_t *out_num, int64_t *out_den) {
    detail::Mpq a;
    detail::Mpq b;
    detail::Mpq sum;
    if (!detail::mpq_set_i64(a.get(), a_num, a_den) ||
        !detail::mpq_set_i64(b.get(), b_num, b_den)) {
        return false;
    }
    mpq_add(sum.get(), a.get(), b.get());
    return detail::mpq_get_fraction(sum.get(), out_num, out_den);
}

inline bool sub_fraction(int64_t a_num, int64_t a_den, int64_t b_num, int64_t b_den,
                         int64_t *out_num, int64_t *out_den) {
    detail::Mpq a;
    detail::Mpq b;
    detail::Mpq difference;
    if (!detail::mpq_set_i64(a.get(), a_num, a_den) ||
        !detail::mpq_set_i64(b.get(), b_num, b_den)) {
        return false;
    }
    mpq_sub(difference.get(), a.get(), b.get());
    return detail::mpq_get_fraction(difference.get(), out_num, out_den);
}

inline bool mul_fraction(int64_t a_num, int64_t a_den, int64_t b_num, int64_t b_den,
                         int64_t *out_num, int64_t *out_den) {
    detail::Mpq a;
    detail::Mpq b;
    detail::Mpq product;
    if (!detail::mpq_set_i64(a.get(), a_num, a_den) ||
        !detail::mpq_set_i64(b.get(), b_num, b_den)) {
        return false;
    }
    mpq_mul(product.get(), a.get(), b.get());
    return detail::mpq_get_fraction(product.get(), out_num, out_den);
}

inline bool div_fraction(int64_t a_num, int64_t a_den, int64_t b_num, int64_t b_den,
                         int64_t *out_num, int64_t *out_den) {
    detail::Mpq a;
    detail::Mpq b;
    detail::Mpq quotient;
    if (b_num == 0 || !detail::mpq_set_i64(a.get(), a_num, a_den) ||
        !detail::mpq_set_i64(b.get(), b_num, b_den)) {
        return false;
    }
    mpq_div(quotient.get(), a.get(), b.get());
    return detail::mpq_get_fraction(quotient.get(), out_num, out_den);
}

inline bool rational_add(const Rational &a, const Rational &b, Rational *out) {
    return add_fraction(a.num, a.den, b.num, b.den, &out->num, &out->den);
}

inline bool rational_sub(const Rational &a, const Rational &b, Rational *out) {
    return sub_fraction(a.num, a.den, b.num, b.den, &out->num, &out->den);
}

inline bool rational_mul(const Rational &a, const Rational &b, Rational *out) {
    return mul_fraction(a.num, a.den, b.num, b.den, &out->num, &out->den);
}

inline bool rational_div(const Rational &a, const Rational &b, Rational *out) {
    return div_fraction(a.num, a.den, b.num, b.den, &out->num, &out->den);
}

inline bool integer_power(int64_t base, uint64_t exponent, int64_t *out) {
    detail::Mpz source;
    detail::Mpz powered;
    detail::mpz_set_i64(source.get(), base);
    if (!detail::mpz_pow_bounded(powered.get(), source.get(), exponent))
        return false;
    return detail::mpz_get_i64(powered.get(), out);
}

inline bool rational_power(const Rational &base, int64_t exponent, Rational *out) {
    detail::Mpq canonical;
    if (!detail::mpq_set_rational(canonical.get(), base) ||
        (exponent < 0 && mpq_sgn(canonical.get()) == 0)) {
        return false;
    }

    const uint64_t power = magnitude(exponent);
    detail::Mpz numerator;
    detail::Mpz denominator;
    if (!detail::mpz_pow_bounded(numerator.get(), mpq_numref(canonical.get()), power) ||
        !detail::mpz_pow_bounded(denominator.get(), mpq_denref(canonical.get()), power)) {
        return false;
    }

    detail::Mpq powered;
    mpq_set_num(powered.get(), exponent < 0 ? denominator.get() : numerator.get());
    mpq_set_den(powered.get(), exponent < 0 ? numerator.get() : denominator.get());
    mpq_canonicalize(powered.get());
    return detail::mpq_get_rational(powered.get(), out);
}

inline bool rational_equal(const Rational &a, const Rational &b) {
    detail::Mpq left;
    detail::Mpq right;
    return detail::mpq_set_rational(left.get(), a) && detail::mpq_set_rational(right.get(), b) &&
           mpq_equal(left.get(), right.get()) != 0;
}

inline bool rational_leading_decimal_place(const Rational &value, int *out) {
    detail::Mpq canonical;
    if (!detail::mpq_set_rational(canonical.get(), value))
        return false;
    *out = detail::mpq_leading_decimal_place(canonical.get());
    return true;
}

inline bool exact_isqrt(int64_t value, int64_t *root) {
    if (value < 0)
        return false;
    detail::Mpz source;
    detail::Mpz square_root;
    detail::mpz_set_i64(source.get(), value);
    if (mpz_perfect_square_p(source.get()) == 0)
        return false;
    mpz_sqrt(square_root.get(), source.get());
    return detail::mpz_get_i64(square_root.get(), root);
}

inline bool rational_sqrt_exact(const Rational &value, Rational *out) {
    detail::Mpq canonical;
    if (!detail::mpq_set_rational(canonical.get(), value) || mpq_sgn(canonical.get()) < 0 ||
        mpz_perfect_square_p(mpq_numref(canonical.get())) == 0 ||
        mpz_perfect_square_p(mpq_denref(canonical.get())) == 0) {
        return false;
    }
    detail::Mpq square_root;
    mpz_sqrt(mpq_numref(square_root.get()), mpq_numref(canonical.get()));
    mpz_sqrt(mpq_denref(square_root.get()), mpq_denref(canonical.get()));
    return detail::mpq_get_rational(square_root.get(), out);
}

namespace detail {

// The decimal scanner, stopping at the exact value the text names. Kept apart from
// rational_from_text because the narrowing to int64 is the only step that can fail on a numeral
// this reads perfectly well, and a caller comparing in Mpq has no reason to pay for it.
//
// max_fractional_digits is the caller's, not the format's. A value bound for int64 keeps 18, since
// that is what the narrowing can hold. A comparison in Mpq has no such ceiling and passes the text
// length, which Limits already bounds, because a rounding the engine can print it must be able to
// read: eighteen significant figures of a value below a tenth needs nineteen decimal places.
inline bool mpq_from_text(mpq_ptr out, const std::string &text,
                          unsigned long max_fractional_digits = 18) {
    if (text.size() > Limits().max_input_bytes)
        return false;

    size_t at = 0;
    if (at < text.size() && text[at] == '-')
        ++at;

    bool saw_digit = false;
    bool saw_point = false;
    unsigned long fractional_digits = 0;
    for (; at < text.size(); ++at) {
        const char character = text[at];
        if (character == '.') {
            if (saw_point)
                return false;
            saw_point = true;
            continue;
        }
        if (character < '0' || character > '9')
            return false;
        if (saw_point && ++fractional_digits > max_fractional_digits)
            return false;
        saw_digit = true;
    }
    if (!saw_digit)
        return false;

    std::string integer_text = text;
    const size_t point = integer_text.find('.');
    if (point != std::string::npos)
        integer_text.erase(point, 1);
    Mpz numerator;
    if (mpz_set_str(numerator.get(), integer_text.c_str(), 10) != 0)
        return false;

    Mpz denominator;
    mpz_pow10(denominator.get(), fractional_digits);
    mpq_set_num(out, numerator.get());
    mpq_set_den(out, denominator.get());
    mpq_canonicalize(out);
    return true;
}

}  // namespace detail

inline bool rational_from_text(const std::string &text, Rational *out) {
    detail::Mpq value;
    return detail::mpq_from_text(value.get(), text) && detail::mpq_get_rational(value.get(), out);
}

inline std::string magnitude_text(uint64_t value) {
    std::array<char, std::numeric_limits<uint64_t>::digits10 + 2> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), converted.ptr);
}

inline std::string rational_text(const Rational &value) {
    detail::Mpq canonical;
    if (!detail::mpq_set_rational(canonical.get(), value))
        return "invalid";

    const mpz_srcptr numerator = mpq_numref(canonical.get());
    const mpz_srcptr denominator = mpq_denref(canonical.get());
    if (mpz_cmp_ui(denominator, 1) == 0)
        return detail::mpz_text(numerator);

    detail::Mpz two;
    detail::Mpz five;
    detail::Mpz after_twos;
    detail::Mpz remaining;
    mpz_set_ui(two.get(), 2);
    mpz_set_ui(five.get(), 5);
    const mp_bitcnt_t twos = mpz_remove(after_twos.get(), denominator, two.get());
    const mp_bitcnt_t fives = mpz_remove(remaining.get(), after_twos.get(), five.get());
    const mp_bitcnt_t places = std::max(twos, fives);
    if (mpz_cmp_ui(remaining.get(), 1) != 0 || places > 18)
        return detail::mpz_text(numerator) + "/" + detail::mpz_text(denominator);

    detail::Mpz absolute;
    detail::Mpz scale;
    detail::Mpz scaled;
    mpz_abs(absolute.get(), numerator);
    detail::mpz_pow10(scale.get(), static_cast<unsigned long>(places));
    mpz_mul(scaled.get(), absolute.get(), scale.get());
    mpz_divexact(scaled.get(), scaled.get(), denominator);
    std::string body = detail::mpz_text(scaled.get());
    if (body.size() <= places)
        body.insert(0, static_cast<size_t>(places) + 1 - body.size(), '0');
    body.insert(body.size() - static_cast<size_t>(places), 1, '.');
    return (mpz_sgn(numerator) < 0 ? "-" : "") + body;
}

inline bool rounded_text(const Rational &value, unsigned digits, std::string *out) {
    if (digits == 0 || digits > 18)
        return false;

    detail::Mpq canonical;
    if (!detail::mpq_set_rational(canonical.get(), value))
        return false;
    const mpz_srcptr numerator = mpq_numref(canonical.get());
    const mpz_srcptr denominator = mpq_denref(canonical.get());
    if (mpz_sgn(numerator) == 0) {
        *out = "0";
        return true;
    }

    detail::Mpz absolute;
    mpz_abs(absolute.get(), numerator);
    int lead = detail::mpq_leading_decimal_place(canonical.get());
    detail::Mpz scale;
    const int shift = static_cast<int>(digits) - 1 - lead;
    detail::Mpz scaled_numerator;
    detail::Mpz scaled_denominator;
    mpz_set(scaled_numerator.get(), absolute.get());
    mpz_set(scaled_denominator.get(), denominator);
    if (shift > 0) {
        detail::mpz_pow10(scale.get(), static_cast<unsigned long>(shift));
        mpz_mul(scaled_numerator.get(), scaled_numerator.get(), scale.get());
    } else if (shift < 0) {
        detail::mpz_pow10(scale.get(), static_cast<unsigned long>(-shift));
        mpz_mul(scaled_denominator.get(), scaled_denominator.get(), scale.get());
    }

    detail::Mpz rounded;
    detail::Mpz remainder;
    detail::Mpz twice_remainder;
    mpz_fdiv_qr(rounded.get(), remainder.get(), scaled_numerator.get(), scaled_denominator.get());
    mpz_mul_2exp(twice_remainder.get(), remainder.get(), 1);
    if (mpz_cmp(twice_remainder.get(), scaled_denominator.get()) >= 0)
        mpz_add_ui(rounded.get(), rounded.get(), 1);

    std::string body = detail::mpz_text(rounded.get());
    if (body.size() > digits) {
        body.resize(digits);
        ++lead;
    }

    std::string text;
    if (lead >= static_cast<int>(digits) - 1) {
        text = body + std::string(static_cast<size_t>(lead - static_cast<int>(digits) + 1), '0');
    } else if (lead >= 0) {
        text = body.substr(0, static_cast<size_t>(lead) + 1) + "." +
               body.substr(static_cast<size_t>(lead) + 1);
    } else {
        text = "0." + std::string(static_cast<size_t>(-lead) - 1, '0') + body;
    }
    *out = (mpz_sgn(numerator) < 0 ? "-" : "") + text;
    return true;
}

// What a half-place comparison found. Named for what happened rather than for what a caller should
// do about it, because deciding that is the caller's job and conflating the two is what this type
// exists to stop: a rounding that disagrees and one whose text could not be read are different
// answers, and a single bool made every caller guess which it had.
//
// Unreadable has two producers. The text is not a decimal numeral, or the declared place is past
// the widest a unit is built for, which no printer here reaches and a caller can still ask for. A
// value with a zero denominator reaches it too, which is a caller that built one.
enum class HalfPlace { Within, Outside, Unreadable };

inline const char *half_place_name(HalfPlace outcome) {
    switch (outcome) {
        case HalfPlace::Within: return "within half a place";
        case HalfPlace::Outside: return "outside half a place";
        case HalfPlace::Unreadable: return "could not be read back";
    }
    return "could not be read back";
}

namespace detail {

// One unit in a decimal place, as an exact value. The ceiling bounds the work rather than the
// representation, and sits far above any place a numeral built from an int64 Rational can carry.
inline bool mpq_decimal_place_unit(mpq_ptr out, int32_t place) {
    constexpr int64_t kWidestComparablePlace = 1024;
    const int64_t magnitude = place < 0 ? -static_cast<int64_t>(place) : place;
    if (magnitude > kWidestComparablePlace)
        return false;

    Mpz power;
    mpz_pow10(power.get(), static_cast<unsigned long>(magnitude));
    Mpz one;
    mpz_set_ui(one.get(), 1);
    if (place < 0) {
        mpq_set_num(out, one.get());
        mpq_set_den(out, power.get());
    } else {
        mpq_set_num(out, power.get());
        mpq_set_den(out, one.get());
    }
    mpq_canonicalize(out);
    return true;
}

// Twice the error against one unit in the last place, which is the whole comparison.
inline HalfPlace half_place_compare(mpq_srcptr exact, mpq_srcptr reported, mpq_srcptr unit) {
    Mpq error;
    mpq_sub(error.get(), exact, reported);
    mpq_abs(error.get(), error.get());
    mpq_mul_2exp(error.get(), error.get(), 1);
    return mpq_cmp(error.get(), unit) <= 0 ? HalfPlace::Within : HalfPlace::Outside;
}

}  // namespace detail

}

#endif
