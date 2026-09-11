#ifdef _TINSPIRE
#include <os.h>
#endif
#include "storage.h"
#include "factory-6.4/factory_manifest.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_CAPACITY 1024
#define MAX_DEPTH 64

struct paths {
    char **items;
    size_t count, capacity;
};

struct failure {
    char *text;
    size_t capacity;
};

static int fail(struct failure *error, const char *format, ...)
{
    if (error->text && error->capacity) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(error->text, error->capacity, format, arguments);
        va_end(arguments);
    }
    return -1;
}

static void free_paths(struct paths *paths)
{
    for (size_t i = 0; i < paths->count; ++i) free(paths->items[i]);
    free(paths->items);
    memset(paths, 0, sizeof *paths);
}

static int add_path(struct paths *paths, const char *path, struct failure *error)
{
    if (paths->count == paths->capacity) {
        size_t capacity = paths->capacity ? paths->capacity * 2 : 16;
        if (capacity < paths->capacity || capacity > SIZE_MAX / sizeof *paths->items)
            return fail(error, "too many entries");
        char **items = realloc(paths->items, capacity * sizeof *items);
        if (!items) return fail(error, "allocation failed");
        paths->items = items;
        paths->capacity = capacity;
    }
    char *copy = malloc(strlen(path) + 1);
    if (!copy) return fail(error, "allocation failed");
    strcpy(copy, path);
    paths->items[paths->count++] = copy;
    return 0;
}

static int join_path(char *joined, const char *parent, const char *name, struct failure *error)
{
    int length = snprintf(joined, PATH_CAPACITY, "%s%s%s", parent,
                          *parent && strcmp(parent, "/") ? "/" : "", name);
    return length < 0 || length >= PATH_CAPACITY ? fail(error, "path too long") : 0;
}

static int valid_relative(const char *path)
{
    if (!*path || *path == '/' || strlen(path) >= PATH_CAPACITY || strchr(path, '\\')) return 0;
    unsigned depth = 0;
    for (const char *part = path; *part;) {
        const char *slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.') || ++depth > MAX_DEPTH) return 0;
        if (!slash) return 1;
        part = slash + 1;
    }
    return 0;
}

static int root_path(char *copy, const char *root, struct failure *error)
{
    if (!root || root[0] != '/' || strlen(root) >= PATH_CAPACITY)
        return fail(error, "invalid root");
    strcpy(copy, root);
    size_t length = strlen(copy);
    while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
    if (strcmp(copy, "/") && !valid_relative(copy + 1)) return fail(error, "invalid root");
    return 0;
}

static int path_state(const char *path, struct stat *st, struct failure *error)
{
#ifdef _TINSPIRE
    int status = stat(path, st);
#else
    int status = lstat(path, st);
#endif
    if (status != 0) return errno == ENOENT ? 0 : fail(error, "stat failed: %s (%d)", path, errno);
#ifdef S_ISLNK
    if (S_ISLNK(st->st_mode)) return fail(error, "symbolic link refused: %s", path);
#endif
    if (!S_ISDIR(st->st_mode) && !S_ISREG(st->st_mode)) return fail(error, "unsupported entry: %s", path);
    return 1;
}

static int require_directory(const char *path, struct failure *error)
{
    struct stat st;
    int state = path_state(path, &st, error);
    if (state < 0) return -1;
    return state && S_ISDIR(st.st_mode) ? 0 : fail(error, "directory missing: %s", path);
}

static int compare_paths(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static int read_directory(const char *path, struct paths *names, struct failure *error)
{
    DIR *directory = opendir(path);
    if (!directory) return fail(error, "open directory failed: %s (%d)", path, errno);
    int status = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) status = fail(error, "read directory failed: %s (%d)", path, errno);
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strchr(entry->d_name, '/') || !valid_relative(entry->d_name)) {
            status = fail(error, "invalid directory entry: %s", path);
            break;
        }
        if (add_path(names, entry->d_name, error)) { status = -1; break; }
    }
    if (closedir(directory) != 0 && !status) status = fail(error, "close directory failed: %s", path);
    if (!status && names->count) qsort(names->items, names->count, sizeof *names->items, compare_paths);
    return status;
}

static int inspect_tree(const char *source, const char *destination, unsigned depth, struct failure *error)
{
    if (depth > MAX_DEPTH) return fail(error, "directory nesting exceeds %d", MAX_DEPTH);
    struct stat st;
    int state = path_state(source, &st, error);
    if (state < 0) return -1;
    if (!state) return fail(error, "source missing: %s", source);
    if (!S_ISDIR(st.st_mode)) return 0;
    struct paths names = { 0 };
    int status = read_directory(source, &names, error);
    for (size_t i = 0; !status && i < names.count; ++i) {
        char child[PATH_CAPACITY], target[PATH_CAPACITY];
        status = join_path(child, source, names.items[i], error);
        if (!status && destination) status = join_path(target, destination, names.items[i], error);
        if (!status) status = inspect_tree(child, destination ? target : NULL, depth + 1, error);
    }
    free_paths(&names);
    return status;
}

static int file_crc(const char *path, uint64_t expected_size, uint32_t *crc, struct failure *error)
{
    FILE *file = fopen(path, "rb");
    if (!file) return fail(error, "open file failed: %s (%d)", path, errno);
    unsigned char bytes[1024];
    uint64_t size = 0;
    uint32_t checksum = UINT32_C(0xffffffff);
    size_t count;
    while ((count = fread(bytes, 1, sizeof bytes, file)) != 0) {
        size += count;
        for (size_t i = 0; i < count; ++i) {
            checksum ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                checksum = (checksum >> 1) ^ ((checksum & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    int status = ferror(file) ? fail(error, "read file failed: %s", path) : 0;
    if (fclose(file) != 0 && !status) status = fail(error, "close file failed: %s", path);
    if (!status && size != expected_size) status = fail(error, "file changed: %s", path);
    *crc = ~checksum;
    return status;
}

static int factory_directory(const char *relative)
{
    size_t length = strlen(relative);
    for (size_t i = 0; i < FACTORY_FILE_COUNT; ++i)
        if (!strncmp(factory_files[i].path, relative, length) && factory_files[i].path[length] == '/') return 1;
    return 0;
}

static int factory_file(const char *path, const char *relative, const struct stat *st, struct failure *error)
{
    int candidate = 0;
    for (size_t i = 0; i < FACTORY_FILE_COUNT; ++i)
        if (!strcmp(factory_files[i].path, relative) && st->st_size >= 0 &&
            (uint64_t)st->st_size == factory_files[i].size) candidate = 1;
    if (!candidate) return 0;
    uint32_t crc;
    if (file_crc(path, (uint64_t)st->st_size, &crc, error)) return -1;
    for (size_t i = 0; i < FACTORY_FILE_COUNT; ++i)
        if (!strcmp(factory_files[i].path, relative) && (uint64_t)st->st_size == factory_files[i].size &&
            crc == factory_files[i].crc32) return 1;
    return 0;
}

static int build_plan(const char *docroot, const char *relative, unsigned depth,
                      struct paths *plan, FILE *log, struct failure *error)
{
    if (depth > MAX_DEPTH) return fail(error, "directory nesting exceeds %d", MAX_DEPTH);
    char directory[PATH_CAPACITY];
    if (*relative) {
        if (join_path(directory, docroot, relative, error)) return -1;
    } else strcpy(directory, docroot);
    struct paths names = { 0 };
    int status = read_directory(directory, &names, error);
    for (size_t i = 0; !status && i < names.count; ++i) {
        char child[PATH_CAPACITY], path[PATH_CAPACITY];
        struct stat st;
        if (join_path(child, relative, names.items[i], error) || join_path(path, docroot, child, error)) {
            status = -1;
            break;
        }
        int state = path_state(path, &st, error);
        if (state != 1) { status = state < 0 ? -1 : fail(error, "source missing: %s", path); break; }
        int protected = S_ISDIR(st.st_mode) ? factory_directory(child) :
            !strcmp(child, "test_app.tns") ? 1 : factory_file(path, child, &st, error);
        if (protected < 0) { status = -1; break; }
        if (log && fprintf(log, "%s\t%s\n", protected ? "preserve" : "conceal", child) < 0) {
            status = fail(error, "inventory write failed");
            break;
        }
        if (protected && S_ISDIR(st.st_mode)) status = build_plan(docroot, child, depth + 1, plan, log, error);
        else if (!protected) {
            status = inspect_tree(path, NULL, depth + 1, error);
            if (!status) status = add_path(plan, child, error);
        }
    }
    free_paths(&names);
    return status;
}

static int batch_name(const char *name)
{
    if (strlen(name) != 11 || strncmp(name, "batch", 5)) return 0;
    for (unsigned i = 5; i < 11; ++i) if (name[i] < '0' || name[i] > '9') return 0;
    return strcmp(name, "batch000000") != 0;
}

static int finished_batch(const char *batch, struct failure *error)
{
    char path[PATH_CAPACITY];
    if (join_path(path, batch, "done", error)) return -1;
    struct stat st;
    int state = path_state(path, &st, error);
    if (state <= 0) return state;
    if (!S_ISDIR(st.st_mode)) return fail(error, "invalid completion marker: %s", path);
    struct paths names = { 0 };
    int status = read_directory(path, &names, error);
    if (!status && names.count) status = fail(error, "completion marker is not empty: %s", path);
    free_paths(&names);
    return status ? -1 : 1;
}

static int write_manifest(const char *batch, const struct paths *plan, struct failure *error)
{
    char temporary[PATH_CAPACITY], committed[PATH_CAPACITY];
    if (join_path(temporary, batch, "manifest.tmp", error) || join_path(committed, batch, "manifest.bin", error)) return -1;
    if (plan->count > UINT32_MAX) return fail(error, "too many manifest entries");
    FILE *file = fopen(temporary, "wb");
    if (!file) return fail(error, "open manifest failed: %s", temporary);
    uint32_t count = plan->count;
    unsigned char header[12] = { 'C', 'X', '2', 'H', 'I', 'D', 'E', '1', count, count >> 8, count >> 16, count >> 24 };
    int status = fwrite(header, 1, sizeof header, file) == sizeof header ? 0 : -1;
    for (size_t i = 0; !status && i < plan->count; ++i) {
        size_t length = strlen(plan->items[i]);
        unsigned char encoded[2] = { length, length >> 8 };
        if (fwrite(encoded, 1, 2, file) != 2 || fwrite(plan->items[i], 1, length, file) != length) status = -1;
    }
    if (fclose(file) != 0) status = -1;
    if (status) return fail(error, "write manifest failed: %s", temporary);
    struct stat st;
    int state = path_state(committed, &st, error);
    if (state < 0) return -1;
    if (state || rename(temporary, committed) != 0) return fail(error, "commit manifest failed: %s", committed);
    return 0;
}

static int read_manifest(const char *batch, struct paths *plan, struct failure *error)
{
    char path[PATH_CAPACITY];
    if (join_path(path, batch, "manifest.bin", error)) return -1;
    struct stat st;
    int state = path_state(path, &st, error);
    if (state < 0) return -1;
    if (!state) {
        struct paths names = { 0 };
        int status = read_directory(batch, &names, error);
        for (size_t i = 0; !status && i < names.count; ++i) {
            char temporary[PATH_CAPACITY];
            if (strcmp(names.items[i], "manifest.tmp")) status = fail(error, "uncommitted batch contains entries: %s", batch);
            else if (join_path(temporary, batch, names.items[i], error) ||
                     path_state(temporary, &st, error) != 1 || !S_ISREG(st.st_mode)) status = fail(error, "invalid incomplete manifest: %s", batch);
        }
        free_paths(&names);
        return status;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 12) return fail(error, "invalid manifest: %s", path);
    FILE *file = fopen(path, "rb");
    if (!file) return fail(error, "open manifest failed: %s", path);
    unsigned char header[12];
    int status = fread(header, 1, sizeof header, file) == sizeof header && !memcmp(header, "CX2HIDE1", 8) ? 0 : -1;
    uint32_t count = 0;
    if (!status) {
        count = header[8] | (uint32_t)header[9] << 8 | (uint32_t)header[10] << 16 | (uint32_t)header[11] << 24;
        if ((uint64_t)count > ((uint64_t)st.st_size - 12) / 3) status = -1;
    }
    for (uint32_t i = 0; !status && i < count; ++i) {
        unsigned char encoded[2];
        char relative[PATH_CAPACITY];
        if (fread(encoded, 1, 2, file) != 2) { status = -1; break; }
        unsigned length = encoded[0] | encoded[1] << 8;
        if (!length || length >= sizeof relative || fread(relative, 1, length, file) != length) { status = -1; break; }
        relative[length] = '\0';
        if (strlen(relative) != length || !valid_relative(relative)) { status = -1; break; }
        for (size_t j = 0; j < plan->count; ++j) {
            size_t earlier = strlen(plan->items[j]);
            if (!strcmp(relative, plan->items[j]) ||
                (!strncmp(relative, plan->items[j], earlier) && relative[earlier] == '/') ||
                (!strncmp(relative, plan->items[j], length) && plan->items[j][length] == '/')) { status = -1; break; }
        }
        if (!status && add_path(plan, relative, error)) status = -1;
    }
    if (!status && (fgetc(file) != EOF || ferror(file))) status = -1;
    if (fclose(file) != 0) status = -1;
    struct paths names = { 0 };
    if (!status) status = read_directory(batch, &names, error);
    for (size_t i = 0; !status && i < names.count; ++i) {
        const char *name = names.items[i];
        if (!strcmp(name, "manifest.bin")) continue;
        if (strncmp(name, "item", 4)) { status = -1; break; }
        char *end;
        errno = 0;
        unsigned long index = strtoul(name + 4, &end, 10);
        char canonical[32];
        int length = snprintf(canonical, sizeof canonical, "item%08lu", index);
        if (errno || *end || end == name + 4 || index >= plan->count || length < 0 ||
            (size_t)length >= sizeof canonical || strcmp(name, canonical)) status = -1;
    }
    free_paths(&names);
    return status ? fail(error, "invalid or unreadable manifest: %s", path) : 0;
}

static int slot_path(char *path, const char *batch, size_t index, struct failure *error)
{
    char name[32];
    int length = snprintf(name, sizeof name, "item%08lu", (unsigned long)index);
    return length < 0 || (size_t)length >= sizeof name ? fail(error, "invalid slot") : join_path(path, batch, name, error);
}

static int check_parent(const char *root, const char *relative, struct failure *error)
{
    char path[PATH_CAPACITY];
    strcpy(path, root);
    for (const char *part = relative; ; ) {
        const char *slash = strchr(part, '/');
        if (!slash) return 0;
        char component[PATH_CAPACITY], joined[PATH_CAPACITY];
        size_t length = slash - part;
        memcpy(component, part, length);
        component[length] = '\0';
        if (join_path(joined, path, component, error) || require_directory(joined, error)) return -1;
        strcpy(path, joined);
        part = slash + 1;
    }
}

static int plan_parents(const char *root, const char *relative, struct paths *directories, struct failure *error)
{
    char path[PATH_CAPACITY];
    strcpy(path, root);
    for (const char *part = relative; ; ) {
        const char *slash = strchr(part, '/');
        if (!slash) return 0;
        char component[PATH_CAPACITY], joined[PATH_CAPACITY];
        size_t length = slash - part;
        memcpy(component, part, length);
        component[length] = '\0';
        if (join_path(joined, path, component, error)) return -1;
        struct stat st;
        int state = path_state(joined, &st, error);
        if (state < 0) return -1;
        if (state && !S_ISDIR(st.st_mode)) return fail(error, "restore parent is not a directory: %s", joined);
        if (!state) {
            size_t i;
            for (i = 0; i < directories->count; ++i)
                if (!strcmp(directories->items[i], joined)) break;
            if (i == directories->count && add_path(directories, joined, error)) return -1;
        }
        strcpy(path, joined);
        part = slash + 1;
    }
}

static int move_entry(const char *source, const char *destination, struct failure *error)
{
    struct stat st;
    int from = path_state(source, &st, error), to;
    if (from < 0) return -1;
    to = path_state(destination, &st, error);
    if (to < 0) return -1;
    if (!from || to) return fail(error, "move collision or missing source: %s", destination);
    return rename(source, destination) == 0 ? 0 : fail(error, "move failed: %s (%d)", source, errno);
}

static int identical_files(const char *source, const char *destination, struct failure *error)
{
    struct stat from, to;
    if (path_state(source, &from, error) != 1 || path_state(destination, &to, error) != 1) return -1;
    if (!S_ISREG(from.st_mode) || !S_ISREG(to.st_mode) || from.st_size != to.st_size) return 0;
    FILE *stored = fopen(source, "rb"), *visible = fopen(destination, "rb");
    int same = stored && visible ? 1 : -1;
    unsigned char stored_bytes[4096], visible_bytes[4096];
    while (same == 1) {
        size_t stored_count = fread(stored_bytes, 1, sizeof stored_bytes, stored);
        size_t visible_count = fread(visible_bytes, 1, sizeof visible_bytes, visible);
        if (ferror(stored) || ferror(visible)) same = -1;
        else if (stored_count != visible_count || memcmp(stored_bytes, visible_bytes, stored_count)) same = 0;
        if (!stored_count || same != 1) break;
    }
    if (stored && fclose(stored)) same = -1;
    if (visible && fclose(visible)) same = -1;
    return same < 0 ? fail(error, "compare failed: %s", destination) : same;
}

static int roots(const char *documents, const char *store, char *docroot, char *storeroot, struct failure *error)
{
    if (root_path(docroot, documents, error) || root_path(storeroot, store, error) || require_directory(docroot, error)) return -1;
    size_t doc_length = strlen(docroot), store_length = strlen(storeroot);
    if (!strcmp(docroot, storeroot) || !strcmp(docroot, "/") || !strcmp(storeroot, "/") ||
        (!strncmp(docroot, storeroot, store_length) && docroot[store_length] == '/') ||
        (!strncmp(storeroot, docroot, doc_length) && storeroot[doc_length] == '/')) return fail(error, "overlapping roots");
    return 0;
}

int storage_inventory(const char *documents, FILE *log)
{
    struct failure error = { NULL, 0 };
    struct paths plan = { 0 };
    char docroot[PATH_CAPACITY];
    if (!log || root_path(docroot, documents, &error) || require_directory(docroot, &error)) return -1;
    int status = build_plan(docroot, "", 0, &plan, log, &error);
    free_paths(&plan);
    if (fflush(log) != 0) status = -1;
    return status;
}

int storage_hide(const char *documents, const char *store, char *message, size_t message_size)
{
    struct failure error = { message, message_size };
    if (message && message_size) message[0] = '\0';
    char docroot[PATH_CAPACITY], storeroot[PATH_CAPACITY], batch[PATH_CAPACITY];
    if (roots(documents, store, docroot, storeroot, &error)) return -1;
    struct stat st;
    int store_state = path_state(storeroot, &st, &error);
    if (store_state < 0 || (store_state && !S_ISDIR(st.st_mode))) return fail(&error, "invalid store");
    struct paths names = { 0 }, plan = { 0 };
    int status = store_state ? read_directory(storeroot, &names, &error) : 0;
    for (size_t i = 0; !status && i < names.count; ++i) {
        if (!batch_name(names.items[i])) continue;
        if (join_path(batch, storeroot, names.items[i], &error) || require_directory(batch, &error)) { status = -1; break; }
        int finished = finished_batch(batch, &error);
        if (finished <= 0) status = finished < 0 ? -1 : fail(&error, "unfinished batch, restore first: %s", batch);
    }
    free_paths(&names);
    if (!status) status = build_plan(docroot, "", 0, &plan, NULL, &error);
    if (status || !plan.count) { free_paths(&plan); return status; }
    unsigned number;
    for (number = 1; number <= 999999; ++number) {
        char name[16];
        snprintf(name, sizeof name, "batch%06u", number);
        if (join_path(batch, storeroot, name, &error)) { status = -1; break; }
        int state = path_state(batch, &st, &error);
        if (state < 0) { status = -1; break; }
        if (!state) break;
    }
    if (!status && number > 999999) status = fail(&error, "batch identifiers exhausted");
    for (size_t i = 0; !status && i < plan.count; ++i) {
        char source[PATH_CAPACITY], destination[PATH_CAPACITY];
        if (join_path(source, docroot, plan.items[i], &error) || slot_path(destination, batch, i, &error) ||
            check_parent(docroot, plan.items[i], &error) || inspect_tree(source, destination, 0, &error)) { status = -1; break; }
        int state = path_state(destination, &st, &error);
        if (state != 0) status = state < 0 ? -1 : fail(&error, "destination exists: %s", destination);
    }
    if (!status && !store_state && mkdir(storeroot, 0700) != 0) status = fail(&error, "create store failed: %s", storeroot);
    if (!status && mkdir(batch, 0700) != 0) status = fail(&error, "create batch failed: %s", batch);
    if (!status) status = write_manifest(batch, &plan, &error);
    for (size_t i = 0; !status && i < plan.count; ++i) {
        char source[PATH_CAPACITY], destination[PATH_CAPACITY];
        if (join_path(source, docroot, plan.items[i], &error) || slot_path(destination, batch, i, &error) ||
            check_parent(docroot, plan.items[i], &error)) { status = -1; break; }
        status = move_entry(source, destination, &error);
    }
    free_paths(&plan);
    return status;
}

int storage_restore(const char *documents, const char *store, char *message, size_t message_size)
{
    struct failure error = { message, message_size };
    if (message && message_size) message[0] = '\0';
    char docroot[PATH_CAPACITY], storeroot[PATH_CAPACITY];
    if (roots(documents, store, docroot, storeroot, &error)) return -1;
    struct stat st;
    int state = path_state(storeroot, &st, &error);
    if (state <= 0) return state;
    if (!S_ISDIR(st.st_mode)) return fail(&error, "invalid store");
    struct paths batches = { 0 }, sources = { 0 }, destinations = { 0 }, markers = { 0 }, directories = { 0 };
    int status = read_directory(storeroot, &batches, &error);
    for (size_t b = 0; !status && b < batches.count; ++b) {
        if (!batch_name(batches.items[b])) continue;
        char batch[PATH_CAPACITY], marker[PATH_CAPACITY];
        if (join_path(batch, storeroot, batches.items[b], &error) || require_directory(batch, &error)) { status = -1; break; }
        int finished = finished_batch(batch, &error);
        if (finished < 0) { status = -1; break; }
        if (finished) continue;
        struct paths entries = { 0 };
        status = read_manifest(batch, &entries, &error);
        for (size_t i = 0; !status && i < entries.count; ++i) {
            char original[PATH_CAPACITY], stored[PATH_CAPACITY];
            if (join_path(original, docroot, entries.items[i], &error) || slot_path(stored, batch, i, &error) ||
                plan_parents(docroot, entries.items[i], &directories, &error)) { status = -1; break; }
            int from = path_state(stored, &st, &error);
            if (from < 0) { status = -1; break; }
            int to = path_state(original, &st, &error);
            if (to < 0) { status = -1; break; }
            if (from && to) {
                int same = identical_files(stored, original, &error);
                if (same < 0) { status = -1; break; }
                if (same) continue;
            }
            if (from == to) { status = fail(&error, from ? "restore collision: %s" : "restore entry missing: %s", original); break; }
            if (!from) continue;
            for (size_t j = 0; j < destinations.count; ++j) {
                size_t length = strlen(original), earlier = strlen(destinations.items[j]);
                if (!strcmp(original, destinations.items[j]) ||
                    (!strncmp(original, destinations.items[j], earlier) && original[earlier] == '/') ||
                    (!strncmp(original, destinations.items[j], length) && destinations.items[j][length] == '/')) {
                    status = fail(&error, "overlapping restore destination: %s", original);
                    break;
                }
            }
            if (!status) status = inspect_tree(stored, original, 0, &error);
            if (!status) status = add_path(&sources, stored, &error);
            if (!status) status = add_path(&destinations, original, &error);
        }
        free_paths(&entries);
        if (!status && (join_path(marker, batch, "done", &error) || add_path(&markers, marker, &error))) status = -1;
    }
    for (size_t i = 0; !status && i < directories.count; ++i)
        if (mkdir(directories.items[i], 0700) != 0) status = fail(&error, "create restore parent failed: %s", directories.items[i]);
    for (size_t i = 0; !status && i < sources.count; ++i) {
        status = check_parent(docroot, destinations.items[i] + strlen(docroot) + 1, &error);
        if (!status) status = move_entry(sources.items[i], destinations.items[i], &error);
    }
    for (size_t i = 0; !status && i < markers.count; ++i)
        if (mkdir(markers.items[i], 0700) != 0) status = fail(&error, "complete batch failed: %s", markers.items[i]);
    free_paths(&batches);
    free_paths(&sources);
    free_paths(&destinations);
    free_paths(&markers);
    free_paths(&directories);
    return status;
}
