#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ARM_BOOT_TEST
#if defined(ARM_BOOT_RESTART) && ARM_BOOT_RESTART
static void fixture_reset(void);
static int fixture_is_cx2(void);
#define is_cx2 fixture_is_cx2()
#define ARM_BOOT_RESET() fixture_reset()
#endif
#define main arm_boot_main
#include "arm_boot.c"
#undef main
#undef stat
#undef fopen
#undef rename
#undef fread
#undef fwrite
#undef ferror
#undef fclose

static char suite_root[] = "/tmp/cx2-arm-boot-tests-XXXXXX";
static char fixture_root[PATH_MAX];
static unsigned cases, rename_calls, fail_rename;
static int startup;
#if ARM_BOOT_RESTART
static unsigned reset_calls;
static int cx2, status_closed;
#endif
static const char *fail_open, *fail_read, *fail_write, *fail_close;
static unsigned char persistent_bytes[2305];
static const unsigned char resources_bytes[] = "custom resources with relocated paths";
static const unsigned char old_tns[] = "original currentdoc tns, preserved byte for byte";
static const unsigned char old_state[] = "original currentdoc state, preserved byte for byte";
static const struct {
    const char *path;
    const unsigned char *bytes;
    size_t size;
} initial_files[] = {
    { "/appdata/ndl/ndl_resources.tns", resources_bytes, sizeof resources_bytes },
    { "/appdata/ndl/persistent.tns", persistent_bytes, sizeof persistent_bytes },
    #if ARM_BOOT_RESTART
    { "/appdata/currentdoc.stock.tns", old_tns, sizeof old_tns },
    { "/appdata/currentdoc.stock.data", old_state, sizeof old_state },
    #else
    { "/phoenix/syst/poweroff/currentdoc.tns", old_tns, sizeof old_tns },
    { "/phoenix/syst/poweroff/currentdoc.data", old_state, sizeof old_state },
    #endif
};

static struct {
    FILE *stream;
    const char *path;
    int read_error;
} streams[8];

static void mapped_path(const char *path, char *mapped)
{
    assert((!strncmp(path, "/documents", 10) && (!path[10] || path[10] == '/')) ||
           (!strncmp(path, "/appdata", 8) && (!path[8] || path[8] == '/')) ||
           (!strncmp(path, "/phoenix", 8) && (!path[8] || path[8] == '/')));
    assert(!strstr(path, "/.."));
    int length = snprintf(mapped, PATH_MAX, "%s%s", fixture_root, path);
    assert(length > 0 && length < PATH_MAX);
}

static void make_directory(const char *path)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    for (char *slash = mapped + strlen(fixture_root) + 1; *slash; ++slash) {
        if (*slash != '/')
            continue;
        *slash = 0;
        assert(mkdir(mapped, 0700) == 0 || errno == EEXIST);
        *slash = '/';
    }
    assert(mkdir(mapped, 0700) == 0 || errno == EEXIST);
}

static void write_file(const char *path, const void *bytes, size_t size)
{
    char parent[PATH_MAX], mapped[PATH_MAX];
    assert(strlen(path) < sizeof parent);
    strcpy(parent, path);
    *strrchr(parent, '/') = 0;
    make_directory(parent);
    mapped_path(path, mapped);
    FILE *stream = fopen(mapped, "wx");
    assert(stream);
    assert(fwrite(bytes, 1, size, stream) == size);
    assert(fclose(stream) == 0);
}

static int fixture_stat(const char *path, struct stat *st)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    return stat(mapped, st);
}

static int path_is(const char *path, const char *failure)
{
    return failure && !strcmp(path, failure);
}

static FILE *fixture_fopen(const char *path, const char *mode)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    if (path_is(path, fail_open)) {
        errno = EIO;
        return NULL;
    }
    FILE *stream = fopen(mapped, mode);
    if (!stream)
        return NULL;
    for (unsigned i = 0; i < sizeof streams / sizeof streams[0]; ++i) {
        if (streams[i].stream)
            continue;
        streams[i].stream = stream;
        streams[i].path = path;
        streams[i].read_error = 0;
        return stream;
    }
    abort();
}

static unsigned stream_slot(FILE *stream)
{
    for (unsigned i = 0; i < sizeof streams / sizeof streams[0]; ++i)
        if (streams[i].stream == stream)
            return i;
    abort();
}

static size_t fixture_fread(void *bytes, size_t size, size_t count, FILE *stream)
{
    unsigned slot = stream_slot(stream);
    if (path_is(streams[slot].path, fail_read)) {
        streams[slot].read_error = 1;
        errno = EIO;
        return 0;
    }
    return fread(bytes, size, count, stream);
}

static size_t fixture_fwrite(const void *bytes, size_t size, size_t count, FILE *stream)
{
    unsigned slot = stream_slot(stream);
    if (path_is(streams[slot].path, fail_write)) {
        assert(count > 0);
        return fwrite(bytes, size, count - 1, stream);
    }
    return fwrite(bytes, size, count, stream);
}

static int fixture_ferror(FILE *stream)
{
    #if ARM_BOOT_RESTART
    if (path_is(streams[stream_slot(stream)].path, "/documents/RelocationStatus.tns") &&
        path_is(streams[stream_slot(stream)].path, fail_write))
        return 1;
    #endif
    return streams[stream_slot(stream)].read_error || ferror(stream);
}

static int fixture_fclose(FILE *stream)
{
    unsigned slot = stream_slot(stream);
    int failed = path_is(streams[slot].path, fail_close);
    int status = fclose(stream);
    #if ARM_BOOT_RESTART
    if (path_is(streams[slot].path, "/documents/RelocationStatus.tns"))
        status_closed = !failed && status == 0;
    #endif
    streams[slot].stream = NULL;
    return failed ? EOF : status;
}

static int fixture_rename(const char *source, const char *destination)
{
    char mapped_source[PATH_MAX], mapped_destination[PATH_MAX];
    mapped_path(source, mapped_source);
    mapped_path(destination, mapped_destination);
    if (++rename_calls == fail_rename) {
        errno = EIO;
        return -1;
    }
    struct stat st;
    assert(lstat(mapped_destination, &st) != 0 && errno == ENOENT);
    return rename(mapped_source, mapped_destination);
}

static int nl_isstartup(void)
{
    return startup;
}

#if ARM_BOOT_RESTART
static int fixture_is_cx2(void)
{
    return cx2;
}
#endif

static void expect_file(const char *path, const void *bytes, size_t size)
{
    char mapped[PATH_MAX];
    unsigned char actual[4096];
    mapped_path(path, mapped);
    FILE *stream = fopen(mapped, "r");
    assert(stream);
    size_t count = fread(actual, 1, sizeof actual, stream);
    assert(!ferror(stream) && feof(stream));
    assert(count == size && !memcmp(actual, bytes, size));
    assert(fclose(stream) == 0);
}

static void expect_absent(const char *path)
{
    struct stat st;
    assert(fixture_stat(path, &st) != 0 && errno == ENOENT);
}

static void expect_originals(int missing)
{
    for (unsigned i = 0; i < sizeof initial_files / sizeof initial_files[0]; ++i)
        if ((int)i != missing)
            expect_file(initial_files[i].path, initial_files[i].bytes, initial_files[i].size);
    for (unsigned i = 0; i < sizeof streams / sizeof streams[0]; ++i)
        assert(!streams[i].stream);
}

static void new_fixture(int missing)
{
    int length = snprintf(fixture_root, sizeof fixture_root, "%s/case-%02u", suite_root, ++cases);
    assert(length > 0 && length < (int)sizeof fixture_root);
    assert(mkdir(fixture_root, 0700) == 0);
    rename_calls = fail_rename = 0;
    startup = 0;
    #if ARM_BOOT_RESTART
    reset_calls = 0;
    status_closed = 0;
    cx2 = 1;
    #endif
    fail_open = fail_read = fail_write = fail_close = NULL;
    for (unsigned i = 0; i < sizeof streams / sizeof streams[0]; ++i)
        assert(!streams[i].stream);
    make_directory("/documents");
    #if ARM_BOOT_RESTART
    make_directory("/phoenix/syst/poweroff");
    #endif
    for (unsigned i = 0; i < sizeof initial_files / sizeof initial_files[0]; ++i)
        if ((int)i != missing)
            write_file(initial_files[i].path, initial_files[i].bytes, initial_files[i].size);
}

#if !ARM_BOOT_RESTART
static void check_guards(void)
{
    new_fixture(-1);
    startup = 1;
    assert(arm_boot_main() == 1 && rename_calls == 0);
    expect_originals(-1);
    for (int missing = 0; missing < 4; ++missing) {
        new_fixture(missing);
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(missing);
    }
    for (int file = 0; file < 4; ++file) {
        new_fixture(file);
        make_directory(initial_files[file].path);
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(file);
        new_fixture(file);
        write_file(initial_files[file].path, initial_files[file].bytes, file < 2 ? 0 : 15);
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(file);
    }
    new_fixture(-1);
    make_directory("/documents/ndl");
    assert(arm_boot_main() == 1 && rename_calls == 0);
    expect_originals(-1);
    const char *blocked[] = {
        "/appdata/currentdoc.stock.tns", "/appdata/currentdoc.stock.data",
        "/appdata/currentdoc.next.tns", "/appdata/currentdoc.next.data",
    };
    for (unsigned i = 0; i < sizeof blocked / sizeof blocked[0]; ++i) {
        new_fixture(-1);
        write_file(blocked[i], old_tns, sizeof old_tns);
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(-1);
        expect_file(blocked[i], old_tns, sizeof old_tns);
    }
}

static void check_success(void)
{
    new_fixture(-1);
    assert(arm_boot_main() == 0 && rename_calls == 4);
    expect_file("/appdata/ndl/persistent.tns", persistent_bytes, sizeof persistent_bytes);
    expect_file("/appdata/ndl/ndl_resources.tns", resources_bytes, sizeof resources_bytes);
    expect_file("/appdata/currentdoc.stock.tns", old_tns, sizeof old_tns);
    expect_file("/appdata/currentdoc.stock.data", old_state, sizeof old_state);
    expect_file(initial_files[2].path, persistent_bytes, sizeof persistent_bytes);
    unsigned char expected[0x21c] = { 0 };
    expected[0] = expected[8] = expected[16] = 1;
    expect_file(initial_files[3].path, expected, sizeof expected);
    expect_absent("/appdata/currentdoc.next.tns");
    expect_absent("/appdata/currentdoc.next.data");
}

static void check_failures(void)
{
    for (unsigned move = 1; move <= 4; ++move) {
        new_fixture(-1);
        fail_rename = move;
        assert(arm_boot_main() == 1 && rename_calls == 2 * move - 1);
        expect_originals(-1);
        expect_absent("/appdata/currentdoc.stock.tns");
        expect_absent("/appdata/currentdoc.stock.data");
    }
    const char *copy_paths[] = {
        "/appdata/ndl/persistent.tns", "/appdata/currentdoc.next.tns",
        "/appdata/currentdoc.next.data",
    };
    for (unsigned i = 0; i < 3; ++i) {
        new_fixture(-1);
        fail_open = copy_paths[i];
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(-1);
        new_fixture(-1);
        fail_close = copy_paths[i];
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(-1);
    }
    new_fixture(-1);
    fail_read = copy_paths[0];
    assert(arm_boot_main() == 1 && rename_calls == 0);
    expect_originals(-1);
    for (unsigned i = 1; i < 3; ++i) {
        new_fixture(-1);
        fail_write = copy_paths[i];
        assert(arm_boot_main() == 1 && rename_calls == 0);
        expect_originals(-1);
    }
}
#else
static void expect_installed(void)
{
    unsigned char expected[0x21c] = { 0 };
    expected[0] = expected[8] = expected[16] = 1;
    expect_file("/phoenix/syst/poweroff/currentdoc.tns", persistent_bytes, sizeof persistent_bytes);
    expect_file("/phoenix/syst/poweroff/currentdoc.data", expected, sizeof expected);
}

static void fixture_reset(void)
{
    assert(reset_calls == 0 && status_closed && rename_calls == 2);
    expect_originals(-1);
    expect_installed();
    ++reset_calls;
}

static void check_restart(void)
{
    new_fixture(-1);
    assert(arm_boot_main() == 0 && reset_calls == 1 && rename_calls == 2);
    expect_originals(-1);
    expect_absent("/appdata/currentdoc.next.tns");
    expect_absent("/appdata/currentdoc.next.data");

    const char *active[] = {
        "/phoenix/syst/poweroff/currentdoc.tns", "/phoenix/syst/poweroff/currentdoc.data"
    };
    for (unsigned mask = 1; mask <= 3; ++mask) {
        new_fixture(-1);
        for (unsigned i = 0; i < 2; ++i)
            if (mask & (1u << i))
                write_file(active[i], old_tns, sizeof old_tns);
        assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
        expect_originals(-1);
        for (unsigned i = 0; i < 2; ++i)
            if (mask & (1u << i))
                expect_file(active[i], old_tns, sizeof old_tns);
    }
    for (int missing = 2; missing < 4; ++missing) {
        new_fixture(missing);
        assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
        expect_originals(missing);
        write_file(initial_files[missing].path, old_tns, 0);
        assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
        expect_originals(missing);
        new_fixture(missing);
        make_directory(initial_files[missing].path);
        assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
        expect_originals(missing);
    }
    const char *prepared[] = { "/appdata/currentdoc.next.tns", "/appdata/currentdoc.next.data" };
    for (unsigned i = 0; i < 2; ++i) {
        new_fixture(-1);
        write_file(prepared[i], old_tns, sizeof old_tns);
        assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
        expect_originals(-1);
        expect_file(prepared[i], old_tns, sizeof old_tns);
    }
    new_fixture(-1);
    startup = 1;
    assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
    expect_originals(-1);
    new_fixture(-1);
    cx2 = 0;
    assert(arm_boot_main() == 1 && rename_calls == 0 && reset_calls == 0);
    expect_originals(-1);

    for (unsigned move = 1; move <= 2; ++move) {
        new_fixture(-1);
        fail_rename = move;
        assert(arm_boot_main() == 1 && rename_calls == 2 * move - 1 && reset_calls == 0);
        expect_originals(-1);
        expect_absent(active[0]);
        expect_absent(active[1]);
        expect_file(prepared[0], persistent_bytes, sizeof persistent_bytes);
        unsigned char expected[0x21c] = { 0 };
        expected[0] = expected[8] = expected[16] = 1;
        expect_file(prepared[1], expected, sizeof expected);
    }
    new_fixture(-1);
    fail_close = "/documents/RelocationStatus.tns";
    assert(arm_boot_main() == 1 && rename_calls == 2 && reset_calls == 0 && !status_closed);
    expect_originals(-1);
    expect_installed();
    new_fixture(-1);
    fail_write = "/documents/RelocationStatus.tns";
    assert(arm_boot_main() == 1 && rename_calls == 2 && reset_calls == 0);
    expect_originals(-1);
    expect_installed();
}
#endif

int main(void)
{
    assert(mkdtemp(suite_root));
    for (unsigned i = 0; i < sizeof persistent_bytes; ++i)
        persistent_bytes[i] = i % 251;
    #if ARM_BOOT_RESTART
    check_restart();
    #else
    check_guards();
    check_success();
    check_failures();
    #endif
    printf("arm boot: %u fixtures passed, originals preserved across copy and rename failures. Retained %s\n", cases, suite_root);
    return 0;
}
