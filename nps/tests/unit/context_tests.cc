#include <iterator>
#include <string>
#include <vector>

#include "nps/core/budgets.h"
#include "nps/core/canonical.h"
#include "nps/core/capability_manifest.h"
#include "nps/core/context.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/physics/kinematics.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/integrate.h"
#include "nps/steps/linear.h"
#include "golden/golden.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

// Every byte the format gives a meaning to, so splitting on them instead of counting fails here.
const char kDelimiters[] = "0:1*2#3\n4scx";

const char kPlainBlobV1[] = "scx1\n1:a1:b1:c1:d7#1:e0*1:f1:g1:h1:i1:j1*1:k1#";
const char kPlainBlobV2[] =
    "scx2\n1:a1:b1:c1:d4294967295#1:l1:m1:e0*1:f1:g1:h1:i1:j1*1:k1#";
const char kPlainBlobV3[] =
    "scx3\n1:a1:b1:c1:d4294967295#1:l1:m1:e0*1:f1:g1:h1:i1:j1*1:k0#1#";
// The same context solved in decimal mode. One byte apart from the blob above, which is the point:
// a replay that ignored the mode would read this back as the exact derivation.
const char kDecimalBlobV3[] =
    "scx3\n1:a1:b1:c1:d4294967295#1:l1:m1:e0*1:f1:g1:h1:i1:j1*1:k1#1#";

const InstalledModule kExpectedModules[] = {
    {"solver", "algebra.linear-equation.one-unknown"},
    {"solver", "algebra.quadratic.pure-square.one-unknown"},
    {"solver", "algebra.formula-rearrangement.single-occurrence"},
    {"solver", "algebra.polynomial-rewrite.single-expression"},
    {"solver", "number.integer-method.literal"},
    {"solver", "matrix.ref.rational"},
    {"solver", "matrix.rref.rational"},
    {"solver", "matrix.det.rational"},
    {"solver", "calculus.derivative.single-variable"},
    {"solver", "calculus.integral.indefinite.single-variable"},
    {"solver", "calculus.integral.definite.single-variable"},
    {"solver", "calculus.limit.single-variable"},
    {"solver", "calculus.tangent-line.single-variable"},
    {"solver", "calculus.linearization.single-variable"},
    {"solver", "physics.kinematics.constant-acceleration.one-dimension"},
    {"solver", "physics.kinematics.constant-acceleration.projectile.two-dimension"},
    {"solver", "physics.kinematics.constant-acceleration.two-dimension"},
    {"solver", "physics.kinematics.catch-up.equal-position"},
    {"solver", "physics.kinematics.relative-motion.components.two-dimension"},
    {"solver", "physics.density.mass-volume"},
    {"solver", "physics.vectors.cartesian-addition.two-dimension"},
    {"solver", "physics.vectors.cartesian-cross-product.three-dimension"},
    {"solver", "physics.vectors.magnitude-components.two-dimension"},
    {"solver", "physics.forces.newton-second-law"},
    {"solver", "physics.work.constant-force-dot-product"},
    {"solver", "physics.optics.refraction.snell"},
    {"solver", "physics.optics.thin-lens.image"},
    {"solver", "physics.optics.spherical-mirror.image"},
    {"solver", "physics.optics.double-slit.maxima"},
    {"solver", "physics.optics.single-slit.minima"},
    {"solver", "physics.gravitation.point-masses"},
    {"solver", "physics.oscillation.restoring-force"},
    {"solver", "physics.wave.speed-frequency-wavelength"},
    {"solver", "units.chain-link-conversion"},
    {"content", "units.si"},
};

const SchemaVersion kExpectedSchemas[] = {
    {"capability-manifest", 2},
    {"solution-context", 3},
};

bool same_text(TestSink &t, const char *got, const char *want, const std::string &what) {
    const bool present = got != nullptr && want != nullptr;
    t.check(present, what + " is present");
    if (present)
        t.equal(got, want, what);
    return present && std::string(got) == want;
}

bool is_sha256(const char *value) {
    if (value == nullptr)
        return false;
    const std::string hash(value);
    if (hash.size() != 64)
        return false;
    for (char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool is_manifest_id(const char *value, const char *artifact) {
    if (value == nullptr)
        return false;
    const std::string identity_kind = std::string(artifact) == "host"
                                          ? "semantic-inputs-sha256."
                                          : "inputs-sha256.";
    const std::string prefix = std::string("stepcas.") + artifact + "." + identity_kind;
    const std::string id(value);
    return id.size() == prefix.size() + 64 && id.compare(0, prefix.size(), prefix) == 0 &&
           is_sha256(id.c_str() + prefix.size());
}

SolutionContext plain(Arena &arena) {
    SolutionContext c;
    c.application_version = "a";
    c.capability_manifest_id = "b";
    c.problem_family_id = "c";
    c.problem_family_envelope_version = "d";
    c.normalized_problem_model = arena.symbol("m");
    c.original_expression = "l";
    c.normalized_expression = "m";
    c.requested_method = "e";
    c.angle_convention = "f";
    c.branch_convention = "g";
    c.unit_policy = "h";
    c.detail_projection = "i";
    c.resource_policy = "j";
    c.content_pack_versions.push_back("k");
    c.derivation_status = DerivationStatus::SolvedAndVerified;
    return c;
}

// Delimiters, a nul, an empty field, an empty list entry, an empty list, and the largest NodeId.
SolutionContext awkward(Arena &arena) {
    SolutionContext c;
    c.application_version = kDelimiters;
    c.capability_manifest_id = std::string("a nul \0 inside", 14);
    c.problem_family_id = "";
    c.problem_family_envelope_version = "007";
    c.normalized_problem_model = parse(arena, "f(x, (1 + y))").root;
    c.original_expression = std::string(kDelimiters) + " raw";
    c.normalized_expression = print(arena, c.normalized_problem_model);
    c.requested_method = std::string(kDelimiters) + kDelimiters;
    c.active_assumptions.push_back("");
    c.active_assumptions.push_back(kDelimiters);
    c.angle_convention = "radians";
    c.branch_convention = "principal branch";
    c.unit_policy = "none";
    c.detail_projection = "standard";
    c.resource_policy = "depth 64, nodes 4096";
    c.derivation_status = DerivationStatus::ConditionallySolved;
    return c;
}

ContextInputs solve_inputs(Arena &arena) {
    ContextInputs in;
    in.application_version = "ki-v2 0.1";
    in.problem_family_id = "linear.one-unknown";
    in.requested_method = "inverse operations";
    in.normalized_problem_model = parse(arena, "((5 + (2 * x)) = 13)").root;
    in.original_expression = " 2x + 5 = 13 ";
    in.normalized_expression = "((5 + (2 * x)) = 13)";
    in.active_assumptions.push_back("x is real");
    in.angle_convention = "radians";
    in.branch_convention = "principal branch";
    in.detail_projection = "standard";
    in.resource_policy = "depth 64, nodes 4096, input 4096 bytes";
    in.derivation_status = DerivationStatus::SolvedAndVerified;
    return in;
}

ContextInputs replay_inputs(Arena &arena, const char *family, const char *method,
                            const char *expression) {
    ContextInputs in;
    in.application_version = "ki-v4 0.2";
    in.problem_family_id = family;
    in.requested_method = method;
    in.normalized_problem_model = parse(arena, expression).root;
    in.original_expression = expression;
    // Taken from the AST rather than from the input, because serialize_context refuses a context
    // whose recorded expression disagrees with what its model prints.
    if (in.normalized_problem_model != kNoNode)
        in.normalized_expression = print(arena, in.normalized_problem_model);
    in.angle_convention = "radians";
    in.branch_convention = "principal branch";
    in.detail_projection = "standard";
    in.resource_policy = budget_policy(Budget());
    in.derivation_status = DerivationStatus::SolvedAndVerified;
    return in;
}

// One route, used for both the first solve and every replay of it, so the comparison below cannot
// pass because two different code paths agree about something neither of them derived.
std::string solve_and_render(Arena &arena, NodeId model, const std::string &method) {
    Derivation derivation;
    const Budget budget;
    const NodeId variable = arena.symbol("x");
    std::string outcome;
    std::string answer;
    if (method == "inverse operations") {
        const SolveResult r = solve_linear(arena, derivation, model, variable, budget);
        outcome = solve_outcome_name(r.outcome);
        if (r.solution != kNoNode)
            answer = print(arena, r.solution);
    } else if (method == "differentiate by rule") {
        const DiffResult r = differentiate(arena, derivation, model, variable, budget);
        outcome = diff_outcome_name(r.outcome);
        if (r.derivative != kNoNode)
            answer = print(arena, r.derivative);
    } else if (method == "integrate by rule") {
        const IntegrateResult r = integrate(arena, derivation, model, variable, budget);
        outcome = integrate_outcome_name(r.outcome);
        if (r.antiderivative != kNoNode)
            answer = print(arena, r.antiderivative);
    } else {
        return "no route for method: " + method;
    }
    return method + " | " + outcome + " | " + answer + "\n" + render_derivation(arena, derivation);
}

// PLAT-013 wants the derivation reproducible from the serialized context alone, so a replay has to
// rebuild the working and not merely the answer.
std::string replay(const std::string &blob) {
    Arena arena;
    SolutionContext restored;
    const ContextParseResult parsed = parse_context(blob, arena, &restored);
    if (!parsed.ok())
        return std::string("restore refused: ") + context_status_name(parsed.status);
    return solve_and_render(arena, restored.normalized_problem_model, restored.requested_method);
}

// Noise between two replays. PLAT-013 names local entry history as something that must not reach
// the derivation, and a replay that is only ever run on a clean process never tests that.
void unrelated_work() {
    Arena arena;
    const ParseResult parsed = parse(arena, "((3 * y) = 12)");
    if (parsed.ok())
        solve_and_render(arena, parsed.root, "inverse operations");
    Arena second;
    const ParseResult other = parse(second, "cos(y)");
    if (other.ok())
        solve_and_render(second, other.root, "differentiate by rule");
}

void same_list(TestSink &t, const std::vector<std::string> &got,
               const std::vector<std::string> &want, const std::string &what) {
    t.check(got.size() == want.size(), what + " keeps its entry count");
    for (size_t i = 0; i < got.size() && i < want.size(); ++i)
        t.equal(got[i], want[i], what + " keeps its entries");
}

void same_context(TestSink &t, const SolutionContext &got, const SolutionContext &want,
                  const std::string &what) {
    t.equal(got.application_version, want.application_version, what + ": application version");
    t.equal(got.capability_manifest_id, want.capability_manifest_id, what + ": manifest id");
    t.equal(got.problem_family_id, want.problem_family_id, what + ": problem family");
    t.equal(got.problem_family_envelope_version, want.problem_family_envelope_version,
            what + ": envelope version");
    t.equal(got.original_expression, want.original_expression, what + ": original expression");
    t.equal(got.normalized_expression, want.normalized_expression, what + ": normalized expression");
    t.equal(got.requested_method, want.requested_method, what + ": requested method");
    same_list(t, got.active_assumptions, want.active_assumptions, what + ": assumptions");
    t.equal(got.angle_convention, want.angle_convention, what + ": angle convention");
    t.equal(got.branch_convention, want.branch_convention, what + ": branch convention");
    t.equal(got.unit_policy, want.unit_policy, what + ": unit policy");
    t.equal(got.detail_projection, want.detail_projection, what + ": detail projection");
    t.equal(got.resource_policy, want.resource_policy, what + ": resource policy");
    same_list(t, got.content_pack_versions, want.content_pack_versions, what + ": content packs");
    t.equal(numeric_mode_name(got.numeric_mode), numeric_mode_name(want.numeric_mode),
            what + ": numeric mode");
    t.equal(derivation_status_name(got.derivation_status),
            derivation_status_name(want.derivation_status), what + ": derivation status");
}

void refuses(TestSink &t, const std::string &blob, ContextStatus expected, const std::string &what) {
    Arena arena;
    SolutionContext scratch;
    ContextParseResult r = parse_context(blob, arena, &scratch);
    t.equal(context_status_name(r.status), context_status_name(expected), what);
}

}  // namespace

void run_context_tests(TestSink &t) {
    Arena pinned_arena;
    const SolutionContext pinned_context = plain(pinned_arena);
    t.evidence("PERF-007", serialize_context(pinned_arena, pinned_context), kPlainBlobV3,
               "the version 3 wire format is pinned independently of the serializer");

    {
        const CapabilityManifest manifest = capability_manifest();
        bool exact = is_manifest_id(manifest.id, "host");
        t.check(exact, "host manifest id is derived from its build inputs");
        exact = same_text(t, capability_manifest_id(), manifest.id, "manifest id accessor") && exact;
        exact = same_text(t, manifest.artifact, "host", "manifest artifact") && exact;
        t.check(manifest.schema_version == 2, "manifest schema version is 2");
        exact = manifest.schema_version == 2 && exact;
        exact = same_text(t, manifest.stepcas_version, "nps 0.2", "StepCAS version") && exact;
        exact = same_text(t, manifest.stepcas_version, application_version(),
                          "manifest uses the application version") &&
                exact;

        t.check(manifest.supported_targets == nullptr && manifest.supported_target_count == 0,
                "host manifest claims no calculator target");
        exact = manifest.supported_targets == nullptr && manifest.supported_target_count == 0 && exact;

        exact = same_text(t, manifest.symbolic_backend.name, "none", "symbolic backend name") && exact;
        exact = same_text(t, manifest.symbolic_backend.version, "not-installed",
                          "symbolic backend version") &&
                exact;
        exact = same_text(t, manifest.symbolic_backend.interface_id, "none",
                          "symbolic backend interface") &&
                exact;
        exact = same_text(t, manifest.symbolic_backend.deployment, "absent",
                          "symbolic backend deployment") &&
                exact;

        t.check(manifest.installed_module_count == std::size(kExpectedModules),
                "manifest has every installed module");
        exact = manifest.installed_module_count == std::size(kExpectedModules) && exact;
        t.check(manifest.installed_modules != nullptr, "installed module table is present");
        exact = manifest.installed_modules != nullptr && exact;
        for (size_t i = 0; manifest.installed_modules != nullptr &&
                           i < manifest.installed_module_count && i < std::size(kExpectedModules);
             ++i) {
            exact = same_text(t, manifest.installed_modules[i].kind, kExpectedModules[i].kind,
                              "installed module kind") &&
                    exact;
            exact = same_text(t, manifest.installed_modules[i].id, kExpectedModules[i].id,
                              "installed module id") &&
                    exact;
        }

        t.check(manifest.schema_version_count == std::size(kExpectedSchemas),
                "manifest has every schema version");
        exact = manifest.schema_version_count == std::size(kExpectedSchemas) && exact;
        t.check(manifest.schema_versions != nullptr, "schema version table is present");
        exact = manifest.schema_versions != nullptr && exact;
        for (size_t i = 0; manifest.schema_versions != nullptr &&
                           i < manifest.schema_version_count && i < std::size(kExpectedSchemas);
             ++i) {
            exact = same_text(t, manifest.schema_versions[i].id, kExpectedSchemas[i].id,
                              "schema id") &&
                    exact;
            t.check(manifest.schema_versions[i].version == kExpectedSchemas[i].version,
                    "schema version matches");
            exact = manifest.schema_versions[i].version == kExpectedSchemas[i].version && exact;
        }

        t.check(manifest.integrity_identifier_count == 1,
                "host manifest has only its StepCAS source identity");
        exact = manifest.integrity_identifier_count == 1 && exact;
        t.check(manifest.integrity_identifiers != nullptr,
                "integrity identifier table is present");
        exact = manifest.integrity_identifiers != nullptr && exact;
        if (manifest.integrity_identifiers != nullptr && manifest.integrity_identifier_count == 1) {
            exact = same_text(t, manifest.integrity_identifiers[0].component,
                              "stepcas.build-inputs",
                              "integrity component") &&
                    exact;
            exact = same_text(t, manifest.integrity_identifiers[0].scheme, "sha256",
                              "integrity scheme") &&
                    exact;
            t.check(is_sha256(manifest.integrity_identifiers[0].value),
                    "StepCAS source identity is a SHA-256 digest");
            exact = is_sha256(manifest.integrity_identifiers[0].value) && exact;
        }
        t.check(exact, "the host capability manifest records its reduced truth");
    }

    {
        Arena source_arena;
        const SolutionContext source = awkward(source_arena);
        const std::string blob = serialize_context(source_arena, source);
        Arena restored_arena;
        SolutionContext restored;
        ContextParseResult r = parse_context(blob, restored_arena, &restored);
        t.check(r.ok(), "a context with delimiters, nuls and empty fields parses back");
        same_context(t, restored, source, "a round trip");
        t.equal(print(restored_arena, canonicalize(restored_arena, restored.normalized_problem_model)),
                print(source_arena, canonicalize(source_arena, source.normalized_problem_model)),
                "a round trip rebuilds the normalized AST semantics in a separate arena");
        t.check(restored.normalized_problem_model < restored_arena.node_count(),
                "the restored AST root belongs to the destination arena");
    }

    {
        // plain() has the two lists the other way round from awkward(), so between them each one is
        // carried both empty and not.
        Arena restored_arena;
        SolutionContext restored;
        t.check(parse_context(kPlainBlobV3, restored_arena, &restored).ok(),
                "the pinned version 3 blob parses back");
        same_context(t, restored, pinned_context, "the pinned version 3 blob");
        t.equal(print(restored_arena, restored.normalized_problem_model), "m",
                "the pinned version 3 blob rebuilds its normalized AST");

        {
            SolutionContext decimal_want = pinned_context;
            decimal_want.numeric_mode = NumericMode::Decimal;
            Arena decimal_arena;
            SolutionContext decimal_got;
            t.check(parse_context(kDecimalBlobV3, decimal_arena, &decimal_got).ok(),
                    "a version 3 blob written in decimal mode parses back");
            same_context(t, decimal_got, decimal_want, "the decimal-mode version 3 blob");
            t.equal(serialize_context(decimal_arena, decimal_got), std::string(kDecimalBlobV3),
                    "and rewrites to the same bytes, so the mode is carried rather than defaulted");
        }

        Arena v2_arena;
        SolutionContext v2_restored;
        t.check(parse_context(kPlainBlobV2, v2_arena, &v2_restored).ok(),
                "the version 2 blob remains readable");
        same_context(t, v2_restored, pinned_context, "the version 2 blob");
        t.equal(numeric_mode_name(v2_restored.numeric_mode), "exact",
                "and reads as exact, which is the only arithmetic the build that wrote it had");

        SolutionContext legacy = pinned_context;
        legacy.normalized_problem_model = kNoNode;
        legacy.original_expression = kContextUnknown;
        legacy.normalized_expression = kContextUnknown;
        Arena legacy_arena;
        t.check(parse_context(kPlainBlobV1, legacy_arena, &restored).ok(),
                "the version 1 blob remains readable");
        same_context(t, restored, legacy, "the version 1 blob");
        t.check(restored.normalized_problem_model == kNoNode,
                "a version 1 process-local node id is not restored as an AST");

        std::string stale_model = kPlainBlobV2;
        const size_t stale_model_at = stale_model.find("4294967295#");
        stale_model.replace(stale_model_at, 11, "7#");
        refuses(t, stale_model, ContextStatus::BadAst,
                "a version 2 process-local node id is refused");

        std::string invalid_ast = kPlainBlobV2;
        invalid_ast[invalid_ast.find("1:m") + 2] = '+';
        SolutionContext unchanged = pinned_context;
        Arena invalid_arena;
        const ContextParseResult invalid = parse_context(invalid_ast, invalid_arena, &unchanged);
        t.check(!invalid.ok(), "a malformed normalized AST is refused");
        same_context(t, unchanged, pinned_context,
                     "a malformed normalized AST leaves the output context");
        t.check(unchanged.normalized_problem_model == pinned_context.normalized_problem_model,
                "a malformed normalized AST leaves the output model");
        refuses(t, invalid_ast, ContextStatus::BadAst,
                "a version 2 normalized expression must reconstruct as an AST");

        Arena occupied_arena;
        const NodeId existing = occupied_arena.symbol("existing");
        const size_t occupied_nodes = occupied_arena.node_count();
        SolutionContext occupied_output = pinned_context;
        const ContextParseResult occupied =
            parse_context(kPlainBlobV2, occupied_arena, &occupied_output);
        t.check(occupied.status == ContextStatus::BadAst &&
                    occupied_arena.node_count() == occupied_nodes && !occupied_arena.failed() &&
                    print(occupied_arena, existing) == "existing",
                "restoration refuses a nonempty arena without mutating or poisoning it");
        same_context(t, occupied_output, pinned_context,
                     "a nonempty destination leaves the output context");

        Arena failed_arena;
        failed_arena.fail(Status::SizeExceeded);
        SolutionContext failed_output = pinned_context;
        const ContextParseResult failed =
            parse_context(kPlainBlobV2, failed_arena, &failed_output);
        t.check(failed.status == ContextStatus::BadAst && failed_arena.node_count() == 0 &&
                    failed_arena.status() == Status::SizeExceeded,
                "restoration refuses an already failed empty arena without changing its state");
        same_context(t, failed_output, pinned_context,
                     "a failed destination leaves the output context");
    }

    {
        Arena source_arena;
        SolutionContext source = plain(source_arena);
        source.normalized_expression = "x";
        t.check(serialize_context(source_arena, source).empty(),
                "serialization refuses normalized text that does not match its AST");

        source = plain(source_arena);
        source.normalized_problem_model = kNoNode;
        t.check(serialize_context(source_arena, source).empty(),
                "serialization refuses normalized text without an AST");

        source = plain(source_arena);
        source.normalized_problem_model = static_cast<NodeId>(source_arena.node_count() + 7);
        t.check(serialize_context(source_arena, source).empty(),
                "serialization refuses a node id outside its arena");

        Limits tight_limits;
        tight_limits.max_input_bytes = 1;
        Arena tight_arena(tight_limits);
        SolutionContext oversized = plain(tight_arena);
        oversized.normalized_problem_model = tight_arena.symbol("mm");
        oversized.normalized_expression = "mm";
        t.check(serialize_context(tight_arena, oversized).empty(),
                "serialization enforces the existing normalized-expression byte limit");
    }

    {
        // MVP criterion 14. Same inputs, separate construction, byte identical.
        Arena once_arena;
        once_arena.symbol("unrelated_source_node");
        const std::string once =
            serialize_context(once_arena, make_context(solve_inputs(once_arena)));
        Arena again_arena;
        const std::string again =
            serialize_context(again_arena, make_context(solve_inputs(again_arena)));
        t.equal(once, again, "two contexts built from the same inputs serialize to identical bytes");

        Arena other_arena;
        ContextInputs other = solve_inputs(other_arena);
        other.detail_projection = "concise";
        t.check(serialize_context(other_arena, make_context(other)) != once,
                "and one built from different inputs does not, so the bytes are not a constant");
    }

    {
        Arena source_arena;
        source_arena.symbol("unrelated_source_node");
        const ContextInputs inputs = solve_inputs(source_arena);
        SolutionContext built = make_context(inputs);
        t.check(context_known(built.application_version), "an input the caller gave is recorded");
        t.check(context_known(built.capability_manifest_id),
                "the context records a capability manifest id");
        t.equal(built.capability_manifest_id, capability_manifest_id(),
                "the context names the compiled capability manifest");
        t.check(!context_known(built.problem_family_envelope_version),
                "and neither is the problem family envelope version");
        t.check(built.content_pack_versions.empty(),
                "no content pack is installed separately from the binary, which is a count of zero "
                "rather than a field left blank");
        t.check(!built.unit_policy.empty() && context_known(built.unit_policy),
                "the build's own numeric policy is a fixed value it can state");
        t.evidence("MATH-016",
                   built.normalized_problem_model == inputs.normalized_problem_model &&
                       built.requested_method == inputs.requested_method &&
                       built.active_assumptions == inputs.active_assumptions &&
                       built.angle_convention == inputs.angle_convention &&
                       built.branch_convention == inputs.branch_convention &&
                       built.detail_projection == inputs.detail_projection &&
                       built.resource_policy == inputs.resource_policy && context_known(built.unit_policy),
                   "the normalized context records every setting that changes mathematical meaning");
        const std::string blob = serialize_context(source_arena, built);
        Arena restored_arena;
        SolutionContext restored;
        const ContextParseResult restoration = parse_context(blob, restored_arena, &restored);
        const std::string source_semantics =
            print(source_arena, canonicalize(source_arena, built.normalized_problem_model));
        const std::string restored_semantics =
            restoration.ok()
                ? print(restored_arena,
                        canonicalize(restored_arena, restored.normalized_problem_model))
                : std::string();
        t.evidence(
            "MATH-012",
            built.original_expression == inputs.original_expression &&
                built.normalized_expression == inputs.normalized_expression && restoration.ok() &&
                restored.original_expression == inputs.original_expression &&
                restored.normalized_expression == inputs.normalized_expression &&
                restored_semantics == source_semantics,
            "source text and normalized AST semantics survive a fresh-arena wire round trip");

        ContextInputs blank;
        SolutionContext empty_built = make_context(blank);
        t.check(!context_known(empty_built.requested_method),
                "an input left empty becomes explicitly unknown rather than a blank that reads as a "
                "value");
        t.check(empty_built.normalized_problem_model == kNoNode,
                "and a context nobody gave a problem model to names no node");
        t.equal(derivation_status_name(empty_built.derivation_status), "not recorded",
                "a context nobody finished does not claim an outcome");
    }

    {
        std::string blob = kPlainBlobV1;
        refuses(t, std::string("qcx1\n"), ContextStatus::BadMagic, "a blob that is not one of ours");
        refuses(t, blob.substr(0, 3) + "4" + blob.substr(4), ContextStatus::UnknownVersion,
                "a version this build does not have");
        refuses(t, blob.substr(0, 3) + "01\n", ContextStatus::BadNumber,
                "a version spelled with a leading zero, which would give one context two blobs");
        refuses(t, "scx18446744073709551616\n", ContextStatus::BadNumber,
                "a wire number outside uint64 is refused");
        refuses(t, "scx4294967296\n", ContextStatus::BadNumber,
                "a wire number outside the format ceiling is refused");
        refuses(t, blob + "x", ContextStatus::TrailingBytes,
                "a byte after the last field, which is a blob that means more than it says");
        refuses(t, blob.substr(0, 18) + ":" + blob.substr(19), ContextStatus::BadFraming,
                "a number that ends with a string's mark");
        refuses(t, blob.substr(0, 23) + ":" + blob.substr(24), ContextStatus::BadFraming,
                "a list that ends with a string's mark");
        refuses(t, blob.substr(0, 5) + "01:a" + blob.substr(8), ContextStatus::BadNumber,
                "a length spelled with a leading zero");
        refuses(t, blob.substr(0, 41) + "4" + blob.substr(42), ContextStatus::Truncated,
                "a length that reaches past the end of the blob");
        refuses(t, blob.substr(0, 5) + "9" + blob.substr(6), ContextStatus::BadNumber,
                "a length that swallows the fields after it");
        // Taken from the enum rather than written out, so appending an outcome moves the boundary
        // here instead of turning this into a failure about a number.
        const uint64_t last_status = static_cast<uint64_t>(DerivationStatus::SolvedAndCorroborated);
        refuses(t, blob.substr(0, 44) + std::to_string(last_status + 1) + "#",
                ContextStatus::BadStatus, "an outcome index past the last one the enum defines");

        SolutionContext edge;
        Arena edge_arena;
        t.check(parse_context(blob.substr(0, 44) + std::to_string(last_status) + "#", edge_arena,
                              &edge)
                        .ok() &&
                    edge.derivation_status == DerivationStatus::SolvedAndCorroborated,
                "and the last one it defines is accepted");

        // The reason Cancelled and SolvedButUnchecked were appended rather than placed in section
        // 15's reading order. A context stores the number, so an outcome inserted above this one
        // would leave every stored 13 meaning whatever took its place. Criterion 14 replays these.
        SolutionContext stored;
        Arena stored_arena;
        t.check(parse_context(blob.substr(0, 44) + "13#", stored_arena, &stored).ok() &&
                    stored.derivation_status == DerivationStatus::DependencyUnavailable,
                "and an index written before the two new outcomes existed still means what it did");

        // NumericallyApproximated retains index 9 in the wire format for stored records.
        SolutionContext approx;
        Arena approx_arena;
        t.check(parse_context(blob.substr(0, 44) + "9#", approx_arena, &approx).ok() &&
                    approx.derivation_status == DerivationStatus::NumericallyApproximated,
                "and index 9 written before the two new outcomes existed decodes to NumericallyApproximated");
    }

    {
        // A prefix cannot diverge from the whole blob, so it can only run out. One check, every byte.
        Arena source_arena;
        const SolutionContext source = awkward(source_arena);
        const std::string full = serialize_context(source_arena, source);
        size_t accepted = 0;
        for (size_t n = 0; n < full.size(); ++n) {
            Arena parsed_arena;
            SolutionContext scratch;
            if (parse_context(full.substr(0, n), parsed_arena, &scratch).ok())
                ++accepted;
        }
        t.check(accepted == 0, "every proper prefix of a context blob is refused");
    }

    {
        // Accepted mutations must round-trip. Refusals must identify a byte inside the blob.
        Arena source_arena;
        const SolutionContext source = awkward(source_arena);
        const std::string full = serialize_context(source_arena, source);
        const char kBytes[] = "0129:*#\n\0sx";
        const size_t kByteCount = sizeof kBytes - 1;
        size_t accepted = 0, refused = 0, wrong = 0;
        auto probe = [&](const std::string &mutant) {
            Arena parsed_arena;
            SolutionContext parsed;
            ContextParseResult r = parse_context(mutant, parsed_arena, &parsed);
            if (r.offset > mutant.size())
                ++wrong;
            if (!r.ok()) {
                ++refused;
                return;
            }
            ++accepted;
            if (serialize_context(parsed_arena, parsed) != mutant)
                ++wrong;
        };
        for (size_t at = 0; at <= full.size(); ++at) {
            for (size_t b = 0; b < kByteCount; ++b) {
                std::string inserted = full;
                inserted.insert(at, 1, kBytes[b]);
                probe(inserted);
                if (at == full.size())
                    continue;
                std::string replaced = full;
                replaced[at] = kBytes[b];
                probe(replaced);
            }
            if (at < full.size()) {
                std::string deleted = full;
                deleted.erase(at, 1);
                probe(deleted);
            }
        }
        t.check(wrong == 0, "an accepted single byte edit prints back as itself");
        t.check(accepted > 0, "the edit sweep accepts something, so it reaches the accept path");
        t.check(refused > accepted, "and refuses most of what it sees");
    }

    {
        // The refusal a parser writing as it goes gets wrong: well formed until the very last byte.
        Arena target_arena;
        SolutionContext target = make_context(solve_inputs(target_arena));
        SolutionContext before = target;
        Arena late_arena;
        ContextParseResult late =
            parse_context(std::string(kPlainBlobV1) + "x", late_arena, &target);
        t.check(!late.ok(), "a blob refused at its last byte is still refused");
        same_context(t, target, before, "a refusal leaves the output context");

        Arena early_arena;
        ContextParseResult early = parse_context("scx9\n", early_arena, &target);
        t.check(!early.ok(), "and so is one refused at its version");
        same_context(t, target, before, "a refusal at the version leaves it too");
    }

    {
        // Criterion 14. The easy cases are the three that succeed; the ones that earn their place
        // are the refusal, whose derivation must be identical too, and the domain-restricted
        // integral, whose restriction has to survive the round trip rather than the answer alone.
        struct Case {
            const char *family;
            const char *method;
            const char *expression;
            const char *what;
        };
        const Case cases[] = {
            {"linear.one-unknown", "inverse operations", "((5 + (2 * x)) = 13)", "a linear solve"},
            {"calculus.derivative", "differentiate by rule", "((x^2) * sin(x))", "a nested derivative"},
            {"calculus.integral", "integrate by rule", "(x^(-1))", "a domain-restricted integral"},
            {"calculus.integral", "integrate by rule", "tan(x)", "an unsupported integral"},
            {"linear.one-unknown", "inverse operations", "((x^2) = 4)", "an equation the solver refuses"},
            {"calculus.derivative", "differentiate by rule", "(-2 * x)",
             "a negated coefficient, which only parses since c364171"},
        };
        bool every_case_reproduced = true;
        for (const Case &c : cases) {
            Arena source_arena;
            const SolutionContext source =
                make_context(replay_inputs(source_arena, c.family, c.method, c.expression));
            const std::string blob = serialize_context(source_arena, source);
            t.check(!blob.empty(), std::string("a context for ") + c.what + " serializes");

            // The original working, from the arena the problem was first parsed into.
            const std::string original =
                solve_and_render(source_arena, source.normalized_problem_model, c.method);
            const bool reached = original.find("no route for method") == std::string::npos;
            t.check(reached, std::string("solving ") + c.what + " reaches a route");

            const std::string first = replay(blob);
            unrelated_work();
            const std::string second = replay(blob);

            t.equal(first, original,
                    std::string("replaying ") + c.what + " reproduces the original derivation");
            t.equal(second, first, std::string("replaying ") + c.what +
                                       " again after unrelated solves gives the same derivation");
            every_case_reproduced =
                every_case_reproduced && reached && first == original && second == first;
        }
        t.evidence("PLAT-013", every_case_reproduced,
                   "a derivation is reproduced from its serialized context alone, unchanged by "
                   "unrelated solves run between the replays");
    }

    {
        // Serializing what was restored must give the same bytes back, or two saves of one solution
        // would differ and criterion 14's premise of an identical context would never be testable.
        Arena source_arena;
        const SolutionContext source = make_context(replay_inputs(
            source_arena, "linear.one-unknown", "inverse operations", "((5 + (2 * x)) = 13)"));
        const std::string first = serialize_context(source_arena, source);
        Arena restored_arena;
        SolutionContext restored;
        t.check(parse_context(first, restored_arena, &restored).ok(), "the blob restores");
        t.equal(serialize_context(restored_arena, restored), first,
                "re-serializing a restored context gives the same bytes");
    }

    {
        // The falsification. Two contexts that differ only in their model must not replay alike,
        // or the comparison above would pass on a replay that ignored the restored AST entirely.
        Arena left_arena;
        const std::string left = serialize_context(
            left_arena, make_context(replay_inputs(left_arena, "linear.one-unknown",
                                                   "inverse operations", "((5 + (2 * x)) = 13)")));
        Arena right_arena;
        const std::string right = serialize_context(
            right_arena, make_context(replay_inputs(right_arena, "linear.one-unknown",
                                                    "inverse operations", "((7 + (2 * x)) = 13)")));
        t.check(left != right, "two different problems serialize differently");
        t.check(replay(left) != replay(right),
                "and replay a different derivation, so the comparison reads the restored model");
    }

    {
        KinematicsProblem slow;
        KinematicsProblem fast;
        std::string why;
        const bool parsed = parse_kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", &slow, &why) &&
                            parse_kinematics("find v; v0 = 1 m/s; a = 1 m/s^2; t = 1 s", &fast, &why);
        t.check(parsed, "two kinematics problems with different givens both parse");

        Arena slow_arena;
        Derivation slow_derivation;
        const KinematicsResult slow_result =
            solve_kinematics(slow_arena, slow_derivation, slow, Budget(), nullptr);
        Arena fast_arena;
        Derivation fast_derivation;
        const KinematicsResult fast_result =
            solve_kinematics(fast_arena, fast_derivation, fast, Budget(), nullptr);
        t.check(slow_result.outcome == KinematicsOutcome::Solved &&
                    fast_result.outcome == KinematicsOutcome::Solved,
                "and both solve, so the contexts below are the ones a real solve records");
        t.check(slow_result.value_text != fast_result.value_text,
                "and they reach different answers");

        const std::string slow_blob = serialize_context(slow_arena, slow_derivation.context);
        const std::string fast_blob = serialize_context(fast_arena, fast_derivation.context);
        t.check(!slow_blob.empty(), "a physics context serializes at all");
        t.evidence("MATH-016", !slow_blob.empty() && slow_blob != fast_blob,
                   "two kinematics problems with different givens record different contexts");

        // Different is not enough on its own. The recorded statement has to read back as the same
        // problem, or replay would reproduce a derivation for something the user did not ask.
        Arena wire_arena;
        SolutionContext restored;
        KinematicsProblem again;
        const bool round_trip = parse_context(slow_blob, wire_arena, &restored).ok() &&
                                parse_kinematics(restored.original_expression, &again, &why);
        t.check(round_trip, "and the recorded statement parses back into a kinematics problem");
        bool same_givens = round_trip && again.unknown == slow.unknown &&
                           again.knowns.size() == slow.knowns.size();
        for (size_t i = 0; same_givens && i < again.knowns.size(); ++i)
            same_givens = again.knowns[i].symbol == slow.knowns[i].symbol &&
                          again.knowns[i].quantity.unit.text == slow.knowns[i].quantity.unit.text;
        t.evidence("PLAT-013", same_givens,
                   "a kinematics problem survives its context round trip with its unit spelling "
                   "intact, so a replay renders the prose the original did");

        Arena replay_arena;
        Derivation replay_derivation;
        const KinematicsResult replayed =
            solve_kinematics(replay_arena, replay_derivation, again, Budget(), nullptr);
        t.check(replayed.outcome == slow_result.outcome &&
                    replayed.value_text == slow_result.value_text,
                "and re-solving the restored problem reaches the same outcome and answer");
        t.equal(render_derivation(replay_arena, replay_derivation),
                render_derivation(slow_arena, slow_derivation),
                "and renders the same derivation as the original solve");
    }
}

}  // namespace nps
