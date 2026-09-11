import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ndl/src/resources/persistency.c"

HARNESS = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *directory, *scenario;
static FILE *input, *output;
static int hooks, null_closes, read_failed, fault_count, failures, open_streams, source_opens;
static const char *fault_operation;
static int fault_index, rollback_failure, secondary_index, secondary_count;
static unsigned ut_os_version_index = 44;
#define NDL_MAX_OSID 49
#define KEY_NSPIRE_ESC 1
#define isKeyPressed(key) (!strcmp(scenario, "install-escape"))
#define HOOK_INSTALL(address, hook) (++hooks)

static const char *mapped(const char *path) {
    static char paths[8][1024];
    static unsigned slot;
    char *target = paths[slot++ % 8];
    snprintf(target, 1024, "%s%s", directory, path);
    return target;
}
static int fail_at(const char *operation) {
    if (!fault_operation || strcmp(operation, fault_operation)) return 0;
    return ++fault_count == fault_index;
}
static FILE *probe_open(const char *path, const char *mode) {
    if (mode[0] == 'r') ++source_opens;
    if (!strcmp(scenario, "uninstall-access")) { errno = EACCES; return NULL; }
    if ((!strcmp(scenario, "copy-open-in") && mode[0] == 'r') ||
        (!strcmp(scenario, "copy-open-out") && mode[0] == 'w') ||
        (!strcmp(scenario, "uninstall-open") && strstr(path, "currentdoc.data")) ||
        fail_at("open")) { errno = EIO; return NULL; }
    FILE *stream = fopen(mapped(path), mode);
    if (stream) ++open_streams;
    if (mode[0] == 'r') input = stream;
    else output = stream;
    return stream;
}
static size_t probe_read(void *bytes, size_t size, size_t count, FILE *stream) {
    if ((!strcmp(scenario, "copy-read") || fail_at("read"))) {
        read_failed = 1;
        return 0;
    }
    return fread(bytes, size, count, stream);
}
static int probe_error(FILE *stream) { return read_failed || ferror(stream); }
static size_t probe_write(const void *bytes, size_t size, size_t count, FILE *stream) {
    if (!strcmp(scenario, "copy-short") || fail_at("write")) {
        if (size * count > 1) fwrite(bytes, 1, size * count - 1, stream);
        return size == 1 && count > 1 ? count - 1 : 0;
    }
    return fwrite(bytes, size, count, stream);
}
static int probe_close(FILE *stream) {
    if (!stream) { ++null_closes; return EOF; }
    --open_streams;
    int fail = (!strcmp(scenario, "copy-close-in") && stream == input) ||
               (!strcmp(scenario, "copy-close-out") && stream == output) || fail_at("close");
    int closed = fclose(stream);
    return fail ? EOF : closed;
}
static int probe_stat(const char *path, struct stat *info) {
    if (fail_at("stat")) { errno = EIO; return -1; }
    return stat(mapped(path), info);
}
static int probe_rename(const char *from, const char *to) {
    int primary = fail_at("rename");
    int secondary = rollback_failure && ++secondary_count == secondary_index;
    if (primary || secondary) {
        errno = EIO;
        return -1;
    }
    return rename(mapped(from), mapped(to));
}
static int probe_unlink(const char *path) {
    int secondary = !strcmp(scenario, "cleanup-failure") && ++secondary_count == secondary_index;
    if (fail_at("unlink") || secondary) { errno = EIO; return -1; }
    return unlink(mapped(path));
}
static void expect(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL %s: %s\n", scenario, message); ++failures; }
}
static void seed(const char *path, const char *text) {
    FILE *stream = fopen(mapped(path), "wb");
    if (!stream) abort();
    fwrite(text, 1, strlen(text), stream);
    fclose(stream);
}
static int contains(const char *path, const char *text) {
    char contents[8192];
    FILE *stream = fopen(mapped(path), "rb");
    if (!stream) return 0;
    size_t length = fread(contents, 1, sizeof(contents), stream);
    fclose(stream);
    return length == strlen(text) && !memcmp(contents, text, length);
}
static int valid_metadata(void) {
    uint32_t words[135];
    FILE *stream = fopen(mapped("/phoenix/syst/poweroff/currentdoc.data"), "rb");
    if (!stream) return 0;
    size_t length = fread(words, 1, sizeof(words), stream);
    int extra = fgetc(stream);
    fclose(stream);
    if (length != sizeof(words) || extra != EOF) return 0;
    for (unsigned i = 0; i < 135; ++i)
        if (words[i] != (i == 0 || i == 2 || i == 4)) return 0;
    return 1;
}
#define fopen probe_open
#define fread probe_read
#define fwrite probe_write
#define ferror probe_error
#define fclose probe_close
#define stat(...) probe_stat(__VA_ARGS__)
#define rename probe_rename
#define unlink probe_unlink
'''

MAIN = r'''
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    directory = argv[1]; scenario = argv[2];
    if (argc > 4) { fault_operation = argv[3]; fault_index = atoi(argv[4]); }
    rollback_failure = !strcmp(scenario, "rollback-failure");
    if (argc > 5) secondary_index = atoi(argv[5]);
    seed("/documents/ndl/persistent.tns", "new persistent document");
    int fallback = !strcmp(scenario, "install-fallback-root") ? 2 :
                   !strcmp(scenario, "install-fallback-current") ? 3 :
                   !strcmp(scenario, "install-fallback-root-current") ? 4 : 0;
    if (fallback || !strcmp(scenario, "missing-document")) {
        probe_unlink("/documents/ndl/persistent.tns");
        if (fallback) seed(fallback == 2 ? "/documents/persistent.tns" :
                           fallback == 3 ? "/documents/ndl/currentdoc.tns" : "/documents/currentdoc.tns",
                           "new persistent document");
    }
    if (!strcmp(scenario, "copy-multiple")) {
        char long_document[4097];
        memset(long_document, 'X', sizeof(long_document) - 1);
        long_document[sizeof(long_document) - 1] = 0;
        seed("/documents/ndl/persistent.tns", long_document);
        int status = copy_file("/documents/ndl/persistent.tns", "/copy.tns");
        expect(status == 0 && contains("/copy.tns", long_document), "multi-buffer copy preserves every byte");
        return failures != 0;
    }
    if (!strncmp(scenario, "copy-", 5)) {
        int status = copy_file("/documents/ndl/persistent.tns", "/copy.tns");
        expect((status == 0) == !strcmp(scenario, "copy-good"), "copy status reflects I/O completion");
        if (!strcmp(scenario, "copy-good"))
            expect(contains("/copy.tns", "new persistent document"), "copy preserves every byte");
    } else if (!strncmp(scenario, "uninstall-", 10)) {
        int absent = !strcmp(scenario, "uninstall-absent-file") ||
                     !strcmp(scenario, "uninstall-absent-directory");
        if (!absent) seed("/phoenix/syst/poweroff/currentdoc.data", "old metadata");
        int status = persistency_uninstall();
        expect((status == 0) == absent, "uninstall distinguishes absent activation from I/O failure");
        if (absent) {
            struct stat info;
            expect(probe_stat("/phoenix/syst/poweroff/currentdoc.data", &info) < 0 && errno == ENOENT,
                   "already absent activation remains absent");
        }
        expect(null_closes == 0, "uninstall never closes a null stream");
    } else {
        int fresh = !strcmp(scenario, "install-fresh") || !strcmp(scenario, "fresh-failure");
        if (!fresh) {
            seed("/phoenix/syst/poweroff/currentdoc.tns", "old document");
            seed("/phoenix/syst/poweroff/currentdoc.data", "old metadata");
        }
        if (!strcmp(scenario, "backup-exists"))
            seed("/phoenix/syst/poweroff/currentdoc.tns.ndl.bak", "retained recovery");
        if (!strcmp(scenario, "stage-exists"))
            seed("/phoenix/syst/poweroff/currentdoc.tns.ndl.tmp", "retained staging");
        if (!strcmp(scenario, "empty-document")) seed("/documents/ndl/persistent.tns", "");
        seed("/documents/!!!MyDocuments/!!!UnsavedDocument.tns", "unsaved recovery");
        int status = persistency_install();
        if (!strcmp(scenario, "install-good") || !strcmp(scenario, "install-fresh") || fallback) {
            expect(status == 0, "complete publication reports success");
            expect(hooks == 3, "successful publication installs all three persistence hooks");
            expect(contains("/phoenix/syst/poweroff/currentdoc.tns", "new persistent document"),
                   "successful publication installs complete document");
            expect(valid_metadata(), "successful publication installs exact activation metadata");
        } else {
            int committed = !strcmp(scenario, "committed-cleanup-failure");
            expect(status == (committed ? 3 : !strcmp(scenario, "install-escape") ? 2 : 1),
                   "persistence distinguishes failure, cancellation and committed cleanup warning");
            expect(hooks == (committed ? 3 : 0),
                   "a committed boot pair installs all required hooks even after a cleanup warning");
            if (!rollback_failure && !fresh && strcmp(scenario, "committed-cleanup-failure")) {
                expect(contains("/phoenix/syst/poweroff/currentdoc.tns", "old document"),
                       "failure preserves the original boot document");
                expect(contains("/phoenix/syst/poweroff/currentdoc.data", "old metadata"),
                       "failure preserves the original activation metadata");
            }
            if (fresh) {
                struct stat info;
                expect(probe_stat("/phoenix/syst/poweroff/currentdoc.data", &info) < 0 && errno == ENOENT,
                       "failed fresh setup does not leave activation metadata");
                expect(probe_stat("/phoenix/syst/poweroff/currentdoc.tns", &info) < 0 && errno == ENOENT,
                       "failed fresh setup does not leave a boot document");
            }
            if (!strcmp(scenario, "backup-exists"))
                expect(contains("/phoenix/syst/poweroff/currentdoc.tns.ndl.bak", "retained recovery"),
                       "preexisting recovery is never overwritten");
            if (!strcmp(scenario, "stage-exists"))
                expect(contains("/phoenix/syst/poweroff/currentdoc.tns.ndl.tmp", "retained staging"),
                       "preexisting staging is never overwritten");
            if (rollback_failure) {
                expect(contains("/phoenix/syst/poweroff/currentdoc.tns.ndl.bak", "old document") ||
                       contains("/phoenix/syst/poweroff/currentdoc.tns", "old document"),
                       "failed rollback retains original document");
                expect(contains("/phoenix/syst/poweroff/currentdoc.data.ndl.bak", "old metadata"),
                       "failed rollback retains original metadata backup");
            }
            if (committed) {
                expect(contains("/phoenix/syst/poweroff/currentdoc.tns", "new persistent document") && valid_metadata(),
                       "cleanup failure leaves the completed boot document intact");
                expect(contains(fault_index == 1 ? "/phoenix/syst/poweroff/currentdoc.data.ndl.bak" :
                                "/phoenix/syst/poweroff/currentdoc.tns.ndl.bak",
                                fault_index == 1 ? "old metadata" : "old document"),
                       "committed cleanup warning retains the backup that could not be removed");
                expect(fault_count == 2, "cleanup tries each backup once without destructive retries");
            }
        }
        expect(contains("/documents/!!!MyDocuments/!!!UnsavedDocument.tns", "unsaved recovery"),
               "install preserves the user's unsaved recovery document");
    }
    expect(null_closes == 0, "all closed streams were open");
    expect(open_streams == 0, "every opened stream is closed even on failure");
    if (fault_operation) expect(fault_count >= fault_index, "the requested I/O fault was actually reached");
    if (secondary_index) expect(secondary_count >= secondary_index, "the rollback or cleanup fault was reached");
    if (source_opens) expect(source_opens == (fallback ? fallback :
                            !strcmp(scenario, "missing-document") ? 4 : 1),
                            "only absent loader sources try the next documented location");
    printf("%s: %d failures\n", scenario, failures);
    return failures != 0;
}
'''


class PersistencyIO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.case_count = 0
        cls.work = Path(tempfile.mkdtemp(prefix="ndl-persistency-"))
        source = SOURCE.read_text()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.MULTILINE)
        source, count = re.subn(r'HOOK_DEFINE\([^)]*\)\s*\{.*?^\}', '', source,
                               flags=re.MULTILINE | re.DOTALL)
        if count != 3:
            raise AssertionError("expected exactly three ARM hook bodies")
        harness = cls.work / "persistency.c"
        harness.write_text(HARNESS + source + MAIN)
        cls.binary = cls.work / "persistency"
        command = [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                   "-Wno-unused-function", str(harness), "-o", str(cls.binary)]
        if os.environ.get("SANITIZE"):
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        if cls.binary.stat().st_mtime_ns < max(SOURCE.stat().st_mtime_ns, harness.stat().st_mtime_ns):
            raise AssertionError("test binary predates its sources")
        print("Persistence evidence:", cls.work)

    @classmethod
    def tearDownClass(cls):
        print("Persistence cases:", cls.case_count)

    def run_case(self, scenario, operation=None, index=1, secondary=None):
        type(self).case_count += 1
        directory = Path(tempfile.mkdtemp(prefix=scenario + "-", dir=self.work))
        for path in ("documents/ndl", "documents/!!!MyDocuments", "phoenix/syst/poweroff"):
            if scenario == "uninstall-absent-directory" and path == "phoenix/syst/poweroff":
                continue
            (directory / path).mkdir(parents=True, exist_ok=True)
        command = [str(self.binary), str(directory), scenario]
        if operation:
            command.extend([operation, str(index)])
        if secondary is not None:
            command.append(str(secondary))
        process = subprocess.run(command, capture_output=True, text=True)
        (directory / "output.txt").write_text(process.stdout + process.stderr)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        if scenario == "rollback-failure":
            self.assertIn("rollback failed", process.stdout)
        if scenario in ("cleanup-failure", "committed-cleanup-failure"):
            self.assertIn("cleanup failed", process.stdout)

    def test_copy(self):
        for fault in ("good", "multiple", "short", "read", "close-in", "close-out", "open-in", "open-out"):
            with self.subTest(fault=fault):
                self.run_case("copy-" + fault)

    def test_install_completion(self):
        self.run_case("install-good")
        self.run_case("install-fresh")
        for location in ("root", "current", "root-current"):
            self.run_case("install-fallback-" + location)
        for operation, count in (("open", 3), ("read", 2), ("write", 2), ("close", 3),
                                 ("stat", 7), ("rename", 4)):
            for index in range(1, count + 1):
                with self.subTest(operation=operation, index=index):
                    self.run_case("install-failure", operation, index)
        for index in (1, 2):
            self.run_case("fresh-failure", "rename", index)

    def test_retained_backup(self):
        self.run_case("backup-exists")
        self.run_case("stage-exists")
        self.run_case("install-escape")
        self.run_case("empty-document")
        self.run_case("missing-document")

    def test_failed_rollback(self):
        for index in (5, 6, 7):
            with self.subTest(rename=index):
                self.run_case("rollback-failure", "rename", 4, index)

    def test_failed_cleanup(self):
        for index in (1, 2):
            with self.subTest(unlink=index):
                self.run_case("cleanup-failure", "rename", 4, index)
                self.run_case("committed-cleanup-failure", "unlink", index)

    def test_uninstall_open_failure(self):
        self.run_case("uninstall-open")
        self.run_case("uninstall-access")
        self.run_case("uninstall-stat", "stat", 1)
        self.run_case("uninstall-close", "close", 1)
        for scenario in ("uninstall-absent-file", "uninstall-absent-directory"):
            with self.subTest(scenario=scenario):
                self.run_case(scenario)


if __name__ == "__main__":
    unittest.main()
