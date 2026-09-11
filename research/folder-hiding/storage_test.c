#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "storage.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char testroot[1024], documents[1024], store[1024];
static char error_text[1024];

static void path_join(char *path, const char *root, const char *relative)
{
    int length = snprintf(path, 1024, "%s/%s", root, relative);
    assert(length > 0 && length < 1024);
}

static int exists(const char *root, const char *relative)
{
    char path[1024];
    path_join(path, root, relative);
    struct stat st;
    int status = lstat(path, &st);
    assert(status == 0 || errno == ENOENT);
    return status == 0;
}

static void new_directory(const char *root, const char *relative)
{
    char path[1024];
    path_join(path, root, relative);
    assert(mkdir(path, 0700) == 0);
}

static FILE *new_file(const char *root, const char *relative)
{
    char path[1024];
    path_join(path, root, relative);
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    assert(file);
    return file;
}

static void write_text(const char *root, const char *relative, const char *text)
{
    FILE *file = new_file(root, relative);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void copy_factory(const char *factory, const char *source, const char *destination, int change)
{
    char path[1024];
    path_join(path, factory, source);
    FILE *input = fopen(path, "rb");
    assert(input);
    FILE *output = new_file(documents, destination);
    unsigned char bytes[1024];
    size_t count;
    while ((count = fread(bytes, 1, sizeof bytes, input)) != 0) {
        if (change) { bytes[0] ^= 1; change = 0; }
        assert(fwrite(bytes, 1, count, output) == count);
    }
    assert(!ferror(input));
    assert(fclose(input) == 0 && fclose(output) == 0);
}

static uint32_t checksum(const char *root, const char *relative)
{
    char path[1024];
    path_join(path, root, relative);
    FILE *file = fopen(path, "rb");
    assert(file);
    uint32_t crc = UINT32_C(0xffffffff);
    int byte;
    while ((byte = fgetc(file)) != EOF) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    assert(!ferror(file) && fclose(file) == 0);
    return ~crc;
}

static void stored_slot(char *slot, unsigned batch, const char *relative)
{
    char name[64], manifest[1024];
    assert(snprintf(name, sizeof name, "batch%06u/manifest.bin", batch) > 0);
    path_join(manifest, store, name);
    FILE *file = fopen(manifest, "rb");
    assert(file);
    unsigned char header[12];
    assert(fread(header, 1, sizeof header, file) == sizeof header);
    assert(!memcmp(header, "CX2HIDE1", 8));
    uint32_t count = header[8] | (uint32_t)header[9] << 8 | (uint32_t)header[10] << 16 | (uint32_t)header[11] << 24;
    int found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char encoded[2];
        char entry[1024];
        assert(fread(encoded, 1, 2, file) == 2);
        unsigned length = encoded[0] | encoded[1] << 8;
        assert(length > 0 && length < sizeof entry);
        assert(fread(entry, 1, length, file) == length);
        entry[length] = '\0';
        if (!strcmp(relative, entry)) {
            assert(snprintf(name, sizeof name, "batch%06u/item%08u", batch, i) > 0);
            path_join(slot, store, name);
            found = 1;
        }
    }
    assert(found && fclose(file) == 0);
}

static void expect_hidden(void)
{
    assert(exists(documents, "test_app.tns"));
    assert(exists(documents, "MyLib/linalg.tns"));
    assert(exists(documents, "Examples/Getting Started.tns"));
    assert(exists(documents, "PyLib"));
    assert(!exists(documents, "000.txt"));
    assert(!exists(documents, "MyLib/numtheory.tns"));
    assert(!exists(documents, "MyLib/custom.tns"));
    assert(!exists(documents, "Empty"));
    assert(!exists(documents, "Work"));
    assert(!exists(documents, ".Custom"));
    for (unsigned i = 0; i < 150; ++i) {
        char name[32];
        snprintf(name, sizeof name, "user%03u.txt", i);
        assert(!exists(documents, name));
    }
}

static void expect_restored(uint32_t changed_checksum)
{
    assert(exists(documents, "000.txt"));
    assert(exists(documents, "Empty"));
    assert(exists(documents, "Work/Nested/Deep.tns"));
    assert(exists(documents, "Work/Nested/Empty"));
    assert(exists(documents, ".Custom"));
    assert(exists(documents, "MyLib/custom.tns"));
    assert(checksum(documents, "MyLib/numtheory.tns") == changed_checksum);
    for (unsigned i = 0; i < 150; ++i) {
        char name[32];
        snprintf(name, sizeof name, "user%03u.txt", i);
        assert(exists(documents, name));
    }
}

static void check_cycles(const char *factory)
{
    new_directory(testroot, "documents");
    path_join(documents, testroot, "documents");
    path_join(store, testroot, "store");
    new_directory(documents, "MyLib");
    new_directory(documents, "Examples");
    new_directory(documents, "PyLib");
    new_directory(documents, "Empty");
    new_directory(documents, "Work");
    new_directory(documents, "Work/Nested");
    new_directory(documents, "Work/Nested/Empty");
    new_directory(documents, ".Custom");
    write_text(documents, "000.txt", "first custom file");
    write_text(documents, "zzz-collision.txt", "last custom file");
    write_text(documents, "test_app.tns", "application fixture");
    write_text(documents, "Work/Nested/Deep.tns", "nested user content");
    write_text(documents, "MyLib/custom.tns", "custom addition");
    copy_factory(factory, "documents/MyLib/linalg.tns", "MyLib/linalg.tns", 0);
    copy_factory(factory, "documents/MyLib/numtheory.tns", "MyLib/numtheory.tns", 1);
    copy_factory(factory, "samples/en/Getting Started.tns", "Examples/Getting Started.tns", 0);
    uint32_t changed_checksum = checksum(documents, "MyLib/numtheory.tns");
    assert(checksum(documents, "MyLib/linalg.tns") == UINT32_C(0x67f38430));
    assert(changed_checksum != UINT32_C(0x33dbb66e));
    for (unsigned i = 0; i < 150; ++i) {
        char name[32];
        snprintf(name, sizeof name, "user%03u.txt", i);
        write_text(documents, name, name);
    }
    FILE *inventory = new_file(testroot, "inventory.log");
    assert(inventory && storage_inventory(documents, inventory) == 0);
    assert(ftell(inventory) > 1500);
    assert(fclose(inventory) == 0);
    assert(!exists(testroot, "store"));
    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    expect_hidden();
    assert(exists(store, "batch000001/manifest.bin"));
    assert(!exists(store, "batch000001/done"));
    write_text(documents, "zzz-collision.txt", "new conflicting content");
    assert(storage_restore(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "collision") && !exists(documents, "000.txt"));
    char original[1024], quarantined[1024];
    path_join(original, documents, "zzz-collision.txt");
    path_join(quarantined, testroot, "collision-content.txt");
    assert(rename(original, quarantined) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    expect_restored(changed_checksum);
    assert(exists(store, "batch000001/done"));

    write_text(documents, "later.tns", "new user file");
    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    expect_hidden();
    assert(!exists(documents, "later.tns"));
    write_text(documents, "arrived-while-hidden.tns", "new file while hidden");
    assert(storage_hide(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "unfinished") && exists(documents, "arrived-while-hidden.tns"));
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    expect_restored(changed_checksum);
    assert(exists(documents, "later.tns") && exists(documents, "arrived-while-hidden.tns"));

    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    char slot[1024];
    stored_slot(slot, 3, "000.txt");
    path_join(original, documents, "000.txt");
    assert(rename(slot, original) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    expect_restored(changed_checksum);

    new_directory(store, "batch000004");
    write_text(store, "batch000004/manifest.tmp", "CX2HI");
    assert(storage_hide(documents, store, error_text, sizeof error_text) == -1);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    assert(exists(store, "batch000004/done"));
    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    stored_slot(slot, 5, "zzz-collision.txt");
    path_join(quarantined, testroot, "temporarily-missing.txt");
    assert(rename(slot, quarantined) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "missing") && !exists(documents, "000.txt"));
    assert(rename(quarantined, slot) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    expect_restored(changed_checksum);

    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    new_directory(store, "batch000007");
    FILE *manifest = new_file(store, "batch000007/manifest.bin");
    const unsigned char traversal[] = { 'C','X','2','H','I','D','E','1',1,0,0,0,9,0,'.','.','/','e','s','c','a','p','e' };
    assert(fwrite(traversal, 1, sizeof traversal, manifest) == sizeof traversal && fclose(manifest) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "manifest") && !exists(testroot, "escape") && !exists(documents, "000.txt"));
}

static void check_missing_parent(void)
{
    char docs[1024], stash[1024], original[1024], saved[1024];
    new_directory(testroot, "parent-documents");
    path_join(docs, testroot, "parent-documents");
    path_join(stash, testroot, "parent-store");
    new_directory(docs, "MyLib");
    write_text(docs, "MyLib/custom.tns", "custom addition");
    write_text(docs, "000.txt", "first file");
    assert(storage_hide(docs, stash, error_text, sizeof error_text) == 0);
    path_join(original, docs, "MyLib");
    path_join(saved, testroot, "saved-parent");
    assert(rename(original, saved) == 0);
    write_text(docs, "MyLib", "conflicting parent file");
    assert(storage_restore(docs, stash, error_text, sizeof error_text) == -1);
    assert(!exists(docs, "000.txt"));
    path_join(saved, testroot, "saved-parent-conflict.txt");
    assert(rename(original, saved) == 0);
    assert(storage_restore(docs, stash, error_text, sizeof error_text) == 0);
    assert(exists(docs, "MyLib/custom.tns") && exists(docs, "000.txt"));
}

static void check_rejections(void)
{
    char docs[1024], stash[1024], outside[1024], link[1024];
    new_directory(testroot, "linked-documents");
    path_join(docs, testroot, "linked-documents");
    path_join(stash, testroot, "linked-store");
    write_text(testroot, "outside.txt", "outside content");
    path_join(outside, testroot, "outside.txt");
    path_join(link, docs, "link.tns");
    assert(symlink(outside, link) == 0);
    assert(storage_hide(docs, stash, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "symbolic") && !exists(testroot, "linked-store"));
    assert(exists(testroot, "outside.txt"));

    new_directory(testroot, "deep-documents");
    path_join(docs, testroot, "deep-documents");
    path_join(stash, testroot, "deep-store");
    char nested[1024];
    strcpy(nested, docs);
    for (unsigned i = 0; i < 65; ++i) {
        strcat(nested, "/d");
        assert(mkdir(nested, 0700) == 0);
    }
    assert(storage_hide(docs, stash, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "nesting") && !exists(testroot, "deep-store"));
    path_join(stash, docs, "store");
    assert(storage_hide(docs, stash, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "overlapping") && !exists(docs, "store"));

    new_directory(testroot, "metadata-documents");
    path_join(docs, testroot, "metadata-documents");
    new_directory(testroot, "metadata-store");
    path_join(stash, testroot, "metadata-store");
    new_directory(stash, "batch000001");
    new_directory(stash, "batch000001/manifest.tmp");
    write_text(stash, "batch000001/manifest.tmp/content.txt", "unexpected user content");
    assert(storage_restore(docs, stash, error_text, sizeof error_text) == -1);
    assert(!exists(stash, "batch000001/done") && exists(stash, "batch000001/manifest.tmp/content.txt"));

    new_directory(testroot, "extra-slot-store");
    path_join(stash, testroot, "extra-slot-store");
    new_directory(stash, "batch000001");
    FILE *manifest = new_file(stash, "batch000001/manifest.bin");
    const unsigned char empty_manifest[] = { 'C','X','2','H','I','D','E','1',0,0,0,0 };
    assert(fwrite(empty_manifest, 1, sizeof empty_manifest, manifest) == sizeof empty_manifest && fclose(manifest) == 0);
    write_text(stash, "batch000001/item00000000", "unlisted content");
    assert(storage_restore(docs, stash, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "manifest") && !exists(stash, "batch000001/done"));
    assert(exists(stash, "batch000001/item00000000"));
}

static void check_regenerated_files(void)
{
    new_directory(testroot, "regenerated-documents");
    path_join(documents, testroot, "regenerated-documents");
    path_join(store, testroot, "regenerated-store");
    write_text(documents, "NspireLogs.zip", "");
    char content[8194];
    memset(content, 'x', sizeof content - 1);
    content[sizeof content - 1] = '\0';
    write_text(documents, "same.tns", content);
    write_text(documents, "different.tns", content);
    write_text(documents, "personal.tns", "retained personal content");
    new_directory(documents, "Folder");
    assert(storage_hide(documents, store, error_text, sizeof error_text) == 0);
    write_text(documents, "NspireLogs.zip", "");
    write_text(documents, "same.tns", content);
    content[sizeof content - 2] = 'y';
    write_text(documents, "different.tns", content);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "collision") && !exists(documents, "personal.tns"));
    char path[1024], aside[1024], slot[1024];
    path_join(path, documents, "different.tns");
    path_join(aside, testroot, "regenerated-different.tns");
    assert(rename(path, aside) == 0);
    new_directory(documents, "Folder");
    assert(storage_restore(documents, store, error_text, sizeof error_text) == -1);
    assert(strstr(error_text, "collision") && !exists(documents, "personal.tns"));
    path_join(path, documents, "Folder");
    path_join(aside, testroot, "regenerated-Folder");
    assert(rename(path, aside) == 0);
    stored_slot(slot, 1, "same.tns");
    struct stat before, after;
    assert(stat(slot, &before) == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
    assert(stat(slot, &after) == 0 && before.st_ino == after.st_ino && before.st_size == after.st_size);
    assert(checksum(documents, "same.tns") == checksum(documents, "different.tns"));
    assert(exists(documents, "NspireLogs.zip") && exists(documents, "personal.tns"));
    assert(exists(store, "batch000001/done"));
    stored_slot(slot, 1, "NspireLogs.zip");
    assert(stat(slot, &after) == 0 && after.st_size == 0);
    assert(storage_restore(documents, store, error_text, sizeof error_text) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    strcpy(testroot, "/tmp/cx2-storage-test-XXXXXX");
    assert(mkdtemp(testroot));
    check_cycles(argv[1]);
    check_missing_parent();
    check_rejections();
    check_regenerated_files();
    printf("storage: factory hashes, 150 entries, nested/empty folders, collision preflight, repeat cycles, crash recovery, and path checks passed\n%s\n", testroot);
    return 0;
}
