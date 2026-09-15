#ifndef NPS_PHYSICS_RELATION_H
#define NPS_PHYSICS_RELATION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "nps/units/units.h"

namespace nps {

const size_t kRelationMaxFactors = 4;

// One named position in a relation whose right-hand side is a product of powers.
struct RelationTerm {
    const char *symbol = "?";
    const char *name = "term";
    Dimension dimension;
    int power = 1;
};

// A law of the shape target = constant * product of factor^power, carried exactly.
struct RelationModel {
    const char *family_id = "";
    const char *rule_prefix = "";
    const char *equation_text = "";
    const char *strategy_text = "";
    const char *rule_name = "";
    const char *method_text = "";
    const char *substitution_detail = "";
    // The modelling conditions PHYS-025 wants recorded alongside the formula.
    const char *conditions[3] = {nullptr, nullptr, nullptr};
    Rational constant;
    Dimension constant_dimension;
    const char *constant_symbol = nullptr;
    const char *constant_note = nullptr;
    RelationTerm target;
    RelationTerm factors[kRelationMaxFactors];
    size_t factor_count = 0;
};

// Index 0 is the target and 1 through factor_count are the factors, in model order.
const size_t kRelationTarget = 0;

size_t relation_term_count(const RelationModel &model);
const RelationTerm &relation_term(const RelationModel &model, size_t index);

enum class RelationOutcome : uint8_t {
    Solved,
    InvalidProblem,
    MissingKnown,
    DuplicateKnown,
    DimensionMismatch,
    // The unknown occurs at a power this exact linear path cannot isolate.
    UnsupportedUnknown,
    ArithmeticOverflow,
    VerificationFailed,
    Cancelled,
    ResourceExceeded,
};

const char *relation_outcome_name(RelationOutcome outcome);

struct RelationKnown {
    size_t index = 0;
    Quantity quantity;
};

struct RelationProblem {
    size_t unknown = 0;
    std::vector<RelationKnown> knowns;
};

struct RelationResult {
    RelationOutcome outcome = RelationOutcome::InvalidProblem;
    Quantity quantity;
    std::string value_text;
    std::string unit_text;
    std::string detail;
    NodeId value = kNoNode;
    NodeId unknown = kNoNode;
    NodeId equation = kNoNode;
    NodeId substituted = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
};

RelationResult solve_relation(Arena &arena, Derivation &derivation, const RelationModel &model,
                              const RelationProblem &problem, const Budget &budget = Budget());

}  // namespace nps

#endif
