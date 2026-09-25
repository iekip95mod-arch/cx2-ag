#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

#include "sha256.h"

#include "nps/platform/nspire/device_identity.h"

#include "device_readings.h"
#include "evidence.h"

namespace {

struct Check {
    std::string requirement;
    std::string what;
    bool passed = false;
};

struct Record {
    std::string artifact;
    std::string digest;
    std::string source;
    std::string manifest_header;
    std::vector<Check> checks;
    nps_tools::Readings readings;
    unsigned schema = 0;
    bool seen_artifact = false;
    bool seen_digest = false;
    bool seen_schema = false;
    bool seen_reading = false;
    bool seen_source = false;
    bool seen_manifest_header = false;
};

enum class Refusal {
    Accepted,
    Unreadable,
    Malformed,
    Repeated,
    WrongSchema,
    Incomplete,
    ArtifactMissing,
    DigestMismatch,
    HeaderUnreadable,
    HeaderIncomplete,
    ReadingMissing,
    ReadingUnknown,
};

const char *refusal_name(Refusal refusal) {
    switch (refusal) {
        case Refusal::Accepted:
            return "accepted";
        case Refusal::Unreadable:
            return "the record cannot be read";
        case Refusal::Malformed:
            return "the record has a line this format does not define";
        case Refusal::Repeated:
            return "the record gives one field twice";
        case Refusal::WrongSchema:
            return "the record is a schema this tool does not read";
        case Refusal::Incomplete:
            return "the record names no artifact, no digest, or no check";
        case Refusal::ArtifactMissing:
            return "the artifact the record names is not in the tree";
        case Refusal::DigestMismatch:
            return "the artifact does not hash to what the record recorded";
        case Refusal::HeaderUnreadable:
            return "the manifest header the record names is not in the tree";
        case Refusal::HeaderIncomplete:
            return "the manifest header names no id, artifact or backend";
        case Refusal::ReadingMissing:
            return "the record leaves out a reading the requirements are judged on";
        case Refusal::ReadingUnknown:
            return "the record carries a reading this tool does not judge";
    }
    return "refused";
}

std::vector<std::string> split(const std::string &line, char on) {
    std::vector<std::string> fields;
    std::string current;
    for (char character : line) {
        if (character == on) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    fields.push_back(current);
    return fields;
}

bool hex_digest_of(const std::string &path, std::string *digest) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;
    SHA256_CTX hash;
    sha256_init(&hash);
    char buffer[4096];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        sha256_update(&hash, reinterpret_cast<const BYTE *>(buffer),
                      static_cast<std::size_t>(in.gcount()));
        if (!in)
            break;
    }
    BYTE bytes[SHA256_BLOCK_SIZE];
    sha256_final(&hash, bytes);
    static const char nibbles[] = "0123456789abcdef";
    digest->clear();
    for (BYTE byte : bytes) {
        digest->push_back(nibbles[byte >> 4]);
        digest->push_back(nibbles[byte & 0x0F]);
    }
    return true;
}

Refusal parse(const std::string &path, Record *record) {
    std::ifstream in(path.c_str());
    if (!in)
        return Refusal::Unreadable;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() == 3 && fields[0] == "device") {
            // A field given twice is a record that was edited, and which copy to believe is not
            // this reader's to guess, the same as the corpus reader's rule for a repeated field.
            if (fields[1] == "schema") {
                long schema = 0;
                if (record->seen_schema)
                    return Refusal::Repeated;
                if (!nps_tools::reading_number_of(fields[2], &schema))
                    return Refusal::Malformed;
                record->schema = static_cast<unsigned>(schema);
                record->seen_schema = true;
            } else if (fields[1] == "artifact") {
                if (record->seen_artifact)
                    return Refusal::Repeated;
                record->artifact = fields[2];
                record->seen_artifact = true;
            } else if (fields[1] == "digest") {
                if (record->seen_digest)
                    return Refusal::Repeated;
                record->digest = fields[2];
                record->seen_digest = true;
            } else if (fields[1] == "source") {
                if (record->seen_source)
                    return Refusal::Repeated;
                record->source = fields[2];
                record->seen_source = true;
            } else if (fields[1] == "manifest-header") {
                if (record->seen_manifest_header)
                    return Refusal::Repeated;
                record->manifest_header = fields[2];
                record->seen_manifest_header = true;
            } else {
                return Refusal::Malformed;
            }
            continue;
        }
        // Both line kinds are read wherever they appear and the two grammars are held apart in
        // ingest instead, so a record that puts its schema line last is still refused for the
        // reason it is wrong rather than for the order it was written in.
        if (fields.size() == 4 && fields[0] == "check") {
            if (fields[2] != "pass" && fields[2] != "fail")
                return Refusal::Malformed;
            // Refused here rather than written out, because an id the PRD has no row for reaches
            // traceability as a row it can only report as unknown, and an empty one it cannot name.
            if (!nps_tools::is_requirement_id(fields[1]))
                return Refusal::Malformed;
            Check check;
            check.requirement = fields[1];
            check.passed = fields[2] == "pass";
            check.what = fields[3];
            record->checks.push_back(check);
            continue;
        }
        if (fields.size() == 3 && fields[0] == "reading") {
            record->seen_reading = true;
            const nps_tools::ReadingResult taken =
                nps_tools::assign_reading(&record->readings,
                    fields[1] == "ndless-revision" ? "ndl-revision" : fields[1], fields[2]);
            if (taken == nps_tools::ReadingResult::UnknownName)
                return Refusal::ReadingUnknown;
            if (taken == nps_tools::ReadingResult::Malformed)
                return Refusal::Malformed;
            if (taken == nps_tools::ReadingResult::Repeated)
                return Refusal::Repeated;
            continue;
        }
        return Refusal::Malformed;
    }
    return Refusal::Accepted;
}

// What the requirements mean, held here rather than where the readings were written down, because a
// reader cannot tell a verdict a writer computed from one a person typed beside a number.
//
// PERF-005's two seconds and PERF-006's five are the PRD rows, which call both targets provisional.
// The browse bound is proposed here: PERF-001 asks for "immediate" and names no figure.
//
// PERF-010 has no row and its two readings are recorded rather than judged. That requirement is the
// freeze itself, and benchmarks/BUDGETS.md:238 still says the freeze is the maintainer's to sign off, so a
// passing row would be the requirement asserting its own precondition. When the file no longer says
// proposed, a row added here lights from records already on disk.
const long kBrowsePressBudgetMs = 100;
const long kFirstStepBudgetMs = 2000;
const long kTotalBudgetMs = 5000;

struct Manifest {
    std::string id;
    std::string artifact;
    std::string backend_name;
    std::string backend_version;
};

std::string text_of(long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    return buffer;
}

// The header is generated, so this reads the shape cmake writes rather than parsing C.
bool define_value(const std::string &line, const std::string &name, std::string *value) {
    const std::string prefix = "#define " + name + " \"";
    if (line.size() < prefix.size() || line.compare(0, prefix.size(), prefix) != 0)
        return false;
    const std::size_t close = line.rfind('"');
    if (close == std::string::npos || close < prefix.size())
        return false;
    *value = line.substr(prefix.size(), close - prefix.size());
    return true;
}

Refusal read_manifest(const std::string &path, Manifest *manifest) {
    std::ifstream in(path.c_str());
    if (!in)
        return Refusal::HeaderUnreadable;
    std::string line;
    std::string value;
    while (std::getline(in, line)) {
        if (define_value(line, "NPS_MANIFEST_ID", &value))
            manifest->id = value;
        else if (define_value(line, "NPS_MANIFEST_ARTIFACT", &value))
            manifest->artifact = value;
        else if (define_value(line, "NPS_MANIFEST_BACKEND_NAME", &value))
            manifest->backend_name = value;
        else if (define_value(line, "NPS_MANIFEST_BACKEND_VERSION", &value))
            manifest->backend_version = value;
    }
    if (manifest->id.empty() || manifest->artifact.empty() || manifest->backend_name.empty() ||
        manifest->backend_version.empty())
        return Refusal::HeaderIncomplete;
    return Refusal::Accepted;
}

// The same shortening nps_v4.lua does at line 3029, deliberately duplicated. If the document ever
// changes how it prints the build, every PLAT-010 row fails rather than passing on a stale shape.
std::string manifest_build(const std::string &id) {
    const std::size_t dot = id.rfind('.');
    std::string build = dot == std::string::npos ? id : id.substr(dot + 1);
    if (build.empty())
        build = id;
    if (build.size() > 27)
        build = build.substr(0, 12) + "..." + build.substr(build.size() - 12);
    return build;
}

// What the document prints for this exact build, up to the module count, which is whatever the
// pinned binary declares rather than a number worth a threshold.
std::string expected_manifest_prefix(const Manifest &manifest) {
    return manifest.artifact + " " + manifest_build(manifest.id) + ", " + manifest.backend_name +
           " " + manifest.backend_version + ", ";
}

bool starts_with(const std::string &text, const std::string &prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &text, const std::string &suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The manifest line has to be this build's, and to end in a module count the running binary reported.
bool manifest_line_agrees(const std::string &line, const Manifest &manifest) {
    const std::string prefix = expected_manifest_prefix(manifest);
    if (!starts_with(line, prefix) || !ends_with(line, " modules"))
        return false;
    const std::string count = line.substr(prefix.size(), line.size() - prefix.size() - 8);
    long modules = 0;
    return nps_tools::reading_number_of(count, &modules) && modules > 0;
}

std::vector<Check> judge(const nps_tools::Readings &readings, const Manifest &manifest) {
    std::vector<Check> checks;
    const std::string prefix = expected_manifest_prefix(manifest);
    const std::string expected = prefix.substr(0, prefix.size() - 2);
    const bool manifest_agrees = manifest_line_agrees(readings.manifest_line, manifest);
    const bool backend_named =
        readings.launch_header.find(manifest.backend_name + " " + manifest.backend_version) !=
        std::string::npos;

    // The four raw numbers go through the project's own table rather than being named by hand, so
    // the model, the build and the OS in the sentence are what device_identity.cc reads them as.
    const nps::DeviceIdentity identity = nps::interpret_device_identity(
        static_cast<unsigned>(readings.hardware_type),
        static_cast<unsigned>(readings.hardware_subtype), static_cast<unsigned>(readings.os_index),
        static_cast<unsigned>(readings.ndl_revision), false);

    Check plat001;
    plat001.requirement = "PLAT-001";
    plat001.passed = identity.model == nps::CalculatorModel::CXII &&
                     identity.cas == nps::CasBuild::NonCas && identity.model_agrees_with_os &&
                     readings.ndl_revision > 0;
    plat001.what = std::string("ran on ") + identity.os_name + ", read as model " +
                   nps::calculator_model_name(identity.model) + " and " +
                   nps::cas_build_name(identity.cas) + " build from hardware subtype " +
                   text_of(readings.hardware_subtype) + " and OS index " +
                   text_of(readings.os_index) +
                   (identity.model_agrees_with_os ? ", the two agreeing" : ", the two disagreeing") +
                   ", at ndl revision " + text_of(readings.ndl_revision) +
                   ", against the non-CAS CX II the requirement names";
    checks.push_back(plat001);

    Check plat002;
    plat002.requirement = "PLAT-002";
    plat002.passed = readings.ndl_revision > 0 && readings.outside_writes == 0;
    plat002.what = "loaded as a separate ndl application at revision " +
                   text_of(readings.ndl_revision) + ", with " +
                   text_of(readings.outside_writes) +
                   " files written outside its own directory against a bound of 0";
    checks.push_back(plat002);

    Check plat003;
    plat003.requirement = "PLAT-003";
    plat003.passed = readings.offline_solves > 0 && readings.offline_needed_link == 0;
    plat003.what = text_of(readings.offline_solves) +
                   " solves completed with the link down, of which " +
                   text_of(readings.offline_needed_link) +
                   " needed it, against a bound of 0 and at least one solve";
    checks.push_back(plat003);

    Check plat004;
    plat004.requirement = "PLAT-004";
    plat004.passed = manifest_agrees && !readings.solve.empty();
    plat004.what = "the manifest line reads " + readings.manifest_line + ", against " + expected +
                   " from the build's own manifest header, and a solve on that backend returned " +
                   readings.solve;
    checks.push_back(plat004);

    // Both clauses, and the second is the one a stopwatch cannot answer: the backend call count in
    // the metrics header must not move while the browse keys do, or browsing recomputed something.
    Check perf001;
    perf001.requirement = "PERF-001";
    const long recomputed = readings.giac_calls_after_browse - readings.giac_calls_before_browse;
    // Totals rather than a per-press average, since integer division rounds a press that is over
    // the budget down onto it.
    // A press count whose budget will not fit is judged as a failure rather than multiplied, because
    // the product wraps negative and a clause that cannot be evaluated is not evidence either way.
    const bool budget_fits =
        readings.browse_presses > 0 &&
        readings.browse_presses <= std::numeric_limits<long>::max() / kBrowsePressBudgetMs;
    const long browse_budget_ms = budget_fits ? kBrowsePressBudgetMs * readings.browse_presses : 0;
    perf001.passed = budget_fits && readings.browse_total_ms <= browse_budget_ms && recomputed == 0;
    const std::string against =
        budget_fits ? "against " + text_of(browse_budget_ms) + " ms at a proposed " +
                          text_of(kBrowsePressBudgetMs) +
                          " ms a press that PERF-001 does not give a number for, "
                    : "which is too many presses for a budget at " +
                          text_of(kBrowsePressBudgetMs) +
                          " ms a press to be represented, so the clause cannot be judged, ";
    perf001.what = text_of(readings.browse_total_ms) + " ms across " +
                   text_of(readings.browse_presses) + " step-browse presses, " + against +
                   "and the backend call count went from " +
                   text_of(readings.giac_calls_before_browse) + " to " +
                   text_of(readings.giac_calls_after_browse) + " across them, against a bound of 0";
    checks.push_back(perf001);

    Check perf005;
    perf005.requirement = "PERF-005";
    perf005.passed = readings.first_step_ms <= kFirstStepBudgetMs;
    perf005.what = text_of(readings.first_step_ms) + " ms to the first step against PERF-005's " +
                   text_of(kFirstStepBudgetMs) + " ms, which the PRD calls provisional";
    checks.push_back(perf005);

    Check perf006;
    perf006.requirement = "PERF-006";
    perf006.passed = readings.total_ms <= kTotalBudgetMs;
    perf006.what = text_of(readings.total_ms) + " ms to the answer against PERF-006's " +
                   text_of(kTotalBudgetMs) + " ms, which the PRD calls provisional";
    checks.push_back(perf006);

    Check plat010;
    plat010.requirement = "PLAT-010";
    plat010.passed =
        backend_named && readings.launch_red == 0 && manifest_agrees && !readings.solve.empty();
    plat010.what = "one launch of the bundled unified artifact showed " + readings.launch_header +
                   " with " + text_of(readings.launch_red) +
                   " red lines against a bound of 0, the manifest line read " +
                   readings.manifest_line + " against " + expected +
                   " from the build's own header, and one solve returned " + readings.solve;
    checks.push_back(plat010);

    return checks;
}

// The clause the whole thing rests on. A transcript is a text file anyone can type, so it becomes
// evidence only when the artifact it names is in the tree and hashes to what it recorded.
Refusal ingest(const std::string &record_path, const std::string &artifact_directory,
               Record *record) {
    const Refusal parsed = parse(record_path, record);
    if (parsed != Refusal::Accepted)
        return parsed;
    if (!record->seen_schema || (record->schema < 1 || record->schema > 3))
        return Refusal::WrongSchema;
    // Schema 1 stores verdicts. Later schemas store readings.
    if ((record->schema == 1 && record->seen_reading) ||
        (record->schema >= 2 && !record->checks.empty()))
        return Refusal::Malformed;
    if (!record->seen_artifact || !record->seen_digest || record->artifact.empty() ||
        record->digest.empty())
        return Refusal::Incomplete;
    if (record->schema == 1 && record->checks.empty())
        return Refusal::Incomplete;
    if (record->schema >= 2 && record->manifest_header.empty())
        return Refusal::Incomplete;
    if (record->schema >= 2 && !nps_tools::first_missing_reading(record->readings).empty())
        return Refusal::ReadingMissing;

    std::string actual;
    if (!hex_digest_of(artifact_directory + "/" + record->artifact, &actual))
        return Refusal::ArtifactMissing;
    if (actual != record->digest)
        return Refusal::DigestMismatch;

    if (record->schema >= 2) {
        Manifest manifest;
        const Refusal read =
            read_manifest(artifact_directory + "/" + record->manifest_header, &manifest);
        if (read != Refusal::Accepted)
            return read;
        record->checks = judge(record->readings, manifest);
    }
    return Refusal::Accepted;
}

bool append_evidence(const std::string &path, const Record &record, std::string *error) {
    const std::string group = record.source.empty() ? "device" : "device " + record.source;
    std::vector<std::string> rows;
    // The artifact names itself in the row, because one requirement can have a record per package and
    // the reader has to be able to tell which one this was.
    for (const Check &check : record.checks)
        rows.push_back("evidence\t" + check.requirement + (check.passed ? "\tpass\t" : "\tfail\t") +
                       group + "\t" + record.artifact + ", " + check.what);
    return nps_tools::append_evidence(path, "adapter", group, rows, error);
}

// The two suffixes a record can carry, in the order they are swept, so the evidence file comes out
// the same whatever order the directory hands the names back in.
const char *const kRecordSuffixes[] = {".offline-audit.txt", ".device-run.txt"};

// Read at run time rather than globbed at configure time. A glob only re-runs when cmake does, so a
// record written after the last configure went unread and its requirements reported unmet, which is
// an evidence gate that lies by omission rather than one that is merely out of date.
std::vector<std::string> records_in(const std::string &directory) {
    std::vector<std::string> found;
    for (const char *suffix : kRecordSuffixes) {
        std::vector<std::string> matching;
        DIR *open = ::opendir(directory.c_str());
        if (open == nullptr)
            return found;
        const std::string wanted = suffix;
        while (const dirent *entry = ::readdir(open)) {
            const std::string name = entry->d_name;
            if (name.size() > wanted.size() &&
                name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0)
                matching.push_back(name);
        }
        ::closedir(open);
        std::sort(matching.begin(), matching.end());
        for (const std::string &name : matching)
            found.push_back(name);
    }
    return found;
}

std::string temporary_directory() {
    char pattern[] = "/tmp/nps_device_evidence_XXXXXX";
    const char *made = ::mkdtemp(pattern);
    return made ? std::string(made) : std::string();
}

bool write_file(const std::string &path, const std::string &contents) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out)
        return false;
    out << contents;
    return out.good();
}

int failures = 0;

void expect(bool ok, const char *what) {
    if (!ok)
        ++failures;
    std::cout << "device evidence selftest: " << (ok ? "ok   " : "FAIL ") << what << "\n";
}

std::string record_text(const std::string &artifact, const std::string &digest) {
    return "device\tschema\t1\ndevice\tsource\thandheld\ndevice\tartifact\t" + artifact +
           "\ndevice\tdigest\t" + digest +
           "\ncheck\tPLAT-001\tpass\ta non-CAS CX II under the ndl toolchain\n";
}

const char *const kSelftestManifestId =
    "stepcas.unified.inputs-sha256.c0fe750fa73c5a04726a0be2b644de8243c3b63a1c18de0762d977f561a544fb";

std::string manifest_header_text() {
    return std::string("#define NPS_MANIFEST_ID \"") + kSelftestManifestId +
           "\"\n#define NPS_MANIFEST_ARTIFACT \"unified\"\n"
           "#define NPS_MANIFEST_BACKEND_NAME \"Giac\"\n"
           "#define NPS_MANIFEST_BACKEND_VERSION \"1.9.0\"\n";
}

// The index of the device the project targets, found in the table rather than written down, so this
// fails if the entry is ever dropped instead of silently judging some other calculator.
unsigned target_os_index() {
    for (unsigned index = 0; index < nps::kOsVersionIndexCount; ++index) {
        if (std::strcmp(nps::os_version_name(index), "6.4.0.74 non-CAS CX II") == 0)
            return index;
    }
    return nps::kOsVersionIndexCount;
}

nps_tools::Readings selftest_readings() {
    Manifest manifest;
    manifest.id = kSelftestManifestId;
    manifest.artifact = "unified";
    manifest.backend_name = "Giac";
    manifest.backend_version = "1.9.0";
    nps_tools::Readings readings;
    readings.manifest_line = expected_manifest_prefix(manifest) + "12 modules";
    readings.launch_header = "Giac 1.9.0 :";
    readings.solve = "x = 4";
    readings.hardware_type = 1;
    readings.hardware_subtype = 2;
    readings.os_index = target_os_index();
    readings.ndl_revision = 2005;
    readings.outside_writes = 0;
    readings.offline_solves = 3;
    readings.offline_needed_link = 0;
    readings.launch_red = 0;
    readings.browse_presses = 20;
    readings.browse_total_ms = 340;
    readings.giac_calls_before_browse = 6;
    readings.giac_calls_after_browse = 6;
    readings.first_step_ms = 40;
    readings.total_ms = 260;
    readings.free_kb = 19609;
    readings.contiguous_kb = 13120;
    return readings;
}

std::string reading_record_text(const std::string &artifact, const std::string &digest,
                            const nps_tools::Readings &readings) {
    std::string text = "device\tschema\t3\ndevice\tsource\thandheld run\ndevice\tartifact\t" +
                       artifact + "\ndevice\tdigest\t" + digest +
                       "\ndevice\tmanifest-header\tnps_manifest_config.h\n";
    for (const nps_tools::TextReading &reading : nps_tools::text_readings())
        text += std::string("reading\t") + reading.name + "\t" + readings.*reading.field + "\n";
    for (const nps_tools::NumberReading &reading : nps_tools::number_readings())
        text +=
            std::string("reading\t") + reading.name + "\t" + text_of(readings.*reading.field) + "\n";
    return text;
}

bool verdict_for(const std::vector<Check> &checks, const std::string &requirement, bool *passed) {
    for (const Check &check : checks) {
        if (check.requirement == requirement) {
            *passed = check.passed;
            return true;
        }
    }
    return false;
}

std::string what_for(const std::vector<Check> &checks, const std::string &requirement) {
    for (const Check &check : checks) {
        if (check.requirement == requirement)
            return check.what;
    }
    return std::string();
}

// Every threshold driven past on both sides. A judge that has only ever returned pass cannot be told
// from one that returns pass whatever the readings say.
void selftest_verdicts() {
    Manifest manifest;
    manifest.id = kSelftestManifestId;
    manifest.artifact = "unified";
    manifest.backend_name = "Giac";
    manifest.backend_version = "1.9.0";
    const nps_tools::Readings clean = selftest_readings();

    std::vector<Check> checks = judge(clean, manifest);
    bool passed = false;
    bool all = checks.size() == 8;
    for (const Check &check : checks)
        all = all && check.passed;
    expect(all, "a clean run passes all eight rows");
    expect(!verdict_for(checks, "PERF-010", &passed),
           "and PERF-010 is not among them, because BUDGETS.md still calls the budgets proposed");

    expect(target_os_index() < nps::kOsVersionIndexCount,
           "the OS the project targets, 6.4.0.74 non-CAS CX II, is in the identity table");

    nps_tools::Readings wrong_device = clean;
    wrong_device.hardware_subtype = 0;
    checks = judge(wrong_device, manifest);
    expect(verdict_for(checks, "PLAT-001", &passed) && !passed,
           "a model register that does not read as a CX II fails PLAT-001");
    // By value, because the first draft of this sentence said the two agreed whatever they did.
    expect(what_for(checks, "PLAT-001").find("the two disagreeing") != std::string::npos,
           "and the sentence says the register and the OS disagreed rather than that they agreed");
    expect(what_for(judge(clean, manifest), "PLAT-001").find("the two agreeing") !=
               std::string::npos,
           "while the clean run's sentence says they agreed");

    nps_tools::Readings cas_device = clean;
    cas_device.os_index = target_os_index() + 2;
    checks = judge(cas_device, manifest);
    expect(std::strcmp(nps::os_version_name(static_cast<unsigned>(cas_device.os_index)),
                       "6.4.0.74 CAS CX II") == 0 &&
               verdict_for(checks, "PLAT-001", &passed) && !passed,
           "the CAS build of the same OS fails PLAT-001, which is the clause the requirement adds");

    nps_tools::Readings wrote_outside = clean;
    wrote_outside.outside_writes = 1;
    checks = judge(wrote_outside, manifest);
    expect(verdict_for(checks, "PLAT-002", &passed) && !passed,
           "a run that wrote outside its own directory fails PLAT-002");

    nps_tools::Readings needed_link = clean;
    needed_link.offline_needed_link = 1;
    checks = judge(needed_link, manifest);
    expect(verdict_for(checks, "PLAT-003", &passed) && !passed,
           "a solve that needed the link fails PLAT-003");

    nps_tools::Readings other_build = clean;
    other_build.manifest_line = "unified 0123456789ab, Giac 1.9.0, 12 modules";
    checks = judge(other_build, manifest);
    expect(verdict_for(checks, "PLAT-004", &passed) && !passed,
           "a manifest line from a different build fails PLAT-004");
    expect(verdict_for(checks, "PLAT-010", &passed) && !passed,
           "and fails PLAT-010, which is the second copy on the handheld caught");

    nps_tools::Readings no_answer = clean;
    no_answer.solve = "";
    checks = judge(no_answer, manifest);
    expect(verdict_for(checks, "PLAT-010", &passed) && !passed,
           "a launch whose solve returned nothing fails PLAT-010");

    nps_tools::Readings red_line = clean;
    red_line.launch_red = 1;
    checks = judge(red_line, manifest);
    expect(verdict_for(checks, "PLAT-010", &passed) && !passed,
           "a red line on the launch header fails PLAT-010");

    nps_tools::Readings slow_browse = clean;
    slow_browse.browse_total_ms = kBrowsePressBudgetMs * clean.browse_presses + clean.browse_presses;
    checks = judge(slow_browse, manifest);
    expect(verdict_for(checks, "PERF-001", &passed) && !passed,
           "browsing a millisecond a press over the proposed budget fails PERF-001");
    slow_browse.browse_total_ms = kBrowsePressBudgetMs * clean.browse_presses + 1;
    checks = judge(slow_browse, manifest);
    expect(verdict_for(checks, "PERF-001", &passed) && !passed,
           "and so does one millisecond over it in total, which a per-press average rounds away");
    slow_browse.browse_total_ms = kBrowsePressBudgetMs * clean.browse_presses;
    checks = judge(slow_browse, manifest);
    expect(verdict_for(checks, "PERF-001", &passed) && passed,
           "while exactly on it passes, so the bound is inclusive");

    // A press count that cannot have its budget represented. The multiplication wrapped negative and
    // reported a fail for the wrong reason, and one digit fewer wrapped positive and passed.
    nps_tools::Readings absurd_browse = clean;
    absurd_browse.browse_presses = std::numeric_limits<long>::max();
    checks = judge(absurd_browse, manifest);
    expect(verdict_for(checks, "PERF-001", &passed) && !passed &&
               what_for(checks, "PERF-001").find("cannot be judged") != std::string::npos,
           "a press count too large for its budget fails PERF-001 and says the clause cannot be judged");
    long overflowing = 0;
    expect(!nps_tools::reading_number_of("99999999999999999999", &overflowing),
           "a reading of twenty nines is refused rather than wrapped into a smaller number");
    expect(nps_tools::reading_number_of("9223372036854775807", &overflowing) &&
               overflowing == std::numeric_limits<long>::max(),
           "and the largest number that does fit is still read");

    nps_tools::Readings recomputing = clean;
    recomputing.giac_calls_after_browse = clean.giac_calls_before_browse + 1;
    checks = judge(recomputing, manifest);
    expect(verdict_for(checks, "PERF-001", &passed) && !passed,
           "one backend call made while browsing fails PERF-001, whatever the timing said");
    expect(what_for(checks, "PERF-001").find(" from 6 to 7 across them") != std::string::npos,
           "and the sentence names both counts rather than saying browsing recomputed");

    nps_tools::Readings slow_first = clean;
    slow_first.first_step_ms = kFirstStepBudgetMs + 1;
    checks = judge(slow_first, manifest);
    expect(verdict_for(checks, "PERF-005", &passed) && !passed,
           "one millisecond over the first-step budget fails PERF-005");
    slow_first.first_step_ms = kFirstStepBudgetMs;
    checks = judge(slow_first, manifest);
    expect(verdict_for(checks, "PERF-005", &passed) && passed,
           "and exactly on it passes, so the bound is inclusive");

    nps_tools::Readings slow_total = clean;
    slow_total.total_ms = kTotalBudgetMs + 1;
    checks = judge(slow_total, manifest);
    expect(verdict_for(checks, "PERF-006", &passed) && !passed,
           "one millisecond over the total budget fails PERF-006");

    expect(manifest_build(kSelftestManifestId) == "c0fe750fa73c...77f561a544fb",
           "a build id longer than 27 characters shortens the way the document shortens it");
    expect(manifest_build("stepcas.unified.short") == "short", "and a short one is left alone");
}

// A sweep with no records proves nothing, so it answers with its own code rather than with success.
constexpr int kNothingToSweep = 77;

// An accepted record whose rows never reached the file is a broken file rather than a bad record.
constexpr int kEvidenceNotWritten = 3;

// What became of one record. A refusal is about the record and an unwritten append is about the file.
enum class Ingested {
    Written,
    Refused,
    NotWritten,
};

int sweep(const std::string &directory);

// Every refusal is made to fire before the acceptance is believed, because a gate that has only ever
// accepted is indistinguishable from no gate.
int selftest() {
    selftest_verdicts();
    const std::string directory = temporary_directory();
    if (directory.empty()) {
        std::cout << "device evidence selftest: no temporary directory\n";
        return 1;
    }
    const std::string artifact = directory + "/nps_nspire.luax.tns";
    const std::string record_path = directory + "/record.txt";
    if (!write_file(artifact, "the packaged bytes, whatever they are")) {
        std::cout << "device evidence selftest: could not stage an artifact\n";
        return 1;
    }
    std::string digest;
    if (!hex_digest_of(artifact, &digest)) {
        std::cout << "device evidence selftest: could not hash the staged artifact\n";
        return 1;
    }

    Record record;
    expect(ingest(directory + "/absent.txt", directory, &record) == Refusal::Unreadable,
           "a record that is not there is refused");

    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", digest));
    expect(ingest(record_path, directory, &record) == Refusal::Accepted &&
               record.checks.size() == 1 && record.checks[0].requirement == "PLAT-001",
           "a record naming an artifact that hashes as recorded is accepted");

    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", std::string(64, 'a')));
    expect(ingest(record_path, directory, &record) == Refusal::DigestMismatch,
           "a typed transcript whose digest is invented is refused");

    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", digest));
    write_file(artifact, "the packaged bytes, whatever they are.");
    expect(ingest(record_path, directory, &record) == Refusal::DigestMismatch,
           "a record that no longer matches the artifact beside it is refused");
    write_file(artifact, "the packaged bytes, whatever they are");

    record = Record();
    write_file(record_path, record_text("nps_split.luax.tns", digest));
    expect(ingest(record_path, directory, &record) == Refusal::ArtifactMissing,
           "a record naming an artifact that is not in the tree is refused");

    record = Record();
    write_file(record_path, "device\tschema\t4\ndevice\tartifact\tx\ndevice\tdigest\ty\n"
                            "check\tPLAT-001\tpass\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::WrongSchema,
           "a record written to a schema this tool does not read is refused");

    record = Record();
    write_file(record_path, "device\tsource\thandheld\ncheck\tPLAT-001\tpass\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::WrongSchema,
           "a record with no schema line at all is refused rather than assumed current");

    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tartifact\tnps_nspire.luax.tns\n"
                            "device\tdigest\t" + digest + "\n");
    expect(ingest(record_path, directory, &record) == Refusal::Incomplete,
           "a record carrying no check is refused rather than counted as nothing found");

    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tinvented\tfield\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "a field this format does not define is refused rather than skipped");

    // Read with the same digit scanner as a reading, so what follows the digits is not dropped.
    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", digest)
                                .replace(std::string("device\tschema\t1\n").size() - 2, 1, "1x"));
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "a schema line with something after the number is refused rather than read as the number");
    record = Record();
    write_file(record_path, "device\tschema\t 1\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ncheck\tPLAT-001\tpass\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "and so is one with a space before it");

    // One field twice is an edited record, and the reader has no business choosing a copy.
    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", digest) +
                                "device\tartifact\tnps_nspire.luax.tns\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "a record naming its artifact twice is refused even when both copies agree");
    record = Record();
    write_file(record_path, "device\tschema\t1\n" + record_text("nps_nspire.luax.tns", digest));
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "and so is one saying its schema twice");
    // Emptiness is not the question, having been given the field is. The first copy of these two was
    // allowed to be empty, which let the second through.
    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tsource\t\ndevice\tsource\thandheld\n"
                            "device\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ncheck\tPLAT-001\tpass\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "a record giving its source twice is refused even when the first copy is empty");
    record = Record();
    write_file(record_path, "device\tschema\t2\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ndevice\tmanifest-header\t\n"
                                         "device\tmanifest-header\tnps_manifest_config.h\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "and so is one giving its manifest header twice on the same terms");

    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ncheck\tPLAT-001\tmaybe\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "a check that is neither pass nor fail is refused rather than read as one");

    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ncheck\t\tpass\tthe run nobody can name\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "a check naming no requirement is refused rather than written into the evidence file");
    record = Record();
    write_file(record_path, "device\tschema\t1\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" +
                                digest + "\ncheck\tplat-001\tpass\tanything\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "and so is one naming something that is not a requirement id");

    record = Record();
    const std::string failing =
        "device\tschema\t1\ndevice\tartifact\tnps_nspire.luax.tns\ndevice\tdigest\t" + digest +
        "\ncheck\tPLAT-003\tfail\tthe solve did not finish with the link down\n";
    write_file(record_path, failing);
    expect(ingest(record_path, directory, &record) == Refusal::Accepted &&
               !record.checks[0].passed,
           "a device run that failed is ingested as a failure rather than dropped");

    // Current records carry measurements for the reader to judge.
    write_file(directory + "/nps_manifest_config.h", manifest_header_text());
    const nps_tools::Readings clean = selftest_readings();

    record = Record();
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, clean));
    expect(ingest(record_path, directory, &record) == Refusal::Accepted &&
               record.checks.size() == 8 && record.checks[0].requirement == "PLAT-001",
           "a schema 3 record of readings is ingested as the eight rows this tool judges");

    std::string legacy = reading_record_text("nps_nspire.luax.tns", digest, clean);
    legacy.replace(0, std::string("device\tschema\t3\n").size(), "device\tschema\t2\n");
    const std::size_t revision_name = legacy.find("reading\tndl-revision\t");
    legacy.replace(revision_name, std::string("reading\tndl-revision\t").size(),
                   "reading\tndless-revision\t");
    record = Record();
    write_file(record_path, legacy);
    expect(ingest(record_path, directory, &record) == Refusal::Accepted &&
               record.readings.ndl_revision == clean.ndl_revision,
           "retained records preserve their original revision field name");
    record = Record();
    write_file(record_path, legacy + "reading\tndl-revision\t2005\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "legacy and current names cannot supply the same reading twice");

    record = Record();
    nps_tools::Readings over_budget = clean;
    over_budget.total_ms = kTotalBudgetMs + 1;
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, over_budget));
    bool perf006 = true;
    expect(ingest(record_path, directory, &record) == Refusal::Accepted &&
               verdict_for(record.checks, "PERF-006", &perf006) && !perf006,
           "a reading over budget in the record comes out as a failed row, not a refused one");

    record = Record();
    std::string missing = reading_record_text("nps_nspire.luax.tns", digest, clean);
    const std::size_t drop = missing.find("reading\ttotal-ms\t");
    missing.erase(drop, missing.find('\n', drop) + 1 - drop);
    write_file(record_path, missing);
    expect(ingest(record_path, directory, &record) == Refusal::ReadingMissing,
           "a measurement record leaving out one reading is refused rather than judged without it");

    record = Record();
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, clean) +
                                "reading\tbattery\tfull\n");
    expect(ingest(record_path, directory, &record) == Refusal::ReadingUnknown,
           "a reading this tool does not judge is refused rather than carried along unread");

    record = Record();
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, clean) +
                                "reading\ttotal-ms\t" + text_of(kTotalBudgetMs + 1) + "\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "a reading given twice is refused rather than the later copy being the one judged");
    record = Record();
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, clean) +
                                "reading\tsolve\tx = 5\n");
    expect(ingest(record_path, directory, &record) == Refusal::Repeated,
           "and so is a text reading given twice");

    record = Record();
    write_file(record_path, reading_record_text("nps_nspire.luax.tns", digest, clean) +
                                "check\tPLAT-001\tpass\tsomebody else decided\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "a verdict smuggled into a measurement record is refused, which is the whole point of it");

    record = Record();
    write_file(record_path, record_text("nps_nspire.luax.tns", digest) +
                                "reading\ttotal-ms\t260\n");
    expect(ingest(record_path, directory, &record) == Refusal::Malformed,
           "and a reading in a schema 1 record is refused too, so the two grammars stay apart");

    record = Record();
    std::string headerless = reading_record_text("nps_nspire.luax.tns", digest, clean);
    const std::size_t header_at = headerless.find("device\tmanifest-header\t");
    headerless.erase(header_at, headerless.find('\n', header_at) + 1 - header_at);
    write_file(record_path, headerless);
    expect(ingest(record_path, directory, &record) == Refusal::Incomplete,
           "a schema 2 record naming no manifest header is refused, since nothing pins the build");

    record = Record();
    std::string elsewhere = reading_record_text("nps_nspire.luax.tns", digest, clean);
    write_file(record_path, elsewhere);
    write_file(directory + "/nps_manifest_config.h",
               "#define NPS_MANIFEST_ID \"stepcas.unified.abc\"\n");
    expect(ingest(record_path, directory, &record) == Refusal::HeaderIncomplete,
           "a manifest header naming an id but no backend is refused rather than half read");
    write_file(directory + "/nps_manifest_config.h", manifest_header_text());

    // The sweep, on a directory of its own so the ordering can be seen rather than argued about.
    const std::string swept = temporary_directory();
    if (swept.empty()) {
        expect(false, "a second temporary directory could be made");
    } else {
        expect(records_in(swept).empty(),
               "an empty directory sweeps to no records rather than to a failure");
        expect(sweep(swept) == kNothingToSweep,
               "and an empty sweep answers nothing to sweep rather than success");
        write_file(swept + "/notes.txt", "not a record");
        write_file(swept + "/device-run.txt", "a name that is only the suffix");
        expect(records_in(swept).empty(),
               "a file that is not a record, and one named only for the suffix, are both passed by");

        write_file(swept + "/nps_split.offline-audit.txt", "");
        write_file(swept + "/nps_nspire.offline-audit.txt", "");
        write_file(swept + "/nps_nspire.device-run.txt", "");
        const std::vector<std::string> found = records_in(swept);
        expect(found.size() == 3 && found[0] == "nps_nspire.offline-audit.txt" &&
                   found[1] == "nps_split.offline-audit.txt" &&
                   found[2] == "nps_nspire.device-run.txt",
               "audits come before runs and each group is sorted, so the evidence file is stable");
        expect(found[0] != found[2],
               "and one module carrying both kinds is two records rather than a collision");
        expect(sweep(swept) == 1,
               "while records the gate refuses come back as refused rather than as nothing to sweep");
    }

    // The gate and the evidence file fail for different reasons and answer with different codes.
    const std::string appended = temporary_directory();
    if (appended.empty()) {
        expect(false, "a third temporary directory could be made");
    } else {
        const std::string packaged = appended + "/nps_nspire.luax.tns";
        write_file(packaged, "the packaged bytes, whatever they are");
        std::string packaged_digest;
        hex_digest_of(packaged, &packaged_digest);
        write_file(appended + "/nps_nspire.offline-audit.txt",
                   record_text("nps_nspire.luax.tns", packaged_digest));

        const char *inherited = getenv("NPS_EVIDENCE");
        const bool had_evidence = inherited != nullptr;
        const std::string restore = had_evidence ? inherited : "";

        setenv("NPS_EVIDENCE", (appended + "/no-such-directory/evidence.txt").c_str(), 1);
        expect(sweep(appended) == kEvidenceNotWritten,
               "a record the gate accepted and the file could not take is not counted as refused");

        const std::string evidence = appended + "/evidence.txt";
        write_file(evidence, "group\tadapter\n");
        setenv("NPS_EVIDENCE", evidence.c_str(), 1);
        expect(sweep(appended) == 0, "and the same record passes once the file can take its rows");
        std::ifstream reading(evidence.c_str());
        std::string line;
        bool carried = false;
        while (std::getline(reading, line)) {
            if (line.find("PLAT-001") != std::string::npos)
                carried = true;
        }
        expect(carried, "which is a real append rather than a write that was quietly passed over");

        if (had_evidence)
            setenv("NPS_EVIDENCE", restore.c_str(), 1);
        else
            unsetenv("NPS_EVIDENCE");
    }

    std::cout << "device evidence: " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}

Ingested ingest_one(const std::string &record_path, const std::string &directory) {
    Record record;
    const Refusal refusal = ingest(record_path, directory, &record);
    if (refusal != Refusal::Accepted) {
        std::cout << "device evidence: refused, " << refusal_name(refusal) << ": " << record_path
                  << "\n";
        return Ingested::Refused;
    }
    std::cout << "device evidence: " << record.checks.size() << " checks from " << record.artifact
              << ", digest matches the artifact in the tree\n";

    const char *evidence_path = getenv("NPS_EVIDENCE");
    if (evidence_path != nullptr) {
        std::string error;
        if (!append_evidence(evidence_path, record, &error)) {
            std::cout << "device evidence: not written, " << error << "\n";
            return Ingested::NotWritten;
        }
    }
    return Ingested::Written;
}

int exit_code_for(Ingested outcome) {
    switch (outcome) {
        case Ingested::Written:
            return 0;
        case Ingested::Refused:
            return 1;
        case Ingested::NotWritten:
            return kEvidenceNotWritten;
    }
    return 1;
}

int sweep(const std::string &directory) {
    const std::vector<std::string> records = records_in(directory);
    if (records.empty()) {
        // Success here is the one answer indistinguishable from a suite that covers the device rows.
        std::cout << "device evidence: no records under " << directory
                  << ", so the device requirements have no evidence in this tree\n";
        return kNothingToSweep;
    }
    int refused = 0;
    int unwritten = 0;
    for (const std::string &name : records) {
        switch (ingest_one(directory + "/" + name, directory)) {
            case Ingested::Refused:
                ++refused;
                break;
            case Ingested::NotWritten:
                ++unwritten;
                break;
            case Ingested::Written:
                break;
        }
    }
    std::cout << "device evidence: " << records.size() << " records swept from " << directory
              << ", " << refused << " refused, " << unwritten << " not written\n";
    // A file that cannot take rows voids every record it should have carried, so it outranks one.
    if (unwritten != 0)
        return kEvidenceNotWritten;
    return refused == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0)
        return selftest();
    if (argc == 3 && std::strcmp(argv[1], "--sweep") == 0)
        return sweep(argv[2]);
    if (argc != 3) {
        std::cout << "usage: nps_device_evidence <record> <artifact-directory>\n"
                     "       nps_device_evidence --sweep <artifact-directory>\n"
                     "       nps_device_evidence --selftest\n";
        return 2;
    }
    return exit_code_for(ingest_one(argv[1], argv[2]));
}
