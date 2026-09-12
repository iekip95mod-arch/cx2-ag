#include "nps/platform/nspire/device_identity.h"
#include "nps/platform/nspire/integrity.h"
#include "sha256.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "unit/adapter_tests.h"

namespace nps {
namespace {

std::string sidecar_record(const std::string &digest, const std::string &basename) {
    return digest + "  " + basename + "\n";
}

// /private/tmp is a macOS spelling. On Linux it does not exist, so every mkstemp and mkdtemp below
// failed and took eighteen integrity checks down with it.
std::string temporary_root() {
    const char *from_environment = std::getenv("TMPDIR");
    if (!from_environment || from_environment[0] == '\0')
        return "/tmp";
    std::string root = from_environment;
    while (root.size() > 1 && root.back() == '/')
        root.pop_back();
    return root;
}

class TemporarySidecar {
   public:
    explicit TemporarySidecar(const std::string &contents) {
        std::snprintf(path_, sizeof(path_), "%s/nps_integrity_XXXXXX", temporary_root().c_str());
        const int descriptor = ::mkstemp(path_);
        if (descriptor < 0) {
            path_[0] = '\0';
            return;
        }

        std::size_t offset = 0;
        while (offset < contents.size()) {
            std::ptrdiff_t count = -1;
            do {
                count = static_cast<std::ptrdiff_t>(
                    ::write(descriptor, contents.data() + offset, contents.size() - offset));
            } while (count < 0 && errno == EINTR);
            if (count <= 0)
                break;
            offset += static_cast<std::size_t>(count);
        }
        const bool closed = ::close(descriptor) == 0;
        ready_ = offset == contents.size() && closed;
    }

    ~TemporarySidecar() {
        if (path_[0] != '\0')
            static_cast<void>(::unlink(path_));
    }

    TemporarySidecar(const TemporarySidecar &) = delete;
    TemporarySidecar &operator=(const TemporarySidecar &) = delete;

    bool ready() const { return ready_; }
    const char *path() const { return path_; }

   private:
    char path_[kIntegrityMaxPathLength + 1] = {};
    bool ready_ = false;
};

std::string hex_of(const BYTE digest[SHA256_BLOCK_SIZE]) {
    static const char kHexDigits[] = "0123456789abcdef";
    std::string text;
    for (std::size_t index = 0; index < SHA256_BLOCK_SIZE; ++index) {
        text.push_back(kHexDigits[digest[index] >> 4]);
        text.push_back(kHexDigits[digest[index] & 0x0f]);
    }
    return text;
}

std::string sha256_text(const std::string &bytes) {
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, reinterpret_cast<const BYTE *>(bytes.data()), bytes.size());
    BYTE digest[SHA256_BLOCK_SIZE];
    sha256_final(&hash, digest);
    return hex_of(digest);
}

bool write_whole_file(const std::string &path, const std::string &contents) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool wrote = contents.empty() ||
                       std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    return std::fclose(file) == 0 && wrote;
}

// A package beside its generated sidecar, under the two names the runtime derives, so a check can
// damage one of them and ask the runtime what it makes of the result.
class TemporaryDeployment {
   public:
    explicit TemporaryDeployment(const std::string &package_bytes,
                                 const std::string &basename = kUnifiedPackageBasename) {
        std::string pattern = temporary_root() + "/nps_deploy_XXXXXX";
        const char *made = ::mkdtemp(pattern.data());
        if (!made)
            return;
        directory_ = made;
        package_ = directory_ + "/" + basename;
        sidecar_ = directory_ + "/" + basename.substr(0, basename.size() - 4) + ".sha256.tns";
        ready_ = write_whole_file(package_, package_bytes) &&
                 write_whole_file(sidecar_, sha256_text(package_bytes) + "  " + basename + "\n");
    }

    ~TemporaryDeployment() {
        if (directory_.empty())
            return;
        static_cast<void>(::unlink(package_.c_str()));
        static_cast<void>(::unlink(sidecar_.c_str()));
        static_cast<void>(::rmdir(directory_.c_str()));
    }

    TemporaryDeployment(const TemporaryDeployment &) = delete;
    TemporaryDeployment &operator=(const TemporaryDeployment &) = delete;

    bool ready() const { return ready_; }
    const std::string &package() const { return package_; }
    const std::string &sidecar() const { return sidecar_; }

   private:
    std::string directory_;
    std::string package_;
    std::string sidecar_;
    bool ready_ = false;
};

std::string patterned_bytes(std::size_t length) {
    std::string bytes;
    bytes.reserve(length);
    for (std::size_t index = 0; index < length; ++index)
        bytes.push_back(static_cast<char>(index * 31 + 7));
    return bytes;
}

std::string trimmed_line(const std::string &line) {
    std::size_t first = 0;
    std::size_t last = line.size();
    while (first < last && (line[first] == ' ' || line[first] == '\t'))
        ++first;
    while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t' || line[last - 1] == '\r'))
        --last;
    return line.substr(first, last - first);
}

struct SdkOsVersions {
    bool readable = false;
    unsigned checked = 0;
    unsigned disagreements = 0;
};

// ut_read_os_version_index is where ndl decides which build it is running on, and it names each
// one in a comment beside the case. Reading both back is what stops the table in device_identity.cc
// drifting from the source it was copied out of.
SdkOsVersions compare_os_versions_against_sdk(const char *path) {
    SdkOsVersions result;
    std::ifstream source(path);
    if (!source)
        return result;
    result.readable = true;

    const std::string assignment = "ut_os_version_index = ";
    std::string line;
    std::string named;
    while (std::getline(source, line)) {
        const std::string flat = trimmed_line(line);
        const std::size_t comment = flat.find("// ");
        if (flat.compare(0, 5, "case ") == 0 && comment != std::string::npos) {
            named = trimmed_line(flat.substr(comment + 3));
            continue;
        }
        if (named.empty() || flat.compare(0, assignment.size(), assignment) != 0)
            continue;

        unsigned index = 0;
        std::size_t at = assignment.size();
        while (at < flat.size() && flat[at] >= '0' && flat[at] <= '9') {
            index = index * 10 + static_cast<unsigned>(flat[at] - '0');
            ++at;
        }
        ++result.checked;

        const bool sdk_cas = named.find("CAS") != std::string::npos &&
                             named.find("non-CAS") == std::string::npos;
        const bool sdk_cx2 = named.find("CX II") != std::string::npos;
        if (named != os_version_name(index) ||
            (os_version_cas(index) == CasBuild::Cas) != sdk_cas ||
            os_version_is_cx2(index) != sdk_cx2)
            ++result.disagreements;
        named.clear();
    }
    return result;
}

void run_device_identity_tests(TestSink &sink) {
    // The drifted excerpt goes first, because a comparison that has never rejected anything says
    // nothing when it accepts the real source.
    const SdkOsVersions drifted =
        compare_os_versions_against_sdk("tests/unit/testdata/os_version_drift.c");
    sink.check(drifted.readable && drifted.checked == 2 && drifted.disagreements == 1,
               "a renamed OS build is caught, so accepting the real source means something");

    const SdkOsVersions sdk =
        compare_os_versions_against_sdk("../ndl/ndl/src/resources/utils.c");
    if (sdk.readable)
        sink.check(sdk.checked >= 48 && sdk.disagreements == 0,
                   "every OS build ndl recognises is named and classified the same way here");
    else
        sink.check(true, "the ndl source is not in this checkout, so the OS table is unchecked");

    sink.check(std::strcmp(os_version_name(14), "unknown") == 0 &&
                   os_version_cas(14) == CasBuild::Unknown && !os_version_is_cx2(14),
               "an index ndl leaves as a gap reports unknown rather than a neighbour's name");
    sink.check(std::strcmp(os_version_name(kOsVersionIndexCount), "unknown") == 0 &&
                   os_version_cas(kOsVersionIndexCount) == CasBuild::Unknown &&
                   std::strcmp(os_version_name(0xFFFFFFFFu), "unknown") == 0,
               "an OS newer than this table reports unknown rather than reading past its end");

    const DeviceIdentity handheld = interpret_device_identity(1, 2, 47, 2022, false);
    sink.check(handheld.model == CalculatorModel::CXII && handheld.cas == CasBuild::NonCas &&
                   handheld.model_agrees_with_os &&
                   std::strcmp(handheld.os_name, "6.4.0.74 non-CAS CX II") == 0,
               "a non-CAS CX II on 6.4.0.74 is read as exactly that");

    const DeviceIdentity cas = interpret_device_identity(1, 2, 49, 2022, false);
    sink.check(cas.model == CalculatorModel::CXII && cas.cas == CasBuild::Cas &&
                   cas.model_agrees_with_os,
               "the CAS build of the same OS is not mistaken for the non-CAS one");

    const DeviceIdentity mismatched = interpret_device_identity(1, 2, 32, 2022, false);
    sink.check(mismatched.model == CalculatorModel::CXII && !mismatched.model_agrees_with_os,
               "CX II hardware running a CX OS is reported as a disagreement rather than resolved");

    const DeviceIdentity unmapped = interpret_device_identity(1, 2, kOsVersionIndexCount, 2022, false);
    sink.check(unmapped.model == CalculatorModel::CXII && unmapped.cas == CasBuild::Unknown &&
                   !unmapped.model_agrees_with_os,
               "an OS this table does not know leaves the CAS question open rather than assuming");

    // The mirror of the unmapped-OS case above. Unknown is not CX II, so against any OS index that
    // is not a CX II build the two false values used to match and read as corroboration.
    const DeviceIdentity strange_subtype = interpret_device_identity(1, 3, 42, 2022, false);
    sink.check(strange_subtype.model == CalculatorModel::Unknown &&
                   !strange_subtype.model_agrees_with_os,
               "a hardware subtype this code does not know agrees with no OS rather than with every "
               "OS that is not a CX II");

    const DeviceIdentity classic = interpret_device_identity(0, 0, 0, 2022, false);
    const DeviceIdentity touchpad = interpret_device_identity(1, 0, 12, 2022, false);
    const DeviceIdentity cm = interpret_device_identity(1, 1, 4, 2022, false);
    sink.check(classic.model == CalculatorModel::Classic &&
                   touchpad.model == CalculatorModel::TouchpadCX &&
                   cm.model == CalculatorModel::CM && classic.model_agrees_with_os &&
                   touchpad.model_agrees_with_os && cm.model_agrees_with_os,
               "the three models that are not a CX II are told apart and agree with their OS");

    const DeviceIdentity loaded = interpret_device_identity(1, 2, 47, 2022, true);
    sink.check(loaded.loaded_by_third_party_loader && loaded.ndl_revision == 2022,
               "the loader and the ndl revision are carried through rather than dropped");
}

}

void run_integrity_tests(TestSink &sink) {
    const std::string digest(kIntegrityHexLength, 'a');
    const std::string valid = sidecar_record(digest, kUnifiedPackageBasename);
    char parsed[kIntegrityHexLength + 1] = {};

    sink.check(parse_integrity_sidecar(valid.data(), valid.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Verified &&
                   std::strcmp(parsed, digest.c_str()) == 0,
               "the integrity parser accepts exactly one canonical sidecar record");

    std::string changed = valid;
    changed[0] = 'A';
    sink.check(parse_integrity_sidecar(changed.data(), changed.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Malformed,
               "the integrity parser rejects uppercase digest text");
    changed = valid;
    changed[31] = 'g';
    sink.check(parse_integrity_sidecar(changed.data(), changed.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Malformed,
               "the integrity parser rejects non-hex digest text");

    const std::string short_digest =
        sidecar_record(digest.substr(0, kIntegrityHexLength - 1), kUnifiedPackageBasename);
    sink.check(parse_integrity_sidecar(short_digest.data(), short_digest.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects a short digest");
    const std::string long_digest = sidecar_record(digest + "0", kUnifiedPackageBasename);
    sink.check(parse_integrity_sidecar(long_digest.data(), long_digest.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects a long digest");

    changed = valid;
    changed[kIntegrityHexLength + 1] = '\t';
    sink.check(parse_integrity_sidecar(changed.data(), changed.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Malformed,
               "the integrity parser requires two literal spaces");
    const std::string half_digest =
        sidecar_record(digest.substr(0, 40), kUnifiedPackageBasename);
    sink.check(parse_integrity_sidecar(half_digest.data(), half_digest.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects a digest truncated part way");
    const std::string trailing_space = sidecar_record(digest, std::string(kUnifiedPackageBasename) + " ");
    sink.check(parse_integrity_sidecar(trailing_space.data(), trailing_space.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects a space between the name and the newline");
    const std::string carriage_return =
        sidecar_record(digest, std::string(kUnifiedPackageBasename) + "\r");
    sink.check(parse_integrity_sidecar(carriage_return.data(), carriage_return.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects a carriage return the build never wrote");
    // cmake's file(SHA256) writes lowercase, so an uppercase pair is not a record this build wrote.
    const std::string uppercase_pair =
        sidecar_record(digest.substr(0, 62) + "AB", kUnifiedPackageBasename);
    sink.check(parse_integrity_sidecar(uppercase_pair.data(), uppercase_pair.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser rejects an uppercase hex pair inside a full-length digest");
    const std::string relative_name =
        sidecar_record(digest, std::string("./") + kUnifiedPackageBasename);
    const std::string absolute_name =
        sidecar_record(digest, std::string("/documents/ndl/") + kUnifiedPackageBasename);
    sink.check(parse_integrity_sidecar(relative_name.data(), relative_name.size(),
                                       kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::WrongBasename &&
                   parse_integrity_sidecar(absolute_name.data(), absolute_name.size(),
                                           kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::WrongBasename,
               "the integrity parser wants a bare basename, not a path that ends in one");

    const std::string wrong_basename = sidecar_record(digest, "nps_other.luax.tns");
    sink.check(parse_integrity_sidecar(wrong_basename.data(), wrong_basename.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::WrongBasename,
               "the integrity parser rejects a different package basename");

    const std::string no_newline = valid.substr(0, valid.size() - 1);
    sink.check(parse_integrity_sidecar(no_newline.data(), no_newline.size(),
                                       kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::Malformed,
               "the integrity parser requires one terminating newline");
    const std::string trailing = valid + "\n";
    sink.check(parse_integrity_sidecar(trailing.data(), trailing.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Malformed,
               "the integrity parser rejects trailing bytes");
    changed = valid;
    changed[10] = '\0';
    sink.check(parse_integrity_sidecar(changed.data(), changed.size(), kUnifiedPackageBasename,
                                       parsed) == IntegrityStatus::Malformed,
               "the integrity parser rejects embedded zero bytes");

    char path[kIntegrityMaxPathLength + 1] = {};
    constexpr char kPackagePath[] = "/documents/ndl/stepcas/nps_nspire.luax.tns";
    sink.check(derive_integrity_sidecar_path(kPackagePath, kUnifiedPackageBasename, path, sizeof(path)) ==
                       IntegrityStatus::Verified &&
                   std::strcmp(path,
                               "/documents/ndl/stepcas/nps_nspire.luax.sha256.tns") == 0,
               "the integrity path stays adjacent and ends in a transferable tns suffix");
    sink.check(derive_integrity_sidecar_path(kUnifiedPackageBasename, kUnifiedPackageBasename, path,
                                                 sizeof(path)) ==
                       IntegrityStatus::Verified &&
                   std::strcmp(path, kUnifiedSidecarBasename) == 0,
               "the integrity path supports an Ndl basename without a directory");
    sink.check(derive_integrity_sidecar_path("/documents/nps_other.luax.tns",
                                             kUnifiedPackageBasename, path, sizeof(path)) == IntegrityStatus::PathInvalid,
               "the integrity path rejects a renamed package");
    sink.check(derive_integrity_sidecar_path(kPackagePath, kUnifiedPackageBasename, path,
                                             std::strlen(kPackagePath)) ==
                   IntegrityStatus::PathInvalid,
               "the integrity path rejects a short output buffer");
    const std::string overlong_path(kIntegrityMaxPathLength + 1, 'x');
    sink.check(derive_integrity_sidecar_path(overlong_path.c_str(), kUnifiedPackageBasename, path,
                                             sizeof(path)) ==
                   IntegrityStatus::PathInvalid,
               "the integrity path rejects an unterminated bounded prefix");

    TemporarySidecar valid_file(valid);
    parsed[0] = '\0';
    sink.check(valid_file.ready() &&
                   read_integrity_sidecar(valid_file.path(), kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::Verified &&
                   std::strcmp(parsed, digest.c_str()) == 0,
               "the integrity reader accepts a canonical sidecar file");

    TemporarySidecar malformed_file(no_newline);
    sink.check(malformed_file.ready() &&
                   read_integrity_sidecar(malformed_file.path(), kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::Malformed,
               "the integrity reader rejects malformed sidecar files");

    const std::string oversized(kIntegrityHexLength + 2 + kIntegrityMaxBasenameLength + 2, 'x');
    TemporarySidecar oversized_file(oversized);
    sink.check(oversized_file.ready() &&
                   read_integrity_sidecar(oversized_file.path(), kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::Malformed,
               "the integrity reader rejects sidecar files beyond its fixed bound");

    TemporarySidecar wrong_basename_file(wrong_basename);
    sink.check(wrong_basename_file.ready() &&
                   read_integrity_sidecar(wrong_basename_file.path(), kUnifiedPackageBasename,
                                          parsed) == IntegrityStatus::WrongBasename,
               "the integrity reader rejects a sidecar naming another package");

    TemporarySidecar missing_file(valid);
    const bool removed_missing_file = missing_file.ready() && ::unlink(missing_file.path()) == 0;
    sink.check(removed_missing_file &&
                   read_integrity_sidecar(missing_file.path(), kUnifiedPackageBasename, parsed) ==
                       IntegrityStatus::Missing,
               "a removed integrity sidecar returns its typed missing status");

    TemporarySidecar unreadable_file(valid);
    bool unreadable_file_checked = unreadable_file.ready();
    if (unreadable_file_checked && ::geteuid() != 0) {
        unreadable_file_checked = ::chmod(unreadable_file.path(), 0000) == 0 &&
                                  read_integrity_sidecar(unreadable_file.path(),
                                                         kUnifiedPackageBasename, parsed) ==
                                      IntegrityStatus::Unreadable;
        const bool permissions_restored = ::chmod(unreadable_file.path(), 0600) == 0;
        unreadable_file_checked = unreadable_file_checked && permissions_restored;
    }
    sink.check(unreadable_file_checked,
               "an unreadable integrity sidecar returns its typed status when enforceable");

    sink.check(read_integrity_sidecar(overlong_path.c_str(), kUnifiedPackageBasename, parsed) ==
                   IntegrityStatus::PathInvalid,
               "sidecar reads reject overlong paths before file access");

    bool every_digest_mutation_rejected = true;
    for (std::size_t index = 0; index < digest.size(); ++index) {
        std::string mutated_digest = digest;
        mutated_digest[index] = 'b';
        if (compare_integrity_digests(digest.c_str(), mutated_digest.c_str()) !=
            IntegrityStatus::Mismatch) {
            every_digest_mutation_rejected = false;
            break;
        }
    }
    sink.check(compare_integrity_digests(digest.c_str(), digest.c_str()) ==
                       IntegrityStatus::Verified &&
                   every_digest_mutation_rejected,
               "digest comparison rejects a mutation at every digest position");
    sink.check(std::strcmp(integrity_status_name(IntegrityStatus::Verified), "verified") == 0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::Missing), "missing") == 0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::Unreadable), "unreadable") ==
                       0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::Malformed), "malformed") ==
                       0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::WrongBasename),
                               "wrong-basename") == 0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::Mismatch), "mismatch") == 0 &&
                   std::strcmp(integrity_status_name(IntegrityStatus::PathInvalid), "path-invalid") ==
                       0,
               "every integrity failure has a stable typed status");

    // Everything below this line compares two digests, so a wrong implementation agrees with itself
    // and every one of those checks passes. These are what say the digest is the right one.
    // Keep all four. Writing the padding length as a byte count, or little-endian, both survive the
    // empty vector, whose length is zero either way, and die on the other three.
    sink.check(sha256_text("") ==
                       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
                   sha256_text("abc") ==
                       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
                   sha256_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" &&
                   sha256_text(std::string(1000000, 'a')) ==
                       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
               "the package digest matches the published SHA-256 vectors");

    bool every_feed_length_agrees = true;
    for (std::size_t length = 53; length <= 66 && every_feed_length_agrees; ++length) {
        const std::string bytes(length, 'a');
        SHA256_CTX split;
        sha256_init(&split);
        for (char byte : bytes) sha256_update(&split, reinterpret_cast<const BYTE *>(&byte), 1);
        BYTE fed[SHA256_BLOCK_SIZE];
        sha256_final(&split, fed);
        every_feed_length_agrees = sha256_text(bytes) == hex_of(fed);
    }
    // 55 is the last length that pads inside one block and 56 the first that needs a second, so this
    // range is the only place either side of that transition is covered. The package lengths below
    // start at 63 and never reach it.
    sink.check(every_feed_length_agrees,
               "a byte-at-a-time feed agrees at every length that straddles the padding block");

    const std::string package = patterned_bytes(5000);

    {
        TemporaryDeployment good(package);
        sink.check(good.ready() && verify_package_integrity(good.package().c_str(), kUnifiedPackageBasename) ==
                                       IntegrityStatus::Verified,
                   "an untouched package verifies against its generated sidecar");
    }

    bool every_flipped_bit_refused = true;
    for (std::size_t position : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{64},
                                std::size_t{2499}, std::size_t{4998}, std::size_t{4999}}) {
        TemporaryDeployment staged(package);
        std::string mutated = package;
        mutated[position] = static_cast<char>(mutated[position] ^ 0x01);
        every_flipped_bit_refused =
            every_flipped_bit_refused && staged.ready() &&
            write_whole_file(staged.package(), mutated) &&
            verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) == IntegrityStatus::Mismatch;
    }
    sink.evidence("PLAT-012", every_flipped_bit_refused,
                  "a package corrupted by one flipped bit anywhere is refused before it is trusted");

    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() &&
                       write_whole_file(staged.package(), package.substr(0, package.size() - 1)) &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Mismatch,
                   "a package truncated by one byte is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() && write_whole_file(staged.package(), package + '\0') &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Mismatch,
                   "a package with one appended byte is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() &&
                       write_whole_file(staged.sidecar(),
                                        sha256_text(package + "x") + "  " +
                                            std::string(kUnifiedPackageBasename) + "\n") &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Mismatch,
                   "a sidecar carrying another package's digest is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() && ::unlink(staged.sidecar().c_str()) == 0 &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Missing,
                   "a package deployed without its sidecar is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() && ::unlink(staged.package().c_str()) == 0 &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Missing,
                   "a sidecar whose package is gone is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() &&
                       write_whole_file(staged.sidecar(),
                                        sha256_text(package) + "  nps_other.luax.tns\n") &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::WrongBasename,
                   "a sidecar naming another package is refused");
    }
    {
        TemporaryDeployment staged(package);
        sink.check(staged.ready() && write_whole_file(staged.sidecar(), "not a digest\n") &&
                       verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::Malformed,
                   "a sidecar that is not a digest record is refused");
    }
    // The lengths that straddle the 64 byte hash block and the 1,024 byte read buffer, each one
    // verified whole and refused with its last byte flipped, so neither boundary can pass by
    // accepting everything or by rejecting everything.
    bool every_boundary_length_verifies = true;
    bool every_boundary_length_refuses = true;
    for (std::size_t length : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{64},
                              std::size_t{65}, std::size_t{1023}, std::size_t{1024},
                              std::size_t{1025}, std::size_t{2048}, std::size_t{4096}}) {
        const std::string bytes = patterned_bytes(length);
        {
            TemporaryDeployment whole(bytes);
            every_boundary_length_verifies =
                every_boundary_length_verifies && whole.ready() &&
                verify_package_integrity(whole.package().c_str(), kUnifiedPackageBasename) == IntegrityStatus::Verified;
        }
        if (length == 0)
            continue;
        TemporaryDeployment staged(bytes);
        std::string mutated = bytes;
        mutated[length - 1] = static_cast<char>(mutated[length - 1] ^ 0x01);
        every_boundary_length_refuses =
            every_boundary_length_refuses && staged.ready() &&
            write_whole_file(staged.package(), mutated) &&
            verify_package_integrity(staged.package().c_str(), kUnifiedPackageBasename) == IntegrityStatus::Mismatch;
    }
    sink.check(every_boundary_length_verifies,
               "a package verifies at every length that straddles the block and buffer boundaries");
    sink.check(every_boundary_length_refuses,
               "and its last byte cannot be changed at any of those lengths without being caught");

    // The artifact a check is for is an argument, so a package cannot pass as one it is not. Without
    // this the basename parameter could be ignored and every check above would still pass.
    {
        TemporaryDeployment split(package, kSplitPackageBasename);
        TemporaryDeployment probe(package, kProbePackageBasename);
        sink.check(split.ready() && probe.ready() &&
                       verify_package_integrity(split.package().c_str(),
                                                kSplitPackageBasename) ==
                           IntegrityStatus::Verified &&
                       verify_package_integrity(probe.package().c_str(),
                                                kProbePackageBasename) ==
                           IntegrityStatus::Verified,
                   "the split module and the probe verify under the same one derivation rule");
        sink.check(verify_package_integrity(split.package().c_str(), kUnifiedPackageBasename) ==
                           IntegrityStatus::PathInvalid &&
                       verify_package_integrity(probe.package().c_str(),
                                                kUnifiedPackageBasename) ==
                           IntegrityStatus::PathInvalid,
                   "a package cannot verify as an artifact it is not");
    }

    char split_sidecar[kIntegrityMaxPathLength + 1] = {};
    char probe_sidecar[kIntegrityMaxPathLength + 1] = {};
    sink.check(derive_integrity_sidecar_path("/documents/ndl/nps_split.luax.tns",
                                             kSplitPackageBasename, split_sidecar,
                                             sizeof(split_sidecar)) ==
                       IntegrityStatus::Verified &&
                   std::strcmp(split_sidecar,
                               "/documents/ndl/nps_split.luax.sha256.tns") == 0 &&
                   derive_integrity_sidecar_path("/documents/nps_m0_probe.tns",
                                                 kProbePackageBasename, probe_sidecar,
                                                 sizeof(probe_sidecar)) ==
                       IntegrityStatus::Verified &&
                   std::strcmp(probe_sidecar, "/documents/nps_m0_probe.sha256.tns") == 0,
               "every artifact's sidecar name ends in tns so it can reach the calculator");

    run_device_identity_tests(sink);
}

}
