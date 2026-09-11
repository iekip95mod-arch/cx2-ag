#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main relocate_main
#include "relocate.c"
#undef main
#undef stat
#undef mkdir
#undef fopen
#undef rename

static char suite_root[] = "/tmp/cx2-relocate-tests-XXXXXX";
static char fixture_root[PATH_MAX];
static unsigned cases, rename_calls, fail_rename, refresh_calls;
static int startup;

static const struct {
    const char *path;
    const char *contents;
} initial_files[] = {
    { "/documents/ndl/ndl_resources.tns", "original resources\n" },
    { "/documents/ndl/persistent.tns", "original persistent\n" },
    { "/documents/ndl_resources_custom.tns", "custom resources\n" },
    { "/documents/persistent_custom.tns", "custom persistent\n" },
    { "/documents/calc_helpers.luax.tns", "calculator helper\n" },
    { "/documents/ndl/startup/keysvc.tns", "resident helper\n" },
    { "/documents/ndl/owner.tns", "owner document\n" },
};

static void mapped_path(const char *path, char *mapped)
{
    assert((!strncmp(path, "/documents", 10) && (!path[10] || path[10] == '/')) ||
           (!strncmp(path, "/appdata", 8) && (!path[8] || path[8] == '/')));
    assert(!strstr(path, "/../") && !strstr(path, "/.."));
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

static void write_file(const char *path, const char *contents)
{
    char parent[PATH_MAX], mapped[PATH_MAX];
    assert(strlen(path) < sizeof parent);
    strcpy(parent, path);
    *strrchr(parent, '/') = 0;
    make_directory(parent);
    mapped_path(path, mapped);
    FILE *stream = fopen(mapped, "wx");
    assert(stream);
    assert(fwrite(contents, 1, strlen(contents), stream) == strlen(contents));
    assert(fclose(stream) == 0);
}

static int fixture_stat(const char *path, struct stat *st)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    return stat(mapped, st);
}

static int fixture_mkdir(const char *path, mode_t mode)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    return mkdir(mapped, mode);
}

static FILE *fixture_fopen(const char *path, const char *mode)
{
    char mapped[PATH_MAX];
    mapped_path(path, mapped);
    return fopen(mapped, mode);
}

static int fixture_rename(const char *source, const char *destination)
{
    char mapped_source[PATH_MAX], mapped_destination[PATH_MAX];
    mapped_path(source, mapped_source);
    mapped_path(destination, mapped_destination);
    ++rename_calls;
    if (rename_calls == fail_rename) {
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

static void refresh_osscr(void)
{
    ++refresh_calls;
}

static void expect_file(const char *path, const char *contents)
{
    char mapped[PATH_MAX], bytes[128];
    mapped_path(path, mapped);
    FILE *stream = fopen(mapped, "r");
    assert(stream);
    size_t size = fread(bytes, 1, sizeof bytes, stream);
    assert(!ferror(stream) && feof(stream));
    assert(size == strlen(contents) && !memcmp(bytes, contents, size));
    assert(fclose(stream) == 0);
}

static void expect_absent(const char *path)
{
    struct stat st;
    assert(fixture_stat(path, &st) != 0 && errno == ENOENT);
}

static void new_fixture(int missing)
{
    int length = snprintf(fixture_root, sizeof fixture_root, "%s/case-%02u", suite_root, ++cases);
    assert(length > 0 && length < (int)sizeof fixture_root);
    assert(mkdir(fixture_root, 0700) == 0);
    rename_calls = fail_rename = refresh_calls = 0;
    startup = 0;
    for (unsigned i = 0; i < sizeof initial_files / sizeof initial_files[0]; ++i)
        if ((int)i != missing)
            write_file(initial_files[i].path, initial_files[i].contents);
}

static void expect_originals(int missing)
{
    for (unsigned i = 0; i < sizeof initial_files / sizeof initial_files[0]; ++i)
        if ((int)i != missing)
            expect_file(initial_files[i].path, initial_files[i].contents);
}

static void check_guards(void)
{
    new_fixture(-1);
    startup = 1;
    assert(relocate_main() == 1 && rename_calls == 0 && refresh_calls == 0);
    expect_originals(-1);
    expect_absent("/appdata");
    for (int missing = 0; missing < 5; ++missing) {
        new_fixture(missing);
        assert(relocate_main() == 1 && rename_calls == 0 && refresh_calls == 0);
        expect_originals(missing);
        expect_absent("/appdata");
    }
    for (int i = 0; i < 5; ++i) {
        int directory = (i + 2) % 5;
        new_fixture(directory);
        make_directory(initial_files[directory].path);
        int status = relocate_main();
        if (status != 1 || rename_calls || refresh_calls)
            fprintf(stderr, "directory accepted as required file: %s, rc=%d, moves=%u, fixture=%s\n",
                    initial_files[directory].path, status, rename_calls, fixture_root);
        assert(status == 1 && rename_calls == 0 && refresh_calls == 0);
        expect_originals(directory);
        expect_absent("/appdata");
    }
    for (int empty = 0; empty < 5; ++empty) {
        new_fixture(empty);
        write_file(initial_files[empty].path, "");
        assert(relocate_main() == 1 && rename_calls == 0 && refresh_calls == 0);
        expect_originals(empty);
        expect_file(initial_files[empty].path, "");
        expect_absent("/appdata");
    }
    new_fixture(-1);
    write_file("/appdata/ndl/owner.tns", "existing destination\n");
    assert(relocate_main() == 1 && rename_calls == 0 && refresh_calls == 0);
    expect_originals(-1);
    expect_file("/appdata/ndl/owner.tns", "existing destination\n");
    expect_absent("/appdata/relocation.log");
}

static unsigned check_success(void)
{
    new_fixture(-1);
    assert(relocate_main() == 0 && refresh_calls == 1);
    expect_absent("/documents/ndl");
    expect_absent("/documents/ndl_resources_custom.tns");
    expect_absent("/documents/persistent_custom.tns");
    expect_absent("/documents/calc_helpers.luax.tns");
    expect_file("/appdata/ndl/ndl_resources.stock.tns", initial_files[0].contents);
    expect_file("/appdata/ndl/persistent.stock.tns", initial_files[1].contents);
    expect_file("/appdata/ndl/ndl_resources.tns", initial_files[2].contents);
    expect_file("/appdata/ndl/persistent.tns", initial_files[3].contents);
    expect_file("/appdata/ndl/calc_helpers.luax.tns", initial_files[4].contents);
    expect_file("/appdata/ndl/startup/keysvc.tns", initial_files[5].contents);
    expect_file("/appdata/ndl/owner.tns", initial_files[6].contents);
    return rename_calls;
}

static void check_rollbacks(unsigned moves)
{
    for (unsigned stage = 1; stage <= moves; ++stage) {
        new_fixture(-1);
        fail_rename = stage;
        assert(relocate_main() == 1 && refresh_calls == 1);
        assert(rename_calls == 2 * stage - 1);
        expect_originals(-1);
        expect_absent("/appdata/ndl");
    }
    const char *collisions[] = {
        "/documents/ndl/ndl_resources.stock.tns",
        "/documents/ndl/calc_helpers.luax.tns",
        "/documents/ndl/persistent.stock.tns",
    };
    for (unsigned i = 0; i < sizeof collisions / sizeof collisions[0]; ++i) {
        new_fixture(-1);
        write_file(collisions[i], "existing file must survive\n");
        assert(relocate_main() == 1 && refresh_calls == 1);
        expect_originals(-1);
        expect_file(collisions[i], "existing file must survive\n");
        expect_absent("/appdata/ndl");
    }
}

int main(void)
{
    assert(mkdtemp(suite_root));
    check_guards();
    unsigned moves = check_success();
    assert(moves == 6);
    check_rollbacks(moves);
    printf("relocation: %u fixtures passed, all %u move failures rolled back. Retained %s\n", cases, moves, suite_root);
    return 0;
}
