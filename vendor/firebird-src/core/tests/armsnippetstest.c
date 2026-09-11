#include "../armsnippets_loader.c"

struct arm_state arm;
static unsigned callback_count;
static unsigned failures;
static unsigned checks;
static uint8_t *memory;
static unsigned writes;
static unsigned fail_write;
static uint32_t invalid_address;

void *virt_mem_ptr(uint32_t address, uint32_t size) {
    (void)size;
    return address == invalid_address ? NULL : memory + (address & 4095);
}

uint32_t phys_mem_addr(void *pointer) {
    return (uint32_t)((uint8_t *)pointer - memory);
}

void *phys_mem_ptr(uint32_t address, uint32_t size) {
    (void)size;
    return memory + address;
}

bool virt_mem_write(uint32_t address, const void *source, uint32_t size) {
    (void)address;
    (void)source;
    (void)size;
    return ++writes != fail_write;
}

void flush_translations(void) {}
void emuprintf(const char *format, ...) { (void)format; }

static void check(bool passed, const char *name) {
    ++checks;
    if (!passed) {
        ++failures;
        fprintf(stderr, "FAIL %s\n", name);
    }
}

static void returned(struct arm_state *after) {
    ++callback_count;
    check(after->reg[0] == 73, "callback receives computed return value");
}

static void initial_state(void) {
    memset(&arm, 0, sizeof(arm));
    arm.reg[13] = 0x1800e4e0;
    arm.reg[15] = 0x101b6e6c;
    arm.cpsr_low28 = MODE_SVC;
    callback_count = 0;
}

static void snippet_case(unsigned size) {
    initial_state();
    const struct arm_state original = arm;
    const char path[] = "abcde";
    struct armloader_load_params params[2];
    params[0].t = ARMLOADER_PARAM_PTR;
    params[0].p.ptr = (void *)path;
    params[0].p.size = size;
    params[1].t = ARMLOADER_PARAM_VAL;
    params[1].v = 9;
    check(armloader_load_snippet(SNIPPET_ndls_exec, params, 2, returned), "load snippet");
    const uint32_t return_sp = arm.reg[13] - 4;
    arm.reg[13] = 0x120dea80;
    arm.reg[15] = original.reg[15];
    arm.reg[0] = 123;
    const struct arm_state other_task = arm;
    armloader_cb();
    check(memcmp(&arm, &other_task, sizeof(arm)) == 0, "same PC on another stack leaves CPU unchanged");
    check(callback_count == 0, "same PC on another stack does not consume callback");
    arm.reg[13] = return_sp;
    arm.reg[15] = original.reg[15];
    arm.reg[0] = 73;
    armloader_cb();
    check(memcmp(&arm, &original, sizeof(arm)) == 0, "real snippet return restores original CPU");
    check(callback_count == 1, "real snippet return invokes callback once");
}

static void blob_case(void) {
    initial_state();
    const struct arm_state original = arm;
    const uint32_t instructions[] = {0xe3a00049, 0xe12fff1e};
    check(armloader_defer_blob((const uint8_t *)instructions, sizeof(instructions)), "load blob");
    const uint32_t return_sp = arm.reg[13];
    arm.reg[15] = original.reg[15];
    arm.reg[13] = 0x120dea80;
    const struct arm_state other_task = arm;
    armloader_cb();
    check(memcmp(&arm, &other_task, sizeof(arm)) == 0, "foreign blob return leaves CPU unchanged");
    arm.reg[13] = return_sp;
    arm.reg[0] = 73;
    armloader_cb();
    check(memcmp(&arm, &original, sizeof(arm)) == 0, "real blob return restores CPU");
}

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
    if (!memory)
        return 1;
    snippet_case(0);
    snippet_case(1);
    snippet_case(4);
    snippet_case(5);
    blob_case();
    overlap();
    null_and_modes();
    failed_setup();
    queued_owner();
    invalid_and_reset();
    free(memory);
    fprintf(stderr, "%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
