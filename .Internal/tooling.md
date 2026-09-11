# Tooling for working without a human in the loop

## Current entry points, 2026-09-08

Use [nps/README.md](../nps/README.md) for CMake builds and [the codebase map](../docs/codebase-map.md) for the current tool boundaries. The SDK is ndl-src/ndl-sdk with NDL_SDK as its explicit override. The retained compiler was moved during the naming migration, not rebuilt. Run nps/scripts/bootstrap-ndl.sh to inspect readiness. Older debugger build claims below do not establish that an optional debugger exists in the current installation.

Physical transfers use libnspire-src and tools/nsptool. Build a separate candidate with the command below. It checks the session protocol without opening the device. Do not replace a shared binary during an active session.

~~~sh
python3 tools/nsptool/build.py --output /private/tmp/nsptool-tooling-check
~~~

Use one serialized USB session. A timeout can occur during the request, reply, a file chunk, final status or service close. A failed command does not prove whether its intended device effect occurred. Establish that effect before retrying. A key-service receipt proves packet acceptance, not visible input. Verify the screen separately. Transfer dialogs have accompanied failures but the observations do not establish their cause.

The ordinary runtime loads startup programs from /documents/ndl/startup. An older resident runtime and its saved OS boot document keep their earlier names until explicitly replaced. Reset alone can reload persistence. Use the matching resources document only when uninstall is intended, wait for reboot and establish runtime state before starting a new installer. The persistent.tns document built by the current SDK contains its own installer. The old claim below that it requires a resident runtime is historical.

The later [physical checks](../nps/benchmarks/BUDGETS.md#physical-package-check-2026-09-08) identify loaded StepCAS packages and report Ndl r2022. Runtime persistence remains unqualified. A bounded transport probe passed 41 serial requests without reproducing the observed timeouts. Its retained traces are in /private/tmp/cx2-continuation-LPC7N8. This is no diagnosis of the intermittent failures. Emulator automation cannot replace physical qualification.

## Retained tooling investigations, 2026-09-01 through 2026-09-04

The remaining sections preserve commands, output, image names and hypotheses from those sessions. Their present-tense descriptions apply to those observations. Do not infer a current inventory, unresolved defect or current command contract from them without checking the owning source.

The question this file answers: what lets an agent build, deploy, run and debug StepCAS without
someone physically pressing keys on a calculator.

Short version: **a headless Firebird emulator is the whole answer**, and it is already built and
running on this machine. The blocker is not software, it is that emulating a CX II needs images
dumped from the physical device first.

## firebird-headless, built and verified here

Firebird is the community TI-Nspire emulator, GPLv3, at https://github.com/nspire-emus/firebird.
Its README states it "supports the emulation of Nspire Touchpad (CAS), CX (CAS) and CX II (-T/CAS)
calcs on Android, iOS, Linux, macOS and Windows."

Source cloned to firebird-src/. The headless target builds and runs on this Mac.

```
cd firebird-src
git submodule update --init --recursive     # core/gif-h is required
cd headless
make -j8 TOOLCHAIN_ARCH=aarch64
./firebird-headless --help
```

**The TOOLCHAIN_ARCH override is required on Apple Silicon.** The Makefile derives the arch from
the compiler target and tests `$(filter arm%,...)` before the aarch64 case. On this machine the
target is arm64-apple-darwin, which yields "arm64", which matches the `arm%` pattern, so the build
picks the 32-bit ARM JIT and fails assembling core/asmcode_arm.S. Forcing aarch64 selects
core/asmcode_aarch64.S and core/translate_aarch64.cpp and the build succeeds. Alternative escape
hatch is TRANSLATION_ENABLED=FALSE, which disables the JIT and is slower.

Verified output of the built binary:

```
firebird-headless:
  --help                   Show this help menu
  --boot1                  Path to Boot1 image (required)
  --flash                  Path to Flash image (required)
  --snapshot               Path to snapshot image (optional)
  --rampayload             Path to RAM payload (optional)
  --rampayload-address     Address to load RAM payload at (default: 0x10000000)
  --debug-on-start         Enter debugger on start
  --debug-on-warn          Enter debugger on warnings
  --print-on-warn          Print warnings to console
  --diags                  Use diagnostics boot order
```

CX II support is linked in: the headless Makefile compiles core/cx2.cpp, core/usb_cx2.cpp and
core/usblink_cx2.cpp. It also runs in turbo mode, see firebird-src/headless/main.cpp near the end
of main, which sets turbo_mode true before emu_loop. Corpus runs are not limited to real-time.

## Why headless matters: it takes commands on stdin

firebird-src/headless/main.cpp implements gui_debugger_request_input by calling fgets on stdin. The
debugger is therefore scriptable by piping commands in. The command set, quoted from
firebird-src/core/debug.cpp lines 193 to 216:

```
b - stack backtrace
c - continue
d <address> - dump memory
k <address> <+r|+w|+x|-r|-w|-x> - add/remove breakpoint
k - show breakpoints
ln c - connect
ln s <file> - send a file
ln st <dir> - set target directory
mmu - dump memory mappings
n - continue until next instruction
pr <address> - port or memory read
pw <address> <value> - port or memory write
r - show registers
rs <regnum> <value> - change register value
ss <address> <length> <string> - search a string
s - step instruction
t+ - enable instruction translation
t- - disable instruction translation
u[a|t] [address] - disassemble memory
wm <file> <start> <size> - write memory to file
wf <file> <start> [size] - write file to memory
key <name> [scans] - tap a key, released after scans keypad sweeps
key <name> <+|-> - hold or release a key
key ? - list key names
stop - stop the emulation
exec <path> - exec file with ndless
```

The three that make a hands-off loop possible are **ln c** to connect the emulated USB link,
**ln s <file>** to send a .tns into the emulated calculator, and **exec <path>** to run it under
Ndless. Add **wm** to dump a memory region to a host file and a test harness can assert on emulator
state directly.

The underlying transfer API is broader than the debugger exposes. From
firebird-src/core/usblink_queue.h:

```
usblink_queue_delete    usblink_queue_dirlist   usblink_queue_download
usblink_queue_put_file  usblink_queue_move      usblink_queue_new_dir
usblink_queue_send_os
```

If the debugger commands prove awkward, a small patch to headless/main.cpp exposing these as CLI
arguments is a contained change.

## GDB

Firebird has a GDB stub. The wiki page "Debugging with GDB" states "Firebird v0.20 or later, with
the GDB stub exposed on port 3333" and gives the attach command:

```
arm-none-eabi-gdb -ex "target remote :3333" program.elf
```

That is the same arm-none-eabi-gdb the Ndless toolchain builds, GDB 16.2, so one toolchain covers
both compiling and debugging.

**Done, 2026-09-01.** The stub was compiled in already, core/gdbstub.c is in the headless Makefile's
CSOURCES, but headless/main.cpp called emu_start(0, 0, snapshot) and port 0 disables it. There is
now a `--gdb <port>` option wired to that argument. With images/nspire-try6.img this is a working
debugger for on-calculator code, and the toolchain's own arm-none-eabi-gdb 16.2 is the client.

## Other pieces

| Tool | What it gives us | State |
|---|---|---|
| firebird-headless | scripted emulation, file transfer, exec, memory dumps, breakpoints | built and verified here |
| arm-none-eabi-gdb 16.2 | source-level debugging over the stub | built, and the stub is now exposed with --gdb |
| n-link | real-hardware file transfer and OS install, macOS build and WebUSB version | not installed |
| PolyDumper | dumps boot images off the calculator, needs Ndless running | done, images/ is populated |
| gif recording | core/gif.h offers gif_start_recording, gif_new_frame, gif_stop_recording | compiled into headless, not wired to a CLI flag |
| Ndless SDK | nspire-gcc, nspire-g++, genzehn, make-prg, luna | toolchain built, Ndless built and installed on hardware |

The gif recorder is worth remembering. Screenshots of the emulated 320x240 screen would let a UI
change be checked without a person looking at hardware, which matters for the PRD's
two-dimensional maths rendering requirements.

## What used to block the hands-off loop. All of it is now done

Everything in this section is history as of 2026-09-01: Ndless is live on the device, the images are
dumped, eMMUlate has supplied the keys, and the emulator boots an OS. Kept because it records where
the images came from.

### The original chain

firebird-headless needs a boot1 image and a flash image and refuses to start without both. Neither
can be downloaded. Per the Firebird wiki First Time Setup page, boot1 comes from **PolyDumper**,
run on the calculator with Ndless already active, which also dumps boot2, diags and manuf. The
flash image is then assembled in the Firebird GUI from those dumps plus an OS file.

So the dependency chain is linear and there is no way around the first step:

```
install Ndless on the real CX II
  -> run PolyDumper, dump boot1 / boot2 / manuf and the CX II partitions
    -> assemble nspire.img in the Firebird GUI
      -> from then on, iterate entirely in firebird-headless from the shell
```

**The CX II step nobody mentions up front.** Confirmed against
https://www.hackspire.org/Emulators/, which notes that a CX II also needs the keys eMMUlate recovers,
written into the btrom file produced by PolyDumper. So the
Firebird First Time Setup page, which only covers Nspire, CX and CM, is not the whole procedure.

eMMUlate is at https://github.com/satyamedh/eMMUlate. It recovers the boot keys the emulator needs,
using the MMU.

### The QR code is skippable

eMMUlate's README says to scan a QR with a phone and write the keys into the bootrom via a website.
Reading main.c shows that is a convenience rather than the mechanism, and the local path is both shorter
and safer.

`print_all_keys` (main.c around line 274) displays every extracted value on the calculator screen
before any QR exists, and says so outright at line 292: "You might want to write these keys down or
take a picture." `genqrcode` at line 299 then builds a URL whose entire payload is those same 24 hex
words as query parameters, pointing at eMMUlate's own static page.

That web page is static. Its whole algorithm is in js/app.js lines 124 to 175, and it is 30
lines: four key sets identified by DES parameter indices 0x27, 0x3d, 0x25 and 0x2d, each mapped to
an offset, each writing three key pairs as little-endian words, R before L.

```
index < 0x20 -> base 0x26000, adjusted = index
index >= 0x20 -> base 0x27800, adjusted = index - 0x20
offset = base + adjusted * 24
key N at offset + (N-1)*8 : uint32 R little endian, then uint32 L
```

All four indices in use are above 0x20, so in practice the sets land at 0x278a8, 0x27ab8, 0x27878
and 0x27938.

That is ported into the nspire MCP server as its `keys` tool, tested against independently computed
offsets. So the keys are typed off the calculator screen straight into the tool and never reach a
browser, which matters: pasting that URL would put the machine's own BootROM keys into history and
possibly into a referrer header.

### The real CX II chain

```
Ndless on the device            (done, the maintainer confirmed 2026-09-01)
  -> PolyDumper: btrom.img.tns (keys blanked), plus btldr, osldr, diags, manuf
    -> eMMUlate on the device: read 24 hex words off the screen
      -> nspire keys tool: patch them into btrom, producing boot1.img   (local, no phone)
        -> assemble nspire.img in the Firebird GUI
          -> firebird-headless, driven from the nspire MCP server
```

Note PolyDumper writes **btrom.img.tns** on CX II, not boot1.img. Its readme lists the CX II outputs
as Boot ROM (btrom), Boot Loader (btldr), OS Loader (osldr), Diagnostic Software (diags), Installer
(instl) and Manuf data (manuf). The boot1/boot2 names in the Firebird wiki are the CX and classic
layout.

Both tools are now on disk under `tools/`:

```
tools/polydumper.zip   620880 bytes, PolyDumper 5.0 by Critor, LGPL-2.1
tools/polydumper/polydumper_5/polyDumper.tns     249584 bytes, the one to transfer
tools/emmulate.zip     56097 bytes, eMMUlate v1
tools/emmulate/eMMUlate.tns
```

PolyDumper's page at https://tiplanet.org/forum/archives_voir.php?id=3829 returns 403 to a plain
fetch and its advertised download URL 404s. The path that works is
https://tiplanet.org/modules/archives/download.php?id=3829 with a browser user agent and a referer,
note the missing `/forum` segment.

If CX II emulation turns out not to work, the fallback is that firebird also emulates plain CX.
Much of the derivation kernel is portable C++ that does not care, so the kernel could still be
exercised in emulation with only the CX II specific work needing hardware.

## Building the emulator images

`tools/mkflash` assembles the flash image by linking Firebird's own `flash_create_new` against the
already compiled `firebird-src/core/*.o`, so the Qt GUI never has to be built. The four inputs are
PolyDumper's Manuf, Bootloader, Diags and Installer, in that order, and the preload indices are not
obvious from the names (core/flash.cpp lines 705 to 714). osldr and btrom are not part of the flash
image. btrom becomes the separate boot1 file after the keys are patched in.

```
c++ -std=c++11 -O2 -I../../firebird-src -DSUPPORT_LINUX mkflash.cpp ../../firebird-src/core/*.o -lz -o mkflash
./tools/mkflash/mkflash images/manuf.img images/btldr.img images/diags.img images/instl.img images/nspire.img
```

Product 0x1D0 is a non-CAS CX II, matching the 0x1D the calculator reports as its hardware type. The
image is 132 MiB, which is the size `flash_open` insists on for the large NAND part.

The flash image carries no OS. It boots to "Waiting for OS download", and the OS is a separate file
from TI. Only four builds are usable, because Ndless r2022 supports only 5.2.0.771, 5.3.0.564,
6.2.0.333 and 6.4.0.74. TI publishes the current one, 6.4.0.74, so the emulator runs that while the
physical calculator is on 6.2.0.333. Ndless covers both, so a program built against it runs on
either, but a hardcoded OS address would not.

## Fixes carried on top of upstream Firebird

Four, all in the headless build, and the first one is why none of this worked before.

**The headless debugger deadlocked on its own input mutex.** `native_debugger` holds `debug_input_m`
and then calls `gui_debugger_request_input`, and the headless implementation of that read a line and
invoked the callback before returning. The callback locks the same non-recursive `std::mutex` on the
same thread. Qt's front end stores the callback and calls it later from the GUI thread, so upstream
never hit it. Every debugger command was silently ignored. `headless/main.cpp` now runs a stdin
reader thread that delivers lines asynchronously.

**A command typed while the emulator was running was never read.** The reader drains stdin whether or
not the debugger has asked, and a line arriving mid-run sets a flag that `gui_do_stuff` turns into
`debugger(DBG_USER, 0)` on the emulation thread. This mirrors what `emuthread.cpp` does for the GUI's
debugger button, and it is what makes the OS install scriptable: boot, break in, send, continue.

**Headless never wrote flash changes back.** `flash_save_changes` had no caller outside the Qt front
end, so anything installed in a headless run was discarded when the process ended. There is now a
`flashsave` debugger command. `ln os <file>` was added alongside it.

**The debugger could not press a key.** Neither memory primitive reaches the keypad: `wf` guards on
`phys_mem_ptr` and refuses anything outside RAM, at core/debug.cpp line 487, and while `pw` does
call `mmio_write_word`, `keypad_write` has no case for the data registers at offsets 0x10 to 0x2C
so they fall to `bad_write_word`, at core/keypad.cpp lines 64 to 88. Even a successful write would
be overwritten, because `keypad_scan_event` refreshes those registers from `key_map` on every scan,
and `key_map` lives in the emulator's own memory where no guest write can reach it.

There is also a `--gdb <port>` option. `core/gdbstub.c` was always compiled into the headless build,
but `main.cpp` called `emu_start(0, 0, snapshot)` and port 0 disables the stub. With an OS installed
the pair is a real debugger for on-calculator code:

```
./firebird-src/headless/firebird-headless --boot1 images/boot1.img --flash images/nspire-try6.img --gdb 3333
arm-none-eabi-gdb -ex "target remote :3333" khi-src/src/luagiac.luax.elf
```

That gdb is the toolchain's own, 16.2, so one toolchain compiles and debugs.

There is now a `key` command. `keypad_tap_key(row, col, scans)` in core/keypad.cpp presses a key and
auto-releases it after N full keypad sweeps, riding the existing SCHED_KEYPAD event rather than
adding a scheduler slot, with the countdown state kept outside `keypad_state` so the snapshot layout
is unchanged. core/debug.cpp carries the command and a name table built from the `keymap.h` enum, so
the ids cannot drift from what the Qt keypad bridge uses. 70 names: esc, menu, doc, tab, pad, on,
ret, enter, del, ctrl, shift, the digits, the letters and the operators.

Verified against a control rather than by reading. With `key esc +` held the boot ROM prints
`Boot option: Normal (1 keys pressed)`; on a fresh boot with no key it prints `Boot option: Normal`.

Also worth knowing: the old input path used a 40 byte buffer, so any debugger command longer than 39
characters was truncated. The path to the OS file is 42. The reader now uses 256.

### Driving it from a script

tools/emu-drive.sh takes a flash image, a step file and a log path. A step line is either a debugger
command or `wait <seconds>`, which lets the emulator run before the next command.

**`--debug-on-start` used to be mandatory. It is not any more.** Headless spawned its stdin reader
lazily, the first time the debugger asked for a line. A session started without the flag never
entered the debugger, so nothing ever read the pipe, every command was dropped in silence, and the
run looked like a clean boot that ignored you. `main()` now calls `input_reader_start()` before
`emu_loop`, so stdin is read for the life of the process whatever flags were passed.

The symptom is worth recognising, because it is invisible from the outside: commands return empty,
the emulator sits at 100% CPU, and nothing is obviously wrong. `sample <pid>` settles it in one
call. One thread means the reader is missing, two means it is there.

### It is not the keypad: the touchpad and ON are inert too. 2026-09-02

Retested on the finished 6.4.0 image, at the Document Received dialog with Open focused, so a single
activation would have been visible. All three are screen-identical before and after, compared byte
for byte on the framebuffer rather than by eye:

- `key ret +` held across seconds of guest time, then released. Duration is not the variable, which
  rules out the debounce theory the tap length was raised for.
- `touch 0.5 0.5` pressed and released, a click in the middle of the pad.
- `key on`, which goes through the PMU rather than the keypad matrix.

Two of those are separate peripherals, so two independently broken device models is the unlikely
explanation and a common gate downstream is the likely one. `enter` and `ret` are also not
interchangeable: keymap.h index 1 (`enter`) does not appear in qml/Keypad.qml at all, and index 0
(`ret`) is the key this keypad actually has. Both were tried.

What has not been checked, and should be first next time, is whether the Qt GUI keyboard drives this
same image. That premise has been carried through several rounds of this investigation without being
re-measured, and everything above is framed as "headless-specific" on the strength of it.

### The OS ignores emulated keys, and the keypad is not why. 2026-09-02

At the first-boot "Choose Language" dialog, no key does anything: `tab`, which the dialog itself
names, and `enter` both leave the screen byte-identical over five seconds. The keypad emulation is
not the cause, and each step below is a reading rather than an inference.

- **The press reaches the hardware model.** Row 6 of the data register at 0x900E001C goes 00000000
  to 00000200 for `tab`. Bit 9 of row 6 is exactly `tab` in keymap.h, index 75.
- **Scanning is live and the polarity is right.** control reads 5b680c33, so repeat-scan and
  scan-enable are both set, and size reads 00ff0b08 for 8 rows by 11 columns. An idle row reads
  0x0000 rather than 0xF800, which means `emulate_cx` is true and the CX inversion at keypad.cpp
  line 111 is being applied, so pressed reads as 1.
- **The OS does read it.** A histogram in `keypad_read` counts 10778 reads of 0x1C, continuing long
  after locale init. It reads all four data registers plus 0x08 and 0x0C.
- **The keypad interrupt is enabled at the keypad and masked at the controller.** int_enable
  (0x900E000C) is 2, and int_active (0x900E0008) goes 1 to 3 on the press, so the data-change
  interrupt does fire. But intr.mask[0], readable at 0xDC000010, is 00008000: bit 15 INT_POWER
  only, never bit 16 INT_KEYPAD.
- **Unmasking it changes nothing.** `pw DC000010 10000` makes mask[0] read 00018000, and the
  screen is still byte-identical after a 400 sweep press. So the masked interrupt is not the cause.
- **The OS is a real keypad driver, so the reads are not some other subsystem.** It writes
  0x900E0008 <- 7 5206 times, which is acknowledging the interrupt. It also toggles scanning at
  control, ffff3ffc to clear bits 0 and 1 and ffff3fff to set them. Only 2 of those writes happen
  after locale init though: once the dialog is up the OS keeps reading and almost stops writing,
  and the dominant late access is a tight read loop on 0x0C.

  A first pass here recorded "nothing ever acknowledges int_active" on the strength of it reading 3
  four seconds after the press. That was wrong. The driver clears it constantly and the next scan
  re-sets bit 0, so any single read finding it set says nothing at all.
- **The debug command is not a special path.** core/debug.cpp calls the same `keypad_set_key` that
  qtkeypadbridge.cpp line 17 and qmlbridge.cpp line 264 call, so a `key` command is a GUI keypress.
- **The screenshot is live, so this is not a stale framebuffer.** Captures at 8s and 16s of boot
  differ; 16s, 26s and 40s are identical because the dialog is simply up and static by then.

So the emulated matrix holds the key, a real OS keypad driver reads the register that holds it, the
interrupt is not the blocker, and the OS still does not act. Nothing cheap is left to measure from
this side.

The open question is whether a mouse click on the Qt GUI's own keypad dismisses this same dialog.
That is one experiment and it splits the remaining space cleanly. If the GUI cannot either, this is
a Firebird CX II gap rather than anything about the `key` command, since core/debug.cpp calls the
same `keypad_set_key` the GUI calls. keypad.cpp line 33 already carries an upstream "the CX II may
have an enable bit somewhere" note, which is a gap of the same shape.

The workaround either way is to complete first-boot setup once in the GUI and `flashsave`, so the
image boots to a usable OS and this stops blocking GDB.

The default tap is now 120 sweeps rather than 4. APB is 99 MHz and a sweep is
8 rows * 776 + 23400 APB cycles, so a sweep is about 0.3 ms and 120 is a 36 ms press. The old
4-sweep default was 1.2 ms, far under any debounce, though raising it did not change the outcome
above.

## Installing the OS into the flash image. Works, 2026-09-01

Use tools/os-install-attempt.sh rather than a one-liner, because the timings are the whole trick:

```
cp images/nspire.img images/nspire-fresh.img
FLASH=images/nspire-fresh.img bash tools/os-install-attempt.sh
```

**`ln os` has to land inside a window the boot loader closes on its own.** It enables USB device
interrupts, prints "USB Download is enabled", then its own auto power down clears them again within
moments. Sending `ln os` at 34 seconds misses it and nothing whatsoever transfers. At 20 seconds it
lands: 14439 packets from the calculator, 14441 to it, and the OS installs. See open-questions.md
for the `devctrl` trace.

The script also feeds `c` every 3 seconds across the transfer. Hackspire's Emulators page says a
CX II install can drop into the debugger needing a manual continue, and a line arriving mid-run is
a harmless break-then-continue when nothing is wrong.

BOOT_WAIT, INSTALL_WINDOW, TICK and FLASH are all overridable. images/nspire-try6.img is the first
image produced this way and boots 6.4.0.74.

**Read these logs with `grep -a`.** They carry ANSI escapes and binary bytes, so plain grep decides
they are binary and prints nothing rather than a count. That silently produced three wrong
conclusions in one session, each of which looked like a clean measurement.

## Talking to the physical calculator

`tools/nsptool` is a CLI over libnspire, exposed as the `device` tool in the nspire MCP server. It
needs root only to detach IOKit's driver from the USB interface, so it is installed root-owned at
`/usr/local/bin/nsptool` with a NOPASSWD sudoers rule and drops back to the invoking user before it
looks at any argument. Actions: info, ls, get, put, rm, mkdir, screenshot.

Two fields of `info` mean something different on the CX II, measured 2026-09-03 by dumping the raw
0x4020 reply and decoding it by hand against libnspire's struct, which lines up field for field.
The run level comes back 4, where the old CX said 2 for the OS and 1 for recovery, so libnspire's
enum now names 4 as the CX II OS value and `info` prints it as os. The recovery value on a CX II
has not been seen. The battery byte is 0xff, unknown, with charging 0, on a calculator plugged in
and running, so the legacy battery field carries nothing on this model and `info` says so with the
raw value beside it. Hackspire documents low as 0x01 where libnspire's enum says 0xF1, and neither
can be checked on a device that answers 0xff.

libnspire itself had three defects on the transfer path, fixed 2026-09-03 in libnspire-src/src and
the static library rebuilt. `nspire_dirlist` wrote the entry count through the malloc result before
testing it for null, and an error part way through an enumeration freed the list while leaving the
caller's pointer on it, so the caller's free on the way out would have been a double free. It now
frees and nulls on every error after allocation. `nspire_file_read` took each packet's length minus
one for the type byte and copied that many bytes into the caller's buffer, so an empty packet
wrapped the count and a file longer than the stat it was sized from wrote past the buffer. It now
refuses an empty packet and clamps the copy to the space left, counting the rest. `data_scan`
returned success when the buffer ran out before the format string did, leaving the later outputs
unset, and now refuses. Two more in the CX II transport: `readPacket` in cx2.cpp summed the
checksum over the byte count of the last bulk transfer rather than the packet's declared length,
which only agrees when the whole packet arrives in one transfer, and usb.c let an interface index
equal to the interface count through. The 16 bit byte swap in endianconv.h was written as a 32 bit
swap of `x << 16`, which shifts the 0x8000 host session id into the sign bit of int on every
packet, undefined behaviour that UBSan stopped on at once. It is `__builtin_bswap16` now. After
the rebuild ls, get, put, rm, screenshot, info and a key all ran against the handheld, with get
returning bytes identical to the host build of the module it fetched. The same set then ran from
a build of nsptool and every libnspire source under ASan and UBSan, against the calculator, and
reported nothing. That build is how to check a transport change: compile the sources together
with the sanitizer flags rather than linking the .a, and run the real actions.

Still open in cx2.cpp: the ack loop in `packet_send_cx2` hands every packet to `handlePacket`
with no stream buffer, so a data packet that arrived before the ack would be consumed and lost.
Not seen on this calculator, which acks first, and not changed.

## Ndless named the wrong OS function as lua_toboolean, 2026-09-03

The 2026-09-02 measurement that a Lua `true` read as false through `lua_toboolean` in a .luax on
6.2.0.333 had a cause in Ndless, not in the OS. In every CX II table under
ndless-src/ndless/src/tools/MakeSyscalls/idc the name sat on an address about 0xC700 bytes past
`lua_tolstring`, while the other lapi.c conversions sit within 0xD0 bytes of each other. On the
emulator's 6.4.0.74 the named address, 0x108bd6c8, disassembles to a function that loads the first
byte of its third argument and jumps through a 0x29 entry table on it, a string switch. The real
function is at 0x108afc0c, found by dumping 2 MB of OS text with `wm` and scanning for a call to
index2adr (0x108af7c4) followed by a load of the type tag, a compare with 0 and a compare with 1,
which is the whole body of lua_toboolean and matched exactly once. It sits 0x20 past
`lua_isuserdata` and 0x34 before `lua_tocfunction`, and that spacing is identical in all six CX II
tables (cascx2, ncascx2, ncascx2t, at 6.2.0.333 and 6.4.0.74), so the same slot was named in each.
The older CX tables share the shape and were left alone, since no OS is here to check them on.
The other Lua calls src/lua_module.cc uses were disassembled the same way on 6.4.0.74 and match
their Lua 5.1 bodies: lua_atpanic swaps the panic slot, lua_gettop divides the stack span by 16,
lua_settop, lua_pushnil, lua_pushboolean and lua_pushinteger write tags 0, 1 and 3,
lua_checkstack compares against 8000 and lua_gc switches on eight options. Only lua_toboolean
was wrong.

Proof is probe/boolprobe: an 18 KB module whose one function returns `lua_type` and
`lua_toboolean` of its argument, and a document that calls it with true, false, nil, a number and
a string. With the old table the emulator showed true 0, false 1, nil 0 bool 1, number 1, string 0
(probe/s52.png) and the handheld showed the same pattern (probe/hw12.png). With the rebuilt
resources on the emulator image and a reboot it shows true 1, false 0, nil 0, number 1, string 1
(probe/s53.png). The handheld, restarted by hand on the rebuilt resources the same day, shows the
same right answers on 6.2.0.333 (probe/hw13.png), which also confirms the slot inferred for that
table from the spacing.

The same day libndls config.c gained the check its own MAX_CFG_FILE_SIZE was for: the parser
stores 16 bit offsets, so a config past 10000 bytes was read through wrapped indices. Refused
now, with probe/config_host as the host harness for that file, built the way
probe/file_each_host is. Resources rebuilt again after it (196588 bytes) and put on both targets;
the handheld runs it from its next restart.

Two things about installing resources over the emulator link: `ln s` of a file named
ndless_resources.tns fails part way with "Link transfer failed" even after `ln rm` of the old one,
while the same bytes as resx.tns go over, and the `ln mv` to the real name then prints "Link
transfer failed" while actually renaming. Fetch the file back and cmp it rather than trusting
either line. And a fetch attempted while the Document Received dialog from the send is still on
screen fails with no status line at all; `ln svc 0x4B45 1B 96 00 00` (esc) first, then the same
fetch succeeds. A path that really is absent fails with "status 0a", which is how to tell the two
apart.

The calculator leaves the USB bus entirely while PolyDumper or eMMUlate is running, so a call during
either fails rather than waiting.

**The NNSE protocol has no input service of its own**, see open-questions.md. nsptool's key, type
and keys commands only work once keysvc is resident on the calculator. The startup folder does that at
every boot, proven on hardware 2026-09-02. Without it the way round is the Ndless startup folder, which runs every .tns under
/documents/ndless/startup at Ndless load with no keypress. Verified: probe/heap.c placed there
reported `startup=1` after a reboot. With persistency installed this fires on every boot, which
gives a full hands-off loop on real hardware:

```
build -> device put <prog>.tns /ndless/startup/ -> reboot -> device get /ndless/<result>.tns
```

Two constraints on what goes in that folder. A program that faults there is a reboot loop, so
anything probing stack depth or doing unbounded recursion belongs in a hand-launched program
instead. And a startup program cannot reach Giac, because nl_lua_getstate is NULL outside a running
Lua interpreter.

`screenshot` writes a PPM. `sips -s format png x.ppm --out x.png` makes it readable.

## Practical consequence for how we work

Two loops, and it is worth being deliberate about which one a task belongs in.

1. **Host loop, available today.** The derivation kernel, rule engine, unit algebra, verifier and
   corpus tests are ordinary C++ that compiles for the Mac. PRD section 5.2 already asks for a
   host-side test runner so most logic can be tested without deploying to a calculator. This needs
   no emulator, no toolchain and no hardware, and it is where the 720-case MVP acceptance corpus
   should live.
2. **Device loop, blocked on the dumps.** Anything about memory ceilings, latency, the Giac bridge,
   rendering and keypad handling. PRD PERF-011 insists these be measured in the real release
   configuration, and an emulator does not settle a memory or latency budget anyway. The emulator
   makes this loop fast and hands-off, but the numbers that qualify a release still come from
   physical hardware per requirement 19.6.

The honest split: the emulator buys iteration speed and lets an agent work alone. It does not
replace the on-device qualification the PRD requires.

## The GUI keyboard works, so the input bug is headless-only. 2026-09-02

Measured on the same nspire-os.img the headless runs use, with the kit pointing at the same boot1
and flash (checked in the QSettings kits blob). Down moved the home screen highlight from Calculate
to Graph, and Return opened the Graph scratchpad. Return maps to `keymap::enter` in
qtkeypadbridge.cpp:156, which is the same enum the debugger's `key enter` uses, and `setKeypad`
calls `keypad_set_key` exactly as debug.cpp does. So the two paths are the same code with different
outcomes, and the premise carried through earlier rounds is confirmed rather than assumed.

One genuine difference found while looking: the arrow keys do not go through the keypad matrix at
all. qtkeypadbridge.cpp:213 moves the touchpad and then raises `gpio_int_active |= 0x800` before
calling `keypad_int_check`. The `touch` debugger command added here sets the touchpad position but
not that interrupt bit, which is why it did nothing.

## persistent.tns is an Ndless program, not the installer. 2026-09-02

This cost several rounds. Opening `persistent.tns` without Ndless already running just displays a
blank document, because it needs the program loader hook Ndless installs. The bootstrap that works
from nothing is `ndless_installer_4.5.5-6.2.0-6.4.0.tns`, which draws its own green installer screen
and asks for a keypress.

The working order, all in the Qt GUI: run the installer, then run `persistent`, then Flash > Save.
Quitting the GUI does NOT write flash, so a bootstrap without that explicit save is lost. After it,
a cold headless boot ends with "Setting theDoc.filename to NULL in Execution Context", which earlier
boots never printed, and `exec` stopped resetting.

## Why the debugger's `exec` resets the calculator. 2026-09-02

Not for the reason its old error message claimed, and not because Ndless is missing. Ndless is
resident and the syscall reaches it: an execute breakpoint on the SWI vector's target, 0x13A1BA40,
is hit every time.

Svc mode is required rather than wrong. `ints_swi_handler` says so itself: "only supports calls from
the svc mode (i.e. the mode used by the OS)" and "destroys the caller's mode lr" (ints.c:47). An
earlier guess that the privileged mode was the fault was wrong, and the guard written for it is now
only a warning.

With `--debug-on-warn` the actual fault is visible instead of unwinding into a reset:

    Warning (10490168): Data abort: address=fffffffc status=05 instruction at 10490168

The faulting code is a three instruction accessor in the OS:

    10490160: cmp   r0,#4
    10490164: mvneq r0,#0
    10490168: ldrne r0,[r0 - 4]

r0 is 0, so it reads 0xFFFFFFFC. Following the return addresses names every step, and the null does
not come from Ndless at all:

    10491bf8  open                      entered with r0 = the path, saved to r5, r0 untouched
    10491c0c  bl 1048f814               lr = 0x10491c10, the lr seen at the fault
    1048f814  stmdb sp!,{r4,lr}
    1048f818  bl 10428b38               TCC_Current_Task_Pointer, returns r0
    1048f820  b  10490160               tail branch, so r0 is that return value
    10490160  the accessor above, reading [task - 4]

Both names come from Ndless's own symbol file for this exact OS,
ndless/src/tools/MakeSyscalls/idc/OS_ncascx2-6.4.0.74.idc:470. So `open` looks up the current
Nucleus task to reach its file table, and `TCC_Current_Task_Pointer()` returns NULL.

It returns NULL because there is no current task. Every register dump taken at an arbitrary break
shows pc at 0xA400xxxx with sp at 0xA4001xxx, in SRAM rather than in the OS image, svc mode with
interrupts off. That is the Nucleus idle loop, between tasks. `armloader_load_snippet` takes over
whichever context is current by pushing the snippet onto its stack and pointing pc at it
(firebird-src/core/armsnippets_loader.c:56), so `exec` from the debugger almost always runs there.

The full path is `swi e_nl_exec` -> `sc_ext_table[10]` = `sc_nl_exec` (syscalls.c:299) ->
`ld_exec_with_args` -> `nuc_fopen(prgm_path, "rb")` (ploaderhook.c:286) -> the OS `open` above.
`expand_stack()` runs first and was the earlier suspect, but it is not where this dies. It returns
immediately unless `tct_current_thread_addrs[ut_os_version_index]` is set, and a breakpoint on the
OS malloc it would otherwise call never fired.

So `exec` runs its snippet from the wrong place rather than being broken. Nothing depends on fixing
it, because the startup folder below is a working way to run code, but the fix would be to reach a
real task before running the snippet rather than using whatever context the debugger caught.

### Running the snippet from a real task instead

Confirmed both directions rather than argued. `TCC_Current_Task_Pointer` is four instructions:

    10428b30  ldr r0,[11430858]     the global holding the current task
    10428b34  ldr r0,[r0]
    10428b38  cmp r0,#0
    10428b40  beq  ...              return NULL when nothing is scheduled

Breaking there in the idle loop gives 0. To catch a real task instead, set the breakpoint and then
make the calculator do something, since nothing calls it while idle:

    k 10428b38 +x
    ln /tmp/ndtest/paint.tns /ndless
    c

The link wakes the OS task named CN_READ, the breakpoint fires, and stepping through shows r0 =
0x113C5B2C with sp = 0x113C6AC0 in SDRAM rather than 0xA4001xxx in SRAM. `exec` from there does not
produce the data abort at all, which settles the mechanism above.

It dies later and differently:

    Warning (1042af50): Undefined instruction at 1042af50

0x1042AF50 is inside `TCT_Check_Stack` (idc:481), reached from `TCT_Terminate_Task` (idc:473), so the
task is already being torn down by the time this faults. The task control block says why that is
likely: at +0x24 and +0x28 CN_READ's stack runs 0x113C5BD4 to 0x113C6BC8, which is 4 KB, and only
0xED4 of it was still free at the point the snippet runs. `ld_exec_with_args` puts three FILENAME_MAX
buffers on the stack before doing anything (255 each on this toolchain, sys/config.h:242) and the OS
`open` alone reserves 0x108. The termination is consistent with a stack overflow, though the trigger
itself was not observed.

So any fix for `exec` has to pick a task with room, not merely a task. A 4 KB service stack is not
somewhere a program loader can run.

### The task list, and which one Ndless wants

Nucleus task control blocks carry the magic "KSAT" at +0xC, the name at +0x10, and stack_start,
stack_end, saved sp and stack size at +0x24, +0x28, +0x2C and +0x30. That is enough to enumerate
them from a RAM dump:

    wm /tmp/ram.bin 11000000 1000000

39 tasks on this image. Most are 4 KB to 16 KB service stacks. The one that matters is `gui`:

    task 114457b4 gui  stack_end 1800fdf8  size fe00  sp 1800fa58

Its stack is therefore 0x17FFFFF8 to 0x1800FDF8, in the same 1 MB section that `expand_stack()`
maps a 128 KB coarse page table into (0x17F00000, ploaderhook.c:213). That is not a coincidence:
`gui` is the task the boot-time startup hook runs on, and it is the one Ndless prepared.

`gui` is also the only task whose stack_start at +0x24 does not agree with stack_end minus size. It
reads 0x13A48000, an SDRAM heap address rather than anything on its stack. That is what
`expand_stack()` writes (`myself->stack_start = new_stack_aligned`, ploaderhook.c:228), so the
one-shot `already_done` expansion had already happened during boot, on this task. Earlier I read
CN_READ's untouched stack_start as evidence that `expand_stack()` never ran anywhere, which was the
wrong inference from the right observation.

### The fix: exec waits for that task instead of running here

`exec` no longer runs its snippet in whatever the debugger caught. It queues the path and the
emulation loop runs it the moment the guest is on the loader task, tested in
`armloader_ndless_context()` (firebird-src/core/armsnippets_loader.c):

- svc mode, since `ints_swi_handler` only supports svc callers
- interrupts unmasked, which rules out the idle loop and the exception handlers
- sp inside 0x17F00000 to 0x18010000, the 1 MB section holding the `gui` stack and the region
  `expand_stack()` maps

`armloader_poll_deferred()` runs from `emu_loop` next to `sched_process_pending_events()`
(firebird-src/core/emu.cpp). `exec -` cancels a queued run.

What it does now, on a booted image:

    > exec /documents/ndless/paint20.tns
    exec: this is not the task Ndless loads programs from (sp a4001d5c), so ... is queued ...
    exec: reached the Ndless program task at pc 100653d0 sp 1800e460, running ...

Every run since has landed with sp in the `gui` stack, and the calculator has not reset once,
where before it reset every time. Ndless now answers for itself: a path that does not exist comes
back as its own `ld_exec: couldn't fopen file!`, and a program built without `--240x320-support`
brings up Ndless's compatibility-mode dialog, which only appears once `ld_exec` has loaded the
program. After running a program the home screen is intact.

The whole loop is proven end to end: build, deploy, exec, and then read the program's own output
back off the calculator.

    fetched /ndless/exec-out.txt.tns -> exec-out.txt (30 bytes)
    deferred exec ran the program

One limit worth knowing. A queued run waits while the `gui` task is parked in a modal dialog, such
as the "Document Received" one a deploy raises, and `dbg key` cannot dismiss it because the
debugger key path is separately unreliable.

### The debugger key path is unreliable, not the emulator, still open, 2026-09-02

`dbg key` reaches the OS correctly and the OS mostly ignores it. Traced the whole path with
temporary prints in keypad.cpp, since removed.

A tap of `5` sets key_map, and the next scan sees the change and raises the data-change interrupt:

    tap r5 c6 control=5b680c33 size=00ff0b08 int_active=00000001 int_enable=00000002
    sweep 1: key_map[5]=0040 data[5]=0040 int_active=00000003 left=120
    read 18 -> 00400000
    sweep 2: key_map[5]=0040 data[5]=0040 int_active=00000001 left=119

So within one sweep, about 0.3 ms of guest time, the OS read the data register holding the pressed
bit for row 5 and cleared int_active bit 1. That is a healthy driver servicing an interrupt. Nothing
appears on screen.

Ruled out by measurement, so do not re-suspect these:

- press duration, from 120 sweeps to 4000 (roughly 36 ms to 1.2 s), and an indefinite hold
- the key name table, which agrees with keymap.h, and the row and column arithmetic
- scan configuration: size 0x00ff0b08 is 8 rows by 11 columns, control bit 0 set, so it repeats
- polarity: `emulate_cx` is `product >= 0x0F0` (emu.h:52) so it is true on CX II, and the inversion
  giving 1-means-pressed is intentional
- the touchpad as a separate path: `touchpad_set_state` already raises `gpio_int_active |= 0x800`
  (keypad.cpp:520), contradicting an earlier note here that said `touch` omitted it. A touchpad
  click is ignored too.
- the headless front end differing from Qt: both reach `keypad_set_key` the same way, and Qt's
  `gui_do_stuff` does nothing keypad related

One press did work: `key 5 +` held on the home screen opened the Settings menu, confirmed by
screenshot. No other press has, including the same key as a tap, holds of `esc`, and both a tap and
a hold of `1` inside that menu. So this is closer to unreliable than to dead, which is a different
shape of bug from the one I went looking for.

Two traps that cost time here and will again:

- After the debugger breaks in, only `c` resumes the guest. A batch like `key 5; r; r` reports the
  same registers three times because nothing ran between them, which reads exactly like a hung
  emulator.
- Comparing consecutive screenshots byte for byte says nothing unless you also look at one. Two
  identical shots of an already-changed screen say "unchanged", and that is how I missed the
  Settings menu opening for several rounds.

The duration test was first run with `esc`, a key whose effect in that context was never
established, which is not a test. Repeated with a positive control: `key 5 4000` on the home screen,
where a held `5` is known to open the Settings menu. The screen did not change. So duration really
is not the variable.

The measurement that would settle it is the Qt GUI on this same flash image. If the GUI is also
ignored, nothing in `key` is at fault and the problem is the OS state on this image. Note the GUI
section further up recorded the GUI working, but a GUI press is held for as long as the user holds
it, so it is a hold rather than a tap, and the one headless press that worked was also a hold.

### Two session hazards worth knowing, 2026-09-02

**Reloading the MCP drops the running session.** `nspire reload` re-imports the module, which loses
the global holding the emulator handle, and the next call says no session has been started. The
emulator process survives as an orphan and is reaped on the next start. Do not reload while an
emulator is running unless you are willing to reboot it.

**The "Document Received" dialog blocks the link, not just exec.** A deploy raises a modal dialog on
the gui task, and while it is up a following `fetch` can stall until the queue's deadline. It is the
same task the deferred exec waits for. `flashsave` then a restart is the reliable way out, since
`dbg key` cannot dismiss it.

**The link degrades over a long session.** Transfers that worked earlier start failing, and the file
size is a red herring: the falsifying test is one deploy of a file already known to transfer. A
fresh boot fixes it.

### The link and the OS disagree about where documents live, 2026-09-02

I spent a while believing `ln g` was broken, because a file that had just been sent successfully
would not come back. It was the path. The link's namespace is rooted at the documents folder, so the
file `ln s` puts at `/ndless/exectest.tns` is the same file Ndless and `exec` call
`/documents/ndless/exectest.tns`. Asking the link for the OS spelling gets a refusal.

Two fixes, because the wrong path should not have been this hard to see.

`get_file_next` ignored the calculator's refusal entirely (firebird-src/core/usblink.c). The
calculator answers a get it cannot serve with `FF <status>`, the switch had no case for it, and the
transfer sat there until `usblink_queue`'s own 20 second deadline gave up with nothing to say. It
now closes the file, removes the empty one it had already created on the host, and fails through the
callback straight away:

    File receive error: the calculator refused the path, status 0a

The empty file mattered: a zero byte result on disk reads as a fetch that worked.

The `fetch` tool now accepts either spelling and strips a leading `/documents` itself
(`_link_path`, nspire.py), because the caller should not have to know which side of the wire they
are on. Its old failure text asserted "A missing file looks like this", which was the misleading
half of a guess; the emulator now supplies the calculator's actual reason.

## Ndless does load at boot. 2026-09-02

It always did. Three separate faults made it look otherwise, and each one produced the same
symptom, so they masked each other.

The proof is direct rather than inferred. `HOOK_INSTALL` writes 0xE51FF004 at the address it patches
(ndless-sdk/include/hook.h:13), and 0x100280CC reads exactly that, with 0x100280D0 holding
0x122F9674, a pointer into Ndless's resident RAM. The SWI vector at 0x10000028 reads 0x13A1BA40,
outside the OS image, so Ndless has hooked it too. Both are true on every cold boot tested.

The reset vector at 0x10000020 is 0x10429EC0, which is the 6.4.0.74 CX II entry in stage0.S's OS
table, so the loader recognises this OS. Booting a copy of the image and counting signatures
afterwards shows a fresh `currentdoc.data` and a fresh copy of the loader written during the boot,
which is what `persistency_install` does when it re-arms itself.

What actually went wrong:

- The output reader was dead, so `exec` printing `Reset` was read as `exec` printing nothing. See
  the plumbing section below.
- `exec` resets for its own reason, unrelated to Ndless. Its snippet is entered (a breakpoint on
  0x13A1BA40 is hit, so the swi dispatches correctly even from svc mode) and the calculator dies
  after that, inside Ndless's exec path. Do not read a reset from `exec` as "Ndless is missing";
  check the hook word instead.
- Test programs blocked on a modal Ndless dialog. genzehn warns "Your application does not appear to
  support 240x320px displays", and a program built without `--240x320-support true` stops at
  "Activating compatibility mode" waiting for OK, which needs a keypress this fork cannot deliver.
  A program that looked like it never ran was sitting on that dialog.

### Running code without keys or exec

`plh_startup` runs every document in `ndless/startup` at boot, unless ESC is held
(ploaderhook.c:484). That is the working execution path here, and it needs neither the keyboard nor
`exec`:

1. `ln md /ndless/startup` once.
2. `deploy` the program with `target=/ndless/startup`, built with `--240x320-support true`.
3. `flashsave`, then restart.
4. The program runs during boot. `fetch` whatever it wrote.

Verified end to end: a program compiled on the host wrote "ndless is loaded" to
/documents/ndless/hello-out.txt.tns during boot, and fetch brought back those 17 bytes.

`ln rm <path>` was added for the case this creates, which is a startup program that has to be taken
out again. A blocking one runs on every boot and the folder is read in order, so it stops everything
behind it.

## The output plumbing, fixed. 2026-09-02

The cause was one line of decoding. `Session.start` opened the emulator with `text=True`, which
decodes stdout as strict UTF-8. The debugger's `d` prints the raw bytes of memory in its ASCII
column, and the OS emits non-UTF-8 bytes of its own, so sooner or later a byte like 0x80 raised
UnicodeDecodeError inside the reader thread. The thread died, nothing else was ever queued, and the
session went blind for the rest of its life: commands still executed, files they wrote still
appeared on disk, and every call returned empty.

Isolated by driving the emulator through a plain FIFO with no Python in the way
(/tmp/ndtest/rawtest.sh). Through the raw pipe `ln ?`, a full `d` dump and `screenshot` all printed
normally, which put the fault on the driver side rather than the emulator. Decoding that same log
with strict UTF-8 reproduces the exception at the first byte of the dump's ASCII column.

Three changes, all needed:

- `encoding="utf-8", errors="replace"` on the Popen, so a byte that is not text cannot raise.
- `_pump` catches, records `_pump_error` and puts a visible line in the stream. A reader that stops
  without saying so is indistinguishable from an emulator that stopped talking.
- `emu action=status` reports a dead reader and says outright that any deploy or fetch which
  reported no transfer since then is unproven.

The framing was a guess as well, so that is gone too. headless/main.cpp prints `<<fb-ready>>`
whenever the debugger is about to wait for a command, and `read_until_ready` reads up to it instead
of waiting for a gap in the output. A command that resumes the guest never returns to a prompt, so
those still fall back on the quiet gap. `send` drains anything queued before writing, so a prompt
printed earlier cannot end the next command's read, and reports it separately when it happens.

**This had been corrupting conclusions, not just output.** Several `exec` calls were read as "did not
reset, so Ndless is loaded". They printed nothing because the reader was dead. Repeated with the
fix, the same call prints `Reset`: Ndless is not loaded from persistence, and the earlier reading
was backwards.

## Headless loses debugger output intermittently, 2026-09-02

The symptom is that a command produces no output while plainly having run. `screenshot <file>` is
the discriminator, because it writes a file rather than printing: the file appears, so the command
executed and only its printed output was lost. `deploy` hit this too, reporting no transfer while
the calculator was showing the Document Received dialog for the file it had just been sent.

This is unfinished. `setvbuf(stdout, nullptr, _IOLBF, BUFSIZ)` in `main` is a partial fix and the
only flush before it was the one in `gui_debugger_request_input`, which never runs for a command
that resumes execution. Passing size 0 there instead of BUFSIZ is invalid and broke stdout
completely for one build, which is worth knowing because it looks identical to the intermittent bug.
Treat any "no transfer happened" from deploy or fetch as unproven until a screenshot confirms it.

## New headless debugger commands, 2026-09-02

- `ln ?` reports whether the link is connected and what the target folder is. Without it a live link
  with a stuck transfer at the head of the queue is indistinguishable from a link that never came
  up, and both of the MCP's error messages used to assert the wrong one of those.
- `ln mv <old> <new>` exposes `usblink_queue_move`. This OS refuses it, even for a `.tns` to `.tns`
  rename, so it is a dead end here rather than a working tool.
- `touch [x] [y] [+|-]` drives the touchpad, coordinates 0..1, defaulting to a click in the middle.
- `savestate <file>` calls `emu_suspend`. `flashsave` keeps the filesystem but not the running OS,
  and Ndless only exists in RAM, so a snapshot is the only way to carry a loaded Ndless forward.

The queue also had no timeout. Any request the calculator never answered, a GET for a missing file
being the easy one to hit, left `busy` set for the rest of the session and silently dropped every
later transfer. `usblink_queue_do` now gives the front of the queue 20 seconds, refreshed on
progress, and fails it through the normal callback. `usblink_abandon_transfer` clears the half
finished transfer without clearing `usblink_connected`, which on the CX II cannot be recovered
without another boot.

## Why Ndless loaded on one boot and never again, 2026-09-02

Ndless persistence re-arms itself on every boot. `currentdoc.tns` is run and then deleted by the OS,
and `persistency_install()` (install.c:175, reached because the loader passes 'P') copies it back.
The copy source is `/documents/ndless/persistent.tns`, with `/documents/persistent.tns` as a
fallback. When neither exists the function unlinks `currentdoc.data` and returns (persistency.c:170),
so persistence survives exactly one boot and then disappears.

Measured, not inferred: with the link up, `fetch /ndless/persistent.tns` returned nothing, and
`ln ?` confirmed the link was still connected at that moment. The distributed file is named
`persistent_6.2.0-6.4.0.tns`, so an install that copies it across under its own name leaves the
re-arm looking for a file that is not there.

The arming step still cannot be done over USB. The OS link service refuses a non-`.tns` upload
(`File send error: Didn't get 04`), and `currentdoc.data` is not a `.tns`. It has to come from
`persistency_install` running on the calculator, which means launching the loader once.

## The MCP against the emulator, 2026-09-02

Measured by running it, not by reading it. `emu` was booting `nspire.img`, the bare image an OS is
installed *into*, hardcoded at nspire.py line 48. It reached "Waiting for OS download" and sat
there, so `deploy` and `exec` could never work: both go through usblink, and usblink_queue_do
returns immediately unless usblink_connected, which only a running OS sets.

Fixed in nspire.py:

- The flash image is chosen by `_flash()`: the emu call's own `flash`, else $NSPIRE_FLASH, else
  `nspire-os.img`. Absolute paths pass through. `sdk` reports whichever one is in play rather than
  the constant.
- `emu` takes `gdb` and passes `--gdb <port>` through to headless, which already supported it.
- Two descriptions were wrong and are corrected. `debug_on_start` was not a convenience: without it
  headless never spawned its stdin reader, so every `dbg` command was discarded in silence rather
  than queued until the next break, which is what the old text claimed. The emulator now starts the
  reader in `main()`, so the flag is back to being a convenience and stdin works either way.
- `dbg` now documents this fork's own commands, `key` and `screenshot`, which were reachable all
  along but undocumented.

images/nspire-try6.img is renamed images/nspire-os.img, since a tool default should not carry the
number of the attempt that happened to work.

**Verified end to end.** `emu start` then three `c` steps boots 6.4.0.74, and
`dbg screenshot ...` writes probe/mcp.ppm showing the OS. So the emulator half of the MCP works up
to the first-boot language dialog.

**Still blocked there.** That dialog is the keyboard problem above, and it is the one thing between
this and `deploy`/`exec`: setup has to complete before connectivity services come up.

### First-boot setup completed and baked in, 2026-09-02

The GUI keypad dismissed the dialogs the emulated OS ignored from the debugger. Tab focused OK,
Return accepted, three screens: Choose Language, Font Size, Welcome. Then Flash > Save reported
"Saved 35 modified blocks" into images/nspire-os.img.

Verified from the other side rather than assumed: headless now boots that image straight to the
Home screen, probe/setup-done.png, with no locale-init pass and no dialog. So the MCP starts on a
usable OS in one `emu start` and three `c` steps.

Setting the kit up again needed the flash path repointed, since renaming try6 to nspire-os.img left
the saved kit pointing at a file that no longer existed and the GUI refused to start. Preferences >
Flash & Boot1 > Flash. The kit list is a binary QVariant blob under `kits` in
org.firebird-emus.firebird-emu.plist, so it cannot be edited with `defaults`.

### The key command still does not work, and turbo is not why

The GUI's own keyboard works on the same build and the same image, so this is not a Firebird CX II
gap. Both front ends call the same `keypad_set_key`, qtkeypadbridge.cpp line 17 against
core/debug.cpp, and the bridge computes row and column exactly as the debug command does.

Ruled out since:

- **Not the dialog.** On the Home screen after setup, `key menu` leaves the screen byte identical.
- **Not tap length.** An explicit `key menu +`, four seconds of running, then `key menu -` is also
  byte identical.
- **Not turbo mode.** headless/main.cpp had `turbo_mode = true` hardcoded while the GUI ran at 83
  to 86 percent, which looked like the answer. There is now a `--realtime` flag. With it the result
  is byte identical again, so the rate is not the cause. The flag is kept because the question will
  come up again, and the comment at the assignment says what it measured.

What has not been tried: reading intr.mask[0] on the Home screen to see whether the OS enables
INT_KEYPAD once setup is done, and diffing the emulator state a GUI keypress produces against the
one a debug keypress produces.

### deploy and exec are still blocked, and it is usblink rather than the OS

With the OS up and idle at Home, `ln c` followed by `ln s` transfers nothing. The evidence is
precise: `ln_progress` at core/debug.cpp line 70 prints "Link transfer complete." at 100 and
"Link transfer failed." on a negative progress, and **neither ever appears**, so the callback is
never invoked and the transfer never starts. That is `usblink_queue_do` returning early at
core/usblink_queue.cpp line 77 because `usblink_connected` is false.

`usblink_connect` only sets usblink_state to 1, core/usblink.c line 769, and the bus reset then runs
from `usblink_timer`. `usblink_connected` is set in one place for CX II, core/usblink_cx2.cpp line
287, on an NNSE TimeService message from the calculator. Waiting 25 seconds between `ln c` and
`ln s` changes nothing, so this is not the tools firing too close together.

So the emulator half of the MCP boots and observes, and cannot yet transfer or run.

### Why the link never came up, and the two things that fix it. 2026-09-02

Traced rather than guessed. USBTRACE was put back at four points: usblink_state transitions,
usb_cx2_receive_setup_packet, usb_cx2_packet_from_calc, and writes to devctrl, otgier and gimr.

The first run said the host does its part and the calculator does nothing:

```
USBTRACE usblink_state=1 / 2 / 3
USBTRACE setup->calc req=5 val=1 gimr0=0000003f
(no calc->host, ever. no nnse, ever)
```

req 5 is SET_ADDRESS. `gimr0=0000003f` is the answer: usb_cx2.cpp line 23 tests
`gisr[i] & ~gimr[i]`, so a 1 in gimr means **masked**, and bit 0 is the EP0 setup flag that
usb_cx2_receive_setup_packet raises. The OS had that interrupt masked, so it never woke to answer.

The write trace showed why it was masked. The OS brings USB up during boot, devctrl to 00000c24 so
device interrupts are gated on, otgier to 00000f21, gimr[2] to 3ff. Then, in the six writes
immediately after the "OS Time" line, it takes it all back down: otgier to 0, devctrl to 00000c20
losing bit 2, and all three gimr back to fully masked. It finds no cable and powers the subsystem
off. Every `ln c` so far had arrived after that.

**Fix one: offer the link during boot, not after.** Sending `ln c` every 2 seconds from 20s gets
`usblink connected.` and a real NNSE handshake, service 01 then 08 then 02 TimeService, which is the
only thing that sets usblink_connected, at usblink_cx2.cpp line 287. The link then stays up.

**Fix two: the remote path is documents-root relative.** With the link up, `/documents/ndless`
failed with "File send error: Didn't get FF 00 or 06 XX", which reads like a transport fault and is
a bad path. `ln st /` gives **Link transfer complete.** The same convention the device tool uses,
where `/` is My Documents.

This is the same shape as the OS install: a window the OS closes on its own, missed by arriving
late. Two independent instances now, so treat "did it arrive inside the window" as the first
question for anything on this link.

MCP changes made for it, **written but not yet verified end to end**: DEVICE_DIR is now `/ndless`,
and `emu` takes `boot` which drives the boot with repeated `ln c` and reports linked or not. That
call blocks for the length of a boot, about 50 seconds, which is too slow to sit in a foreground
tool call and should be reworked before it is relied on.

### The link is attached in the background now

`emu action=start boot=true` returns at once and drives the boot on its own thread, because a boot
is about a minute and holding a tool call open that long is unusable. Poll `emu action=status`,
which reports the session and a `usblink:` line: booting, linked, or why not.

Two things this needed beyond the thread:

- `Session.send` takes a lock. It writes stdin and then drains a shared output queue, so a
  background linker and a foreground `dbg` call running at once would each read the other's output
  and attribute it to their own command.
- `deploy` refuses outright unless the state is linked. Queuing a transfer with the link down is
  silent: `usblink_queue_do` returns early and the progress callback never fires, so there is no
  success line and no failure line either, which reads like the tool did nothing at all.

Two defects surfaced getting that to work, and both are worth knowing because neither announces
itself:

- **`Session.read` had no total cap.** It extends its deadline on every line, so a source that
  never pauses for the quiet gap holds it forever. A booting OS is exactly that, printing solidly
  for most of a minute, so the linker sat inside its first `send` and the state stayed "booting"
  long past its own deadline. `read` and `send` now take a `budget` that caps the whole wait.
- **The USBTRACE instrumentation became the problem.** Once USB is live the OS rewrites gimr
  continuously, so tracing those writes floods stdout, which is the same thing that keeps `read`
  from ever going quiet. A trace that is cheap during the failure being diagnosed can be ruinous
  once the failure is fixed. It has all been removed now that the findings above are written down.

## The emulator is feature complete, 2026-09-02

Everything below was run through the MCP, not asserted.

| Capability | How | State |
|---|---|---|
| start, stop, status | `emu` | works, start returns at once |
| boot with the USB link | `emu boot=true`, background thread | works, `usblink: linked` |
| send a file | `deploy` | works, "Link transfer complete." |
| create a directory | `ln md`, added to Firebird | works, deploy makes its own target |
| fetch a file back | `fetch`, `ln g` added to Firebird | works, round trip byte-identical |
| run a program | `exec` | works, queued until the loader task |
| screenshot | `dbg screenshot <file.ppm>` | works |
| GDB stub | `emu gdb=<port>` | flag wired, not yet attached to |
| press a key | `dbg key` | mostly does NOT work, see above, with one exception below |
| press a key through the OS | `ln svc 0x4B45 <records>` to keysvc | works, see the last section |

The exec row was proven end to end on 2026-09-02: deploy a built .tns, `exec` it, and read the file
the program wrote back off the calculator with `fetch`. See the two exec sections above for what
was wrong with it and what the fix is.

The `dbg key` exception, observed 2026-09-03 and worth knowing because it solves the bootstrap.
`key enter` at the home screen, with the Document Received dialog up and keysvc not yet registered,
opened the document. The same `key enter` against the Document Sent dialog with the Lua document
already running did nothing at all. So it reaches the OS shell and not the running document, which
is enough to open the first document of a session and no more. Why the two differ is not
established, so do not read the one success as the row being wrong.

It solves the bootstrap because the first `ln svc` of a session is unreliable. On this boot it
reported "the calculator refused the service or never answered", and keysvc's own log confirms it
never arrived: the log opens with `keysvc: startup=1`, so the service was registered from
/ndless/startup at boot rather than by the document, and its batch lines account for every later
packet and not that one. So the refusal is the link, not the service. The order that works from a
cold boot: `deploy target=/`, `key enter` to take the Document Received dialog, then everything else
through keysvc.

The same intermittency recurs mid-session. A packet is sometimes echoed with no reply line, and the
failure is only reported on the next debugger command. Resending it works. Fetch keysvc's log
(/documents/ndless/keysvc.txt.tns, one `batch: N bytes, M accepted` line per packet it actually
received) to tell a lost packet from one that arrived and did nothing, rather than guessing from the
screen.

### Sending a document while one is saving resets the calculator, into Press-to-Test. 2026-09-04

Observed once and worth knowing, because the recovery is not obvious. A `ln s` of a document landed
while the open document was showing "Saving...". The transfer reported complete, then the debugger
printed "Setting theDoc.filename to NULL in Execution Context" followed by "Reset" and a full boot
sequence. The boot log said `Boot option: Normal (1 keys pressed)`, and the calculator came up in
**Press-to-Test Mode**: pre-existing content disabled, and Ndless not loading.

The recovery is to stop the emulator and start it again **without** a flashsave. Press-to-Test lives
in RAM and unsaved flash until something writes it down, so a restart from the saved image comes
back normal. Check `d 100280cc` reads 04 F0 1F E5 afterwards to confirm Ndless is resident again.

The cost of getting this wrong is high: flashsave while in Press-to-Test would persist it. So after
an unexplained reset, screenshot before saving anything.

The avoidable half is the trigger. Wait for a "Saving..." indicator to clear before sending the next
file, and dismiss any Document Sent dialog first.

### Ndless is installed in the image now

Same one-time GUI trick as the first-boot setup, because installing Ndless means opening a document
and that needs a keypress. The files went over the link first, `deploy` into /ndless, then in the
GUI: Browse, ndless, the installer, a key to start it, then persistent_6.2.0-6.4.0 so it survives a
reboot, then Flash > Save after each. images/nspire-os.img carries all of it.

### Two ordering rules, both learned by tripping over them

- **The USB link can only be attached while the OS boots**, and it stays up afterwards. That is what
  `emu boot=true` is for.
- **Ndless loads a moment after the OS finishes booting**, from persistence. `exec` before that is a
  `swi` nothing handles: the calculator resets, and the reset also drops the USB link, so deploy and
  fetch stop working until the session is restarted. `exec` now detects the reset and says this,
  rather than returning a boot log that reads like success.
- **A saved image can boot without Ndless at all.** Persistence is a file the OS consumes at boot and
  Ndless recreates a moment later. I think a `flashsave` inside that window is how the image ended
  up disarmed. `d 100280cc` reading 04 F0 1F E5 is the proof it is resident. `blob` re-arms it, see
  the last section.

So the working order in one session is: start with boot=true, wait for status to say linked *and*
for the OS Time line, then deploy, exec and fetch.

## keysvc never registered because Ndless was not loading. 2026-09-02

keysvc sat in /ndless/startup, the link was up and connecting to 0x4B45 got nothing. The log file
was never written either. So main was not reaching its fopen. Both hypotheses carried over from the
last session were wrong. It was not TI_NN_Init and it was not a fault in the program. Ndless was not
resident at all and the startup folder never ran.

Measured:

- `d 100280cc` read E1 A0 30 00 on two readings well after boot, which is the OS's own `mov r3, r0`
  rather than the E5 1F F0 04 that HOOK_INSTALL writes. The SWI vector at 0x10000028 read
  0x1042A560, inside the OS image, where an earlier session had recorded 0x13A1BA40 with Ndless
  loaded.
- The boot log ended at the OS Time line. The "Setting theDoc.filename to NULL in Execution
  Context" line that marks the OS reopening the persisted document did not appear.
- The files were all there. /ndless/persistent.tns is byte identical to persistent_6.2.0-6.4.0.tns,
  /ndless/ndless_resources.tns is present and keysvc.tns in the startup folder is identical to a
  fresh build of the current source.
- Scanning the flash image on the host for the currentdoc.data block that persistency.c writes
  (words 0, 2 and 4 set to 1 in a 0x21C block) found it twice in nspire-os.img and not at all in the
  09:51 backup or in nspire-keytest.img. That did not settle whether the image was armed, because
  Reliance keeps copies of things. The arming state was read off the boot behaviour instead.

So the image had lost its persistence arming at some point before the 17:49 save. I think the save
caught a boot where the OS had consumed currentdoc.tns without Ndless coming back to recreate it, but
that is inferred from the timestamps rather than measured. What matters is that a saved image can be
disarmed and nothing in the loop noticed. The check is one command: `d 100280cc` must read
04 F0 1F E5. If it reads 00 30 A0 E1 nothing in the startup folder has run.

### `blob` runs Ndless's own stage0 with no keypress

The only known bootstrap was the Qt GUI with a person pressing keys. The debugger key command is
unreliable. The persistent installer is a Lua document whose whole job is to get a few hundred bytes
of position independent ARM code running on the gui task. stage0 (ndless/src/persistent-6.4/stage0.S)
matches the reset vector against a table, calls the OS's stat, malloc, fopen, fread and fclose on
ndless_resources.tns, flushes the caches and jumps into it with argv "PER", which is what makes
ndless_resources run persistency_install. Then it returns through lr. The 6.4.0.74 CX II entry checks
out against the IDC: stat 0x104935d0, malloc 0x1009a264, fopen 0x1042359c, fread 0x10423740, fclose
0x1042301c.

The exec machinery already knew how to wait for the gui task and push code onto its stack, so
`blob <host file>` reuses it (firebird-src/core/armsnippets_loader.c). The blob is queued, pushed
the next time armloader_ndless_context() holds and entered at its first byte with lr set to the
interrupted pc, which carries RF_ARMLOADER_CB so the saved registers come back when the blob
returns. One thing is queued at a time, a blob or an exec. That way a poll cannot push twice onto the same
task.

```
blob <workspace>/ndless-src/ndless/src/persistent-6.4/ndless_installer.bin
c
```

What it printed: queued at sp a4001d5c, the idle loop, then `blob: loading 440 bytes at pc
103fea00 sp 1800f258 cpsr 00000013`, then `blob: returned with r0 00000000`. After that the
hook word read 04 F0 1F E5 and /ndless/keysvc.txt.tns existed. So the install also ran the startup
folder. `flashsave` then wrote 19 blocks.

Cold boot on the saved image with no blob: the theDoc.filename line is back in the boot log, the
hook word reads Ndless and the keysvc log was rewritten at boot with only its two startup lines. So
the OS persistence mechanism does work on this emulator once the image is armed. The loop no
longer depends on the GUI for a bootstrap. The image as it was before the re-arm is
images/nspire-os.img.bak-20260902-pre-blob.

Two things to know about it. A second `exec` or `blob` issued while the previous snippet is still
running overwrites the saved return state, which was true of `exec` before this and is not guarded.
Wait for the blob returned line. Also the blob runs on the gui task's 64K stack. The command
refuses anything over 32K.

### keysvc works end to end

With Ndless resident the helper registers without TI_NN_Init. The log reads
`TI_NN_StartService(0x4b45) = 1`. A packet of four byte records (code low, code high, modifiers,
action) gets `6b 01` back, k and the count applied.

```
ln svc 0x4B45 0D 10 00 00
```

That is a tap of enter, code 0x100D from the plain column of the keydefs table in
tools/nsptool/nsptool.c. It dismissed the modal dialog on screen, checked by screenshot before and
after, on the blob-installed boot and again on the cold boot. The dialog was there because every `fetch`
raises a "Document Sent" dialog on the gui task, the twin of the "Document Received" one deploy
raises. This is now the way to clear either without the GUI. A tap of doc (0xFE00) while that
dialog was up changed nothing on screen even though the log shows the record applied. Presumably
the modal dialog ignores keys it has no use for.

It also works on the physical calculator, OS 6.2.0.333. With keysvc.tns put into /ndless/startup over
USB and a reboot, the log read `startup=1 hwsubtype=2 start=0x100bb25c` and
`TI_NN_StartService(0x4b45) = 1`. Then `nsptool.new key enter` reported `keysvc applied 1 of 1` and
dismissed the Document Sent dialog the log fetch had raised, checked by device screenshot before and
after. The one difference from the emulator is the read that ends a batch: status -257 on hardware
where the emulator gave 0. Also the installed /usr/local/bin/nsptool predates the key commands. The
MCP's device tool picks the newest build itself, see the next section, and the checkout build reaches
the device without sudo.

### The MCP could not send a key and now it can. 2026-09-02

The MCP ignored enter, ret and click against a Document Received dialog on the handheld, with the
screen byte identical after each. The calculator was fine. The device tool in nspire.py had no key
action at all and ran only /usr/local/bin/nsptool. That install is the Sep 1 build whose usage lists
no key command. Nothing was ever sent. The identical screenshots were the screen being left
alone.

Checked on the hardware before changing anything: a put raised Document Received with Open focused,
`nsptool.new key esc` came back `keysvc applied 1 of 1` and the next frame was the Home screen. So
the dialog that blocks the link on the emulator does not block it on the handheld. Screenshot and key
both went through with it up.

The device tool now has key and type, taking `keys` and `text`. It runs whichever nsptool is newest
out of the installed binary and the checkout's own builds. An install lagging nsptool.c cannot
drop a command silently again that way. Only the installed binary goes through sudo -n, because that is the
one location the sudoers rule names. The checkout build runs as the user, which nsptool.c already
relies on at line 38. A failure now carries the binary, the action and the exit code with nsptool's
own text. Re-proven through the MCP itself: put, `device key esc`, screenshot, Home screen. The
selftest covers the argv for both binaries and passes. It had been crashing before those checks on
two stale calls left over from renaming the keys helper to `keys.patch`.

Two things learned on the way. An rm raises a Document Deleted dialog of its own. Put, get and rm
all leave one and `key esc` clears any of them. And once, with the handheld in the document browser's
Save dialog for ki_v2perf, a key was refused with `Invalid packet received` and the next log fetch
showed a fresh boot, two startup lines and no batch. Whether it was rebooted by hand or reset on its
own is not known. If keys are refused again, the screen state and the log size are the first two
things to look at.

## Driving a document end to end, 2026-09-02

The first session that opened a Lua document, typed into it and read the answer back, on both
targets. Four things about the link and one about the emulator, all measured while getting there.

**`ln s` refuses a name with two extensions.** `nps_split.luax.tns` failed three times with "Link
transfer failed" on the emulator. The same bytes as `stepcasx.tns` went over immediately, and a
172804-byte file transferred in between, so it is neither size nor link state. The comment on the mv
command in core/debug.cpp already said the OS refuses an upload whose name it does not like. The
handheld's file service, which `device put` uses, takes `nps_split.luax.tns` directly.

This paragraph used to end by saying `ln mv` renames it in place afterwards. That is wrong and the
section above already says so: this OS refuses `ln mv` even for a `.tns` to `.tns` rename, confirmed
again on 2026-09-03 with both an absolute and a relative pair. There is no rename dance on the
emulator. What there is instead is retrying the two-extension send, which is intermittent rather than
always refused, and which succeeded on the second identical attempt that day.

Not the name alone, though, measured 2026-09-03: `luagiac.luax.tns`, 3814775 bytes, went over
under its own two-extension name at the first attempt while `nps_split.luax.tns` failed again the
same minute. What the OS objects to is not known; the rename dance stays the reliable path for the
module, and a fetch plus `cmp` is the proof it landed.

**A document the calculator has open cannot be replaced.** Sending `nps_v2.tns` while that document
was open failed the same way, and a differently named copy of the same bytes went over. Send under a
new name during a session, or close the document first.

**Three Lua documents that each load luagiac in one OS session is one too many.** 2026-09-03: the
first ki_v3 opened (and errored in Lua), the second opened and ran with "Giac 1.9.0 : OK.", the
third sat on the busy clock for over a minute with keysvc getting no reply, and never came up. Each
open is a fresh `nrequire("luagiac")` of an 8.4 MB load into a heap that the earlier copies never
gave back. Restart the emulator between documents that load it (flashsave first, so the sent files
survive the reboot), and expect the same on the handheld.

**keysvc cannot press an arrow.** 2026-09-03: the arrow codes in nsptool's table (up 0x7100,
right 0x7300, down 0x7500, left 0x7700, copied from the OS keypad map) are accepted by keysvc and
do nothing, checked on the emulator's home screen where a real down moves the highlight from
Calculate to Graph (probe/e34.png, probe/e35.png). Letters, enter, esc, shift+tab and the ctrl
column all work through the same path, so it is the arrows specifically: on the CX II they come
from the touchpad, and the touchpad driver evidently does not go through send_key_event with these
codes. Consequence: a document's arrow handling cannot be exercised from the host on either target
until keysvc learns how the touchpad posts them. Ki V3's viewer takes both arrow events and was
checked on the host only.

**Every debugger command stops the guest**, screenshot included, and only `c` resumes it. A batch
that ends in `screenshot` leaves the calculator frozen with the busy clock up, which reads exactly
like a document that will not load. End such a batch with `; c`. A `screenshot` in the same batch as
the command before it captures the guest mid-frame: 2026-09-04 a batch of `ln svc ...; screenshot; c`
came back black with only the cursor drawn, and the same screenshot taken after a `c` was fine.

**A caret typed over the link swallows the rest of the line.** 2026-09-04, typing
`!kv=?,v0=0m/s,a=9.8m/s^2,t=2.5s` into Ki V4 through keysvc: the 2D editor opens a superscript box on
`^` and everything after it goes inside, so the document received `a=9.8m/s^(2,t=2.5s)` and refused
it. Leaving the box needs a right arrow, which keysvc cannot press. Spell the unit `m/s/s` instead,
which is the same dimension and has a test for it. The same applies to any editor box a caret,
a fraction or a root opens.

**After a solve the step viewer has the focus, not the input line.** Characters sent straight after
one get keysvc's receipt and never appear: the viewer is what receives them and it ignores a letter.
An `esc` leaves the viewer and the same characters then land. Send it as a habit before typing,
because on screen a swallowed key and a lost packet look the same.

**The keysvc reply is the receipt.** `Link svc reply (2 bytes): 6b 01` means one key record was
accepted; since 2026-09-03 the keys are posted after the reply, see the keysvc section below. When
the OS is busy the call comes back with no reply line at all and the key is simply lost, which looks
exactly like a key that was delivered and ignored.

On the handheld this is half true, checked 2026-09-02. The half that holds: a call that gets no reply
delivers nothing. nsptool reports it as `keysvc: Invalid packet received` with exit 1. On the CX II
that is libnspire giving up after ten reads with no usable reply (libnspire-src/src/cx2.cpp lines 453
to 466) and it takes about twelve seconds. The one keysvc refusal seen left no batch line in a log
fetched straight after it. The log's batch count matched the successful calls one for one. So a reply
is a receipt and a refusal is a key not sent. The half that does not hold is the busy OS. A nested sum
that kept the Scratchpad computing for over sixteen seconds got a digit sent right after enter and
another four seconds in. Both were answered `keysvc applied 1 of 1` and when the result appeared the
entry line read 12. Keys sent while the OS computes are queued behind it, not lost. The refusals came
in a cluster around the end of that computation and hit screenshot as well as keysvc (two of six
paced screenshots) and then stopped: twelve paced info and key calls all answered afterwards. What
starves the link for those seconds is not known.

**One enter opens a received document.** A deploy raises "Document Received" with Open under the
cursor, so `ln svc 0x4B45 0D 10 00 00` opens what was just sent. That is the whole bootstrap: no
file browser navigation, no GUI.

**The emulator does not hang in a power routine when it idles.** That was the reading on 2026-09-02
and it was wrong; the correction is in the next section. The idle resets seen that day were keysvc
blocking the task that then processed its key, also below.

## keysvc blocked the task that processed its keys, 2026-09-03

Every document opened through `ln svc 0x4B45 0D 10 00 00` reset the calculator, on three boots, and
the same document opened through the fork's `key enter` every time. The fault moved with each
change to keysvc, which is what said the defect was in keysvc and not in the document: first its own
`serve` popped a return address that pointed into the stack, with r4 to r10 replaced; then the
resident's strcmp read through a callback context of 0x20000013, a saved CPSR; then keysvc's
`_write` faulted in its errno path with the GOT register, computed from pc at function entry, holding
a stack address. Nothing between that entry and the fault could have set it, so the task had been
switched out inside the OS call and resumed with its saved context overwritten. `serve` runs on the
task that processes the key. When it posts a key that opens a document and then blocks in
TI_NN_Write or fwrite, the document open runs on that task and the parked frame comes back corrupted.

Three changes did not fix it and are recorded so they are not tried again: one packet per connection
instead of a 30 second read loop, moving the read buffer and count out of the stack, and saving and
restoring r4 to r11 around send_key_event. What fixed it: reply, log, then post the keys, with
nothing blocking afterwards. After that the probe document and Ki V2 opened and ran through keysvc
with no fault, and the emulator idled for two minutes with a document open and stayed up.

Two consequences. The reply `6b <n>` is now a receipt for n records accepted, not applied: the keys
go out after it. The sleep record (action 3, `sleep:<ms>` in nsptool) is gone for the same reason:
a wait inside keysvc blocks the task that processes the keys, so it could not pace them and it had
already wedged the emulator's link once. Pace on the host, between calls. And the handheld's USB drops after back to back keys, in open-questions.md, had the same shape:
with the new build in /ndless/startup (put on 2026-09-03 with the rebuilt resources) two keys in
one packet and a third straight after were all accepted and the bus stayed up. One warning from
that day: a reboot forced over the link, through a document loading a module that faults on
purpose, reset the calculator but it did not re-enumerate on USB until restarted by hand.

**The PMU warnings were a debugger artefact.** The routine at 0xa40019f4 does not poll. It walks
five power domains reading two status bytes each, at 0x20 and 0x24 plus 0, 0x40, 0x80, 0xc0 and
0x140, into a ten byte record per domain, and returns. `debug_on_warn` broke on each unmodelled read
in turn, which read as a loop that never left. core/cx2.cpp now backs every offset below 0x200 with
storage, so the reads answer what was last written, zero by default, and no warning fires.

**The fork's `key` only lands inside a short window after other activity.** Measured 2026-09-03.
A keypad tap is serviced when it follows link traffic (a deploy) or a keysvc key within a few
seconds; the `key x` right after a keysvc `x` typed into Ki V2, the same tap seventy seconds later
did not, and a tap on a Document Received dialog left idle for two minutes did nothing. The press
itself reaches the OS every time: the row data changes, the keypad's change interrupt (int_enable
2, bit 16 unmasked at dc000010) fires and the OS clears it, so the loss is above the interrupt
handler, in whatever the input path does when the OS has gone quiet. Ruled out: tap length (a hold
across seconds fails the same way), a touchpad contact before the tap, and raising the PMU
interrupt on the press (tried and reverted). The emulator runs faster than real time, which is why
the window closes between tool calls. Use keysvc, or send the tap in the same `dbg` batch as the
deploy or the keysvc packet that precedes it.

**Do not `exec` a program that registers a NavNet service.** A second keysvc under another service
id, run through `exec`, left both ids unanswered and every deploy failing on two boots. Put it in
/ndless/startup, flashsave, and reboot.
