#ifndef NPS_CONTEXT_H
#define NPS_CONTEXT_H

#include <string>
#include <vector>
#if NPS_FAMILY_CENSUS
#include <set>
#endif

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"

namespace nps {

// A field this build does not record says so, because an empty string reads as a blank value.
extern const char kContextUnknown[];

bool context_known(const std::string &field);

// What a solve knows about itself. make_context supplies the rest, so two solves cannot disagree.
struct ContextInputs {
    std::string application_version;
    std::string problem_family_id;
    std::string requested_method;
    NodeId normalized_problem_model = kNoNode;
    std::string original_expression;
    std::string normalized_expression;
    std::vector<std::string> active_assumptions;
    std::string angle_convention;
    std::string branch_convention;
    // Left empty by an engine that does no conversion, which is what the build default describes.
    // An engine that does convert says so here rather than letting one build-wide string go stale.
    std::string unit_policy;
    std::string detail_projection;
    std::string resource_policy;
    NumericMode numeric_mode = NumericMode::Exact;
    DerivationStatus derivation_status = DerivationStatus::NotRecorded;
};

// Any input left empty becomes explicitly unknown rather than staying blank.
SolutionContext make_context(const ContextInputs &inputs);

#if NPS_FAMILY_CENSUS
// Host only, and it sits at make_context because that is the one point every family id passes.
const std::set<std::string> &family_census();
#endif

// PERF-007. The version is the schema: field order, count and kind are fixed per version.
const uint32_t kContextFormatVersion = 3;

// Returns empty if the normalized AST cannot be reconstructed within the arena limits.
std::string serialize_context(const Arena &arena, const SolutionContext &context);

enum class ContextStatus : uint8_t {
    Ok,
    BadMagic,
    UnknownVersion,
    Truncated,
    BadNumber,
    BadFraming,
    BadAst,
    BadStatus,
    BadNumericMode,
    TrailingBytes,
};

const char *context_status_name(ContextStatus s);

struct ContextParseResult {
    ContextStatus status = ContextStatus::BadMagic;
    // Byte the refusal was found at, so a corrupt save can be pointed at rather than just rejected.
    size_t offset = 0;

    bool ok() const { return status == ContextStatus::Ok; }
};

// A save is data, never code. The empty destination arena owns the restored normalized AST.
ContextParseResult parse_context(const std::string &blob, Arena &arena, SolutionContext *out);

}  // namespace nps

#endif
