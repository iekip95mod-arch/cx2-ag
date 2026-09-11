#include "debug.h"
#include "emu.h"
#include "mem.h"
#include "os/os.h"
#include <cstdlib>
#include <cstring>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

constexpr uint32_t base = 0x10040000;
constexpr uint32_t table = 0x10003000;
constexpr uint32_t service = 0x10002000;
constexpr uint32_t caller_a = 0x10001000;
constexpr uint32_t caller_b = 0x10001020;
static uint32_t handler;
static uint32_t table_pointer;
static unsigned failures;
static unsigned checks;
static arm_state initial;
static std::vector<uint32_t> returns;
static uint32_t stub;

struct Call {
    arm_state dispatched;
    std::array<uint32_t, 13> registers;
    uint32_t caller;
    uint32_t stack;
    uint32_t flags;
    bool thumb;
    bool wrapper;
};

static void check(bool pass, const char *name) {
    ++checks;
    if (!pass) {
        ++failures;
        std::cout << "FAIL " << name << '\n';
    }
}

static void word(uint32_t address, uint32_t value) {
    if (!virt_mem_write(address, &value, sizeof(value))) std::exit(2);
}

static void tick() {
    uint32_t instruction = 0;
    const void *encoded = virt_mem_ptr(arm.reg[15], sizeof(instruction));
    if (!encoded) std::exit(2);
    std::memcpy(&instruction, encoded, sizeof(instruction));
    if (!(arm.cpsr_low28 & 0x20) && (instruction & 0xff000000) == 0xef000000) {
        const uint32_t saved = get_cpsr();
        arm.reg[14] = arm.reg[15] + 4;
        set_cpsr_full((saved & ~0x3fU) | 0x93);
        arm.spsr_svc = saved;
        arm.reg[15] = handler;
        return;
    }
    cycle_count_delta = -1;
    cpu_events = 0;
    if (arm.cpsr_low28 & 0x20) cpu_thumb_loop();
    else cpu_arm_loop();
}

static bool returned() {
    for (uint32_t address : returns) if (arm.reg[15] == address) return true;
    return false;
}

static Call begin(unsigned identity, uint32_t stack, unsigned flags, bool thumb, bool wrapper,
                  const arm_state *parent = nullptr, unsigned syscall = 96) {
    Call call{};
    call.caller = 0x10006000 + identity * 16;
    call.stack = stack;
    call.flags = flags << 27;
    call.thumb = thumb;
    call.wrapper = wrapper;
    returns.push_back(call.caller + (thumb ? 2 : 4));
    word(call.caller, thumb ? 0xdf60 : 0xef000000 | syscall);
    arm = parent ? *parent : initial;
    for (unsigned index = 0; index < call.registers.size(); ++index) {
        call.registers[index] = parent ? parent->reg[index] : 0x12340000 + identity * 32 + index;
        arm.reg[index] = call.registers[index];
    }
    for (unsigned index = 0; index < 12; ++index) word(stack + index * 4, 0xabc00000 + index);
    arm.reg[13] = stack;
    if (wrapper) {
        arm.reg[14] = call.caller + (thumb ? 3 : 4);
        arm.reg[15] = stub;
        set_cpsr_full(call.flags | 0x13);
    } else {
        arm.reg[14] = call.caller + (thumb ? 2 : 4);
        arm.reg[15] = handler;
        set_cpsr_full(call.flags | 0x93);
        arm.spsr_svc = call.flags | (thumb ? 0x33 : 0x13);
    }
    for (unsigned count = 0; count < 2000 && arm.reg[15] != service && !returned(); ++count) tick();
    check(arm.reg[15] == service && arm.reg[13] == stack, "dispatch preserves stack-argument position");
    for (unsigned index = 0; index < 4; ++index)
        check(arm.reg[index] == call.registers[index], "dispatch preserves register arguments");
    for (unsigned index = 0; index < 12; ++index) {
        uint32_t argument = 0;
        const void *stored = virt_mem_ptr(stack + index * 4, sizeof(argument));
        if (!stored) std::exit(2);
        std::memcpy(&argument, stored, sizeof(argument));
        check(argument == 0xabc00000 + index, "dispatch preserves stack-argument values");
    }
    call.dispatched = arm;
    return call;
}

static bool complete(const Call &call, bool irq_masked) {
    const unsigned before = failures;
    arm = call.dispatched;
    for (unsigned index = 0; index < 4; ++index) arm.reg[index] = 0xa1000000 + index;
    arm.reg[12] = 0xdeadbeef;
    const bool service_returns_thumb = arm.reg[14] & 1;
    arm.reg[15] = arm.reg[14] & ~1U;
    set_cpsr_full(0x88000053 | (irq_masked ? 0x80 : 0) | (service_returns_thumb ? 0x20 : 0));
    for (unsigned count = 0; count < 2000 && !returned(); ++count) tick();
    check(arm.reg[15] == call.caller + (call.thumb ? 2 : 4), "return reaches its own caller");
    check(arm.reg[13] == call.stack, "return preserves caller stack");
    for (unsigned index = 0; index < 4; ++index)
        check(arm.reg[index] == 0xa1000000 + index, "return preserves all result registers");
    for (unsigned index = call.wrapper ? 4 : 5; index < 12; ++index)
        check(arm.reg[index] == call.registers[index], "return preserves callee-saved registers");
    if (!call.wrapper) check((get_cpsr() & 0xf8000000) == call.flags, "return restores NZCVQ flags");
    check((get_cpsr() & 0xff) == (0x13U | (call.thumb ? 0x20U : 0U) | (irq_masked ? 0x80U : 0U) |
                                (call.wrapper ? 0x40U : 0U)),
          "return preserves service masks with the raw SWI FIQ override and caller instruction state");
    return failures == before;
}

static arm_state enter(uint32_t caller, uint32_t stack) {
    arm = initial;
    arm.reg[13] = stack;
    arm.reg[14] = caller + 4;
    arm.reg[15] = handler;
    arm.spsr_svc = 0x60000013;
    set_cpsr_full(0x60000093);
    for (unsigned count = 0; count < 1000 && arm.reg[15] != service; ++count) tick();
    check(arm.reg[15] == service && arm.reg[13] == stack, "dispatch preserves the caller's argument stack");
    return arm;
}

static void finish(const arm_state &task, uint32_t expected) {
    arm = task;
    arm.reg[0] = 0x1000a000;
    arm.reg[15] = arm.reg[14];
    for (unsigned count = 0; count < 1000 && arm.reg[15] != caller_a + 4 &&
         arm.reg[15] != caller_b + 4; ++count) tick();
    std::cout << "expected return " << std::hex << expected << ", actual " << arm.reg[15]
              << ", r0 " << arm.reg[0] << std::dec << '\n';
    check(arm.reg[15] == expected, "each task returns to its own syscall caller");
    check(arm.reg[0] == 0x1000a000, "the syscall value survives return");
}

static void reset_code(const std::vector<char> &code) {
    std::vector<char> cleared(1024 * 1024);
    if (!virt_mem_write(base, cleared.data(), static_cast<uint32_t>(cleared.size())) ||
        !virt_mem_write(base, code.data(), static_cast<uint32_t>(code.size()))) std::exit(2);
    word(table_pointer, table);
}

int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 7) return 2;
    handler = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 16));
    table_pointer = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16));
    const std::array<uint32_t, 4> stubs = {0,
        static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 16)),
        static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 16)),
        static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 16))};
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> code{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (code.empty() || code.size() > 1024 * 1024) return 2;
    product = 0x1d0;
    do_translate = false;
    addr_cache_init();
    if (!memory_initialize(64 * 1024 * 1024)) return 2;
    cpu_reset();
    arm.control &= ~1U;
    arm.cpsr_low28 = MODE_SVC;
    initial = arm;
    word(table + 96 * 4, service);
    word(table + 273 * 4, service);
    word(table + 108 * 4, service);
    word(table + 100 * 4, service);
    word(table + 246 * 4, service);
    word(caller_a, 0xef000060);
    word(caller_b, 0xef000111);
    reset_code(code);
    finish(enter(caller_a, 0x10030000), caller_a + 4);
    finish(enter(caller_b, 0x10031000), caller_b + 4);
    reset_code(code);
    const auto nested_a = enter(caller_a, 0x10030000);
    const auto nested_b = enter(caller_b, 0x10031000);
    finish(nested_b, caller_b + 4);
    finish(nested_a, caller_a + 4);
    reset_code(code);
    const auto overlapping_a = enter(caller_a, 0x10030000);
    const auto overlapping_b = enter(caller_b, 0x10031000);
    finish(overlapping_a, caller_a + 4);
    finish(overlapping_b, caller_b + 4);
    for (uint32_t entry : stubs) {
        const bool wrapper = entry != 0;
        stub = entry;
        for (unsigned flags = 0; flags < 32; ++flags) {
            for (bool thumb : {false, true}) {
                reset_code(code);
                returns.clear();
                complete(begin(0, 0x10030000, flags, thumb, wrapper), flags & 1);
            }
        }
        for (unsigned first = 0; first < 3; ++first) {
            for (unsigned second = 0; second < 3; ++second) {
                if (first == second) continue;
                reset_code(code);
                returns.clear();
                std::array<Call, 3> calls;
                for (unsigned index = 0; index < calls.size(); ++index)
                    calls[index] = begin(index, 0x10030000 + index * 0x1000, index * 7, index & 1, wrapper);
                if (!complete(calls[first], true)) continue;
                if (!complete(calls[second], false)) continue;
                complete(calls[3 - first - second], true);
            }
        }
        reset_code(code);
        returns.clear();
        std::vector<Call> nested;
        for (unsigned depth = 0; depth < 16; ++depth)
            nested.push_back(begin(depth, 0x10030000 - depth * 0x100, depth, depth & 1, wrapper,
                                   nested.empty() ? nullptr : &nested.back().dispatched));
        for (size_t depth = nested.size(); depth-- > 0;)
            if (!complete(nested[depth], depth & 1)) break;
    }
    memory_deinitialize();
    addr_cache_deinit();
    std::cout << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
