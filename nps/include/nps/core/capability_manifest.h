#ifndef NPS_CAPABILITY_MANIFEST_H
#define NPS_CAPABILITY_MANIFEST_H

#include <cstddef>
#include <cstdint>

namespace nps {

struct SupportedTarget {
    const char *calculator_model;
    const char *os_version;
    const char *ndl_version;
};

struct SymbolicBackendCapability {
    const char *name;
    const char *version;
    const char *interface_id;
    const char *deployment;
};

struct InstalledModule {
    const char *kind;
    const char *id;
};

struct SchemaVersion {
    const char *id;
    uint32_t version;
};

struct IntegrityIdentifier {
    const char *component;
    const char *scheme;
    const char *value;
};

struct CapabilityManifest {
    const char *id;
    const char *artifact;
    uint32_t schema_version;
    const char *stepcas_version;
    const SupportedTarget *supported_targets;
    size_t supported_target_count;
    SymbolicBackendCapability symbolic_backend;
    const InstalledModule *installed_modules;
    size_t installed_module_count;
    const SchemaVersion *schema_versions;
    size_t schema_version_count;
    const IntegrityIdentifier *integrity_identifiers;
    size_t integrity_identifier_count;
};

const char *capability_manifest_id();
CapabilityManifest capability_manifest();

}

#endif
