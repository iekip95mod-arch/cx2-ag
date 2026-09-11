#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <zehn.h>

struct NUC_FILE {
    std::vector<uint8_t> bytes;
    size_t position = 0;
};

static int allocation_index, fail_allocation, live_allocations, null_reads;
static int read_index, fail_read, decompress_status, case_count;
static void *probe_malloc(size_t size) {
    if (++allocation_index == fail_allocation) return nullptr;
    void *allocation = std::malloc(size ? size : 1);
    if (allocation) ++live_allocations;
    return allocation;
}
static void probe_free(void *allocation) {
    if (allocation) --live_allocations;
    std::free(allocation);
}
static void *execmem_alloc(size_t size) { return probe_malloc(size); }
static void execmem_free(void *allocation) { probe_free(allocation); }
static long nuc_ftell(NUC_FILE *file) { return static_cast<long>(file->position); }
static size_t nuc_fread(void *bytes, size_t size, size_t count, NUC_FILE *file) {
    if (!size || !count) return 0;
    if (++read_index == fail_read) return 0;
    if (!bytes) { ++null_reads; return 0; }
    size_t read_count = std::min(count, (file->bytes.size() - file->position) / size);
    std::memcpy(bytes, file->bytes.data() + file->position, size * read_count);
    file->position += size * read_count;
    return read_count;
}
using uLongf = unsigned long;
static const int Z_OK = 0;
static int uncompress(uint8_t *out, uLongf *length, const uint8_t *, size_t) {
    if (decompress_status) return decompress_status;
    std::memset(out, 0, *length);
    return Z_OK;
}
static void show_msgbox(const char *, const char *) {}
static bool has_colors = true, is_touchpad = true, is_cm = false;
static bool catalog_pressed;
static bool isKeyPressed(int) { return catalog_pressed; }
#define KEY_NSPIRE_CAT 1
#define NDL_VERSION 64
#define NDL_REVISION 2022
#define malloc probe_malloc
#define free probe_free

LOAD_SOURCE

#undef malloc
#undef free

static NUC_FILE package(std::vector<Zehn_reloc> relocs = {{Zehn_reloc_type::FILE_COMPRESSED, 0}},
                        std::vector<Zehn_flag> flags = {{Zehn_flag_type::EXECUTABLE_VERSION, 1}}, uint32_t extra_size = 4) {
    NUC_FILE file;
    uint32_t metadata = sizeof(Zehn_header) + 4 * static_cast<uint32_t>(relocs.size() + flags.size()) + extra_size;
    Zehn_header header{ZEHN_SIGNATURE, ZEHN_VERSION, metadata + 8,
                       static_cast<uint32_t>(relocs.size()), static_cast<uint32_t>(flags.size()),
                       extra_size, metadata + 16, 0};
    file.bytes.resize(header.file_size);
    std::memcpy(file.bytes.data(), &header, sizeof(header));
    if (!relocs.empty()) std::memcpy(file.bytes.data() + sizeof(header), relocs.data(), relocs.size() * 4);
    if (!flags.empty()) std::memcpy(file.bytes.data() + sizeof(header) + relocs.size() * 4, flags.data(), flags.size() * 4);
    std::memset(file.bytes.data() + metadata - extra_size, 'x', extra_size - 1);
    file.bytes[metadata - 1] = 0;
    return file;
}

static void check(NUC_FILE file, const char *stage, int code = 1, size_t bytes = 0, int status = 1) {
    ++case_count;
    allocation_index = read_index = 0;
    ld_diagnostic diagnostic{"stale", 123, 456};
    void *base = nullptr;
    int (*entry)(int, char *[]) = nullptr;
    bool supports_hww = false;
    assert(zehn_load_diagnostic(&file, &base, &entry, &supports_hww, &diagnostic) == status);
    if (std::strcmp(diagnostic.stage, stage)) std::cerr << diagnostic.stage << " expected " << stage << '\n';
    assert(std::strcmp(diagnostic.stage, stage) == 0);
    assert(diagnostic.code == code && diagnostic.bytes == bytes);
    assert(null_reads == 0);
    if (status) assert(base == nullptr && entry == nullptr);
    else {
        assert(base && entry && live_allocations == 1);
        execmem_free(base);
    }
    assert(live_allocations == 0);
    fail_allocation = fail_read = decompress_status = 0;
}

int main() {
    for (int allocation = 1; allocation <= 5; ++allocation) {
        fail_allocation = allocation;
        check(package(), allocation <= 3 ? "zehn.metadata.allocate" :
              allocation == 4 ? "zehn.executable.allocate" : "zehn.compressed.allocate",
              1, allocation <= 3 ? 4 : allocation == 4 ? 16 : 8);
    }
    for (int read = 1; read <= 5; ++read) {
        fail_read = read;
        check(package(), read == 1 ? "zehn.header.read" : read == 5 ? "zehn.compressed.read" : "zehn.metadata.read",
              1, read == 1 ? sizeof(Zehn_header) : read == 5 ? 8 : 0);
    }
    for (int code : {-3, -4, -5}) {
        decompress_status = code;
        check(package(), "zehn.decompress", code, 16);
    }
    NUC_FILE wrong_header = package();
    wrong_header.bytes[0] = 0;
    check(wrong_header, "zehn.header.format");
    check(package({{Zehn_reloc_type::FILE_COMPRESSED, 1}}), "zehn.compression.format");
    fail_read = 4;
    check(package({}), "zehn.executable.read", 1, 8);
    check(package({{Zehn_reloc_type::SET_ZERO, 16}}), "zehn.relocation.offset");
    check(package({{Zehn_reloc_type::UNALIGNED_RELOC, 1}}), "zehn.relocation.unaligned");
    check(package({{static_cast<Zehn_reloc_type>(99), 0}}), "zehn.relocation.type");
    check(package({}, {{Zehn_flag_type::RUNS_ON_COLOR, 0}}), "zehn.color", 2, 0, 2);
    check(package({}, {{Zehn_flag_type::RUNS_ON_TOUCHPAD, 0}}), "zehn.touchpad", 2, 0, 2);
    is_touchpad = false;
    check(package({}, {{Zehn_flag_type::RUNS_ON_CLICKPAD, 0}}), "zehn.clickpad", 2, 0, 2);
    is_touchpad = true;
    is_cm = true;
    check(package({}, {{Zehn_flag_type::RUNS_ON_32MB, 0}}), "zehn.ram", 2, 0, 2);
    is_cm = false;
    catalog_pressed = true;
    check(package(), "zehn.information", 2, 0, 2);
    catalog_pressed = false;
    check(package({}, {{Zehn_flag_type::NDL_VERSION_MIN, 65}}), "zehn.version.minimum", 2, 0, 2);
    check(package({}, {{Zehn_flag_type::NDL_VERSION_MAX, 63}}), "zehn.version.maximum", 2, 0, 2);
    check(package({}, {{Zehn_flag_type::NDL_REVISION_MIN, 2023}, {Zehn_flag_type::NDL_VERSION_MIN, 64}}), "zehn.version.minimum", 2, 0, 2);
    check(package({}, {{Zehn_flag_type::NDL_REVISION_MAX, 2021}, {Zehn_flag_type::NDL_VERSION_MAX, 64}}), "zehn.version.maximum", 2, 0, 2);
    check(package({}, {{Zehn_flag_type::EXECUTABLE_NAME, 0}}, 260), "zehn.name");
    check(package(), "none", 0, 0, 0);
    check(package({{Zehn_reloc_type::SET_ZERO, 0}}), "none", 0, 0, 0);
    NUC_FILE legacy = package();
    void *base = nullptr;
    int (*entry)(int, char *[]) = nullptr;
    bool supports_hww = false;
    assert(zehn_load(&legacy, &base, &entry, &supports_hww) == 0);
    execmem_free(base);
    assert(live_allocations == 0);
    std::cout << "Zehn diagnostic cases: " << case_count + 1 << '\n';
}
