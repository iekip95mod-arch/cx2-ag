// The one file that includes both this project's AST and Giac's. Everything Giac-shaped stops here.
//
// Compiled only into the unified build, where Giac is linked into the same image. The two-image
// arrangement reaches Giac through a Lua call and cannot pass an object across it, so it keeps the
// string path in lua_module.cc.

#include "nps/cas/giac_typed.h"

#include <algorithm>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/core/matrix.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/cas/matrix_events.h"
#include "nps/steps/matrix_row.h"

#include <os.h>

#include "giac.h"
#include "giac_layout.h"
#include "k_csdk.h"
#include "luabridge.h"

namespace nps {
namespace {

using giac::gen;

// Giac's own init runs on the first caseval and sets up the parser, the console and the angle mode.
// A typed call made before that would run against a context that does not exist yet, so the first
// one here forces it through the same door the shell uses.
giac::context *context_pointer() {
    static giac::context *ctx = 0;
    if (!ctx) {
        giac_caseval("0");
        // Giac hands the context back through its string channel, which is how luabridge.cc gets
        // it too. The pointer is Giac's own and not const, so the const comes off the char pointer
        // this arrives as rather than off the context.
        char *reply = const_cast<char *>(giac::caseval("caseval contextptr"));
        ctx = reinterpret_cast<giac::context *>(reply);
    }
    return ctx;
}

const giac::unary_function_ptr *function_for(const std::string &name) {
    if (name == "sin")
        return giac::at_sin;
    if (name == "cos")
        return giac::at_cos;
    if (name == "tan")
        return giac::at_tan;
    if (name == "exp")
        return giac::at_exp;
    if (name == "ln")
        return giac::at_ln;
    if (name == "sqrt")
        return giac::at_sqrt;
    if (name == "atan2")
        return giac::at_atan2;
    return 0;
}

const char *name_for(const giac::unary_function_ptr &f) {
    if (f == *giac::at_sin)
        return "sin";
    if (f == *giac::at_cos)
        return "cos";
    if (f == *giac::at_tan)
        return "tan";
    if (f == *giac::at_exp)
        return "exp";
    if (f == *giac::at_ln)
        return "ln";
    if (f == *giac::at_sqrt)
        return "sqrt";
    if (f == *giac::at_atan2)
        return "atan2";
    return 0;
}

const giac::unary_function_ptr *relation_for(Kind kind) {
    switch (kind) {
        case Kind::Equals: return giac::at_equal;
        case Kind::Less: return giac::at_inferieur_strict;
        case Kind::LessEqual: return giac::at_inferieur_egal;
        case Kind::Greater: return giac::at_superieur_strict;
        case Kind::GreaterEqual: return giac::at_superieur_egal;
        default: return 0;
    }
}

bool decimal_to_gen(const std::string &text, gen *out, std::string *why) {
    const size_t max_bytes = Limits().max_input_bytes;
    if (text.empty() || text.size() > max_bytes) {
        *why = "the decimal literal exceeds the shared input limit";
        return false;
    }

    size_t cursor = 0;
    bool negative = false;
    if (text[cursor] == '+' || text[cursor] == '-') {
        negative = text[cursor] == '-';
        if (++cursor == text.size()) {
            *why = "a decimal literal Giac would not read exactly";
            return false;
        }
    }

    const size_t exponent_at = text.find_first_of("eE", cursor);
    const size_t significand_end = exponent_at == std::string::npos ? text.size() : exponent_at;
    std::string digits;
    digits.reserve(significand_end - cursor);
    size_t fractional_digits = 0;
    bool saw_digit = false;
    bool saw_point = false;
    for (; cursor < significand_end; ++cursor) {
        const char character = text[cursor];
        if (character == '.') {
            if (saw_point) {
                *why = "a decimal literal Giac would not read exactly";
                return false;
            }
            saw_point = true;
            continue;
        }
        if (character < '0' || character > '9') {
            *why = "a decimal literal Giac would not read exactly";
            return false;
        }
        digits.push_back(character);
        if (saw_point)
            ++fractional_digits;
        saw_digit = true;
    }
    if (!saw_digit) {
        *why = "a decimal literal Giac would not read exactly";
        return false;
    }

    size_t exponent = 0;
    bool exponent_negative = false;
    if (exponent_at != std::string::npos) {
        cursor = exponent_at + 1;
        if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
            exponent_negative = text[cursor] == '-';
            ++cursor;
        }
        if (cursor == text.size()) {
            *why = "a decimal literal Giac would not read exactly";
            return false;
        }
        const auto converted =
            std::from_chars(text.data() + cursor, text.data() + text.size(), exponent);
        if (converted.ec != std::errc() || converted.ptr != text.data() + text.size()) {
            *why = "the decimal exponent exceeds the exact-number resource limit";
            return false;
        }
    }

    size_t numerator_zeros = 0;
    size_t denominator_places = 0;
    if (exponent_negative) {
        if (exponent > max_bytes - fractional_digits) {
            *why = "the decimal exponent exceeds the exact-number resource limit";
            return false;
        }
        denominator_places = fractional_digits + exponent;
    } else if (exponent >= fractional_digits) {
        numerator_zeros = exponent - fractional_digits;
    } else {
        denominator_places = fractional_digits - exponent;
    }
    if (denominator_places > max_bytes || numerator_zeros > max_bytes ||
        digits.size() > max_bytes - numerator_zeros) {
        *why = "the decimal literal exceeds the exact-number resource limit";
        return false;
    }

    mpq_t value;
    mpq_init(value);
    mpz_t power;
    mpz_init(power);
    bool valid = mpz_set_str(mpq_numref(value), digits.c_str(), 10) == 0;
    if (valid && numerator_zeros != 0) {
        mpz_ui_pow_ui(power, 10, static_cast<unsigned long>(numerator_zeros));
        mpz_mul(mpq_numref(value), mpq_numref(value), power);
    }
    if (valid && denominator_places != 0)
        mpz_ui_pow_ui(mpq_denref(value), 10, static_cast<unsigned long>(denominator_places));
    if (valid && negative)
        mpz_neg(mpq_numref(value), mpq_numref(value));
    if (valid)
        mpq_canonicalize(value);

    gen exact;
    if (valid) {
        mpz_t normalized_numerator;
        mpz_t normalized_denominator;
        mpz_init_set(normalized_numerator, mpq_numref(value));
        mpz_init_set(normalized_denominator, mpq_denref(value));
        const gen numerator(normalized_numerator);
        const gen denominator(normalized_denominator);
        valid = !giac::is_undef(numerator) && !giac::is_undef(denominator);
        if (valid) {
            exact = gen(giac::fraction(numerator, denominator));
            valid = !giac::is_undef(exact);
        }
        mpz_clear(normalized_denominator);
        mpz_clear(normalized_numerator);
    }
    mpz_clear(power);
    mpq_clear(value);

    if (!valid) {
        *why = "the decimal literal exceeds Giac's exact-number resource limit";
        return false;
    }
    *out = exact;
    return true;
}

// The AST as gen. Refuses rather than approximating: a node this cannot represent exactly is a
// reason to fall back, not a reason to send something close.
bool to_gen(const Arena &arena, NodeId id, gen *out, std::string *why) {
    const Node &n = arena.at(id);
    const ChildView kids = arena.children(n);

    switch (n.kind) {
        case Kind::Integer: {
            if (n.small_valid) {
                *out = gen(static_cast<longlong>(n.small));
                return true;
            }
            if (arena.text(id).size() > Limits().max_input_bytes) {
                *why = "the integer literal exceeds the shared input limit";
                return false;
            }
            // Wider than an int64. The arena kept the digits, which is exactly what mpz reads.
            mpz_t m;
            if (mpz_init_set_str(m, arena.text(id).c_str(), 10) != 0) {
                mpz_clear(m);
                *why = "an integer literal Giac would not read";
                return false;
            }
            const gen exact(m);
            mpz_clear(m);
            if (giac::is_undef(exact)) {
                *why = "the integer literal exceeds Giac's exact-number resource limit";
                return false;
            }
            *out = exact;
            return true;
        }
        case Kind::Decimal: return decimal_to_gen(arena.text(id), out, why);
        case Kind::Symbol:
            *out = arena.text(id) == "pi" ? giac::cst_pi : gen(giac::identificateur(arena.text(id)));
            return true;
        case Kind::Neg: {
            gen inner;
            if (!to_gen(arena, kids[0], &inner, why))
                return false;
            *out = -inner;
            return true;
        }
        case Kind::Add:
        case Kind::Mul: {
            gen acc;
            for (size_t i = 0; i < kids.size(); ++i) {
                gen part;
                if (!to_gen(arena, kids[i], &part, why))
                    return false;
                if (i == 0)
                    acc = part;
                else
                    acc = n.kind == Kind::Add ? acc + part : acc * part;
            }
            *out = acc;
            return true;
        }
        case Kind::Pow: {
            gen base, exponent;
            if (!to_gen(arena, kids[0], &base, why) || !to_gen(arena, kids[1], &exponent, why))
                return false;
            *out = giac::pow(base, exponent, context_pointer());
            return true;
        }
        case Kind::Call: {
            const giac::unary_function_ptr *f = function_for(arena.text(id));
            if (!f) {
                *why = "the function " + arena.text(id) + " is not one the adapter converts";
                return false;
            }
            if (kids.size() != 1 && !(arena.text(id) == "atan2" && kids.size() == 2)) {
                *why = "the function has the wrong number of arguments";
                return false;
            }
            gen first;
            if (!to_gen(arena, kids[0], &first, why))
                return false;
            if (kids.size() == 1) {
                *out = giac::symbolic(f, first);
                return true;
            }
            gen second;
            if (!to_gen(arena, kids[1], &second, why))
                return false;
            *out = giac::symbolic(f, giac::makevecteur(first, second));
            return true;
        }
        case Kind::Equals:
        case Kind::Less:
        case Kind::LessEqual:
        case Kind::Greater:
        case Kind::GreaterEqual: {
            const giac::unary_function_ptr *f = relation_for(n.kind);
            gen left, right;
            if (!to_gen(arena, kids[0], &left, why) || !to_gen(arena, kids[1], &right, why))
                return false;
            *out = giac::symbolic(f, left, right);
            return true;
        }
    }
    *why = "an expression kind the adapter does not convert";
    return false;
}

bool components_to_gen(const Arena &arena, const std::vector<NodeId> &components, gen *out,
                       std::string *why) {
    giac::vecteur values;
    values.reserve(components.size());
    for (NodeId component : components) {
        gen value;
        if (!to_gen(arena, component, &value, why))
            return false;
        values.push_back(value);
    }
    *out = gen(values, 0);
    return true;
}

bool matrix_to_gen(const Arena &arena, NodeId root, gen *out, std::string *why) {
    const auto matrix = MatrixView::from(arena, root);
    if (!matrix)
        return false;
    giac::vecteur rows;
    rows.reserve(matrix->rows());
    for (size_t row = 0; row < matrix->rows(); ++row) {
        giac::vecteur cells;
        cells.reserve(matrix->columns());
        for (size_t column = 0; column < matrix->columns(); ++column) {
            Rational cell;
            if (!read_matrix_rational(arena, matrix->cell(row, column), &cell)) {
                *why = "a matrix cell is outside the exact rational envelope";
                return false;
            }
            const gen numerator(static_cast<longlong>(cell.num));
            cells.push_back(cell.den == 1 ? numerator :
                            gen(giac::fraction(numerator, gen(static_cast<longlong>(cell.den)))));
        }
        rows.push_back(gen(cells, 0));
    }
    *out = gen(rows, giac::_MATRIX__VECT);
    return true;
}

// Text the engine's own reader accepts: no exponent and at most the eighteen places rational.h
// scans, which only a value inside this window can be given.
bool fixed_decimal_text(double value, std::string *out) {
    const double magnitude = std::fabs(value);
    if (magnitude < 1e-4 || magnitude >= 1e18)
        return false;
    char buffer[48];
    for (int places = 0; places <= 18; ++places) {
        const int length = std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
        if (length < 0 || static_cast<size_t>(length) >= sizeof buffer)
            return false;
        if (places == 18 || std::strtod(buffer, nullptr) == value) {
            out->assign(buffer, static_cast<size_t>(length));
            return true;
        }
    }
    return false;
}

bool decimal_text(double value, std::string *out) {
    std::string formatted;
    if (!fixed_decimal_text(value, &formatted)) {
        char buffer[std::numeric_limits<double>::max_digits10 + 8];
        const int length = std::snprintf(buffer, sizeof buffer, "%.*g",
                                         std::numeric_limits<double>::max_digits10, value);
        if (length < 0 || static_cast<size_t>(length) >= sizeof buffer)
            return false;
        formatted.assign(buffer, static_cast<size_t>(length));
    }
    const std::lconv *locale = std::localeconv();
    const char *decimal_point = locale ? locale->decimal_point : nullptr;
    if (decimal_point && decimal_point[0] != '\0' && std::strcmp(decimal_point, ".") != 0) {
        const size_t point = formatted.find(decimal_point);
        if (point != std::string::npos)
            formatted.replace(point, std::strlen(decimal_point), ".");
    }
    *out = formatted;
    return true;
}

// Giac names an infinite or undefined result as an identifier, which would otherwise arrive here as
// an ordinary free variable the engine substitutes numbers into.
bool sentinel_identifier(const char *name) {
    return std::strcmp(name, giac::string_infinity) == 0 ||
           std::strcmp(name, giac::string_undef) == 0;
}

bool from_gen(Arena &arena, const gen &g, NodeId *out, std::string *why);

bool children_from_gen(Arena &arena, const gen &feuille, std::vector<NodeId> *out,
                       std::string *why) {
    if (feuille.type == giac::_VECT) {
        const giac::vecteur &v = *feuille._VECTptr;
        for (size_t i = 0; i < v.size(); ++i) {
            NodeId child;
            if (!from_gen(arena, v[i], &child, why))
                return false;
            out->push_back(child);
        }
        return true;
    }
    NodeId only;
    if (!from_gen(arena, feuille, &only, why))
        return false;
    out->push_back(only);
    return true;
}

// The answer as an AST. Every type Giac can hand back that this project has no node for is refused
// by name, so a shape nobody thought about arrives as a typed refusal rather than as an expression
// that is quietly the wrong one.
bool from_gen(Arena &arena, const gen &g, NodeId *out, std::string *why) {
    if (g == giac::cst_pi) {
        *out = arena.symbol("pi");
        return *out != kNoNode;
    }
    switch (g.type) {
        case giac::_INT_:
            *out = arena.integer(integer_text(static_cast<int64_t>(g.val)));
            return *out != kNoNode;
        case giac::_ZINT: {
            std::string digits(mpz_sizeinbase(*g.ref_ZINTptr(), 10) + 2, '\0');
            if (!mpz_get_str(digits.data(), 10, *g.ref_ZINTptr())) {
                *why = "an integer Giac would not print";
                return false;
            }
            digits.resize(std::strlen(digits.c_str()));
            *out = arena.integer(digits);
            return *out != kNoNode;
        }
        case giac::_DOUBLE_: {
            if (!std::isfinite(g._DOUBLE_val)) {
                *why = "a non-finite approximate value";
                return false;
            }
            std::string formatted;
            if (!decimal_text(g._DOUBLE_val, &formatted)) {
                *why = "an approximate value that could not be formatted";
                return false;
            }
            *out = arena.decimal(formatted);
            return *out != kNoNode;
        }
        case giac::_IDNT: {
            const char *name = g._IDNTptr->id_name;
            if (!name) {
                *why = "an unnamed identifier";
                return false;
            }
            if (sentinel_identifier(name)) {
                *why = std::string("Giac answered with ") + name + ", which is not a number";
                return false;
            }
            *out = arena.symbol(name);
            return *out != kNoNode;
        }
        case giac::_FRAC: {
            // Our AST spells division as a power of minus one, which is what the parser produces
            // for a/b, so a fraction converts without introducing a node kind for it.
            NodeId num, den;
            if (!from_gen(arena, g._FRACptr->num, &num, why) ||
                !from_gen(arena, g._FRACptr->den, &den, why))
                return false;
            NodeId inverse = arena.binary(Kind::Pow, den, arena.integer("-1"));
            *out = arena.binary(Kind::Mul, num, inverse);
            return *out != kNoNode;
        }
        case giac::_SYMB: {
            const giac::symbolic &s = *g._SYMBptr;
            std::vector<NodeId> kids;
            if (!children_from_gen(arena, s.feuille, &kids, why))
                return false;

            if (s.sommet == *giac::at_plus && kids.size() >= 2) {
                *out = arena.nary(Kind::Add, kids);
                return *out != kNoNode;
            }
            if (s.sommet == *giac::at_prod && kids.size() >= 2) {
                *out = arena.nary(Kind::Mul, kids);
                return *out != kNoNode;
            }
            if (s.sommet == *giac::at_pow && kids.size() == 2) {
                *out = arena.binary(Kind::Pow, kids[0], kids[1]);
                return *out != kNoNode;
            }
            if (s.sommet == *giac::at_neg && kids.size() == 1) {
                *out = arena.unary(Kind::Neg, kids[0]);
                return *out != kNoNode;
            }
            if (s.sommet == *giac::at_inv && kids.size() == 1) {
                *out = arena.binary(Kind::Pow, kids[0], arena.integer("-1"));
                return *out != kNoNode;
            }
            if (s.sommet == *giac::at_equal && kids.size() == 2) {
                *out = arena.binary(Kind::Equals, kids[0], kids[1]);
                return *out != kNoNode;
            }
            const char *name = name_for(s.sommet);
            if (name && (kids.size() == 1 || (s.sommet == *giac::at_atan2 && kids.size() == 2))) {
                *out = arena.call(name, kids);
                return *out != kNoNode;
            }
            *why = std::string("Giac answered with ") + s.sommet.ptr()->s +
                   ", which the adapter has no node for";
            return false;
        }
        default:
            break;
    }
    *why = "Giac answered with a value of a type the adapter has no node for";
    return false;
}

// Why a conversion was refused, decided once: a dead arena is terminal and a shape with no node is
// not, and a caller that guesses records the first as the second.
ResultTag conversion_failure(const Arena &arena) {
    return arena.failed() ? ResultTag::ResourceFailure : ResultTag::UnsupportedOperation;
}

// The conversion is ours, so its resource refusal is our arena rather than Giac giving up, and the
// adapter must not retire a backend that answered perfectly well over it.
void refuse_conversion(const Arena &arena, TypedResult *out) {
    out->tag = conversion_failure(arena);
    out->from_backend = false;
}

ResultTag matrix_from_gen(Arena &arena, const Request &request, const gen &answer,
                          NodeId *out, std::string *why) {
    const auto input = MatrixView::from(arena, request.target);
    if (!input || answer.type != giac::_VECT || answer._VECTptr->size() != input->rows()) {
        *why = "the backend matrix row count does not match the request";
        return ResultTag::MalformedResult;
    }
    std::vector<NodeId> rows;
    rows.reserve(input->rows());
    for (const gen &row : *answer._VECTptr) {
        if (row.type != giac::_VECT || row._VECTptr->size() != input->columns()) {
            *why = "the backend matrix column count does not match the request";
            return ResultTag::MalformedResult;
        }
        std::vector<NodeId> cells;
        cells.reserve(input->columns());
        for (const gen &cell : *row._VECTptr) {
            if (cell.type == giac::_VECT) {
                *why = "a matrix cell contains another collection";
                return ResultTag::MalformedResult;
            }
            NodeId value = kNoNode;
            if (!from_gen(arena, cell, &value, why))
                return conversion_failure(arena);
            cells.push_back(value);
        }
        rows.push_back(arena.list(cells));
        if (arena.failed())
            return ResultTag::ResourceFailure;
    }
    const NodeId matrix = arena.list(rows);
    const ResultTag tag = matrix_result_status(arena, request, matrix, why);
    if (tag == ResultTag::Exact || tag == ResultTag::Approximate)
        *out = matrix;
    return tag;
}

// Scalar operations accept singleton wrappers but cannot consume a wider collection.
bool peel_singleton(const gen &g, gen *inner, bool *empty, bool *wide) {
    *empty = false;
    *wide = false;
    gen current = g;
    while (current.type == giac::_VECT) {
        const giac::vecteur &v = *current._VECTptr;
        if (v.empty()) {
            *empty = true;
            return false;
        }
        if (v.size() != 1) {
            *wide = true;
            return false;
        }
        current = v[0];
    }
    *inner = current;
    return true;
}

bool contains_decimal(const gen &g) {
    if (g.type == giac::_DOUBLE_ || g.type == giac::_FLOAT_ || g.type == giac::_REAL)
        return true;
    if (g.type == giac::_VECT) {
        const giac::vecteur &v = *g._VECTptr;
        for (size_t i = 0; i < v.size(); ++i) {
            if (contains_decimal(v[i]))
                return true;
        }
        return false;
    }
    if (g.type == giac::_SYMB)
        return contains_decimal(g._SYMBptr->feuille);
    if (g.type == giac::_FRAC)
        return contains_decimal(g._FRACptr->num) || contains_decimal(g._FRACptr->den);
    return false;
}

// Every call runs with interrupts masked and the drawing context released afterwards, the same way
// the shell's caseval does. reset_gc is the OS graphics context rather than a garbage collector
// (k_csdk.c line 853 calls gui_gc_finish), so it holds nothing of Giac's and cannot invalidate the
// result. Matching caseval here keeps the only difference between the two paths the conversion,
// which is the one thing this change is meant to be about.
class DrawingGuard {
  public:
    DrawingGuard() : mask_(TCT_Local_Control_Interrupts(-1)) {}
    ~DrawingGuard() {
        reset_gc();
        TCT_Local_Control_Interrupts(mask_);
    }
    DrawingGuard(const DrawingGuard &) = delete;
    DrawingGuard &operator=(const DrawingGuard &) = delete;

  private:
    int mask_;
};

gen guarded(gen (*body)(const gen &, const gen &, giac::context *), const gen &a, const gen &b) {
    DrawingGuard guard;
    return body(a, b, context_pointer());
}

gen do_simplify(const gen &a, const gen &, giac::context *ctx) { return giac::simplify(a, ctx); }
gen do_expand(const gen &a, const gen &, giac::context *ctx) { return giac::expand(a, ctx); }
gen do_factor(const gen &a, const gen &, giac::context *ctx) {
    return giac::factor(a, false, ctx);
}
gen do_evalf(const gen &a, const gen &, giac::context *ctx) { return giac::evalf(a, 1, ctx); }
gen do_diff(const gen &a, const gen &v, giac::context *ctx) { return giac::derive(a, v, ctx); }
gen do_integrate(const gen &a, const gen &v, giac::context *ctx) {
    if (v.type == giac::_VECT) {
        const giac::vecteur &bounds = *v._VECTptr;
        return giac::_integrate(giac::makevecteur(a, bounds[0], bounds[1], bounds[2]), ctx);
    }
    return giac::integrate_gen(a, v, ctx);
}
gen do_solve(const gen &a, const gen &v, giac::context *ctx) {
    return gen(giac::solve(a, v, 0, ctx), 0);
}
gen do_subst(const gen &a, const gen &pair, giac::context *ctx) {
    const giac::vecteur &v = *pair._VECTptr;
    return giac::subst(a, v[0], v[1], false, ctx);
}
gen do_limit(const gen &a, const gen &pair, giac::context *ctx) {
    const giac::vecteur &v = *pair._VECTptr;
    return giac::limit(a, *v[0]._IDNTptr, v[1], v[2].val, ctx);
}
gen do_dot(const gen &a, const gen &b, giac::context *ctx) {
    return giac::_dotprod(giac::makevecteur(a, b), ctx);
}
gen do_cross(const gen &a, const gen &b, giac::context *ctx) {
    return giac::_cross(giac::makevecteur(a, b), ctx);
}
gen do_norm(const gen &a, const gen &, giac::context *ctx) { return giac::_l2norm(a, ctx); }
gen do_sin(const gen &a, const gen &, giac::context *ctx) { return giac::sin(a, ctx); }
gen do_cos(const gen &a, const gen &, giac::context *ctx) { return giac::cos(a, ctx); }
gen do_ref(const gen &a, const gen &, giac::context *ctx) { return giac::_ref(a, ctx); }
gen do_rref(const gen &a, const gen &, giac::context *ctx) { return giac::_rref(a, ctx); }
gen do_atan2(const gen &a, const gen &b, giac::context *ctx) {
    return giac::eval(giac::symbolic(giac::at_atan2, giac::makevecteur(a, b)), 1, ctx);
}

class MatrixCapture {
  public:
    MatrixCapture(const Request &request, Arena &arena, MatrixRowSink &sink, giac::context *context)
        : request_(request), arena_(arena), sink_(sink), context_(context), current_(request.target),
          level_(giac::step_infolevel(context)), callback_(giac::my_gprintf),
          ctrl_c_(giac::ctrl_c), interrupted_(giac::interrupted) {
        active_ = this;
        // Small exact matrices use Giac's Gauss-Jordan event path at this level.
        giac::step_infolevel(1, context_);
        giac::my_gprintf = capture;
    }
    ~MatrixCapture() {
        giac::my_gprintf = callback_;
        giac::step_infolevel(level_, context_);
        giac::ctrl_c = ctrl_c_;
        giac::interrupted = interrupted_;
        active_ = nullptr;
    }
    MatrixCapture(const MatrixCapture &) = delete;
    MatrixCapture &operator=(const MatrixCapture &) = delete;

    static bool active() { return active_ != nullptr; }
    bool stopped() const { return tag_ != ResultTag::Exact; }
    ResultTag tag() const { return arena_.failed() ? ResultTag::ResourceFailure : tag_; }
    const std::string &detail() const { return detail_; }
    NodeId current() const { return current_; }

    bool observe(const gen &matrix) {
        if (stopped())
            return false;
        NodeId next = kNoNode;
        std::string why;
        const ResultTag converted = matrix_from_gen(arena_, request_, matrix, &next, &why);
        if (converted != ResultTag::Exact) {
            stop(converted, why);
            return false;
        }
        const MatrixRowOperation operation = pending_.value_or(MatrixRowScale{0, {1, 1}});
        const auto check = verify_matrix_row(arena_, current_, next, operation);
        if (check.verification.outcome != VerificationOutcome::Passed) {
            stop(check.verification.outcome == VerificationOutcome::Inconclusive ?
                     ResultTag::UnsupportedOperation : ResultTag::MalformedResult,
                 "the Giac matrix trace has an unverified transition: " +
                                                check.verification.detail);
            return false;
        }
        if (pending_) {
            const bool accepted = sink_.row(current_, next, operation);
            if (arena_.failed()) {
                stop(ResultTag::ResourceFailure, "the matrix step consumer exhausted the shared Arena");
                return false;
            }
            if (!accepted) {
                stop(ResultTag::Unevaluated, "matrix step delivery stopped at the consumer's request");
                return false;
            }
        }
        current_ = next;
        pending_.reset();
        return true;
    }

  private:
    void stop(ResultTag tag, const std::string &why) {
        if (stopped())
            return;
        tag_ = tag;
        detail_ = why;
        giac::ctrl_c = true;
    }

    bool index(const gen &value, size_t bound, size_t *out) {
        if (value.type != giac::_INT_ || value.val < 1 || static_cast<size_t>(value.val) > bound)
            return false;
        *out = static_cast<size_t>(value.val - 1);
        return true;
    }

    bool rational(const gen &value, Rational *out) {
        NodeId coefficient = kNoNode;
        std::string why;
        if (from_gen(arena_, value, &coefficient, &why) &&
            read_matrix_rational(arena_, coefficient, out))
            return true;
        stop(ResultTag::UnsupportedOperation, "a matrix event coefficient is outside the checked exact rational range");
        return false;
    }

    void event(unsigned id, const giac::vecteur &operands, const giac::context *context) {
        if (stopped())
            return;
        if (context != context_) {
            stop(ResultTag::MalformedResult, "a Giac matrix event belongs to another context");
            return;
        }
        const auto shape = MatrixView::from(arena_, request_.target);
        if (!shape) {
            stop(arena_.failed() ? ResultTag::ResourceFailure : ResultTag::MalformedResult,
                 "the matrix request is no longer readable");
            return;
        }
        size_t first = 0, second = 0, repeated = 0;
        Rational factor, coefficient;
        switch (id) {
            case giac::step_rrefpivot:
                if (operands.size() != 4 || !index(operands[1], shape->columns(), &first) ||
                    !index(operands[3], shape->rows(), &second) ||
                    !rational(operands[2], &factor) || factor.num == 0)
                    break;
                if (!observe(operands[0]))
                    return;
                if (!read_matrix_rational(arena_, MatrixView::from(arena_, current_)->cell(second, first),
                                          &coefficient) || factor.num != coefficient.num ||
                    factor.den != coefficient.den)
                    break;
                pivot_observed_ = true;
                return;
            case giac::step_rrefexchange:
                if (operands.size() != 2 || pending_ || !pivot_observed_ ||
                    !index(operands[0], shape->rows(), &first) ||
                    !index(operands[1], shape->rows(), &second) || first == second)
                    break;
                pending_ = MatrixRowSwap{first, second};
                pivot_observed_ = false;
                return;
            case giac::step_rrefpivot0:
                if (operands.size() != 6 || !index(operands[0], shape->rows(), &first) ||
                    !index(operands[2], shape->rows(), &repeated) || first != repeated ||
                    !index(operands[4], shape->rows(), &second) || first == second ||
                    !rational(operands[1], &coefficient) || coefficient.num != 1 || coefficient.den != 1 ||
                    !rational(operands[3], &coefficient))
                    break;
                if (!rational_mul(coefficient, Rational{-1, 1}, &factor)) {
                    stop(ResultTag::UnsupportedOperation, "a row multiplier is outside the checked exact rational range");
                    return;
                }
                if (!observe(operands[5]))
                    return;
                pending_ = MatrixRowAddMultiple{first, second, factor};
                pivot_observed_ = false;
                return;
            case giac::step_rrefscale:
                if (operands.size() != 3 || !index(operands[0], shape->rows(), &first) ||
                    !rational(operands[1], &factor) || factor.num == 0)
                    break;
                if (!observe(operands[2]))
                    return;
                pending_ = MatrixRowScale{first, factor};
                pivot_observed_ = false;
                return;
            case giac::step_rrefend:
                if (operands.size() != 1)
                    break;
                observe(operands[0]);
                pivot_observed_ = false;
                return;
            default:
                break;
        }
        stop(arena_.failed() ? ResultTag::ResourceFailure : ResultTag::MalformedResult,
             "Giac emitted an unsupported matrix event or operand");
    }

    static void capture(unsigned id, const std::string &, const giac::vecteur &operands,
                        const giac::context *context) {
        if (active_)
            active_->event(id, operands, context);
    }

    static MatrixCapture *active_;
    const Request &request_;
    Arena &arena_;
    MatrixRowSink &sink_;
    giac::context *context_;
    NodeId current_;
    std::optional<MatrixRowOperation> pending_;
    ResultTag tag_ = ResultTag::Exact;
    std::string detail_;
    int level_;
    decltype(giac::my_gprintf) callback_;
    bool ctrl_c_;
    bool interrupted_;
    bool pivot_observed_ = false;
};

MatrixCapture *MatrixCapture::active_ = nullptr;

}  // namespace

bool TypedGiacBackend::eval(const std::string &command, std::string *out, std::string *error) {
    const char *reply = giac_caseval(command.c_str());
    if (!reply) {
        *error = "Giac returned nothing";
        return false;
    }
    // The reply points into Giac's own storage, which the next call writes over.
    out->assign(reply);
    return true;
}

bool TypedGiacBackend::matrix_steps(const Request &request, Arena &arena, MatrixRowSink &sink,
                                    TypedResult *out) {
    *out = TypedResult{};
    out->shape = ResultShape::Matrix;
    // Unmarked on purpose: Adapter::dispatch runs this same check before it calls a backend at all,
    // so a refusal here is unreachable. The duplication is what holds it, not a from_backend line.
    out->tag = matrix_request_status(arena, request, &out->detail);
    if (out->tag != ResultTag::Exact)
        return true;
    if (MatrixCapture::active()) {
        out->tag = ResultTag::UnsupportedOperation;
        out->detail = "a matrix capture is already active";
        return true;
    }
    if (giac::ctrl_c || giac::interrupted) {
        out->tag = ResultTag::Cancelled;
        out->detail = "Giac already has a pending interruption";
        return true;
    }
    gen target;
    if (!matrix_to_gen(arena, request.target, &target, &out->detail)) {
        refuse_conversion(arena, out);
        return true;
    }
    giac::context *context = context_pointer();
    if (giac::ctrl_c || giac::interrupted) {
        out->tag = ResultTag::Cancelled;
        out->detail = "Giac already has a pending interruption";
        return true;
    }
    MatrixCapture capture(request, arena, sink, context);
    gen answer;
#if defined(__cpp_exceptions)
    try {
#endif
        answer = guarded(request.op == Op::Ref ? do_ref : do_rref, target, gen{});
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc &) {
        out->tag = ResultTag::ResourceFailure;
        out->detail = "Giac exhausted memory during matrix reduction";
        return true;
    } catch (const std::exception &exception) {
        out->tag = capture.stopped() ? capture.tag() :
            giac::ctrl_c || giac::interrupted ? ResultTag::Cancelled : ResultTag::BackendError;
        out->detail = capture.stopped() ? capture.detail() : exception.what();
        // The same reading as the stopped branch below, because it is the same tag by the same route.
        out->from_backend = !capture.stopped() || out->tag != ResultTag::ResourceFailure;
        return true;
    }
#endif
    if (!capture.stopped() && !giac::is_undef(answer) && !giac::ctrl_c && !giac::interrupted)
        capture.observe(answer);
    if (capture.stopped()) {
        out->tag = capture.tag();
        out->detail = capture.detail();
        // The consumer stopped on our own arena or our own row limits, never on Giac's report.
        out->from_backend = out->tag != ResultTag::ResourceFailure;
    } else if (giac::ctrl_c || giac::interrupted) {
        out->tag = ResultTag::Cancelled;
        out->detail = "Giac was interrupted during matrix reduction";
    } else if (giac::is_undef(answer)) {
        out->tag = ResultTag::Unevaluated;
        out->detail = "Giac stopped before completing the matrix reduction";
    } else {
        out->tag = ResultTag::Exact;
        out->value = capture.current();
    }
    return true;
}

bool TypedGiacBackend::typed(const Request &request, Arena &arena, TypedResult *out) {
    const bool matrix_op = request.op == Op::Ref || request.op == Op::Rref;
    if (matrix_op) {
        *out = TypedResult{};
        out->shape = ResultShape::Matrix;
        // Unmarked for the same reason as the one in matrix_steps above, and held by the same
        // duplication in Adapter::dispatch rather than by a from_backend line.
        out->tag = matrix_request_status(arena, request, &out->detail);
        if (out->tag != ResultTag::Exact)
            return true;
    }
    if (giac::ctrl_c || giac::interrupted) {
        out->tag = ResultTag::Cancelled;
        out->detail = "Giac already has a pending interruption";
        return true;
    }
    gen target;
    std::string why;
    const bool vector_op = request.op == Op::Dot || request.op == Op::Cross || request.op == Op::Norm;
    const bool target_ok = matrix_op ? matrix_to_gen(arena, request.target, &target, &why) : vector_op
                               ? components_to_gen(arena, request.target_components, &target, &why)
                               : to_gen(arena, request.target, &target, &why);
    if (!target_ok) {
        refuse_conversion(arena, out);
        out->detail = why;
        return true;
    }

    gen second;
    switch (request.op) {
        case Op::Dot:
        case Op::Cross:
            if (!components_to_gen(arena, request.argument_components, &second, &why)) {
                out->tag = ResultTag::UnsupportedOperation;
                out->detail = why;
                return true;
            }
            break;
        case Op::Solve:
        case Op::Differentiate:
        case Op::Integrate:
            if (!to_gen(arena, request.variable, &second, &why)) {
                out->tag = ResultTag::UnsupportedOperation;
                out->detail = why;
                return true;
            }
            if (request.op == Op::Integrate && request.lower != kNoNode) {
                gen lower, upper;
                if (!to_gen(arena, request.lower, &lower, &why) ||
                    !to_gen(arena, request.upper, &upper, &why)) {
                    out->tag = ResultTag::UnsupportedOperation;
                    out->detail = why;
                    return true;
                }
                second = giac::makevecteur(second, lower, upper);
            }
            break;
        case Op::Substitute:
        case Op::Limit: {
            gen a, b;
            const NodeId other =
                request.op == Op::Substitute ? request.replacement : request.point;
            if (!to_gen(arena, request.variable, &a, &why) || !to_gen(arena, other, &b, &why)) {
                out->tag = ResultTag::UnsupportedOperation;
                out->detail = why;
                return true;
            }
            second = request.op == Op::Limit ? giac::makevecteur(a, b, gen(request.direction))
                                            : giac::makevecteur(a, b);
            break;
        }
        case Op::Atan2:
            if (!to_gen(arena, request.argument, &second, &why)) {
                out->tag = ResultTag::UnsupportedOperation;
                out->detail = why;
                return true;
            }
            break;
        default:
            break;
    }

    gen answer;
    switch (request.op) {
        case Op::Simplify:
        case Op::IsZero: answer = guarded(do_simplify, target, second); break;
        case Op::Expand: answer = guarded(do_expand, target, second); break;
        case Op::Factor: answer = guarded(do_factor, target, second); break;
        case Op::Approximate: answer = guarded(do_evalf, target, second); break;
        case Op::Differentiate: answer = guarded(do_diff, target, second); break;
        case Op::Integrate: answer = guarded(do_integrate, target, second); break;
        case Op::Solve: answer = guarded(do_solve, target, second); break;
        case Op::Substitute: answer = guarded(do_subst, target, second); break;
        case Op::Limit: answer = guarded(do_limit, target, second); break;
        case Op::Dot: answer = guarded(do_dot, target, second); break;
        case Op::Cross: answer = guarded(do_cross, target, second); break;
        case Op::Norm: answer = guarded(do_norm, target, second); break;
        case Op::Sin: answer = guarded(do_sin, target, second); break;
        case Op::Cos: answer = guarded(do_cos, target, second); break;
        case Op::Atan2: answer = guarded(do_atan2, target, second); break;
        case Op::Ref: answer = guarded(do_ref, target, second); break;
        case Op::Rref: answer = guarded(do_rref, target, second); break;
    }

    // Giac clears these before each of its own top level evaluations (global.h line 440) and this
    // path calls simplify and friends rather than caseval, so a request that ends interrupted has to
    // clear them or every later one re-enters with the interruption still pending.
    if (giac::ctrl_c || giac::interrupted) {
        giac::ctrl_c = false;
        giac::interrupted = false;
        out->tag = ResultTag::Cancelled;
        out->detail = "Giac was interrupted";
        return true;
    }

    // Giac is built here with NO_STDEXCEPT, so a failure comes back as an undef gen carrying the
    // message rather than as a thrown exception. Cancellation is read from the globals above and
    // memory exhaustion is only ever named in the message, so this one branch stays prose.
    if (giac::is_undef(answer)) {
        const std::string message =
            answer.type == giac::_STRNG ? *answer._STRNGptr : std::string("Giac refused the call");
        out->detail = message;
        out->tag = message.find("emory") != std::string::npos ||
                           message.find("tack") != std::string::npos
                       ? ResultTag::ResourceFailure
                       : ResultTag::Unevaluated;
        return true;
    }

    if (matrix_op) {
        out->tag = matrix_from_gen(arena, request, answer, &out->value, &out->detail);
        // Giac answered. Every resource refusal matrix_from_gen can return is our arena.
        out->from_backend = out->tag != ResultTag::ResourceFailure;
        return true;
    }

    if (request.op == Op::Solve) {
        out->shape = ResultShape::FiniteSolutions;
        if (answer.type != giac::_VECT) {
            out->tag = ResultTag::MalformedResult;
            out->detail = "solve did not return a solution collection";
            return true;
        }
        if (answer._VECTptr->size() > arena.limits().max_nodes) {
            out->tag = ResultTag::ResourceFailure;
            out->detail = "the solution collection exceeds the shared node limit";
            out->from_backend = false;
            return true;
        }
        for (const gen &root : *answer._VECTptr) {
            NodeId value;
            if (!from_gen(arena, root, &value, &why)) {
                out->values.clear();
                refuse_conversion(arena, out);
                out->detail = why;
                return true;
            }
            out->values.push_back(value);
        }
        out->tag = contains_decimal(answer) ? ResultTag::Approximate : ResultTag::Exact;
        return true;
    }

    if (request.op == Op::Cross) {
        out->shape = ResultShape::Vector;
        if (answer.type != giac::_VECT || answer._VECTptr->size() != 3) {
            out->tag = ResultTag::MalformedResult;
            out->detail = "cross returned something other than a three component vector";
            return true;
        }
        for (const gen &component : *answer._VECTptr) {
            NodeId value;
            if (!from_gen(arena, component, &value, &why)) {
                out->values.clear();
                refuse_conversion(arena, out);
                out->detail = why;
                return true;
            }
            out->values.push_back(value);
        }
        out->tag = contains_decimal(answer) ? ResultTag::Approximate : ResultTag::Exact;
        return true;
    }

    bool empty = false, wide = false;
    gen single;
    if (!peel_singleton(answer, &single, &empty, &wide)) {
        out->tag = ResultTag::UnsupportedOperation;
        out->detail = empty ? "the backend returned an empty solution set"
                            : "the backend returned more than one solution and this carries one";
        return true;
    }

    NodeId value;
    if (!from_gen(arena, single, &value, &why)) {
        refuse_conversion(arena, out);
        out->detail = why;
        return true;
    }

    out->value = value;
    out->tag = contains_decimal(single) ? ResultTag::Approximate : ResultTag::Exact;
    return true;
}

namespace {

// The typed backend with its typed path switched off, so the same class answers through eval and
// the comparison below is between two paths and not between two backends.
class StringOnlyBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *out, std::string *error) override {
        return typed_.eval(command, out, error);
    }

  private:
    TypedGiacBackend typed_;
};

struct DifferentialCase {
    Op op;
    const char *target;
    const char *variable;
    const char *target_components[3] = {};
    const char *argument_components[3] = {};
    size_t rank = 0;
    const char *argument = 0;
};

struct ExactDecimalCase {
    Op op;
    const char *target;
    const char *variable;
    const char *expected;
};

const DifferentialCase *differential_cases(size_t *count) {
    static const DifferentialCase cases[] = {
        {Op::Simplify, "(3*x + 6)/3", 0},
        {Op::Simplify, "x + x + x", 0},
        {Op::Expand, "(x + 1)*(x + 2)", 0},
        {Op::Factor, "x^2 + 3*x + 2", 0},
        {Op::Differentiate, "x^2*sin(x)", "x"},
        {Op::Differentiate, "1/x", "x"},
        {Op::Integrate, "2*x", "x"},
        {Op::Integrate, "cos(x)", "x"},
        {Op::Solve, "2*x + 5 = 13", "x"},
        {Op::Solve, "3*x = 7", "x"},
        {Op::Simplify, "12345678901234567890 + 1", 0},
        {Op::Simplify, "1/3 + 1/6", 0},
        {Op::Approximate, "1/4", 0},
        {Op::Simplify, "sqrt(4)", 0},
        {Op::Differentiate, "exp(2*x)", "x"},
        {Op::Simplify, "x^0", 0},
        {Op::Dot, 0, 0, {"1", "2", 0}, {"3", "4", 0}, 2},
        {Op::Cross, 0, 0, {"1", "0", "0"}, {"0", "1", "0"}, 3},
        {Op::Norm, 0, 0, {"3", "4", 0}, {0, 0, 0}, 2},
        {Op::Sin, "pi/6", 0},
        {Op::Cos, "pi/3", 0},
        {Op::Atan2, "1", 0, {}, {}, 0, "1"},
    };
    *count = sizeof(cases) / sizeof(cases[0]);
    return cases;
}

std::string describe(const Arena &arena, const Response &r) {
    std::string s = tag_name(r.tag);
    if (r.value != kNoNode)
        s += " " + print(arena, r.value);
    else if (r.shape != ResultShape::Scalar) {
        s += " [";
        for (size_t i = 0; i < r.values.size(); ++i) {
            if (i)
                s += ",";
            s += print(arena, r.values[i]);
        }
        s += "]";
    }
    else if (!r.detail.empty())
        s += " (" + r.detail + ")";
    return s;
}

int exact_decimal_checks(std::string *report) {
    static const ExactDecimalCase cases[] = {
        {Op::Simplify, "0.1 + 0.2", 0, "3/10"},
        {Op::Simplify, "0.12345678901234567890123456789 * 1e29", 0,
         "12345678901234567890123456789"},
        {Op::Simplify, "-0.125 * 8", 0, "-1"},
        {Op::Simplify, "1.25e+3", 0, "1250"},
        {Op::Simplify, "1.25e-3 * 800", 0, "1"},
        {Op::Solve, "0.1*x = 0.3", "x", "3"},
    };

    TypedGiacBackend backend;
    int failures = 0;
    for (const ExactDecimalCase &c : cases) {
        Arena arena;
        const ParseResult parsed = parse(arena, c.target);
        if (!parsed.ok()) {
            *report += std::string("exact decimal ") + c.target + ": input did not parse\n";
            ++failures;
            continue;
        }
        Request request;
        request.op = c.op;
        request.target = parsed.root;
        if (c.variable)
            request.variable = arena.symbol(c.variable);
        const Response response = Adapter(arena, backend).run(request);
        const ParseResult expected = parse(arena, c.expected);
        const NodeId actual_canonical =
            response.single_value() == kNoNode ? kNoNode : canonicalize(arena, response.single_value());
        const NodeId expected_canonical =
            expected.ok() ? canonicalize(arena, expected.root) : kNoNode;
        const bool passed = response.tag == ResultTag::Exact && actual_canonical != kNoNode &&
                            expected_canonical != kNoNode &&
                            print(arena, actual_canonical) == print(arena, expected_canonical);
        *report += std::string("exact decimal ") + c.target + ": ";
        if (passed) {
            *report += "exact\n";
        } else {
            ++failures;
            *report += "FAILED " + describe(arena, response) + "\n";
        }
    }

    {
        Arena arena;
        gen converted;
        std::string why;
        const NodeId boundary = arena.decimal("1e4095");
        const bool passed = to_gen(arena, boundary, &converted, &why) && !giac::is_undef(converted);
        *report += passed ? "exact decimal bounded exponent: accepted\n"
                          : "exact decimal bounded exponent: FAILED " + why + "\n";
        if (!passed)
            ++failures;
    }

    const std::string overlong(Limits().max_input_bytes + 1, '1');
    const std::string refused[] = {"1e4096", "1e+", "1.2.3", overlong};
    for (const std::string &literal : refused) {
        Arena arena;
        Request request;
        request.op = Op::Simplify;
        request.target = arena.decimal(literal);
        TypedResult response;
        const bool passed = backend.typed(request, arena, &response) &&
                            response.tag == ResultTag::UnsupportedOperation &&
                            !response.detail.empty();
        *report += passed ? "exact decimal refusal: refused\n"
                          : "exact decimal refusal: FAILED\n";
        if (!passed)
            ++failures;
    }
    {
        Arena arena;
        Request request;
        request.op = Op::Simplify;
        request.target = arena.integer(overlong);
        TypedResult response;
        const bool passed = backend.typed(request, arena, &response) &&
                            response.tag == ResultTag::UnsupportedOperation &&
                            response.detail.find("shared input limit") != std::string::npos;
        *report += passed ? "exact integer refusal: refused\n"
                          : "exact integer refusal: FAILED\n";
        if (!passed)
            ++failures;
    }
    {
        Arena arena;
        gen converted;
        std::string why;
        const NodeId boundary = arena.integer(std::string(Limits().max_input_bytes, '9'));
        const bool passed = to_gen(arena, boundary, &converted, &why) && !giac::is_undef(converted);
        *report += passed ? "exact integer input boundary: accepted\n"
                          : "exact integer input boundary: FAILED\n";
        if (!passed)
            ++failures;
    }
    return failures;
}

int conversion_boundary_checks(std::string *report) {
    int failures = 0;
    auto record = [&](bool passed, const std::string &label) {
        *report += "boundary " + label + (passed ? ": passed\n" : ": FAILED\n");
        if (!passed)
            ++failures;
    };

    const struct { const char *what; gen value; } sentinels[] = {
        {"unsigned_inf", giac::unsigned_inf},
        {"plus_inf", giac::plus_inf},
        {"minus_inf", giac::minus_inf},
        {"undef", giac::undef},
        {"1+minus_inf", giac::symbolic(giac::at_plus, giac::makevecteur(gen(1), giac::minus_inf))},
    };
    for (const auto &sentinel : sentinels) {
        Arena arena;
        NodeId converted = kNoNode;
        std::string why;
        record(!from_gen(arena, sentinel.value, &converted, &why) && converted == kNoNode &&
                   !why.empty(),
               std::string("sentinel refusal ") + sentinel.what);
    }
    {
        Arena arena;
        NodeId converted = kNoNode;
        std::string why;
        record(from_gen(arena, gen(giac::identificateur("y")), &converted, &why) &&
                   converted != kNoNode && arena.text(converted) == "y",
               "ordinary identifier still converts");
    }

    TypedGiacBackend backend;
    {
        Arena arena;
        Request request{Op::Simplify, parse(arena, "x+1").root};
        TypedResult reply;
        giac::ctrl_c = true;
        const bool answered = backend.typed(request, arena, &reply);
        giac::ctrl_c = false;
        giac::interrupted = false;
        record(answered && reply.tag == ResultTag::Cancelled && reply.value == kNoNode,
               "a pending interruption cancels before reaching Giac");
    }
    {
        Limits limits;
        limits.max_nodes = 16;
        Arena arena(limits);
        Request request{Op::Simplify, parse(arena, "1+1").root};
        while (!arena.failed())
            arena.integer(integer_text(static_cast<int64_t>(arena.node_count())));
        TypedResult reply;
        record(backend.typed(request, arena, &reply) && reply.tag == ResultTag::ResourceFailure,
               "an exhausted arena reports a resource failure");
    }

    const double approximations[] = {0.25, 1.0 / 3.0, 1.0 / 300.0, -1.0 / 300.0, 2.0 / 7.0,
                                     1.0 / 1024.0, 1e17, 0.0};
    for (double value : approximations) {
        Arena arena;
        NodeId converted = kNoNode;
        std::string why;
        Rational read;
        const bool passed = from_gen(arena, gen(value), &converted, &why) && converted != kNoNode &&
                            rational_from_text(arena.text(converted), &read);
        record(passed, "approximate text the engine can read " +
                           (converted == kNoNode ? why : arena.text(converted)));
    }
    return failures;
}

int finite_solution_checks(std::string *report) {
    struct Case {
        const char *equation;
        const char *roots[2];
        size_t count;
    };
    const Case cases[] = {
        {"3*x^2-12=0", {"-2", "2"}, 2},
        {"3*x*x-12=0", {"-2", "2"}, 2},
        {"4*x^2=1", {"-1/2", "1/2"}, 2},
        {"x^2=0", {"0", nullptr}, 1},
        {"x^2=-4", {nullptr, nullptr}, 0},
    };
    // One backend across every case, so a genuine terminal reply anywhere in the loop latches and
    // the cases after it are refused without being asked. That is the latch doing what it says, and
    // it makes the report read as if those cases ran and failed.
    TypedGiacBackend typed;
    StringOnlyBackend string;
    int failures = 0;
    for (Backend *backend : {static_cast<Backend *>(&typed), static_cast<Backend *>(&string)}) {
        for (const Case &c : cases) {
            Arena arena;
            Request request;
            request.op = Op::Solve;
            request.target = parse(arena, c.equation).root;
            request.variable = arena.symbol("x");
            const Response reply = Adapter(arena, *backend).run(request);
            bool passed = reply.tag == ResultTag::Exact && reply.usable() &&
                          reply.shape == ResultShape::FiniteSolutions && reply.values.size() == c.count;
            std::vector<std::string> actual, expected;
            for (NodeId root : reply.values) {
                const NodeId canonical = canonicalize(arena, root);
                passed = passed && canonical != kNoNode;
                actual.push_back(print(arena, canonical));
            }
            for (size_t i = 0; i < c.count; ++i) {
                const NodeId canonical = canonicalize(arena, parse(arena, c.roots[i]).root);
                passed = passed && canonical != kNoNode;
                expected.push_back(print(arena, canonical));
            }
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            passed = passed && actual == expected;
            *report += std::string("finite solutions ") + (backend == &typed ? "typed " : "string ") +
                       c.equation + (passed ? ": expected roots, " : ": FAILED, ") +
                       describe(arena, reply) + "\n";
            if (!passed)
                ++failures;
        }
    }
    return failures;
}

int matrix_checks(std::string *report) {
    struct MatrixCase {
        Op op;
        const char *input;
        const char *expected;
        const char *without_steps = nullptr;
    };
    const MatrixCase cases[] = {
        {Op::Ref, "[[2]]", "[[1]]"},
        {Op::Rref, "[[2]]", "[[1]]"},
        {Op::Ref, "[[0,2]]", "[[0,2]]", "[[0,1]]"},
        {Op::Rref, "[[0,2]]", "[[0,1]]"},
        {Op::Ref, "[[1,2,3],[2,4,6]]", "[[1,2,3],[0,0,0]]"},
        {Op::Rref, "[[1,2,3],[2,4,6]]", "[[1,2,3],[0,0,0]]"},
        {Op::Ref, "[[1/2,1/3],[0,1/4]]", "[[1,2/3],[0,1]]"},
        {Op::Rref, "[[1/2,1/3],[0,1/4]]", "[[1,0],[0,1]]"},
    };
    // One backend across every case, so a genuine terminal reply anywhere in the loop latches and
    // the cases after it are refused without being asked.
    TypedGiacBackend typed;
    StringOnlyBackend strings;
    int failures = 0;
    auto record = [&](bool passed, const std::string &label) {
        *report += "matrix " + label + (passed ? ": passed\n" : ": FAILED\n");
        if (!passed)
            ++failures;
    };
    for (const MatrixCase &c : cases) {
        for (Backend *backend : {static_cast<Backend *>(&typed), static_cast<Backend *>(&strings)}) {
            Arena arena;
            Request request{c.op, parse(arena, c.input).root};
            const Response reply = Adapter(arena, *backend).run(request);
            const auto actual = MatrixView::from(arena, reply.value);
            const char *expected_text = c.without_steps && giac::step_infolevel(context_pointer()) == 0 ?
                c.without_steps : c.expected;
            const auto expected = MatrixView::from(arena, parse(arena, expected_text).root);
            bool passed = reply.tag == ResultTag::Exact && reply.usable() &&
                          reply.shape == ResultShape::Matrix && reply.single_value() == kNoNode &&
                          reply.values.empty() && actual && expected &&
                          actual->rows() == expected->rows() && actual->columns() == expected->columns();
            for (size_t row = 0; passed && row < actual->rows(); ++row) {
                for (size_t column = 0; passed && column < actual->columns(); ++column) {
                    Rational lhs, rhs;
                    passed = read_matrix_rational(arena, actual->cell(row, column), &lhs) &&
                             read_matrix_rational(arena, expected->cell(row, column), &rhs) &&
                             lhs.num == rhs.num && lhs.den == rhs.den;
                }
            }
            record(passed, std::string(backend == &typed ? "typed " : "string ") +
                               op_name(c.op) + " " + c.input + " " + describe(arena, reply));
        }
    }
    for (const char *input : {"[]", "[1]", "[[1],[2,3]]", "[[[1]]]", "[[1.0]]",
                             "[[x]]", "[[i]]", "[[1,2,3,4,5,6,7]]"}) {
        Arena arena;
        Request request{Op::Rref, parse(arena, input).root};
        TypedResult reply;
        typed.typed(request, arena, &reply);
        record(reply.tag == ResultTag::UnsupportedOperation && reply.value == kNoNode,
               std::string("direct typed refusal ") + input);
    }
    {
        Arena arena;
        const NodeId input = parse(arena, "[[1/2,-2/3,1],[0,3,4]]").root;
        const Request request{Op::Rref, input};
        gen converted;
        std::string why;
        NodeId recovered = kNoNode;
        const bool converted_ok = matrix_to_gen(arena, input, &converted, &why);
        const ResultTag tag = converted_ok
                                  ? matrix_from_gen(arena, request, converted, &recovered, &why)
                                  : ResultTag::UnsupportedOperation;
        const NodeId left = canonicalize(arena, input);
        const NodeId right = recovered == kNoNode ? kNoNode : canonicalize(arena, recovered);
        record(tag == ResultTag::Exact && right != kNoNode &&
                   print(arena, left) == print(arena, right),
               "rectangular rational conversion round trip");
    }
    struct RefusalCase { const char *reply; ResultTag tag; };
    const RefusalCase refusals[] = {
        {"1", ResultTag::MalformedResult},
        {"[1]", ResultTag::MalformedResult},
        {"[]", ResultTag::MalformedResult},
        {"[[[1]]]", ResultTag::MalformedResult},
        {"[[1,2]]", ResultTag::MalformedResult},
        {"[[1],[2]]", ResultTag::MalformedResult},
        {"[[x]]", ResultTag::UnsupportedOperation},
        {"[[i]]", ResultTag::UnsupportedOperation},
        {"[[when(x,1,0)]]", ResultTag::UnsupportedOperation},
    };
    for (const RefusalCase &c : refusals) {
        Arena arena;
        const Request request{Op::Ref, parse(arena, "[[1]]").root};
        const gen answer(std::string(c.reply), context_pointer());
        NodeId converted = kNoNode;
        std::string why;
        const ResultTag tag = matrix_from_gen(arena, request, answer, &converted, &why);
        record(tag == c.tag && converted == kNoNode, std::string("typed conversion refusal ") + c.reply);
    }
    {
        Arena arena;
        const Request request{Op::Ref, parse(arena, "[[1]]").root};
        const gen answer(giac::makevecteur(gen(giac::makevecteur(gen(0.5)), 0)), 0);
        NodeId converted = kNoNode;
        std::string why;
        const ResultTag tag = matrix_from_gen(arena, request, answer, &converted, &why);
        record(tag == ResultTag::Approximate && MatrixView::from(arena, converted).has_value(),
               "typed approximate output keeps matrix shape");
    }
    return failures;
}

}  // namespace

int typed_differential_check(std::string *report) {
    size_t count = 0;
    const DifferentialCase *cases = differential_cases(&count);

    // One backend across every case, so a genuine terminal reply anywhere in the loop latches and
    // the cases after it are refused without being asked.
    TypedGiacBackend typed_backend;
    StringOnlyBackend string_backend;

    int disagreements = exact_decimal_checks(report) + conversion_boundary_checks(report) +
                        finite_solution_checks(report) + matrix_checks(report);
    for (size_t i = 0; i < count; ++i) {
        const DifferentialCase &c = cases[i];

        // One arena per case, so a canonical form from an earlier case cannot be the reason two
        // answers here compare equal.
        Arena typed_arena, string_arena;
        Adapter typed_adapter(typed_arena, typed_backend);
        Adapter string_adapter(string_arena, string_backend);

        Request typed_request, string_request;
        typed_request.op = string_request.op = c.op;

        bool parsed_case = true;
        if (c.rank) {
            for (size_t component = 0; component < c.rank; ++component) {
                ParseResult tp = parse(typed_arena, c.target_components[component]);
                ParseResult sp = parse(string_arena, c.target_components[component]);
                if (!tp.ok() || !sp.ok()) {
                    *report += std::string(op_name(c.op)) + ": a vector component did not parse\n";
                    ++disagreements;
                    parsed_case = false;
                    break;
                }
                typed_request.target_components.push_back(tp.root);
                string_request.target_components.push_back(sp.root);
                if (c.op != Op::Norm) {
                    tp = parse(typed_arena, c.argument_components[component]);
                    sp = parse(string_arena, c.argument_components[component]);
                    if (!tp.ok() || !sp.ok()) {
                        *report += std::string(op_name(c.op)) + ": a vector component did not parse\n";
                        ++disagreements;
                        parsed_case = false;
                        break;
                    }
                    typed_request.argument_components.push_back(tp.root);
                    string_request.argument_components.push_back(sp.root);
                }
            }
            if (!parsed_case)
                continue;
        } else {
            ParseResult tp = parse(typed_arena, c.target);
            ParseResult sp = parse(string_arena, c.target);
            if (!tp.ok() || !sp.ok()) {
                *report += std::string(c.target) + ": the case itself did not parse\n";
                ++disagreements;
                continue;
            }
            typed_request.target = tp.root;
            string_request.target = sp.root;
        }
        if (c.variable) {
            typed_request.variable = typed_arena.symbol(c.variable);
            string_request.variable = string_arena.symbol(c.variable);
        }
        if (c.argument) {
            const ParseResult tp = parse(typed_arena, c.argument);
            const ParseResult sp = parse(string_arena, c.argument);
            if (!tp.ok() || !sp.ok()) {
                *report += std::string(op_name(c.op)) + ": the second argument did not parse\n";
                ++disagreements;
                continue;
            }
            typed_request.argument = tp.root;
            string_request.argument = sp.root;
        }

        const Response t = typed_adapter.run(typed_request);
        const Response s = string_adapter.run(string_request);

        bool same = t.tag == s.tag && t.shape == s.shape;
        if (same && t.value != kNoNode && s.value != kNoNode) {
            const NodeId tc = canonicalize(typed_arena, t.value);
            const NodeId sc = canonicalize(string_arena, s.value);
            same = tc != kNoNode && sc != kNoNode &&
                   print(typed_arena, tc) == print(string_arena, sc);
        } else if (same && (!t.values.empty() || !s.values.empty())) {
            same = t.values.size() == s.values.size();
            for (size_t component = 0; same && component < t.values.size(); ++component) {
                const NodeId tc = canonicalize(typed_arena, t.values[component]);
                const NodeId sc = canonicalize(string_arena, s.values[component]);
                same = tc != kNoNode && sc != kNoNode &&
                       print(typed_arena, tc) == print(string_arena, sc);
            }
        } else if (same) {
            same = t.value == kNoNode && s.value == kNoNode;
        }

        *report += std::string(op_name(c.op)) + " " + (c.target ? c.target : "vector") + ": ";
        if (same) {
            *report += "agree, " + describe(typed_arena, t) + "\n";
        } else {
            ++disagreements;
            *report += "DISAGREE typed=" + describe(typed_arena, t) +
                       " string=" + describe(string_arena, s) + " raw=" + s.raw + "\n";
        }
    }
    return disagreements;
}

}  // namespace nps
