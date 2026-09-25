#include "nps/core/context.h"

#include "nps/core/capability_manifest.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

#include <charconv>
#include <utility>

namespace nps {

const char kContextUnknown[] = "<not recorded>";

namespace {

// Every value carries its own length, so a delimiter inside one is not a special case. The mark says
// which kind of value just ended, which is what catches a blob whose fields have drifted.
const char kStringMark = ':';
const char kListMark = '*';
const char kNumberMark = '#';
const char kHeaderMark = '\n';
const char kMagic[] = "scx";

// Nothing here is wider than the NodeId it holds, so a longer decimal is refused as it is read.
const uint64_t kWireCeiling = 0xFFFFFFFFull;

// What an engine that never touches units does with numbers. One that does states its own.
const char kBuildUnitPolicy[] = "exact integers, decimals as written, no unit conversion";

std::string recorded(const std::string &value) {
    return value.empty() ? std::string(kContextUnknown) : value;
}

void put_number(std::string *out, uint64_t value, char mark) {
    char digits[32];
    const auto conversion = std::to_chars(digits, digits + sizeof digits, value);
    if (conversion.ec != std::errc())
        return;
    out->append(digits, conversion.ptr);
    out->push_back(mark);
}

void put_text(std::string *out, const std::string &value) {
    put_number(out, value.size(), kStringMark);
    out->append(value);
}

void put_list(std::string *out, const std::vector<std::string> &values) {
    put_number(out, values.size(), kListMark);
    for (const std::string &value : values)
        put_text(out, value);
}

class Scanner {
  public:
    explicit Scanner(const std::string &blob) : blob_(blob) {}

    bool ok() const { return status_ == ContextStatus::Ok; }
    ContextStatus status() const { return status_; }
    size_t offset() const { return offset_; }
    size_t pos() const { return pos_; }
    bool at_end() const { return pos_ >= blob_.size(); }

    void fail(ContextStatus s) { fail_at(s, pos_); }

    void fail_at(ContextStatus s, size_t at) {
        if (status_ == ContextStatus::Ok) {
            status_ = s;
            offset_ = at;
        }
    }

    void magic() {
        for (size_t i = 0; kMagic[i] != '\0'; ++i) {
            if (at_end()) {
                fail(ContextStatus::Truncated);
                return;
            }
            if (blob_[pos_] != kMagic[i]) {
                fail(ContextStatus::BadMagic);
                return;
            }
            ++pos_;
        }
    }

    uint64_t number(char mark, uint64_t cap) {
        uint64_t value = digits(cap);
        expect(mark);
        return value;
    }

    std::string text() {
        uint64_t length = number(kStringMark, remaining());
        if (!ok())
            return std::string();
        if (length > remaining()) {
            fail(ContextStatus::Truncated);
            return std::string();
        }
        size_t start = pos_;
        pos_ += static_cast<size_t>(length);
        return blob_.substr(start, static_cast<size_t>(length));
    }

    std::vector<std::string> list() {
        std::vector<std::string> values;
        uint64_t count = number(kListMark, remaining());
        // An entry costs two bytes even when empty, so a larger count cannot be honoured.
        if (ok() && count > remaining() / 2)
            fail(ContextStatus::Truncated);
        for (uint64_t i = 0; ok() && i < count; ++i)
            values.push_back(text());
        return values;
    }

  private:
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    size_t remaining() const { return blob_.size() - pos_; }

    uint64_t digits(uint64_t cap) {
        if (!ok())
            return 0;
        if (at_end()) {
            fail(ContextStatus::Truncated);
            return 0;
        }
        size_t start = pos_;
        while (pos_ < blob_.size() && is_digit(blob_[pos_]))
            ++pos_;
        uint64_t value = 0;
        const auto conversion =
            std::from_chars(blob_.data() + start, blob_.data() + pos_, value);
        // Two spellings of one length would give one context two blobs, which is MVP criterion 14.
        if (pos_ == start || (pos_ - start > 1 && blob_[start] == '0') ||
            conversion.ec != std::errc() || conversion.ptr != blob_.data() + pos_ ||
            value > kWireCeiling || value > cap) {
            fail_at(ContextStatus::BadNumber, start);
            return 0;
        }
        return value;
    }

    void expect(char mark) {
        if (!ok())
            return;
        if (at_end()) {
            fail(ContextStatus::Truncated);
            return;
        }
        if (blob_[pos_] != mark) {
            fail(ContextStatus::BadFraming);
            return;
        }
        ++pos_;
    }

    const std::string &blob_;
    size_t pos_ = 0;
    ContextStatus status_ = ContextStatus::Ok;
    size_t offset_ = 0;
};

}  // namespace

const char *context_status_name(ContextStatus s) {
    switch (s) {
        case ContextStatus::Ok: return "ok";
        case ContextStatus::BadMagic: return "bad magic";
        case ContextStatus::UnknownVersion: return "unknown version";
        case ContextStatus::Truncated: return "truncated";
        case ContextStatus::BadNumber: return "bad number";
        case ContextStatus::BadFraming: return "bad framing";
        case ContextStatus::BadAst: return "bad normalized ast";
        case ContextStatus::BadStatus: return "bad status";
        case ContextStatus::BadNumericMode: return "bad numeric mode";
        case ContextStatus::TrailingBytes: return "trailing bytes";
    }
    return "unknown";
}

bool context_known(const std::string &field) {
    return !field.empty() && field != kContextUnknown;
}

#if NPS_FAMILY_CENSUS
std::set<std::string> &mutable_family_census() {
    static std::set<std::string> seen;
    return seen;
}

const std::set<std::string> &family_census() { return mutable_family_census(); }
#endif

SolutionContext make_context(const ContextInputs &inputs) {
    SolutionContext context;
    context.application_version = recorded(inputs.application_version);
    context.capability_manifest_id = capability_manifest_id();
    context.problem_family_id = recorded(inputs.problem_family_id);
#if NPS_FAMILY_CENSUS
    if (context_known(context.problem_family_id))
        mutable_family_census().insert(context.problem_family_id);
#endif
    // Nothing versions a problem family envelope either.
    context.problem_family_envelope_version = kContextUnknown;
    context.normalized_problem_model = inputs.normalized_problem_model;
    context.original_expression = recorded(inputs.original_expression);
    context.normalized_expression = recorded(inputs.normalized_expression);
    context.requested_method = recorded(inputs.requested_method);
    context.active_assumptions = inputs.active_assumptions;
    context.angle_convention = recorded(inputs.angle_convention);
    context.branch_convention = recorded(inputs.branch_convention);
    context.unit_policy = inputs.unit_policy.empty() ? kBuildUnitPolicy : inputs.unit_policy;
    context.detail_projection = recorded(inputs.detail_projection);
    context.resource_policy = recorded(inputs.resource_policy);
    // content_pack_versions stays empty: the rules are compiled in, so no pack is versioned alone.
    context.numeric_mode = inputs.numeric_mode;
    context.derivation_status = inputs.derivation_status;
    return context;
}

std::string serialize_context(const Arena &arena, const SolutionContext &context) {
    std::string normalized_expression = kContextUnknown;
    if (context.normalized_problem_model != kNoNode) {
        if (context.normalized_problem_model >= arena.node_count())
            return std::string();
        const std::string source_expression = print(arena, context.normalized_problem_model);
        if (source_expression.empty() || source_expression.size() > arena.limits().max_input_bytes ||
            (context_known(context.normalized_expression) &&
             context.normalized_expression != source_expression)) {
            return std::string();
        }
        Arena wire_arena(arena.limits());
        const ParseResult reparsed = parse(wire_arena, source_expression);
        if (!reparsed.ok())
            return std::string();
        normalized_expression = print(wire_arena, reparsed.root);
        if (normalized_expression.empty() ||
            normalized_expression.size() > arena.limits().max_input_bytes) {
            return std::string();
        }
    } else if (context_known(context.normalized_expression)) {
        return std::string();
    }

    std::string out(kMagic);
    put_number(&out, kContextFormatVersion, kHeaderMark);
    put_text(&out, context.application_version);
    put_text(&out, context.capability_manifest_id);
    put_text(&out, context.problem_family_id);
    put_text(&out, context.problem_family_envelope_version);
    // The v2 numeric slot is reserved because a NodeId has meaning only inside its source arena.
    put_number(&out, kNoNode, kNumberMark);
    put_text(&out, context.original_expression);
    put_text(&out, normalized_expression);
    put_text(&out, context.requested_method);
    put_list(&out, context.active_assumptions);
    put_text(&out, context.angle_convention);
    put_text(&out, context.branch_convention);
    put_text(&out, context.unit_policy);
    put_text(&out, context.detail_projection);
    put_text(&out, context.resource_policy);
    put_list(&out, context.content_pack_versions);
    put_number(&out, static_cast<uint64_t>(context.numeric_mode), kNumberMark);
    put_number(&out, static_cast<uint64_t>(context.derivation_status), kNumberMark);
    return out;
}

ContextParseResult parse_context(const std::string &blob, Arena &arena, SolutionContext *out) {
    Scanner scan(blob);
    scan.magic();
    size_t version_at = scan.pos();
    uint64_t version = scan.number(kHeaderMark, kWireCeiling);
    if (scan.ok() && version != 1 && version != 2 && version != kContextFormatVersion)
        scan.fail_at(ContextStatus::UnknownVersion, version_at);

    SolutionContext parsed;
    parsed.application_version = scan.text();
    parsed.capability_manifest_id = scan.text();
    parsed.problem_family_id = scan.text();
    parsed.problem_family_envelope_version = scan.text();
    const size_t model_at = scan.pos();
    const uint64_t serialized_model = scan.number(kNumberMark, kWireCeiling);
    parsed.normalized_problem_model = kNoNode;
    size_t normalized_expression_at = scan.pos();
    if (version >= 2) {
        parsed.original_expression = scan.text();
        parsed.normalized_expression = scan.text();
        normalized_expression_at = scan.pos() - parsed.normalized_expression.size();
    } else {
        parsed.original_expression = kContextUnknown;
        parsed.normalized_expression = kContextUnknown;
    }
    parsed.requested_method = scan.text();
    parsed.active_assumptions = scan.list();
    parsed.angle_convention = scan.text();
    parsed.branch_convention = scan.text();
    parsed.unit_policy = scan.text();
    parsed.detail_projection = scan.text();
    parsed.resource_policy = scan.text();
    parsed.content_pack_versions = scan.list();

    if (version >= 3) {
        size_t mode_at = scan.pos();
        uint64_t mode_value = scan.number(kNumberMark, kWireCeiling);
        if (scan.ok() && mode_value > static_cast<uint64_t>(NumericMode::Decimal))
            scan.fail_at(ContextStatus::BadNumericMode, mode_at);
        if (scan.ok())
            parsed.numeric_mode = static_cast<NumericMode>(mode_value);
    } else {
        // Every blob written before the mode existed came from a build that only did exact
        // arithmetic, so reading one as exact reports what it actually ran under.
        parsed.numeric_mode = NumericMode::Exact;
    }

    size_t status_at = scan.pos();
    uint64_t status_value = scan.number(kNumberMark, kWireCeiling);
    if (scan.ok() && !derivation_status_in_range(status_value))
        scan.fail_at(ContextStatus::BadStatus, status_at);
    if (scan.ok())
        parsed.derivation_status = static_cast<DerivationStatus>(status_value);

    if (scan.ok() && !scan.at_end())
        scan.fail(ContextStatus::TrailingBytes);

    if (scan.ok() && version >= 2 && serialized_model != kNoNode)
        scan.fail_at(ContextStatus::BadAst, model_at);

    ContextParseResult result;
    result.status = scan.status();
    result.offset = scan.offset();
    if (!result.ok())
        return result;

    if (version >= 2 && context_known(parsed.normalized_expression)) {
        if (arena.node_count() != 0 || arena.failed()) {
            result.status = ContextStatus::BadAst;
            result.offset = normalized_expression_at;
            return result;
        }
        Arena restored_arena(arena.limits());
        const ParseResult restored = parse(restored_arena, parsed.normalized_expression);
        if (!restored.ok() || print(restored_arena, restored.root) != parsed.normalized_expression) {
            result.status = ContextStatus::BadAst;
            result.offset = normalized_expression_at + restored.offset;
            return result;
        }
        parsed.normalized_problem_model = restored.root;
        arena = std::move(restored_arena);
    }

    *out = std::move(parsed);
    return result;
}

}  // namespace nps
