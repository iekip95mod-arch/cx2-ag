/* Loads and run ARM code snippets on the target */

#include "emu.h"
#include "cpu.h"
#include "armsnippets.h"
#include "mem.h"
#include "translate.h"
#include "armsnippets.h"
#include "debug.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "armcode_bin.h"

struct arm_state armloader_orig_arm_state;

static void armloader_restore_state(void) {
    memcpy(&arm, &armloader_orig_arm_state, sizeof(arm));
}

static void (*armloader_cb_ptr)(struct arm_state*);
static uint32_t armloader_return_sp;
static uint32_t armloader_return_phys;
static bool armloader_running;
static unsigned rejected_returns;

static void armloader_retire(void) {
    if(armloader_running) {
        void *instruction = phys_mem_ptr(armloader_return_phys, 4);
        if(instruction)
            RAM_FLAGS(instruction) &= ~RF_ARMLOADER_CB;
    }
    armloader_running = false;
    armloader_cb_ptr = NULL;
}

bool armloader_cb(void) {
    if(!armloader_running || arm.reg[15] != armloader_orig_arm_state.reg[15] ||
       arm.reg[13] != armloader_return_sp ||
       (arm.cpsr_low28 & 0x3F) != (armloader_orig_arm_state.cpsr_low28 & 0x3F)) {
        if(rejected_returns++ < 5)
            emuprintf("snippet: ignore shared pc %08x sp %08x, expected %08x\n",
                      arm.reg[15], arm.reg[13], armloader_return_sp);
        return false;
    }
    emuprintf("snippet: matched return pc %08x sp %08x after %u foreign hits\n",
              arm.reg[15], arm.reg[13], rejected_returns);
    struct arm_state after_snippet_exec_arm_state;
    memcpy(&after_snippet_exec_arm_state, &arm, sizeof(arm));
    void (*callback)(struct arm_state*) = armloader_cb_ptr;
    armloader_retire();
    armloader_restore_state();
    if(callback)
        callback(&after_snippet_exec_arm_state);
    return true;
}

/* Push code onto the guest stack and enter it at entry_offset, with lr set to the interrupted pc.
 * Fails with the state untouched if the caller is in Thumb state or the stack has no room. */
static bool armloader_push_code(const uint8_t *code, uint32_t code_size, uint32_t entry_offset) {
    if(armloader_running)
        return false;
    // The code is ARM and only cpu_arm_loop honours RF_ARMLOADER_CB, so a Thumb caller can
    // neither be entered correctly nor returned from. Entering one decodes the code as Thumb and
    // runs off into the stack.
    if(arm.cpsr_low28 & 0x20) {
        emuprintf("snippet not loaded: the caller is in Thumb state at pc %08x\n", arm.reg[15]);
        return false;
    }

    void *return_instruction = virt_mem_ptr(arm.reg[15], 4);
    if(!return_instruction || !virt_mem_ptr(arm.reg[13] /* sp */, 4)) {
        emuprintf("snippet stack or return address is invalid\n");
        return false;
    }
    memcpy(&armloader_orig_arm_state, &arm, sizeof(arm));
    armloader_return_phys = phys_mem_addr(return_instruction);
    arm.reg[13] -= code_size;
    if(!virt_mem_write(arm.reg[13], code, code_size)) {
        emuprintf("not enough stack space to run snippet\n");
        armloader_restore_state();
        return false;
    }

    arm.reg[14] = arm.reg[15]; // return address
    arm.reg[15] = arm.reg[13] + entry_offset;
    return true;
}

/* Have callback run, and the saved state come back, when the guest returns to the interrupted pc. */
static void armloader_arm_return(void (*callback)(struct arm_state*), uint32_t return_sp) {
    armloader_cb_ptr = callback;
    armloader_return_sp = return_sp;
    armloader_running = true;
    rejected_returns = 0;
    uint32_t *flags = &RAM_FLAGS(virt_mem_ptr(armloader_orig_arm_state.reg[15], 4));
    if (*flags & RF_CODE_TRANSLATED) flush_translations();
    *flags |= RF_ARMLOADER_CB;
}

/* Load the snippet and jump to it.
 * snippets are defined in armsnippets.S.
 * params can be null if not required.
 * params may contain pointers to data which should be copied to device space.
 * Each param will be copied to the ARM stack, and its address written in rX, starting from r0.
 * params_num must be less or equal than 12.
 * callback() will be called once the snippet has finished its execution. Can be NULL.
 * returns 0 if success.
 */
bool armloader_load_snippet(enum SNIPPETS snippet, struct armloader_load_params params[],  uint32_t params_num, void (*callback)(struct arm_state*)) {
    if(armloader_deferred_pending())
        return false;
    uint32_t i;
    uint32_t code_size = snippets_bin_len;
    uint32_t entry_offset;

    if(code_size % 4)
        code_size += 4 - (code_size % 4); // word-aligned

    memcpy(&entry_offset, snippets_bin, sizeof(entry_offset)); // load_snippet
    if(!armloader_push_code(snippets_bin, code_size, entry_offset))
        return false;
    arm.reg[12] = snippet;

    for(i = 0; i < params_num; i++) {
        if(params[i].t == ARMLOADER_PARAM_VAL)
            arm.reg[i] = params[i].v;
        else {
            uint32_t size = params[i].p.size;
            if (size % 4)
                size += 4 - size % 4; // word-aligned
            arm.reg[13] -= size;
            arm.reg[i] = arm.reg[13];
            if (!virt_mem_write(arm.reg[13], params[i].p.ptr, params[i].p.size)) {
                emuprintf("not enough stack space for snippet parameters\n");
                armloader_restore_state();
                return false;
            }
        }
    }
    armloader_arm_return(callback, arm.reg[13] - 4);

    // for debugging
    /*flags = &RAM_FLAGS(virt_mem_ptr(arm.reg[15], 4));
    if (*flags & RF_CODE_TRANSLATED) flush_translations();
    *flags |= RF_EXEC_BREAKPOINT;*/

    return true;
}

/* Ndl's exec syscall runs on the caller's own stack. ints_swi_handler restores the caller's spsr
 * into cpsr before jumping to the syscall (ndl/src/resources/ints.c), so ld_exec_with_args gets
 * whatever sp the run point had. That makes the point where the snippet runs the whole problem.
 *
 * Two contexts fail. The debugger nearly always breaks into the Nucleus idle loop, where sp is in
 * SRAM and the OS has no current task at all, so the first file open dereferences a null task
 * pointer and the calculator resets. A service task is no better: CN_READ, for instance, has a 4K
 * stack, and ld_exec_with_args puts three FILENAME_MAX buffers on it before doing anything.
 *
 * The context that works is the one Ndl prepared for itself. expand_stack() runs once, at boot,
 * from the startup hook, and maps a 128K coarse page table at 0x17F00000 for the task it is running
 * on (ndl/src/resources/ploaderhook.c:213). That task's stack sits in the same 1MB section, so an
 * sp in this window means the task Ndl expects to load programs from. */
#define NDLS_STACK_LOW  0x17F00000u
#define NDLS_STACK_HIGH 0x18010000u

static char deferred_path[512];
static bool deferred_armed;

bool armloader_ndl_context(void) {
    if((arm.cpsr_low28 & 0x1F) != MODE_SVC)
        return false;
    // The idle loop and the exception handlers run with interrupts masked; a task does not.
    if(arm.cpsr_low28 & 0x80)
        return false;
    // A Thumb caller cannot run the snippet, so wait for an ARM moment on the same task.
    if(arm.cpsr_low28 & 0x20)
        return false;
    return arm.reg[13] >= NDLS_STACK_LOW && arm.reg[13] < NDLS_STACK_HIGH;
}

static void armloader_exec_now(const char *path) {
    struct armloader_load_params params[3];
    params[0].t = ARMLOADER_PARAM_PTR;
    params[0].p.ptr = (void *)path;
    params[0].p.size = strlen(path) + 1;
    params[1].t = ARMLOADER_PARAM_VAL;
    params[1].v = 0;
    params[2] = params[1];
    emuprintf("exec: loading at pc %08x sp %08x cpsr %08x, running %s\n", arm.reg[15], arm.reg[13],
              arm.cpsr_low28, path);
    armloader_load_snippet(SNIPPET_ndls_exec, params, 3, NULL);
}

/* An ARM code blob runs the same way, entered at its first byte and returning through lr. Ndl's own
 * stage0 is one: position independent, it loads ndl_resources.tns through the OS's file calls
 * and returns, so Ndl can be installed here without a document ever being opened. One thing is
 * queued at a time, a blob or an exec, so a poll can never push twice onto the same task. */
static uint8_t *deferred_blob;
static uint32_t deferred_blob_size;

static void armloader_blob_done(struct arm_state *after) {
    emuprintf("blob: returned with r0 %08x, resuming at pc %08x\n", after->reg[0], arm.reg[15]);
}

static void armloader_blob_now(void) {
    emuprintf("blob: loading %u bytes at pc %08x sp %08x cpsr %08x\n", deferred_blob_size,
              arm.reg[15], arm.reg[13], arm.cpsr_low28);
    if(armloader_push_code(deferred_blob, deferred_blob_size, 0))
        armloader_arm_return(armloader_blob_done, arm.reg[13]);
    free(deferred_blob);
    deferred_blob = NULL;
}

void armloader_defer_exec(const char *path) {
    if(armloader_running)
        return;
    free(deferred_blob);
    deferred_blob = NULL;
    deferred_armed = false;
    if(armloader_ndl_context()) {
        armloader_exec_now(path);
        return;
    }
    snprintf(deferred_path, sizeof(deferred_path), "%s", path);
    deferred_armed = true;
}

bool armloader_defer_blob(const uint8_t *blob, uint32_t size) {
    if(armloader_running)
        return false;
    uint32_t padded = (size + 3) & ~3u; // virt_mem_write wants whole words
    uint8_t *copy = calloc(padded, 1);
    if(!copy)
        return false;
    memcpy(copy, blob, size);
    deferred_armed = false;
    free(deferred_blob);
    deferred_blob = copy;
    deferred_blob_size = padded;
    if(armloader_ndl_context())
        armloader_blob_now();
    return true;
}

void armloader_cancel_deferred(void) {
    deferred_armed = false;
    free(deferred_blob);
    deferred_blob = NULL;
}

void armloader_reset(void) {
    armloader_retire();
    armloader_cancel_deferred();
}

bool armloader_deferred_pending(void) {
    return deferred_armed || deferred_blob;
}

void armloader_poll_deferred(void) {
    if(armloader_running || !armloader_ndl_context())
        return;
    if(deferred_blob) {
        armloader_blob_now();
        return;
    }
    if(!deferred_armed)
        return;
    deferred_armed = false;
    armloader_exec_now(deferred_path);
}
