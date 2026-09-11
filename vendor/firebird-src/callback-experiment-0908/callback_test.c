#include "armsnippets_loader.c"

#include <assert.h>

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

int main(void) {
    memory = calloc(1, MEM_MAXSIZE + 4096);
    assert(memory);
    snippet_case(0);
    snippet_case(1);
    snippet_case(4);
    snippet_case(5);
    blob_case();
    free(memory);
    fprintf(stderr, "%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
