#ifndef NPS_EVALUATE_H
#define NPS_EVALUATE_H

#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/rational.h"

namespace nps {

struct SymbolValue {
    std::string symbol;
    Rational value;
};

bool evaluate_rational_function(const std::string &name, const Rational &argument, Rational *out);

// The exact value of an expression once every symbol in it has a rational value. False when a
// symbol has none, when a form has no rational value here, or when the arithmetic left what an
// int64 rational can hold. A false is never a zero: a caller that cannot tell the two apart would
// report a disagreement as agreement.
bool evaluate_rational(const Arena &arena, NodeId id, const std::vector<SymbolValue> &values,
                       Rational *out);

// Every symbol name in the expression, once each, in ascending order so two runs of the same
// expression assign the same values to the same names.
void collect_symbols(const Arena &arena, NodeId id, std::vector<std::string> *out);

struct SampleAgreement {
    size_t evaluated = 0;
    size_t agreed = 0;
    // Where they first differed, for the record. Empty when they never did.
    std::string disagreement;
};

// Whether two expressions take the same exact value at a fixed series of rational assignments of
// their symbols. A sample where either side has no value is skipped rather than counted, so the
// caller can tell "they agree" from "there was nothing to compare".
//
// PRD section 17 forbids claiming a numerical spot check proves a symbolic identity, so this
// answers the question it can answer: a disagreement is proof they differ, and agreement is
// evidence they do not. Callers word the verification record that way.
SampleAgreement agrees_on_samples(const Arena &arena, NodeId left, NodeId right, size_t samples);

}  // namespace nps

#endif
