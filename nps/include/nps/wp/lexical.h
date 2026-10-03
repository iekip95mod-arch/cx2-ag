#ifndef NPS_WP_LEXICAL_H
#define NPS_WP_LEXICAL_H

#include <cstdint>
#include <string>
#include <vector>

#include "nps/units/units.h"
#include "nps/wp/problem_ir.h"

namespace nps::wp {

// PRD section 26.16, WP Milestone 1. Reads prose into grounded quantities, units and lexicon concepts
// with spans into the original text. It proposes nothing about the problem and never solves it.
constexpr const char *kLexiconVersion = "wp1-lexicon 1";
constexpr const char *kNormalizationProfile = "wp1-normalize 1";

// WP-040. The normalized text, with each normalized byte mapped to the original bytes it came from.
SourceDocument normalize_source(const std::string &source_id, const std::string &original_utf8);

// A normalized half-open range back in the original text, with the original surface.
Span span_from_normalized(const SourceDocument &source, size_t normalized_begin, size_t normalized_end);

struct GroundedQuantity {
    std::string value_text;
    std::string unit_text;
    nps::Quantity quantity;
    Span span;
};

struct LexiconConcept {
    std::string concept_id;
    Span span;
};

enum class LexFault : uint8_t {
    UnsupportedCharacter,
    UnknownUnit,
    Range,
    Tolerance,
    Percentage,
    ScientificNotation,
};

const char *lex_fault_name(LexFault fault);

struct LexFailure {
    LexFault fault;
    std::string detail;
    Span span;
};

// Kept apart so a refusal to read a construction never reads as running out of room or being stopped.
enum class LexStatus : uint8_t {
    Grounded,
    Unsupported,
    ResourceExhausted,
    Cancelled,
};

const char *lex_status_name(LexStatus status);

// WP-018. Provisional like every other budget until the calculator measures them.
struct LexicalLimits {
    size_t max_bytes = 1024;
    size_t max_tokens = 256;
    size_t max_quantities = 32;
    bool (*poll)(void *) = nullptr;
    void *poll_context = nullptr;
};

struct LexicalReading {
    LexStatus status = LexStatus::Grounded;
    std::string detail;
    SourceDocument source;
    std::vector<GroundedQuantity> quantities;
    std::vector<LexiconConcept> concepts;
    std::vector<LexFailure> failures;
    size_t tokens = 0;
};

LexicalReading read_lexical(const std::string &source_id, const std::string &original_utf8,
                            const LexicalLimits &limits = LexicalLimits());

// The reading as an untrusted InterpretationSet with one candidate and no proposed problem model.
InterpretationSet lexical_interpretation(const LexicalReading &reading);

// One line per grounded quantity, concept and failure, as the calculator would list them.
std::string lexical_summary(const LexicalReading &reading);

}  // namespace nps::wp

#endif
