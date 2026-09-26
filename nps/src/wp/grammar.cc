#include "nps/wp/grammar.h"

#include <iterator>
#include <string_view>

#include "nps/core/budgets.h"

namespace nps::wp {
namespace {

const char *const kRoles[] = {"initial_velocity", "final_velocity", "acceleration", "elapsed_time", "displacement"};

// The words just before a number that say which role it plays, longest spelling first.
struct Cue {
    const char *words;
    const char *role;
    bool negated;
};

const Cue kCues[] = {
    {"initial velocity of", "initial_velocity", false},
    {"initial speed of", "initial_velocity", false},
    {"final velocity of", "final_velocity", false},
    {"final speed of", "final_velocity", false},
    {"travelling at", "initial_velocity", false},
    {"traveling at", "initial_velocity", false},
    {"acceleration of", "acceleration", false},
    {"accelerating at", "acceleration", false},
    {"displacement of", "displacement", false},
    {"accelerates at", "acceleration", false},
    {"decelerates at", "acceleration", true},
    {"distance of", "displacement", false},
    {"speeds up at", "acceleration", false},
    {"slows down at", "acceleration", true},
    {"travels at", "initial_velocity", false},
    {"moving at", "initial_velocity", false},
    {"drives at", "initial_velocity", false},
    {"moves at", "initial_velocity", false},
    {"rolls at", "initial_velocity", false},
    {"speed of", "initial_velocity", false},
    {"reaching", "final_velocity", false},
    {"reaches", "final_velocity", false},
    {"travels", "displacement", false},
    {"covers", "displacement", false},
    {"during", "elapsed_time", false},
    {"after", "elapsed_time", false},
    {"moves", "displacement", false},
    {"for", "elapsed_time", false},
    {"in", "elapsed_time", false},
};

// Questions that name the requested unknown.
struct Goal {
    const char *words;
    const char *role;
};

const Goal kGoals[] = {
    {"what is its acceleration", "acceleration"},
    {"what was its initial speed", "initial_velocity"},
    {"what is its final speed", "final_velocity"},
    {"what acceleration", "acceleration"},
    {"how fast", "final_velocity"},
    {"how far", "displacement"},
    {"how long", "elapsed_time"},
};

const char *const kDeterminers[] = {"a", "an", "the"};
const char *const kChangeWords[] = {"speeds", "accelerates", "accelerating", "acceleration", "slows", "decelerates",
                                    "decelerating"};
const char *const kVerbs[] = {"moving", "travels", "travelling", "traveling", "moves", "drives", "runs", "rolls",
                              "speeds", "accelerates", "starts", "begins", "slows", "decelerates", "covers", "is",
                              "falls", "goes"};

struct Word {
    size_t begin;
    size_t end;
};

bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::vector<Word> words_in(const std::string &text, size_t begin, size_t end) {
    std::vector<Word> out;
    size_t i = begin;
    while (i < end) {
        if (!is_letter(text[i])) {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < end && (is_letter(text[i]) || text[i] == '\''))
            ++i;
        out.push_back({start, i});
    }
    return out;
}

std::string_view view(const std::string &text, const Word &w) {
    return std::string_view(text).substr(w.begin, w.end - w.begin);
}

bool in_list(std::string_view word, const char *const *list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (words_equal(word, list[i]))
            return true;
    }
    return false;
}

// How many of the phrase's words end the list at position end, or zero when they do not.
size_t suffix_match(const std::string &text, const std::vector<Word> &words, size_t end, std::string_view phrase) {
    std::vector<std::string_view> parts;
    for (size_t at = 0; at < phrase.size();) {
        size_t space = phrase.find(' ', at);
        if (space == std::string_view::npos)
            space = phrase.size();
        parts.push_back(phrase.substr(at, space - at));
        at = space + 1;
    }
    if (parts.size() > end)
        return 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!words_equal(view(text, words[end - parts.size() + i]), parts[i]))
            return 0;
    }
    return parts.size();
}

struct Sentence {
    size_t begin;
    size_t end;
    bool question;
};

std::vector<Sentence> sentences_of(const std::string &text) {
    std::vector<Sentence> out;
    size_t begin = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool boundary = c == '?' || c == '!' || (c == '.' && (i + 1 == text.size() || text[i + 1] == ' '));
        if (boundary) {
            out.push_back({begin, i + 1, c == '?'});
            begin = i + 1;
        }
    }
    if (begin < text.size())
        out.push_back({begin, text.size(), false});
    return out;
}

const Sentence *sentence_at(const std::vector<Sentence> &sentences, size_t at) {
    for (const Sentence &s : sentences) {
        if (at >= s.begin && at < s.end)
            return &s;
    }
    return nullptr;
}

bool role_dimension(const std::string &role, Dimension *dimension, std::string *symbol) {
    return semantic_type_info(role, dimension, symbol);
}

std::string negate(const std::string &value) {
    return value.empty() || value[0] != '-' ? "-" + value : value.substr(1);
}

std::string at_text(const Span &span) {
    return std::to_string(span.original_begin) + ".." + std::to_string(span.original_end);
}

Provenance provenance(const SourceDocument &source, const Span &span, const std::string &rule, bool inferred) {
    Provenance p;
    p.source_id = source.source_id;
    p.extraction_rule_or_packaged_model = rule;
    p.explicit_fact = !inferred;
    p.candidate_id = "grammar-1";
    if (!inferred && span.original_end > span.original_begin)
        p.supporting_source_spans.push_back(span);
    return p;
}

// The proposal for one set of roles. Rerun at confirmation so an answered clarification is checked
// exactly as an assignment the grammar made itself.
std::optional<ProblemIR> assemble(const GrammarResult &r, const std::vector<RoleAssignment> &roles, std::string *why) {
    const SourceDocument &source = r.lexical.source;
    ProblemIR ir;
    ir.problem_id = source.source_id;
    ir.source_document_id = source.source_id;
    ir.source_content_hash = source.original_content_hash;
    ir.selected_candidate_id = "grammar-1";
    ir.parser_build_id = application_version();
    ir.grammar_module_versions = {kGrammarVersion, kLexiconVersion};
    ir.domain = "physics";
    ir.curriculum_family_ids = {kMotionFamily};
    ir.entities.push_back({"body", r.entity, provenance(source, r.entity_span, "determiner-noun-verb", false)});
    ir.unused_information = r.unused;

    const auto add = [&](const std::string &role, const std::string &value, const std::string &unit, const Span &span,
                         const std::string &rule, bool inferred) {
        Dimension dimension;
        std::string symbol;
        role_dimension(role, &dimension, &symbol);
        for (const Quantity &q : ir.quantities) {
            if (q.semantic_type == role) {
                *why = "two values are given for the " + role;
                return false;
            }
        }
        Quantity q;
        q.id = "q-" + symbol;
        q.symbol = symbol;
        q.semantic_type = role;
        q.value_expression = value;
        q.unit = unit;
        q.owner_entity_id = "body";
        q.exactness = value.find('.') == std::string::npos ? "exact" : "measured";
        q.provenance = provenance(source, span, rule, inferred);
        ir.quantities.push_back(q);
        if (!value.empty())
            ir.knowns.push_back(q.id);
        return true;
    };
    for (const RoleAssignment &a : roles) {
        const GroundedQuantity &g = r.lexical.quantities[a.quantity];
        if (a.role == "unused") {
            ir.unused_information.push_back(g.span);
            continue;
        }
        if (a.role.empty()) {
            *why = "the role of " + g.span.surface + " is not settled";
            return std::nullopt;
        }
        if (a.role == r.goal_role) {
            *why = "the " + a.role + " is asked for and also given";
            return std::nullopt;
        }
        if (!add(a.role, a.negated ? negate(g.value_text) : g.value_text, g.unit_text, g.span, a.production, false))
            return std::nullopt;
    }
    bool zero_acceleration = false;
    for (const ImpliedQuantity &i : r.implied) {
        if (i.role == r.goal_role) {
            *why = "the " + i.role + " is asked for and also given";
            return std::nullopt;
        }
        if (!add(i.role, i.value, i.unit, i.span, i.production, i.inferred))
            return std::nullopt;
        zero_acceleration = zero_acceleration || i.role == "acceleration";
    }
    bool acceleration = zero_acceleration;
    for (const Quantity &q : ir.quantities)
        acceleration = acceleration || q.semantic_type == "acceleration";
    if (!acceleration && !r.acceleration_mentioned && ir.knowns.size() < 3) {
        // Rate and distance: with no acceleration named, the motion is uniform, which the user confirms.
        add("acceleration", "0", "m/s^2", Span(), "uniform-motion", true);
        zero_acceleration = true;
    }
    if (ir.knowns.size() < 3) {
        *why = "three of the initial velocity, final velocity, acceleration, time and displacement are needed and " +
               std::to_string(ir.knowns.size()) + " are given";
        return std::nullopt;
    }
    Dimension dimension;
    std::string symbol;
    role_dimension(r.goal_role, &dimension, &symbol);
    Quantity goal;
    goal.id = "q-" + symbol;
    goal.symbol = symbol;
    goal.semantic_type = r.goal_role;
    goal.unit = si_unit_text(dimension);
    goal.owner_entity_id = "body";
    goal.provenance = provenance(source, r.goal_span, "question", false);
    ir.quantities.push_back(goal);
    ir.unknowns.push_back(goal.id);
    ir.requested_goal = goal.id;
    if (!r.stated_constant_acceleration.empty()) {
        ir.explicit_assumptions.push_back({"a-constant", "the acceleration stays constant", "",
                                           provenance(source, r.stated_constant_acceleration.front(), "stated", false)});
    } else if (!zero_acceleration) {
        ir.confirmed_inferred_assumptions.push_back(
            {"a-constant", "the acceleration stays constant for the whole motion", "",
             provenance(source, Span(), "constant-acceleration", true)});
    }
    return ir;
}

GrammarResult &finish(GrammarResult &r, GrammarOutcome outcome, std::string detail) {
    r.outcome = outcome;
    r.detail = std::move(detail);
    r.set.completion_status = grammar_outcome_name(outcome);
    if (!r.set.candidates.empty()) {
        InterpretationCandidate &c = r.set.candidates.front();
        c.candidate_id = "grammar-1";
        if (outcome == GrammarOutcome::Interpreted || outcome == GrammarOutcome::NeedsClarification)
            c.proposed_problem_model = kMotionFamily;
        if (outcome == GrammarOutcome::Unsupported && !r.detail.empty())
            c.contradictions.push_back(r.detail);
        for (const Clarification &q : r.clarifications)
            c.required_clarifications.push_back(q.id + ": " + q.question);
        for (const RoleAssignment &a : r.roles) {
            if (!a.role.empty())
                c.grounded_items.push_back(a.role + " at " + at_text(r.lexical.quantities[a.quantity].span) + " by " +
                                           a.production);
        }
        if (r.draft) {
            for (const Quantity &q : r.draft->quantities) {
                if (!q.provenance.explicit_fact)
                    c.inferred_items.push_back(q.semantic_type + " " + q.value_expression + " " + q.unit + " by " +
                                               q.provenance.extraction_rule_or_packaged_model);
            }
            for (const Assumption &a : r.draft->confirmed_inferred_assumptions)
                c.inferred_items.push_back(a.text);
        }
    }
    return r;
}

}  // namespace

const char *grammar_outcome_name(GrammarOutcome outcome) {
    switch (outcome) {
        case GrammarOutcome::Interpreted: return "interpreted";
        case GrammarOutcome::NeedsClarification: return "needs clarification";
        case GrammarOutcome::Unsupported: return "unsupported";
        case GrammarOutcome::ResourceExhausted: return "resource exhausted";
        case GrammarOutcome::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char *confirm_status_name(ConfirmStatus status) {
    switch (status) {
        case ConfirmStatus::Confirmed: return "confirmed";
        case ConfirmStatus::NeedsClarification: return "needs clarification";
        case ConfirmStatus::InvalidAnswer: return "invalid answer";
        case ConfirmStatus::SourceChanged: return "source changed";
        case ConfirmStatus::Rejected: return "rejected";
    }
    return "unknown";
}

GrammarResult interpret_motion(const std::string &source_id, const std::string &text, const LexicalLimits &limits) {
    GrammarResult r;
    r.lexical = read_lexical(source_id, text, limits);
    r.set = lexical_interpretation(r.lexical);
    r.set.grammar_module_versions = {kGrammarVersion};
    if (r.lexical.status == LexStatus::ResourceExhausted)
        return finish(r, GrammarOutcome::ResourceExhausted, r.lexical.detail);
    if (r.lexical.status == LexStatus::Cancelled)
        return finish(r, GrammarOutcome::Cancelled, r.lexical.detail);
    if (r.lexical.status == LexStatus::Unsupported)
        return finish(r, GrammarOutcome::Unsupported,
                      std::string("the reader refused ") + lex_fault_name(r.lexical.failures.front().fault) + " at " +
                          at_text(r.lexical.failures.front().span));
    const SourceDocument &source = r.lexical.source;
    const std::string &norm = source.normalized_utf8;
    const std::vector<Sentence> sentences = sentences_of(norm);
    if (sentences.empty())
        return finish(r, GrammarOutcome::Unsupported, "there is no text to read");

    const std::vector<Word> first = words_in(norm, sentences.front().begin, sentences.front().end);
    size_t verb = 0;
    for (size_t i = 1; i < first.size() && verb == 0; ++i) {
        if (in_list(view(norm, first[i]), kVerbs, std::size(kVerbs)))
            verb = i;
    }
    if (first.empty() || !in_list(view(norm, first[0]), kDeterminers, std::size(kDeterminers)) || verb < 2)
        return finish(r, GrammarOutcome::Unsupported, "the first sentence does not name one moving body before its verb");
    r.entity = std::string(view(norm, first[verb - 1]));
    r.entity_span = span_from_normalized(source, first[verb - 1].begin, first[verb - 1].end);
    for (size_t i = 1; i + 1 < verb; ++i)
        r.unused.push_back(span_from_normalized(source, first[i].begin, first[i].end));

    for (const Word &w : words_in(norm, 0, norm.size()))
        r.acceleration_mentioned = r.acceleration_mentioned || in_list(view(norm, w), kChangeWords, std::size(kChangeWords));
    bool opposes_consumed = false;
    for (size_t k = 0; k < r.lexical.quantities.size(); ++k) {
        const GroundedQuantity &g = r.lexical.quantities[k];
        const Sentence *s = sentence_at(sentences, g.span.normalized_begin);
        const std::vector<Word> before = words_in(norm, s ? s->begin : 0, g.span.normalized_begin);
        const Cue *cue = nullptr;
        for (const Cue &c : kCues) {
            if (suffix_match(norm, before, before.size(), c.words) > 0) {
                cue = &c;
                break;
            }
        }
        const Dimension &dimension = g.quantity.unit.dimension;
        if (cue) {
            Dimension wanted;
            std::string symbol;
            role_dimension(cue->role, &wanted, &symbol);
            if (wanted != dimension)
                return finish(r, GrammarOutcome::Unsupported,
                              std::string("the words \"") + cue->words + "\" ask for the " + cue->role + " but " +
                                  g.span.surface + " has dimension " + dimension_text(dimension));
            std::string production(cue->words);
            for (char &c : production)
                c = c == ' ' ? '-' : c;
            r.roles.push_back({k, cue->role, production, cue->negated});
            opposes_consumed = opposes_consumed || cue->negated;
            continue;
        }
        std::vector<std::string> options;
        for (const char *role : kRoles) {
            Dimension wanted;
            std::string symbol;
            role_dimension(role, &wanted, &symbol);
            if (wanted == dimension)
                options.push_back(role);
        }
        if (options.empty()) {
            r.roles.push_back({k, "unused", "no-motion-role", false});
        } else if (options.size() == 1 && options.front() != "displacement") {
            // Only a time and an acceleration have one role and no incidental reading along a line.
            r.roles.push_back({k, options.front(), "by-dimension", false});
        } else {
            options.push_back("unused");
            std::string question = "is " + g.span.surface + " the ";
            for (size_t i = 0; i < options.size(); ++i)
                question += (i == 0 ? "" : i + 1 == options.size() ? " or " : ", ") + options[i];
            r.clarifications.push_back({"c" + std::to_string(r.clarifications.size() + 1), k, question + "?", options,
                                        g.span});
            r.roles.push_back({k, "", "", false});
        }
    }

    for (const LexiconConcept &c : r.lexical.concepts) {
        if (c.concept_id == "initial_speed_equals_zero")
            r.implied.push_back({"initial_velocity", "0", "m/s", "from-rest", false, c.span});
        else if (c.concept_id == "final_speed_equals_zero")
            r.implied.push_back({"final_velocity", "0", "m/s", "comes-to-rest", false, c.span});
        else if (c.concept_id == "zero_acceleration")
            r.implied.push_back({"acceleration", "0", "m/s^2", "constant-speed", false, c.span});
        else if (c.concept_id == "constant_acceleration")
            r.stated_constant_acceleration.push_back(c.span);
        else if (c.concept_id == "acceleration_opposes_velocity" && opposes_consumed)
            continue;
        else
            return finish(r, GrammarOutcome::Unsupported,
                          "the phrase \"" + c.span.surface + "\" is outside the motion grammar");
    }

    for (const Sentence &s : sentences) {
        if (!s.question)
            continue;
        const std::vector<Word> words = words_in(norm, s.begin, s.end);
        for (size_t end = 1; end <= words.size(); ++end) {
            for (const Goal &goal : kGoals) {
                const size_t n = suffix_match(norm, words, end, goal.words);
                if (n == 0)
                    continue;
                if (!r.goal_role.empty() && r.goal_role != goal.role)
                    return finish(r, GrammarOutcome::Unsupported, "the question asks for more than one unknown");
                r.goal_role = goal.role;
                r.goal_span = span_from_normalized(source, words[end - n].begin, words[end - 1].end);
                break;
            }
        }
    }
    if (r.goal_role.empty())
        return finish(r, GrammarOutcome::Unsupported, "no question names the unknown");
    if (!r.clarifications.empty())
        return finish(r, GrammarOutcome::NeedsClarification, std::to_string(r.clarifications.size()) + " to settle");
    std::string why;
    r.draft = assemble(r, r.roles, &why);
    if (!r.draft)
        return finish(r, GrammarOutcome::Unsupported, why);
    return finish(r, GrammarOutcome::Interpreted, "");
}

ConfirmResult confirm_motion(const GrammarResult &interpreted, const std::string &current_text,
                             const std::vector<ClarificationAnswer> &answers, const std::string &confirmed_by) {
    ConfirmResult out;
    if (interpreted.outcome != GrammarOutcome::Interpreted && interpreted.outcome != GrammarOutcome::NeedsClarification) {
        out.detail = "there is no interpretation to confirm: " + interpreted.detail;
        return out;
    }
    if (source_hash(current_text) != interpreted.lexical.source.original_content_hash) {
        out.status = ConfirmStatus::SourceChanged;
        out.detail = "the text changed after it was read, so it has to be read again";
        return out;
    }
    std::vector<RoleAssignment> roles = interpreted.roles;
    for (const ClarificationAnswer &a : answers) {
        const Clarification *asked = nullptr;
        for (const Clarification &c : interpreted.clarifications) {
            if (c.id == a.clarification_id)
                asked = &c;
        }
        bool offered = false;
        for (const std::string &option : asked ? asked->options : std::vector<std::string>())
            offered = offered || option == a.option;
        if (!offered) {
            out.status = ConfirmStatus::InvalidAnswer;
            out.detail = a.clarification_id + " was not asked or does not offer " + a.option;
            return out;
        }
        for (RoleAssignment &role : roles) {
            if (role.quantity == asked->quantity) {
                role.role = a.option;
                role.production = "answered-" + asked->id;
            }
        }
    }
    for (const RoleAssignment &role : roles) {
        if (role.role.empty()) {
            out.status = ConfirmStatus::NeedsClarification;
            out.detail = "the role of " + interpreted.lexical.quantities[role.quantity].span.surface + " is still open";
            return out;
        }
    }
    std::string why;
    // Rebuilt from the grounded roles rather than taken from the stored draft, which nothing vouches for.
    std::optional<ProblemIR> draft = assemble(interpreted, roles, &why);
    if (!draft) {
        out.detail = why;
        return out;
    }
    ProblemIR ir = *draft;
    ConfirmationRecord &record = ir.confirmation_record;
    record.id = "c-" + ir.problem_id;
    record.confirmed_by = confirmed_by;
    record.confirmed = true;
    record.source_content_hash = ir.source_content_hash;
    record.selected_candidate_id = ir.selected_candidate_id;
    record.problem_revision = ir.revision;
    record.parser_versions = std::string(kGrammarVersion) + "+" + kLexiconVersion;
    for (Quantity &q : ir.quantities) {
        if (!q.provenance.explicit_fact) {
            q.provenance.confirmation_record_id = record.id;
            record.material_assumption_ids.push_back(q.id);
        }
    }
    for (Assumption &a : ir.confirmed_inferred_assumptions) {
        a.confirmation_record_id = record.id;
        record.material_assumption_ids.push_back(a.id);
    }
    out.committed = commit(std::move(ir), interpreted.lexical.source, &out.validation);
    if (!out.committed) {
        out.detail = out.validation.detail;
        return out;
    }
    out.status = ConfirmStatus::Confirmed;
    return out;
}

std::string grammar_summary(const GrammarResult &r) {
    std::string out = std::string("outcome ") + grammar_outcome_name(r.outcome) + (r.detail.empty() ? "" : ": " + r.detail) + "\n";
    if (!r.entity.empty())
        out += "entity " + r.entity + " from \"" + r.entity_span.surface + "\" at " + at_text(r.entity_span) + "\n";
    for (const RoleAssignment &a : r.roles) {
        const GroundedQuantity &g = r.lexical.quantities[a.quantity];
        if (a.role == "unused")
            out += "unused quantity \"" + g.span.surface + "\" at " + at_text(g.span) + "\n";
        else if (!a.role.empty())
            out += "given " + a.role + " " + (a.negated ? negate(g.value_text) : g.value_text) +
                   (g.unit_text.empty() ? "" : " " + g.unit_text) + " from \"" + g.span.surface + "\" at " +
                   at_text(g.span) + " by " + a.production + "\n";
    }
    for (const ImpliedQuantity &i : r.implied)
        out += "given " + i.role + " " + i.value + " " + i.unit + " from \"" + i.span.surface + "\" at " +
               at_text(i.span) + " by " + i.production + "\n";
    if (r.draft) {
        for (const Quantity &q : r.draft->quantities) {
            if (!q.provenance.explicit_fact)
                out += "inferred " + q.semantic_type + " " + q.value_expression + " " + q.unit + " by " +
                       q.provenance.extraction_rule_or_packaged_model + "\n";
        }
        for (const Assumption &a : r.draft->explicit_assumptions)
            out += "stated assumption " + a.text + " from \"" + a.provenance.supporting_source_spans.front().surface +
                   "\" at " + at_text(a.provenance.supporting_source_spans.front()) + "\n";
        for (const Assumption &a : r.draft->confirmed_inferred_assumptions)
            out += "inferred assumption " + a.text + "\n";
    }
    if (!r.goal_role.empty())
        out += "goal " + r.goal_role + " from \"" + r.goal_span.surface + "\" at " + at_text(r.goal_span) + "\n";
    for (const Clarification &c : r.clarifications)
        out += "clarify " + c.id + " " + c.question + "\n";
    for (const Span &s : r.unused)
        out += "unused \"" + s.surface + "\" at " + at_text(s) + "\n";
    return out;
}

}  // namespace nps::wp
