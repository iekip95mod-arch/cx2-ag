# Open questions and unverified claims

## Current unresolved work, 2026-09-08

The [menu walkthrough plan](../docs/menu-walkthrough-plan.md) tracks unfinished family coverage and UI incrementality. Native SolveTask now retains published linear and rearrangement work across checkpoints. Lua remains synchronous. The older blanket cancellation-and-rewind claims below do not describe that ownership contract.

Current [physical qualification](../nps/benchmarks/BUDGETS.md) still needs runtime persistence, disconnected operation, full workloads and frozen resource budgets. Later loaded-package checks supersede the old pending-launch and next-experiment notes below. Host evidence does not close the [MVP acceptance gates](mvp-scorecard.md).

## Retained September question log

The settled items, absence claims and proposed experiments below describe the sessions that recorded them. They are preserved as history and are not the current work queue.

Kept separate so nothing in the other notes has to hedge. Everything here is either unmeasured or
unchecked as of the session that wrote these files.

## Settled by measurement, 2026-09-01

1. ~~Does the local luagiac build load on OS 6.2.0.333?~~ **Yes.** khicaslua run on the device
   prints `Giac CAS engine : OK.` The 2024-07-06 luagiac build works on 6.2.0.333 despite its
   README naming only 4.5.3 and 5.2. Confirmed statically beforehand as well:
   `interp_startup_addrs[44]` is 0x101866DC, nonzero, at ndless/src/resources/luaext.c lines 76 to
   95, and 44 is the index for 6.2.0.333 non-CAS CX II at ndless/src/resources/utils.c line 220. So
   the hook that sets the state nl_lua_getstate returns is installed on this OS.
2. ~~Is Ndless currently installed on the device?~~ **Yes**, rev 2022. smoke.tns ran on hardware and
   its result file reports exact 64-bit integers (20! = 2432902008176640000), a successful 1 MiB
   malloc through the OS heap, and exact soft-float halving. persistent.tns is installed and is
   byte-identical to a fresh build of persistent_6.2.0-6.4.0.tns, sha256 ef71da40.
3. ~~Does the toolchain build on this Mac?~~ **Yes.** ndless-sdk/toolchain/install/bin is populated
   and the whole of Ndless was built through it. One undocumented prerequisite: mkSyscalls.php
   needs php, which was absent. Installed via `brew install php`, 8.5.10. The toolchain is not on
   PATH, see tooling.md for the export line.
4. ~~Free heap after luagiac loads.~~ **About 22 MiB in one block, 25 MiB total**, measured by
   probe/heap.c. Full table and the startup-versus-launched split in target-hardware.md. The
   startup-folder reading is five times smaller and must not be used as a budget.

   **Superseded 2026-09-05, and the 25 MiB was wrong for a reason nobody suspected.** probe/heap.c
   counts free memory in 1 MiB chunks, which loses whatever does not divide the free runs. Counting
   in 64 KiB chunks, and again by repeatedly taking the largest block available, both give about
   28.4 MiB in the same state, and the two agree with each other within 65 KB. So the 1 MiB figure
   understates free memory by 2.0625 MiB with nothing of ours loaded and by 2.5 MiB with everything
   resident. It does so whether or not luagiac was loaded, which makes the residual doubt about
   luagiac beside the point. The with-everything-resident reading now exists in
   nps/benchmarks/BUDGETS.md: 18.4 MiB free at launch with the unified module, Giac and the Ki V4
   document up, 13.8 MiB of it contiguous. Emulator only, so PRD section 19.6 is not satisfied.

## Unresolved by measurement, not by reading

5. ~~**Cancellation.**~~ **Settled by construction, 2026-09-03: cooperative polling.** The
   toolchain is built --disable-threads, so there is no worker thread to interrupt. Every rule
   engine counts its work through one `Meter` (nps/src/budgets.h) that polls a cancel callback
   every 64 rewrites and enforces the rewrite, step and backend-call budgets; a halt rewinds the
   derivation to what it held on entry, which is PERF-009. Tested for the linear solver, the
   derivative and the integral, including a budget the integral's own derivative check runs out
   of. What it does not cover is a call already inside Giac: luagiac exposes no interrupt, so a
   long `caseval` runs to Giac's own limits, and that is the residual of section 12.3's warning.
6. ~~**Lua-first versus native-core.**~~ **Settled by construction, 2026-09-02, and the PRD should
   be amended to say so rather than left describing a comparison that will not happen.** Section
   12.2 asks for a vertical prototype of both arms. Only the native-core arm was built, and item 10
   below is why the other arm cannot answer the question: luagiac exposes exactly one function
   taking a command string, so the typed adapter of section 12.1 and every validation of section
   12.3 have to be native whichever arm wins. A Lua-first prototype would have to write the same
   native code to be comparable.

   What was measured on the native-core arm rather than argued: the bridge is one file, 289 lines,
   passing text one way and a table back. The module is 93675 bytes on target. It loads and runs
   under the OS Lua interpreter on the emulator and on the handheld, and Giac answers it. Crash
   isolation is the one thing this arm does badly and the comparison would have shown it: a fault in
   native code resets the calculator, and the Lua guard in nps_v2.lua only catches Lua errors.
7. ~~Giac deployment contract, PLAT-010.~~ **Settled.** Licensing is out of scope for this
   project, which was the only real argument for the separately-installed branch. Bundle luagiac.
   See giac-backend.md.
8. ~~**First-step and total latency.**~~ **Measured 2026-09-03 on the handheld**, OS 6.2.0.333,
   Ki V2 with Giac and the module resident, the clock around the whole call including the Giac
   cross-check (probe/hw_perf1.png, probe/hw_perf2.png):

   | Problem | Total | Nodes | Steps | Giac calls | Lua heap |
   |---|---|---|---|---|---|
   | d/dx of x^2*sin(x), first run of the session | 20 ms | 24 | 4 | 2 | 85 KB |
   | solve 2x + 5 = 13 | 10 ms | 16 | 4 | 2 | 92 KB |

   PERF-005's two seconds and PERF-006's five are two orders of magnitude away for the worked
   examples, so the provisional targets stand and the budget question moves to the corpus rather
   than the examples. The first run is the cold one: nothing was warmed before it.

## Not documented anywhere found so far

9. Heap size, stack size and recursion depth limits. The hackspire features and limitations page
   does not state them. PERF-008 wants enforced AST depth and expression size limits, and those
   have to be calibrated against a stack depth nobody has measured yet.
10. ~~Whether luagiac exposes a typed adapter surface or only command strings.~~ **Settled: string
    only.** giac-src/src/luagiac.c registers exactly one function, and its whole body is
    `giac_caseval(param)` on a `luaL_checkstring`:

    ```c
    static const luaL_reg lualib[] = {
        {"caseval", caseval},
        {NULL, NULL}
    };
    ```

    khicas.lua uses nothing else, at khicas53/khicas.lua line 1393. So none of the typed surface PRD
    section 12.1 asks for exists, and section 12.3's ban on concatenating user text into Giac
    commands falls entirely on our adapter: it has to do all quoting, escaping and validation, and
    parse results back out of a string. Budget for that, and note it is a real argument against the
    Lua-first branch of question 6.
11. CX II CPU and RAM figures came from datamath.org and the hackspire CX II memory-map page. The
    hackspire Hardware page has no CX II entry at all, so there is no single authoritative page.

## PRD open questions this research already answers

- Question 2, which TI OS and Ndless versions define the baseline. On CX II the only supported
  builds are 5.2.0.771, 5.3.0.564, 6.2.0.333 and 6.4.0.74. The device runs 6.2.0.333, and Ndless
  r2022 covers it with per-OS syscall tables for the non-CAS variant specifically.
- Question 15, bundle Giac or validate a separately installed component. Answered: bundle it.
  Licensing is out of scope for this project, and that was the only argument for the other branch.

## Solved: the emulated CX II now has an OS. 2026-09-01

The flash image boots and stops at "Waiting for OS download", which is correct, because
`flash_create_new` has no OS slot for CX II. Its CX II branch takes exactly four preloads, Manuf,
Bootloader, Diags and Installer, and returns (core/flash.cpp lines 702 to 726). The CX branch further
down is the one the Firebird wiki's "choose an operating system file" step describes. So on CX II the
OS has to arrive over usblink, and it does not.

Where it stops, measured rather than guessed:

- `ln os` reaches the debugger. Sending `ln os <file>` then `r` mid-run prints registers, so both
  lines were processed.
- The transfer never starts. `usblink_queue_do` returns immediately unless `usblink_connected`
  (core/usblink_queue.cpp line 77), and that flag is only set when the calculator sends a TimeService
  request and the emulator answers it (core/usblink_cx2.cpp lines 284 to 287). That path prints
  "usblink connected.", which appears in no run.
- The host side does its part. Instrumenting `usb_cx2_bus_reset_on`, `usb_cx2_bus_reset_off` and
  `usb_cx2_receive_setup_packet` shows all three fire once, in order, after `ln os`.
- The calculator never answers. At the moment the SETUP packet is queued, `usb_cx2.imr` is 0x5.
  `usb_cx2_int_check` ends with `int_set(INT_USB, isr & ~imr)` (core/usb_cx2.cpp line 41), so a set
  bit means masked, and bit 0 is the device interrupt. The boot loader has the device interrupt
  masked, leaving only OTG, so the SETUP packet raises a status bit nobody is watching.

**The analysis above was right and the conclusion was wrong. It is a timing problem, not a protocol
or emulation defect.** Tracing `devctrl` writes shows the boot loader open a USB window and then
close it on its own within moments:

```
devctrl <- 00000c24      bit 2 set, device interrupts enabled
USB Download is enabled.
Received TI_OFFSYNC_APD_REQ
devctrl <- 00000c20      bit 2 cleared
devctrl <- 00000c00
```

`TI_OFFSYNC_APD_REQ` is the boot loader's own auto power down, not something the emulator sends:
tracing `usb_cx2_real_packet_to_calc` shows zero host to calculator packets before it. So the
SETUP packet was arriving after the window shut, and the recorded `imr` and `devctrl` values were a
consequence of that rather than a cause.

The fix is to send `ln os` earlier. tools/os-install-attempt.sh does it at 20 seconds instead of the
34 in tooling.md, and feeds `c` every 3 seconds across the transfer, which Hackspire's Emulators
page says a CX II install can need.

Measured, same script, only the wait differing:

| ln os at | calc to host packets | host to calc | outcome |
|---|---|---|---|
| 40s | 0 | 0 | stuck at Waiting for OS download |
| 20s | 14439 | 14441 | OS installs and boots |

images/nspire-try6.img now boots `<BOOT LOADE>` then `<OS LOADER>` then `<TI-Nspire>` and reaches
`TI_LOCALE_initializeDefaultLocale`, with no OS-download prompt. That is a working emulated CX II
running 6.4.0.74, and with it a GDB target, see tooling.md.

**Measurement hazard that cost three wrong conclusions.** Firebird's console output contains ANSI
escapes and binary bytes, so plain `grep` treats these logs as binary and prints nothing instead of
a count. That produced "zero packets from the calculator" and "the setup packet never fires", both
reported as measurements and both artifacts of the tool. Always `grep -a` on emulator logs.

## USB keypad input: no built-in service, but a helper can add one

Researched rather than assumed, because it decides how much can be automated. The NNSE protocol has
no input service of its own. libnspire connects to exactly four service ids, 0x4020 devinfo, 0x4024
screenshot, 0x4060 file and 0x4080 OS install, in libnspire-src/src/services. The hackspire USB
Protocol page lists the full known set and none of them carry key or input events; the only two
that even gesture at it are 0x4022 "Event (?)", undocumented, and 0x4043 TE_RPC, marked not enabled
in the OS.

**The first conclusion here, that on-device keypad automation is impossible, was one step short and
is now superseded.** A small resident helper can register its own NavNet service and forward host
records to the OS routine send_key_event, the same call the OS keypad driver uses. The helper is
tools/keysvc and as of 2026-09-02 it registers at boot on the emulator. A host packet of key
records dismisses a modal dialog, checked by screenshot before and after.

The registration problem was not the helper. Ndless was not loading on the emulator image. Nothing
in the startup folder ran and the log was never written. Also TI_NN_Init is not needed before
TI_NN_StartService, which returns 1 as the code stands. The diagnosis and the `blob` command that
installs Ndless with no keypress are in tooling.md.

Also proven on the physical device the same day. On 6.2.0.333 keysvc reaches StartService at
0x100bb25c, the IDC address, and the log shows it returning 1 at boot. (Since 2026-09-03 it calls
the TI_NN_StartService syscall instead: that address is a one instruction thunk onto the function
the syscall table already holds, checked by disassembly on 6.4.0.74, so the hand-carried list is
gone and keysvc is not tied to two OS versions.) `nsptool.new key enter`
dismissed a modal dialog on the handheld, checked by screenshot. The two workarounds below are no
longer needed for key input, but they are still how a program gets run without keysvc.

The MCP's device tool could not send a key until the evening of 2026-09-02. It had no key action and
ran only the installed nsptool, which predates the key commands. It now has key and type and runs the
newest nsptool it can find, see tooling.md. One observation from that round is unexplained: with the
handheld in the document browser's Save dialog a key was refused with `Invalid packet received`. The
next log fetch showed a fresh boot. Whether it was rebooted by hand or reset on its own is not known.
Later the same refusal hit screenshot too, in a cluster after a long Scratchpad computation. So it is
a transport stall rather than anything keysvc does. What starves the link is open. The details are
under the keysvc receipt note in tooling.md.

- **The Ndless startup folder runs programs with no keypress at all.** Verified on the device:
  probe/heap.c reported `startup=1`. plh_startup runs everything under ./ndless/startup at
  ndless/src/resources/ploaderhook.c line 484, and for OS index 44 it is called directly rather
  than through a hook table, at ndless/src/resources/install.c line 180, because the index is above
  10. With persistency installed this fires on every boot.
- **The emulator now takes keys**, see tooling.md. That does not help with the OS-download problem
  below, but it does make the Firebird side scriptable.

What the startup folder cannot do is reach Giac. nl_lua_getstate returns a state that is only set
by the lua_interp_startup hook when the OS Lua interpreter starts, at
ndless/src/resources/luaext.c lines 130 to 140. A startup program runs before any Lua document
exists, so it gets NULL and a .luax module's main bails without registering. Exercising Giac needs
a Lua document open, which needs a keypress.

## Closed 2026-09-03: the emulator did not hang in a power routine

The reading below was wrong. The routine at 0xa40019f4 snapshots five power domains, two status
bytes each, and returns; `debug_on_warn` breaking on each unmodelled read in turn looked like a loop.
core/cx2.cpp now models the low PMU block and the warnings are gone. The idle resets that prompted
this were keysvc blocking the task that processed its key, in tooling.md under "keysvc blocked the
task that processed its keys". The original note is kept for the shape of the mistake.

Firebird does not model two registers in the CX II power management unit at 0x90140000, and the OS
polls them. With `debug_on_warn` on, the break alternates forever between two reads:

```
Warning (a4001a04): Bad read_word: 901400e0
Warning (a4001a40): Bad read_word: 901400e4
```

The code doing it runs from RAM at 0xa40019f4 and holds `aladdin_pmu.clocks` (0x21020303) in r4, so
it is a clock reconfiguration, and `aladdin_pmu_read` in core/cx2.cpp handles only offsets below
0x100 with named cases plus the 0x800 efuse block. `bad_read_word` returns zero and warns, so the
guest reads zero forever and never leaves the routine. Left to run, the calculator resets.

It fires when the emulated calculator sits idle with a document open, which is why the short probe
runs never hit it and the interactive UI runs did. Two ways round it, neither a fix: drive the
emulator without long gaps, or model the registers. The handheld does not have this problem, so the
emulator is good for the bridge and the rules and the handheld is where an interactive session gets
proven.

## New, 2026-09-02: the handheld leaves the USB bus when keys are sent back to back

It happened twice and the first explanation was wrong, so both are written down.

**First drop.** It followed a tab and an enter into the solve mode, so the solve path was the
obvious suspect: it asks Giac `solve(...)` rather than `diff(...)`, and the differentiate run had
just completed and been screenshotted. The calculator did not return within ten minutes and needed a
manual restart.

**That was falsified rather than confirmed.** A probe document ran the solve path in four separate
pieces, each armed and painted before it fired so a freeze would name the guilty call: our solver
alone, Giac's `simplify`, the exact `solve((((2*x)+5))=(13),x)` the adapter builds, and the full
cross-checked solve. All four completed. Giac returns `[(4)]` for the solve, which our parser
refuses as malformed, which is the handled path.

**Second drop, and the actual common factor.** Nine `nsptool.new key enter` calls chained in one
shell command dropped the device after three, and the third had just run our solver with no Giac
call in it at all. Every sequence that survived had something else between the key presses, usually
a screenshot, which is a separate USB session. So the pattern is rapid back to back sessions to
keysvc rather than anything StepCAS does.

Not yet proven, because proving it costs a manual restart each time. The experiment when someone is
willing to pay for it: pace keys one per second and confirm a long sequence survives, then send two
with no gap and see whether it drops. If it does, keysvc or the link service needs a settle between
sessions, and the tool should enforce it rather than leaving it to whoever is typing.

## New, 2026-09-03: a typed adapter at Giac's gen level, now that Giac is linked in

Not a question so much as a door that opened. Every backend call today prints our AST to a Giac
command string, Giac parses it, computes, prints its answer, and our adapter parses that back. Two
prints and two parses per call, and every typed guarantee in PRD 12.1 and 12.3 is the adapter's to
recover from text, which is why a solve reply arriving as [[4]] once cost every cross-check silently.

With Ki V4 that round trip through text is no longer forced. Giac's own gen type is in the same
image, so the adapter could build a gen and read a gen. What that buys: no printing, no reparsing, no
reply that has to be told apart from an error message by matching words, and the vector work gets the
struct CartesianVector { giac::gen X, Y, Z; } shape it wants instead of building and reading lists as
strings.

What it costs, and why this is an open item rather than a plan. The adapter is the trust boundary,
and a gen is a much wider surface than a string: refusing a malformed reply is a scanner today and
would become a type walk. Giac is built -fno-exceptions -DNO_STDEXCEPT, so a gen operation that
would have thrown does something else, and what it does needs finding out before anything depends on
it. And the host tests cannot see any of it, since giac_caseval only exists on the device, so a
typed adapter needs an on-device test story that the string adapter never needed.

Worth doing before the vector families, since they are the first work whose shape suffers from the
text round trip rather than merely paying for it.

**Settled 2026-09-03, done: src/giac_typed.cc.** All three costs turned out to have answers. The
gen surface is walked by type tag with everything unrecognised refused by name, which is narrower
than the text scanner it replaced, not wider. -DNO_STDEXCEPT returns an undef gen on failure, found
structurally by is_undef, so nothing needs to be told apart by matching words any more. The on-device
test story is typed_differential_check: 16 allowlisted calls down both paths, compared on tag and
canonical form, reached with `!t` and written to /documents/ndless/typedcheck.txt.tns. All 16 agree.
The string path stays as the fallback for Ki V3, which reaches Giac across a Lua call that cannot
carry a gen. See ki-v4.md.

## Closed 2026-09-04: short solves now observe cancellation

Every Meter now polls when it is constructed and keeps the 64 rewrite stride for later work. The
shared fix covers linear solve, differentiation, integration and kinematics without adding a poll to
their hot paths. Tests require one-hop and two-hop kinematics to return cancelled with no answer or
partial derivation, and short tests cover the other three engines.

The ARM bridge supplies a keypad-matrix poll for Escape to every native entry point and skips a
later Giac cross-check after cancellation or a resource halt. The sanitizer bridge harness sends
a pressed key and proves all four entry points return before making a Giac call. Giac still exposes
no cooperative interrupt here, so a call already executing inside Giac cannot be preempted. The
final module runs normally in the emulator, but the automated held-Escape gesture opened the OS
Press-to-Test dialog.
Live cancellation through the production UI remains device evidence to collect.

## New, 2026-09-03: the debugger's ln svc line limit reads as a missing service

The Firebird fork's ln svc takes a debugger command line of at most 255 bytes. A packet of 24 key
records is 302 bytes on the line and comes back as "Link svc failed: the calculator refused the
service or never answered", with no reply line at all. That is the same output keysvc produces when it
is not resident, so the first reading was that keysvc had stopped working. Twelve records fit and are
accepted. keysvc itself reads up to 512 bytes, so the limit is the debugger's rather than the
service's.

Worth fixing in the fork, since the message names the wrong side. Until then, keep a keysvc packet at
twelve records or fewer.

## Closed 2026-09-04: the kinematics solver is one hop, and that is fine only while there is one family

Closed by writing the search rather than by waiting for the second family. solve_kinematics now
chains backward from the unknown, deepens one hop at a time to a bound of three, and drops a branch
that needs more quantities than there are hops left. The probe below answers v = 11 m/s on the
calculator, through v0 = -1 m/s from the displacement equation, and the plan step says so before the
steps start. The reasoning that decided the ordering is kept below.

solve_kinematics picks a single equation. It substitutes the knowns into each of the four
constant-acceleration equations and takes the first the linear solver can solve, so a problem needing
two equations chained refuses. Measured with a scratch probe rather than assumed:

```
find v; x = 20 m; t = 4 s; a = 3 m/s^2
  no applicable equation
```

The answer is 11 m/s by way of v0 from the displacement equation. The refusal message is honest and
names each equation with the solver's own reason, which is why this is a missing capability rather
than a defect.

Within SUVAT the cost is close to nothing, because four equations over five quantities put every
single unknown one hop from any three givens. It becomes the main limit the moment a second family
arrives. Andes solves this with a state-space search over problem solving methods, and
solution_tracer with a backward chain from the sought variable that prunes routes never reaching the
givens. Both are written up in the Andes and solution_tracer section of ki-v4-plan.md. The argument
there is that the chaining search should land before the second family of equations rather than
after, because a one-hop solver given forces or energy refuses most of what it appears to cover.

## New, 2026-09-03: three decisions the agent pack raises

Full reconciliation in spec-reconciliation.md. Three items there need a call from the maintainer rather than a
task, because each changes the shape of code that already works.

**C++20.** The spec requires it, the tree is C++11, and switching is measured as nearly free: the
installed toolchain is arm-none-eabi-g++ 14.2.0, and all twelve core sources compile clean for both
the host and the ARM target under -std=c++20 -Os -marm -fno-exceptions -fno-rtti with no edits and no
warnings. The reason to take it is not conformance. It is that std::span over a contiguous child pool
is the natural shape for the next item, and every traversal written under C++11 first is one more to
convert later.

**The child pool.** src/ast.h line 41 gives each Node its own std::vector<NodeId> for children, so
every node with children heap-allocates. The spec forbids per-expression new and delete on target and
asks for a contiguous child pool with an offset and a count, which is what its own ExprNode sketch
shows. This is the one architectural conflict that costs something at runtime rather than costing
conformance. It touches every traversal, so it wants its own task and its own before and after
numbers on the device.

**Precision metadata.** Absent from the tree entirely and required by the pack's M1. There is no
distinction between an exact value and a measured one, no significant-digit count, and no final-only
rounding. A physics answer that cannot say how many figures it is entitled to is not finished, and
the corpus teaches this from chapter 1. This is new work rather than a change to existing work, so it
needs a slot in the plan rather than a decision.

## New 2026-09-04: capability identity, and 2026-09-05: its enforcement

The capability manifest is now present for the host, probe, split and unified artifacts. The device
manifests support exactly the CX II non-CAS on OS 6.2.0.333 and 6.4.0.74 with Ndless r2022. The
unified manifest records bundled Giac 1.9.0 and five compiled modules. Its identifier is generated
from the build inputs rather than written by hand.

Every packaged module and document has an externally verified SHA-256 sidecar. The manifest object
occupies 1,165 bytes, with zero data bytes, zero BSS bytes and no constructors. Firebird displayed
the unified build fingerprint ee65a3bb59d1...60e975148537, and the deployed module and document
fetched back byte for byte.

The sentence that stood here said the runtime reads and enforces none of those sidecars. That stopped
being true in e5d46c2, where main gained a check of its own package against its sidecar before it
registers anything. What was actually missing is narrower and was closed on 2026-09-05: the digest
step used Giac's sha256 and so existed only in the unified image, where no test on any target could
execute it. It is our own SHA-256 now, one implementation for both targets, and each refusal is
proven by corrupting a real package. See the 2026-09-05 entry in nps/STATUS.md.

Three things are still open rather than done. Only the unified artifact verifies itself, so the split
module and the probe carry sidecars nothing reads. The document sidecar has no reader and probably
cannot have one, since the Nspire Lua sandbox gives a document no file access. And the sidecar has to
be deployed beside the module or a valid install refuses to load, which nothing in the deploy path
currently guarantees.

## Next experiment

Milestone 0's functional exit condition is met: on 2026-09-02 the derivative of x squared sin x was
entered, evaluated through Giac and displayed on the physical handheld, with the cross-check
reporting agreement. Milestone 0 as a whole is not complete because its measurement work remains:

1. ~~Free heap with StepCAS and Giac both resident.~~ **Done on the emulator 2026-09-05**, in
   nps/benchmarks/BUDGETS.md. 18.4 MiB free at launch, 13.8 MiB of it contiguous, against 28.4 MiB
   with nothing of ours loaded. The headroom function's perturbation is settled too: it restores the
   heap it took. What is still open under this heading is three things rather than one. Its latency
   is measured only on the emulator, where turbo mode and a 1 ms timer put it somewhere between 10
   and 30 microseconds, so on target it is unverified. End-to-end peak memory is not measured at all,
   and cannot be by sampling free heap between operations. And **no reading of any of this can come
   from the handheld while StepCAS is resident**, because only the debugger's exec keeps a Lua
   document alive across running a program and the handheld has no exec: opening a program from the
   file browser tears the Lua state down first, measured both ways. The measurement has to come from
   inside the release module.
2. ~~First-step and total latency for the two worked examples on the handheld.~~ Done 2026-09-03,
   item 8 above: 20 ms and 10 ms.
3. Stack depth and recursion limits, item 9 below. Not from the startup folder: a faulting startup
   program is a reboot loop.
4. Settle the USB drop above before trusting an unattended hardware session.
