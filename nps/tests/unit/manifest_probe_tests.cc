#include "nps/core/capability_manifest.h"

#include <cstring>
#include <iostream>

int main() {
    const nps::CapabilityManifest manifest = nps::capability_manifest();
    for (std::size_t i = 0; i < manifest.integrity_identifier_count; ++i) {
        const nps::IntegrityIdentifier &identifier = manifest.integrity_identifiers[i];
        if (std::strcmp(identifier.component, "artifact.package") != 0)
            continue;
        if (std::strcmp(identifier.scheme, "external-sha256-sidecar") == 0) {
            std::cout << "probe package integrity is externally verified\n";
            return 0;
        }
        std::cout << "probe package integrity scheme is " << identifier.scheme << '\n';
        return 1;
    }
    std::cout << "probe package integrity identifier is missing\n";
    return 1;
}
