# Hooking a debugger onto CX II hardware

**Historical research report.** This document preserves the investigation behind a
possible debugger on physical CX II hardware. It is not a current device-status report
or an operating procedure.

Scope clarified on 2026-09-06. The original investigation date is not recorded here.
Source line numbers, installed tools, device state and image behavior describe that
investigation. Recheck them against the relevant checkout and hardware before using
the proposals below. The [codebase map](codebase-map.md) describes the source and
transport components. [nps/README.md](../nps/README.md) carries build and test guidance.

| Research question | Evidence and limits |
|---|---|
| What debugger support was found? | [Recorded support](#recorded-debugger-support) |
| Can the CPU and exception machinery support a stub? | [CPU](#the-cpu) and [exception vectors](#exception-vectors) |
| Can a stopped program still communicate? | [Transport](#transport), [USB fallback](#the-usb-fallback-looks-bad) and [link lifetime](#keeping-the-link-alive-while-stopped) |
| How could the stub stay resident? | [Residency](#residency) |
| What work did the investigation propose? | [Proposed debugger work](#proposed-debugger-work) |

What is known about running a GDB stub on the physical calculator rather than in the
emulator. Findings from reading this tree plus two web research passes. Claims verified
locally cite a file and line. Claims from the web cite a URL and say how well sourced
they are.

## Recorded debugger support

GDB debugging for Ndless exists and has existed since Ndless r848, but every working
instance of it is emulator hosted. Firebird serves a GDB remote stub, 855 lines in
firebird-src/core/gdbstub.c, reading emulator CPU state directly through state->reg and
the emulator's virtual memory. The nspire MCP server exposes it as emu gdb=port, and
build debug=true compiles -O0 -g for it.

Four independent sources describe GDB with Ndless and all four are the emulator:
the Ndless README, hackspire, the Firebird wiki, and a Ncubate demonstration. None
describes debugging on hardware. No source found states why it was never done, so
"nobody has" is well corroborated and "why" is not answered.

The only reference to GDB inside Ndless itself is emu_debug_alloc at
ndless-src/ndless/src/resources/emu.c:66, a syscall whose whole purpose is to hand
Firebird's stub a fixed load address. It is emulator support code, not a stub.

arm-none-eabi-gdb was not built on this machine during the investigation. The toolchain
had the full binutils and gcc set in ndless-src/ndless-sdk/toolchain/install/bin and no gdb. The build script
pins GDB=gdb-16.2 at ndless-src/ndless-sdk/toolchain/build_toolchain.sh:25, so building
it is a matter of running the script. Every route below needs this first.

## The CPU

The CX II is an ARM926EJ-S. Confirmed twice: datamath's teardown
(http://www.datamath.org/Graphing/NSpire_CXII.htm) identifies an ARM9 in a custom ASIC
marked ET-NS2018-000, and Firebird returns the matching MIDR value 0x41069264 with the
comment "ARM926EJ-S revision 4" at firebird-src/core/coproc.cpp:10.

Whether that core gives us hardware breakpoints and watchpoints from software is
**unresolved, and the research got it wrong**. The research returned two cards claiming
CP14 breakpoint and watchpoint registers, but one cites ARM document ddi0338 and the
other the ARM720T manual. Neither is the ARM926EJ-S manual, and the summaries hedge with
"this core generation" rather than naming the core. ARM9-era cores use EmbeddedICE-RT
rather than the CP14 BCR/BVR/WCR/WVR layout of ARMv7, so those cards are very likely
describing a different debug architecture.

Open question: read the ARM926EJ-S TRM (DDI0198) directly and establish how many
EmbeddedICE watchpoint units exist, whether they are reachable by MCR and MRC to CP14
from software, and whether monitor mode works without JTAG. This decides whether we get
hardware watchpoints or only software breakpoints by patching undefined instructions.

## Exception vectors

Hooking abort vectors is precedented in this tree, at a lighter weight than the research
suggests. Ndless already rewrites the OS copy of the vector pointer table: the constants
are INTS_INIT_HANDLER_ADDR 0x20 and INTS_SWI_HANDLER_ADDR 0x28 at
ndless-src/ndless/src/resources/ndless.h:64, and ints_setup_handlers patches the SWI
slot at ndless-src/ndless/src/resources/ints.c:32. The 8-entry spacing puts undefined
instruction at 0x24 and prefetch and data abort at 0x2C and 0x30, which is what
breakpoints and watchpoints need.

The only worked example the research found is eMMUlate (https://satyamedh.me/emmulate/),
which builds its own vector table, maps it at 0xFFFF0000 and sets the high vectors bit
in the ARM control register. That is a heavier mechanism aimed at taking the machine
over. A stub that has to coexist with a running OS wants the in-place patch Ndless
already does, not eMMUlate's replacement table. Both agree on the important part: there
is no Ndless SDK call for this, and no permission gate once code is running in SVC mode.
Hackspire states plainly that Ndless programs have total control of the machine
(https://www.hackspire.org/Ndless_features_and_limitations/).

## Transport

The service channel worked during the recorded hardware session. That session ran
device info and device key esc against the plugged-in CX II and both replied, the second with
"keysvc applied 1 of 1", so keysvc was resident and the NavNet path was live end to end.

The shape of that path constrains the design. nspire_service_exchange connects, writes
once, reads once and disconnects, at libnspire-src/src/services/raw.c:24. There is no
persistent stream and no out-of-band channel.

That is survivable. The GDB features that would need an async channel are opt-in and
default to off: QStartNoAckMode and QNonStop are only enabled if the stub advertises
them in its qSupported reply
(https://sourceware.org/gdb/current/onlinedocs/gdb.html/General-Query-Packets.html,
https://rocm.docs.amd.com/projects/ROCgdb/en/docs-6.4.1/ROCgdb/gdb/doc/gdb/Remote-Non_002dStop.html).
Stay silent and GDB uses ack-based all-stop mode, which is what a polled transport wants.

The minimum packet set for a single-threaded stub is six: question mark for halt reason,
g and G for registers, m and M for memory, plus c and s for run control. vCont only
matters once there are multiple threads, and unimplemented packets answer empty so GDB
falls back (https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html,
corroborated by https://www.acrc.bris.ac.uk/acrc/RedHat/rhel-gdb-en-4/remote-protocol.html).

The one real casualty is control-C break-in, which needs an out-of-band byte while a
command is in flight. The host relay has to replace it by polling for a stop condition.

### The USB fallback looks bad

Driving the controller directly from an abort handler was the fallback if the OS route
fails. The research makes it look expensive and unproven. A TI-Planet discussion states
the OS has to be running for USB communication to work at all, that IRQs must be
re-enabled and the hardware shared rather than claimed, and that the gadget-side stack is
poorly understood (https://tiplanet.org/forum/viewtopic.php?lang=en&p=225255&t=20857).
A developer who tried a custom Ndless USB driver found the callbacks never fired, even
resident and with cache cleared (https://github.com/ndless-nspire/Ndless/issues/193).
The hardware itself is at least documented: hackspire identifies a ChipIdea dual-role
OTG controller, full speed only, and names the PORTSC PFSC bit
(https://hackspire.org/index.php/USB_Protocol). Nobody in the sources has driven it
bare-metal on this device.

## Keeping the link alive while stopped

This is the load-bearing unknown. If a breakpoint halts the world, the stub cannot answer
the host, because the link is serviced by the OS.

The generic RTOS answer is encouraging. NU_Suspend_Task takes a pointer to one task
control block, so it parks one task and leaves the scheduler running the rest
(https://www.embedded.com/tasks-configuration-and-api-introduction/), and Nucleus PLUS is
a preemptive multitasking kernel (https://embedded-os.de/en/nucleus.shtml).
nspire-task-manager (https://github.com/compujuckel/nspire-task-manager) proves the
Nspire runs multiple enumerable Nucleus tasks on real hardware.

What is not established is whether any of that is reachable here. Hackspire notes TI
licensed the source and may have customised it, and that which Nucleus modules are
included is unknown (https://www.hackspire.org/Operating_System/). Nothing confirms the
link service is a task, or that it keeps running while another task is suspended.

The precedent for reaching an undocumented OS entry point is in this tree: keysvc calls
TI_NN_StartService by hardcoded address per OS version, 0x100bb25c on 6.2.0.333 and
0x100bb24c on 6.4.0.74, at tools/keysvc/keysvc.c:28. The same route is open for task
control, with one gap.

The IDC files in ndless-src/ndless/src/tools/MakeSyscalls/idc name these on
ncascx2 6.2.0.333, the OS recorded for the device:

	TCC_Current_Task_Pointer  0x10428a88
	TCC_Resume_Task           0x1042924c
	TCC_Terminate_Task        0x10429554
	TCC_Reset_Task            0x104296b8
	TCC_Create_Task           0x104298c0

The public NU_Suspend_Task name never appears in any IDC file. The internal Nucleus name
is TCC_Suspend_Task, and it is named only in the 3.x and 4.x files, not in any CX II one.
The CX II IDCs are a lightened subset, 854 names in OS_ncascx2-6.2.0.333.idc, so the
absence means unnamed rather than missing from the OS.

Its address can be inferred, and two independent calculations agree. On
ncascx-4.4.0.idc the layout is Current_Task_Pointer 0x103A8B88, Suspend_Task 0x103A9044,
Resume_Task 0x103A934C, giving deltas of 0x4BC and 0x308. Both deltas land on the same
answer for 6.2.0.333: 0x10428a88 plus 0x4BC is 0x10428F44, and 0x1042924c minus 0x308 is
0x10428F44. The Resume to Terminate gap is 0x308 on both OS versions, so the layout is
stable. The same arithmetic gives 0x10428FF4 on 6.4.0.74.

So TCC_Suspend_Task is predicted at 0x10428F44 on ncascx2 6.2.0.333 and 0x10428FF4 on
6.4.0.74.

The 6.4.0.74 prediction is confirmed. The emulator flash
images/nspire-os.img.bak-20260902 runs 6.4.0.74, and disassembling 0x10428FF4 there gives
a clean function start, stmdb sp!, {r4-r8,lr}, preceded by a literal pool at 0x10428FE4
and the previous function's terminating branch at 0x10428FE0. It reads a fifth argument
from [sp+0x18] on top of r0 to r3, which is the arity of Nucleus TCC_Suspend_Task
(task, suspend type, cleanup, information, timeout). The tail of the function that
precedes TCC_Resume_Task calls it at 0x10429258 with r1 set to the constant 0x0b, a
suspend type. Signature, position and call site all agree.

The 6.2.0.333 address is still inferred rather than seen, because no 6.2 image is
available to boot here. The deltas from TCC_Current_Task_Pointer to TCC_Resume_Task are
identical on both versions, 0x7C4, so the layout is stable and the 6.2 figure carries the
same arithmetic. The device was recorded as running 6.20.333, so this is the one that
matters and it should be checked before any code depends on it.

The instruction-level comparison against 4.4.0 was not possible: the IDC file gives
names and addresses only, and no 4.4.0 OS image is present in this tree. What replaced it
is the arity and call-site evidence above.

During this investigation, images/nspire-os.img itself no longer booted. The emulator
process exited shortly after the boot loader launched. The backup image booted fine,
which is what the disassembly above used.

Still unknown either way: whether the NavNet link service is a task, and whether it keeps
running when another task is suspended.

## Residency

nl_set_resident is the only persistence primitive, and it is one-way: Ndless has no way
to free a resident block afterwards (https://www.hackspire.org/Ndless_features_and_limitations/).
It exists in the local SDK, declared at ndless-src/ndless-sdk/include/nucleus.h:130 with
syscall id e_nl_set_resident at ndless-src/ndless-sdk/include/syscall-list.h:383.

Residency alone does not get CPU time. The TI-Planet daemon thread recommends Nucleus
tasks and timers over hooking an interrupt handler, on the grounds that they are made for
it (https://tiplanet.org/forum/viewtopic.php?f=20&t=14464), and an open Ndless issue
confirms there is no clean interrupt hook API
(https://github.com/ndless-nspire/Ndless/issues/23). An earlier research pass reported a
HOOK_INSTALL mechanism from a search snippet of a page that returned 403. The second pass
did not corroborate it, so treat it as unconfirmed.

A resident program lives until power off, reset, or an Ndless reload, since Ndless itself
is reloaded every boot.

## Proposed debugger work

These were proposed next steps. This report does not establish them as completed or
validated hardware procedures.

1. Build gdb. Needed by every route including the emulator one.
2. Settle the two open questions: EmbeddedICE watchpoint units on ARM926EJ-S, and whether
   Nucleus task suspend is locatable in OS 6.2.0.333.
3. Stub on the calculator: patch the abort vector slots Ndless already patches, save the
   exception frame, implement the six packets. The packet logic ports from
   firebird-src/core/gdbstub.c with the accessors swapped from emulator state to the real
   frame and real pointers.
4. Host relay: a TCP listener that pipes RSP bytes through nspire_service_exchange and
   polls for stop events. Then target remote on the port.

The cheap alternative, if the suspend question comes back badly: build -g, install a
fault handler that dumps registers and a stack window to a file, pull it with device get,
symbolize with arm-none-eabi-addr2line. Post-mortem only, no stepping, needs nothing that
does not already exist.
