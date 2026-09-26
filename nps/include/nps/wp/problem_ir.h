#ifndef NPS_WP_PROBLEM_IR_H
#define NPS_WP_PROBLEM_IR_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nps/physics/density.h"
#include "nps/physics/kinematics.h"

namespace nps::wp {

// PRD section 26.5. Word-problem Milestone 0 fixes these shapes and the commit boundary between
// them. Nothing in the tree produces an InterpretationSet yet, so every ProblemIR here is authored.
constexpr uint32_t kProblemIrSchemaVersion = 1;

// Half-open byte offsets into the original and normalized text, with the original surface text.
struct Span {
    size_t original_begin = 0;
    size_t original_end = 0;
    size_t normalized_begin = 0;
    size_t normalized_end = 0;
    std::string surface;
};

struct SourceDocument {
    std::string source_id;
    std::string original_utf8;
    std::string normalized_utf8;
    std::string language;
    std::string source_kind;
    std::string original_content_hash;
    std::string normalization_profile_version;
    std::vector<std::pair<size_t, size_t>> normalized_to_original_span_map;
};

struct Provenance {
    std::string source_id;
    std::vector<Span> supporting_source_spans;
    std::string extraction_rule_or_packaged_model;
    bool explicit_fact = true;
    std::string candidate_id;
    std::string user_correction_id;
    std::string confirmation_record_id;
};

struct Entity {
    std::string id;
    std::string name;
    Provenance provenance;
};

// An event or a state an entity passes through, which quantities attach to.
struct Occurrence {
    std::string id;
    std::string entity_id;
    std::string description;
    Provenance provenance;
};

struct Quantity {
    std::string id;
    std::string symbol;
    std::string semantic_type;
    std::string exactness;
    std::string value_expression;
    std::string unit;
    std::string dimensions;
    std::string owner_entity_id;
    std::string state_or_event_id;
    std::string coordinate_frame_id;
    std::string vector_or_scalar = "scalar";
    std::string component_or_magnitude = "magnitude";
    std::string sign_semantics;
    int significant_figures = 0;
    Provenance provenance;
};

struct Relation {
    std::string id;
    std::string kind;
    std::vector<std::string> operands;
    Provenance provenance;
};

struct Assumption {
    std::string id;
    std::string text;
    std::string confirmation_record_id;
    Provenance provenance;
};

// WP-043. What the user approved, bound to the source, candidate, revision and versions it was shown.
struct ConfirmationRecord {
    std::string id;
    std::string confirmed_by;
    bool confirmed = false;
    std::string source_content_hash;
    std::string selected_candidate_id;
    uint32_t problem_revision = 0;
    std::vector<std::string> material_assumption_ids;
    std::string parser_versions;
};

// Untrusted. A candidate's ranking evidence orders proposals and never authorizes solving.
struct InterpretationCandidate {
    std::string candidate_id;
    std::string proposed_problem_model;
    std::vector<std::string> grounded_items;
    std::vector<std::string> inferred_items;
    std::vector<std::string> unresolved_references;
    std::vector<std::string> required_clarifications;
    std::vector<std::string> contradictions;
    std::vector<Span> unsupported_spans;
    std::string ranking_evidence;
    std::vector<std::string> validation_results;
};

struct InterpretationSet {
    uint32_t schema_version = kProblemIrSchemaVersion;
    std::string interpretation_id;
    SourceDocument source_document;
    std::string parser_build_id;
    std::vector<std::string> grammar_module_versions;
    std::vector<std::string> lexicon_module_versions;
    std::vector<std::string> packaged_model_versions;
    std::vector<InterpretationCandidate> candidates;
    std::vector<std::string> global_diagnostics;
    std::string resource_usage;
    std::string completion_status;
};

struct ProblemIR {
    uint32_t schema_version = kProblemIrSchemaVersion;
    std::string problem_id;
    uint32_t revision = 1;
    std::string source_document_id;
    std::string source_content_hash;
    std::string selected_candidate_id;
    std::string parser_build_id;
    std::vector<std::string> grammar_module_versions;
    std::string domain;
    std::vector<std::string> curriculum_family_ids;
    std::vector<Entity> entities;
    std::vector<Occurrence> events;
    std::vector<Occurrence> states;
    std::vector<Quantity> quantities;
    std::vector<Relation> relations;
    std::vector<Relation> constraints;
    std::vector<std::string> coordinate_frames;
    std::vector<std::string> knowns;
    std::vector<std::string> unknowns;
    std::string requested_goal;
    std::string requested_method;
    std::vector<Assumption> explicit_assumptions;
    std::vector<Assumption> confirmed_inferred_assumptions;
    std::vector<Span> unused_information;
    std::string validation_record;
    ConfirmationRecord confirmation_record;
    std::vector<std::string> correction_lineage;
};

// One named failure per invariant the PRD lists before commit.
enum class IrFault : uint8_t {
    None,
    DuplicateId,
    MissingReference,
    KnownUnknownConflict,
    UnconfirmedInference,
    DimensionMismatch,
    UndefinedGoal,
    IncompatibleMethod,
    SourceMismatch,
    ProvenanceMismatch,
    MissingProvenance,
    NotConfirmed,
    ConfirmationMismatch,
};

const char *ir_fault_name(IrFault fault);

struct IrValidation {
    IrFault fault = IrFault::None;
    std::string detail;
    bool ok() const { return fault == IrFault::None; }
};

// The hex SHA-256 of the original text, which the IR's source hash has to match.
std::string source_hash(const std::string &original_utf8);

// The dimension a semantic type requires and the symbol the kinematics engine gives it, if any.
bool semantic_type_info(const std::string &type, Dimension *dimension, std::string *kinematics_symbol);

// True when the span's original offsets lie inside the source and cover exactly its surface text.
bool span_matches(const Span &span, const SourceDocument &source);

IrValidation validate(const ProblemIR &ir, const SourceDocument &source);

// A ProblemIR that passed validation with a confirmation. It cannot be edited, only corrected.
class CommittedProblem {
  public:
    const ProblemIR &ir() const { return ir_; }
    const SourceDocument &source() const { return source_; }

  private:
    CommittedProblem(ProblemIR ir, SourceDocument source) : ir_(std::move(ir)), source_(std::move(source)) {}
    ProblemIR ir_;
    SourceDocument source_;
    friend std::optional<CommittedProblem> commit(ProblemIR draft, const SourceDocument &source,
                                                  IrValidation *why);
};

std::optional<CommittedProblem> commit(ProblemIR draft, const SourceDocument &source, IrValidation *why);

// A correction is a new revision with the old one in its lineage and no confirmation until given again.
ProblemIR correct(const CommittedProblem &committed, const std::string &correction_id);

enum class IrReadStatus : uint8_t {
    Ok,
    BadMagic,
    NewerVersion,
    UnsupportedVersion,
    Malformed,
    UnknownRecord,
};

const char *ir_read_status_name(IrReadStatus status);

struct IrReadResult {
    IrReadStatus status = IrReadStatus::Malformed;
    std::string detail;
    size_t line = 0;
    ProblemIR ir;
    SourceDocument source;
};

IrReadResult read_problem_ir(const std::string &text);

// The structured problems an existing engine already solves, built from a committed IR.
bool to_kinematics(const CommittedProblem &committed, KinematicsProblem *out, std::string *why);
bool to_density(const CommittedProblem &committed, DensityProblem *out, std::string *why);

}  // namespace nps::wp

#endif
