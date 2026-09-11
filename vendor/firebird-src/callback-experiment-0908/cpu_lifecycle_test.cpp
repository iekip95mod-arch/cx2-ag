#include "armsnippets.h"
#include "debug.h"
#include "emu.h"
#include "mem.h"
#include "os/os.h"

#include <cassert>
#include <iostream>

static unsigned failures;
static unsigned checks;
static unsigned first_calls;
static unsigned second_calls;
static uint32_t second_entry;
static uint32_t second_stack;

static void check(bool passed, const char *name) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cout << "FAIL " << name << '\n';
    }
}

static void second_done(struct arm_state *) { ++second_calls; }

static void reenter(struct arm_state *) {
    ++first_calls;
    check(armloader_load_snippet(SNIPPET_ndls_exec, nullptr, 0, second_done), "callback can start its successor");
    second_entry = arm.reg[15];
    second_stack = arm.reg[13];
}

int main() {
    product = 0x1d0;
    do_translate = false;
    addr_cache_init();
    assert(memory_initialize(64 * 1024 * 1024));
    cpu_reset();
    arm.reg[13] = 0x10010000;
    arm.reg[15] = 0x10001000;
    arm.cpsr_low28 = MODE_SVC;
    arm.control &= ~1U;
    const uint32_t return_pc = arm.reg[15];
    const uint32_t nop = 0xe1a00000;
    assert(virt_mem_write(return_pc, &nop, sizeof(nop)));
    check(armloader_load_snippet(SNIPPET_ndls_exec, nullptr, 0, reenter), "load first owner");
    arm.reg[13] -= 4;
    arm.reg[15] = return_pc;
    cycle_count_delta = -1;
    cpu_events = 0;
    exiting = false;
    cpu_arm_loop();
    check(first_calls == 1, "CPU dispatch invokes completed owner once");
    check((RAM_FLAGS(virt_mem_ptr(return_pc, 4)) & RF_ARMLOADER_CB) != 0, "CPU preserves reentrant same-site successor flag");
    check(arm.reg[15] == second_entry + 4, "CPU refetches successor instruction after callback");
    check(arm.reg[13] == second_stack - 4, "CPU executes successor LR push instead of stale NOP");
    arm.reg[15] = second_entry + 24;
    check(*static_cast<uint32_t *>(virt_mem_ptr(arm.reg[15], 4)) == 0xe89d8000, "exercise the existing snippet return instruction");
    cycle_count_delta = -1;
    cpu_arm_loop();
    check(arm.reg[15] == return_pc && arm.reg[13] == second_stack - 4, "ARM return restores PC without changing the saved-LR stack");
    cycle_count_delta = -1;
    cpu_arm_loop();
    check(second_calls == 1, "CPU dispatch invokes successor");
    check((RAM_FLAGS(virt_mem_ptr(return_pc, 4)) & RF_ARMLOADER_CB) == 0, "CPU retires completed successor flag");
    cpu_reset();
    memory_deinitialize();
    addr_cache_deinit();
    std::cout << checks << " CPU checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
