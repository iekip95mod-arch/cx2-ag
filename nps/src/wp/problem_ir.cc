#include "nps/wp/problem_ir.h"

#include <set>
#include <string_view>

#include "nps/units/units.h"
#include "sha256.h"

namespace nps::wp {
namespace {

// What a semantic type means: its dimension, and where each engine that accepts it puts it.
struct SemanticType {
    const char *name;
    int length, mass, time;
    const char *kinematics_symbol;
    int density_variable;
};

const SemanticType kSemanticTypes[] = {
    {"initial_velocity", 1, 0, -1, "v0", -1},
    {"final_velocity", 1, 0, -1, "v", -1},
    {"acceleration", 1, 0, -2, "a", -1},
    {"elapsed_time", 0, 0, 1, "t", -1},
    {"displacement", 1, 0, 0, "x", -1},
    {"mass", 0, 1, 0, nullptr, static_cast<int>(DensityVariable::Mass)},
    {"volume", 3, 0, 0, nullptr, static_cast<int>(DensityVariable::Volume)},
    {"density", -3, 1, 0, nullptr, static_cast<int>(DensityVariable::Density)},
};

const SemanticType *semantic_type(std::string_view name) {
    for (const SemanticType &type : kSemanticTypes) {
        if (name == type.name)
            return &type;
    }
    return nullptr;
}

// Which families accept which requested method, so a method the family cannot honour is refused.
bool method_allowed(const std::string &family, const std::string &method) {
    if (method.empty())
        return true;
    if (family == "physics.kinematics.constant-acceleration.one-dimension")
        return method == "constant-acceleration-equations";
    if (family == "physics.density.mass-volume")
        return method == "density-definition";
    return false;
}

IrValidation fail(IrFault fault, std::string detail) {
    return {fault, std::move(detail)};
}

}  // namespace

const char *ir_fault_name(IrFault fault) {
    switch (fault) {
        case IrFault::None: return "valid";
        case IrFault::DuplicateId: return "duplicate id";
        case IrFault::MissingReference: return "missing reference";
        case IrFault::KnownUnknownConflict: return "known and unknown conflict";
        case IrFault::UnconfirmedInference: return "unconfirmed inference";
        case IrFault::DimensionMismatch: return "dimension mismatch";
        case IrFault::UndefinedGoal: return "undefined goal";
        case IrFault::IncompatibleMethod: return "incompatible method";
        case IrFault::SourceMismatch: return "source mismatch";
        case IrFault::ProvenanceMismatch: return "provenance mismatch";
        case IrFault::MissingProvenance: return "missing provenance";
        case IrFault::NotConfirmed: return "not confirmed";
        case IrFault::ConfirmationMismatch: return "confirmation mismatch";
    }
    return "unknown";
}

std::string source_hash(const std::string &original_utf8) {
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, reinterpret_cast<const BYTE *>(original_utf8.data()), original_utf8.size());
    BYTE digest[SHA256_BLOCK_SIZE];
    sha256_final(&hash, digest);
    static const char kHex[] = "0123456789abcdef";
    std::string text;
    for (BYTE b : digest) {
        text += kHex[b >> 4];
        text += kHex[b & 15];
    }
    return text;
}

bool span_matches(const Span &span, const SourceDocument &source) {
    return span.original_begin <= span.original_end && span.original_end <= source.original_utf8.size() &&
           source.original_utf8.compare(span.original_begin, span.original_end - span.original_begin,
                                        span.surface) == 0 &&
           span.original_end - span.original_begin == span.surface.size();
}

bool semantic_type_info(const std::string &type, Dimension *dimension, std::string *kinematics_symbol) {
    const SemanticType *known = semantic_type(type);
    if (!known)
        return false;
    *dimension = Dimension();
    dimension->length = known->length;
    dimension->mass = known->mass;
    dimension->time = known->time;
    *kinematics_symbol = known->kinematics_symbol ? known->kinematics_symbol : "";
    return true;
}

IrValidation validate(const ProblemIR &ir, const SourceDocument &source) {
    if (ir.source_document_id != source.source_id)
        return fail(IrFault::SourceMismatch, "the problem names source " + ir.source_document_id + ", not " + source.source_id);
    if (ir.source_content_hash != source_hash(source.original_utf8) || source.original_content_hash != ir.source_content_hash)
        return fail(IrFault::SourceMismatch, "the source content hash does not match the saved text");

    std::set<std::string> ids;
    std::set<std::string> entities, occurrences, quantities, assumptions;
    const auto claim = [&ids](const std::string &id) { return !id.empty() && ids.insert(id).second; };
    for (const Entity &e : ir.entities) {
        if (!claim(e.id))
            return fail(IrFault::DuplicateId, "the id " + e.id + " is used twice or is empty");
        entities.insert(e.id);
    }
    for (const std::vector<Occurrence> *list : {&ir.events, &ir.states}) {
        for (const Occurrence &o : *list) {
            if (!claim(o.id))
                return fail(IrFault::DuplicateId, "the id " + o.id + " is used twice or is empty");
            if (!entities.count(o.entity_id))
                return fail(IrFault::MissingReference, o.id + " refers to the entity " + o.entity_id + ", which is not defined");
            occurrences.insert(o.id);
        }
    }
    for (const Quantity &q : ir.quantities) {
        if (!claim(q.id))
            return fail(IrFault::DuplicateId, "the id " + q.id + " is used twice or is empty");
        quantities.insert(q.id);
    }
    for (const std::vector<Assumption> *list : {&ir.explicit_assumptions, &ir.confirmed_inferred_assumptions}) {
        for (const Assumption &a : *list) {
            if (!claim(a.id))
                return fail(IrFault::DuplicateId, "the id " + a.id + " is used twice or is empty");
            assumptions.insert(a.id);
        }
    }
    for (const std::vector<Relation> *list : {&ir.relations, &ir.constraints}) {
        for (const Relation &r : *list) {
            if (!claim(r.id))
                return fail(IrFault::DuplicateId, "the id " + r.id + " is used twice or is empty");
            for (const std::string &operand : r.operands) {
                if (!quantities.count(operand) && !entities.count(operand) && !occurrences.count(operand))
                    return fail(IrFault::MissingReference, r.id + " refers to " + operand + ", which is not defined");
            }
        }
    }

    const auto provenance_ok = [&source](const Provenance &p, bool *explicit_without_span) {
        *explicit_without_span = p.explicit_fact && p.supporting_source_spans.empty();
        for (const Span &span : p.supporting_source_spans) {
            if (!span_matches(span, source))
                return false;
        }
        return p.source_id == source.source_id;
    };
    for (const Quantity &q : ir.quantities) {
        if (!q.owner_entity_id.empty() && !entities.count(q.owner_entity_id))
            return fail(IrFault::MissingReference, q.id + " is owned by " + q.owner_entity_id + ", which is not defined");
        if (!q.state_or_event_id.empty() && !occurrences.count(q.state_or_event_id))
            return fail(IrFault::MissingReference, q.id + " belongs to " + q.state_or_event_id + ", which is not defined");
        bool bare = false;
        if (!provenance_ok(q.provenance, &bare))
            return fail(IrFault::ProvenanceMismatch, q.id + " cites a span that does not match the source text");
        if (bare)
            return fail(IrFault::MissingProvenance, q.id + " is stated as explicit but cites no span");
        const SemanticType *type = semantic_type(q.semantic_type);
        if (!type)
            return fail(IrFault::DimensionMismatch, q.id + " has the unknown semantic type " + q.semantic_type);
        Dimension expected;
        expected.length = type->length;
        expected.mass = type->mass;
        expected.time = type->time;
        if (!q.unit.empty()) {
            nps::Quantity parsed;
            std::string why;
            if (!parse_quantity("1 " + q.unit, &parsed, &why) || parsed.unit.dimension != expected)
                return fail(IrFault::DimensionMismatch, q.id + " is a " + q.semantic_type + " but its unit " + q.unit +
                                                            " has dimension " + dimension_text(parsed.unit.dimension));
        }
    }

    for (const std::string &k : ir.knowns) {
        if (!quantities.count(k))
            return fail(IrFault::MissingReference, "the known " + k + " is not a defined quantity");
        for (const std::string &u : ir.unknowns) {
            if (k == u)
                return fail(IrFault::KnownUnknownConflict, k + " is listed as both known and unknown");
        }
    }
    for (const std::string &u : ir.unknowns) {
        if (!quantities.count(u))
            return fail(IrFault::MissingReference, "the unknown " + u + " is not a defined quantity");
    }
    for (const Quantity &q : ir.quantities) {
        bool listed = false;
        for (const std::string &k : ir.knowns)
            listed = listed || k == q.id;
        if (listed && q.value_expression.empty())
            return fail(IrFault::KnownUnknownConflict, q.id + " is known but has no value");
    }
    bool goal_is_unknown = false;
    for (const std::string &u : ir.unknowns)
        goal_is_unknown = goal_is_unknown || u == ir.requested_goal;
    if (!goal_is_unknown)
        return fail(IrFault::UndefinedGoal, "the requested goal " + ir.requested_goal + " is not one of the unknowns");
    for (const Assumption &a : ir.confirmed_inferred_assumptions) {
        if (a.confirmation_record_id.empty() || a.confirmation_record_id != ir.confirmation_record.id)
            return fail(IrFault::UnconfirmedInference, "the inferred assumption " + a.id + " has no confirmation record");
    }
    for (const Quantity &q : ir.quantities) {
        if (!q.provenance.explicit_fact && (q.provenance.confirmation_record_id.empty() ||
                                            q.provenance.confirmation_record_id != ir.confirmation_record.id))
            return fail(IrFault::UnconfirmedInference, "the inferred quantity " + q.id + " has no confirmation record");
    }
    const ConfirmationRecord &record = ir.confirmation_record;
    if ((!record.source_content_hash.empty() && record.source_content_hash != ir.source_content_hash) ||
        (!record.selected_candidate_id.empty() && record.selected_candidate_id != ir.selected_candidate_id) ||
        (record.problem_revision != 0 && record.problem_revision != ir.revision))
        return fail(IrFault::ConfirmationMismatch, "the confirmation " + record.id + " approved a different source, candidate or revision");
    const std::string family = ir.curriculum_family_ids.empty() ? std::string() : ir.curriculum_family_ids.front();
    if (!method_allowed(family, ir.requested_method))
        return fail(IrFault::IncompatibleMethod, "the method " + ir.requested_method + " is not one " + family + " offers");
    return {};
}

std::optional<CommittedProblem> commit(ProblemIR draft, const SourceDocument &source, IrValidation *why) {
    IrValidation result = validate(draft, source);
    if (result.ok() && (!draft.confirmation_record.confirmed || draft.confirmation_record.id.empty()))
        result = fail(IrFault::NotConfirmed, "a problem is solved only after its interpretation is confirmed");
    if (why)
        *why = result;
    if (!result.ok())
        return std::nullopt;
    draft.validation_record = "valid against schema " + std::to_string(draft.schema_version);
    return CommittedProblem(std::move(draft), source);
}

ProblemIR correct(const CommittedProblem &committed, const std::string &correction_id) {
    ProblemIR next = committed.ir();
    next.correction_lineage.push_back(next.problem_id + "@" + std::to_string(next.revision) + " by " + correction_id);
    ++next.revision;
    next.confirmation_record = ConfirmationRecord();
    next.validation_record.clear();
    return next;
}

const char *ir_read_status_name(IrReadStatus status) {
    switch (status) {
        case IrReadStatus::Ok: return "ok";
        case IrReadStatus::BadMagic: return "not a problem file";
        case IrReadStatus::NewerVersion: return "written by a newer schema";
        case IrReadStatus::UnsupportedVersion: return "unsupported schema version";
        case IrReadStatus::Malformed: return "malformed";
        case IrReadStatus::UnknownRecord: return "unknown record";
    }
    return "unknown";
}

namespace {

struct Fields {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::string> flags;
    const std::string *get(std::string_view key) const {
        for (const auto &[k, v] : pairs) {
            if (k == key)
                return &v;
        }
        return nullptr;
    }
    bool has(std::string_view flag) const {
        for (const std::string &f : flags) {
            if (f == flag)
                return true;
        }
        return false;
    }
};

// Space-separated key=value words, where a bare word is a flag. Values carry no spaces.
Fields split_fields(std::string_view text) {
    Fields out;
    size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ')
            ++at;
        size_t end = at;
        while (end < text.size() && text[end] != ' ')
            ++end;
        if (end > at) {
            const std::string_view word = text.substr(at, end - at);
            const size_t eq = word.find('=');
            if (eq == std::string_view::npos)
                out.flags.emplace_back(word);
            else
                out.pairs.emplace_back(std::string(word.substr(0, eq)), std::string(word.substr(eq + 1)));
        }
        at = end;
    }
    return out;
}

bool read_size(const std::string &text, size_t *out) {
    if (text.empty() || text.size() > 9)
        return false;
    size_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<size_t>(c - '0');
    }
    *out = value;
    return true;
}

// span=begin,end reads the surface from the original text, so a stale offset shows up at validation.
bool read_span(const Fields &f, const SourceDocument &source, Provenance *p) {
    const std::string *text = f.get("span");
    p->source_id = source.source_id;
    p->explicit_fact = !f.has("inferred");
    if (const std::string *rule = f.get("rule"))
        p->extraction_rule_or_packaged_model = *rule;
    if (!text)
        return true;
    const size_t comma = text->find(',');
    Span span;
    if (comma == std::string::npos || !read_size(text->substr(0, comma), &span.original_begin) ||
        !read_size(text->substr(comma + 1), &span.original_end) || span.original_begin > span.original_end)
        return false;
    span.normalized_begin = span.original_begin;
    span.normalized_end = span.original_end;
    if (const std::string *surface = f.get("surface"))
        span.surface = *surface;
    else if (span.original_end <= source.original_utf8.size())
        span.surface = source.original_utf8.substr(span.original_begin, span.original_end - span.original_begin);
    p->supporting_source_spans.push_back(span);
    return true;
}

std::string field(const Fields &f, std::string_view key) {
    const std::string *v = f.get(key);
    return v ? *v : std::string();
}

std::vector<std::string> comma_list(const std::string &text) {
    std::vector<std::string> items;
    for (size_t begin = 0; begin < text.size();) {
        size_t end = text.find(',', begin);
        if (end == std::string::npos)
            end = text.size();
        items.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return items;
}

}  // namespace

IrReadResult read_problem_ir(const std::string &text) {
    IrReadResult result;
    size_t at = 0;
    size_t line_number = 0;
    const auto next_line = [&text, &at, &line_number](std::string *out) {
        if (at >= text.size())
            return false;
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        *out = text.substr(at, end - at);
        at = end + 1;
        ++line_number;
        return true;
    };
    const auto refuse = [&result, &line_number](IrReadStatus status, std::string why) {
        result.status = status;
        result.detail = std::move(why);
        result.line = line_number;
        return result;
    };
    std::string line;
    if (!next_line(&line) || line.compare(0, 15, "nps-problem-ir ") != 0)
        return refuse(IrReadStatus::BadMagic, "the first line has to be nps-problem-ir and a version");
    size_t version = 0;
    if (!read_size(line.substr(15), &version))
        return refuse(IrReadStatus::Malformed, "the schema version is not a whole number");
    if (version > kProblemIrSchemaVersion)
        return refuse(IrReadStatus::NewerVersion, "schema " + std::to_string(version) + " is newer than this reader's " +
                                                      std::to_string(kProblemIrSchemaVersion));
    if (version != kProblemIrSchemaVersion)
        return refuse(IrReadStatus::UnsupportedVersion, "schema " + std::to_string(version) + " is not supported");
    ProblemIR &ir = result.ir;
    SourceDocument &source = result.source;
    ir.schema_version = static_cast<uint32_t>(version);
    while (next_line(&line)) {
        if (line.empty() || line[0] == '#')
            continue;
        const size_t space = line.find(' ');
        const std::string record = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (record == "text") {
            source.original_utf8 = rest;
            source.normalized_utf8 = rest;
            continue;
        }
        const Fields f = split_fields(rest);
        if (record == "source") {
            source.source_id = field(f, "id");
            source.source_kind = field(f, "kind");
            source.language = field(f, "language");
            source.normalization_profile_version = field(f, "profile");
            source.original_content_hash = field(f, "hash");
        } else if (record == "problem") {
            ir.problem_id = field(f, "id");
            size_t revision = 0;
            if (!read_size(field(f, "revision"), &revision) || revision == 0)
                return refuse(IrReadStatus::Malformed, "the revision has to be a positive whole number");
            ir.revision = static_cast<uint32_t>(revision);
            ir.source_document_id = source.source_id;
            ir.source_content_hash = source.original_content_hash;
            ir.domain = field(f, "domain");
            ir.curriculum_family_ids.push_back(field(f, "family"));
            ir.requested_goal = field(f, "goal");
            ir.requested_method = field(f, "method");
            ir.parser_build_id = "authored";
        } else if (record == "entity") {
            Entity e;
            e.id = field(f, "id");
            e.name = field(f, "name");
            if (!read_span(f, source, &e.provenance))
                return refuse(IrReadStatus::Malformed, "the span is not begin,end");
            ir.entities.push_back(e);
        } else if (record == "state" || record == "event") {
            Occurrence o;
            o.id = field(f, "id");
            o.entity_id = field(f, "entity");
            o.description = field(f, "description");
            if (!read_span(f, source, &o.provenance))
                return refuse(IrReadStatus::Malformed, "the span is not begin,end");
            (record == "state" ? ir.states : ir.events).push_back(o);
        } else if (record == "quantity") {
            Quantity q;
            q.id = field(f, "id");
            q.symbol = field(f, "symbol");
            q.semantic_type = field(f, "semantic");
            q.value_expression = field(f, "value");
            q.unit = field(f, "unit");
            q.owner_entity_id = field(f, "owner");
            q.state_or_event_id = field(f, "state");
            q.exactness = q.value_expression.find('.') == std::string::npos ? "exact" : "measured";
            q.provenance.confirmation_record_id = field(f, "confirmation");
            if (!read_span(f, source, &q.provenance))
                return refuse(IrReadStatus::Malformed, "the span is not begin,end");
            ir.quantities.push_back(q);
        } else if (record == "relation") {
            Relation r;
            r.id = field(f, "id");
            r.kind = field(f, "kind");
            r.operands = comma_list(field(f, "operands"));
            if (!read_span(f, source, &r.provenance))
                return refuse(IrReadStatus::Malformed, "the span is not begin,end");
            ir.relations.push_back(r);
        } else if (record == "known") {
            ir.knowns.push_back(rest);
        } else if (record == "unknown") {
            ir.unknowns.push_back(rest);
        } else if (record == "assumption") {
            Assumption a;
            a.id = field(f, "id");
            a.confirmation_record_id = field(f, "confirmation");
            const size_t said = rest.find(" says ");
            a.text = said == std::string::npos ? std::string() : rest.substr(said + 6);
            if (!read_span(f, source, &a.provenance))
                return refuse(IrReadStatus::Malformed, "the span is not begin,end");
            (a.provenance.explicit_fact ? ir.explicit_assumptions : ir.confirmed_inferred_assumptions).push_back(a);
        } else if (record == "unused") {
            Provenance p;
            if (!read_span(f, source, &p) || p.supporting_source_spans.empty())
                return refuse(IrReadStatus::Malformed, "unused information needs a span");
            ir.unused_information.push_back(p.supporting_source_spans.front());
        } else if (record == "confirmation") {
            ir.confirmation_record.id = field(f, "id");
            ir.confirmation_record.confirmed_by = field(f, "by");
            ir.confirmation_record.confirmed = field(f, "confirmed") == "yes";
            ir.confirmation_record.source_content_hash = field(f, "hash");
            ir.confirmation_record.selected_candidate_id = field(f, "candidate");
            ir.confirmation_record.parser_versions = field(f, "versions");
            if (f.get("revision")) {
                size_t revision = 0;
                if (!read_size(field(f, "revision"), &revision) || revision == 0)
                    return refuse(IrReadStatus::Malformed, "the confirmed revision has to be a positive whole number");
                ir.confirmation_record.problem_revision = static_cast<uint32_t>(revision);
            }
            ir.confirmation_record.material_assumption_ids = comma_list(field(f, "assumptions"));
        } else {
            return refuse(IrReadStatus::UnknownRecord, "the record " + record + " is not part of schema 1");
        }
    }
    result.status = IrReadStatus::Ok;
    return result;
}

namespace {

bool quantity_of(const Quantity &q, nps::Quantity *out, std::string *why) {
    return parse_quantity(q.value_expression + " " + q.unit, out, why);
}

}  // namespace

bool to_kinematics(const CommittedProblem &committed, KinematicsProblem *out, std::string *why) {
    const ProblemIR &ir = committed.ir();
    KinematicsProblem problem;
    for (const Quantity &q : ir.quantities) {
        const SemanticType *type = semantic_type(q.semantic_type);
        if (!type || !type->kinematics_symbol) {
            *why = q.id + " is not a one-dimensional kinematics quantity";
            return false;
        }
        if (q.id == ir.requested_goal) {
            problem.unknown = type->kinematics_symbol;
            continue;
        }
        bool known = false;
        for (const std::string &k : ir.knowns)
            known = known || k == q.id;
        if (!known)
            continue;
        Known k;
        k.symbol = type->kinematics_symbol;
        if (!quantity_of(q, &k.quantity, why))
            return false;
        problem.knowns.push_back(k);
    }
    *out = std::move(problem);
    return true;
}

bool to_density(const CommittedProblem &committed, DensityProblem *out, std::string *why) {
    const ProblemIR &ir = committed.ir();
    DensityProblem problem;
    for (const Quantity &q : ir.quantities) {
        const SemanticType *type = semantic_type(q.semantic_type);
        if (!type || type->density_variable < 0) {
            *why = q.id + " is not a mass, a volume or a density";
            return false;
        }
        const DensityVariable variable = static_cast<DensityVariable>(type->density_variable);
        if (q.id == ir.requested_goal) {
            problem.unknown = variable;
            continue;
        }
        bool known = false;
        for (const std::string &k : ir.knowns)
            known = known || k == q.id;
        if (!known)
            continue;
        DensityKnown k;
        k.variable = variable;
        if (!quantity_of(q, &k.quantity, why))
            return false;
        problem.knowns.push_back(k);
    }
    *out = std::move(problem);
    return true;
}

}  // namespace nps::wp
