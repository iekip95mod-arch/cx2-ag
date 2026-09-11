#ifndef NPS_PLATFORM_NSPIRE_INTEGRITY_H
#define NPS_PLATFORM_NSPIRE_INTEGRITY_H

#include <cstddef>
#include <cstdint>

namespace nps {

enum class IntegrityStatus : uint8_t {
    Verified,
    Missing,
    Unreadable,
    Malformed,
    WrongBasename,
    Mismatch,
    PathInvalid,
};

inline constexpr std::size_t kIntegrityHexLength = 64;
inline constexpr std::size_t kIntegrityMaxPathLength = 511;
inline constexpr std::size_t kIntegrityMaxBasenameLength = 63;
inline constexpr char kUnifiedPackageBasename[] = "nps_nspire.luax.tns";
inline constexpr char kUnifiedSidecarBasename[] = "nps_nspire.luax.sha256.tns";
inline constexpr char kSplitPackageBasename[] = "nps_split.luax.tns";
inline constexpr char kProbePackageBasename[] = "nps_m0_probe.tns";

const char *integrity_status_name(IntegrityStatus status);

IntegrityStatus derive_integrity_sidecar_path(const char *package_path,
                                              const char *expected_package_basename,
                                              char *sidecar_path, std::size_t sidecar_capacity);

IntegrityStatus parse_integrity_sidecar(const char *contents, std::size_t length,
                                        const char *expected_package_basename,
                                        char expected_digest[kIntegrityHexLength + 1]);

IntegrityStatus read_integrity_sidecar(const char *sidecar_path,
                                       const char *expected_package_basename,
                                       char expected_digest[kIntegrityHexLength + 1]);

IntegrityStatus compare_integrity_digests(const char *expected_digest, const char *actual_digest);

IntegrityStatus verify_package_integrity(const char *package_path,
                                         const char *expected_package_basename);

}

#endif
