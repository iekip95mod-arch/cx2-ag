// PRD section 22.1's MVP acceptance corpus, and MVP criteria 2, 10 and 11 with it. Every case names
// an input, the outcome the engine must reach and, where there is one, the answer. The answer is
// written the way a person writes it and compared in canonical form, so a case does not have to
// spell the printer's parenthesisation.
//
// The gate is the count of distinct semantic cases, not the count of records, because section 22.1
// says its counts are "minimum distinct semantic cases, not cosmetic variants with different
// numbers". A signature is the family, the canonical shape of the input with each literal reduced to
// a class, the outcome and the class of the result, so renumbering a case the corpus already holds
// adds a record and no coverage. tools/corpus-mutation.sh proves that, along with the rest.
//
// The count is a floor on the five families section 22.1 names, not a statement about the engine
// set. Eight engines have no family here, so a green gate says nothing at all about them.
//
// Named apart from nps_corpus_audit on purpose: that one is the copyright-safe EPUB metadata audit
// the agent pack asks for, and it has nothing to do with this corpus.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "nps/core/ast.h"
#include "nps/core/budgets.h"
#include "nps/core/canonical.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/physics/kinematics.h"
#include "nps/steps/calculus.h"
#include "nps/steps/derivation.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/integrate.h"
#include "nps/steps/linear.h"
#include "nps/steps/quadratic.h"

#include "catalog.h"
#include "evidence.h"
#include "../tests/step_invariants.h"

using nps::Arena;
using nps::Budget;
using nps::CalculusResult;
using nps::Derivation;
using nps::DerivationStatus;
using nps::DiffResult;
using nps::IntegrateResult;
using nps::KinematicsProblem;
using nps::KinematicsResult;
using nps::Kind;
using nps::kNoNode;
using nps::Node;
using nps::NodeId;
using nps::ParseResult;
using nps::QuadraticResult;
using nps::SolveResult;
using nps::Step;
using nps::StepKind;
using nps_tools::append_evidence;
using nps_tools::count_text;
using nps_tools::first_word;
using nps_tools::trimmed;

namespace {

struct Minimum {
    const char *family;
    size_t cases;
    const char *success_outcome;
};

// PRD section 22.1's table. The counts are minimum distinct semantic cases, which is why the gate
// below runs on signatures rather than on how many records the corpus files hold.
const Minimum kMinimums[] = {
    {"linear", 150, "solved"},
    {"derivative", 300, "differentiated"},
    {"integral", 120, "integrated"},
    {"kinematics", 100, "solved"},
    {"invalid", 150, ""},
};

const Minimum *minimum_for(const std::string &family) {
    for (size_t i = 0; i < sizeof(kMinimums) / sizeof(kMinimums[0]); ++i) {
        if (family == kMinimums[i].family)
            return &kMinimums[i];
    }
    return nullptr;
}

struct Case {
    std::string id;
    std::string family;
    std::string file;
    size_t line = 0;
    std::map<std::string, std::string> fields;

    std::string value(const char *key) const {
        const std::map<std::string, std::string>::const_iterator it = fields.find(key);
        return it == fields.end() ? std::string() : it->second;
    }
    bool has(const char *key) const { return fields.find(key) != fields.end(); }

    // Which arithmetic the solve runs under. Absent means exact, which is the product default, so a
    // case that says nothing is testing the mode nearly every case should be testing. A spelling
    // that is neither is refused rather than read as the default, because a case that claimed a mode
    // it never ran in would pass while testing nothing.
    bool numeric_mode(nps::NumericMode *out) const {
        const std::string named = value("numeric");
        if (named.empty() || named == "exact") {
            *out = nps::NumericMode::Exact;
            return true;
        }
        if (named == "decimal") {
            *out = nps::NumericMode::Decimal;
            return true;
        }
        return false;
    }

    // Section 22.1's fifth row names resource-limit inputs, so a case can tighten a budget and the
    // engine's halt becomes the outcome under test. Absent means the default budget.
    Budget budget() const {
        Budget b;
        size_t value = 0;
        if (unsigned_field("max_rewrites", &value))
            b.max_rewrites = value;
        if (unsigned_field("max_steps", &value))
            b.max_steps = value;
        if (unsigned_field("max_backend_calls", &value))
            b.max_backend_calls = value;
        // Read here rather than only in the three above, because a case cannot pin what a rule does
        // when a limit lands mid-split without being able to set the limit that lands there.
        if (unsigned_field("max_branches", &value))
            b.max_branches = value;
        return b;
    }

    bool unsigned_field(const char *key, size_t *out) const {
        if (!has(key))
            return false;
        const std::string text = value(key);
        size_t total = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] < '0' || text[i] > '9')
                return false;
            total = total * 10 + static_cast<size_t>(text[i] - '0');
        }
        *out = total;
        return !text.empty();
    }
};

struct Run {
    std::string outcome;
    NodeId answer = kNoNode;
    bool has_answer = false;
    std::string answer_text;
    DerivationStatus status = DerivationStatus::NotRecorded;
    std::string shape;
    std::string result_class;
    // Which numeric-mode steps the derivation actually recorded. Two flags rather than one: the
    // promotion fires on a decimal in the input and the report on a terminating decimal in the
    // answer, so 2x = 1 in decimal mode reports without ever promoting.
    bool promoted_decimals = false;
    bool reported_decimals = false;
    std::vector<std::string> restrictions;
    size_t restrictions_recorded = 0;
    std::string detail;
    bool every_transformation_verified = true;
    bool any_failed_verification = false;
    size_t transformations = 0;
    // One entry per invariant that a step of this derivation broke, already worded for the report.
    std::vector<std::string> broken_invariants;
    std::string could_not_run;
};

// The pass that checks them lives in tests/step_invariants.h so the golden fixtures can be a second
// population for it. They cover families this corpus does not reach, and a criterion that held over
// a thousand corpus cases and was never read against the fixtures is a gap rather than a result.
nps::invariants::Pass g_pass;

void read_corpus_file(const std::string &path, std::vector<Case> *out,
                      std::vector<std::string> *faults) {
    std::ifstream in(path.c_str());
    if (!in) {
        faults->push_back("cannot read " + path);
        return;
    }
    const std::string name = std::filesystem::path(path).filename().string();
    std::string line;
    size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::string s = trimmed(line);
        if (s.empty() || s[0] == '#')
            continue;
        std::string word, rest;
        if (!first_word(s, &word, &rest)) {
            faults->push_back(name + ":" + count_text(number) + " is not a field and a value");
            continue;
        }
        if (word == "case") {
            Case c;
            c.id = rest;
            c.file = name;
            c.line = number;
            out->push_back(c);
            continue;
        }
        if (out->empty()) {
            faults->push_back(name + ":" + count_text(number) + " comes before any case");
            continue;
        }
        Case &c = out->back();
        if (c.fields.find(word) != c.fields.end()) {
            faults->push_back(c.id + " repeats the field " + word);
            continue;
        }
        c.fields[word] = rest;
        if (word == "family")
            c.family = rest;
    }
}

// Section 22.1 counts distinct semantic cases and says outright that cosmetic variants with
// different numbers are not distinct. So a literal contributes its class rather than its value, and
// two cases that differ only in which numbers were chosen collapse onto one signature.
//
// Two values earn a class of their own. Zero is the degenerate value every rule treats separately,
// and minus one is what tells the reciprocal integral apart from the power rule, so x^-1 and x^-2
// have to stay two cases. One does not: after canonicalisation there is no x^1 and no 1*x left, so
// giving it a class only split x + 1 from x + 5, which is the cosmetic variation section 22.1 says
// not to count.
void shape_of(const Arena &arena, NodeId id, const std::string &variable, std::string *out) {
    int64_t v = 0;
    if (nps::small_integer(arena, id, &v)) {
        if (v == 0)
            *out += "<0>";
        else if (v == -1)
            *out += "<-1>";
        else if (v < 0)
            *out += "<-n>";
        else
            *out += "<n>";
        return;
    }
    const Node &n = arena.at(id);
    switch (n.kind) {
        case Kind::Integer:
            *out += "<wide>";
            return;
        case Kind::Decimal:
            *out += "<d>";
            return;
        case Kind::Symbol:
            // The letter the problem happens to use is cosmetic in exactly the way section 22.1
            // means, so d/dx of x^2 and d/dt of t^2 are one case rather than two. Every other symbol
            // keeps its name, since a second symbol is a symbolic parameter and that is semantic.
            *out += arena.text(id) == variable ? "<var>" : arena.text(id);
            return;
        case Kind::Neg: {
            *out += "-";
            const nps::ChildView kids = arena.children(id);
            if (kids.size() == 1)
                shape_of(arena, kids[0], variable, out);
            return;
        }
        default:
            break;
    }
    const char *joiner = "?";
    switch (n.kind) {
        case Kind::Add: joiner = " + "; break;
        case Kind::Mul: joiner = " * "; break;
        case Kind::Pow: joiner = "^"; break;
        case Kind::Equals: joiner = " = "; break;
        case Kind::Less: joiner = " < "; break;
        case Kind::LessEqual: joiner = " <= "; break;
        case Kind::Greater: joiner = " > "; break;
        case Kind::GreaterEqual: joiner = " >= "; break;
        default: break;
    }
    const nps::ChildView kids = arena.children(id);
    if (n.kind == Kind::Call) {
        *out += arena.text(id);
        *out += "(";
        for (size_t i = 0; i < kids.size(); ++i) {
            if (i)
                *out += ", ";
            shape_of(arena, kids[i], variable, out);
        }
        *out += ")";
        return;
    }
    *out += "(";
    for (size_t i = 0; i < kids.size(); ++i) {
        if (i)
            *out += joiner;
        shape_of(arena, kids[i], variable, out);
    }
    *out += ")";
}

bool name_starts(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool name_continues(char c) { return name_starts(c) || (c >= '0' && c <= '9'); }

// A refusal has no tree to shape, so its tokens stand in, with the literals and the unknown
// abstracted exactly as shape_of abstracts them.
std::string unparsed_shape(const std::string &input, const std::string &variable) {
    std::string out;
    size_t i = 0;
    while (i < input.size()) {
        const char c = input[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (!out.empty())
            out += " ";
        if (c >= '0' && c <= '9') {
            while (i < input.size() && input[i] >= '0' && input[i] <= '9')
                ++i;
            // One placeholder for every number, not shape_of's <0> and <1>: a value no engine read
            // cannot be what makes two refusals different cases.
            out += "<n>";
            continue;
        }
        if (name_starts(c)) {
            const size_t start = i;
            while (i < input.size() && name_continues(input[i]))
                ++i;
            const std::string name = input.substr(start, i - start);
            out += (!variable.empty() && name == variable) ? "<var>" : name;
            continue;
        }
        out += c;
        ++i;
    }
    return "<unparsed> " + out;
}

std::string shape_text(Arena &arena, NodeId id, const std::string &input,
                       const std::string &variable) {
    if (id == kNoNode)
        return unparsed_shape(input, variable);
    const NodeId canonical = canonicalize(arena, id);
    std::string out;
    shape_of(arena, canonical == kNoNode ? id : canonical, variable, &out);
    return out;
}

bool contains_kind(const Arena &arena, NodeId id, Kind kind) {
    if (id == kNoNode)
        return false;
    if (arena.at(id).kind == kind)
        return true;
    const nps::ChildView kids = arena.children(id);
    for (size_t i = 0; i < kids.size(); ++i) {
        if (contains_kind(arena, kids[i], kind))
            return true;
    }
    return false;
}

// A result's class, so 2x + 5 = 13 and 2x + 5 = 14 stay two cases: one lands on an integer and the
// other on a fraction, and which arithmetic a case reaches is semantic rather than cosmetic.
std::string result_class_of(const Arena &arena, NodeId id) {
    if (id == kNoNode)
        return "none";
    int64_t v = 0;
    if (nps::small_integer(arena, id, &v))
        return v == 0 ? "zero" : (v < 0 ? "negative integer" : "integer");
    if (contains_kind(arena, id, Kind::Symbol))
        return "symbolic";
    if (contains_kind(arena, id, Kind::Decimal))
        return "decimal";
    return "rational";
}

std::string text_result_class(const std::string &text) {
    if (text.empty())
        return "none";
    bool decimal = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '.')
            decimal = true;
    }
    if (decimal)
        return "decimal";
    return text[0] == '-' ? "negative integer" : "integer";
}

// Whether the step is one the viewer lists rather than one hidden inside an expansion: a root, or a
// direct child of a root. Criterion 5 is about what a user can select, which is these.
void read_derivation(const Arena &arena, const Derivation &derivation, bool refusal, Run *run) {
    run->status = derivation.context.derivation_status;
    for (size_t i = 0; i < derivation.size(); ++i) {
        const nps::StepId id = static_cast<nps::StepId>(i);
        const Step &step = derivation.at(id);
        for (size_t r = 0; r < step.domain_restrictions.size(); ++r) {
            run->restrictions.push_back(step.domain_restrictions[r]);
            ++run->restrictions_recorded;
        }
        if (step.rule_id == "num.decimal-to-rational")
            run->promoted_decimals = true;
        if (step.rule_id == "num.rational-to-decimal")
            run->reported_decimals = true;
        if (step.has_failed_verification())
            run->any_failed_verification = true;
        if (step.kind == StepKind::Transformation) {
            ++run->transformations;
            if (!step.verified())
                run->every_transformation_verified = false;
        }

    }

    g_pass.walk(arena, derivation, refusal, true, &run->broken_invariants, &run->has_answer);
}

NodeId parse_into(Arena &arena, const std::string &text, std::string *error) {
    ParseResult r = parse(arena, text);
    if (!r.ok()) {
        *error = std::string(status_name(r.status)) + ": " + r.message;
        return kNoNode;
    }
    return r.root;
}

void run_linear(Arena &arena, const Case &c, bool refusal, Run *run) {
    std::string error;
    const NodeId equation = parse_into(arena, c.value("input"), &run->detail);
    const std::string unknown_text = c.value("unknown");
    if (unknown_text.empty()) {
        run->could_not_run = "no unknown field";
        return;
    }
    const NodeId unknown = arena.symbol(unknown_text);
    run->shape = shape_text(arena, equation, c.value("input"), unknown_text);
    if (equation == kNoNode) {
        // A case whose text does not parse is still a case: the corpus records that the engine is
        // never reached, and the invalid family is where those belong.
        run->outcome = "not parsed";
        run->result_class = "none";
        return;
    }
    Derivation derivation;
    if (!c.numeric_mode(&derivation.request.numeric_mode)) {
        run->could_not_run = "numeric mode has to be exact or decimal";
        return;
    }
    const SolveResult result = solve_linear(arena, derivation, equation, unknown, c.budget());
    run->outcome = solve_outcome_name(result.outcome);
    run->detail = result.detail;
    run->answer = result.solution;
    run->has_answer = result.solution != kNoNode;
    if (run->has_answer)
        run->answer_text = print(arena, result.solution);
    run->result_class = result_class_of(arena, result.solution);
    read_derivation(arena, derivation, refusal, run);
}

// The square-root rule, reached from the invalid family alone. PRD section 22.1 fixes the five
// families above and names no minimum for a quadratic one, so a case here is a refusal this engine
// owes rather than a solve it is counted for: the split itself is evidenced in the golden fixtures,
// which is the population that reaches one.
void run_quadratic(Arena &arena, const Case &c, bool refusal, Run *run) {
    const NodeId equation = parse_into(arena, c.value("input"), &run->detail);
    const std::string unknown_text = c.value("unknown");
    if (unknown_text.empty()) {
        run->could_not_run = "no unknown field";
        return;
    }
    const NodeId unknown = arena.symbol(unknown_text);
    run->shape = shape_text(arena, equation, c.value("input"), unknown_text);
    if (equation == kNoNode) {
        run->outcome = "not parsed";
        run->result_class = "none";
        return;
    }
    Derivation derivation;
    if (!c.numeric_mode(&derivation.request.numeric_mode)) {
        run->could_not_run = "numeric mode has to be exact or decimal";
        return;
    }
    const QuadraticResult result =
        solve_by_square_root(arena, derivation, equation, unknown, c.budget());
    run->outcome = quadratic_outcome_name(result.outcome);
    run->detail = result.detail;
    run->answer = result.solutions.empty() ? kNoNode : result.solutions[0];
    run->has_answer = !result.solutions.empty();
    if (run->has_answer)
        run->answer_text = print(arena, run->answer);
    run->result_class = result_class_of(arena, run->answer);
    read_derivation(arena, derivation, refusal, run);
}

void run_derivative(Arena &arena, const Case &c, bool refusal, Run *run) {
    const NodeId expression = parse_into(arena, c.value("input"), &run->detail);
    const std::string variable_text = c.value("variable");
    if (variable_text.empty()) {
        run->could_not_run = "no variable field";
        return;
    }
    const NodeId variable = arena.symbol(variable_text);
    run->shape = shape_text(arena, expression, c.value("input"), variable_text);
    if (expression == kNoNode) {
        run->outcome = "not parsed";
        run->result_class = "none";
        return;
    }
    Derivation derivation;
    if (!c.numeric_mode(&derivation.request.numeric_mode)) {
        run->could_not_run = "numeric mode has to be exact or decimal";
        return;
    }
    const DiffResult result = differentiate(arena, derivation, expression, variable, c.budget());
    run->outcome = diff_outcome_name(result.outcome);
    run->detail = result.detail;
    run->answer = result.derivative;
    run->has_answer = result.derivative != kNoNode;
    if (run->has_answer)
        run->answer_text = print(arena, result.derivative);
    run->result_class = result_class_of(arena, result.derivative);
    read_derivation(arena, derivation, refusal, run);
}

void run_integral(Arena &arena, const Case &c, bool refusal, Run *run) {
    const NodeId expression = parse_into(arena, c.value("input"), &run->detail);
    const std::string variable_text = c.value("variable");
    if (variable_text.empty()) {
        run->could_not_run = "no variable field";
        return;
    }
    const NodeId variable = arena.symbol(variable_text);
    run->shape = shape_text(arena, expression, c.value("input"), variable_text);
    if (expression == kNoNode) {
        run->outcome = "not parsed";
        run->result_class = "none";
        return;
    }
    Derivation derivation;
    if (!c.numeric_mode(&derivation.request.numeric_mode)) {
        run->could_not_run = "numeric mode has to be exact or decimal";
        return;
    }
    const IntegrateResult result = integrate(arena, derivation, expression, variable, c.budget());
    run->outcome = integrate_outcome_name(result.outcome);
    run->detail = result.detail;
    // The particular antiderivative is what an expectation can name, since the general one carries
    // the constant of integration and every case would have to spell it.
    run->answer = result.particular;
    run->has_answer = result.antiderivative != kNoNode;
    if (result.particular != kNoNode)
        run->answer_text = print(arena, result.particular);
    run->result_class = result_class_of(arena, result.particular);
    read_derivation(arena, derivation, refusal, run);
}

// The problem's shape rather than its expression: which quantities were given, for which unknown,
// each with the class of its value. Two kinematics problems that differ only in the numbers land on
// one signature, which is what section 22.1 asks for.
std::string kinematics_shape(const KinematicsProblem &problem) {
    std::vector<std::string> parts;
    for (size_t i = 0; i < problem.knowns.size(); ++i) {
        const nps::Known &known = problem.knowns[i];
        std::string one = known.symbol + "=";
        if (known.quantity.value.num == 0)
            one += "<0>";
        else if (known.quantity.precision.kind == nps::NumberKind::Exact)
            one += known.quantity.value.num < 0 ? "<-exact>" : "<exact>";
        else
            one += known.quantity.value.num < 0 ? "<-measured>" : "<measured>";
        one += " " + known.quantity.unit.text;
        parts.push_back(one);
    }
    std::sort(parts.begin(), parts.end());
    std::string out = "find " + problem.unknown;
    for (size_t i = 0; i < parts.size(); ++i)
        out += "; " + parts[i];
    return out;
}

void run_kinematics(Arena &arena, const Case &c, bool refusal, Run *run) {
    KinematicsProblem problem;
    if (!parse_kinematics(c.value("input"), &problem, &run->detail)) {
        run->outcome = "not parsed";
        // No unknown to name: the problem statement carries it and this one was refused before it
        // could be read.
        run->shape = unparsed_shape(c.value("input"), std::string());
        run->result_class = "none";
        return;
    }
    run->shape = kinematics_shape(problem);
    Derivation derivation;
    const KinematicsResult result = solve_kinematics(arena, derivation, problem, c.budget());
    run->outcome = kinematics_outcome_name(result.outcome);
    run->detail = result.detail;
    run->has_answer = !result.value_text.empty();
    run->answer_text = result.value_text;
    if (!result.unit_text.empty())
        run->answer_text += " " + result.unit_text;
    run->result_class = text_result_class(result.value_text);
    read_derivation(arena, derivation, refusal, run);
}

// CALC-010, refusals only for the reason run_quadratic gives. The input is a whole command, since
// the variable and the point are arguments rather than fields.
void run_tangent(Arena &arena, const Case &c, bool refusal, Run *run) {
    const NodeId invocation = parse_into(arena, c.value("input"), &run->detail);
    const std::string variable_text = c.value("variable");
    if (variable_text.empty()) {
        run->could_not_run = "no variable field";
        return;
    }
    run->shape = shape_text(arena, invocation, c.value("input"), variable_text);
    if (invocation == kNoNode) {
        run->outcome = "not parsed";
        run->result_class = "none";
        return;
    }
    Derivation derivation;
    if (!c.numeric_mode(&derivation.request.numeric_mode)) {
        run->could_not_run = "numeric mode has to be exact or decimal";
        return;
    }
    derivation.request.original_expression = c.value("input");
    const CalculusResult result = calculus_walkthrough(
        arena, derivation, nps::parse_command(arena, c.value("input"), variable_text), c.budget());
    run->outcome = calculus_outcome_name(result.outcome);
    run->detail = result.detail;
    run->answer = result.value;
    run->has_answer = result.value != kNoNode;
    if (run->has_answer)
        run->answer_text = print(arena, result.value);
    run->result_class = result_class_of(arena, result.value);
    read_derivation(arena, derivation, refusal, run);
}

void run_case(const Case &c, Run *run) {
    Arena arena;
    const std::string family = c.family;
    const std::string mode = c.has("mode") ? c.value("mode") : family;
    // Criterion 8 is about refusals, and the invalid family is where the corpus keeps them.
    const Minimum *m = minimum_for(family);
    const bool refusal = m != nullptr && *m->success_outcome == '\0';
    if (mode == "linear")
        run_linear(arena, c, refusal, run);
    else if (mode == "quadratic")
        run_quadratic(arena, c, refusal, run);
    else if (mode == "derivative")
        run_derivative(arena, c, refusal, run);
    else if (mode == "integral")
        run_integral(arena, c, refusal, run);
    else if (mode == "kinematics")
        run_kinematics(arena, c, refusal, run);
    else if (mode == "tangent" || mode == "linearize")
        run_tangent(arena, c, refusal, run);
    else
        run->could_not_run = "no runner for mode " + mode;
}

bool canonically_equal(const std::string &expected, const std::string &actual) {
    if (expected == actual)
        return true;
    Arena arena;
    ParseResult a = parse(arena, expected);
    ParseResult b = parse(arena, actual);
    if (!a.ok() || !b.ok())
        return false;
    const NodeId left = canonicalize(arena, a.root);
    const NodeId right = canonicalize(arena, b.root);
    return left != kNoNode && left == right;
}

// A well-posed problem whose answer is not a value: an empty solution set, or one that holds for
// every value. Both are complete verified derivations, so the refusal rules do not apply to them.
bool degenerate_outcome(const std::string &outcome) {
    return outcome == "no solution" || outcome == "true for every value";
}

void judge(const Case &c, const Run &run, std::vector<std::string> *faults) {
    const std::string where = c.file + ":" + count_text(c.line) + " " + c.id;
    if (!run.could_not_run.empty()) {
        faults->push_back(where + ": " + run.could_not_run);
        return;
    }
    const std::string expected = c.value("expect");
    if (expected.empty()) {
        faults->push_back(where + ": no expect field");
        return;
    }
    // Reported before the outcome check, so a break the pass counted is never dropped by a return.
    for (size_t i = 0; i < run.broken_invariants.size(); ++i)
        faults->push_back(where + ": " + run.broken_invariants[i]);
    if (expected != run.outcome) {
        faults->push_back(where + ": expected outcome '" + expected + "', got '" + run.outcome +
                          "'");
        return;
    }

    const Minimum *m = minimum_for(c.family);
    const bool refusal_family = m != nullptr && *m->success_outcome == '\0';
    const bool reached_an_answer = m != nullptr && expected == m->success_outcome;

    if (c.has("result")) {
        if (!run.has_answer) {
            faults->push_back(where + ": names a result and the engine produced none");
        } else if (!canonically_equal(c.value("result"), run.answer_text)) {
            faults->push_back(where + ": expected result '" + c.value("result") + "', got '" +
                              run.answer_text + "'");
        }
    }

    // Criterion 8 wants an unsupported input to fail clearly rather than merely to fail, so a case
    // can pin the words the engine refused with.
    if (c.has("detail") && run.detail.find(c.value("detail")) == std::string::npos)
        faults->push_back(where + ": expected the detail to contain '" + c.value("detail") +
                          "', got '" + run.detail + "'");

    if (c.has("restriction")) {
        bool found = false;
        for (size_t i = 0; i < run.restrictions.size(); ++i) {
            if (run.restrictions[i].find(c.value("restriction")) != std::string::npos)
                found = true;
        }
        // Criterion 10 names a lost domain restriction as an acceptance failure of its own, so a
        // case that says one is required fails when the derivation stops carrying it.
        if (!found)
            faults->push_back(where + ": the derivation lost the restriction '" +
                              c.value("restriction") + "'");
    }

    // Criterion 10's "no known incorrect accepted step".
    if (run.any_failed_verification && run.status != DerivationStatus::VerificationFailed)
        faults->push_back(where + ": a step's verification failed and the outcome does not say so");

    // Criterion 11: "no false claim of a complete verified solution", meaning not by the status and
    // not by an answer left sitting there. This used to bind only the invalid family, which is where
    // most refusals live but not where all of them do. A kinematics case expecting a dimension
    // mismatch is a refusal in a solving family, and it was checked for neither. What decides the
    // rule is what the engine was asked to do, not which file the case is written in.
    if (refusal_family || (!reached_an_answer && !degenerate_outcome(expected))) {
        if (run.status == DerivationStatus::SolvedAndVerified)
            faults->push_back(where + ": a refusal claims a complete verified solution");
        if (run.has_answer)
            faults->push_back(where + ": a refusal produced the answer '" + run.answer_text + "'");
        return;
    }

    if (reached_an_answer) {
        // A case that says the engine reaches an answer has to name it, or the outcome check alone
        // would pass a solve that returned the wrong number.
        if (!c.has("result"))
            faults->push_back(where + ": expects an answer and names no result");
        if (!run.has_answer)
            faults->push_back(where + ": succeeded and produced no answer");
        if (run.status != DerivationStatus::SolvedAndVerified)
            faults->push_back(where + ": succeeded with derivation status '" +
                              derivation_status_name(run.status) + "'");
        return;
    }

    // What is left is a well-posed problem whose answer is not a value: an empty solution set, or
    // one that holds for every value. Both are complete verified derivations, which is why they are
    // in a solving family rather than the invalid one, and neither may hand back a value.
    if (run.has_answer)
        faults->push_back(where + ": a degenerate outcome produced the answer '" + run.answer_text +
                          "'");
}

// An invariant that has never been shown to fail reads as coverage and is worse than an honest gap.
// So each of the five gets a derivation built to break it and nothing else, run through the same
// pass the corpus uses, and the pass has to report that one criterion and only that one.
//
// What is faked here is the derivation, not the check. Breaking a real engine to prove this would
// mean shipping a broken engine, and building the record by hand exercises exactly the code the
// corpus run exercises.
struct Broken {
    const char *criterion;
    const char *what;
    bool refusal;
};

void reset_criteria() { g_pass.reset(); }

nps::NodeId sum_of(Arena &arena, const char *left, const char *right) {
    return arena.binary(nps::Kind::Add, arena.integer(left), arena.integer(right));
}

bool refused_definition_flaw(const std::string &flaw) {
    return flaw == "undeclared definition prefix" || flaw == "unverified definition prefix" ||
           flaw == "unfinished definition";
}

// A record a well behaved engine would produce: a plan with a strategy, and one verified
// transformation under it. The flaw argument names the one thing to leave out.
void build_derivation(Arena &arena, Derivation *d, const std::string &flaw) {
    nps::PlanPayload plan;
    plan.strategy_id = flaw == "plan strategy" ? "" : "selftest.plan";
    plan.selected_strategy = "A selftest strategy";
    plan.selection_rationale = "the record is built to exercise the invariant pass";
    Step plan_step;
    plan_step.kind = StepKind::Plan;
    plan_step.phase = "plan";
    plan_step.goal = "Exercise the invariant pass";
    plan_step.rule_id = "selftest.plan";
    plan_step.rule_name = "Selftest plan";
    plan_step.explanation_short = "A plan a well behaved engine would write";
    // PHYS-026's first sentence: selecting a strategy has to record what makes it apply. A plan
    // with no registered condition is the fault the requirement names, so the baseline record
    // registers one rather than standing as a well behaved record that could not pass.
    nps::register_strategy_precondition(
        plan, plan_step, "pre.selftest.applies", "the record is the shape the pass expects",
        "selftest structural check", nps::EvidenceStrength::StructurallyValid,
        nps::VerificationOutcome::Passed, "the record is the shape the pass expects");
    const nps::StepId root = d->add_plan(nps::kNoStep, plan_step, plan);

    nps::TransformationPayload payload;
    // The before state is the sum rather than the left operand, because the step claims an
    // equivalent expression and VER-002 now reads that claim against the two nodes. A record whose
    // sides are 2 and 3 asserts that two is three, and a fixture standing in for a well behaved
    // engine cannot be built on one.
    payload.before = sum_of(arena, "2", "1");
    payload.after = flaw == "exact value"             ? arena.decimal("3.0")
                    : flaw == "false equivalence"     ? arena.integer("4")
                    : flaw == "unfinished definition" ? nps::kNoNode
                                                      : arena.integer("3");
    payload.concrete_action = flaw == "action" ? "" : "Add one";
    payload.reversible = true;

    Step move;
    move.kind = StepKind::Transformation;
    move.phase = "solve";
    move.goal = "Add one";
    move.rule_id = flaw == "rule id" ? "" : "selftest.add-one";
    move.rule_name = "Selftest addition";
    move.explanation_short = "Adding one to a number";
    move.explanation_detailed = "Reach for this whenever a number needs to be one larger.";
    move.claim = refused_definition_flaw(flaw) ? nps::ClaimType::Definition
                                               : nps::ClaimType::EquivalentExpression;
    if (flaw != "verification" && flaw != "halted unchecked step" &&
        flaw != "unverified definition prefix") {
        nps::VerificationRecord v;
        v.method = "selftest arithmetic";
        v.outcome = nps::VerificationOutcome::Passed;
        v.detail = "two plus one is three";
        move.verifications.push_back(v);
    }
    const nps::StepId parent = d->add_transformation(root, move, payload);

    // STEP-025 lets a solve that ran out keep the prefix that was checked. The step below was not,
    // so criterion 8 has to separate the two rather than wave the whole halt through.
    if (flaw == "halted unchecked step")
        d->context.derivation_status = nps::DerivationStatus::ResourceLimitReached;

    if (refused_definition_flaw(flaw))
        d->context.derivation_status = nps::DerivationStatus::Unsupported;

    // A grandchild of the plan rather than a child, so it is not a major step. Criterion 5 asks
    // only about major steps, which is what leaves an arm that breaks STEP-002 and nothing else.
    nps::TransformationPayload inner;
    inner.before = sum_of(arena, "3", "1");
    inner.after = arena.integer("4");
    inner.concrete_action = "Add one again";
    inner.reversible = true;

    Step nested;
    nested.kind = StepKind::Transformation;
    nested.phase = "solve";
    nested.goal = "Add one again";
    nested.rule_id = "selftest.add-one";
    nested.rule_name = flaw == "nested rule name"    ? ""
                       : flaw == "generic rule name" ? "Simplify"
                                                     : "Selftest addition";
    nested.explanation_short = "Adding one to a number";
    // Empty for the STEP-021 arm, and the short one again for the "repeated recognition" arm, since
    // the requirement is broken both by saying nothing and by saying the same thing twice.
    nested.explanation_detailed =
        flaw == "nested recognition"     ? ""
        : flaw == "repeated recognition" ? nested.explanation_short
                                         : "Reach for this whenever a number needs to be one larger.";
    nested.claim = nps::ClaimType::EquivalentExpression;
    nps::VerificationRecord nv;
    nv.method = "selftest arithmetic";
    nv.outcome = nps::VerificationOutcome::Passed;
    nv.detail = "three plus one is four";
    nested.verifications.push_back(nv);
    d->add_transformation(parent, nested, inner);
}

int selftest() {
    const Broken cases[] = {
        // Both, and legitimately: STEP-002 asks for a rule identifier on a transformation in the
        // same words criterion 3 does, so a check that skipped it would not prove the requirement.
        {"3 and STEP-002", "rule id", false},
        {"4", "verification", false},
        {"5", "action", false},
        {"6", "exact value", false},
        // VER-002. The record keeps its claim, its rule, its obligation and its passing
        // verification and moves only the after node, which is the mutation the record-reading
        // arms of this pass cannot see and the one this criterion exists for.
        {"VER-002", "false equivalence", false},
        {"8", "", true},
        // The other half of criterion 8, and the half a check that only counts survivors cannot
        // see: a refusal whose status says it ran out, keeping a transformation nothing stands
        // behind. Criterion 4 fires on the same record, which is why both are named.
        {"4 and 8", "halted unchecked step", true},
        // The three ways a definition is still refused a prefix. Its one way through is a real
        // engine record in calculus_tests.cc.
        {"8", "undeclared definition prefix", true},
        {"4 and 8", "unverified definition prefix", true},
        {"8 and STEP-002", "unfinished definition", true},
        {"STEP-002", "nested rule name", false},
        {"STEP-022", "generic rule name", false},
        // Both ways STEP-021 goes wrong. Saying nothing is the obvious one; saying the short
        // explanation again is the one a rule falls into while looking like it complied.
        {"STEP-021", "nested recognition", false},
        {"STEP-021", "repeated recognition", false},
    };
    size_t missed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset_criteria();
        Arena arena;
        Derivation d;
        build_derivation(arena, &d, cases[i].what);
        Run run;
        const bool refusal = cases[i].refusal;
        read_derivation(arena, d, refusal, &run);
        const std::string reported = g_pass.broken_ids();
        if (reported != cases[i].criterion) {
            ++missed;
            std::cout << "acceptance corpus selftest: breaking criterion " << cases[i].criterion
                      << " was reported as \"" << (reported.empty() ? "nothing" : reported)
                      << "\"\n";
        }
    }
    reset_criteria();
    {
        Arena arena;
        Derivation d;
        build_derivation(arena, &d, "");
        Run run;
        read_derivation(arena, d, false, &run);
        if (!run.broken_invariants.empty()) {
            ++missed;
            std::cout << "acceptance corpus selftest: a record with nothing wrong reported "
                      << count_text(run.broken_invariants.size()) << " broken invariants\n";
        }
    }
    // Criterion 11 binds every refusal rather than only the invalid family, and no engine path
    // today returns a refusal outcome under a verified status, so no input can break that rule. A
    // guard nobody has seen fail reads as coverage, so it is broken here by hand instead.
    {
        Case c;
        c.id = "selftest.refusal-in-a-solving-family";
        c.family = "derivative";
        c.file = "selftest";
        c.fields["expect"] = "unsupported form";

        Run rewound;
        rewound.outcome = "unsupported form";
        rewound.status = DerivationStatus::Unsupported;
        std::vector<std::string> quiet;
        judge(c, rewound, &quiet);

        Run claimed = rewound;
        claimed.status = DerivationStatus::SolvedAndVerified;
        std::vector<std::string> loud;
        judge(c, claimed, &loud);

        if (!quiet.empty() || loud.size() != 1) {
            ++missed;
            std::cout << "acceptance corpus selftest: a refusal outside the invalid family gave "
                      << count_text(quiet.size()) << " faults when it rewound and "
                      << count_text(loud.size()) << " when it claimed a verified solution\n";
        }
    }

    reset_criteria();
    std::cout << "acceptance corpus selftest: "
              << count_text(g_pass.claims().size()) << " invariants, "
              << count_text(missed)
              << " that the pass did not catch when broken\n";
    return missed == 0 ? 0 : 1;
}

std::string signature_of(const Case &c, const Run &run) {
    // What the numeric mode did, read off the derivation rather than off the case file. The mode a
    // case declares is not evidence: a case author could raise the distinct count by typing one
    // line, which is the thing every other component here denies by being measured. Where the two
    // modes reach different answers result_class already separates them, so this earns its place
    // only on the routes, and the routes are four because the two steps are independent.
    const char *route = run.promoted_decimals
                            ? (run.reported_decimals ? "promoted and reported" : "promoted")
                            : (run.reported_decimals ? "reported" : "exact throughout");
    return c.family + " | " + run.shape + " | " + run.outcome + " | " + run.result_class + " | " +
           route;
}

// The evidence link lives on the check in the unit harness, and this is the same link from a run
// that is not in that harness. A universal claim needs a population, and 214 derivations are the
// population, so an invariant that held across all of them is the evidence a single hand-built
// record cannot be. Appended in the format tools/traceability.cc reads, the way
// tests/target/ui_smoke_v4.lua appends the UI half.
// A set as one table cell. An empty set prints as a dash rather than as nothing, so a blank column
// cannot be read as a column the tool forgot to fill.
std::string joined(const std::set<std::string> &values) {
    if (values.empty())
        return "-";
    std::string out;
    for (std::set<std::string>::const_iterator it = values.begin(); it != values.end(); ++it) {
        if (!out.empty())
            out += "; ";
        out += *it;
    }
    return out;
}

std::string evidence_row(const char *requirement, bool passed, const std::string &what) {
    return std::string("evidence\t") + requirement + (passed ? "\tpass\t" : "\tfail\t") +
           "acceptance corpus\t" + what;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--selftest")
        return selftest();
    if (argc < 3) {
        std::cout << "usage: nps_acceptance_corpus <corpus dir> <report.md> [--gate]\n";
        std::cout << "       nps_acceptance_corpus --selftest\n";
        return 2;
    }
    const std::string corpus_dir = argv[1];
    const std::string report_path = argv[2];
    // Two failures worth telling apart. A fault is a case whose declared outcome or answer the
    // engine no longer produces, and it is a regression. A family below its section 22.1 minimum is
    // the corpus being unfinished, which is a standing fact rather than something that just broke,
    // so the gate on it is a run of its own and says so by name.
    const bool gate = argc > 3 && std::string(argv[3]) == "--gate";

    std::vector<Case> cases;
    std::vector<std::string> faults;
    std::error_code ec;
    std::vector<std::string> files;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(corpus_dir, ec)) {
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    if (ec || files.empty()) {
        std::cout << "acceptance corpus: no corpus files in " << corpus_dir << "\n";
        return 1;
    }
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i < files.size(); ++i)
        read_corpus_file(files[i], &cases, &faults);

    std::set<std::string> ids;
    for (size_t i = 0; i < cases.size(); ++i) {
        if (cases[i].id.empty())
            faults.push_back(cases[i].file + " has a case with no id");
        else if (!ids.insert(cases[i].id).second)
            faults.push_back("duplicate case id " + cases[i].id);
        if (cases[i].family.empty())
            faults.push_back(cases[i].id + " names no family");
        else if (minimum_for(cases[i].family) == nullptr)
            faults.push_back(cases[i].id + " names family '" + cases[i].family +
                             "', which section 22.1 does not have");
        if (!cases[i].has("input"))
            faults.push_back(cases[i].id + " has no input");
        // Section 22.1's fifth row is inputs that must not succeed, so a case there declaring its
        // engine's success outcome would make the family prove the opposite of what it is for.
        const Minimum *declared = minimum_for(cases[i].family);
        const Minimum *engine = minimum_for(cases[i].value("mode"));
        if (declared != nullptr && *declared->success_outcome == '\0' && engine != nullptr &&
            cases[i].value("expect") == engine->success_outcome)
            faults.push_back(cases[i].id + " is in the invalid family and expects '" +
                             cases[i].value("expect") + "', which is how " +
                             cases[i].value("mode") + " succeeds");
    }

    std::map<std::string, std::set<std::string> > distinct;
    std::map<std::string, size_t> records;
    size_t restrictions_recorded = 0;
    size_t restrictions_required = 0;
    size_t stopped_after_parsing = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const Minimum *m = minimum_for(cases[i].family);
        if (m == nullptr)
            continue;
        Run run;
        run_case(cases[i], &run);
        judge(cases[i], run, &faults);
        records[cases[i].family] += 1;
        distinct[cases[i].family].insert(signature_of(cases[i], run));
        restrictions_recorded += run.restrictions_recorded;
        if (cases[i].has("restriction"))
            ++restrictions_required;
        // An input that never parsed says nothing about STEP-005, which is about an engine that
        // started work and stopped, so the count below leaves those out.
        if (*m->success_outcome == '\0' && run.could_not_run.empty() && run.outcome != "not parsed")
            ++stopped_after_parsing;
    }

    std::ofstream report(report_path.c_str());
    if (!report) {
        std::cout << "acceptance corpus: could not write " << report_path << "\n";
        return 1;
    }
    report << "# MVP acceptance corpus\n\n";
    report << "Generated by tools/acceptance_corpus.cc from tests/corpus. Do not edit.\n\n";
    report << "PRD section 22.1 counts minimum distinct semantic cases and excludes cosmetic "
              "variants with different numbers, so the column that gates is the distinct one. A "
              "case's signature is its family, the canonical shape of its input with every literal "
              "reduced to a class, the outcome the engine reached, and the class of the result. Two "
              "cases that differ only in which numbers were chosen share a signature and count "
              "once.\n\n";
    report << "| Family | Minimum | Distinct cases | Records | Short by |\n|---|---:|---:|---:|---:|\n";
    size_t short_families = 0;
    for (size_t i = 0; i < sizeof(kMinimums) / sizeof(kMinimums[0]); ++i) {
        const Minimum &m = kMinimums[i];
        const size_t have = distinct[m.family].size();
        const size_t missing = have >= m.cases ? 0 : m.cases - have;
        if (missing)
            ++short_families;
        report << "| " << m.family << " | " << count_text(m.cases) << " | " << count_text(have)
               << " | " << count_text(records[m.family]) << " | "
               << (missing ? count_text(missing) : std::string("met")) << " |\n";
    }

    report << "\n## Section 20.2 invariants\n\n";
    report << "Five of the MVP criteria are claims about every displayed step, so none of them can "
              "be evidenced by a test that builds one record. This is the pass over every "
              "derivation the corpus above produced.\n\n";
    report << "| Criterion | Claim | Looked at | Broke it |\n|---|---|---:|---:|\n";
    for (size_t i = 0; i < g_pass.claims().size(); ++i) {
        const nps::invariants::Claim &c = g_pass.claims()[i];
        if (c.requirement)
            continue;
        report << "| " << c.id << " | " << c.claim << " | " << count_text(c.checked) << " | "
               << count_text(c.broken) << " |\n";
    }

    report << "\n## Requirements checked over every derivation\n\n";
    report << "The same pass, applied to PRD requirements that are also claims about every step "
              "rather than about one solve.\n\n";
    report << "| Requirement | Claim | Looked at | Broke it |\n|---|---|---:|---:|\n";
    for (size_t i = 0; i < g_pass.claims().size(); ++i) {
        const nps::invariants::Claim &c = g_pass.claims()[i];
        if (!c.requirement)
            continue;
        report << "| " << c.id << " | " << c.claim << " | " << count_text(c.checked) << " | "
               << count_text(c.broken) << " |\n";
    }

    for (size_t i = 0; i < g_pass.claims().size(); ++i) {
        if (g_pass.claims()[i].checked == 0)
            report << "\nCriterion " << g_pass.claims()[i].id
                   << " had nothing to look at in this corpus, so it is unevidenced rather than "
                      "met.\n";
    }

    // VER-011 and STEP-004 both want the strength of the evidence recorded beside its method. It is
    // a gate now that the count has reached zero, which is the point at which a red build means
    // something: a passing verification with no classification is a new site that skipped it.
    if (g_pass.unclassified_verifications() > 0) {
        faults.push_back(count_text(g_pass.unclassified_verifications()) +
                         " passing verifications do not name what their evidence is worth");
    }
    report << "\n## Evidence strength recorded\n\n"
           << count_text(g_pass.passing_verifications() - g_pass.unclassified_verifications())
           << " of " << count_text(g_pass.passing_verifications())
           << " passing verifications name what their evidence is worth. Anything left on the "
              "Unsupported default is a fault above rather than a number here.\n";

    // VER-016 wants the schema declared for every rule and strategy. It is a gate now that the
    // count has reached zero, and the table below stays because a rule that turns up undeclared
    // needs the material an entry is written from rather than only its name.
    const std::map<std::string, nps::invariants::Pass::Observed> &observed = g_pass.observations();
    if (g_pass.undeclared_rules() > 0) {
        faults.push_back(count_text(observed.size()) +
                         (observed.size() == 1 ? " rule does not" : " rules do not") +
                         " declare a proof-obligation schema");
    }
    // A step with no rule id cannot be asked which schema it answers to, so this is the same fault
    // one move earlier. It was 157 steps until the kinematics dimensional check was given the rule
    // id its sibling families all carry.
    if (g_pass.nameless_steps() > 0) {
        faults.push_back(count_text(g_pass.nameless_steps()) +
                         " steps name no rule, so no schema can be asked of them");
    }
    report << "\n## Proof-obligation schema declared\n\n"
           << count_text(g_pass.rules_seen() - g_pass.undeclared_rules()) << " of "
           << count_text(g_pass.rules_seen())
           << " recorded steps name a rule whose proof-obligation schema is declared, and every one "
              "of them names a rule.\n";
    if (!observed.empty()) {
        report << "\n" << count_text(observed.size())
               << " rules are still undeclared, listed with what this corpus saw them do.\n\n";
        report << "| Rule | Steps | Claim | Obligations raised | Evidence that passed | Status when "
                  "a check failed |\n|---|---:|---|---|---|---|\n";
        for (std::map<std::string, nps::invariants::Pass::Observed>::const_iterator it =
                 observed.begin();
             it != observed.end(); ++it) {
            report << "| " << it->first << " | " << count_text(it->second.steps) << " | "
                   << joined(it->second.claims) << " | " << joined(it->second.obligations) << " | "
                   << joined(it->second.evidence) << " | " << joined(it->second.statuses_on_failure)
                   << " |\n";
        }
    }

    report << "\n## Faults\n\n";
    if (faults.empty()) {
        report << "None.\n";
    } else {
        for (size_t i = 0; i < faults.size(); ++i)
            report << "- " << faults[i] << "\n";
    }
    report.close();

    for (size_t i = 0; i < faults.size(); ++i)
        std::cout << "acceptance corpus: " << faults[i] << "\n";

    // Not under --gate, because that run fails on purpose and would append a second copy. The
    // ordinary run is the right one anyway: it is the one that fails when a corpus case stops
    // matching, which is what makes the evidence below mean anything.
    bool evidence_refused = false;
    const char *evidence_path = gate ? nullptr : getenv("NPS_EVIDENCE");
    if (evidence_path != nullptr) {
        const nps::invariants::Claim *kinds = g_pass.find("3");
        // STEP-003 has two halves and needs both. The kind and its registered payload come from the
        // invariant pass, and the restrictions half needs a rule that actually carries one, which
        // is why a case can name the restriction it expects to survive.
        const bool step003 = kinds != nullptr && kinds->checked > 0 && kinds->broken == 0 &&
                             restrictions_required > 0 && restrictions_recorded > 0 &&
                             faults.empty();
        const nps::invariants::Claim *labels = g_pass.find("STEP-022");
        const bool step022 =
            labels != nullptr && labels->checked > 0 && labels->broken == 0 && faults.empty();
        const nps::invariants::Claim *fields = g_pass.find("STEP-002");
        const bool step002 =
            fields != nullptr && fields->checked > 0 && fields->broken == 0 && faults.empty();
        const nps::invariants::Claim *recognition = g_pass.find("STEP-021");
        const bool step021 = recognition != nullptr && recognition->checked > 0 &&
                             recognition->broken == 0 && faults.empty();
        const nps::invariants::Claim *implications = g_pass.find("STEP-007");
        // Gated on the zero as well as on the faults, because the zero is what the row's sentence
        // says. The day a corpus case first reaches an implication step this fails, which is the
        // point: the line would otherwise keep passing while the claim under it went stale, and a
        // reported sentence nothing checks is the failure this report exists to avoid.
        const bool step007 = implications != nullptr && implications->broken == 0 &&
                             g_pass.implication_steps() == 0 && faults.empty();
        const nps::invariants::Claim *schema = g_pass.find("VER-016");
        const bool ver016 = schema != nullptr && schema->checked > 0 && schema->broken == 0 &&
                            g_pass.undeclared_rules() == 0 && faults.empty();
        std::vector<std::string> rows;
        // VER-016 asks for four things per rule, and the check reads all four off the record: the
        // claim, the obligations raised, a verification of a declared method for each declared
        // obligation, and the strength that method is worth. The fourth part, what a rule does when
        // its obligation is not discharged, is checked as behaviour rather than as a label, so a
        // rule declaring that it withholds cannot fail its check and still report a solution.
        // Nothing in this corpus reaches either arm, because no derivation here fails a check and
        // claims an answer, so both are driven from a hand-built record in derivation_tests.cc.
        rows.push_back(evidence_row(
            "VER-016", ver016,
            "every one of " + count_text(schema ? schema->checked : 0) + " steps across " +
                count_text(cases.size()) +
                " corpus derivations matches the proof-obligation schema its rule declares, and no "
                "step names a rule that declares none"));
        // STEP-007 asks for a candidate check where an operation may introduce extraneous results,
        // which is a condition on the operations rather than an instruction to have them. What this
        // population says is the absence, and it is worth saying: across every case here no step
        // claims an implication at all, so no derivation widens a solution set and walks away. The
        // three operations the requirement names are refused before they run, linear.cc turning back
        // an unknown in a denominator and rearrange.cc an even power for want of branches. The count
        // is printed rather than gated on, because a zero here is the finding and reading it as
        // coverage would be the mistake: the population that does exercise the gate is the golden
        // fixtures, where catch-up and density check their candidates and one is rejected.
        rows.push_back(evidence_row(
            "STEP-007", step007,
            "no step in any of " + count_text(cases.size()) +
                " corpus derivations claims a one-way transformation of the solution set, because "
                "the operations that would introduce an extraneous root are refused rather than "
                "performed, so the corpus contains no candidate left unchecked"));
        // STEP-019 is deliberately not claimed from this population. No corpus case runs with a
        // backend, so the gate judges nothing here, and unlike the STEP-007 zero this one carries
        // no information: a run that never consulted the backend says nothing about whether a
        // backend answer could become a walkthrough. The golden fixtures reach six such steps and
        // relative_motion_tests drives the same problem under four different backends, which is
        // where the requirement is answered.
        rows.push_back(evidence_row(
            "STEP-021", step021,
            "every one of " + count_text(recognition ? recognition->checked : 0) +
                " transformation steps across " + count_text(cases.size()) +
                " corpus derivations says when to reach for its rule, in words that are not its "
                "short explanation repeated"));
        rows.push_back(evidence_row(
            "STEP-022", step022,
            "across " + count_text(labels ? labels->checked : 0) + " steps in " +
                count_text(cases.size()) +
                " corpus derivations, no rule name and no recorded action is the bare label "
                "simplify, solve, after algebra or by CAS"));
        rows.push_back(evidence_row(
            "STEP-002", step002,
            "every one of " + count_text(fields ? fields->checked : 0) +
                " transformation steps across " + count_text(cases.size()) +
                " corpus derivations carries a before state, an after state, a rule id, a readable "
                "rule name and a short explanation"));
        // STEP-005 wants the engine to stop and report rather than continue speculatively. Every
        // case in the invalid family is judged against criterion 11 (no verified-solution status,
        // no answer handed back) on top of its declared outcome, so a clean run over the ones that
        // reached an engine is what shows it.
        rows.push_back(evidence_row(
            "STEP-005", faults.empty() && stopped_after_parsing > 0,
            count_text(stopped_after_parsing) +
                " unsupported, unverifiable, resource-limited and adversarial inputs that reached an "
                "engine each stopped at a named non-success outcome, with no verified-solution "
                "status and no answer handed back"));
        // VER-011 asks for the claim type as well as the strength. A verification result's claim
        // type is the claim of the step it sits on, which criterion 3 and criterion 5 already hold
        // every step to, so the strength is what this adds.
        const bool classified =
            g_pass.passing_verifications() > 0 && g_pass.unclassified_verifications() == 0;
        rows.push_back(evidence_row(
            "VER-011", classified && faults.empty(),
            "every one of " + count_text(g_pass.passing_verifications()) +
                " passing verifications across " + count_text(cases.size()) +
                " corpus derivations names what its evidence is worth, under the step whose claim "
                "type it answers for"));
        rows.push_back(evidence_row(
            "STEP-004", g_pass.methodless_verifications() == 0 && faults.empty(),
            "and each of those " + count_text(g_pass.passing_verifications()) +
                " records names its validation method beside that strength, so a transformation is "
                "accepted on evidence that says both what was done and what it is worth"));
        rows.push_back(evidence_row(
            "STEP-003", step003,
            "every one of " + count_text(kinds ? kinds->checked : 0) +
                " steps across " + count_text(cases.size()) +
                " corpus derivations carries a kind and that kind's registered payload, and the "
                "rules that carry a domain restriction record it (" +
                count_text(restrictions_recorded) + " restrictions over " +
                count_text(restrictions_required) + " cases that require one)"));
        std::string error;
        // The unit run truncates this file and writes the adapter group into it, so an append that
        // lands first would be thrown away by the run it was waiting for.
        if (!append_evidence(evidence_path, "adapter", "acceptance corpus", rows, &error)) {
            evidence_refused = true;
            std::cout << "acceptance corpus: evidence not written, " << error << "\n";
        }
    }

    // A criterion with nothing to look at is not one that held. It counts with the broken ones, so
    // an empty corpus cannot report five invariants passing.
    size_t broken_criteria = 0;
    for (size_t i = 0; i < g_pass.claims().size(); ++i) {
        const nps::invariants::Claim &c = g_pass.claims()[i];
        if (c.requirement)
            continue;
        if (c.broken || c.checked == 0)
            ++broken_criteria;
    }
    const size_t section_criteria = g_pass.count(false);

    size_t total_distinct = 0;
    size_t total_minimum = 0;
    for (size_t i = 0; i < sizeof(kMinimums) / sizeof(kMinimums[0]); ++i) {
        total_distinct += distinct[kMinimums[i].family].size();
        total_minimum += kMinimums[i].cases;
    }
    std::cout << "acceptance corpus: " << count_text(cases.size()) << " cases, "
              << count_text(total_distinct) << " distinct of " << count_text(total_minimum)
              << " required, " << count_text(faults.size()) << " faults, "
              << count_text(short_families) << " of 5 families below the section 22.1 minimum, "
              << count_text(section_criteria - broken_criteria) << " of "
              << count_text(section_criteria) << " section 20.2 invariants held, "
              << "report in " << report_path << "\n";
    if (gate)
        return short_families == 0 ? 0 : 1;
    return faults.empty() && !evidence_refused ? 0 : 1;
}
