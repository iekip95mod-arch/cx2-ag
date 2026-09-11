# StepCAS resource budgets and measurement history

## Current qualification status, 2026-09-08

Version 1 below is a retained proposal. Its emulator measurements, historical handheld examples and artifact hashes remain useful evidence for those builds. They do not freeze release budgets for the current package. PERF-010 and PERF-011 still require qualification against the complete release configuration on the handheld, including the UI and Giac.

The latest loaded package in this task is release-9cf6eab9. Its September 8 hardware check below supersedes the installation state in earlier sections. The unopened package, saved document and previous installation are retained separately.

Use the current CMake commands in [../README.md](../README.md). A separate ARM configuration with NPS_RESOURCE_PROFILE=1 enables the allocation and render-ready diagnostic build. The current Lua boundary and UI support these reports. The old diagnostic-manifest rejection described below is historical. Diagnostic instrumentation and host allocator fixtures do not substitute for measurements of the release package.

Reports distinguish failed requests, unavailable solver counters and operation mismatches. Preserve those states as recorded. Do not turn unavailable fields into zero measurements. Sampled free heap and contiguous headroom are different quantities from peak live allocation. Keep the report's exclusions when interpreting either.

Before taking measurements, verify the actual loaded module identity and its matching sidecar, the UI package, runtime revision and boot. Check for duplicate module basenames. Record visible execution and compare results against the complete acceptance corpus. Measure first-step latency separately from total latency and measure cancellation response on the handheld. Freeze each budget with its tested configuration, workload, units, acceptance threshold and supporting artifact identities.

The renamed runtime was staged and read back during the 2026-09-07 migration. The new startup service and StepCAS were later visible, but the service did not return after a hard restart. Later cleanup retained the essential /ndl and /nps installation. The physical checks below identify subsequent loaded StepCAS releases and report Ndl r2022. They did not repeat a restart or hash the resident runtime, so persistence remains unqualified. Neither earlier fault frequencies nor proposed dialog explanations establish the cause of a current transfer failure. After an uncertain transfer, establish its outcome before retrying.

The boot log of an isolated 2026-09-07 run from the current images/nspire-os.img reported Version 3.6.0.337 CE, Product 16 and Platform 2. No loaded-module device identity was captured in that run. That instance was stopped and its private copy retained. The filename does not establish a CX II or OS 6.4 measurement environment. Keep the original version labels on earlier measurements and verify each new instance before using it as evidence.

A later instance from integer-os64-seed-20260907.img returned device code 0x1d and OS 6.40.74 through the OS device-info service despite the same older boot banner. That confirms the emulator's OS identity, not loaded StepCAS identity or release performance. The Lua document probe was mistakenly sent through the native executable loader and did not establish application execution.

## Native task boundary, 2026-09-08

The native linear and rearrangement wrappers reserve 262144 bytes for coroutine frames by default in this source snapshot. Explicit overloads accept a caller-selected capacity. SolveTask requires its capacity at construction and owns that storage across advances. Its resource report gives exact frame capacity, live and peak bytes, live frames, AST counts and metered work. Total retained allocation is not measured. Parsing, canonicalization and sampling remain atomic under structural limits. Checkpoint counts do not establish elapsed slice limits.

The native foundation passed scoped independent review and a full ASan and UBSan host check against the matching host Giac archive. External numeric and system libraries remain outside that instrumentation. Its ARM package passed the offline audit and is retained as release-solve-task-0bedd3e9 in /private/tmp/cx2-continuation-LPC7N8. Module SHA-256 is 0bedd3e9ee932d3cac2f4c074ff537701e44f8e9f63525f5d10d2f5b262aa80b. This package was not uploaded.

Subsequent hardening repairs identifier admission, terminal provenance and retained conditional prefixes. Focused normal and sanitizer checks cover malformed input, Unicode identifier normalization, cancellation and frame or AST failure after publication. Overlay paint-error checks cover retained input and history, repaint and restored focus without another solve. Scoped independent review passed for this code. The combined ASan and UBSan host check passed all 33 groups and a separate fuzz run passed 10000 cases at seed 1. External numeric and system libraries remain outside that instrumentation.

The hardening ARM package passed its offline audit and is retained as release-hardening-b498761c beside the earlier foundation package. Module SHA-256 is b498761c2f91728a64812c6a27a58ac439895cda58be9d1f9ad2eb77e1e32f40. UI SHA-256 is 53ac63eafe13da85517309ec7cfcf4dcdf5dc7a8e29daa91bddc922bd21c0f8f. Matching sidecars are retained. All four files were subsequently transferred and the module read-back matched. The next sidecar export timed out and loaded qualification was interrupted. Backups and screenshots are retained in /private/tmp/cx2-hardening-install-ABk4L0. PERF-002 and complete release resource qualification remain open.

## Bug-pass device inspection, 2026-09-08

The later inspection began at Home and found a different installed module, 4141905 bytes with SHA-256 cb4fd9bab82848d3c0bfdb821e67706a6b9460f8692a8dc5fba7c31b5f6f0707. A fresh download matched its sidecar. That confirms file integrity only. This task did not establish its loaded identity or source provenance. Separate deployment artifacts exist under /private/tmp/cx2-mvp-device and were preserved.

UI export failed because the returned document was larger than its advertised size, 40034 versus 39523 bytes. No complete UI read-back was established in that initial inspection. The cause remains unresolved. The Document Sent dialog was dismissed, Home was captured and USB was released. No upload, deletion, restart or solve was performed during this inspection. Evidence is /private/tmp/cx2-bug-pass-oQvNKQ/DEVICE-STATE.md. The subsequent hardware check below backed up that installation before replacing it.

## Bug-pass host regressions, 2026-09-08

The September 8 bug pass repaired linear decimal-limit classification, missing original rearrangement domains, native cleanup after inherited table lookup errors and UI history, overlay and menu-insertion faults. The core regression run initially failed 24 checks. The final native suite passed 8378 checks normally and 8384 under ASan and UBSan. Controls cover synchronous and persistent solves, cache replay, cancellation, retained conditions and immutable published records. Logarithmic sampling retains its existing unchecked status.

The final V4 tests failed 29 of 3597 checks against the old Lua source and passed all 3597 against the repair. The same bridge tests failed 24 of 764 against the old module and passed all 764 against the repair. A separate native allocation probe with C++ exceptions disabled retained zero bytes after repeated lookup errors across all seven structured entry points. Inherited fields remain supported. Lookup errors become typed input refusals. These tests do not establish general allocation-failure safety or physical UI behavior.

Evidence is retained under /private/tmp/cx2-bug-pass-oQvNKQ, /private/tmp/cx2-core-repair-oiuwCh and /private/tmp/cx2-ui-repair-final. The combined release checks and independent review are recorded separately from these focused author results.

## Bug-pass release and hardware check, 2026-09-08

The combined ASan and UBSan check with actual Giac passed all 33 groups in 460.26 seconds. A separate seed 1 fuzz run passed 10000 cases with zero failures. Independent review of the frozen 14-file batch passed 25 sanitizer groups plus separate cancellation, decimal-limit, inherited-field cleanup and UI controls. It reproduced the original failures on the baseline and found no blocking candidate defect. The review is /private/tmp/cx2-bug-review-REVIEW.md. An additional baseline-only sanitizer run aborted with stack overflow and was not diagnosed or attributed to this repair.

The fresh ARM package passed its offline audit. Module, unopened UI and both sidecars were uploaded to the physical CX II non-CAS on OS 6.40.74. All four downloads matched the archive byte for byte before opening. Full Text displayed loaded identity stepcas.unified.inputs-sha256.f82da59361748ebd8d38c1742ac66733c5cdbeb046e7e3918ad75a99383fae39, Giac 1.9.0 and Ndl r2022.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Native module | 4137865 | 9cf6eab9cc74c883da256ba36d48c051c24e7c4bd20229458ac0fdb19172a372 |
| Unopened UI document | 39474 | fddfd7fc2ca8efd27e6b6cda05171eef706148cf706d4d60918632ea66b10403 |
| Saved document after checks | 40110 | 4e226d4b02e11607ba1bed1fac899613f6977ba9244099ca4ebff43d2e0ef7fd |

| Physical check | Visible observation |
| --- | --- |
| Ordinary evaluation | 2+3 returned 5 |
| Clear History with pending input | The earlier result disappeared while 12345 and native input focus remained |
| Template insertion while Full Text was open | The reader remained open and returning to the shell showed unchanged 12345 with guidance to close Full Text first |
| Linear walkthrough | solve(2*x+3=7,x) returned exact 2, four steps and Giac agreement |
| Steps replaced by Guided Physics | T opened the selected cubic-unit example and Escape returned to Physics, then to a focused shell |
| Original rearrangement domain | rearrange(x+1/y=z,x) produced a conditional result. Both plan detail and Full Text displayed Requires: y is not zero |
| Rearrangement work detail | Do, Write and Why remained visible, including the typeset equation and scroll access |

The first rearrangement attempt had misplaced parentheses from remote cursor navigation and was rejected. Full Text confirmed the corrected input before submission. The saved history retains both attempts. No correctness claim is based on the malformed attempt.

Evidence is /private/tmp/cx2-bug-pass-oQvNKQ. The package archive is release-9cf6eab9. Screenshots include candidate-loaded-identity.png, clear-history-preserved-input.png, template-guard-preserved-input.png, linear-result.png, physics-reader-observed.png, reader-return-physics.png, physics-return-shell.png, rearrange-plan-detail.png, rearrange-domain-fulltext.png and rearrange-work-detail.png. The saved export is bug-pass-saved-ui.tns. bug-pass-final-visible.png confirms the transfer dialog was dismissed before USB release. The UI sidecar still names the unopened document, whose bytes differ after saving.

Before replacement, live-native.tns and live-native-sidecar.tns preserved the previous module. Successful later exports preserved the 40034-byte UI as before-validation-ui.tns and before-validation-saved-ui.tns with its original sidecar. The earlier failed size-mismatch export remains a failure. Other task artifacts and recovery material were retained.

Screenshot and key timeouts recurred. Reconnection and fresh screenshots established visible state before any new key action. The tests do not identify the timeout cause or establish transport reliability. No restart or resident-runtime hash was taken. Direct keypad behavior, cold-boot persistence, full-corpus correctness and release performance budgets remain unqualified. Decimal-limit and inherited-field cleanup fault injection have host evidence only. This physical run exercised the deployed UI and ordinary solver behavior without injecting those internal faults.

## Canonical depth and idle USB repairs, 2026-09-08

The baseline stack overflow above was reproduced in canonical normalization with 2048 nested negations admitted by an Arena depth limit of 4096. Direct sanitizer execution overflowed the ordinary process stack while CTest's enlarged stack hid the failure. A dedicated 2 MiB worker reproduced the original crash. The replacement traversal passed six deep expression shapes, idempotence and structural ordering on a 128 KiB worker stack. The focused suite passed 8383 checks normally and 8389 under ASan and UBSan, including direct execution. Four available C++20 frontends accepted the changed translation unit. Formal checking was unavailable. This is host evidence for callers that raise the structural depth limit, not evidence that the handheld editor admits that input depth.

Canonical normalization, associative flattening and structural ordering now keep pending traversal on the heap. The change preserves raw associative folding order, collection shape and existing Arena limits. The retained failure and candidate logs are in /private/tmp/cx2-core-followup-X9RquG. The earlier fork-based stack-limit probe did not expose the original failure and is not counted as a working guard.

Independent review confirmed 8389 sanitizer checks, the failing original stack guards and 6000 identical canonical forms and resource outcomes across baseline and repair. The combined actual-Giac sanitizer suite passed all 33 groups in 380.04 seconds. A separate seed 1 fuzz run passed 10000 cases with zero failures. The fresh ARM package passed its offline audit.

On the physical CX II non-CAS running OS 6.40.74, both the existing and freshly rebuilt original host tools failed their first screenshot after three seconds idle. The request write succeeded but no stream acknowledgment arrived within five seconds. Immediate serial requests and fresh connections passed. Reopening the CX II transport before a new service after one second idle passed paced screenshots and information requests at 0.5, 3, 7 and 12 seconds. An additional untraced information request and screenshot after three seconds idle passed. These observations support the idle refresh policy without identifying the calculator firmware's internal cause or explaining every earlier timeout.

The shared service boundary prepares the transport before dispatch. Busy services and failed refreshes do not send the application request. The original transport failed the new refresh regression. The first candidate passed 34 ASan and UBSan transport groups with both Apple and non-Apple USB paths, 104 service cases across 13 APIs, 36 standard-key contracts, 18 file diagnostics and the file and screenshot suites. The CLI session suite passed all 27 tests. These are host fault-injection results. Physical USB qualification here is limited to macOS. Logs and screenshots are retained in /private/tmp/cx2-transport-followup-7a60rulg.

Independent review found that the first refresh candidate could select another calculator with the same product ID. The replacement retains the original libusb device object across close and reopen. Missing devices and reset-driven reenumeration during refresh are refused before application dispatch. The added identity regression reproduced four failures before this repair, covering reordered devices, an absent original, a replacement at the same port and missing port metadata. Evidence is /private/tmp/cx2-usb-identity-kofz1e. The earlier review remains retained as REVIEW.md in the core evidence directory.

The final author run passed 36 transport groups under ASan and UBSan for both USB paths. The added reset-failure control checked disconnection before application dispatch, no topology search, balanced device references and single cleanup. The Apple branch skips that reset-specific control because its CX II acquisition path does not reset USB.

## Physical package check, 2026-09-08

The reviewed determinant release from 35d7088 was uploaded to a TI-Nspire CX II non-CAS reporting code 0x1d and OS 6.40.74. Before upload, every directory in the transfer namespace was listed and no duplicate StepCAS module basename was found. The previous module, sidecars and saved document are retained in /private/tmp/cx2-continuation-LPC7N8. The earlier full backup remains unchanged.

Read-back before opening matched the release package and both sidecars. Full Text then displayed the loaded identity stepcas.unified.inputs-sha256.4777a64d28cc7ed1308f6abe469c0f215172f885ce6111f1b27c67ea4305109d, Giac 1.9.0 and Ndl r2022. This identifies the loaded StepCAS build and reported runtime revision. It does not hash the resident runtime or prove cold-boot persistence.

| Artifact | SHA-256 |
| --- | --- |
| Native module | 90efe7d92a8bdf6d1832d711d582e64f2d70b964cc7281ac552e8c841879e063 |
| Unopened UI document | 4360eda1cd9be0ef378595e5b7a4c371b9ba64c6cfea46d9186452de6a4a72b7 |

The determinant of [[1,2],[3,4]] returned exact -2. Its row-operation detail displayed Do, Write and Why with the matrix in flat bracket notation. The saved history entry rendered a two-dimensional matrix. Selecting Number, Euclidean Quotient and completing iquo(19,4) returned exact 4. Actions, Read Full Text opened the retained quotient explanation and arrow scrolling exposed its verification text. These are individual remotely entered examples, not the full supported corpus or direct keypad qualification.

The same run exposed a viewer-input fault. T and Tab did not perform their advertised viewer actions, and a later shell submission contained text entered while the viewer was active. Screenshots, key-service queries and one sidecar write also timed out intermittently. Read-back established that the failed write left the old sidecar before it was replaced. No uncertain write was replayed. The transfer failures do not establish a cause in the application or USB library.

A later bounded probe passed 41 serial requests across the existing and directory-capable host binaries. It covered device info, helper queries, screenshots, an idle interval and screenshots after helper keys. The traces contain no timeout, service-close failure or packet-limit marker. This did not reproduce the earlier failures or establish visible effects for every accepted key. The observations and traces are retained beside the screenshots.

Evidence is retained in /private/tmp/cx2-continuation-LPC7N8, including release-identity.png, determinant-result.png, determinant-row-detail.png, quotient-result.png, quotient-menu-fulltext.png and quotient-fulltext-scroll.png. No restart or disconnected solve was performed in this check. Cold and warm starts, complete workloads, corruption, cancellation, repeated-solve memory behavior and frozen performance budgets remain unqualified.

## Native input repair check, 2026-09-08

Commit ea49c10 routes parked native-editor filters to the active walkthrough, physics browser or Full Text reader. Independent matched host runs reproduced 262 failing checks on 76b7524 and none in 3712 candidate checks. A fresh targeted sanitizer run passed all four selected CTest tests, including 3715 V4 checks with actual Giac records. Another 24 injected callback-error assertions passed. The review is retained as UI-REVIEW.md in /private/tmp/cx2-continuation-LPC7N8. This is scoped qualification, not a complete release-suite pass.

The fresh ARM package passed its offline audit. Module, unopened document and both sidecars were uploaded and read back before opening. Full Text displayed stepcas.unified.inputs-sha256.3ff4af3eac277fbad331eb4ec0f389fa169d9baaa6f1499c6a527cdb50d33cbc, Giac 1.9.0 and Ndl r2022 on the same handheld.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Native module | 4124013 | 10832d17d32f1b79e7c08ad3ae26440ee434ec336084ee831fec8bd39546dd46 |
| Unopened UI document | 39529 | dc1a060e3f780460f5962156ae9e1baa53df852c747caf40617f7b668859ca7a |
| Saved document after examples | 40299 | e322989be91428a7a34afc37248de3654cc0bf676b4d07463fcdc6ee33b50952 |

Number, Euclidean Quotient with iquo(19,4) initially withheld the answer in hint mode. Tab revealed exact 4. T opened Full Text from the list, and Tab advanced the reader from lines 1 through 12 to lines 9 through 20. Clear, Delete, Backspace and a typed z in the reader left the shell entry blank and preserved its history. Returning to normal input, 2+3 returned 5 without stray shortcut text.

Algebra, Solve with solve(2*x+3=7,x) returned exact 2, four steps and Giac agreement. Its Work detail showed Do, Write and Why. T opened that detail in Full Text. Physics, Find final speed from acceleration and time returned 17 m/s from v0 = 5 m/s, a = 3 m/s^2 and t = 4 s. Its eight-step walkthrough displayed dimensional checks, the acceleration and motion assumptions, and a typeset unit fraction. T also opened the selected Guided Physics browser description. The determinant example again returned -2 with five steps. Matrix notation in its step detail remained flat, while history used a two-dimensional matrix.

One screenshot and one Enter request timed out during this check. The screenshot was observed again without replaying the shortcut. After the Enter timeout, two separate screenshots showed 2+3 still unsubmitted before a new Enter completed it. No transport repair is claimed. Remote standard-key and helper input were exercised, not direct physical keypad input.

Retained evidence includes repaired-identity.png, repaired-hint-withheld.png, repaired-hint-tab.png, repaired-t-fulltext-observed.png, repaired-reader-tab.png, repaired-return-shell.png, repaired-shell-five.png, repaired-linear-work.png, repaired-detail-t-reader.png, repaired-physics-result.png, repaired-physics-detail.png, repaired-physics-browser-reader.png and repaired-determinant-row.png. The saved document and final shell screenshot are repaired-saved-walkthroughs.tns and repaired-final-visible.png. The device document is saved, so its current bytes differ from the unopened package sidecar. Both versions are retained.

The affected shortcuts now have visible handheld evidence. Full-family correctness, two-dimensional matrix steps, direct keypad behavior, offline operation, cold starts, cancellation, repeated-solve memory limits and release performance budgets remain unqualified.

## Equation opening check, 2026-09-08

Commit 61d6f4b uses a fresh synchronous native measurement during the same paint. Pending, stale and oversized measurements still select complete text fallback. Independent first-paint tests reproduced three failures on 5f08bf4, then passed all 3532 candidate checks. The retained native-module run with actual Giac records passed 3721 checks. Twenty additional independent assertions covered delayed smaller-font completion, stale callbacks, bounded measurements and oversized fallback. RENDER-REVIEW.md records this scoped host approval.

Before repair, switching from determinant step 3 back to step 2 showed flat bracket text. One Down press then showed the same expression as a native matrix. The retained comparison is matrix-repaint-return-cold.png and matrix-repaint-return-warm.png. This supports first-measurement deferral. It does not identify the OS scheduling rule responsible for repainting.

The reviewed package is archived in /private/tmp/cx2-continuation-LPC7N8/release-render-683a1a0d. Its ARM offline audit passed. The module, unopened document and both sidecars read back exactly before opening. Full Text displayed stepcas.unified.inputs-sha256.683a1a0d698c69b63cde4876a0ff1eb6d9ea66bf7b0a31033ee7d4853a957665, Giac 1.9.0 and Ndl r2022.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Native module | 4124026 | 8926118cced23cbb4e9a0118d30a4e491180f2c4a448495f0297053181595c7b |
| Unopened UI document | 39305 | 33e16b8e60aed0a55c75bcacc8450e6e757f234a2b43aa3f5cf8162a8ab37479 |
| Saved document after examples | 39892 | f73be5134acf767a195ace8ec7717e12ff7eff454a95ff5862403aaee7a4034b |

After a fresh document launch, det([[1,2],[3,4]]) returned -2. The first screenshot after opening its row-operation detail showed a native two-dimensional matrix without another scroll or revisit. The next scaling step also displayed its matrix and determinant factor on first opening. Scrolling retained access to Why. These visible observations support the repaired opening behavior, not an instrumented count of physical paint callbacks.

Menu-entered solve(2*x+1=2,x) returned an exact expression equal to one half, with Giac agreement. Its answer and Write equation rendered immediately, but retained the canonical notation 1 times 2 to the power -1. Familiar fraction notation remains separate presentation work. T opened the full explanation. The document was saved and exported, its transfer dialog was dismissed and the final shell remained visible before releasing USB.

Evidence includes render-identity.png, render-determinant-first-detail.png, render-determinant-why.png, render-scale-first-detail.png, render-fraction-first-list.png, render-fraction-first-detail.png, render-fraction-fulltext.png and render-final-visible.png in /private/tmp/cx2-continuation-LPC7N8. The saved file is render-saved-walkthroughs.tns. Earlier packages, saved documents and recovery files remain retained. Helper-query and screenshot timeouts recurred, so transport reliability is not qualified. The existing full-corpus, direct-keypad, offline, restart, cancellation and resource-budget obligations remain open.

## Handheld inventory check, 2026-09-08

A fresh physical sweep inspected every directory exposed by document transfer on CX II non-CAS OS 6.40.74. The visible installation contains /ndl with its resources, persistence document, installer and startup/keysvc.tns, plus /nps with the saved V4 document and runtime module. Both StepCAS sidecars remain present. Other files occupy the standard MyLib, MyWidgets, PyLib and Examples locations, plus themes.csv and the empty OS diagnostic archive. No retired ndless folder, duplicate module or old probe was found. This sweep does not inventory native /appdata storage.

Freshly downloaded module bytes match SHA-256 8926118cced23cbb4e9a0118d30a4e491180f2c4a448495f0297053181595c7b and its sidecar, identifying the installed rendering release from 61d6f4b. The saved document read in this pass is 39892 bytes with SHA-256 3b827777589dc877067f43be230d5b780055fc781c2d177fbecd95ebb6cf7dd2. It differs from the earlier saved export and this pass did not establish why. The unchanged UI sidecar still identifies the unopened package. Preserve these artifact distinctions when inspecting a saved document. Fresh downloads, sidecars and screenshot hashes are retained in /private/tmp/cx2-device-cleanup-rIdEEE.

The active keysvc log was retained because the resident service keeps its file open. The only deletion was the empty NspireLogs.zip. Its attempted export timed out and produced no host copy. Deletion was acknowledged, but the next listing already showed the empty archive again. No repeat deletion was attempted and the final visible file set was unchanged. Final directory checks preserved the installation sizes. Screenshots verified the deletion dialog and its dismissal back to Home before USB was released. No application launch, package upload, restart or hidden-storage operation occurred.

## Retained version 1 record

Commands and device filenames in the remaining sections are historical. Follow the current README for build and deployment commands. Preserve the original measurements and hashes when appending new qualification evidence.

PERF-010 asks Milestone 0 to freeze versioned budgets for ten quantities and to document the harness
that measured them. This file is that. Six of the ten have a measurement behind them, four do not,
and the four say so rather than carrying a number nobody took.

Every figure below is an emulator figure unless it names the handheld. Firebird headless running
images/nspire-os.img, a CX II non-CAS on OS 6.4.0.74 with Ndless r2022. PRD section 19.6 wants the
numbers that qualify a release to come from physical hardware, so none of these qualify a release
yet. They are the first real numbers and they replace guesses.

## The build these were measured against

| Artifact | Bytes | sha256 |
|---|---|---|
| nps_nspire.luax.tns, in-module readings | 3997510 | f92bd56ea57891209cad365c758c2... |
| nps_v4.tns, in-module readings | 26152 | 1c4f190b11060cc067921d6a423ed... |
| nps_nspire.luax.tns, outside-probe readings | 3996377 | 5fd9cad300e0dbe4d9fc4d1232c5c2eb02086dc275078ae465582f014f2ee67c |
| nps_v4.tns, outside-probe readings | 25856 | b8cb08475383ec76f7120691f72766e4382ff5842f7cf8203318f67cb5640215 |

The two module builds differ by the heap_free binding and the document by the calls that display it.
That is why the outside-probe rows are kept separate rather than averaged in.

Application version nps 0.2. The module was fetched back off the calculator and compared byte for
byte against the build before any reading was taken, because a send can report success and still
have failed.

**Read that check with the fetch's own fault rate in mind, which nobody knew until 2026-09-06.**
nsptool get returns the wrong bytes on a large file about one time in four, with the right byte count
and no error, so a read-back that disagrees is as likely to be the fetch as the send. The method is
to fetch until two results agree, and to check the agreed result against the sidecar wherever one
exists. The fault has a shape worth recognising rather than rediscovering: the bad result is exactly
the first 1439 bytes of the file followed by the file again from its start, truncated at the end by
that same 1439 so the length never changes. 1439 is one maxsize chunk in
libnspire-src/src/services/file.c, the packet data size less one. Anything matching that shape is the
transport, not the calculator. The full account is in the stale-reading section below.

## Free heap, measured

Two instruments measure this. The release module reports it from inside (nps_nspire.heap_free, shown
on screen by nps_v4.lua), and benchmarks/heap_probe.c measures the same OS heap from a separate
Ndless program. The in-module figures are the ones that count, because they are the only ones the
handheld can ever produce.

**Every total below is a lower bound rather than a measurement, and the slack is wider than it
looks.** measure_total_free takes the largest block it can get and repeats, but the bisection inside
it stops while its interval is still one resolution wide and returns the understated end of that
interval (include/nps/platform/nspire/measurement.h, the loop at line 329 and the return at 339). The
walk then allocates and counts exactly that understated figure, and drops whatever residue falls
below a resolution. So the shortfall is up to one resolution per block, at a default resolution of
64 KiB.

How wide that is depends on the resolution, and it is not blocks times resolution. Measured by
sweeping three resolutions on one heap, a 64 KiB reading understates by 561466 bytes where blocks
times resolution predicts 524288. The sweep section below has the numbers and the reason.

The direction is understatement either way, which is safe for a floor and wrong for a small
difference. That is what the two corrections below turn on.

**Two counts in this file mean different things and must not be divided into each other.**
greedy_total.blocks is 7 to 9, one per free run. chunked_total.kib64.chunks is 296, the number of
64 KiB allocations that succeed at once, which is larger because the method cuts one 13 MB run into
205 separate chunks. Both are measured. A total divided by a resolution is neither of them, and an
earlier version of this paragraph made exactly that mistake.

### From inside the module, one clean boot

Every row below is the same boot, taken in this order with nothing else run between them. Reading
free heap on a machine that has been running measures that machine's history as much as our program,
so a reading without its boot recorded is not a reading.

| Point | Total free | Largest block |
|---|---|---|
| Ki V4 open, Giac resident, nothing else done | 19609 KB | 13120 KB |
| After `!s 2x+5=13`, 0 ms, 16 nodes, 4 steps, 2 Giac calls | 19326 KB | 13120 KB |
| After `!kv=?,x=20m,t=4s,a=3m/s/s`, 400 ms, 100 nodes, 15 steps, 6 Giac calls | 19326 KB | 13120 KB |

**A solve costs something once and nothing after that, and it moves the largest block by less than
this instrument can see.** The second solve is the largest derivation on record, 6.25 times the nodes
of the first and a two-hop route, and it left both figures where the first solve did. So the arena
and the Lua heap grow once and are reused.

**This file has said two wrong things about that, and both are kept because of how they were wrong.**

The first version said a solve fragments the heap rather than consuming it, moving the largest block
by about 1.3 MiB while the total held. That compared a launch reading taken on one boot against
post-solve readings taken on another, so the difference was between the boots and not across the
solve.

The second version said a solve costs 283 KB and does not move the largest block at all. Same boot
this time, so the comparison was fair, but 283 KB is the difference between two lower bounds whose
shortfall on a comparable heap was measured at 561466 bytes. It is inside the noise and cannot be
quoted. The largest block survives better, because it is one search carrying at most 64 KiB of slack
rather than an accumulated one: it does not move by more than 64 KB, which is a weaker claim than
not moving.

Each version has been less dramatic than the one before it. That is what happens when the instrument
gets checked instead of the reading.

### From an outside probe, three separate boots

Kept because it is the only measurement of the nothing-loaded state, and because two independent
implementations agreeing is worth more than either alone.

| State | Largest block | Total, 1 MiB chunks | Total, 64 KiB chunks | Total, greedy |
|---|---|---|---|---|
| Home screen, nothing of ours loaded | 22282240 to 23003136 | 26214400 | 28377088 to 28442624 | 28195300 to 28464624 |
| Ki V4 open, Giac resident, no solve yet | 14483456 | 16777216 | 19333120 | 19324308 |
| Ki V4 open, Giac resident, one solve done | 12976128 to 13238272 | 16777216 | 19398656 to 19464192 | 19399437 to 19409080 |

Three runs in the first state and one in the second, each on its own boot. The two in the third state
share a boot, so treat the second of them as a warm reading.

The greedy walk took 8, 7, 9 and 8 blocks in those runs, and the 64 KiB chunk count took 433, 295,
296 and 297 chunks. Neither number was in this file before 2026-09-05, which is how a slack estimate
built on guessing at the block count survived as long as it did.

The two instruments disagree by 729 KB on total free at launch, 19609 KB from inside against 18880 KB
from the probe, and the probe is the lower one because the probe is itself a loaded program occupying
the heap it is measuring. That is the expected direction and roughly the expected size.

So StepCAS with Giac linked in, loaded into a running Lua document, costs 8.625 MiB of heap by the
probe's own before and after figures, and leaves 19609 KB free at launch, 13120 KB of it in one
contiguous block.

**The 1 MiB chunk count understates free memory, by 2.0625 MiB with nothing loaded and by 2.5 MiB
with everything resident.** probe/heap.c counted 1 MiB chunks, which is where the 25 MiB in
.Internal/open-questions.md item 4 comes from. The understatement is not the same in both states,
which is what you would expect from a count that loses whatever does not divide the free runs: the
runs differ.

The 64 KiB count and the greedy bisect-and-hold agree with each other within 65 KB in every run.
**That agreement is weaker evidence than it reads as.** Both carry the same 64 KiB granularity bound,
so their errors are correlated by construction rather than independent, and two methods short by the
same amount look exactly like two methods that are right. They are still the figures to use, because
the 1 MiB count is short by more and demonstrably so, but what stands between them is a consistency
check rather than a proof.

Both lose up to one resolution per free run, so both carry the same bound and their errors are
correlated by construction rather than independent. Two methods short by the same amount look
exactly like two methods that are right.

### The resolution sweep, 2026-09-05

Run to settle that, on one boot at the home screen with nothing of ours loaded. Same heap, three
resolutions, and a largest-block reading between each pass to prove the heap came back.

| Method | Resolution | Blocks | Total free | Stated slack bound |
|---|---|---|---|---|
| Chunked count | 1 MiB | 25 chunks | 26214400 | |
| Chunked count | 64 KiB | 428 chunks | 28049408 | |
| Greedy walk | 64 KiB | 8 | 28044491 | 524288 |
| Greedy walk | 4 KiB | 24 | 28552108 | 98304 |
| Greedy walk | 512 B | 33 | 28605957 | 16896 |

The largest block read 22413312 on all seven readings of it, before and after every pass, so each
method gave back exactly what it took.

**The correlated-error worry was right and is now measured.** At 64 KiB the two methods agree to
4917 bytes, which reads like corroboration. Drop the greedy walk to 512 bytes and it finds more than
either: 561466 bytes more than the greedy pass at 64 KiB, and 556549 more than the chunked count.
Those are two figures rather than one, differing by the same 4917 the coarse pair already differed
by. So the agreement at 64 KiB was two instruments making the same mistake.

**Every one of these numbers is a floor, including the finest.** The 512-byte pass cannot see a run
smaller than 512 bytes either, so 28605957 is itself a lower bound and the gap it reveals is a gap
between two floors rather than a distance to the truth. The defensible statement is that dropping the
resolution by a factor of 128 recovered at least half a megabyte, and nothing here says the recovery
has stopped. A fourth pass at 64 bytes would say whether the curve is flattening. The probe now runs
one, and this section will be updated when it has.

**And the slack bound this file has been quoting is itself too small.** Blocks times resolution gives
524288 at 64 KiB, but the walk was short by at least 561466, which is 37178 bytes past its own bound.
A run smaller than one resolution cannot be taken at all, so it never becomes a block and never
enters the bound. Blocks times resolution only ever bounded the remainders inside runs the walk does
take, and never the runs too small to take at all.

**What the block counts cannot tell you, and an earlier version of this section claimed they could.**
8 blocks at 64 KiB against 33 at 512 bytes leaves 25 blocks that only the finer pass found, and those
25 are two different things mixed together: whole runs smaller than 64 KiB, and leftovers inside the
eight runs the coarse pass did take, where a run of 100000 gives one 65536 block and the remaining
34464 only becomes a block at a finer resolution. Both are invisible at 64 KiB and both appear as
extra blocks. Saying the 25 were whole small runs was an inference dressed as a reading.

The smallest block taken per pass does not separate them either, because the walk now falls back to
taking exactly one resolution when the search returns less, so every pass bottoms out at its own
resolution by construction. The probe now counts blocks taken at exactly the resolution separately
from larger ones, which does separate them, and this section will say which mechanism it was once
that has run.

So the honest statement is that a reading at resolution R understates by at least the tail of runs
smaller than R, which nothing at resolution R can measure. Only a finer pass reveals it, which is
why the sweep is the instrument and a single resolution is not.

## Latency, and the hard case

The worked examples are not the shape of this problem. A budget table read without the hard case
beside it will leave the wrong impression of how much room there is.

| Problem | Total | Nodes | Steps | Giac calls | Where |
|---|---|---|---|---|---|
| d/dx of x^2*sin(x) | 20 ms | 24 | 4 | 2 | handheld, 2026-09-03, OS 6.2.0.333 |
| solve 2x + 5 = 13 | 10 ms | 16 | 4 | 2 | handheld, 2026-09-03, OS 6.2.0.333 |
| kv=?, x=20m, t=4s, a=3m/s/s | 400 ms | 100 | 15 | 6 | emulator, 2026-09-05 |

The kinematics problem is a two-hop route and the largest derivation on record. Against the
handheld's 10 ms for the linear solve it is 40 times, and those two figures came from different
machines, so read it as an indication rather than a ratio. The emulator runs in turbo mode, which
means the target figure is not known to be the smaller of the two.

Against PERF-006's five seconds the two easy cases leave a factor of 500 and 250. The hard case
leaves 12.5. An earlier version of this file said the margin was three orders of magnitude, which
overstated the easy cases and was out by a further order for the hard one.

The two emulator figures cannot be divided by each other. The linear solve reports 0 ms there, and
0 ms is below the 1 millisecond timer resolution rather than a measured zero.

Two hops is the deepest route the planner builds today. Nothing has yet measured a three-hop route,
a kinematics problem needing a quadratic, or an expression near the 4096-node limit. Each of those
is a candidate for the case that first threatens the budget.

## What the probe itself costs

The open question this closes is whether the allocator headroom estimate perturbs the heap it is
measuring, and what it costs to call.

- **Perturbation: none that this resolution can see.** One hundred consecutive calls returned the
  same answer every time, and the answer was identical before and after allocating and freeing every
  free block in the heap twice over. The probe restores what it took.
- **Eleven mallocs per call**, which is the bisection: one attempt at the 64 MiB ceiling and ten
  halvings down to 64 KiB.
- **20 microseconds per call on the emulator**, from 100 calls in 2 milliseconds. The emulator runs
  in turbo mode and the timer resolution is 1 millisecond, so read that as 10 to 30 microseconds on
  a machine that is not the target. **Target latency remains unverified.**

## Version 1 budgets

Proposed rather than frozen: PERF-010 is the maintainer's to sign off. Each row says what it rests on.

| Quantity | Budget | Basis |
|---|---|---|
| Installed size | 4.5 MiB for the module, the document and both sidecars together | 4059102 bytes at commit da32f06, measured 2026-09-06, leaving 13.98 per cent of the budget as headroom. Module 4030186, document 28753, sidecars 86 and 77. Two earlier readings: 4055530 at 0f3a521 the same day, and 4048605 at 83df4ce on 2026-09-05. Every reading comes from a worktree of the named commit rather than from the shared tree, so each belongs to a hash and the next is a delta rather than a fresh guess. The recipe is below. The rate is the thing to watch: 23449 bytes across one day, then 6925 across seven commits, then 3572 across seven more, and nothing tracks that but this line |
| Launch-time free memory | leave at least 8 MiB total free and 4 MiB contiguous | measured 19609 KB free and 13120 KB contiguous at launch with everything resident, from inside the module, so the budget is roughly twice the measured margin |
| End-to-end peak memory | not set | not measured, see below |
| AST size | 4096 nodes, unchanged | enforced today by Limits.max_nodes. The largest recorded solve used 100 nodes, so the bound has better than an order of magnitude of room |
| Derivation steps | 512, unchanged | enforced by Budget.max_steps. Largest recorded derivation is 15 steps |
| Branches | 64, provisional | enforced by Budget.max_branches through Derivation::add_branch, which takes the meter and returns kNoStep once the budget is spent, so the first branching engine is bounded by construction rather than by whoever remembers to count. The number is not measured: no engine emits a branch yet, so every recorded solve used zero, and 64 is a placeholder of the same kind max_rewrites and max_steps were before hardware replaced them |
| Repeated canonical states | no number to set | not a budget with a threshold. Meter::reached refuses the second arrival at a state it has already seen, so the halt is a revisit detector and there is nothing to freeze. It is called from rewrite.cc twice and rearrange.cc once, but no rule in the tree cycles, so it has never fired on real work and the only thing that has exercised the failure path is a unit test driving the meter directly. Cost::states reports the distinct states a solve passed through, which is what a size cap would have to be sized against if one is ever wanted |
| Backend calls | 32, unchanged | enforced by Budget.max_backend_calls. Largest recorded solve made 6 |
| First-step latency | PERF-005's two seconds stands | not separately instrumented |
| Total latency | PERF-006's five seconds stands, with one order of magnitude of margin rather than three | see the latency section above. The hard case is the two-hop kinematics solve at 400 ms on the emulator, against 10 ms and 20 ms on the handheld for the worked examples |
| Cancellation response | 64 rewrites between polls, unchanged | enforced by Meter and covered by host tests. No on-device timing |

## Measuring the installed size against a hash

The shared checkout cannot answer this. Every lane builds against a working tree carrying every other
lane's uncommitted work, so a size taken there belongs to no commit and nobody can take it again. The
number in the table above comes from a detached worktree instead.

    git worktree add --detach /tmp/nps-size <commit>
    cmake -S /tmp/nps-size/nps -B /tmp/nps-build -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndless-arm926ej-s.cmake \
          -DGIAC_ROOT=<repo>/khi-src -DNDLESS_SDK=<repo>/ndless-src/ndless-sdk \
          -DNPS_GIAC_OBJECT_DIR=<a populated giac-objects directory>
    cmake --build /tmp/nps-build --target unified

The populated directory on this machine is .giac-objects-shared at the repository root. As of
2026-09-06 it holds 56 object files and 56 dependency files.

**Pointing a build at it can rebuild into it rather than borrow from it, and the 0f3a521 reading
did.** NPS_GIAC_OBJECT_DIR is passed straight through as OBJDIR to a make run in khi-src
(nps/CMakeLists.txt, the GIAC_QUERY block), so a second tree does not read the objects, it drives
that make with its own flags and make decides what is stale. On 2026-09-06 every one of the 56
objects came out with a fresh timestamp, spread from 08:24:27 to 08:27:05.

So there are two observations that disagree. The 2026-09-05 reading recorded no recompilation, with
all 112 files hashing the same before and after. The 2026-09-06 one recompiled everything. Nothing
here says which input differed, and the honest reading is that reuse is what make decides rather than
what the flag guarantees. Check the object timestamps against the configure time before claiming a
build reused anything.

The 0f3a521 size above is unaffected, because a from-scratch compile is the stronger of the two paths
and this file already records that both produce identical bytes. What the incident cost is the
sentence this replaces, which said the build produced its artefacts without compiling any Giac. That
came from the build taking four minutes rather than from looking, and looking took one command.

The serial-use caveat below is therefore sharper than it reads: a build pointed here can write, so
two at once can race, with nothing to warn either.

The way round it while other lanes are working is to point NPS_GIAC_OBJECT_DIR at a private directory
instead. That costs a full Giac compile, roughly two and a half minutes on this machine, and it buys
a build that cannot disturb anybody else's. The da32f06 reading above was taken that way.

The vendor trees are untracked, so a worktree has none and both paths have to be passed in. The
fourth flag is what makes this cheap: the 112 Giac objects come from khi-src, which does not change,
and compiling them costs eighty megabytes and minutes, which is why no reproducible size existed
before. Point it at an existing giac-objects directory and the build is ten seconds.

Three things were checked before the number was trusted, because a shared object directory that
changed the artefact would make the size reproducibly wrong.

- The objects are reused rather than rebuilt into. Hashing all 112 before and after gave the same
  digest, and the build ran 33 steps in under eleven seconds with no Giac compilation in them.
- The artefact is deterministic. Deleting the package and the ELF and relinking reproduced the same
  sha256, so a byte comparison between two builds means something.
- The artefact is the same either way. A build with shared objects and a build that compiled all 112
  from scratch produced identical bytes for all four files, module, document and both sidecars.

One caveat that is documented rather than tested: two build directories sharing one object directory
both drive the same make, so this is for serial use. Nothing stops two concurrent device builds from
racing, and nothing warns you either.

## The harness

benchmarks/heap_probe.c builds with the Ndless toolchain into a standalone .tns and writes
/documents/nps_heap.txt.tns. It reports its own load address, so a stale result file cannot be
mistaken for a fresh run, which cost an hour before it was added.

It wrote to /documents/ndless until 2026-09-05. That directory exists on the emulator and has never
existed on the handheld, so the constant named a destination one of the two targets does not have.
The documents root is the only directory every device is guaranteed to have.

```
nspire build sources=nps/benchmarks/heap_probe.c output=nps_heap_probe dir=nps/benchmarks
nspire deploy file=nps/benchmarks/nps_heap_probe.tns target=/
nspire exec   path=/documents/nps_heap_probe.tns
nspire fetch  path=/nps_heap.txt.tns local=/tmp/heap.txt
```

**Only the debugger's exec preserves residency, and that is emulator-only.** Two ways exist to launch
an Ndless program, and they are not equivalent for this measurement.

- The debugger's exec pushes the program onto the running loader task, so the Lua document and its
  loaded modules stay up. This is the route that produces the resident readings above.
- Sending the program and pressing Open on the Document Received dialog goes through the OS document
  loader, which tears the Lua state down first. Measured rather than assumed: run that way with Ki V4
  open and Giac reporting 1.9.0, the probe reported 22740992 bytes largest and 28246016 total, which
  is the nothing-loaded figure to within a per cent.

The second route is the only one the physical handheld has. **So the handheld cannot be measured with
StepCAS resident by any external program**, and the number PERF-011 actually wants has to come from
inside the module.

Three more hazards, all paid for:

- **The emulator sleeps when idle, and a slept emulator is indistinguishable from a running one.**
  Screenshots keep returning the last framebuffer and every key is ignored, so the screen looks live
  and the machine is not. What settles it is the CPU: the program counter parks at a wait-for-
  interrupt inside the power-management routine and the registers do not move. Continue the guest
  between the two reads. The debugger halts it to take a command, so two bare register reads agree
  with each other whatever the machine is doing, and a busy guest looks exactly like a slept one.
  A read, a continue, then a second read is the comparison that discriminates, and a program counter
  that moves across it means the guest is working rather than asleep however long it has been black.
  Only the ON key wakes a slept one, and waking is a full reboot
  rather than a resume, so anything measured before the sleep is gone. Check that the guest is
  running before trusting a screen, the same way a probe result is checked against its load address.
- A second exec inside one boot resets the calculator often enough to be a rule rather than bad luck.
  One exec per boot.
- nrequire matches a module by basename across the whole documents tree
  (ndless-src/ndless/src/resources/luaext.c line 53), so a stale copy anywhere shadows the deployed
  one. The emulator image carries two, /documents/ndless/nps_nspire.luax.tns and
  /documents/emulator/nps_nspire.luax.tns. Deploying somewhere else leaves a third copy that never
  wins, so the document reports a surface with no os_msgbox, no heap_free and no device_identity
  while the file that was just verified sits unused. Which of the two wins is not settled: this row
  said ndless, and on 2026-09-05 a deploy to /nps alone produced the stripped surface while an
  /emulator deploy in the same boot fixed it, with an ndless copy present throughout. That is the
  wrong way round for an ndless-first walk, and nobody has since measured the order directly. Treat
  the order as unknown, which is what the advice below already assumes.
  benchmarks/module_walk.c lists every copy with its size, which is the way to tell a shadow apart
  from a binding that was never built. Deploy to both /ndless and /emulator, because overwriting only
  the one that happens to win today is a guess about walk order.
- The headless emulator does not write its flash back on its own. images/.nspire-instances/default
  carries the writable image, and its mtime did not move across two verified 4 MB deploys, so every
  boot restores the same snapshot and yesterday's deploy is not on the calculator today. Anything
  measured against a deployed artefact has to be measured in the same boot that deployed it. This is
  separate from the shadow above, and both were in play at once. The debugger has a flashsave command
  that writes the modified blocks back, which is the way to keep a deploy across a restart if that is
  ever wanted. Nothing here has used it, so it is a pointer rather than a measurement.

### A stale reading looks exactly like a fresh one

Four incidents in one session, and they are one hazard wearing four disguises rather than four
separate problems:

- The probe wrote to a fixed path, so a fetch taken before the run had happened returned the previous
  run's file, identical in size. Fixed by printing the program's own load address, which differs per
  run.
- The emulator sleeps when idle and keeps handing back its last framebuffer, so a dead screen reads
  as a live one and every key is dropped in silence.
- The deploy tool searches an accumulated buffer that includes output the transport has already
  labelled as belonging to an earlier command, so a previous transfer's completion line was read as
  this one's, on a send that had left 57344 bytes of a 4 MB module on the calculator.
- A ctest run passed off a binary older than its source.

A fifth, found on 2026-09-06, and this one is inside the defence the other four are answered with.
**nsptool get returns corrupt data on a large file about one time in four, with the right byte count
and no error.** Eleven fetches of the same 4017562-byte module returned two distinct digests, three of
the eleven wrong, and the wrong one is not noise. It is exactly the first 1439 bytes followed by the
file again from its start, truncated at the end by that same 1439 so the length is unchanged. 1439 is
one maxsize chunk in libnspire-src/src/services/file.c, which is the packet data size less one, so a
single stream chunk is being delivered twice.

The mechanism is diagnosed and not confirmed, and it is written down that way on purpose.
handlePacket in libnspire-src/src/cx2.cpp acks any packet that asks for one and passes its stream
payload up with no check on the sequence number, so a retransmission carrying a repeated seqno would
be acked again and handed over as fresh data. That fits the shape exactly. Nobody has instrumented
the transport to watch a seqno repeat, so it stays a hypothesis. Timing does not separate the two
cases: the bad fetch took 20.69 seconds against a good one at 19.39.

What this costs is the line at the top of this file, that the module was fetched back and compared
byte for byte before any reading was taken. That check has a false alarm roughly one time in four and
nothing said so. A read-back that disagrees means nothing until it is repeated, and the rule is to
fetch until two agree and to check the result against the sidecar wherever one exists.

What they share is that the failure produces a plausible answer instead of an error, so nothing
prompts a second look. The defence is the same every time: make the artefact carry something a stale
copy cannot forge, and check that before reading the number. A per-run load address, a program
counter that is actually advancing, a byte-for-byte read-back, a build newer than its source. The
fifth incident is the reminder that a defence is an instrument too, and an instrument that has never
been checked against itself is a claim.

### What the harness assumes about paths

Written down because the layout of both the repository and the handheld is being reorganized, and
these are the places a move lands.

- benchmarks/heap_probe.c writes to /documents/nps_heap.txt.tns, compiled in as RESULT_PATH. It is
  the one genuinely fixed destination here, so a device layout that moves it needs the constant
  changed and the probe rebuilt. It now names the documents root, which every device has, rather than
  a subdirectory only one of the two targets carries.
- scripts/deploy-device.sh finds nsptool by walking two directories up from itself to the repository
  root and looking under tools/nsptool. Moving the script or that tool breaks it. NPS_NSPTOOL
  overrides the search.
- The deploy's destination on the calculator is not fixed. NPS_DEVICE_DIR sets it and defaults to
  the documents root, so a subfolder layout is a matter of setting the variable.
- **A module and its sidecar have to land in the same directory.** integrity.cc derives the sidecar
  path from the module's own path, so splitting them across a reorganization gives a module that
  loads with every native surface off and a document reading "integrity: missing". That is the
  failure this whole line of work exists to stop, and a file move is the easiest way to cause it.
- nrequire matches by basename across the whole documents tree, so a reorganization that copies
  rather than moves leaves two candidates and the old one can win.

## Historical gaps and handheld procedure, 2026-09-06

- **End-to-end peak memory.** Free heap sampled between operations is not a peak. The instrument that
  would catch it is the wrapped-allocator tracker in src/platform/nspire/allocation_probe.cc, and
  that build cannot run the UI: CMakeLists.txt passes NPS_RESOURCE_PROFILE as the manifest's
  diagnostic_only flag, which collapses installed_modules to one diagnostic entry, and nps_v4.lua
  refuses the module. Verified by running it: the module loads, integrity verifies, Giac paints, and
  a solve comes back "StepCAS module incompatible (missing calculus.derivative.single-variable)".
- **Anything from the handheld.** This is the one reading criterion 9 is waiting on, so the whole
  procedure is written out below rather than left to be worked out again.

### Taking the handheld reading

Nothing needs building or deploying. The module already on the handheld is the build carrying the
heap_free binding, and all four files were read back and compared byte for byte when they were sent.

**keysvc answered on the handheld on 2026-09-06, and then a restart took it away.** This section
said until that date that keysvc does not answer on that device and there is no other route to a
keypress, so a human had to open the document. That was wrong. An esc went through, then a down, and
the home screen selection moved from Browse to Recent under a screenshot taken either side of it.

What replaced it is worse and less settled. One nsptool restart took the calculator down cleanly,
brought it back in about thirty seconds on a genuine fresh boot, and left no keysvc behind. Retried
at ten and twenty seconds after the boot, the version query answered "Invalid packet received" every
time.

**What loads keysvc on this handheld is not known, and the gap is the point.** The documented route
is /documents/ndless/startup, which Ndless runs at boot (.Internal/tooling.md, the startup folder
section), and that directory does not exist here. The documents root instead carries a boot chain of
its own, ArmBoot, Bootstrap, Relocate, BootStatus, HideAll and friends, and the note from the
2026-09-05 reorganization says outright that nothing confirms what runs them. So there are three
readings that fit: keysvc was resident from an earlier boot with nothing able to restore it, or the
boot chain restores it but needs a keypress this boot never got, or the chain runs and keysvc is not
part of it. Nothing here separates them. What is measured is only that a reset ended the keypress
route, and that is enough to plan around.

**So the reset this procedure opens with is the step that ends it.** Reset first is still right and
the directive still stands, but on this device a reset costs the ability to press a key, and only a
hand at the calculator gets it back. nsptool restart is itself sent through keysvc
(tools/nsptool/nsptool.c, cmd_restart), so there is not even a second reboot available afterwards.
Stage keysvc.tns in /ndless/startup before resetting, or plan for the boot after the reset to need a
human. What the link keeps at first is ls, get, put, mkdir, rm and screenshot, so nothing is wrong
with the transport and only the keypress route is gone.

**That does not last, and the reason is the dialogs.** A get leaves a Document Sent on the screen, a
put a Document Received, an rm a Document Deleted, and esc is what clears them. With no keysvc there
is no esc. On 2026-09-06 the link answered an ls, a screenshot and two gets after the reset, then
stopped answering, and libusb went on to see zero devices at all with nothing for the calculator in
the system USB list either. What is measured is that the handheld left the bus rather than refused a
command. Why it left is not measured. An inactivity power-down fits, since nothing had pressed a key
since the boot, and so does a transfer dialog wedging the link, and this run cannot tell them apart.
Either way a reset without keysvc gives a handful of link commands and then a calculator that only a
hand can bring back. Any staging put has to happen with a person there to press esc, not before.

The files sit at /nps/nps_v4.tns and /nps/runtime/nps_nspire.luax.tns, each beside its own sidecar,
as of the 2026-09-05 reorganization. Each package sitting with its own sidecar is what matters: split
them and the module loads with every native surface off.

**There is a second copy of the module on the handheld, and by walk order it is the one that loads.**
/MyLib/nps_nspire.luax.tns is 3993845 bytes and /nps/runtime/nps_nspire.luax.tns is 4017562, and each
sits beside its own sidecar whose digest matches it exactly, so the integrity check passes either way
and nothing on the launch screen distinguishes them. file_each sorts each directory with strcmp
before walking it (ndless-src/ndless-sdk/libndls/file_each.c, the qsort before the loop) and the
nrequire callback returns 1 on the first basename match, which aborts the whole walk
(ndless-src/ndless/src/resources/luaext.c, require_file_each_cb). MyLib sorts before nps, so the walk
finds MyLib first and /nps/runtime is never reached. That is the emulator shadow hazard recorded
above, present on the handheld and unnoticed until 2026-09-06.

Settle it on the device rather than by reading this: **!m prints the loaded module's own manifest id**
(nps/lua/nps_v4.lua, the manifest branch), abbreviated to the first twelve characters, an ellipsis and
the last twelve. One caveat before relying on it. That branch was read in main, and the document on
the handheld is 27433 bytes against main's 28753, so it is a different build and nothing yet shows it
carries !m. Typing it is what settles that too. The three ids that matter are distinct in their first
four characters:

| Where | Bytes | Manifest id |
|---|---|---|
| handheld /MyLib | 3993845 | stepcas.unified.inputs-sha256.20bd276536cd51f9690cc9b3a57119558a5fc2fc34bef25ee8822b29d2c20075 |
| handheld /nps/runtime | 4017562 | stepcas.unified.inputs-sha256.9f19fc44cc87c84338e24e284d1f1eb89465eaffcd0b39a9e53465aff1cfac47 |
| main at 0f3a521 | 4026614 | stepcas.unified.inputs-sha256.0f757c60a15c5497ff1f16ad78f4fb718c4835fc388cff0027eec337f7fff950 |
| main at da32f06 | 4030186 | stepcas.unified.inputs-sha256.6eaa9cca7cdbc60332a7ba0267a936397bbad2b9f1a1d2b3fcf671a95d7e1654 |

Neither copy on the handheld is main, and neither is either build in the table at the top of this
file, so no emulator number here was taken against the artefact the handheld runs.

Reading a manifest id out of a .tns needs no device. The container is a Zehn header whose first
relocation entry is FILE_COMPRESSED with type ZLIB (ndless-src/ndless-sdk/tools/genzehn/zehn.h), so
the payload inflates from an offset of 32 plus four times the relocation count, plus four times the
flag count, plus the extra size, and the id is a plain string inside it.

Reset the handheld first. A reading taken on a machine that has been running measures that machine's
history as much as our program, and the first version of the fragmentation finding in this file was
wrong precisely because two readings came from two different boots.

### The one-pass sequence

Written out in full because on this device the reset is the expensive resource. It costs keysvc, and
getting keysvc back needs a hand at the calculator, so the boot after a reset should collect
everything tasks on PLAT and PERF need rather than one reading at a time. Every capture is taken
twice and compared, for the same reason the fetch is: the transport hands back plausible wrong
answers without erroring.

Before the reset, settle two things, because both cost a boot to discover afterwards.

- **How to type an exclamation mark.** Every step command starts with one and nsptool cannot send it.
  Its parse_char table (tools/nsptool/nsptool.c) carries space, newline, tab, full stop, comma,
  parentheses, plus, minus, star, slash, equals, caret, angle brackets, question mark, colon, quotes
  and bar, and neither an exclamation mark nor a semicolon. Try key shift+ques into any text field
  and look at what appears. If that fails, the fallback is the toolbar menu, which the menu key opens
  and which prefills whole problems through menustring (nps/lua/nps_v4.lua, the menu table), so a
  kinematics run needs no typing at all.
- **Which input syntax the deployed document takes.** main's help line reads "!k find v; v0 = 5 m/s;
  ..." and uses semicolons, while the older form recorded here uses commas. The document on the
  handheld is neither build, so read its own !h output rather than assuming either.

Then:

1. Verify keysvc answers, with a key and a screenshot either side, not with the version query alone.
2. Reset. Wait for the device to come back on the bus, which took about thirty seconds on 2026-09-06.
3. **Verify keysvc again the same way. If it does not answer, stop here and say so.** Everything
   below needs a keypress and nothing below can be improvised without one.
4. Screenshot the home screen. Recent and Current are greyed out when no document has been opened
   since the boot, which is the closest thing to a visible receipt for a clean boot that the screen
   offers. Observed both ways on 2026-09-06: Recent was selectable before the reset and greyed after.
5. Record nsptool info. The free RAM there is whole-device RAM and does not belong in the budget
   table, but the boot it names does.
6. Open /nps/nps_v4.tns and stop on the launch screen. It is inside the nps folder rather than at the
   top of My Documents, since the layout changed on 2026-09-05.
7. Capture the launch screen. It carries the Giac version read from Giac at first paint, the
   integrity status, and a line reading "free N/Mk", N being total free in KB and M the largest
   block. This is PERF-010's launch-time free memory and it is the last moment before anything else
   runs, so nothing may be typed before it.
8. Type !m and capture. This prints the loaded module's own manifest id, which settles which of the
   two copies on the device is live without deleting either. Do this before any solve, so a failure
   here cannot be blamed on one.
9. Solve, capturing after each and without resetting between them. The metrics line reads
   milliseconds, nodes, steps, Giac calls and free heap in KB, in that order. Run the linear solve,
   the derivative, and the kinematics problem, so all three have figures from one boot on one OS for
   the first time.
10. For PLAT-002, leave Ki, open the Scratchpad calculator and enter something symbolic. A non-CAS
    handheld still refusing it is what shows Ki did not modify the stock application. This goes last
    because it changes what is on screen.

PLAT-003 has no step of its own on purpose. The CX II carries no radio, so being offline is not a
state a run can create or remove, and a solve completing with nothing attached but a USB link that
moves files and screenshots is the whole of the evidence. Manufacturing a test for it would be
theatre.

Steps 7 through 9 have to be one session. Reset in between and the readings are from different boots
and cannot be compared, which is the error this file already records once.

Record which boot each number came from and what had run on the device before it. A reading without
its boot is not a reading.

One figure that is not this one: the OS reports free RAM over the link, about 32.4 MB of 36.1 MB with
nothing of ours running. That is whole-device RAM, not the malloc heap the budgets are written
against, and it moves with whatever the OS is doing. It does not belong in the table.
- **The probe's latency on target hardware**, for the reason in the section above.
