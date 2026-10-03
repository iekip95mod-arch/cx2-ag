#include <fstream>
#include <sstream>
#include <string>

#include "golden/golden.h"
#include "nps/wp/lexical.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

std::string slurp(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

wp::LexicalReading read(const std::string &text, const wp::LexicalLimits &limits = wp::LexicalLimits()) {
    return wp::read_lexical("src-test", text, limits);
}

void test_normalization(TestSink &t) {
    const std::string original = "a  =\t3 m/s\xC2\xB2 \xC3\x97 2";
    const wp::SourceDocument doc = wp::normalize_source("src-n", original);
    t.equal(doc.normalized_utf8, "a = 3 m/s^2 * 2", "whitespace runs collapse and typographic glyphs become ASCII");
    t.check(doc.normalized_to_original_span_map.size() == doc.normalized_utf8.size(),
            "every normalized byte has an original range");
    t.check(doc.original_content_hash == wp::source_hash(original) && doc.original_utf8 == original,
            "and the original text and its hash are kept unchanged");
    const size_t superscript = doc.normalized_utf8.find("^2");
    const wp::Span glyph = wp::span_from_normalized(doc, superscript, superscript + 2);
    t.equal(glyph.surface, "\xC2\xB2", "the two bytes of ^2 map back to the one superscript glyph");
    const wp::Span gap = wp::span_from_normalized(doc, 1, 2);
    t.equal(gap.surface, "  ", "a collapsed space maps back to the whole run it replaced");
    const wp::Span whole = wp::span_from_normalized(doc, 0, doc.normalized_utf8.size());
    t.check(whole.original_begin == 0 && whole.original_end == original.size() && wp::span_matches(whole, doc),
            "and the whole normalized text maps back to the whole original");
    t.equal(wp::normalize_source("s", "  x  ").normalized_utf8, "x", "leading and trailing space is trimmed");
    t.equal(wp::span_from_normalized(doc, 3, 3).surface, "", "an empty range maps to no text");
}

void test_quantities(TestSink &t) {
    const wp::LexicalReading r = read("A 2 kg block and 250 cm^3 of water.");
    t.equal(wp::lex_status_name(r.status), "grounded", "plain quantities read as grounded");
    t.check(r.quantities.size() == 2 && r.quantities[0].unit_text == "kg" && r.quantities[1].unit_text == "cm^3" &&
                r.quantities[1].quantity.unit.dimension.length == 3,
            "each number keeps the unit written after it, parsed by the unit table");
    t.check(r.quantities.size() == 2 && wp::span_matches(r.quantities[0].span, r.source) &&
                r.quantities[0].span.surface == "2 kg",
            "and a span that covers exactly its original text");
    const wp::LexicalReading bare = read("It has 3 wheels.");
    t.check(bare.quantities.size() == 1 && bare.quantities[0].unit_text.empty() &&
                bare.quantities[0].span.surface == "3",
            "a number followed by an ordinary word is a pure number, not a unit");
    const wp::LexicalReading capital = read("A cart. A 5 A current.");
    t.check(capital.quantities.size() == 1 && capital.quantities[0].unit_text == "A",
            "a unit symbol is read only after a number, so a sentence-initial A is not an ampere");
    const wp::LexicalReading dotted = read("It rose 4 m. Then 2.5 s passed.");
    t.check(dotted.quantities.size() == 2 && dotted.quantities[0].span.surface == "4 m" &&
                dotted.quantities[1].quantity.precision.kind == NumberKind::Measured,
            "a full stop ends a quantity and a written decimal stays measured");
}

void test_words(TestSink &t) {
    const wp::LexicalReading r = read("It covers 120 kilometres in 2 hours at 1.5 meters per second squared.");
    t.check(r.quantities.size() == 3 && r.quantities[0].unit_text == "km" && r.quantities[1].unit_text == "h" &&
                r.quantities[2].unit_text == "m/s^2",
            "written unit names, per and squared become the unit table's own spelling");
    t.check(r.quantities.size() == 3 && r.quantities[2].quantity.unit.dimension.time == -2,
            "so the written acceleration has the dimension of one");
    const wp::LexicalReading cubed = read("It holds 2 metres cubed.");
    t.check(cubed.quantities.size() == 1 && cubed.quantities[0].unit_text == "m^3", "and cubed raises to three");
    const wp::LexicalReading counted = read("It takes three seconds.");
    t.check(counted.quantities.size() == 1 && counted.quantities[0].value_text == "3" &&
                counted.quantities[0].quantity.precision.kind == NumberKind::Exact,
            "a number word before a unit is an exact quantity");
    const wp::LexicalReading pronoun = read("One of the two carts is red.");
    t.check(pronoun.quantities.empty() && pronoun.status == wp::LexStatus::Grounded,
            "a number word with no unit after it is left to the grammar rather than guessed at");
    const wp::LexicalReading fraction = read("The rod is 3/4 m long.");
    Rational three_quarters;
    rational_from_text("0.75", &three_quarters);
    t.check(fraction.quantities.size() == 1 && fraction.quantities[0].value_text == "3/4" &&
                rational_equal(fraction.quantities[0].quantity.value, three_quarters),
            "a written fraction is read exactly");
    const wp::LexicalReading negative = read("Its acceleration is -2 m/s^2 and x-3 is not.");
    t.check(negative.quantities.size() == 2 && negative.quantities[0].value_text == "-2" &&
                negative.quantities[1].value_text == "3",
            "a minus sign after a space belongs to the number and one inside a word does not");
    const wp::LexicalReading exponent = read("It is 5 m s^-2 or 5 m*s^-2.");
    t.check(exponent.quantities.size() == 2 && exponent.quantities[0].unit_text == "m s^-2" &&
                exponent.quantities[0].quantity.unit.dimension.time == -2 &&
                exponent.quantities[1].unit_text == "m*s^-2",
            "a negative power is read after a spaced or a written product, leaving no stray number behind");
    const wp::LexicalReading spaced = read("It rose 5 m in 2 s.");
    t.check(spaced.quantities.size() == 2 && spaced.quantities[0].unit_text == "m",
            "while a spaced word with no power or division after it stays outside the unit");
    const wp::LexicalReading symbol_word = read("It rose 5 m A crowd cheered.");
    t.check(symbol_word.quantities.size() == 1 && symbol_word.quantities[0].unit_text == "m",
            "even when that word is itself a unit symbol");
}

void test_concepts(TestSink &t) {
    const wp::LexicalReading r = read("The ball Starts From Rest and has constant acceleration, at most 3 s.");
    t.check(r.concepts.size() == 3 && r.concepts[0].concept_id == "initial_speed_equals_zero" &&
                r.concepts[0].span.surface == "Starts From Rest",
            "a lexicon phrase is matched without regard to case and keeps its original wording");
    t.check(r.concepts.size() == 3 && r.concepts[1].concept_id == "constant_acceleration" &&
                r.concepts[2].concept_id == "less_than_or_equal",
            "and each phrase maps to its concept");
    const wp::LexicalReading steady = read("It moves at a steady speed of 4 m/s.");
    t.check(steady.concepts.size() == 1 && steady.concepts[0].concept_id == "zero_acceleration" &&
                steady.concepts[0].span.surface == "steady speed",
            "a steady speed is read as zero acceleration");
    const wp::LexicalReading partial = read("It is at rest.");
    t.check(partial.concepts.empty(), "a phrase that is only partly present maps to nothing");
    const wp::LexicalReading longest = read("It moves no more than 3 m.");
    t.check(longest.concepts.size() == 1 && longest.concepts[0].span.surface == "no more than",
            "a three-word phrase is matched whole");
}

std::string refusal(const std::string &text) {
    const wp::LexicalReading r = read(text);
    if (r.failures.size() != 1)
        return "failures " + std::to_string(r.failures.size());
    return std::string(wp::lex_fault_name(r.failures[0].fault)) + " " + r.failures[0].span.surface + " " +
           wp::lex_status_name(r.status);
}

void test_refusals(TestSink &t) {
    t.equal(refusal("It costs \xE2\x82\xAC" "4."), "unsupported character \xE2\x82\xAC unsupported",
            "a character outside the supported text is refused by span");
    t.equal(refusal("It moves 5 furlongs/s."), "unknown unit 5 furlongs/s unsupported",
            "a unit written with an operator that the table does not know is refused");
    t.equal(refusal("It moves 2 m/fortnight."), "unknown unit 2 m/fortnight unsupported",
            "and so is a known unit divided by an unknown one, rather than read as metres");
    t.equal(refusal("It moves 2 m^x."), "unknown unit 2 m^x unsupported", "and a power that is not a number");
    t.equal(refusal("It climbs 3-5 m."), "range 3-5 m unsupported", "a range is refused rather than read as 3");
    t.equal(refusal("It weighs 5 +/- 0.1 kg."), "tolerance 5 +/- 0.1 kg unsupported",
            "a tolerance is refused as a whole");
    t.equal(refusal("It is \xC2\xB1 2 m."), "tolerance \xC2\xB1 unsupported", "and so is a tolerance sign on its own");
    t.equal(refusal("A 20% grade."), "percentage 20% unsupported", "a percentage is refused with its number");
    t.equal(refusal("A 20 % grade."), "percentage 20 % unsupported", "even when spaced from it");
    t.equal(refusal("Light goes 3 x 10^8 m/s."), "scientific notation 3 x 10^8 unsupported",
            "scientific notation is refused rather than read as 3");
    t.equal(refusal("Light goes 3e8 m/s."), "scientific notation 3e8 unsupported", "in either spelling");
    const wp::LexicalReading r = read("It moves 5 m/s and 3-5 m.");
    t.check(r.quantities.size() == 1 && r.failures.size() == 1 && r.status == wp::LexStatus::Unsupported,
            "a refusal leaves the grounded quantities beside it and marks the reading unsupported");
}

bool stop_now(void *) { return true; }

void test_limits(TestSink &t) {
    wp::LexicalLimits bytes;
    bytes.max_bytes = 8;
    const wp::LexicalReading long_text = read("It moves 5 m/s.", bytes);
    t.check(long_text.status == wp::LexStatus::ResourceExhausted && long_text.quantities.empty(),
            "text past the byte limit is refused as resource exhaustion before reading");
    wp::LexicalLimits tokens;
    tokens.max_tokens = 3;
    const wp::LexicalReading many = read("It moves 5 m/s.", tokens);
    t.check(many.status == wp::LexStatus::ResourceExhausted && many.quantities.empty() &&
                many.detail.find("tokens") != std::string::npos,
            "more tokens than the limit is resource exhaustion with nothing kept");
    wp::LexicalLimits count;
    count.max_quantities = 1;
    const wp::LexicalReading two = read("It moves 5 m in 2 s.", count);
    t.check(two.status == wp::LexStatus::ResourceExhausted && two.quantities.empty() &&
                two.detail.find("quantities") != std::string::npos,
            "and so is more quantities than the limit");
    t.check(read("It moves 5 m.", count).status == wp::LexStatus::Grounded, "while one quantity is within it");
    wp::LexicalLimits cancelled;
    cancelled.poll = stop_now;
    const wp::LexicalReading stopped = read("It moves 5 m/s and 3-5 m.", cancelled);
    t.check(stopped.status == wp::LexStatus::Cancelled && stopped.quantities.empty() && stopped.failures.empty(),
            "a cancelled reading keeps nothing, not even its refusals");
    t.check(read("It moves 5 m/s.").status == wp::LexStatus::Grounded, "and the same text reads when nothing stops it");
    const wp::LexicalReading early = read("a b c d e f g h i j k l m n o p q r s t u v w x y z.", cancelled);
    t.check(early.status == wp::LexStatus::Cancelled && early.tokens == 0,
            "a long text is cancelled while it is still being split into tokens");
}

void test_interpretation(TestSink &t) {
    const wp::LexicalReading r = read("It starts from rest and moves 3-5 m in 4 s.");
    const wp::InterpretationSet set = wp::lexical_interpretation(r);
    t.check(set.candidates.size() == 1 && set.candidates[0].proposed_problem_model.empty(),
            "the reading becomes one candidate that proposes no problem model");
    t.check(set.candidates.size() == 1 && set.candidates[0].grounded_items.size() == 2 &&
                set.candidates[0].grounded_items[0] == "quantity 4 s at 39..42" &&
                set.candidates[0].grounded_items[1] == "concept initial_speed_equals_zero at 3..19",
            "with its grounded quantities and concepts at their original offsets");
    t.check(set.candidates.size() == 1 && set.candidates[0].unsupported_spans.size() == 1 &&
                set.candidates[0].unsupported_spans[0].surface == "3-5 m" &&
                set.candidates[0].validation_results[0] == "range at 30..35",
            "and its refusals as unsupported spans");
    t.check(set.lexicon_module_versions.size() == 1 && set.lexicon_module_versions[0] == wp::kLexiconVersion &&
                set.completion_status == "unsupported" && set.source_document.original_utf8 == r.source.original_utf8,
            "carrying the lexicon version, the completion status and the source it read");
    wp::LexicalLimits count;
    count.max_quantities = 0;
    const wp::InterpretationSet exhausted = wp::lexical_interpretation(read("It moves 5 m.", count));
    t.check(exhausted.candidates.empty() && exhausted.completion_status == "resource exhausted" &&
                exhausted.global_diagnostics.size() == 1,
            "a reading that ran out of room offers no candidate at all");
}

void test_corpus(TestSink &t) {
    std::istringstream corpus(slurp("tests/wp_corpus/lexical.corpus"));
    std::string line, name, text, expected;
    size_t cases = 0;
    while (std::getline(corpus, line)) {
        if (line.rfind("case ", 0) == 0) {
            name = line.substr(5);
            text.clear();
            expected.clear();
        } else if (line.rfind("text ", 0) == 0) {
            text = line.substr(5);
        } else if (line.rfind("expect ", 0) == 0) {
            expected += line.substr(7) + "\n";
        } else if (line == "end") {
            const wp::LexicalReading r = wp::read_lexical("corpus-" + name, text);
            t.equal(wp::lexical_summary(r), expected, "the lexical corpus case " + name + " reads as annotated");
            for (const wp::GroundedQuantity &q : r.quantities)
                t.check(wp::span_matches(q.span, r.source), "every grounded quantity in " + name + " has a true span");
            ++cases;
        }
    }
    t.check(cases == 8, "the lexical corpus holds its eight annotated cases: " + std::to_string(cases));
}

// The Milestone 0 spans were written by hand, so the reader has to find the same bytes on its own.
void test_authored_agreement(TestSink &t) {
    const wp::IrReadResult cart = wp::read_problem_ir(slurp("tests/wp_corpus/cart_speed.ir"));
    const wp::LexicalReading r = wp::read_lexical(cart.source.source_id, cart.source.original_utf8);
    size_t agreed = 0;
    for (const wp::Quantity &authored : cart.ir.quantities) {
        if (authored.value_expression.empty())
            continue;
        for (const wp::GroundedQuantity &found : r.quantities) {
            const wp::Span &a = authored.provenance.supporting_source_spans.front();
            if (found.span.original_begin == a.original_begin && found.span.original_end == a.original_end &&
                found.unit_text == authored.unit && found.value_text == authored.value_expression)
                ++agreed;
        }
    }
    t.check(agreed == 3 && r.quantities.size() == 3,
            "the reader grounds the three authored cart quantities at the authored spans with the authored units");
}

}  // namespace

void run_lexical_tests(TestSink &sink) {
    test_normalization(sink);
    test_quantities(sink);
    test_words(sink);
    test_concepts(sink);
    test_refusals(sink);
    test_limits(sink);
    test_interpretation(sink);
    test_corpus(sink);
    test_authored_agreement(sink);
}

}  // namespace nps
