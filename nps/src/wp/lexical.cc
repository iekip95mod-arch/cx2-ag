#include "nps/wp/lexical.h"

#include <algorithm>
#include <string_view>

#include "nps/core/budgets.h"

namespace nps::wp {
namespace {

// Typographic spellings a keypad or a paste produces, each read as the ASCII the unit parser takes.
struct Replacement {
    std::string_view utf8;
    const char *ascii;
};

const Replacement kReplacements[] = {
    {"\xC2\xB2", "^2"}, {"\xC2\xB3", "^3"},  {"\xC2\xB7", "*"},    {"\xE2\x8B\x85", "*"},
    {"\xC3\x97", "*"},  {"\xE2\x88\x92", "-"}, {"\xE2\x80\x93", "-"}, {"\xC2\xB1", "+/-"},
    {"\xC2\xB5", "u"},  {"\xCE\xBC", "u"},   {"\xE2\x80\x99", "'"}, {"\xC2\xA0", " "},
};

size_t utf8_length(unsigned char lead) {
    if (lead >= 0xF0)
        return 4;
    if (lead >= 0xE0)
        return 3;
    if (lead >= 0xC0)
        return 2;
    return 1;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool same_word(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

enum class TokenKind : uint8_t { Word, Number, Symbol, Foreign };

struct Token {
    TokenKind kind;
    size_t begin;
    size_t end;
};

// Unit names written out, each mapped to the symbol the unit table already knows.
struct UnitWord {
    const char *word;
    const char *symbol;
};

const UnitWord kUnitWords[] = {
    {"metre", "m"},        {"metres", "m"},       {"meter", "m"},          {"meters", "m"},
    {"kilometre", "km"},   {"kilometres", "km"},  {"kilometer", "km"},     {"kilometers", "km"},
    {"centimetre", "cm"},  {"centimetres", "cm"}, {"centimeter", "cm"},    {"centimeters", "cm"},
    {"millimetre", "mm"},  {"millimetres", "mm"}, {"millimeter", "mm"},    {"millimeters", "mm"},
    {"second", "s"},       {"seconds", "s"},      {"millisecond", "ms"},   {"milliseconds", "ms"},
    {"minute", "min"},     {"minutes", "min"},    {"hour", "h"},           {"hours", "h"},
    {"kilogram", "kg"},    {"kilograms", "kg"},   {"gram", "g"},           {"grams", "g"},
    {"newton", "N"},       {"newtons", "N"},      {"joule", "J"},          {"joules", "J"},
    {"ampere", "A"},       {"amperes", "A"},      {"amp", "A"},            {"amps", "A"},
    {"coulomb", "C"},      {"coulombs", "C"},     {"volt", "V"},           {"volts", "V"},
    {"watt", "W"},         {"watts", "W"},        {"ohms", "ohm"},         {"farad", "F"},
    {"farads", "F"},
};

struct NumberWord {
    const char *word;
    const char *digits;
};

const NumberWord kNumberWords[] = {
    {"zero", "0"},     {"one", "1"},       {"two", "2"},        {"three", "3"},     {"four", "4"},
    {"five", "5"},     {"six", "6"},       {"seven", "7"},      {"eight", "8"},     {"nine", "9"},
    {"ten", "10"},     {"eleven", "11"},   {"twelve", "12"},    {"thirteen", "13"}, {"fourteen", "14"},
    {"fifteen", "15"}, {"sixteen", "16"},  {"seventeen", "17"}, {"eighteen", "18"}, {"nineteen", "19"},
    {"twenty", "20"},
};

// PRD section 26.6 names these phrases. The original wording stays in the span.
struct Phrase {
    const char *words;
    const char *concept_id;
};

const Phrase kPhrases[] = {
    {"begins at rest", "initial_speed_equals_zero"},
    {"starts at rest", "initial_speed_equals_zero"},
    {"starts from rest", "initial_speed_equals_zero"},
    {"released from rest", "initial_speed_equals_zero"},
    {"dropped from rest", "initial_speed_equals_zero"},
    {"comes to rest", "final_speed_equals_zero"},
    {"uniform acceleration", "constant_acceleration"},
    {"constant acceleration", "constant_acceleration"},
    {"accelerates uniformly", "constant_acceleration"},
    {"slows down", "acceleration_opposes_velocity"},
    {"instantaneous rate", "derivative_at_state"},
    {"total accumulated", "definite_integral"},
    {"at most", "less_than_or_equal"},
    {"no more than", "less_than_or_equal"},
    {"at least", "greater_than_or_equal"},
};

class Reader {
  public:
    Reader(const LexicalLimits &limits, LexicalReading *out) : limits_(limits), out_(out), text_(out->source.normalized_utf8) {}

    bool tokenize() {
        size_t i = 0;
        while (i < text_.size()) {
            const char c = text_[i];
            if (c == ' ') {
                ++i;
                continue;
            }
            const size_t begin = i;
            TokenKind kind = TokenKind::Symbol;
            const bool sign = c == '-' && i + 1 < text_.size() && is_digit(text_[i + 1]) &&
                              (i == 0 || text_[i - 1] == ' ' || text_[i - 1] == '(');
            if (is_digit(c) || sign || (c == '.' && i + 1 < text_.size() && is_digit(text_[i + 1]))) {
                kind = TokenKind::Number;
                if (sign)
                    ++i;
                bool point = false;
                while (i < text_.size()) {
                    if (is_digit(text_[i])) {
                        ++i;
                    } else if (text_[i] == '.' && !point && i + 1 < text_.size() && is_digit(text_[i + 1])) {
                        point = true;
                        ++i;
                    } else {
                        break;
                    }
                }
            } else if (is_letter(c)) {
                kind = TokenKind::Word;
                while (i < text_.size() && (is_letter(text_[i]) ||
                                            (text_[i] == '\'' && i + 1 < text_.size() && is_letter(text_[i + 1]))))
                    ++i;
            } else if (static_cast<unsigned char>(c) >= 0x80 || c < 0x20) {
                kind = TokenKind::Foreign;
                while (i < text_.size() && (static_cast<unsigned char>(text_[i]) >= 0x80 || text_[i] < 0x20) &&
                       text_[i] != ' ')
                    ++i;
            } else {
                ++i;
            }
            tokens_.push_back({kind, begin, i});
            if (tokens_.size() > limits_.max_tokens)
                return stop(LexStatus::ResourceExhausted, "more than " + std::to_string(limits_.max_tokens) + " tokens");
            if (tokens_.size() % 16 == 0 && polled())
                return false;
        }
        out_->tokens = tokens_.size();
        return true;
    }

    bool scan() {
        size_t k = 0;
        while (k < tokens_.size()) {
            if (polled())
                return false;
            const Token &t = tokens_[k];
            if (t.kind == TokenKind::Foreign) {
                refuse(LexFault::UnsupportedCharacter, "a character outside the supported text", k, k + 1);
                ++k;
            } else if (symbol(k, '%')) {
                refuse(LexFault::Percentage, "a percentage is not read yet", k, k + 1);
                ++k;
            } else if (symbol(k, '+') && adjacent(k) && symbol(k + 1, '/') && adjacent(k + 1) && symbol(k + 2, '-')) {
                refuse(LexFault::Tolerance, "a tolerance is not read yet", k, k + 3);
                k += 3;
            } else if (t.kind == TokenKind::Number) {
                if (!number(k, text(k), false, &k))
                    return false;
            } else if (t.kind == TokenKind::Word) {
                if (const char *digits = number_word(text(k)); digits && unit_follows(k + 1)) {
                    if (!number(k, digits, true, &k))
                        return false;
                } else if (!phrase(&k)) {
                    ++k;
                }
            } else {
                ++k;
            }
        }
        return true;
    }

  private:
    std::string_view text(size_t k) const {
        return std::string_view(text_).substr(tokens_[k].begin, tokens_[k].end - tokens_[k].begin);
    }

    bool symbol(size_t k, char c) const {
        return k < tokens_.size() && tokens_[k].kind == TokenKind::Symbol && text_[tokens_[k].begin] == c;
    }

    bool word(size_t k, std::string_view w) const {
        return k < tokens_.size() && tokens_[k].kind == TokenKind::Word && same_word(text(k), w);
    }

    bool numeral(size_t k) const { return k < tokens_.size() && tokens_[k].kind == TokenKind::Number; }

    bool adjacent(size_t k) const { return k + 1 < tokens_.size() && tokens_[k].end == tokens_[k + 1].begin; }

    bool polled() {
        if (limits_.poll && limits_.poll(limits_.poll_context)) {
            stop(LexStatus::Cancelled, "reading was cancelled");
            return true;
        }
        return false;
    }

    bool stop(LexStatus status, std::string detail) {
        out_->status = status;
        out_->detail = std::move(detail);
        return false;
    }

    void refuse(LexFault fault, std::string detail, size_t first, size_t last) {
        out_->failures.push_back({fault, std::move(detail),
                                  span_from_normalized(out_->source, tokens_[first].begin, tokens_[last - 1].end)});
    }

    static const char *number_word(std::string_view w) {
        for (const NumberWord &n : kNumberWords) {
            if (same_word(w, n.word))
                return n.digits;
        }
        return nullptr;
    }

    std::string atom(size_t k) const {
        if (k >= tokens_.size() || tokens_[k].kind != TokenKind::Word)
            return std::string();
        for (const UnitWord &u : kUnitWords) {
            if (same_word(text(k), u.word))
                return u.symbol;
        }
        Unit unit;
        std::string error;
        const std::string spelled(text(k));
        return parse_unit(spelled, &unit, &error) ? spelled : std::string();
    }

    bool unit_follows(size_t k) const { return !atom(k).empty(); }

    // A symbol after a space joins the unit only when a power or a division follows it, so "5 m in" stays metres.
    bool spaced_factor(size_t k) const {
        if (k >= tokens_.size() || tokens_[k].kind != TokenKind::Word || !(symbol(k + 1, '^') || symbol(k + 1, '/')))
            return false;
        Unit unit;
        std::string error;
        return parse_unit(std::string(text(k)), &unit, &error);
    }

    // Reads the longest unit spelling from token k. False with *broken set when an operator was
    // written and what follows it is not a unit, which is a refusal rather than a shorter unit.
    bool unit(size_t k, std::string *spelling, size_t *next, bool *broken) const {
        *broken = false;
        std::string built = atom(k);
        if (built.empty()) {
            const bool operator_after = symbol(k + 1, '/') || symbol(k + 1, '^') || word(k + 1, "per");
            *broken = k < tokens_.size() && tokens_[k].kind == TokenKind::Word && operator_after;
            if (*broken)
                *next = std::min(k + 3, tokens_.size());
            return false;
        }
        size_t at = k + 1;
        while (at < tokens_.size()) {
            if (symbol(at, '/') || symbol(at, '*') || word(at, "per")) {
                const std::string operand = atom(at + 1);
                if (operand.empty()) {
                    *broken = true;
                    *next = std::min(at + 2, tokens_.size());
                    return false;
                }
                built += (symbol(at, '*') ? "*" : "/") + operand;
                at += 2;
            } else if (symbol(at, '^')) {
                const bool negative = symbol(at + 1, '-');
                const size_t digits = at + (negative ? 2 : 1);
                if (!numeral(digits)) {
                    *broken = true;
                    *next = std::min(digits + 1, tokens_.size());
                    return false;
                }
                built += "^" + std::string(negative ? "-" : "") + std::string(text(digits));
                at = digits + 1;
            } else if (spaced_factor(at)) {
                built += " " + std::string(text(at));
                ++at;
            } else if (word(at, "squared") || word(at, "cubed")) {
                built += word(at, "squared") ? "^2" : "^3";
                ++at;
            } else {
                break;
            }
        }
        Unit parsed;
        std::string error;
        if (!parse_unit(built, &parsed, &error)) {
            *broken = true;
            *next = at;
            return false;
        }
        *spelling = built;
        *next = at;
        return true;
    }

    bool number(size_t k, std::string_view digits, bool from_word, size_t *next) {
        size_t at = k + 1;
        if (!from_word) {
            const bool times_ten = (symbol(at, '*') || word(at, "x")) && numeral(at + 1) && text(at + 1) == "10" &&
                                   symbol(at + 2, '^');
            const bool exponent = adjacent(k) && (word(at, "e") || word(at, "E")) && adjacent(at) && numeral(at + 1);
            if (times_ten || exponent) {
                const size_t last = times_ten ? std::min(at + 4, tokens_.size()) : at + 2;
                refuse(LexFault::ScientificNotation, "scientific notation is not read yet", k, last);
                *next = last;
                return true;
            }
            if (symbol(at, '%')) {
                refuse(LexFault::Percentage, "a percentage is not read yet", k, at + 1);
                *next = at + 1;
                return true;
            }
            const bool tolerance = symbol(at, '+') && symbol(at + 1, '/') && symbol(at + 2, '-') && numeral(at + 3);
            // Written as 3-5 only, since "from 0 to 20 m/s" is a change of state rather than a range.
            const bool range = symbol(at, '-') && adjacent(k) && adjacent(at) && numeral(at + 1);
            if (tolerance || range) {
                size_t last = at + (tolerance ? 4 : 2);
                std::string ignored;
                bool broken = false;
                size_t after = last;
                if (unit(last, &ignored, &after, &broken))
                    last = after;
                if (tolerance)
                    refuse(LexFault::Tolerance, "a tolerance is not read yet", k, last);
                else
                    refuse(LexFault::Range, "a range of values is not read yet", k, last);
                *next = last;
                return true;
            }
        }
        std::string value(digits);
        std::string denominator;
        if (!from_word && symbol(at, '/') && adjacent(k) && adjacent(at) && numeral(at + 1) &&
            text(at + 1).find_first_not_of("0123456789") == std::string_view::npos) {
            denominator = std::string(text(at + 1));
            at += 2;
        }
        std::string spelling;
        bool broken = false;
        size_t after = at;
        if (unit(at, &spelling, &after, &broken)) {
            at = after;
        } else if (broken) {
            refuse(LexFault::UnknownUnit, "a unit this table does not know", k, after);
            *next = after;
            return true;
        }
        GroundedQuantity q;
        std::string error;
        if (!parse_quantity(value + (spelling.empty() ? "" : " " + spelling), &q.quantity, &error)) {
            refuse(LexFault::UnknownUnit, error, k, at);
            *next = at;
            return true;
        }
        if (!denominator.empty()) {
            Rational below;
            if (!rational_from_text(denominator, &below) || below.num == 0 ||
                !rational_div(q.quantity.value, below, &q.quantity.value)) {
                refuse(LexFault::UnknownUnit, "a fraction this cannot read exactly", k, at);
                *next = at;
                return true;
            }
            value += "/" + denominator;
        }
        q.value_text = value;
        q.unit_text = spelling;
        q.span = span_from_normalized(out_->source, tokens_[k].begin, tokens_[at - 1].end);
        out_->quantities.push_back(std::move(q));
        *next = at;
        if (out_->quantities.size() > limits_.max_quantities)
            return stop(LexStatus::ResourceExhausted,
                        "more than " + std::to_string(limits_.max_quantities) + " quantities");
        return true;
    }

    bool phrase(size_t *k) {
        for (const Phrase &p : kPhrases) {
            std::string_view rest(p.words);
            size_t at = *k;
            bool matched = true;
            while (!rest.empty()) {
                const size_t space = rest.find(' ');
                const std::string_view w = rest.substr(0, space);
                if (!word(at, w)) {
                    matched = false;
                    break;
                }
                ++at;
                rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
            }
            if (matched) {
                out_->concepts.push_back(
                    {p.concept_id, span_from_normalized(out_->source, tokens_[*k].begin, tokens_[at - 1].end)});
                *k = at;
                return true;
            }
        }
        return false;
    }

    const LexicalLimits &limits_;
    LexicalReading *out_;
    const std::string &text_;
    std::vector<Token> tokens_;
};

std::string at_text(const Span &span) {
    return std::to_string(span.original_begin) + ".." + std::to_string(span.original_end);
}

}  // namespace

SourceDocument normalize_source(const std::string &source_id, const std::string &original_utf8) {
    SourceDocument doc;
    doc.source_id = source_id;
    doc.original_utf8 = original_utf8;
    doc.language = "en";
    doc.source_kind = "typed";
    doc.original_content_hash = source_hash(original_utf8);
    doc.normalization_profile_version = kNormalizationProfile;
    std::string &out = doc.normalized_utf8;
    auto &map = doc.normalized_to_original_span_map;
    size_t i = 0;
    while (i < original_utf8.size()) {
        const std::string_view rest = std::string_view(original_utf8).substr(i);
        std::string_view replaced;
        size_t length = 1;
        bool found = false;
        for (const Replacement &r : kReplacements) {
            if (rest.substr(0, r.utf8.size()) == r.utf8) {
                replaced = r.ascii;
                length = r.utf8.size();
                found = true;
                break;
            }
        }
        if (!found) {
            length = std::min(utf8_length(static_cast<unsigned char>(original_utf8[i])), rest.size());
            replaced = is_space(original_utf8[i]) ? std::string_view(" ") : rest.substr(0, length);
        }
        if (replaced == " ") {
            if (!out.empty() && out.back() == ' ')
                map.back().second = i + length;
            else if (!out.empty()) {
                out += ' ';
                map.emplace_back(i, i + length);
            }
        } else {
            for (char c : replaced) {
                out += c;
                map.emplace_back(i, i + length);
            }
        }
        i += length;
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
        map.pop_back();
    }
    return doc;
}

Span span_from_normalized(const SourceDocument &source, size_t normalized_begin, size_t normalized_end) {
    Span span;
    const auto &map = source.normalized_to_original_span_map;
    if (normalized_begin >= normalized_end || normalized_end > map.size())
        return span;
    span.normalized_begin = normalized_begin;
    span.normalized_end = normalized_end;
    span.original_begin = map[normalized_begin].first;
    span.original_end = map[normalized_end - 1].second;
    span.surface = source.original_utf8.substr(span.original_begin, span.original_end - span.original_begin);
    return span;
}

const char *lex_fault_name(LexFault fault) {
    switch (fault) {
        case LexFault::UnsupportedCharacter: return "unsupported character";
        case LexFault::UnknownUnit: return "unknown unit";
        case LexFault::Range: return "range";
        case LexFault::Tolerance: return "tolerance";
        case LexFault::Percentage: return "percentage";
        case LexFault::ScientificNotation: return "scientific notation";
    }
    return "unknown";
}

const char *lex_status_name(LexStatus status) {
    switch (status) {
        case LexStatus::Grounded: return "grounded";
        case LexStatus::Unsupported: return "unsupported";
        case LexStatus::ResourceExhausted: return "resource exhausted";
        case LexStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

LexicalReading read_lexical(const std::string &source_id, const std::string &original_utf8,
                            const LexicalLimits &limits) {
    LexicalReading reading;
    if (original_utf8.size() > limits.max_bytes) {
        reading.status = LexStatus::ResourceExhausted;
        reading.detail = "more than " + std::to_string(limits.max_bytes) + " bytes";
        return reading;
    }
    reading.source = normalize_source(source_id, original_utf8);
    Reader reader(limits, &reading);
    if (!reader.tokenize() || !reader.scan()) {
        // A reading that stopped part way proposes nothing, so nothing half read can be confirmed.
        reading.quantities.clear();
        reading.concepts.clear();
        reading.failures.clear();
        return reading;
    }
    reading.status = reading.failures.empty() ? LexStatus::Grounded : LexStatus::Unsupported;
    return reading;
}

InterpretationSet lexical_interpretation(const LexicalReading &reading) {
    InterpretationSet set;
    set.interpretation_id = reading.source.source_id + "#lexical";
    set.source_document = reading.source;
    set.parser_build_id = application_version();
    set.lexicon_module_versions = {kLexiconVersion};
    set.resource_usage = "bytes=" + std::to_string(reading.source.original_utf8.size()) +
                         " tokens=" + std::to_string(reading.tokens);
    set.completion_status = lex_status_name(reading.status);
    if (!reading.detail.empty())
        set.global_diagnostics.push_back(reading.detail);
    if (reading.status == LexStatus::ResourceExhausted || reading.status == LexStatus::Cancelled)
        return set;
    InterpretationCandidate candidate;
    candidate.candidate_id = "lexical-1";
    for (const GroundedQuantity &q : reading.quantities)
        candidate.grounded_items.push_back("quantity " + q.value_text + (q.unit_text.empty() ? "" : " " + q.unit_text) +
                                           " at " + at_text(q.span));
    for (const LexiconConcept &c : reading.concepts)
        candidate.grounded_items.push_back("concept " + c.concept_id + " at " + at_text(c.span));
    for (const LexFailure &f : reading.failures) {
        candidate.unsupported_spans.push_back(f.span);
        candidate.validation_results.push_back(std::string(lex_fault_name(f.fault)) + " at " + at_text(f.span));
    }
    set.candidates.push_back(std::move(candidate));
    return set;
}

std::string lexical_summary(const LexicalReading &reading) {
    std::string out;
    for (const GroundedQuantity &q : reading.quantities)
        out += "quantity " + q.value_text + (q.unit_text.empty() ? "" : " " + q.unit_text) + " (" +
               dimension_text(q.quantity.unit.dimension) + ") from \"" + q.span.surface + "\" at " + at_text(q.span) + "\n";
    for (const LexiconConcept &c : reading.concepts)
        out += "concept " + c.concept_id + " from \"" + c.span.surface + "\" at " + at_text(c.span) + "\n";
    for (const LexFailure &f : reading.failures)
        out += std::string("refused ") + lex_fault_name(f.fault) + " from \"" + f.span.surface + "\" at " +
               at_text(f.span) + ": " + f.detail + "\n";
    out += std::string("status ") + lex_status_name(reading.status) + (reading.detail.empty() ? "" : ": " + reading.detail) + "\n";
    return out;
}

}  // namespace nps::wp
