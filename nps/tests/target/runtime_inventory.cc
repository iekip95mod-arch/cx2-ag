#include "nps/platform/nspire/integrity.h"

#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <libndls.h>

int main() {
    FILE *report = std::fopen("/documents/nps/runtime_inventory.txt.tns", "w");
    if (!report) return 1;
    std::fputs("StepCAS runtime inventory\n", report);
    file_each(get_documents_dir(), [](const char *path, void *context) {
        const char *basename = std::strrchr(path, '/');
        basename = basename ? basename + 1 : path;
        if (std::strcmp(basename, nps::kUnifiedPackageBasename) != 0 &&
            std::strcmp(basename, "nps_v4.tns") != 0) return 0;
        FILE *inventory = static_cast<FILE *>(context);
        struct stat metadata{};
        std::fprintf(inventory, "%s bytes=%lld integrity=%s\n", path,
                     ::stat(path, &metadata) == 0 ? static_cast<long long>(metadata.st_size) : -1LL,
                     nps::integrity_status_name(nps::verify_package_integrity(path, basename)));
        std::fflush(inventory);
        return 0;
    }, report);
    const bool written = std::fputs("complete\n", report) >= 0 && !std::ferror(report);
    const bool closed = std::fclose(report) == 0;
    return written && closed ? 0 : 1;
}
