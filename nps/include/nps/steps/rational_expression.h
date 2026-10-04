#ifndef NPS_STEPS_RATIONAL_EXPRESSION_H
#define NPS_STEPS_RATIONAL_EXPRESSION_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/rational.h"
#include "nps/steps/derivation.h"

namespace nps {

// Coefficient size the polynomial arithmetic stops at, in bits of numerator or denominator.
inline constexpr size_t kPolyCoefficientBits = 512;
inline constexpr int kPolyMaxDegree = 12;

// A polynomial in one variable with exact GMP rational coefficients, lowest degree first. The zero
// polynomial has no coefficients, so degree() is -1 for it.
class Poly {
  public:
    Poly() = default;
    explicit Poly(size_t count);
    Poly(const Poly &other);
    Poly(Poly &&other) noexcept;
    Poly &operator=(Poly other) noexcept;
    ~Poly();

    static Poly constant(int64_t value);
    static Poly variable();

    int degree() const { return static_cast<int>(c_.size()) - 1; }
    bool zero() const { return c_.empty(); }
    size_t size() const { return c_.size(); }
    mpq_ptr at(size_t index) { return &c_[index]; }
    mpq_srcptr at(size_t index) const { return &c_[index]; }
    // Drops leading zero coefficients so degree() is the true degree.
    void trim();
    // Whether every coefficient fits kPolyCoefficientBits.
    bool bounded() const;

  private:
    void swap(Poly &other) noexcept { c_.swap(other.c_); }
    std::vector<__mpq_struct> c_;
};

// Each returns false when a coefficient outgrows kPolyCoefficientBits or the degree kPolyMaxDegree.
bool poly_add(const Poly &a, const Poly &b, Poly *out);
bool poly_sub(const Poly &a, const Poly &b, Poly *out);
bool poly_mul(const Poly &a, const Poly &b, Poly *out);
// a = quotient * b + remainder with deg remainder < deg b. False for a zero divisor as well.
bool poly_divide(const Poly &a, const Poly &b, Poly *quotient, Poly *remainder);
// The monic greatest common divisor. Two zero polynomials have none, which is false.
bool poly_gcd(const Poly &a, const Poly &b, Poly *out);
void poly_evaluate(const Poly &p, mpq_srcptr at, mpq_ptr out);
bool poly_derivative(const Poly &p, Poly *out);
bool poly_equal(const Poly &a, const Poly &b);

enum class RootSearch : uint8_t { Found, OutOfRange, Halted };

// The distinct rational roots of p, each with its multiplicity, and what is left once they are
// divided out. The search tries every p over q the rational root theorem allows, so it is bounded
// by the size of the integer coefficients it scales to.
RootSearch poly_rational_roots(const Poly &p, Meter &meter, std::vector<Rational> *roots,
                               std::vector<int> *multiplicities, Poly *remainder);

enum class RationalGoal : uint8_t { Normal, PartialFractions };

enum class RationalOutcome : uint8_t {
    Rewritten,
    NotRational,
    InvalidInput,
    OutsideEnvelope,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *rational_outcome_name(RationalOutcome outcome);

struct RationalResult {
    RationalOutcome outcome = RationalOutcome::OutsideEnvelope;
    NodeId expression = kNoNode;
    std::string detail;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

// ALG-005. Rewrites a rational expression in one variable into one reduced fraction, keeping every
// value its denominators excluded as a published condition, including those a cancellation removes.
RationalResult rational_expression(Arena &arena, Derivation &derivation, NodeId expression,
                                   NodeId variable, RationalGoal goal, const Budget &budget = Budget());

}  // namespace nps

#endif
