#include "nps/platform/nspire/integrity.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "sha256.h"

namespace nps {
namespace {

constexpr char kSidecarSuffix[] = ".sha256.tns";
constexpr std::size_t kSidecarFramingLength = kIntegrityHexLength + 2 + 1;
constexpr std::size_t kMaxSidecarLength =
    kSidecarFramingLength + kIntegrityMaxBasenameLength;

bool lowercase_hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

void hex_digest(const BYTE digest[SHA256_BLOCK_SIZE], char text[kIntegrityHexLength + 1]) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    for (std::size_t index = 0; index < SHA256_BLOCK_SIZE; ++index) {
        text[index * 2] = kHexDigits[digest[index] >> 4];
        text[index * 2 + 1] = kHexDigits[digest[index] & 0x0f];
    }
    text[kIntegrityHexLength] = '\0';
}

std::size_t bounded_length(const char *text, std::size_t limit) {
    std::size_t length = 0;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

int open_read_only(const char *path) {
    int flags = O_RDONLY;
#if defined(O_BINARY)
    flags |= O_BINARY;
#endif

    int descriptor = -1;
    do {
        descriptor = ::open(path, flags);
    } while (descriptor < 0 && errno == EINTR);
    return descriptor;
}

std::ptrdiff_t read_descriptor(int descriptor, void *buffer, std::size_t capacity) {
    std::ptrdiff_t count = -1;
    do {
        count = static_cast<std::ptrdiff_t>(::read(descriptor, buffer, capacity));
    } while (count < 0 && errno == EINTR);
    return count;
}

IntegrityStatus hash_package(const char *package_path,
                             char digest_text[kIntegrityHexLength + 1]) {
    const int package = open_read_only(package_path);
    if (package < 0)
        return errno == ENOENT ? IntegrityStatus::Missing : IntegrityStatus::Unreadable;

    SHA256_CTX hash;
    BYTE buffer[1024];
    sha256_init(&hash);

    while (true) {
        const std::ptrdiff_t count = read_descriptor(package, buffer, sizeof(buffer));
        if (count < 0) {
            static_cast<void>(::close(package));
            return IntegrityStatus::Unreadable;
        }
        if (count == 0)
            break;
        sha256_update(&hash, buffer, static_cast<std::size_t>(count));
    }
    if (::close(package) != 0)
        return IntegrityStatus::Unreadable;

    BYTE digest[SHA256_BLOCK_SIZE];
    sha256_final(&hash, digest);
    hex_digest(digest, digest_text);
    return IntegrityStatus::Verified;
}

}

const char *integrity_status_name(IntegrityStatus status) {
    switch (status) {
        case IntegrityStatus::Verified:
            return "verified";
        case IntegrityStatus::Missing:
            return "missing";
        case IntegrityStatus::Unreadable:
            return "unreadable";
        case IntegrityStatus::Malformed:
            return "malformed";
        case IntegrityStatus::WrongBasename:
            return "wrong-basename";
        case IntegrityStatus::Mismatch:
            return "mismatch";
        case IntegrityStatus::PathInvalid:
            return "path-invalid";
    }
    return "path-invalid";
}

IntegrityStatus derive_integrity_sidecar_path(const char *package_path,
                                              const char *expected_package_basename,
                                              char *sidecar_path, std::size_t sidecar_capacity) {
    if (!package_path || !expected_package_basename || !sidecar_path || sidecar_capacity == 0)
        return IntegrityStatus::PathInvalid;

    const std::size_t package_length = bounded_length(package_path, kIntegrityMaxPathLength + 1);
    if (package_length == 0 || package_length > kIntegrityMaxPathLength)
        return IntegrityStatus::PathInvalid;

    const char *slash = std::strrchr(package_path, '/');
    const char *basename = slash ? slash + 1 : package_path;
    if (std::strcmp(basename, expected_package_basename) != 0)
        return IntegrityStatus::PathInvalid;

    constexpr std::size_t kPackageSuffixLength = 4;
    constexpr std::size_t kSidecarSuffixLength = sizeof(kSidecarSuffix) - 1;
    const std::size_t stem_length = package_length - kPackageSuffixLength;
    const std::size_t sidecar_length = stem_length + kSidecarSuffixLength;
    if (sidecar_length > kIntegrityMaxPathLength || sidecar_length >= sidecar_capacity)
        return IntegrityStatus::PathInvalid;

    std::memcpy(sidecar_path, package_path, stem_length);
    std::memcpy(sidecar_path + stem_length, kSidecarSuffix, sizeof(kSidecarSuffix));
    return IntegrityStatus::Verified;
}

IntegrityStatus parse_integrity_sidecar(const char *contents, std::size_t length,
                                        const char *expected_package_basename,
                                        char expected_digest[kIntegrityHexLength + 1]) {
    if (!contents || !expected_package_basename || !expected_digest)
        return IntegrityStatus::Malformed;

    const std::size_t basename_length =
        bounded_length(expected_package_basename, kIntegrityMaxBasenameLength + 1);
    if (basename_length == 0 || basename_length > kIntegrityMaxBasenameLength)
        return IntegrityStatus::WrongBasename;
    if (length < kSidecarFramingLength || length > kMaxSidecarLength)
        return IntegrityStatus::Malformed;

    for (std::size_t index = 0; index < kIntegrityHexLength; ++index) {
        if (!lowercase_hex(contents[index]))
            return IntegrityStatus::Malformed;
    }
    if (contents[kIntegrityHexLength] != ' ' || contents[kIntegrityHexLength + 1] != ' ')
        return IntegrityStatus::Malformed;
    if (contents[length - 1] != '\n')
        return IntegrityStatus::Malformed;

    const char *record_basename = contents + kIntegrityHexLength + 2;
    const std::size_t record_basename_length = length - kSidecarFramingLength;
    if (record_basename_length != basename_length) {
        if (record_basename_length > basename_length &&
            std::memcmp(record_basename, expected_package_basename, basename_length) == 0)
            return IntegrityStatus::Malformed;
        return IntegrityStatus::WrongBasename;
    }
    if (std::memcmp(record_basename, expected_package_basename, basename_length) != 0)
        return IntegrityStatus::WrongBasename;

    std::memcpy(expected_digest, contents, kIntegrityHexLength);
    expected_digest[kIntegrityHexLength] = '\0';
    return IntegrityStatus::Verified;
}

IntegrityStatus read_integrity_sidecar(const char *sidecar_path,
                                       const char *expected_package_basename,
                                       char expected_digest[kIntegrityHexLength + 1]) {
    if (!sidecar_path || !expected_package_basename || !expected_digest)
        return IntegrityStatus::PathInvalid;
    const std::size_t sidecar_path_length =
        bounded_length(sidecar_path, kIntegrityMaxPathLength + 1);
    if (sidecar_path_length == 0 || sidecar_path_length > kIntegrityMaxPathLength)
        return IntegrityStatus::PathInvalid;
    const std::size_t basename_length =
        bounded_length(expected_package_basename, kIntegrityMaxBasenameLength + 1);
    if (basename_length == 0 || basename_length > kIntegrityMaxBasenameLength)
        return IntegrityStatus::WrongBasename;

    const int sidecar = open_read_only(sidecar_path);
    if (sidecar < 0)
        return errno == ENOENT ? IntegrityStatus::Missing : IntegrityStatus::Unreadable;

    char contents[kMaxSidecarLength + 1];
    std::size_t total = 0;
    while (total < sizeof(contents)) {
        const std::ptrdiff_t count =
            read_descriptor(sidecar, contents + total, sizeof(contents) - total);
        if (count < 0) {
            static_cast<void>(::close(sidecar));
            return IntegrityStatus::Unreadable;
        }
        if (count == 0)
            break;
        total += static_cast<std::size_t>(count);
    }
    if (::close(sidecar) != 0)
        return IntegrityStatus::Unreadable;
    if (total > kMaxSidecarLength)
        return IntegrityStatus::Malformed;
    return parse_integrity_sidecar(contents, total, expected_package_basename, expected_digest);
}

IntegrityStatus compare_integrity_digests(const char *expected_digest, const char *actual_digest) {
    if (!expected_digest || !actual_digest)
        return IntegrityStatus::Malformed;
    if (bounded_length(expected_digest, kIntegrityHexLength + 1) != kIntegrityHexLength ||
        bounded_length(actual_digest, kIntegrityHexLength + 1) != kIntegrityHexLength)
        return IntegrityStatus::Malformed;
    return std::memcmp(expected_digest, actual_digest, kIntegrityHexLength) == 0
               ? IntegrityStatus::Verified
               : IntegrityStatus::Mismatch;
}

IntegrityStatus verify_package_integrity(const char *package_path,
                                         const char *expected_package_basename) {
    char sidecar_path[kIntegrityMaxPathLength + 1];
    IntegrityStatus status = derive_integrity_sidecar_path(package_path, expected_package_basename,
                                                           sidecar_path, sizeof(sidecar_path));
    if (status != IntegrityStatus::Verified)
        return status;

    char expected_digest[kIntegrityHexLength + 1];
    status = read_integrity_sidecar(sidecar_path, expected_package_basename, expected_digest);
    if (status != IntegrityStatus::Verified)
        return status;

    char actual_digest[kIntegrityHexLength + 1];
    status = hash_package(package_path, actual_digest);
    if (status != IntegrityStatus::Verified)
        return status;
    return compare_integrity_digests(expected_digest, actual_digest);
}

}
