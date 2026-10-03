#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "golden/golden.h"
#include "nps/wp/grammar.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

std::string slurp(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

const char *kCart = "A red cart moving at 5 m/s speeds up at 3 m/s^2 for 4 s. How fast is it moving then?";
const char *kTrain = "A train travels at 60 km/h for 3 h. How far does it go?";
const char *kAmbiguous = "A cart goes 5 m/s and then 9 m/s after 2 s. How far does it go?";

std::string record_of(const KinematicsProblem &problem) {
    Arena arena;
    Derivation derivation;
    const KinematicsResult result = solve_kinematics(arena, derivation, problem);
    return result.value_text + " " + result.unit_text + "\n" + render_derivation(arena, derivation);
}

std::string direct_record(const char *text) {
    KinematicsProblem problem;
    std::string why;
    if (!parse_kinematics(text, &problem, &why))
        return "unparsed: " + why;
    return record_of(problem);
}

std::string word_record(const wp::ConfirmResult &confirmed) {
    if (!confirmed.committed)
        return "not committed: " + confirmed.detail;
    KinematicsProblem problem;
    std::string why;
    if (!wp::to_kinematics(*confirmed.committed, &problem, &why))
        return "not converted: " + why;
    return record_of(problem);
}

const wp::Quantity *by_type(const wp::ProblemIR &ir, const std::string &type) {
    for (const wp::Quantity &q : ir.quantities) {
        if (q.semantic_type == type)
            return &q;
    }
    return nullptr;
}

std::set<std::string> types_of(const wp::ProblemIR &ir, const std::vector<std::string> &ids) {
    std::set<std::string> out;
    for (const std::string &id : ids) {
        for (const wp::Quantity &q : ir.quantities) {
            if (q.id == id)
                out.insert(q.semantic_type);
        }
    }
    return out;
}

void test_corpus(TestSink &t) {
    std::istringstream corpus(slurp("tests/wp_corpus/grammar.corpus"));
    std::string line, name, text, expected;
    std::set<std::string> kinds;
    size_t cases = 0;
    while (std::getline(corpus, line)) {
        if (line.rfind("case ", 0) == 0) {
            name = line.substr(5);
            expected.clear();
        } else if (line.rfind("kind ", 0) == 0) {
            kinds.insert(line.substr(5));
        } else if (line.rfind("text ", 0) == 0) {
            text = line.substr(5);
        } else if (line.rfind("expect ", 0) == 0) {
            expected += line.substr(7) + "\n";
        } else if (line == "end") {
            t.equal(wp::grammar_summary(wp::interpret_motion("corpus-" + name, text)), expected,
                    "the grammar corpus case " + name + " reads as annotated");
            ++cases;
        }
    }
    t.check(cases == 15 && kinds.size() == 4,
            "the grammar corpus holds fifteen cases with positive, negative, boundary and ambiguous examples: " +
                std::to_string(cases));
}

void test_authored_equivalence(TestSink &t) {
    const wp::GrammarResult cart = wp::interpret_motion("src-cart", kCart);
    const wp::ConfirmResult confirmed = wp::confirm_motion(cart, kCart, {}, "tester");
    t.equal(wp::confirm_status_name(confirmed.status), "confirmed", "the cart text confirms: " + confirmed.detail);
    t.check(word_record(confirmed) == direct_record("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s") &&
                word_record(confirmed).compare(0, 7, "17 m/s\n") == 0,
            "the word problem reproduces the direct entry's derivation record exactly");
    const wp::IrReadResult authored = wp::read_problem_ir(slurp("tests/wp_corpus/cart_speed.ir"));
    if (!confirmed.committed || authored.status != wp::IrReadStatus::Ok) {
        t.check(false, "the authored cart problem and the word problem are both available to compare");
        return;
    }
    const wp::ProblemIR &word = confirmed.committed->ir();
    size_t matched = 0;
    for (const wp::Quantity &a : authored.ir.quantities) {
        const wp::Quantity *w = by_type(word, a.semantic_type);
        const wp::Span &as = a.provenance.supporting_source_spans.front();
        if (w && w->value_expression == a.value_expression && w->unit == a.unit &&
            w->provenance.supporting_source_spans.size() == 1 &&
            w->provenance.supporting_source_spans.front().original_begin == as.original_begin &&
            w->provenance.supporting_source_spans.front().original_end == as.original_end)
            ++matched;
    }
    t.check(matched == 4 && word.quantities.size() == 4,
            "every authored quantity is proposed with the same type, value, unit and span");
    t.check(types_of(word, word.knowns) == types_of(authored.ir, authored.ir.knowns) &&
                types_of(word, word.unknowns) == types_of(authored.ir, authored.ir.unknowns),
            "with the same knowns and the same unknown");
    t.check(word.confirmed_inferred_assumptions.size() == authored.ir.confirmed_inferred_assumptions.size() &&
                word.explicit_assumptions.size() == authored.ir.explicit_assumptions.size(),
            "and the constant acceleration inferred and confirmed as the authored problem has it");
    t.check(word.unused_information.size() == 1 && authored.ir.unused_information.size() == 1 &&
                word.unused_information.front().original_begin == authored.ir.unused_information.front().original_begin,
            "and the colour kept as the same unused span");
}

void test_slowing(TestSink &t) {
    const char *text = "A bus moving at 20 m/s slows down at 4 m/s^2 for 3 s. How fast is it moving then?";
    const wp::ConfirmResult confirmed = wp::confirm_motion(wp::interpret_motion("src-bus", text), text, {}, "tester");
    t.check(word_record(confirmed) == direct_record("find v; v0 = 20 m/s; a = -4 m/s^2; t = 3 s") &&
                word_record(confirmed).compare(0, 6, "8 m/s\n") == 0,
            "slowing down commits an acceleration against the motion, matching the signed direct entry");
}

void test_uniform_motion(TestSink &t) {
    const wp::GrammarResult train = wp::interpret_motion("src-train", kTrain);
    t.check(train.draft.has_value() && !by_type(*train.draft, "acceleration")->provenance.explicit_fact,
            "rate and distance proposes a zero acceleration as an inference");
    wp::IrValidation why;
    t.check(train.draft && !wp::commit(*train.draft, train.lexical.source, &why) &&
                why.fault == wp::IrFault::UnconfirmedInference,
            "which cannot be committed before the user confirms it");
    const wp::ConfirmResult confirmed = wp::confirm_motion(train, kTrain, {}, "tester");
    t.check(confirmed.committed.has_value() &&
                confirmed.committed->ir().confirmation_record.material_assumption_ids == std::vector<std::string>{"q-a"},
            "confirmation lists the inferred acceleration among what was approved");
    // Entered in the order the text states it, with the inferred acceleration after what was written.
    t.check(word_record(confirmed) == direct_record("find x; v0 = 60 km/h; t = 3 h; a = 0 m/s^2") &&
                word_record(confirmed).compare(0, 9, "180000 m\n") == 0,
            "and the confirmed problem reproduces the direct entry with a zero acceleration");
}

void test_clarification(TestSink &t) {
    const wp::GrammarResult r = wp::interpret_motion("src-ambiguous", kAmbiguous);
    t.check(r.outcome == wp::GrammarOutcome::NeedsClarification && !r.draft && r.clarifications.size() == 2 &&
                r.set.candidates.size() == 1 && r.set.candidates[0].required_clarifications.size() == 2,
            "two uncued velocities are asked about rather than picked");
    t.equal(wp::confirm_status_name(wp::confirm_motion(r, kAmbiguous, {}, "tester").status), "needs clarification",
            "and confirming without answers is refused");
    t.equal(wp::confirm_status_name(wp::confirm_motion(r, kAmbiguous, {{"c1", "acceleration"}}, "tester").status),
            "invalid answer", "an answer the question did not offer is refused");
    t.equal(wp::confirm_status_name(wp::confirm_motion(r, kAmbiguous, {{"c9", "unused"}}, "tester").status),
            "invalid answer", "and so is an answer to a question that was not asked");
    const wp::ConfirmResult both =
        wp::confirm_motion(r, kAmbiguous, {{"c1", "initial_velocity"}, {"c2", "initial_velocity"}}, "tester");
    t.check(both.status == wp::ConfirmStatus::Rejected && both.detail.find("two values") != std::string::npos,
            "answers that give one role twice are rejected rather than committed");
    const wp::ConfirmResult answered =
        wp::confirm_motion(r, kAmbiguous, {{"c1", "initial_velocity"}, {"c2", "final_velocity"}}, "tester");
    t.check(answered.status == wp::ConfirmStatus::Confirmed &&
                word_record(answered) == direct_record("find x; v0 = 5 m/s; v = 9 m/s; t = 2 s") &&
                word_record(answered).compare(0, 5, "14 m\n") == 0,
            "answered, the problem commits and matches the direct entry");
    const wp::ConfirmResult dropped = wp::confirm_motion(r, kAmbiguous, {{"c1", "initial_velocity"}, {"c2", "unused"}}, "tester");
    t.check(dropped.committed && dropped.committed->ir().unused_information.size() == 1 &&
                dropped.committed->ir().unused_information.front().surface == "9 m/s" &&
                !by_type(dropped.committed->ir(), "acceleration")->provenance.explicit_fact,
            "and a value answered as unused is kept as unused information");
}

void test_confirmation_gates(TestSink &t) {
    const wp::GrammarResult cart = wp::interpret_motion("src-cart", kCart);
    std::string edited = kCart;
    edited.replace(edited.find("5 m/s"), 1, "6");
    const wp::ConfirmResult changed = wp::confirm_motion(cart, edited, {}, "tester");
    t.check(changed.status == wp::ConfirmStatus::SourceChanged && !changed.committed,
            "text edited after reading cannot be confirmed against the old reading");
    wp::GrammarResult broken = cart;
    broken.lexical.quantities[0].unit_text = "s";
    const wp::ConfirmResult hard = wp::confirm_motion(broken, kCart, {}, "tester");
    t.check(hard.status == wp::ConfirmStatus::Rejected && hard.validation.fault == wp::IrFault::DimensionMismatch &&
                !hard.committed,
            "confirmation never turns a dimension failure into a committed problem");
    const wp::ConfirmResult good = wp::confirm_motion(cart, kCart, {}, "tester");
    t.check(good.committed.has_value(), "while the same reading without the fault commits");
    if (good.committed) {
        const wp::ConfirmationRecord &record = good.committed->ir().confirmation_record;
        t.check(record.confirmed && record.confirmed_by == "tester" &&
                    record.source_content_hash == wp::source_hash(kCart) && record.selected_candidate_id == "grammar-1" &&
                    record.problem_revision == 1 &&
                    record.parser_versions == std::string(wp::kGrammarVersion) + "+" + wp::kLexiconVersion &&
                    record.material_assumption_ids == std::vector<std::string>{"a-constant"},
                "the confirmation binds the source hash, candidate, revision, versions and inferred assumption");
    }
    const wp::GrammarResult refused = wp::interpret_motion("src-no", "A car moving at 5 s speeds up. How fast is it?");
    t.check(wp::confirm_motion(refused, "A car moving at 5 s speeds up. How fast is it?", {}, "tester").status ==
                    wp::ConfirmStatus::Rejected &&
                refused.set.candidates.size() == 1 && refused.set.candidates[0].contradictions.size() == 1,
            "an unsupported reading has nothing to confirm and records its contradiction");
}

bool stop_now(void *) { return true; }

void test_outcomes(TestSink &t) {
    wp::LexicalLimits few;
    few.max_quantities = 1;
    const wp::GrammarResult exhausted = wp::interpret_motion("src", kCart, few);
    t.check(exhausted.outcome == wp::GrammarOutcome::ResourceExhausted && exhausted.set.candidates.empty() &&
                !exhausted.draft,
            "running out of room while reading is resource exhaustion with no candidate");
    wp::LexicalLimits stop;
    stop.poll = stop_now;
    const wp::GrammarResult cancelled = wp::interpret_motion("src", kCart, stop);
    t.check(cancelled.outcome == wp::GrammarOutcome::Cancelled && cancelled.set.candidates.empty(),
            "and a cancelled reading is cancellation, kept apart from both");
    const wp::GrammarResult cart = wp::interpret_motion("src", kCart);
    t.check(cart.outcome == wp::GrammarOutcome::Interpreted && cart.set.candidates.size() == 1 &&
                cart.set.candidates[0].proposed_problem_model == wp::kMotionFamily &&
                cart.set.grammar_module_versions.size() == 1 && cart.set.completion_status == "interpreted",
            "an interpreted reading proposes the motion family and carries the grammar version");
    t.check(cart.draft && !cart.draft->confirmation_record.confirmed && cart.draft->requested_method.empty(),
            "its draft is unconfirmed and names no method the text did not ask for");
}

}  // namespace

void run_grammar_tests(TestSink &sink) {
    test_corpus(sink);
    test_authored_equivalence(sink);
    test_slowing(sink);
    test_uniform_motion(sink);
    test_clarification(sink);
    test_confirmation_gates(sink);
    test_outcomes(sink);
}

}  // namespace nps
