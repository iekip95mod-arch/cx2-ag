#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DIAGNOSTIC_SOURCE

#define PRGMSIG "PRG"
#define LD_ZEHN_BIN 3
static int ld_bin_format;
typedef struct { unsigned char bytes[64]; size_t position; } NUC_FILE;
struct nuc_stat { size_t st_size; };
static int fail_stat, fail_read, reads, fail_allocation, allocations, live_allocations, embedded_return;
static int nuc_stat(const char *path, struct nuc_stat *info) { (void)path; info->st_size = 64; return fail_stat; }
static size_t nuc_fread(void *bytes, size_t size, size_t count, NUC_FILE *file) {
    if (++reads == fail_read) return 0;
    assert(bytes && size * count <= sizeof(file->bytes) - file->position);
    memcpy(bytes, file->bytes + file->position, size * count);
    file->position += size * count;
    return count;
}
static void nuc_fseek(NUC_FILE *file, long position, int origin) { assert(origin == SEEK_SET); file->position = position; }
static void *probe_malloc(size_t size) {
    if (++allocations == fail_allocation) return NULL;
    void *allocation = malloc(size);
    if (allocation) ++live_allocations;
    return allocation;
}
static void probe_free(void *allocation) { if (allocation) --live_allocations; free(allocation); }
static void *execmem_alloc(size_t size) { return probe_malloc(size); }
static void execmem_free(void *base) { probe_free(base); }
static int zehn_load_diagnostic(NUC_FILE *file, void **base, int (**entry)(int, char *[]), bool *hww, struct ld_diagnostic *diagnostic) {
    (void)file; (void)base; (void)entry; (void)hww;
    return ld_diagnostic_set(diagnostic, "zehn.header.format", embedded_return, 0);
}
#define malloc probe_malloc
#define free probe_free

PRG_SOURCE

#undef malloc
#undef free

static int cases;
static void check(const char *stage, int code, size_t bytes, bool embedded) {
    ++cases;
    allocations = reads = 0;
    NUC_FILE file = {{0}, 0};
    memcpy(file.bytes, PRGMSIG, sizeof(PRGMSIG));
    if (embedded) {
        uint32_t words[] = {0x6e68655a, 1};
        memcpy(file.bytes + 4, words, sizeof(words));
    }
    struct ld_diagnostic diagnostic = {"stale", 123, 456};
    void *base = NULL;
    int (*entry)(int, char *[]) = NULL;
    bool hww = false;
    assert(ndl_load("probe.tns", &file, &base, &entry, &hww, &diagnostic) == code);
    assert(!strcmp(diagnostic.stage, stage) && diagnostic.code == code && diagnostic.bytes == bytes);
    if (!code) { assert(base && entry && live_allocations == 1); execmem_free(base); }
    else assert(!base && !entry);
    assert(live_allocations == 0);
    fail_stat = fail_read = fail_allocation = 0;
}

int main(void) {
    fail_stat = 1;
    check("prg.stat", 1, 0, false);
    fail_allocation = 1;
    check("prg.scan.allocate", 1, 64, false);
    fail_read = 1;
    check("prg.scan.read", 1, 64, false);
    fail_allocation = 2;
    check("prg.executable.allocate", 1, 64, false);
    fail_read = 2;
    check("prg.executable.read", 1, 64, false);
    check("none", 0, 0, false);
    embedded_return = 1;
    check("none", 0, 0, true);
    embedded_return = 2;
    check("zehn.header.format", 2, 0, true);
    assert(ld_bin_format == LD_ZEHN_BIN);
    printf("PRG loader diagnostic cases: %d\n", cases);
}
