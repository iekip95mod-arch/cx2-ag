/**
 * The way this persistency loader works:
 * On startup the operating system will try to execute /phoenix/syst/poweroff/currentdoc.tns
 * This file is created when the user presses Ctrl + On to poweroff the device,
 * such that if the calculator resets it will restore their work.
 * 
 * We simply need to copy a headless ndl installer to the directory, then on
 * resources we can copy it again (the OS deletes it) and then patch some routines
 * 
 * This does disable the operating system's mechanism to save documents.
 * The loader will be deleted on ndl uninstall.
 * 
 * Contributors: @delta (primary loader), @sasdallas (cleanup + uninstaller)
 */

#include <stdio.h>
#include <os.h>
#include <sys/stat.h>
#include <stdint.h>
#include <keys.h>
#include "ndl.h"
#include "hook.h"
#include <nucleus.h>

// OS-specific 
// Address of the save dialog function
static unsigned const save_dialog_hook_addrs[NDL_MAX_OSID+1] =
                           {0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x10027BC8, 0x10027CA4, 0x10027CE4,
                        0x10027BDC, 0x10027CBC, 0x10027CEC};

// OS-specific
// TI_TM_CreateState hook address used to prevent the OS from creating the snapshot
static unsigned const create_state_addrs[NDL_MAX_OSID+1] =
                            {0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x10031D38, 0x10031E44, 0x10031E54,
                        0x10031D4C, 0x10031E5C, 0x10031E5C};

// OS-specific
// TI_TM_ClearSnapshot hook address used to prevent the OS from clearing the snapshot
static unsigned const clear_state_addrs[NDL_MAX_OSID+1] =
                            {0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x0, 0x0, 0x0,
                        0x0, 0x0,
                        0x100319d8, 0x10031AE4, 0x10031AF4,
                        0x100319EC, 0x10031AFC, 0x10031AFC};

// the installer tends to be around 1-2K so this is more than enough
// stack-allocating this causes the calculator to complain on real hw but not emulator!
static char buf[1024];


// helper
int copy_file(const char *src, const char *dst) {
	FILE *in = fopen(src, "rb");
	if (!in) return errno == ENOENT ? 2 : 1;
	FILE *out = fopen(dst, "wb");
	if (!out) { fclose(in); return 1; }

	size_t n;
	int failed = 0;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { failed = 1; break; }
    }

	if (ferror(in)) failed = 1;
	if (fclose(in)) failed = 1;
	if (fclose(out)) failed = 1;
	return failed;
}

HOOK_DEFINE(save_dialog_hook) {
	// this kills the save dialog that normally appears on startup - makes it auto say no
	unsigned *regs = HOOK_SAVED_REGS(save_dialog_hook);
	
    // zero the flag in the dialog struct
	if (regs[0]) *(unsigned*)regs[0] = 0;

    // set return value to magic no
	regs[0] = 0x13F2;
	
    HOOK_RESTORE_SP(save_dialog_hook);
	HOOK_RESTORE_STATE();
	__asm volatile("bx lr");
}

HOOK_DEFINE(create_state_hook) {
    HOOK_RESTORE_SP(create_state_hook);
	HOOK_RESTORE_STATE();
	__asm volatile("bx lr");
}

HOOK_DEFINE(clear_state_hook) {
    HOOK_RESTORE_SP(clear_state_hook);
    HOOK_RESTORE_STATE();
    __asm volatile ("bx lr");
}

// called when a 'P' is passed to the ndless loader. CX II only for now
static int persistence_exists(const char *path) {
    struct stat info;
    if (!stat(path, &info)) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int persistence_remove(const char *path) {
    if (!unlink(path) || errno == ENOENT) return 0;
    puts("Persistence cleanup failed. Retain the .ndl files for recovery.");
    return 1;
}

int persistency_install(void) {
    if (isKeyPressed(KEY_NSPIRE_ESC)) {
        return 2;
    }

    struct {
        const char *active, *staged, *backup;
        int existed, backed, published;
    } files[] = {
        {"/phoenix/syst/poweroff/currentdoc.data", "/phoenix/syst/poweroff/currentdoc.data.ndl.tmp",
         "/phoenix/syst/poweroff/currentdoc.data.ndl.bak", 0, 0, 0},
        {"/phoenix/syst/poweroff/currentdoc.tns", "/phoenix/syst/poweroff/currentdoc.tns.ndl.tmp",
         "/phoenix/syst/poweroff/currentdoc.tns.ndl.bak", 0, 0, 0}
    };
    for (unsigned i = 0; i < 2; ++i) {
        if (persistence_exists(files[i].staged) != 0 || persistence_exists(files[i].backup) != 0 ||
            (files[i].existed = persistence_exists(files[i].active)) < 0) {
            puts("Persistence setup refused. Check existing .ndl files and storage access.");
            return 1;
        }
    }

    int c = copy_file("/documents/ndl/persistent.tns", files[1].staged);
    if (c == 2) c = copy_file("/documents/persistent.tns", files[1].staged);
    if (c == 2) c = copy_file("/documents/ndl/currentdoc.tns", files[1].staged);
    if (c == 2) c = copy_file("/documents/currentdoc.tns", files[1].staged);
    if (c != 0) goto failed;
    struct stat staged_info;
    if (stat(files[1].staged, &staged_info) || staged_info.st_size == 0) goto failed;

    uint32_t currentdoc_data[0x21C / sizeof(uint32_t)];
    memset(currentdoc_data, 0, sizeof(currentdoc_data));
    currentdoc_data[0] = 1;
    currentdoc_data[2] = 1;
    currentdoc_data[4] = 1;

    FILE *data = fopen(files[0].staged, "wb");
    if (!data) goto failed;
    int incomplete = fwrite(currentdoc_data, 1, sizeof(currentdoc_data), data) != sizeof(currentdoc_data);
    if (fclose(data)) incomplete = 1;
    if (incomplete) goto failed;

    // Move activation metadata first so a partial publication cannot activate the replacement.
    for (unsigned i = 0; i < 2; ++i) {
        if (files[i].existed) {
            if (rename(files[i].active, files[i].backup)) goto failed;
            files[i].backed = 1;
        }
    }
    for (int i = 1; i >= 0; --i) {
        if (rename(files[i].staged, files[i].active)) goto failed;
        files[i].published = 1;
    }
    int cleanup_failed = 0;
    for (unsigned i = 0; i < 2; ++i)
        if (files[i].backed && persistence_remove(files[i].backup)) cleanup_failed = 1;

    // Install hooks
    unsigned create_state_addr = create_state_addrs[ut_os_version_index];
    if (create_state_addr) {
        HOOK_INSTALL(create_state_addr, create_state_hook);
    }

    unsigned clear_state_addr = clear_state_addrs[ut_os_version_index];
    if (clear_state_addr) {
        HOOK_INSTALL(clear_state_addr, clear_state_hook);
    }

    unsigned save_addr = save_dialog_hook_addrs[ut_os_version_index];
    if (save_addr) {
        HOOK_INSTALL(save_addr, save_dialog_hook);
    }

    return cleanup_failed ? 3 : 0;

failed:
    puts("Persistence setup failed. Restoring the previous boot files.");
    for (unsigned i = 0; i < 2; ++i) {
        if (files[i].published && rename(files[i].active, files[i].staged)) goto recovery;
    }
    for (int i = 1; i >= 0; --i) {
        if (files[i].backed && rename(files[i].backup, files[i].active)) goto recovery;
    }
    for (unsigned i = 0; i < 2; ++i) persistence_remove(files[i].staged);
    return 1;

recovery:
    puts("Persistence rollback failed. Retain all .ndl files for recovery.");
    return 1;
}

int persistency_uninstall(void) {
    int exists = persistence_exists("/phoenix/syst/poweroff/currentdoc.data");
    if (exists <= 0) return exists != 0;
    // unlink doesn't work here
    // TODO: figure out why unlink doesnt work here, doesnt crash but doesnt delete the file
    // this will truncate the file to 0 anyways
    FILE *f = fopen("/phoenix/syst/poweroff/currentdoc.data", "wb");
    if (!f) return errno != ENOENT;
    return fclose(f) != 0;
}
