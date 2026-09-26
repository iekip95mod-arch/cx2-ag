#include "nps/core/capability_manifest.h"

#include <iterator>

#include "nps/core/budgets.h"
#include "nps/core/context.h"
#include "nps_manifest_config.h"

namespace nps {
namespace {

#if NPS_MANIFEST_RELEASE_TARGETS
const SupportedTarget kSupportedTargets[] = {
    {"TI-Nspire CX II non-CAS", "6.2.0.333", "r2022"},
    {"TI-Nspire CX II non-CAS", "6.4.0.74", "r2022"},
};
#endif

#if NPS_MANIFEST_DIAGNOSTIC_ONLY
const InstalledModule kInstalledModules[] = {
    {"diagnostic", "m0.device-probe"},
};
#else
const InstalledModule kInstalledModules[] = {
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
    {"solver", "calculus.numerical-root.polynomial"},
    {"solver", "calculus.numerical-integral.polynomial"},
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
    {"solver", "units.chain-link-conversion"},
    {"content", "units.si"},
#if NPS_RETAINED_FAULT_INJECTION
    {"diagnostic", "ui.retained.failure-injection"},
#endif
};
#endif

// nps_v4.lua refuses a manifest of more than 128 modules, so outgrowing it disables StepCAS.
static_assert(std::size(kInstalledModules) <= 128);

const SchemaVersion kSchemaVersions[] = {
    {"capability-manifest", 2},
    {"solution-context", kContextFormatVersion},
};

const IntegrityIdentifier kIntegrityIdentifiers[] = {
    {"stepcas.build-inputs", "sha256", NPS_MANIFEST_STEP_HASH},
#if NPS_MANIFEST_HAS_NDL
    {"ndl.build-inputs", "sha256", NPS_MANIFEST_NDL_HASH},
#endif
#if NPS_MANIFEST_HAS_GIAC
    {"giac.sources-config", "sha256", NPS_MANIFEST_GIAC_HASH},
#endif
#if NPS_MANIFEST_HAS_SIDECAR
    {"artifact.package", NPS_MANIFEST_SIDECAR_SCHEME, NPS_MANIFEST_SIDECAR},
#endif
#if NPS_MANIFEST_HAS_UI_SIDECAR
    {"ui.document", "external-sha256-sidecar", NPS_MANIFEST_UI_SIDECAR},
#endif
};

}

const char *capability_manifest_id() { return NPS_MANIFEST_ID; }

CapabilityManifest capability_manifest() {
    CapabilityManifest manifest;
    manifest.id = capability_manifest_id();
    manifest.artifact = NPS_MANIFEST_ARTIFACT;
    manifest.schema_version = 2;
    manifest.stepcas_version = application_version();
#if NPS_MANIFEST_RELEASE_TARGETS
    manifest.supported_targets = kSupportedTargets;
    manifest.supported_target_count = std::size(kSupportedTargets);
#else
    manifest.supported_targets = nullptr;
    manifest.supported_target_count = 0;
#endif
    manifest.symbolic_backend = {NPS_MANIFEST_BACKEND_NAME, NPS_MANIFEST_BACKEND_VERSION,
                                 NPS_MANIFEST_BACKEND_INTERFACE,
                                 NPS_MANIFEST_BACKEND_DEPLOYMENT};
    manifest.installed_modules = kInstalledModules;
    manifest.installed_module_count = std::size(kInstalledModules);
    manifest.schema_versions = kSchemaVersions;
    manifest.schema_version_count = std::size(kSchemaVersions);
    manifest.integrity_identifiers = kIntegrityIdentifiers;
    manifest.integrity_identifier_count = std::size(kIntegrityIdentifiers);
    return manifest;
}

}
