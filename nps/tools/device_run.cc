#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "sha256.h"

#include "device_readings.h"

// Writes down what a handheld run came back with, and nothing else. No threshold lives here and no
// verdict is computed here: nps_device_evidence reads the record and decides, because a reader
// cannot tell a verdict a writer computed from one a person typed, and this way it never has to.
//
// The procedure the readings come from, in order.
//
// 1. Build the device tree and push its artefacts to the handheld. Do not rebuild afterwards. The
//    record binds to one build twice, through the artefact digest and through the manifest build id,
//    and a rebuild between the push and this tool invalidates both.
// 2. List the handheld's filesystem with mcp__nspire__device before the run, and again after, so
//    PLAT-002's count of what changed outside /nps is a comparison rather than an impression.
// 3. Reset the handheld before reading free memory. A heap reading taken after other work has run is
//    a reading of that work.
// 4. Launch the document once. PLAT-010 wants three readings off that one launch: the launch header,
//    the !m manifest line, and one !s solve that returns an answer.
// 5. Run this tool against the same build directory whose artefacts went to the handheld, then run
//    nps_device_evidence on what it wrote to see the rows.

namespace {

enum class Refusal {
    Accepted,
    ReadingUnknown,
    ReadingMissing,
    ReadingMalformed,
    ReadingRepeated,
    ArtifactMissing,
    HeaderMissing,
    RecordUnwritable,
};

const char *refusal_name(Refusal refusal) {
    switch (refusal) {
        case Refusal::Accepted:
            return "accepted";
        case Refusal::ReadingUnknown:
            return "a reading this tool does not take was passed";
        case Refusal::ReadingMissing:
            return "a reading the record needs was not passed";
        case Refusal::ReadingMalformed:
            return "a reading holds a tab, a newline, or something that is not a number";
        case Refusal::ReadingRepeated:
            return "a reading was passed twice";
        case Refusal::ArtifactMissing:
            return "the artifact named is not in the build directory";
        case Refusal::HeaderMissing:
            return "the manifest header named is not in the build directory";
        case Refusal::RecordUnwritable:
            return "the record could not be written";
    }
    return "refused";
}

std::string text_of(long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    return buffer;
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

bool file_exists(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return static_cast<bool>(in);
}

std::string record_text(const std::string &artifact, const std::string &digest,
                        const std::string &header, const nps_tools::Readings &readings) {
    std::string text = "device\tschema\t3\ndevice\tsource\thandheld run\ndevice\tartifact\t" +
                       artifact + "\ndevice\tdigest\t" + digest + "\ndevice\tmanifest-header\t" +
                       header + "\n";
    for (const nps_tools::TextReading &reading : nps_tools::text_readings())
        text += std::string("reading\t") + reading.name + "\t" + readings.*reading.field + "\n";
    for (const nps_tools::NumberReading &reading : nps_tools::number_readings())
        text += std::string("reading\t") + reading.name + "\t" +
                text_of(readings.*reading.field) + "\n";
    return text;
}

bool write_contents(int descriptor, const std::string &contents) {
    FILE *out = ::fdopen(descriptor, "wb");
    if (!out) {
        ::close(descriptor);
        return false;
    }
    const bool written = std::fwrite(contents.data(), 1, contents.size(), out) == contents.size();
    const bool closed = std::fclose(out) == 0;
    return written && closed;
}

bool write_file(const std::string &path, const std::string &contents) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    return descriptor >= 0 && write_contents(descriptor, contents);
}

std::string record_path_for(const std::string &directory, const std::string &artifact) {
    return directory + "/" + artifact + ".XXXXXX.device-run.txt";
}

bool publish_record(int descriptor, const std::string &staged, const std::string &path,
                    const std::string &contents) {
    const bool written = write_contents(descriptor, contents);
    // Publish complete records without replacing any earlier run.
    const bool published = written && ::link(staged.c_str(), path.c_str()) == 0;
    ::unlink(staged.c_str());
    return published;
}

bool write_record(const std::string &directory, const std::string &artifact,
                  const std::string &digest, const std::string &header,
                  const nps_tools::Readings &readings, std::string *path) {
    *path = record_path_for(directory, artifact) + ".pending";
    std::vector<char> name(path->begin(), path->end());
    name.push_back('\0');
    const int descriptor =
        ::mkstemps(name.data(), static_cast<int>(std::strlen(".device-run.txt.pending")));
    if (descriptor < 0)
        return false;
    const std::string staged = name.data();
    *path = staged.substr(0, staged.size() - std::strlen(".pending"));
    return publish_record(descriptor, staged, *path, record_text(artifact, digest, header, readings));
}

Refusal take_readings(int argc, char **argv, int from, nps_tools::Readings *readings,
                      std::string *which) {
    for (int i = from; i < argc; i += 2) {
        const std::string name = argv[i];
        if (i + 1 >= argc) {
            *which = name;
            return Refusal::ReadingMissing;
        }
        if (name.compare(0, 2, "--") != 0) {
            *which = name;
            return Refusal::ReadingUnknown;
        }
        const nps_tools::ReadingResult taken =
            nps_tools::assign_reading(readings, name.substr(2), argv[i + 1]);
        if (taken == nps_tools::ReadingResult::UnknownName) {
            *which = name;
            return Refusal::ReadingUnknown;
        }
        if (taken == nps_tools::ReadingResult::Malformed) {
            *which = name;
            return Refusal::ReadingMalformed;
        }
        if (taken == nps_tools::ReadingResult::Repeated) {
            *which = name;
            return Refusal::ReadingRepeated;
        }
    }
    *which = nps_tools::first_missing_reading(*readings);
    return which->empty() ? Refusal::Accepted : Refusal::ReadingMissing;
}

int failures = 0;

void expect(bool ok, const char *what) {
    if (!ok)
        ++failures;
    std::cout << "device run selftest: " << (ok ? "ok   " : "FAIL ") << what << "\n";
}

std::string temporary_directory() {
    char pattern[] = "/tmp/nps_device_run_XXXXXX";
    const char *made = ::mkdtemp(pattern);
    return made ? std::string(made) : std::string();
}

std::vector<char *> argv_of(const std::vector<std::string> &arguments) {
    static std::vector<std::string> held;
    held = arguments;
    std::vector<char *> pointers;
    for (std::string &argument : held)
        pointers.push_back(&argument[0]);
    return pointers;
}

// Every reading in the table, so the selftest cannot pass by knowing a shorter list than the writer.
std::vector<std::string> complete_arguments() {
    std::vector<std::string> arguments;
    for (const nps_tools::TextReading &reading : nps_tools::text_readings()) {
        arguments.push_back(std::string("--") + reading.name);
        arguments.push_back("a reading");
    }
    for (const nps_tools::NumberReading &reading : nps_tools::number_readings()) {
        arguments.push_back(std::string("--") + reading.name);
        arguments.push_back("7");
    }
    return arguments;
}

std::size_t record_count_in(const std::string &directory) {
    std::size_t records = 0;
    DIR *opened = ::opendir(directory.c_str());
    if (opened) {
        while (const dirent *entry = ::readdir(opened)) {
            const std::string name = entry->d_name;
            const std::string suffix = ".device-run.txt";
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                ++records;
        }
        ::closedir(opened);
    }
    return records;
}

int selftest() {
    nps_tools::Readings readings;
    std::string which;
    std::vector<std::string> arguments = complete_arguments();
    std::vector<char *> pointers = argv_of(arguments);
    expect(take_readings(static_cast<int>(pointers.size()), pointers.data(), 0, &readings,
                         &which) == Refusal::Accepted,
           "every reading in the table together is accepted");

    for (std::size_t drop = 0; drop < nps_tools::reading_count(); ++drop) {
        std::vector<std::string> short_arguments;
        for (std::size_t i = 0; i < arguments.size(); i += 2) {
            if (i / 2 == drop)
                continue;
            short_arguments.push_back(arguments[i]);
            short_arguments.push_back(arguments[i + 1]);
        }
        readings = nps_tools::Readings();
        pointers = argv_of(short_arguments);
        const Refusal refusal = take_readings(static_cast<int>(pointers.size()), pointers.data(), 0,
                                              &readings, &which);
        if (refusal != Refusal::ReadingMissing || which != arguments[drop * 2].substr(2)) {
            expect(false, "leaving out any one reading is refused, and the refusal names it");
            return failures;
        }
    }
    expect(true, "leaving out any one reading is refused, and the refusal names it");

    readings = nps_tools::Readings();
    arguments = {"--battery", "full"};
    pointers = argv_of(arguments);
    expect(take_readings(2, pointers.data(), 0, &readings, &which) == Refusal::ReadingUnknown,
           "a reading this tool does not take is refused rather than ignored");

    readings = nps_tools::Readings();
    arguments = {"--solve", "x =\t4"};
    pointers = argv_of(arguments);
    expect(take_readings(2, pointers.data(), 0, &readings, &which) == Refusal::ReadingMalformed,
           "a reading holding a tab is refused, since a tab would forge a field");

    readings = nps_tools::Readings();
    arguments = {"--total-ms", "fast"};
    pointers = argv_of(arguments);
    expect(take_readings(2, pointers.data(), 0, &readings, &which) == Refusal::ReadingMalformed,
           "a word where a measurement belongs is refused");

    readings = nps_tools::Readings();
    arguments = {"--total-ms"};
    pointers = argv_of(arguments);
    expect(take_readings(1, pointers.data(), 0, &readings, &which) == Refusal::ReadingMissing,
           "a name with no value after it is refused rather than read as empty");

    readings = nps_tools::Readings();
    arguments = {"--total-ms", "7", "--total-ms", "8"};
    pointers = argv_of(arguments);
    expect(take_readings(4, pointers.data(), 0, &readings, &which) == Refusal::ReadingRepeated &&
               which == "--total-ms",
           "a reading passed twice is refused rather than the second one quietly winning");
    readings = nps_tools::Readings();
    arguments = {"--solve", "x = 4", "--solve", "x = 5"};
    pointers = argv_of(arguments);
    expect(take_readings(4, pointers.data(), 0, &readings, &which) == Refusal::ReadingRepeated,
           "and so is a text reading passed twice");

    expect(record_path_for("/tmp/x", "nps_nspire.luax.tns") ==
               "/tmp/x/nps_nspire.luax.tns.XXXXXX.device-run.txt",
           "the record keeps the artifact name and the reader's discovery suffix");

    readings = nps_tools::Readings();
    arguments = complete_arguments();
    pointers = argv_of(arguments);
    take_readings(static_cast<int>(pointers.size()), pointers.data(), 0, &readings, &which);
    const std::string written =
        record_text("nps_nspire.luax.tns", std::string(64, 'a'), "manifest/x.h", readings);
    expect(written.find("device\tschema\t3\n") == 0 &&
               written.find("\ndevice\tmanifest-header\tmanifest/x.h\n") != std::string::npos,
           "the record says schema 3 and names the header the reader has to open");
    expect(written.find("check\t") == std::string::npos &&
               written.find("pass") == std::string::npos &&
               written.find("fail") == std::string::npos,
           "and carries no check line and neither verdict word, because this tool judges nothing");
    std::size_t rows = 0;
    for (std::size_t at = written.find("reading\t"); at != std::string::npos;
         at = written.find("\nreading\t", at + 1))
        ++rows;
    expect(rows == nps_tools::reading_count(),
           "with one reading line per reading in the table and no more");

    const std::string directory = temporary_directory();
    if (directory.empty()) {
        expect(false, "a temporary directory could be made");
    } else {
        std::string digest;
        expect(!hex_digest_of(directory + "/absent.tns", &digest),
               "an artifact that is not in the build directory cannot be hashed");
        write_file(directory + "/present.tns", "bytes");
        expect(hex_digest_of(directory + "/present.tns", &digest) && digest.size() == 64,
               "and one that is gets a digest the reader can check it against");
        const std::string original_digest = digest;
        expect(!write_file(directory + "/present.tns", "replacement"),
               "an existing output is refused rather than overwritten");
        expect(hex_digest_of(directory + "/present.tns", &digest) && digest == original_digest,
               "refusing an existing output preserves every byte");

        std::string first_path;
        std::string second_path;
        readings.total_ms = 7;
        expect(write_record(directory, "nps_nspire.luax.tns", original_digest, "manifest/x.h",
                            readings, &first_path),
               "the first observed run is written");
        std::string first_digest;
        expect(hex_digest_of(first_path, &first_digest), "the first run can be read back");
        readings.total_ms = 8;
        expect(write_record(directory, "nps_nspire.luax.tns", original_digest, "manifest/x.h",
                            readings, &second_path),
               "a later run of the same artifact is written");
        expect(first_path != second_path, "each run of an artifact has a distinct record identity");
        expect(hex_digest_of(first_path, &digest) && digest == first_digest,
               "a later run preserves the earlier measurement unchanged");
        expect(hex_digest_of(second_path, &digest) && digest != first_digest,
               "the later measurement is retained separately");
        expect(!write_file(first_path, "replacement") &&
                   hex_digest_of(first_path, &digest) && digest == first_digest,
               "an existing run cannot be overwritten");
        const std::string collision_stage = directory + "/collision.pending";
        const int collision_descriptor =
            ::open(collision_stage.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        expect(collision_descriptor >= 0 &&
                   !publish_record(collision_descriptor, collision_stage, first_path, "replacement") &&
                   hex_digest_of(first_path, &digest) && digest == first_digest &&
                   !file_exists(collision_stage),
               "publishing refuses an existing record and removes only the staged replacement");

        const std::string legacy_path = directory + "/nps_nspire.device-run.txt";
        expect(write_file(legacy_path, written), "a legacy record is retained beside new runs");
        std::string legacy_digest;
        hex_digest_of(legacy_path, &legacy_digest);
        std::vector<pid_t> writers;
        int start[2];
        const bool gate_opened = ::pipe(start) == 0;
        for (int run = 0; gate_opened && run < 8; ++run) {
            const pid_t writer = ::fork();
            if (writer == 0) {
                ::close(start[1]);
                char release;
                if (::read(start[0], &release, 1) != 0)
                    ::_exit(1);
                ::close(start[0]);
                readings.total_ms = 20 + run;
                std::string path;
                ::_exit(write_record(directory, "nps_nspire.luax.tns", original_digest,
                                     "manifest/x.h", readings, &path) ? 0 : 1);
            }
            if (writer > 0)
                writers.push_back(writer);
        }
        if (gate_opened) {
            ::close(start[0]);
            ::close(start[1]);
        }
        bool all_written = writers.size() == 8;
        for (pid_t writer : writers) {
            int status = 0;
            if (::waitpid(writer, &status, 0) != writer || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                all_written = false;
        }
        expect(all_written, "concurrent writers all retain their observations");
        expect(record_count_in(directory) == 11,
               "the reader's suffix discovers every concurrent run and the legacy run");
        expect(hex_digest_of(first_path, &digest) && digest == first_digest &&
                   hex_digest_of(legacy_path, &digest) && digest == legacy_digest,
               "concurrent runs preserve both earlier and legacy records");
        expect(::symlink(first_path.c_str(), (directory + "/record-link").c_str()) == 0 &&
                   !write_file(directory + "/record-link", "replacement") &&
                   hex_digest_of(first_path, &digest) && digest == first_digest,
               "exclusive creation cannot overwrite a record through a symlink");
        std::string absent_path;
        expect(!write_record(directory + "/absent", "nps_nspire.luax.tns", original_digest,
                             "manifest/x.h", readings, &absent_path),
               "failure to create a run record is reported");
    }

    for (bool partial_write : {false, true}) {
        const std::string fault_directory = temporary_directory();
        if (fault_directory.empty()) {
            expect(false, "the failed-write fixture directory can be created");
            continue;
        }
        const pid_t writer = ::fork();
        if (writer == 0) {
            const rlimit limit = {64, 64};
            if (::signal(SIGXFSZ, SIG_IGN) == SIG_ERR || ::setrlimit(RLIMIT_FSIZE, &limit) != 0)
                ::_exit(2);
            readings.launch_header = std::string(partial_write ? 65536 : 10, 'x');
            const std::string contents =
                record_text("nps_nspire.luax.tns", std::string(64, 'a'), "manifest/x.h", readings);
            FILE *probe = std::fopen((fault_directory + "/failure-phase").c_str(), "wb");
            if (!probe)
                ::_exit(3);
            char buffer[4096];
            if (std::setvbuf(probe, buffer, _IOFBF, sizeof(buffer)) != 0)
                ::_exit(4);
            const bool buffered = std::fwrite(contents.data(), 1, contents.size(), probe) == contents.size();
            const bool closed = std::fclose(probe) == 0;
            if (partial_write ? buffered : (!buffered || closed))
                ::_exit(5);
            std::string path;
            ::_exit(write_record(fault_directory, "nps_nspire.luax.tns", std::string(64, 'a'),
                                 "manifest/x.h", readings, &path) ? 6 : 0);
        }
        int status = 0;
        expect(writer > 0 && ::waitpid(writer, &status, 0) == writer && WIFEXITED(status) &&
                   WEXITSTATUS(status) == 0,
               partial_write ? "a real partial write is reproduced and refused"
                             : "a real buffered close failure is reproduced and refused");
        expect(record_count_in(fault_directory) == 0,
               "a failed write leaves no discoverable record");
        std::string retry_path;
        expect(write_record(fault_directory, "nps_nspire.luax.tns", std::string(64, 'a'),
                            "manifest/x.h", readings, &retry_path) &&
                   record_count_in(fault_directory) == 1,
               "retrying after an output failure exposes only the complete retry");
    }

    std::cout << "device run: " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0)
        return selftest();
    if (argc < 4) {
        std::cout
            << "usage: nps_device_run <build-directory> <artifact> <manifest-header> <reading>...\n"
               "       nps_device_run --selftest\n"
               "the header is manifest/unified/nps_manifest_config.h, relative to the build tree\n"
               "this writes the readings down. nps_device_evidence reads them and decides.\n"
               "each invocation creates a separate artifact.<run-id>.device-run.txt record.\n"
               "all nineteen readings are required, and where each one comes from:\n"
               "  --hardware-type --hardware-subtype --os-index --ndl-revision\n"
               "                                    the raw four from nps_nspire.device_identity(),\n"
               "                                    which no screen shows yet, so this one needs a\n"
               "                                    surface first. The model and the build come from\n"
               "                                    device_identity.cc rather than from typing them\n"
               "  --outside-writes                  an mcp__nspire__device listing before the run\n"
               "                                    and after it, counting changes outside /nps\n"
               "  --offline-solves --offline-needed-link   solves run with the link detached\n"
               "  --launch-header --launch-red      the first screen after a reset, red lines counted\n"
               "  --manifest-line                   what !m printed on that same launch\n"
               "  --solve                           what one !s returned on that same launch\n"
               "  --total-ms                        the ms figure in the metrics line under it\n"
               "  --browse-presses --browse-total-ms   timed step-browse key presses, the reader\n"
               "                                    divides rather than the person reading\n"
               "  --giac-calls-before-browse --giac-calls-after-browse\n"
               "                                    the g figure in that same metrics line, read\n"
               "                                    either side of the browsing\n"
               "  --first-step-ms                   render_ready_ms from the metrics line\n"
               "  --free-kb --contiguous-kb         the launch header's free reading, after a reset\n";
        return 2;
    }

    const std::string directory = argv[1];
    const std::string artifact = argv[2];
    const std::string header = argv[3];
    nps_tools::Readings readings;
    std::string which;
    Refusal refusal = take_readings(argc, argv, 4, &readings, &which);
    std::string digest;
    if (refusal == Refusal::Accepted && !file_exists(directory + "/" + header)) {
        which = header;
        refusal = Refusal::HeaderMissing;
    }
    if (refusal == Refusal::Accepted && !hex_digest_of(directory + "/" + artifact, &digest)) {
        which = artifact;
        refusal = Refusal::ArtifactMissing;
    }
    if (refusal == Refusal::Accepted) {
        std::string path;
        if (write_record(directory, artifact, digest, header, readings, &path)) {
            std::cout << "device run: " << nps_tools::reading_count() << " readings written to "
                      << path << ", no verdict. Run nps_device_evidence on it to see the rows\n";
            return 0;
        }
        which = path;
        refusal = Refusal::RecordUnwritable;
    }
    std::cout << "device run: refused, " << refusal_name(refusal) << ": " << which << "\n";
    return 1;
}
