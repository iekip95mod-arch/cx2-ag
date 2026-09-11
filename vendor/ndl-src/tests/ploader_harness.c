#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DIAGNOSTIC_SOURCE

typedef struct { int unused; } NUC_FILE;
enum e_ld_bin_format { LD_ERROR_BIN, LD_PRG_BIN, LD_BFLT_BIN, LD_ZEHN_BIN };
static enum e_ld_bin_format ld_bin_format;
static bool is_current_prgm_resident, has_colors, is_cx2, is_hww, plh_noscrredraw;
static NUC_FILE input;
static unsigned char program_memory[128], screen_memory[16];
static int fail_stack, fail_open, fail_signature, loader_return, entry_return, nested_entry;
static int allocation_index, fail_allocation, live_allocations, closes, program_frees, entries;
static uint32_t signature = 0x6e68655a;
static char **resident_argv;
#define REAL_SCREEN_BASE_ADDRESS screen_memory
struct assoc_file_each_cb_ctx { const char *prgm_name; char *prgm_path; bool *isassoc; };
static int assoc_file_each_cb(const char *path, void *context) { (void)path; (void)context; return 0; }
static bool expand_stack(void) { return !fail_stack; }
static void cfg_open(void) {}
static void cfg_close(void) {}
static char *cfg_get(const char *key) { (void)key; return NULL; }
static int file_each(const char *path, int (*callback)(const char *, void *), void *context) {
    (void)path; (void)callback; (void)context; return 0;
}
static NUC_FILE *nuc_fopen(const char *path, const char *mode) { (void)path; (void)mode; return fail_open ? NULL : &input; }
static size_t nuc_fread(void *bytes, size_t size, size_t count, NUC_FILE *file) {
    (void)file;
    assert(size == sizeof(signature) && count == 1);
    if (fail_signature) return 0;
    memcpy(bytes, &signature, sizeof(signature));
    return 1;
}
static void nuc_fclose(NUC_FILE *file) { assert(file == &input); ++closes; }
static void nuc_fseek(NUC_FILE *file, long offset, int origin) { (void)file; (void)offset; (void)origin; }
static void *probe_malloc(size_t size) {
    if (++allocation_index == fail_allocation) return NULL;
    void *allocation = malloc(size);
    if (allocation) ++live_allocations;
    return allocation;
}
static void probe_free(void *allocation) { if (allocation) --live_allocations; free(allocation); }
static void ld_free(void *base) { if (base) { assert(base == program_memory); ++program_frees; } }
static int ld_exec_with_args_diagnostic(const char *, int, char *[], void **, struct ld_diagnostic *);
static int program_entry(int argc, char *argv[]) {
    ++entries;
    assert(argc == 1 && argv[0] && !argv[1]);
    resident_argv = argv;
    if (nested_entry) {
        nested_entry = 0;
        fail_open = 1;
        struct ld_diagnostic inner = {0};
        assert(ld_exec_with_args_diagnostic("inner.tns", 0, NULL, NULL, &inner) == 0xDEAD);
        assert(!strcmp(inner.stage, "file.open"));
        fail_open = 0;
        is_current_prgm_resident = true;
    }
    return entry_return;
}
static int zehn_load_diagnostic(NUC_FILE *file, void **base, int (**entry)(int, char *[]), bool *hww, struct ld_diagnostic *diagnostic) {
    (void)file; (void)hww;
    if (loader_return) {
        ld_diagnostic_set(diagnostic, "zehn.decompress", -4, 9654328);
        return loader_return;
    }
    *base = program_memory;
    *entry = program_entry;
    return 0;
}
static int ndl_load(const char *path, NUC_FILE *file, void **base, int (**entry)(int, char *[]), bool *hww, struct ld_diagnostic *diagnostic) {
    (void)path;
    return zehn_load_diagnostic(file, base, entry, hww, diagnostic);
}
static int bflt_load(NUC_FILE *file, void **base, int (**entry)(int, char *[])) {
    bool hww = false;
    return zehn_load_diagnostic(file, base, entry, &hww, NULL);
}
static int TCT_Local_Control_Interrupts(int mask) { (void)mask; return 0; }
static void wait_no_key_pressed(void) {}
static unsigned _scrsize(void) { return sizeof(screen_memory); }
static void ut_disable_watchdog(void) {}
static void lcd_compat_enable(void) {}
static void lcd_compat_disable(void) {}
static void lcd_incolor(void) {}
static void clear_cache(void) {}
#define malloc probe_malloc
#define free probe_free

LOADER_SOURCE

#undef malloc
#undef free

static int cases;
static void check(const char *path, const char *stage, int returned, int code, size_t bytes, int resident) {
    ++cases;
    allocation_index = entries = closes = program_frees = 0;
    resident_argv = NULL;
    struct ld_diagnostic diagnostic = {"stale", 123, 456};
    void *base = NULL;
    int status = ld_exec_with_args_diagnostic(path, 0, NULL, resident ? &base : NULL, &diagnostic);
    assert(status == returned && !strcmp(diagnostic.stage, stage));
    assert(diagnostic.code == code && diagnostic.bytes == bytes);
    assert(closes == (!fail_stack && !fail_open && strchr(path, '.') != NULL));
    if (entries && returned != 0xDEAD && resident) {
        assert(base == program_memory && program_frees == 0);
        ld_free(base);
        probe_free(resident_argv);
    } else if (entries || fail_allocation) assert(program_frees == 1);
    assert(live_allocations == 0);
    fail_stack = fail_open = fail_signature = fail_allocation = loader_return = entry_return = 0;
    signature = 0x6e68655a;
}

int main(void) {
    fail_stack = 1;
    check("probe.tns", "stack.expand", 0xDEAD, 0xDEAD, 0, 1);
    check("probe", "path.extension", 0xDEAD, 0xDEAD, 0, 1);
    fail_open = 1;
    check("probe.tns", "file.open", 0xDEAD, 0xDEAD, 0, 1);
    fail_signature = 1;
    check("probe.tns", "file.signature.read", 0xDEAD, 0xDEAD, 4, 1);
    signature = 17;
    check("probe.tns", "file.signature.format", 0xDEAD, 0xDEAD, 0, 1);
    for (int format = 0; format < 2; ++format) {
        for (int refusal = 1; refusal <= 2; ++refusal) {
            signature = format ? 0x00475250 : 0x6e68655a;
            loader_return = refusal;
            check("probe.tns", "zehn.decompress", refusal == 1 ? 0xDEAD : 0xBEEF, -4, 9654328, 1);
        }
    }
    signature = 0x544c4662;
    loader_return = 1;
    check("probe.tns", "bflt.load", 0xDEAD, 0xDEAD, 0, 1);
    fail_allocation = 1;
    check("probe.tns", "screen.allocate", 0xDEAD, 0xDEAD, sizeof(screen_memory), 1);
    fail_allocation = 2;
    check("probe.tns", "arguments.allocate", 0xDEAD, 0xDEAD, 2 * sizeof(char *), 1);
    for (int resident = 0; resident <= 1; ++resident) {
        int codes[] = {0, 17, -8, 0xDEAD, 0xBEEF};
        for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
            entry_return = codes[i];
            check("probe.tns", codes[i] ? "program.entry" : "none", codes[i], codes[i], 0, resident);
        }
    }
    nested_entry = 1;
    entry_return = 17;
    check("probe.tns", "program.entry", 17, 17, 0, 1);
    assert(ld_exec_with_args("probe.tns", 0, NULL, NULL) == 0);
    assert(live_allocations == 0);
    printf("Outer loader diagnostic cases: %d\n", cases + 1);
}
