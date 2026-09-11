#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#endif

#include <condition_variable>
#include <vector>

#include "armsnippets.h"
#include "debug.h"
#include "interrupt.h"
#include "emu.h"
#include "cpu.h"
#include "mem.h"
#include "disasm.h"
#include "mmu.h"
#include "translate.h"
#include "usblink_queue.h"
#include "gdbstub.h"
#include "lcd.h"
#include "keypad.h"
#include "keymap.h"
#include "os/os.h"

std::string ln_target_folder;

// Names for the keymap ids, so the debugger can say "esc" rather than a row and a column. The ids
// come from keymap.h, which is what the Qt keypad bridge uses, so the two cannot drift apart.
struct key_name { const char *name; int id; };
static const key_name key_names[] = {
    {"ret", keymap::ret}, {"enter", keymap::enter}, {"neg", keymap::neg}, {"space", keymap::space},
    {"punct", keymap::punct}, {"on", keymap::on}, {"pi", keymap::pi}, {"trig", keymap::trig},
    {"pow10", keymap::pow10}, {"ee", keymap::ee}, {"squ", keymap::squ}, {"div", keymap::div},
    {"exp", keymap::exp}, {"equ", keymap::equ}, {"mult", keymap::mult}, {"pow", keymap::pow},
    {"var", keymap::var}, {"minus", keymap::minus}, {"pright", keymap::pright}, {"dot", keymap::dot},
    {"pleft", keymap::pleft}, {"cat", keymap::cat}, {"metrix", keymap::metrix}, {"del", keymap::del},
    {"pad", keymap::pad}, {"flag", keymap::flag}, {"plus", keymap::plus}, {"doc", keymap::doc},
    {"menu", keymap::menu}, {"esc", keymap::esc}, {"tab", keymap::tab}, {"shift", keymap::shift},
    {"ctrl", keymap::ctrl}, {"comma", keymap::comma},
    {"0", keymap::n0}, {"1", keymap::n1}, {"2", keymap::n2}, {"3", keymap::n3}, {"4", keymap::n4},
    {"5", keymap::n5}, {"6", keymap::n6}, {"7", keymap::n7}, {"8", keymap::n8}, {"9", keymap::n9},
    {"a", keymap::aa}, {"b", keymap::ab}, {"c", keymap::ac}, {"d", keymap::ad}, {"e", keymap::ae},
    {"f", keymap::af}, {"g", keymap::ag}, {"h", keymap::ah}, {"i", keymap::ai}, {"j", keymap::aj},
    {"k", keymap::ak}, {"l", keymap::al}, {"m", keymap::am}, {"n", keymap::an}, {"o", keymap::ao},
    {"p", keymap::ap}, {"q", keymap::aq}, {"r", keymap::ar}, {"s", keymap::as}, {"t", keymap::at},
    {"u", keymap::au}, {"v", keymap::av}, {"w", keymap::aw}, {"x", keymap::ax}, {"y", keymap::ay},
    {"z", keymap::az},
};

// Used for debugger input
static std::mutex debug_input_m;
static std::condition_variable debug_input_cv;
static const char * debug_input_cur = nullptr;

// usblink reports a transfer only through its progress callback, and it uses -1 for failure. Passing
// nullptr, as this file used to, makes a queued transfer that never connects look exactly like one
// that succeeded.
static void ln_progress(int progress, void *)
{
    if (progress < 0)
        gui_debug_printf("Link transfer failed.\n");
    else if (progress == 100)
        gui_debug_printf("Link transfer complete.\n");
}

static void ln_svc_reply(const uint8_t *data, uint32_t size, bool is_error, void *)
{
    if (is_error) {
        gui_debug_printf("Link svc failed: the calculator refused the service or never answered.\n");
        return;
    }
    gui_debug_printf("Link svc reply (%u bytes):", size);
    for (uint32_t i = 0; i < size; i++)
        gui_debug_printf(" %02x", data[i]);
    gui_debug_printf("\nLink svc complete.\n");
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Optional surrounding quotes off a path argument. A lone quote used to index one before the string.
static char *strip_quotes(char *file)
{
    if (*file == '"')
        file++;
    size_t len = strlen(file);
    if (len && file[len - 1] == '"')
        file[len - 1] = '\0';
    return file;
}

// Pairs of hex digits, spaces optional between them. Returns false on anything else.
static bool parse_hex_bytes(const char *text, std::vector<uint8_t> &out)
{
    while (*text) {
        if (*text == ' ' || *text == '\t') {
            text++;
            continue;
        }
        int hi = hex_digit(text[0]);
        int lo = text[0] ? hex_digit(text[1]) : -1;
        if (hi < 0 || lo < 0)
            return false;
        out.push_back((uint8_t)(hi << 4 | lo));
        text += 2;
    }
    return true;
}

static void debug_input_callback(const char *input)
{
    std::unique_lock<std::mutex> lk(debug_input_m);

    debug_input_cur = input;

    debug_input_cv.notify_all();
}

void *virt_mem_ptr(uint32_t addr, uint32_t size) {
    // Note: this is not guaranteed to be correct when range crosses page boundary
    return (void *)(intptr_t)phys_mem_ptr(mmu_translate(addr, false, NULL, NULL), size);
}

// virt_mem_ptr translates the first page only, so anything longer than a few bytes has to walk the
// pages itself or it writes past the end of the first one. Checks the whole range before copying so
// a refusal leaves guest memory untouched.
bool virt_mem_write(uint32_t addr, const void *src, uint32_t size) {
    const uint8_t *from = (const uint8_t *)src;

    for(uint32_t done = 0; done < size; ) {
        uint32_t chunk = 0x1000 - ((addr + done) & 0xFFF);
        if(chunk > size - done)
            chunk = size - done;
        if(!virt_mem_ptr(addr + done, chunk))
            return false;
        done += chunk;
    }

    for(uint32_t done = 0; done < size; ) {
        uint32_t chunk = 0x1000 - ((addr + done) & 0xFFF);
        if(chunk > size - done)
            chunk = size - done;
        memcpy(virt_mem_ptr(addr + done, chunk), from + done, chunk);
        done += chunk;
    }

    return true;
}

void backtrace(uint32_t fp) {
    uint32_t *frame;
    gui_debug_printf("Frame     PrvFrame Self     Return   Start\n");
    do {
        gui_debug_printf("%08X:", fp);
        frame = (uint32_t*) virt_mem_ptr(fp - 12, 16);
        if (!frame) {
            gui_debug_printf(" invalid address\n");
            break;
        }
        //vgui_debug_printf(" %08X %08X %08X %08X\n", (void *)frame);
        if (frame[0] <= fp) /* don't get stuck in infinite loop :) */
            break;
        fp = frame[0];
    } while (frame[2] != 0);
}

static void dump(uint32_t addr) {
    uint32_t start = addr;
    uint32_t end = addr + 0x7F;

    uint32_t row, col;
    for (row = start & ~0xF; row <= end; row += 0x10) {
        uint8_t *ptr = (uint8_t*) virt_mem_ptr(row, 16);
        if (!ptr) {
            gui_debug_printf("Address %08X is not in RAM.\n", row);
            break;
        }
        gui_debug_printf("%08X  ", row);
        for (col = 0; col < 0x10; col++) {
            addr = row + col;
            if (addr < start || addr > end)
                gui_debug_printf("  ");
            else
                gui_debug_printf("%02X", ptr[col]);
            gui_debug_printf(col == 7 && addr >= start && addr < end ? "-" : " ");
        }
        gui_debug_printf("  ");
        for (col = 0; col < 0x10; col++) {
            addr = row + col;
            if (addr < start || addr > end)
                gui_debug_printf(" ");
            else if (ptr[col] < 0x20)
                gui_debug_printf(".");
            else
            {
                char str[] = {(char) ptr[col], 0};
                gui_debug_printf(str);
            }
        }
        gui_debug_printf("\n");
    }
}

static uint32_t parse_expr(char *str) {
    uint32_t sum = 0;
    int sign = 1;
    if (str == NULL)
        return 0;
    while (*str) {
        int reg;
        if (isxdigit(*str)) {
            sum += sign * strtoul(str, &str, 16);
            sign = 1;
        } else if (*str == '+') {
            str++;
        } else if (*str == '-') {
            sign = -1;
            str++;
        } else if (*str == 'v') {
            sum += sign * mmu_translate(strtoul(str + 1, &str, 16), false, NULL, NULL);
            sign = 1;
        } else if (*str == 'r') {
            reg = strtoul(str + 1, &str, 10);
            if(reg > 15)
            {
                gui_debug_printf("Reg number out of range!\n");
                return 0;
            }
            sum += sign * arm.reg[reg];
            sign = 1;
        } else {
            for (reg = 13; reg < 16; reg++) {
                if (!memcmp(str, reg_name[reg], 2)) {
                    str += 2;
                    sum += sign * arm.reg[reg];
                    sign = 1;
                    goto ok;
                }
            }
            gui_debug_printf("syntax error\n");
            return 0;
ok:;
        }
    }
    return sum;
}

uint32_t disasm_insn(uint32_t pc) {
    return (arm.cpsr_low28 & 0x20) ? disasm_thumb_insn(pc) : disasm_arm_insn(pc);
}

static void disasm(uint32_t (*dis_func)(uint32_t pc)) {
    char *arg = strtok(NULL, " \n\r");
    uint32_t addr = arg ? parse_expr(arg) : arm.reg[15];
    int i;
    for (i = 0; i < 16; i++) {
        uint32_t len = dis_func(addr);
        if (!len) {
            gui_debug_printf("Address %08X is not in RAM.\n", addr);
            break;
        }
        addr += len;
    }
}

uint32_t *debug_next;
static void set_debug_next(uint32_t *next) {
    if (debug_next != NULL)
        RAM_FLAGS(debug_next) &= ~RF_EXEC_DEBUG_NEXT;
    if (next != NULL) {
        if (RAM_FLAGS(next) & RF_CODE_TRANSLATED)
            flush_translations();
        RAM_FLAGS(next) |= RF_EXEC_DEBUG_NEXT;
    }
    debug_next = next;
}

bool gdb_connected = false;

// return 1: break (should stop being feed with debugger commands), 0: continue (can be feed with other debugger commands)
int process_debug_cmd(char *cmdline) {
    char *cmd = strtok(cmdline, " \n\r");
    if (!cmd)
        return 0;

    if (!strcasecmp(cmd, "?") || !strcasecmp(cmd, "h")) {
        gui_debug_printf(
                    "Debugger commands:\n"
                    "b - stack backtrace\n"
                    "c - continue\n"
                    "d <address> - dump memory\n"
                    "k <address> <+r|+w|+x|-r|-w|-x> - add/remove breakpoint\n"
                    "k - show breakpoints\n"
                    "flashsave - write modified flash blocks back to the image\n"
                    "ln c - connect\n"
                    "ln g <calc path> <host path> - fetch a file off the calculator\n"
                    "ln md <dir> - create a directory\n"
                    "ln os <file> - send an OS image\n"
                    "ln s <file> - send a file\n"
                    "ln st <dir> - set target directory\n"
                    "ln key <code> - send a 24-bit OS key through USB\n"
                    "mmu - dump memory mappings\n"
                    "n - continue until next instruction\n"
                    "pr <address> - port or memory read\n"
                    "pw <address> <value> - port or memory write\n"
                    "r - show registers\n"
                    "rs <regnum> <value> - change register value\n"
                    "ss <address> <length> <string> - search a string\n"
                    "s - step instruction\n"
                    "t+ - enable instruction translation\n"
                    "t- - disable instruction translation\n"
                    "u[a|t] [address] - disassemble memory\n"
                    "wm <file> <start> <size> - write memory to file\n"
                    "wf <file> <start> [size] - write file to memory\n"
                    "key <name> [scans] - tap a key, released after scans keypad sweeps (default 120, ~100ms)\n"
                    "key <name> <+|-> - hold or release a key\n"
                    "key ? - list key names\n"
                    "screenshot <file.ppm> - write the current screen\n"
                    "stop - stop the emulation\n"
                    "exec <path> - run a file with ndl, queued until the guest reaches the loader task\n"
                    "exec - - cancel a queued exec or blob\n"
                    "blob <file> - run an ARM code blob on the gui task, queued like exec (Ndl's stage0 installs Ndl this way)\n");
    } else if (!strcasecmp(cmd, "b")) {
        char *fp = strtok(NULL, " \n\r");
        backtrace(fp ? parse_expr(fp) : arm.reg[11]);
    } else if (!strcasecmp(cmd, "mmu")) {
        mmu_dump_tables();
    } else if (!strcasecmp(cmd, "r")) {
        int i, show_spsr;
        uint32_t cpsr = get_cpsr();
        const char *mode;
        for (i = 0; i < 16; i++) {
            int newline = ((1 << 5) | (1 << 11) | (1 << 15)) & (1 << i);
            gui_debug_printf("%3s=%08x%c", reg_name[i], arm.reg[i], newline ? '\n' : ' ');
        }
        switch (cpsr & 0x1F) {
            case MODE_USR: mode = "usr"; show_spsr = 0; break;
            case MODE_SYS: mode = "sys"; show_spsr = 0; break;
            case MODE_FIQ: mode = "fiq"; show_spsr = 1; break;
            case MODE_IRQ: mode = "irq"; show_spsr = 1; break;
            case MODE_SVC: mode = "svc"; show_spsr = 1; break;
            case MODE_ABT: mode = "abt"; show_spsr = 1; break;
            case MODE_UND: mode = "und"; show_spsr = 1; break;
            default:       mode = "???"; show_spsr = 0; break;
        }
        gui_debug_printf("cpsr=%08x (N=%d Z=%d C=%d V=%d Q=%d IRQ=%s FIQ=%s T=%d Mode=%s)",
                         cpsr,
                         arm.cpsr_n, arm.cpsr_z, arm.cpsr_c, arm.cpsr_v,
                         cpsr >> 27 & 1,
                         (cpsr & 0x80) ? "off" : "on ",
                         (cpsr & 0x40) ? "off" : "on ",
                         cpsr >> 5 & 1,
                         mode);
        if (show_spsr)
            gui_debug_printf(" spsr=%08x", get_spsr());
        gui_debug_printf("\n");
    } else if (!strcasecmp(cmd, "rs")) {
        char *reg = strtok(NULL, " \n\r");
        if (!reg) {
            gui_debug_printf("Parameters are missing.\n");
        } else {
            char *value = strtok(NULL, " \n\r");
            if (!value) {
                gui_debug_printf("Missing value parameter.\n");
            } else {
                int regi = atoi(reg);
                int valuei = parse_expr(value);
                if (regi >= 0 && regi <= 15)
                    arm.reg[regi] = valuei;
                else
                    gui_debug_printf("Invalid register.\n");
            }
        }
    } else if (!strcasecmp(cmd, "k")) {
        const char *addr_str = strtok(NULL, " \n\r");
        const char *flag_str = strtok(NULL, " \n\r");
        if (!flag_str)
            flag_str = "+x";
        if (addr_str) {
            uint32_t addr = parse_expr((char*) addr_str);
            void *ptr = virt_mem_ptr(addr & ~3, 4);
            if (ptr) {
                uint32_t *flags = &RAM_FLAGS(ptr);
                bool on = true;
                for (; *flag_str; flag_str++) {
                    switch (tolower(*flag_str)) {
                        case '+': on = true; break;
                        case '-': on = false; break;
                        case 'r':
                            if (on) *flags |= RF_READ_BREAKPOINT;
                            else *flags &= ~RF_READ_BREAKPOINT;
                            break;
                        case 'w':
                            if (on) *flags |= RF_WRITE_BREAKPOINT;
                            else *flags &= ~RF_WRITE_BREAKPOINT;
                            break;
                        case 'x':
                            if (on) {
                                if (*flags & RF_CODE_TRANSLATED) flush_translations();
                                *flags |= RF_EXEC_BREAKPOINT;
                            } else
                                *flags &= ~RF_EXEC_BREAKPOINT;
                            break;
                    }
                }
            } else {
                gui_debug_printf("Address %08X is not in RAM.\n", addr);
            }
        } else {
            unsigned int area;
            for (area = 0; area < sizeof(mem_areas)/sizeof(*mem_areas); area++) {
                uint32_t *flags;
                uint32_t *flags_start = &RAM_FLAGS(mem_areas[area].ptr);
                uint32_t *flags_end = &RAM_FLAGS(mem_areas[area].ptr + mem_areas[area].size);
                for (flags = flags_start; flags != flags_end; flags++) {
                    uint32_t addr = mem_areas[area].base + ((uint8_t *)flags - (uint8_t *)flags_start);
                    if (*flags & (RF_READ_BREAKPOINT | RF_WRITE_BREAKPOINT | RF_EXEC_BREAKPOINT)) {
                        gui_debug_printf("%08x %c%c%c\n",
                                         addr,
                                         (*flags & RF_READ_BREAKPOINT)  ? 'r' : ' ',
                                         (*flags & RF_WRITE_BREAKPOINT) ? 'w' : ' ',
                                         (*flags & RF_EXEC_BREAKPOINT)  ? 'x' : ' ');
                    }
                }
            }
        }
    } else if (!strcasecmp(cmd, "c")) {
        return 1;
    } else if (!strcasecmp(cmd, "s")) {
        cpu_events |= EVENT_DEBUG_STEP;
        return 1;
    } else if (!strcasecmp(cmd, "n")) {
        set_debug_next((uint32_t*) virt_mem_ptr(arm.reg[15] & ~3, 4) + 1);
        return 1;
    } else if (!strcasecmp(cmd, "d")) {
        char *arg = strtok(NULL, " \n\r");
        if (!arg) {
            gui_debug_printf("Missing address parameter.\n");
        } else {
            uint32_t addr = parse_expr(arg);
            dump(addr);
        }
    } else if (!strcasecmp(cmd, "u")) {
        disasm(disasm_insn);
    } else if (!strcasecmp(cmd, "ua")) {
        disasm(disasm_arm_insn);
    } else if (!strcasecmp(cmd, "ut")) {
        disasm(disasm_thumb_insn);
    } else if (!strcasecmp(cmd, "ln")) {
        char *ln_cmd = strtok(NULL, " \n\r");
        if (!ln_cmd) return 0;
        // The queue discards work while the link is down and says nothing, so a transfer issued
        // then is indistinguishable from one that ran and produced no output. Warn once here.
        if (!usblink_connected && strcasecmp(ln_cmd, "c") && strcasecmp(ln_cmd, "st")
            && strcasecmp(ln_cmd, "?"))
            gui_debug_printf("Warning: the link is not up, so this will be dropped rather than "
                             "sent. Use ln c while the OS is still booting.\n");
        if (!strcasecmp(ln_cmd, "c")) {
            usblink_connect();
            return 1; // and continue, ARM code needs to be run
        } else if (!strcasecmp(ln_cmd, "s")) {
            char *file = strtok(NULL, "\n");
            if (!file) {
                gui_debug_printf("Missing file parameter.\n");
            } else {
                file = strip_quotes(file);
                usblink_connect();

                const char *file_name = file;
                for (const char *p = file; *p; p++)
                    if (*p == ':' || *p == '/' || *p == '\\')
                        file_name = p + 1;

                if (ln_target_folder.length() < 1 || *ln_target_folder.rbegin() != '/')
                    ln_target_folder += '/';

                usblink_queue_put_file(std::string(file), ln_target_folder + std::string(file_name), ln_progress, nullptr);
            }
        } else if (!strcasecmp(ln_cmd, "os")) {
            char *file = strtok(NULL, "\n");
            if (!file) {
                gui_debug_printf("Missing file parameter.\n");
            } else {
                file = strip_quotes(file);
                usblink_connect();
                usblink_queue_send_os(std::string(file), ln_progress, nullptr);
            }
            return 1; // the transfer only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "g")) {
            char *remote = strtok(NULL, " \n\r");
            char *local = strtok(NULL, "\n");
            if (!remote || !local) {
                gui_debug_printf("Usage: ln g <calculator path> <host path>\n");
            } else {
                usblink_connect();
                usblink_queue_download(std::string(remote), std::string(local), ln_progress, nullptr);
            }
            return 1; // the transfer only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "rm")) {
            char *file = strtok(NULL, "\n");
            if (!file) {
                gui_debug_printf("Usage: ln rm <calculator path>\n");
            } else {
                usblink_connect();
                usblink_queue_delete(std::string(file), false, ln_progress, nullptr);
            }
            return 1; // the transfer only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "mv")) {
            // The OS refuses an upload whose name is not .tns, so a file it will not accept
            // directly has to arrive as .tns and be renamed afterwards.
            char *from = strtok(NULL, " \n\r");
            char *to = strtok(NULL, "\n");
            if (!from || !to) {
                gui_debug_printf("Usage: ln mv <old path> <new path>\n");
            } else {
                usblink_connect();
                usblink_queue_move(std::string(from), std::string(to), ln_progress, nullptr);
            }
            return 1; // the transfer only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "md")) {
            char *dir = strtok(NULL, "\n");
            if (!dir) {
                gui_debug_printf("Missing directory parameter.\n");
            } else {
                usblink_connect();
                usblink_queue_new_dir(std::string(dir), ln_progress, nullptr);
            }
            return 1; // the transfer only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "key")) {
            char *code = strtok(NULL, " \n\r");
            char *end = nullptr;
            unsigned long key = code ? strtoul(code, &end, 0) : 0;
            if (!code || end == code || *end || key > 0xffffff || strtok(NULL, " \n\r")) {
                gui_debug_printf("Usage: ln key <24-bit OS key code>\n");
            } else {
                usblink_queue_key(static_cast<uint32_t>(key), ln_progress, nullptr);
            }
            return 1;
        } else if (!strcasecmp(ln_cmd, "svc")) {
            // One packet to a calculator service by number, reply printed as hex. This is how a
            // service that a program registered on the calculator is exercised from here.
            char *sid_arg = strtok(NULL, " \n\r");
            char *hex = strtok(NULL, "\n\r");
            std::vector<uint8_t> payload;
            if (!sid_arg || !hex || !parse_hex_bytes(hex, payload) || payload.empty()) {
                gui_debug_printf("Usage: ln svc <service id> <hex bytes>\n");
            } else {
                usblink_connect();
                usblink_queue_raw(parse_expr(sid_arg), payload, ln_svc_reply, nullptr);
            }
            return 1; // the exchange only progresses while ARM code runs
        } else if (!strcasecmp(ln_cmd, "st")) {
            char *dir = strtok(NULL, " \n\r");
            if (dir)
                ln_target_folder = dir;
            else
                gui_debug_printf("Missing directory parameter.\n");
        } else if (!strcasecmp(ln_cmd, "?")) {
            // usblink_queue_do drops everything queued while this is false, silently, so every
            // failure mode looks the same from outside. Report it instead of inferring it.
            gui_debug_printf("usblink %s, target folder %s\n",
                             usblink_connected ? "connected" : "not connected",
                             ln_target_folder.c_str());
        }
    } else if (!strcasecmp(cmd, "touch")) {
        // A separate peripheral from the keypad, so it is worth trying when keys sent this way are
        // reaching the data register but the OS is not acting on them. x and y are 0..1 over the
        // pad, and no argument means a click in the middle, which activates the focused control.
        char *xs = strtok(NULL, " \n\r");
        char *ys = strtok(NULL, " \n\r");
        char *state = strtok(NULL, " \n\r");
        float x = xs ? atof(xs) : 0.5f, y = ys ? atof(ys) : 0.5f;
        bool down = !state || *state == '+';
        touchpad_set_state(x, y, true, down);
    } else if (!strcasecmp(cmd, "savestate")) {
        // flashsave keeps the filesystem but not the running OS, and some state only exists in RAM:
        // Ndl is loaded by a one-shot persistence trigger the OS consumes at boot, so a flash
        // image alone cannot carry it. A snapshot can, and --snapshot loads one back.
        char *file = strtok(NULL, "\n");
        if (!file)
            gui_debug_printf("Usage: savestate <file>\n");
        else
            gui_debug_printf(emu_suspend(file) ? "State saved to %s\n"
                                              : "Could not save state to %s\n", file);
    } else if (!strcasecmp(cmd, "flashsave")) {
        // Only the Qt front end calls this on exit, so without it a headless OS install is
        // discarded when the process ends.
        flash_save_changes();
    } else if (!strcasecmp(cmd, "taskinfo")) {
        uint32_t task = parse_expr(strtok(NULL, " \n\r"));
        uint8_t *p = (uint8_t*) virt_mem_ptr(task, 52);
        if (p) {
            gui_debug_printf("Previous:	%08x\n", *(uint32_t *)&p[0]);
            gui_debug_printf("Next:		%08x\n", *(uint32_t *)&p[4]);
            gui_debug_printf("ID:		%c%c%c%c\n", p[15], p[14], p[13], p[12]);
            gui_debug_printf("Name:		%.8s\n", &p[16]);
            gui_debug_printf("Status:		%02x\n", p[24]);
            gui_debug_printf("Delayed suspend:%d\n", p[25]);
            gui_debug_printf("Priority:	%02x\n", p[26]);
            gui_debug_printf("Preemption:	%d\n", p[27]);
            gui_debug_printf("Stack start:	%08x\n", *(uint32_t *)&p[36]);
            gui_debug_printf("Stack end:	%08x\n", *(uint32_t *)&p[40]);
            gui_debug_printf("Stack pointer:	%08x\n", *(uint32_t *)&p[44]);
            gui_debug_printf("Stack size:	%08x\n", *(uint32_t *)&p[48]);
            uint32_t sp = *(uint32_t *)&p[44];
            uint32_t *psp = (uint32_t*) virt_mem_ptr(sp, 18 * 4);
            if (psp) {
#ifdef __i386__
                gui_debug_printf("Stack type:	%d (%s)\n", psp[0], psp[0] ? "Interrupt" : "Normal");
                if (psp[0]) {
                    gui_debug_vprintf("cpsr=%08x  r0=%08x r1=%08x r2=%08x r3=%08x  r4=%08x\n"
                                      "  r5=%08x  r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x\n"
                                      " r11=%08x r12=%08x sp=%08x lr=%08x pc=%08x\n",
                                      (va_list)&psp[1]);
                } else {
                    gui_debug_vprintf("cpsr=%08x  r4=%08x  r5=%08x  r6=%08x r7=%08x r8=%08x\n"
                                      "  r9=%08x r10=%08x r11=%08x r12=%08x pc=%08x\n",
                                      (va_list)&psp[1]);
                }
#endif
            }
        }
    } else if (!strcasecmp(cmd, "tasklist")) {
        uint32_t tasklist = parse_expr(strtok(NULL, " \n\r"));
        uint8_t *p = (uint8_t*) virt_mem_ptr(tasklist, 4);
        if (p) {
            uint32_t first = *(uint32_t *)p;
            uint32_t task = first;
            gui_debug_printf("Task      ID   Name     St D Pr P | StkStart StkEnd   StkPtr   StkSize\n");
            do {
                p = (uint8_t*) virt_mem_ptr(task, 52);
                if (!p)
                    return 0;
                gui_debug_printf("%08X: %c%c%c%c %-8.8s %02x %d %02x %d | %08x %08x %08x %08x\n",
                                 task, p[15], p[14], p[13], p[12],
                        &p[16], /* name */
                        p[24],  /* status */
                        p[25],  /* delayed suspend */
                        p[26],  /* priority */
                        p[27],  /* preemption */
                        *(uint32_t *)&p[36], /* stack start */
                        *(uint32_t *)&p[40], /* stack end */
                        *(uint32_t *)&p[44], /* stack pointer */
                        *(uint32_t *)&p[48]  /* stack size */
                        );
                task = *(uint32_t *)&p[4]; /* next */
            } while (task != first);
        }
    } else if (!strcasecmp(cmd, "t+")) {
        do_translate = true;
    } else if (!strcasecmp(cmd, "t-")) {
        flush_translations();
        do_translate = false;
    } else if (!strcasecmp(cmd, "wm") || !strcasecmp(cmd, "wf")) {
        bool frommem = cmd[1] != 'f';
        char *filename = strtok(NULL, " \n\r");
        char *start_str = strtok(NULL, " \n\r");
        char *size_str = strtok(NULL, " \n\r");
        if (!start_str) {
            gui_debug_printf("Parameters are missing.\n");
            return 0;
        }
        uint32_t start = parse_expr(start_str);
        uint32_t size = 0;
        if (size_str)
            size = parse_expr(size_str);
        void *ram = phys_mem_ptr(start, size);
        if (!ram) {
            gui_debug_printf("Address range %08x-%08x is not in RAM.\n", start, start + size - 1);
            return 0;
        }
        FILE *f = fopen_utf8(filename, frommem ? "wb" : "rb");
        if (!f) {
            gui_perror(filename);
            return 0;
        }
        if (!size && !frommem) {
            fseek (f, 0, SEEK_END);
            size = ftell(f);
            rewind(f);
        }
        size_t ret;
        if(frommem)
            ret = fwrite(ram, size, 1, f);
        else
            ret = fread(ram, size, 1, f);
        if (!ret) {
            fclose(f);
            gui_perror(filename);
            return 0;
        }
        fclose(f);
        return 0;
    } else if (!strcasecmp(cmd, "ss")) {
        char *addr_str = strtok(NULL, " \n\r");
        char *len_str = strtok(NULL, " \n\r");
        char *string = strtok(NULL, " \n\r");
        if (!addr_str || !len_str || !string) {
            gui_debug_printf("Missing parameters.\n");
        } else {
            uint32_t addr = parse_expr(addr_str);
            uint32_t len = parse_expr(len_str);
            char *strptr = (char*) phys_mem_ptr(addr, len);
            char *ptr = strptr;
            char *endptr = strptr + len;
            if (ptr) {
                size_t slen = strlen(string);
                while (1) {
                    ptr = (char*) memchr(ptr, *string, endptr - ptr);
                    if (!ptr) {
                        gui_debug_printf("String not found.\n");
                        return 0;
                    }
                    if (!memcmp(ptr, string, slen)) {
                        uint32_t found_addr = ptr - strptr + addr;
                        gui_debug_printf("Found at address %08x.\n", found_addr);
                        return 0;
                    }
                    if (ptr < endptr)
                        ptr++;
                }
            } else {
                gui_debug_printf("Address range %08x-%08x is not in RAM.\n", addr, addr + len - 1);
            }
        }
        return 0;
    } else if (!strcasecmp(cmd, "int")) {
        gui_debug_printf("active		= %08x\n", intr.active);
        gui_debug_printf("status		= %08x\n", intr.status);
        gui_debug_printf("mask		= %08x %08x\n", intr.mask[0], intr.mask[1]);
        gui_debug_printf("priority_limit	= %02x       %02x\n", intr.priority_limit[0], intr.priority_limit[1]);
        gui_debug_printf("noninverted	= %08x\n", intr.noninverted);
        gui_debug_printf("sticky		= %08x\n", intr.sticky);
        gui_debug_printf("priority:\n");
        int i, j;
        for (i = 0; i < 32; i += 16) {
            gui_debug_printf("\t");
            for (j = 0; j < 16; j++)
                gui_debug_printf("%02x ", intr.priority[i+j]);
            gui_debug_printf("\n");
        }
    } else if (!strcasecmp(cmd, "int+")) {
        int_set(atoi(strtok(NULL, " \n\r")), 1);
    } else if (!strcasecmp(cmd, "int-")) {
        int_set(atoi(strtok(NULL, " \n\r")), 0);
    } else if (!strcasecmp(cmd, "pr")) {
        // TODO: need to avoid entering debugger recursively
        // also, where should error() go?
        uint32_t addr = parse_expr(strtok(NULL, " \n\r"));
        gui_debug_printf("%08x\n", mmio_read_word(addr));
    } else if (!strcasecmp(cmd, "pw")) {
        // TODO: ditto
        uint32_t addr = parse_expr(strtok(NULL, " \n\r"));
        uint32_t value = parse_expr(strtok(NULL, " \n\r"));
        mmio_write_word(addr, value);
    } else if (!strcasecmp(cmd, "key")) {
        char *name = strtok(NULL, " \n\r");
        char *arg = strtok(NULL, " \n\r");
        if (!name) {
            gui_debug_printf("Which key? Try 'key ?'.\n");
            return 0;
        }
        if (!strcmp(name, "?")) {
            for (size_t i = 0; i < sizeof key_names / sizeof *key_names; i++)
                gui_debug_printf("%-7s%c", key_names[i].name, (i % 8 == 7) ? '\n' : ' ');
            gui_debug_printf("\n");
            return 0;
        }
        const key_name *k = NULL;
        for (size_t i = 0; i < sizeof key_names / sizeof *key_names; i++)
            if (!strcasecmp(name, key_names[i].name))
                k = &key_names[i];
        if (!k) {
            gui_debug_printf("No key called '%s'. Try 'key ?'.\n", name);
            return 0;
        }
        int row = k->id / KEYPAD_COLS, col = k->id % KEYPAD_COLS;
        if (arg && (!strcmp(arg, "+") || !strcmp(arg, "-")))
            keypad_set_key(row, col, *arg == '+');
        else {
            // A sweep is about 0.9ms at the rate the OS programs, so 120 is a ~100ms press. Shorter
            // taps reach the data register but get debounced away before the OS acts on them.
            int scans = arg ? atoi(arg) : 120;
            keypad_tap_key(row, col, scans > 0 ? scans : 1);
        }
    } else if (!strcasecmp(cmd, "screenshot")) {
        // Same RGB565 buffer the Qt front end paints, written as a binary PPM so a headless run
        // can be looked at rather than guessed at.
        char *filename = strtok(NULL, " \n\r");
        if (!filename) {
            gui_debug_printf("Where to?\n");
            return 0;
        }
        static uint16_t framebuffer[320 * 240];
        lcd_cx_draw_frame(framebuffer);
        FILE *f = fopen_utf8(filename, "wb");
        if (!f) {
            gui_perror(filename);
            return 0;
        }
        fprintf(f, "P6\n320 240\n255\n");
        for (unsigned i = 0; i < 320 * 240; ++i) {
            uint16_t p = framebuffer[i];
            uint8_t rgb[3] = { uint8_t((p >> 11 & 0x1F) * 255 / 31),
                               uint8_t((p >> 5 & 0x3F) * 255 / 63),
                               uint8_t((p & 0x1F) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        gui_debug_printf("screenshot -> %s\n", filename);
    } else if(!strcasecmp(cmd, "stop")) {
	exiting = true;
        return 0;
    } else if(!strcasecmp(cmd, "exec")) {
        char *path = strtok(NULL, " \n\r");
        if(!path)
        {
            gui_debug_printf("You need to supply a path!\n");
            return 0;
        }

        if(!strcmp(path, "-"))
        {
            gui_debug_printf(armloader_deferred_pending() ? "exec: queued run cancelled\n"
                                                          : "exec: nothing was queued\n");
            armloader_cancel_deferred();
            return 0;
        }

        // The snippet ends up running on whatever stack the debugger broke into, which is the idle
        // loop's SRAM stack nearly every time and has no OS task behind it at all. Hand it to the
        // loader, which waits for the task Ndl runs programs on instead of running here.
        bool now = armloader_ndl_context();
        armloader_defer_exec(path);
        if(!now)
            gui_debug_printf("exec: this is not the task Ndl loads programs from (sp %08x), so "
                             "%s is queued until the guest reaches it. Continue the guest, and press "
                             "a key on the calculator if it stays idle. Cancel with `exec -`.\n",
                             arm.reg[13], path);
        return 1;
    } else if(!strcasecmp(cmd, "blob")) {
        char *filename = strtok(NULL, " \n\r");
        if(!filename) {
            gui_debug_printf("Usage: blob <file>\n");
            return 0;
        }
        FILE *f = fopen_utf8(filename, "rb");
        if(!f) {
            gui_perror(filename);
            return 0;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        // The blob runs on the gui task's stack, which is 64K, so leave the task most of it.
        if(size <= 0 || size > 32768) {
            fclose(f);
            gui_debug_printf("blob: %s is %ld bytes, want 1 to 32768\n", filename, size);
            return 0;
        }
        std::vector<uint8_t> blob(size);
        bool read = fread(blob.data(), size, 1, f) == 1;
        fclose(f);
        if(!read) {
            gui_debug_printf("blob: could not read %s\n", filename);
            return 0;
        }

        bool now = armloader_ndl_context();
        if(!armloader_defer_blob(blob.data(), blob.size())) {
            gui_debug_printf("blob: out of memory\n");
            return 0;
        }
        if(!now)
            gui_debug_printf("blob: this is not the gui task (sp %08x), so %s is queued until the "
                             "guest reaches it. Continue the guest. Cancel with `exec -`.\n",
                             arm.reg[13], filename);
        return 1;
    } else {
        gui_debug_printf("Unknown command %s\n", cmd);
    }
    return 0;
}

#define MAX_CMD_LEN 300

static void native_debugger(void) {
    uint32_t *cur_insn = (uint32_t*) virt_mem_ptr(arm.reg[15] & ~3, 4);

    // Did we hit the "next" breakpoint?
    if (cur_insn == debug_next) {
        set_debug_next(NULL);
        disasm_insn(arm.reg[15]);
    }

    if (cpu_events & EVENT_DEBUG_STEP) {
        cpu_events &= ~EVENT_DEBUG_STEP;
        disasm_insn(arm.reg[15]);
    }

    throttle_timer_off();
    while (1) {
        debug_input_cur = nullptr;

        std::unique_lock<std::mutex> lk(debug_input_m);

        gui_debugger_request_input(debug_input_callback);

        while(!debug_input_cur)
        {
            debug_input_cv.wait_for(lk, std::chrono::milliseconds(100), []{return debug_input_cur;});
            if(debug_input_cur || exiting)
                break;

            gui_do_stuff(false);
        }

        gui_debugger_request_input(nullptr);

        if(exiting)
            return;

        char *copy = strdup(debug_input_cur);
        if(!copy)
            return;

        int ret = process_debug_cmd(copy);

        free(copy);

        if(ret)
            break;
        else
            continue;
    }
    throttle_timer_on();
}

static int listen_socket_fd = -1;
static int socket_fd = -1;

static void log_socket_error(const char *msg) {
#ifdef __MINGW32__
    int errCode = WSAGetLastError();
    LPSTR errString = NULL;  // will be allocated and filled by FormatMessage
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, errCode, 0, (LPSTR)&errString, 0, 0);
    gui_debug_printf( "%s: %s (%i)\n", msg, errString, errCode);
    LocalFree( errString );
#else
    gui_perror(msg);
#endif
}

static void set_nonblocking(int socket, bool nonblocking) {
#ifdef __MINGW32__
    u_long mode = nonblocking;
    ioctlsocket(socket, FIONBIO, &mode);
#else
    int ret = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, nonblocking ? ret | O_NONBLOCK : ret & ~O_NONBLOCK);
    ret = fcntl(socket, F_GETFD, 0);
    fcntl(socket, F_SETFD, ret | FD_CLOEXEC);
#endif
}

bool rdebug_bind(unsigned int port) {
    struct sockaddr_in sockaddr;
    int r;

#ifdef __MINGW32__
    WORD wVersionRequested = MAKEWORD(2, 0);
    WSADATA wsaData;
    if (WSAStartup(wVersionRequested, &wsaData)) {
        log_socket_error("WSAStartup failed");
        return false;
    }
#endif

    listen_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_socket_fd == -1) {
        log_socket_error("Remote debug: Failed to create socket");
        return false;
    }
    set_nonblocking(listen_socket_fd, true);

    memset(&sockaddr, '\000', sizeof sockaddr);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    r = bind(listen_socket_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (r == -1) {
        log_socket_error("Remote debug: failed to bind socket. Check that Firebird is not already running!");
        return false;
    }
    r = listen(listen_socket_fd, 0);
    if (r == -1) {
        log_socket_error("Remote debug: failed to listen on socket");
        return false;
    }

    return true;
}

static char rdebug_inbuf[MAX_CMD_LEN];
size_t rdebug_inbuf_used = 0;

void rdebug_recv(void) {
    if(listen_socket_fd == -1)
        return;

    int ret, on;
    if (socket_fd == -1) {
        ret = accept(listen_socket_fd, NULL, NULL);
        if (ret == -1)
            return;
        socket_fd = ret;
        set_nonblocking(socket_fd, true);
        /* Disable Nagle for low latency */
        on = 1;
#ifdef __MINGW32__
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
#else
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
#endif
        if (ret == -1)
            log_socket_error("Remote debug: setsockopt(TCP_NODELAY) failed for socket");
        gui_debug_printf("Remote debug: connected.\n");
        return;
    }

    while(true)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET((unsigned)socket_fd, &rfds);
        struct timeval zero = {0, 100000};
        ret = select(socket_fd + 1, &rfds, NULL, NULL, &zero);
        if (ret == -1 && errno == EBADF) {
            gui_debug_printf("Remote debug: connection closed.\n");
            #ifdef __MINGW32__
                closesocket(socket_fd);
            #else
                close(socket_fd);
            #endif
            socket_fd = -1;
        }
        else if (!ret) // No data available
        {
            if(exiting)
            {
                close(socket_fd);
                socket_fd = -1;
                return;
            }

            gui_do_stuff(false);
        }
        else // Data available
            break;
    }

    size_t buf_remain = sizeof(rdebug_inbuf) - rdebug_inbuf_used;
    if (!buf_remain) {
        gui_debug_printf("Remote debug: command is too long\n");
        return;
    }

#ifdef __MINGW32__
    ssize_t rv = recv(socket_fd, &rdebug_inbuf[rdebug_inbuf_used], buf_remain, 0);
#else
    ssize_t rv = recv(socket_fd, (void*) &rdebug_inbuf[rdebug_inbuf_used], buf_remain, 0);
#endif
    if (!rv) {
        gui_debug_printf("Remote debug: connection closed.\n");
#ifdef __MINGW32__
        closesocket(socket_fd);
#else
        close(socket_fd);
#endif
        socket_fd = -1;
        return;
    }
    if (rv < 0 && errno == EAGAIN) {
        /* no data for now, call back when the socket is readable */
        return;
    }
    if (rv < 0) {
        log_socket_error("Remote debug: connection error");
        return;
    }
    rdebug_inbuf_used += rv;

    char *line_start = rdebug_inbuf;
    char *line_end;
    while ( (line_end = (char*)memchr((void*)line_start, '\n', rdebug_inbuf_used - (line_start - rdebug_inbuf)))) {
        *line_end = 0;
        process_debug_cmd(line_start);
        line_start = line_end + 1;
    }
    /* Shift buffer down so the unprocessed data is at the start */
    rdebug_inbuf_used -= (line_start - rdebug_inbuf);
    memmove(rdebug_inbuf, line_start, rdebug_inbuf_used);
}

bool in_debugger = false;

void debugger(enum DBG_REASON reason, uint32_t addr) {
    /* Avoid debugging the debugger. */
    if (in_debugger)
        return;

    gui_debugger_entered_or_left(in_debugger = true);
    if (gdb_connected)
        gdbstub_debugger(reason, addr);
    else
        native_debugger();
    gui_debugger_entered_or_left(in_debugger = false);
}

void rdebug_quit()
{
    if(socket_fd != -1)
    {
        #ifdef __MINGW32__
            closesocket(socket_fd);
        #else
            close(socket_fd);
        #endif
        socket_fd = -1;
    }

    if(listen_socket_fd != -1)
    {
        #ifdef __MINGW32__
            closesocket(listen_socket_fd);
        #else
            close(listen_socket_fd);
        #endif
        listen_socket_fd = -1;
    }
}
