#define main existing_callback_tests
#include "callback_test.c"
#undef main

static unsigned completed;
static void done(struct arm_state *after) { (void)after; ++completed; }

static void finish(uint32_t pc, uint32_t sp) {
    arm.reg[15] = pc;
    arm.reg[13] = sp;
    arm.cpsr_low28 = MODE_SVC;
    check(armloader_cb(), "owned return accepted");
}

static void overlap(void) {
    initial_state();
    const uint32_t first_pc = arm.reg[15];
    check(armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, done), "load owner A");
    const uint32_t first_sp = arm.reg[13] - 4;
    arm.reg[15] += 0x100;
    const struct arm_state running = arm;
    const unsigned prior_writes = writes;
    check(!armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, done), "reject overlapping owner B");
    check(memcmp(&arm, &running, sizeof(arm)) == 0, "overlap leaves CPU unchanged");
    check(writes == prior_writes, "overlap does not write guest memory");
    armloader_defer_exec("second.tns");
    check(!armloader_deferred_pending(), "running exec cannot gain a queued replacement");
    const uint32_t blob = 0xe12fff1e;
    check(!armloader_defer_blob((const uint8_t *)&blob, sizeof(blob)), "running owner rejects blob overlap");
    armloader_cancel_deferred();
    arm.reg[15] = first_pc + 4;
    arm.reg[13] = first_sp;
    const struct arm_state wrong_pc = arm;
    check(!armloader_cb(), "matching stack at unrelated PC is rejected");
    check(memcmp(&arm, &wrong_pc, sizeof(arm)) == 0, "wrong PC preserves CPU");
    finish(first_pc, first_sp);
}

static void null_and_modes(void) {
    initial_state();
    const uint32_t pc = arm.reg[15];
    check(armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, NULL), "load null callback owner");
    const uint32_t sp = arm.reg[13] - 4;
    arm.reg[15] = pc;
    arm.reg[13] = sp;
    arm.cpsr_low28 = MODE_IRQ;
    check(!armloader_cb(), "same SP in another processor mode is rejected");
    arm.cpsr_low28 = MODE_SVC | 0x20;
    check(!armloader_cb(), "same SP in Thumb state is rejected");
    finish(pc, sp);
    check((RAM_FLAGS(virt_mem_ptr(pc, 4)) & RF_ARMLOADER_CB) == 0, "null callback retires owned flag");
    arm.reg[13] = sp;
    check(!armloader_cb(), "completed owner cannot return twice");
}

static void failed_setup(void) {
    initial_state();
    const struct arm_state original = arm;
    const char path[] = "abc";
    struct armloader_load_params param;
    param.t = ARMLOADER_PARAM_PTR;
    param.p.ptr = (void *)path;
    param.p.size = sizeof(path);
    fail_write = writes + 2;
    check(!armloader_load_snippet(SNIPPET_ndls_exec, &param, 1, done), "failed parameter write rejects setup");
    check(memcmp(&arm, &original, sizeof(arm)) == 0, "failed setup restores CPU");
    check(!armloader_cb(), "failed setup owns no return");
    fail_write = 0;
    check(armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, done), "new owner accepted after failed setup");
    finish(original.reg[15], arm.reg[13] - 4);
}

static void queued_owner(void) {
    initial_state();
    arm.reg[13] = 0xa4001000;
    armloader_defer_exec("queued.tns");
    check(armloader_deferred_pending(), "queue owns a deferred request");
    initial_state();
    const struct arm_state original = arm;
    const unsigned prior_writes = writes;
    const bool accepted = armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, done);
    check(!accepted, "queued request rejects a direct overlapping owner");
    check(writes == prior_writes, "queued overlap cannot write guest memory");
    armloader_cancel_deferred();
    if(accepted)
        finish(original.reg[15], arm.reg[13] - 4);
}

static void invalid_and_reset(void) {
    initial_state();
    const struct arm_state original = arm;
    invalid_address = original.reg[15];
    const unsigned prior_writes = writes;
    check(!armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, done), "invalid return address rejects setup");
    check(memcmp(&arm, &original, sizeof(arm)) == 0 && writes == prior_writes, "invalid return address leaves CPU and guest memory unchanged");
    invalid_address = 0;
    check(armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, NULL), "owner starts before reset");
    armloader_reset();
    check((RAM_FLAGS(virt_mem_ptr(original.reg[15], 4)) & RF_ARMLOADER_CB) == 0, "reset retires owned flag");
    initial_state();
    check(armloader_load_snippet(SNIPPET_ndls_exec, NULL, 0, NULL), "reset releases ownership for a new request");
    finish(original.reg[15], arm.reg[13] - 4);
}

int main(void) {
    memory = calloc(1, MEM_MAXSIZE + 4096);
    assert(memory);
    overlap();
    null_and_modes();
    failed_setup();
    queued_owner();
    invalid_and_reset();
    free(memory);
    fprintf(stderr, "%u ownership checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
