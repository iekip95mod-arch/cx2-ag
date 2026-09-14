#include "nps/core/capability_manifest.h"

#include <cstring>
#include <iostream>

#ifndef NPS_EXPECTED_ARTIFACT
#error NPS_EXPECTED_ARTIFACT must name the manifest artifact under test
#endif

#ifndef NPS_EXPECTED_SIDECAR_SCHEME
#error NPS_EXPECTED_SIDECAR_SCHEME must name the expected package sidecar scheme
#endif

int main() {
    const nps::CapabilityManifest manifest = nps::capability_manifest();
    if (std::strcmp(manifest.artifact, NPS_EXPECTED_ARTIFACT) != 0) {
        std::cout << "expected " << NPS_EXPECTED_ARTIFACT << " manifest, got " << manifest.artifact
                  << '\n';
        return 1;
    }
    for (std::size_t i = 0; i < manifest.integrity_identifier_count; ++i) {
        const nps::IntegrityIdentifier &identifier = manifest.integrity_identifiers[i];
        if (std::strcmp(identifier.component, "artifact.package") != 0)
            continue;
        if (std::strcmp(identifier.scheme, NPS_EXPECTED_SIDECAR_SCHEME) == 0) {
            std::cout << manifest.artifact << " package integrity scheme is " << identifier.scheme << '\n';
            return 0;
        }
        std::cout << manifest.artifact << " package integrity scheme is " << identifier.scheme << '\n';
        return 1;
    }
    std::cout << manifest.artifact << " package integrity identifier is missing\n";
    return 1;
}
