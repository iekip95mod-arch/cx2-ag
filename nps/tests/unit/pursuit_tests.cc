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

const char *kLater = "A car leaves a town at 20 m/s. A truck leaves the same town 10 s later at 30 m/s. "
                     "When does the truck catch up with the car?";
const char *kPronoun = "A car leaves a town at 20 m/s. A truck leaves the same town 10 s later. It drives at 30 m/s. "
                       "Where does the truck catch up?";
const char *kAhead = "A cyclist rides at 5 m/s, 100 m ahead of a runner. The runner runs at 7 m/s in the same direction. "
                     "How long until the runner catches the cyclist?";
const char *kAmbiguous = "A car leaves a town at 20 m/s. A truck follows the car 10 s later. It drives at 30 m/s. "
                         "When does the truck catch up?";

Quantity quantity(const std::string &text) {
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

CatchUpBody body(const char *name, const char *position, const char *velocity, const char *start) {
    CatchUpBody value;
    value.name = name;
    value.frame.name = "track";
    value.position_at_start = quantity(position);
    value.velocity_at_start = quantity(velocity);
    value.start_time = quantity(start);
    return value;
}

std::string record_of(const CatchUpProblem &problem) {
    Arena arena;
    Derivation derivation;
    const CatchUpResult result = solve_catch_up(arena, derivation, problem);
    return result.event_time_text + " " + result.time_unit_text + " " + result.event_position_text + " " +
           result.position_unit_text + "\n" + render_derivation(arena, derivation);
}

std::string direct(const CatchUpBody &first, const CatchUpBody &second) {
    CatchUpProblem problem;
    problem.first = first;
    problem.second = second;
    return record_of(problem);
}

std::string word(const wp::ConfirmResult &confirmed) {
    if (!confirmed.committed)
        return "not committed: " + confirmed.detail;
    CatchUpProblem problem;
    std::string why;
    if (!wp::to_catch_up(*confirmed.committed, &problem, &why))
        return "not converted: " + why;
    return record_of(problem);
}

const wp::Quantity *by_id(const wp::ProblemIR &ir, const std::string &id) {
    for (const wp::Quantity &q : ir.quantities) {
        if (q.id == id)
            return &q;
    }
    return nullptr;
}

void test_corpus(TestSink &t) {
    std::istringstream corpus(slurp("tests/wp_corpus/pursuit.corpus"));
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
            t.equal(wp::pursuit_summary(wp::interpret_pursuit("corpus-" + name, text)), expected,
                    "the pursuit corpus case " + name + " reads as annotated");
            ++cases;
        }
    }
    t.check(cases == 12 && kinds.size() == 4,
            "the pursuit corpus holds twelve cases with positive, negative, boundary and ambiguous examples: " +
                std::to_string(cases));
}

void test_equivalence(TestSink &t) {
    const std::string expected = direct(body("car", "0 m", "20 m/s", "0 s"), body("truck", "0 m", "30 m/s", "10 s"));
    t.check(expected.compare(0, 11, "30 s 600 m\n") == 0,
            "the structured catch-up problem meets at 30 s and 600 m: " + expected.substr(0, 12));
    const wp::PursuitResult later = wp::interpret_pursuit("src-later", kLater);
    const wp::ConfirmResult confirmed = wp::confirm_pursuit(later, kLater, {}, "tester");
    t.check(word(confirmed) == expected, "the later departure reproduces the structured problem's record exactly");
    const wp::PursuitResult pronoun = wp::interpret_pursuit("src-pronoun", kPronoun);
    t.check(word(wp::confirm_pursuit(pronoun, kPronoun, {}, "tester")) == expected,
            "and so does the same problem with the speed given through \"It\"");
    const wp::PursuitResult ahead = wp::interpret_pursuit("src-ahead", kAhead);
    const std::string head_start =
        direct(body("cyclist", "100 m", "5 m/s", "0 s"), body("runner", "0 m", "7 m/s", "0 s"));
    t.check(word(wp::confirm_pursuit(ahead, kAhead, {}, "tester")) == head_start &&
                head_start.compare(0, 11, "50 s 350 m\n") == 0,
            "a head start reproduces the structured problem with the runner at the origin");
}

void test_structure(TestSink &t) {
    const wp::PursuitResult r = wp::interpret_pursuit("src-later", kLater);
    if (!r.draft) {
        t.check(false, "the later departure proposes a problem: " + r.detail);
        return;
    }
    const wp::ProblemIR &ir = *r.draft;
    const wp::Quantity *truck_start = by_id(ir, "q-start_time-truck");
    const wp::Quantity *truck_speed = by_id(ir, "q-initial_velocity-truck");
    const wp::Quantity *goal = by_id(ir, "q-meeting");
    t.check(ir.entities.size() == 2 && truck_start && truck_start->owner_entity_id == "body-truck" &&
                truck_start->state_or_event_id == "depart-truck" && truck_speed &&
                truck_speed->owner_entity_id == "body-truck",
            "each quantity is owned by its body and the later start attaches to that body's departure");
    t.check(goal && goal->semantic_type == "meeting_time" && goal->state_or_event_id == "meeting" &&
                ir.events.size() == 3 && ir.events.back().id == "meeting",
            "and the goal is the time of the meeting event");
    wp::IrValidation why;
    t.check(!wp::commit(ir, r.lexical.source, &why) && why.fault == wp::IrFault::UnconfirmedInference,
            "the inferred clock origin and direction keep the draft from committing unconfirmed");
    const wp::ConfirmResult confirmed = wp::confirm_pursuit(r, kLater, {}, "tester");
    t.check(confirmed.committed &&
                confirmed.committed->ir().confirmation_record.material_assumption_ids ==
                    std::vector<std::string>{"q-start_time-car", "a-same-way"} &&
                confirmed.committed->ir().confirmation_record.parser_versions ==
                    std::string(wp::kPursuitGrammarVersion) + "+" + wp::kLexiconVersion,
            "confirmation approves the clock origin and the direction under the pursuit grammar's version");
    t.check(r.set.candidates.size() == 1 && r.set.candidates[0].proposed_problem_model == wp::kPursuitFamily &&
                r.set.grammar_module_versions.size() == 1 &&
                r.set.grammar_module_versions[0] == wp::kPursuitGrammarVersion,
            "the interpretation proposes the catch-up family");
}

void test_clarification(TestSink &t) {
    const wp::PursuitResult r = wp::interpret_pursuit("src-ambiguous", kAmbiguous);
    t.check(r.outcome == wp::GrammarOutcome::NeedsClarification && !r.draft && r.clarifications.size() == 1 &&
                r.clarifications[0].options == std::vector<std::string>{"truck", "car"},
            "\"It\" after a sentence naming both bodies is asked about, with both bodies offered");
    t.equal(wp::confirm_status_name(wp::confirm_pursuit(r, kAmbiguous, {}, "tester").status), "needs clarification",
            "and cannot be confirmed unanswered");
    t.equal(wp::confirm_status_name(wp::confirm_pursuit(r, kAmbiguous, {{"c1", "bus"}}, "tester").status),
            "invalid answer", "a body that was not offered is refused");
    const wp::ConfirmResult car = wp::confirm_pursuit(r, kAmbiguous, {{"c1", "car"}}, "tester");
    t.check(car.status == wp::ConfirmStatus::Rejected && car.detail.find("two values") != std::string::npos,
            "answering the car gives it two speeds, which is rejected");
    const wp::ConfirmResult truck = wp::confirm_pursuit(r, kAmbiguous, {{"c1", "truck"}}, "tester");
    t.check(word(truck) == direct(body("car", "0 m", "20 m/s", "0 s"), body("truck", "0 m", "30 m/s", "10 s")),
            "answering the truck reproduces the structured problem");
}

void test_gates(TestSink &t) {
    const wp::PursuitResult r = wp::interpret_pursuit("src-later", kLater);
    std::string edited = kLater;
    edited.replace(edited.find("20 m/s"), 2, "25");
    t.equal(wp::confirm_status_name(wp::confirm_pursuit(r, edited, {}, "tester").status), "source changed",
            "text edited after reading cannot be confirmed");
    wp::PursuitResult broken = r;
    broken.facts[0].unit = "s";
    const wp::ConfirmResult hard = wp::confirm_pursuit(broken, kLater, {}, "tester");
    t.check(hard.status == wp::ConfirmStatus::Rejected && hard.validation.fault == wp::IrFault::DimensionMismatch,
            "confirmation never commits a speed with the dimension of a time");
    t.check(wp::confirm_pursuit(r, kLater, {}, "tester").status == wp::ConfirmStatus::Confirmed,
            "while the unbroken reading confirms");
    wp::LexicalLimits few;
    few.max_quantities = 1;
    t.check(wp::interpret_pursuit("src", kLater, few).outcome == wp::GrammarOutcome::ResourceExhausted,
            "running out of room while reading stays resource exhaustion");
    const wp::PursuitResult refused = wp::interpret_pursuit("src", "A car leaves a town at 20%. When does it meet?");
    t.check(refused.outcome == wp::GrammarOutcome::Unsupported &&
                wp::confirm_pursuit(refused, "A car leaves a town at 20%. When does it meet?", {}, "tester").status ==
                    wp::ConfirmStatus::Rejected,
            "and a lexical refusal leaves nothing to confirm");
}

std::string converted(const wp::ProblemIR &draft, const wp::SourceDocument &source) {
    const wp::ConfirmResult confirmed = wp::commit_confirmed(draft, source, "tester");
    if (!confirmed.committed)
        return "not committed: " + confirmed.detail;
    CatchUpProblem problem;
    std::string why;
    return wp::to_catch_up(*confirmed.committed, &problem, &why) ? "converted" : why;
}

void test_converter(TestSink &t) {
    const wp::PursuitResult r = wp::interpret_pursuit("src-later", kLater);
    if (!r.draft) {
        t.check(false, "the later departure proposes a problem to convert");
        return;
    }
    t.equal(converted(*r.draft, r.lexical.source), "converted", "the confirmed pursuit problem converts");
    wp::ProblemIR one_body = *r.draft;
    one_body.entities.pop_back();
    std::vector<wp::Quantity> kept;
    for (const wp::Quantity &q : one_body.quantities) {
        if (q.owner_entity_id != "body-truck")
            kept.push_back(q);
    }
    one_body.quantities = kept;
    one_body.knowns.clear();
    for (const wp::Quantity &q : kept) {
        if (!q.value_expression.empty())
            one_body.knowns.push_back(q.id);
    }
    one_body.events.erase(one_body.events.begin() + 1);
    t.equal(converted(one_body, r.lexical.source), "a catch-up problem needs exactly two bodies",
            "a problem with one body is refused by the converter");
    wp::ProblemIR partial = *r.draft;
    for (size_t i = 0; i < partial.quantities.size(); ++i) {
        if (partial.quantities[i].id == "q-start_position-truck") {
            partial.quantities.erase(partial.quantities.begin() + static_cast<long>(i));
            break;
        }
    }
    std::vector<std::string> knowns;
    for (const std::string &k : partial.knowns) {
        if (k != "q-start_position-truck")
            knowns.push_back(k);
    }
    partial.knowns = knowns;
    t.equal(converted(partial, r.lexical.source), "the truck needs a velocity, a start time and a start position",
            "and so is a body missing its start position");
    wp::ProblemIR elapsed = *r.draft;
    for (wp::Quantity &q : elapsed.quantities) {
        if (q.id == "q-meeting")
            q.semantic_type = "elapsed_time";
    }
    t.equal(converted(elapsed, r.lexical.source), "the goal is not the meeting time or place",
            "two bodies asked for a plain elapsed time are refused too");
    const char *cart = "A red cart moving at 5 m/s speeds up at 3 m/s^2 for 4 s. How fast is it moving then?";
    const wp::GrammarResult motion = wp::interpret_motion("src-cart", cart);
    t.equal(motion.draft ? converted(*motion.draft, motion.lexical.source) : std::string("no draft"),
            "a catch-up problem needs exactly two bodies", "and a one-body motion problem is not a catch-up problem");
}

}  // namespace

void run_pursuit_tests(TestSink &sink) {
    test_converter(sink);
    test_corpus(sink);
    test_equivalence(sink);
    test_structure(sink);
    test_clarification(sink);
    test_gates(sink);
}

}  // namespace nps
