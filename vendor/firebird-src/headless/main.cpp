#include <errno.h>
#include <stdlib.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "core/debug.h"
#include "core/emu.h"
#include "core/mem.h"
#include "core/mmu.h"
#include "core/usblink_queue.h"

// Set by the stdin reader when a line turns up while the emulator is running. Core polls gui_do_stuff
// from the emulation thread, which is the only thread that may enter the debugger.
static std::atomic<bool> break_requested{false};

void gui_do_stuff(bool wait)
{
    if(break_requested.exchange(false) && !in_debugger)
        debugger(DBG_USER, 0);
}

void do_stuff(int i)
{
}

void gui_debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    gui_debug_vprintf(fmt, ap);

    va_end(ap);
}

void gui_debug_vprintf(const char *fmt, va_list ap)
{
    vprintf(fmt, ap);
}

void gui_status_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    gui_debug_vprintf(fmt, ap);

    putchar('\n');

    va_end(ap);
}

void gui_perror(const char *msg)
{
    gui_debug_printf("%s: %s\n", msg, strerror(errno));
}

void gui_debugger_entered_or_left(bool entered) {}

// The caller holds the debugger's input mutex and the callback takes that same mutex, so reading the
// line here and invoking the callback before returning deadlocks the one thread on a non-recursive
// std::mutex. Qt's front end returns immediately and calls back from the GUI thread; this does the
// same with a reader thread that owns stdin for the life of the process.
static std::mutex input_m;
static std::condition_variable input_cv;
static debug_input_cb input_wanted = nullptr;
static std::string input_spare;
static bool input_have_spare = false;
static bool input_reader_started = false;
// The callback only stores the pointer, and the debugger copies it after waking, so the text has to
// outlive the call. It stays valid until the next line is delivered, which cannot happen until the
// debugger has asked again.
static std::string input_delivered;

// The two markers that frame a command. READY says the debugger is waiting for one; ECHO says which
// one it is about to run. Together they let a driver read exactly one command's output instead of
// guessing from a gap in the stream.
const char DEBUGGER_READY[] = "<<fb-ready>>";
const char DEBUGGER_ECHO[] = "<<fb-cmd>> ";

static void input_reader()
{
    while(true)
    {
        std::string line;

        {
            std::unique_lock<std::mutex> lk(input_m);
            if(input_have_spare)
            {
                line = input_spare;
                input_have_spare = false;
            }
        }

        if(line.empty())
        {
            // Read whether or not the debugger has asked yet. Reading only on demand would leave a
            // command typed during a run sitting in the pipe, and nothing would notice it until the
            // emulator stopped for some other reason.
            // Whole lines only: a path or a hex payload longer than one buffer used to arrive as
            // two commands, the second one nonsense.
            char buffer[256];
            while(fgets(buffer, sizeof(buffer), stdin))
            {
                line += buffer;
                if(!line.empty() && line.back() == '\n')
                    break;
            }
            if(line.empty())
            {
                exiting = true;
                return;
            }
        }

        std::unique_lock<std::mutex> lk(input_m);
        if(!input_wanted)
        {
            // Nothing is waiting, so the emulator is running. Hold the line and ask it to stop, which
            // is what makes a command typed mid-run take effect rather than wait for a breakpoint.
            input_spare = line;
            input_have_spare = true;
            break_requested = true;
            input_cv.wait(lk, []{ return input_wanted != nullptr; });
            continue;
        }

        debug_input_cb callback = input_wanted;
        break_requested = false;
        input_wanted = nullptr;
        input_delivered = line;
        lk.unlock();

        // Echo what is about to run. A command written while the emulator is running has to break it
        // out first, and entering the debugger prints its own ready marker before the command is
        // delivered, so "read until ready" would stop on that one and return nothing. The echo is
        // the only unambiguous start of a command's output.
        fputs(DEBUGGER_ECHO, stdout);
        fputs(input_delivered.c_str(), stdout);
        if(input_delivered.empty() || input_delivered.back() != '\n')
            putc('\n', stdout);
        fflush(stdout);

        callback(input_delivered.c_str());
    }
}

// Starting the reader lazily on the first debugger request means a session without
// --debug-on-start never reads stdin at all, because nothing enters the debugger to ask. Commands
// then sit unread in the pipe for the life of the process and the caller sees silence.
static void input_reader_start()
{
    std::unique_lock<std::mutex> lk(input_m);
    if(input_reader_started)
        return;

    input_reader_started = true;
    std::thread(input_reader).detach();
}

// Printed whenever the debugger is about to wait for a command, so a driver can tell "finished and
// printed nothing" from "still working" or "output lost". Without it the only end-of-command signal
// is a gap in the output, which is a guess: it cuts a slow command short and it cannot distinguish
// silence from a command that never ran.

void gui_debugger_request_input(debug_input_cb callback)
{
    std::unique_lock<std::mutex> lk(input_m);

    input_wanted = callback;
    input_cv.notify_all();

    // Stdout is block buffered when it is a pipe, so without this a caller waiting to see the
    // debugger's reply before writing the next command waits forever.
    if(callback)
    {
        fputs(DEBUGGER_READY, stdout);
        putc('\n', stdout);
        fflush(stdout);
    }
}

void gui_putchar(char c) { putc(c, stdout); }
int gui_getchar() { return -1; }
void gui_set_busy(bool busy) {}
void gui_show_speed(double d) {}
void gui_usblink_changed(bool state) {}
void throttle_timer_off() {}
void throttle_timer_on() {}
void throttle_timer_wait(unsigned int usec) {}

static const char OPT_BOOT1[]              = "--boot1";
static const char OPT_FLASH[]              = "--flash";
static const char OPT_SNAPSHOT[]           = "--snapshot";
static const char OPT_RAMPAYLOAD[]         = "--rampayload";
static const char OPT_RAMPAYLOAD_ADDR[]    = "--rampayload-address";
static const char OPT_DEBUG_ON_START[]     = "--debug-on-start";
static const char OPT_DEBUG_ON_WARN[]      = "--debug-on-warn";
static const char OPT_PRINT_ON_WARN[]      = "--print-on-warn";
static const char OPT_DIAGS[]              = "--diags";
static const char OPT_HELP[]               = "--help";
static const char OPT_GDB[]                = "--gdb";
static const char OPT_REALTIME[]           = "--realtime";
static const uint32_t default_rampayload_base = 0x10000000;

void show_help_menu(void)
{
	fprintf(stderr, "firebird-headless:\n");
	fprintf(stderr, "  %-24s Show this help menu\n", OPT_HELP);
	fprintf(stderr, "  %-24s Path to Boot1 image (required)\n", OPT_BOOT1);
	fprintf(stderr, "  %-24s Path to Flash image (required)\n", OPT_FLASH);
	fprintf(stderr, "  %-24s Path to snapshot image (optional)\n", OPT_SNAPSHOT);
	fprintf(stderr, "  %-24s Path to RAM payload (optional)\n", OPT_RAMPAYLOAD);
	fprintf(stderr, "  %-24s Address to load RAM payload at (default: 0x%x)\n",
	       OPT_RAMPAYLOAD_ADDR, default_rampayload_base);
	fprintf(stderr, "  %-24s Enter debugger on start\n", OPT_DEBUG_ON_START);
	fprintf(stderr, "  %-24s Enter debugger on warnings\n", OPT_DEBUG_ON_WARN);
	fprintf(stderr, "  %-24s Print warnings to console\n", OPT_PRINT_ON_WARN);
	fprintf(stderr, "  %-24s Use diagnostics boot order\n", OPT_DIAGS);
	fprintf(stderr, "  %-24s Listen for GDB on this TCP port\n", OPT_GDB);
	fprintf(stderr, "  %-24s Run at real speed instead of turbo\n", OPT_REALTIME);
}

int main(int argc, char *argv[])
{
	// Stdout is block buffered on a pipe, and the only flush is the one in
	// gui_debugger_request_input. Anything printed by a command that then resumes execution sits in
	// the buffer instead, so the command reads as having produced nothing at all.
	setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

	const char *boot1 = nullptr, *flash = nullptr, *snapshot = nullptr, *rampayload = nullptr;
	unsigned int port_gdb = 0;
	bool realtime = false;
	uint32_t rampayload_base = default_rampayload_base;

	// An option's value, or a usage error: a trailing option used to read one past argv.
	auto value = [&](int &argi) -> const char *
	{
		if(argi + 1 >= argc)
		{
			fprintf(stderr, "'%s' needs a value.\n", argv[argi]);
			show_help_menu();
			exit(1);
		}
		return argv[++argi];
	};

	for(int argi = 1; argi < argc; ++argi)
	{
		if(strcmp(argv[argi], OPT_BOOT1) == 0)
			boot1 = value(argi);
		else if(strcmp(argv[argi], OPT_FLASH) == 0)
			flash = value(argi);
		else if(strcmp(argv[argi], OPT_SNAPSHOT) == 0)
			snapshot = value(argi);
		else if(strcmp(argv[argi], OPT_RAMPAYLOAD) == 0)
			rampayload = value(argi);
		else if(strcmp(argv[argi], OPT_RAMPAYLOAD_ADDR) == 0)
			rampayload_base = strtol(value(argi), nullptr, 0);
		else if(strcmp(argv[argi], OPT_DEBUG_ON_START) == 0)
			debug_on_start = true;
		else if(strcmp(argv[argi], OPT_DEBUG_ON_WARN) == 0)
			debug_on_warn = true;
		else if(strcmp(argv[argi], OPT_PRINT_ON_WARN) == 0)
			print_on_warn = true;
		else if(strcmp(argv[argi], OPT_GDB) == 0)
			port_gdb = strtoul(value(argi), nullptr, 0);
		else if(strcmp(argv[argi], OPT_REALTIME) == 0)
			realtime = true;
		else if(strcmp(argv[argi], OPT_DIAGS) == 0)
			boot_order = ORDER_DIAGS;
		else if (strcmp(argv[argi], OPT_HELP) == 0)
		{
			show_help_menu();
			return 0;
		}
		else
		{
			fprintf(stderr, "Unknown argument '%s'.\n", argv[argi]);
			show_help_menu();
			return 1;
		}
	}

	if (!boot1 || !flash)
	{
		fprintf(stderr, "You need to specify at least Boot1 and Flash images.\n");
		show_help_menu();
		return 2;
	}

	path_boot1 = boot1;
	path_flash = flash;

	if(!emu_start(port_gdb, 0, snapshot))
		return 1;

	if(rampayload)
	{
		FILE *f = fopen(rampayload, "rb");
		if(!f)
		{
			perror("Could not open RAM payload");
			return 3;
		}

		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		rewind(f);

		void *target = phys_mem_ptr(rampayload_base, size);
		if(!target)
		{
			fprintf(stderr, "RAM payload too big");
			return 5;
		}

		if(fread(target, size, 1, f) != 1)
		{
			perror("Could not read RAM payload");
			return 4;
		}

		fclose(f);

		// Jump to payload
		arm.reg[15] = rampayload_base;
	}

	// Turbo runs the guest as fast as the host allows, which is what a corpus run wants. --realtime
	// was added to test whether the rate is why the OS ignores keys sent this way. It is not, measured.
	turbo_mode = !realtime;
	input_reader_start();
	emu_loop(false);

	return 0;
}
