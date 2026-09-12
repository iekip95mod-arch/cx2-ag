# StepCAS status and retained history

## Current handoff, 2026-09-08

Current architecture and build commands are in [README.md](README.md) and [the codebase map](../docs/codebase-map.md). Use CMake directly for nps. The SDK is ndl-src/ndl-sdk and its explicit override is NDL_SDK. The Makefile commands below belong to the earlier checkout and are not current instructions.

The [family catalog](catalog/families.md) describes implemented coverage. Generated acceptance and traceability reports describe the cases and checks exercised by their own builds. Read their missing populations and unmet requirements as well as their process exit status. Historical test counts below do not describe the current tree.

The September 8 generated reports evidence 85 of 102 MVP requirements. The acceptance corpus contains 1099 records and 972 distinct cases with no reported faults. Section 27 metadata is complete for 7 of 18 catalog families with 85 field gaps. Supported native cases cover 19 of 139 mathematical menu entries. These are separate evidence populations. The [menu walkthrough plan](../docs/menu-walkthrough-plan.md) tracks the remaining work.

Native SolveTask now owns persistent linear and rearrangement requests. It validates identifiers through the parser's rules and retains request provenance and published conditions through cancellation and resource failures. The synchronous wrappers drain the same engines. Lua still solves synchronously. UI incrementality, total retained memory and elapsed slice budgets remain unfinished. The walkthrough, physics browser and Full Text reader now share protected painting with retained content for recovery.

The native task and overlay hardening passed scoped independent host review and all 33 groups in the complete ASan and UBSan check against actual host Giac. A separate 10000-case fuzz run at seed 1 passed with no failures. The current ARM package passed its offline audit and is retained as release-hardening-b498761c. The earlier foundation package release-solve-task-0bedd3e9 is also retained. The hardening package is now the installed pair on the handheld, by a transfer nothing recorded: see the correction below. The foundation package was not uploaded. These results do not establish physical behavior. Evidence and frozen source manifests are in /private/tmp/cx2-continuation-LPC7N8.

The handheld's latest [qualified rendering release](benchmarks/BUDGETS.md#equation-opening-check-2026-09-08) is 61d6f4b. Its module and unopened document matched read-back before launch on CX II non-CAS OS 6.40.74. Input, hints, detail navigation and first-opening determinant matrices have narrow visible evidence. The saved document is retained separately because the OS rewrote its bytes. Familiar fractions still appear in canonical reciprocal notation in the tested linear example, observed against that release rather than against a later build.

The earlier runtime migration and cleanup retained the essential /ndl and /nps installation and exported recovery copies. A prior hard restart lost the key service. Later loaded-package checks reported Ndl r2022 but did not repeat a restart or hash the resident runtime. Persistence remains unqualified. Intermittent transfer and screenshot failures remain unresolved despite a bounded serial probe that did not reproduce them.

The later [handheld inventory check](benchmarks/BUDGETS.md#handheld-inventory-check-2026-09-08) found no obsolete packages or duplicate modules in the transfer-visible filesystem. Preserve the installation and recovery files under /ndl, the saved document and module under /nps and the active keysvc log. The empty NspireLogs.zip reappeared immediately after deletion. That file is not evidence of a leftover deployment. The calculator was left at Home with USB released.

Correction, 2026-09-08: two installs happened after the lines above were written, and the second one is what is on the calculator now.

The pair found installed before this session's work was the hardening release, not the rendering release the lines above name. Its module sidecar read b498761c2f91728a64812c6a27a58ac439895cda58be9d1f9ad2eb77e1e32f40 at 4137533 bytes. Nothing on either side records when that transfer happened, by what route, or whether anything accompanied it.

The handheld now runs a package built from 193aeaa: module 98374f665b2598a6ea1f13f67a48777afc6b37c7422d17a6d4be91fe49d0810c at 4137535 bytes and document 53ac63eafe13da85517309ec7cfcf4dcdf5dc7a8e29daa91bddc922bd21c0f8f at 39313 bytes, both hashed on the host and both read back byte-identical from the device. The calculator's own Full Text then printed the manifest id stepcas.unified.inputs-sha256.a36fdcae182e50b9b35b37c9d476faaf8582b58640bd592412e5d1bfeda50c2d, which is the build naming itself rather than a host read-back. Qualification evidence for it is in benchmarks/BUDGETS.md. That manifest id is specific to a tree without the pending capability-manifest change, because every artifact id is hashed from CMakeLists.txt and src/core/capability_manifest.cc, so quote the tree alongside the id.

PRD sections 19.9, 20.2, 22.1 and 27 remain acceptance gates. Complete-release handheld evidence still needs keypad and touchpad interaction, mathematical display, supported solves, cancellation and recovery, standalone operation and measured resource budgets with UI and Giac present. Host tests and older Ki screenshots do not discharge those checks for the renamed package.

## Historical record, 2026-09-02 through 2026-09-04

The remainder preserves the observations, commands and artifact names recorded during those sessions. Statements such as done or what does not work yet apply to that snapshot. Use the current references above before acting on them.

Written for someone picking this up cold. The reasoning behind each decision is in
[../.Internal/ki-v2.md](../.Internal/ki-v2.md) for the core and
[../.Internal/ki-v3.md](../.Internal/ki-v3.md) for the document; this is what exists and what
state it is in.

the maintainer supplied an engineering spec pack on 2026-09-03, now in ../.Internal/agent-pack. It targets a
greenfield repository and cannot be run as given.
[../.Internal/spec-reconciliation.md](../.Internal/spec-reconciliation.md) maps it onto this tree:
what is already met, five conflicts, and what is missing. Read that before starting anything the pack
names.

## What works

779 host checks, 16 Ki V2 UI checks, 292 Ki V3 UI checks and 295 Ki V4 UI checks pass, a 10,000
case fuzz run is clean, the sanitizer build is clean with 110 bridge checks under it, every source
cross compiles for the calculator, and the four modes run on the emulator with Giac agreeing with
them, through two images as Ki V3 and through one as Ki V4.

The typed path has no host coverage and cannot have any, since Giac is not linked on the host, so
its evidence is a differential check that runs on the calculator: 16 allowlisted calls down both the
typed and the string path, compared on tag and canonical form, all 16 agreeing. Reached with `!t`.

The build is CMake and Ninja, one tree per compiler, with the Ndless cross toolchain checked in at
cmake/toolchains/ndless-arm926ej-s.cmake. The Makefile is a forwarder now and holds no build logic,
so `make -C nps test` and the cmake invocation underneath it are the same build.

The language is C++20, pinned once as CMAKE_CXX_STANDARD for both trees. Target builds add
`-fno-exceptions -fno-rtti` from the toolchain file, the host keeps both, and the clang-tidy and
clang-query invocations under tools/ carry their own copy of all three. The toolchain is
arm-none-eabi-g++ 14.2.0.

A node owns no memory. `sizeof(Node)` is 40 bytes on target, unchanged by the pool because the
32-bit ARM layout rounds the same either way, and the gain there is the allocation rather than the
size: 3 to 16 per cent fewer allocations across the four measured solves. See
[../.Internal/ki-v4.md](../.Internal/ki-v4.md).

```bash
make -C nps test        # 779 checks, including a 400 case fuzz pass and 24 golden derivations, then every UI smoke, the traceability report, the coverage catalog and the evidence mutation run
make -C nps san         # the same checks, a 2,000 case fuzz pass, and the Lua bridge driven from LuaJIT, all under ASan and UBSan
make -C nps fuzz        # 10,000 generated expressions through the parser and the rules
make -C nps regold      # rewrite the golden fixtures, then fails on purpose so they get read
make -C nps probe       # nps_bench.tns, the standalone measurement program
make -C nps luax        # nps_split.luax.tns, the native module Ki V2 and Ki V3 call
make -C nps unified     # nps_nspire.luax.tns, the same core with Giac linked into it, for Ki V4
make -C nps document    # nps_v2.tns, nps_v3.tns and nps_v4.tns, the UI documents
```

Each of those configures its tree if it has to and then runs one cmake build. The trees are
build/host, build/san and build/device, and the artefact paths are unchanged: build/host/nps_host
and build/device/*.tns are still where the deploy step and the emulator loop look for them.

| Piece | State |
|---|---|
| AST, arena allocated and hash consed | done, runs on device. Children live in one contiguous pool addressed by offset and count, so building a node costs no allocation of its own |
| Scanner and recursive descent parser | done, with depth, size and input limits enforced during the parse |
| Canonical form | done: flattening, ordering, exact integer folding to the int64 boundary, rational coefficients in lowest terms, x^1 as x |
| Canonical printer and Giac printer | done |
| Typed Giac adapter with the nine result tags | done, and answered by the real Giac on the handheld. In the unified build the AST becomes a giac::gen and the answer gen is walked back, with no command string and no reparse; the string path stays for Ki V3, which reaches Giac across a Lua call that cannot carry an object |
| Derivation record from PRD section 10 | done, one payload per kind |
| SolutionContext | done: built at every solve exit, versioned wire format, scanner that refuses a bad blob |
| Cancellation and resource budgets | done locally: every Meter polls on entry and every 64 rewrites, and a halt rewinds the record. Giac calls cannot be preempted |
| Linear equations in one unknown | done, solved through recorded verified steps |
| Differentiation by rule | done: constant, variable, sum, constant multiple, product, quotient, integer power, chain, sin, cos, tan, exp, ln, sqrt |
| Integration by rule, src/steps/integrate.cc | done (Milestone 3): constant, sum, constant multiple, integer power of the variable or of a linear base including the reciprocal to ln, sin, cos, exp with a linear argument, the constant of integration as a step, and VER-005's check: the answer is differentiated by rule and compared in canonical form, and an answer that fails is withheld. Domain restrictions (ln needs a positive argument, a symbolic coefficient is assumed non-zero) are on the step and in the context |
| Lua module, src/platform/nspire/lua_module.cc | done, loaded and called on both the emulator and the handheld. `solve`, `differentiate` and `integrate` always cross-check; the `_local` twins are the opt-outs. Each step row also carries its fuller explanation, rule id, domain restriction and verification text |
| UI document, lua/nps_v2.lua | done: input line, answer, trust label, scrollable nested step list. Superseded by Ki V3 |
| UI document, lua/nps_v3.lua | done: the Ki V1 shell (KhiCAS on our giac 1.9) with step modes, a focused step viewer with two detail levels, assumptions on screen, save and reopen. Runs on the emulator with Giac |
| Unified module, nps_nspire.luax.tns | done: the same core with Giac linked in, built by make unified. src/platform/nspire/lua_module.cc compiles twice, and under STEPCAS_GIAC a backend call is a direct call to giac_caseval rather than a trip out through the Lua interpreter. Registers as ki rather than stepcas, and carries a caseval for the shell's own use |
| UI document, lua/nps_v4.lua | done: Ki V3 against the one module. It asks for ki and for nothing else, which test/ui_smoke_v4.lua checks by refusing every other name |
| Physics group in the tool palette | done: first box, before the Giac groups. Nine entries in the Giac groups' own style, where each label is a kinematics line that runs as it stands and choosing it puts that line in the input editor. Both documents have it |
| Quantities, dimensions and units, include/nps/units/units.h and src/units/units.cc | done (Milestone 4): a Dimension of length, mass and time, a hand written recursive scanner over m, km, cm, mm, s, ms, min, h, kg and g with powers, products, division and brackets, and exact conversion to SI through include/nps/core/rational.h |
| Precision and significant figures, section 13 | done 2026-09-04: an integer literal is exact and a decimal literal is measured, with its figures counted from the first non-zero digit. The answer is reported to the fewest among the measured givens its route used, as the last step, and every step before it holds the exact value. The combination rule is multiplication's applied to the route, so a subtraction that cancels leading digits still reports too many |
| One dimensional kinematics, include/nps/physics/kinematics.h and src/physics/kinematics.cc | done (Milestone 4): structured entry, the four constant acceleration equations, a unit conversion step, then one pass per hop of a dimensional check on the symbolic equation, an optional Giac rearrangement step, substitution, and solve_linear on the real derivation. No private evaluator and no rearrangement table |
| The planner, section 15 | done 2026-09-04: backward chaining from the unknown, deepened one hop at a time, each candidate offered to solve_linear on a scratch derivation and each branch dropped when it needs more quantities than there are hops left. Metered on the rewrite budget. A problem needing an intermediate quantity is solved rather than refused, which is half of M1's archetype 4. The other half is two bodies at equal position, and the problem model has one body |
| Golden derivations and fuzzing | done: 24 fixtures compared as step records, 10,000 case generator |
| Traceability report, section 19.9 | partial 2026-09-04: tools/traceability.cc joins the PRD's requirement tables against evidence the test run wrote, and refuses evidence naming a requirement that does not exist, evidence from a check that failed, or a catalog group no run reports. Seven requirements are evidenced, each linked to the families its tests exercise. The other three links section 19.9 asks for are absent and the report says so |
| Evidence mutation, section 19.9 | done 2026-09-04: tools/evidence-mutation.sh deletes each golden fixture's claim line in turn and requires the suite to fail while it is gone, on a copy so the checkout is untouched. It plants an uncompared fixture first to prove it can tell the difference |
| Coverage catalog, section 27 | partial 2026-09-04: catalog/families.md, four families, checked against the golden fixtures by tools/coverage.cc. A rule the corpus runs and the catalog does not name fails the build, and so does a claim no fixture supports. Nineteen of section 27's twenty-eight fields are carried |
| Vectors and frames, section 11 | partial 2026-09-04: Vector in units.h carries components, a rank, one shared unit and a Frame, and every operation refuses a frame it was not given rather than converting. Addition, subtraction, scaling, dot, cross and an exact magnitude. Entry reads "3 i + 4 j m/s" and "(3, 4) m/s", printing is always the first form. No basis transformation and no physics family uses them yet |
| Proof-obligation coverage, section 19.9 | done 2026-09-04: an obligation carries an id as well as its text, the catalog names the ones each family raises, and tools/coverage.cc reports them in their own table, joined against the fixtures in both directions. Five obligations, all five raised |

## Measured on the physical calculator, 2026-09-02

OS 6.2.0.333, Ndless r2022, no host process involved beyond a keypress over USB.

```
Ki V2  d/dx  (tab switches, enter runs)
x^2*sin(x)
(((x^2) * cos(x)) + (2 * (x^1) * sin(x)))
differentiated | Giac agrees
Differentiate by rule: Differentiate ((x^2) * sin(x)) with respect to x
  Product rule: Differentiate ((x^2) * sin(x))
    Power rule: Differentiate (x^2)
    Trigonometric derivative: Differentiate sin(x)
```

That is Milestone 0's exit condition: entered, evaluated through Giac, displayed on the target
device. "Giac agrees" is the adapter asking Giac the same question independently and then asking it
whether the difference of the two answers simplifies to zero, so it is a cross-check rather than a
restatement. The same run on the emulator says "not cross-checked", because that image has no
luagiac.

The linear solver browses the same way and cross-checks the same way, which is Milestone 1's exit
condition:

```
2x + 5 = 13
4
solved | Giac agrees
Inverse operations on a linear equation: Isolate x
  Collect like terms: Collect the terms in x
  Division property of equality: Isolate x
  check: Check the answer
```

Solve took two attempts to get there, and the first one is worth knowing about. Giac answers a
solve with `[[4]]`, a list of one solution set of one solution, which the parser refused, so every
solve reported "malformed result" and was never cross-checked at all. The adapter now peels a single
element list repeatedly and refuses a set of any other size with a typed reason. When a reply cannot
be used the UI shows it verbatim, because "malformed result" on its own says the check did not
happen and not why.

## Measured on the physical calculator, 2026-09-03

The first timed runs, Ki V2 with Giac and the module resident and the clock around the whole call,
cross-check included (probe/hw_perf1.png, probe/hw_perf2.png):

| Problem | Total | Nodes | Steps | Giac calls | Lua heap |
|---|---|---|---|---|---|
| d/dx of x^2*sin(x), the session's first run | 20 ms | 24 | 4 | 2 | 85 KB |
| solve 2x + 5 = 13 | 10 ms | 16 | 4 | 2 | 92 KB |

PERF-005's two seconds and PERF-006's five are two orders of magnitude away for the worked
examples. The emulator's integral of sin(2x) through Ki V3 took 10 ms with 2 Giac calls
(probe/e10.png).

## Changed 2026-09-04: vectors, framed from the start

PRD section 11 wants vectors first-class and frame-aware, and section 14 rejects a design that
"treats vectors as unframed lists" in as many words. So a Vector is components, a rank of two or
three, one shared unit and a Frame, and every operation refuses a frame it was not given rather than
rotating anything. Section 11.2's last line forbids an implicit conversion, and a quiet rotation is
exactly that. The frame is not optional and has no unset value: an entered vector is in the problem's
frame, named lab, because a vector that has no frame is the thing the pack says not to build.

The plan's step 24 asked which vector family comes first. The pack answers it in section 13: its M1
archetype 5 is 2-D vector addition in Cartesian unit-vector form, ahead of the dot product (6) and
projectile or relative motion (7), so addition is the first slice and this is its core.

In: addition, subtraction, scalar multiplication, dot product, cross product and magnitude, all
exactly over int64 and all refusing rather than wrapping. Two vectors add only when their frame,
rank and dimension agree, and each disagreement names itself. A cross product does not require equal
dimensions, since a distance crossed with a force is a torque, and it does require three components.
Magnitude is the interesting one: it is a square root, most of them are irrational, and this is an
exact engine. It answers when the root is exact and refuses otherwise, naming the vector, rather than
putting an unmarked approximation inside exact arithmetic. rational_sqrt_exact does that by integer
binary search, no floating point, since the device has no hardware for it.

Entry reads both forms a person writes: "3 i + 4 j m/s" and "(3, 4) m/s", the second read and never
echoed. Printing is unit-vector form with the unit once at the end, "(3 i + 4 j) m/s", zero
components dropped, "- 4 j" rather than "+ -4 j", and "0" for an all-zero vector so that something is
printed. The unit shown is the vector's own spelling, so the numbers and the unit always agree: a
sum converts both sides to SI first, which is why 1 km plus 500 m is 1500 i m and not 501 of
anything.

Writing the entry tests found one defect in the entry itself: "(3)" has no comma, so it fell through
to the term reader and was refused for missing an axis letter rather than for being one component.
The bracketed form is now told apart by looking for an axis letter as well as a comma.

Section 13's figure counting was duplicated the moment the vector parser needed it, so it is one
function now that parse_quantity calls too. 770 host checks, 0 failed, 9 ctest targets green, and the
ARM cross build clean.

Not in yet: basis transformations, so a second frame can be refused but not converted between; a
vector in the AST, so these are values rather than expressions; and no physics family uses them, so
archetype 5 is a core that nothing solves with.

## Changed 2026-09-04: a traceability report and a coverage catalog, both generated

The review's finding was that the gap is governance rather than mathematics: PRD section 19.9 wants a
generated report linking requirements to evidence, MVP criterion 15 gates acceptance on it, section
27 wants a versioned coverage catalog, and none of the three existed. Requirement IDs were prose in
source comments, so nothing connected a requirement to the test that would fail without it.

**The link lives on the check.** tests/unit/adapter_tests.h grew `TestSink::evidence`, which is an
ordinary check that also names a requirement. A deleted test takes its evidence with it, which a
list kept beside the tests would not. The group name is set in run_tests.cc and nowhere else, so a
test body cannot label its evidence as something it is not. The run writes what it recorded to
build/host/evidence.txt.

**tools/traceability.cc joins that against the PRD.** It reads the requirement tables out of
StepCAS_Product_Requirements_Document.md rather than a copy of them, so a requirement added there is
in the report on the next run. It refuses two things: evidence naming a requirement the PRD does not
have, and evidence from a check that failed. Both refusals were tested by feeding it each.

```
traceability: 7 of 165 prioritised requirements evidenced, 7 of 209 counting the staged ones
```

Seven, out of a hundred and sixty five. That is the honest state and it is the point of building
this: criterion 15 is nowhere near met, and now the number says so rather than a green test suite
implying otherwise. The 44 word-problem requirements are staged WP1 to WP3 rather than prioritised,
and are counted separately so the report does not quietly leave them out. The seven are PERF-003,
PERF-007, PERF-009, VER-003, VER-005, VER-007 and VER-008, each tagged on a check that proves the
requirement as written. Several more are close but partial, PERF-008 among them, since it names six
limits and the tree enforces four. Tagging those would be the overclaiming the report exists to stop.

**catalog/families.md is section 27's catalog**, one block per family, and tools/coverage.cc checks
it against the golden fixtures. A rule the corpus runs and the catalog does not name is a failure; so
is a rule the catalog claims as fixture evidence that no fixture records; so is a family with
fixtures and no entry. All three were tested by making each happen.

```
coverage: 4 families, 24 fixtures, 18 of 28 section 27 fields carried, 0 undeclared rules
```

It found one thing on its first run: the kinematics fixtures record eq.collect-like-terms and
eq.divide-both-sides, because a kinematics solve nests the linear solver into the same derivation,
and the catalog had not said so. The nine section 27 fields the catalog does not carry are listed in
the generated report rather than filled with placeholders, since an empty field reads as a question
somebody answered.

**Evidence nothing compares is not evidence**, which is section 19.9's last sentence and the part
that makes the rest mean anything. tools/evidence-mutation.sh copies the fixtures, deletes each one's
claim line in turn (its first verification record, or its outcome where a refusal has none) and
requires the suite to fail while the line is gone. All 24 do. The script proves it can detect the
failure first, by planting a fixture no check reads and requiring that one to survive: without that
selftest it would report success on a suite that compares nothing at all. Nothing in the checkout is
touched, since the suite is pointed at the copy through NPS_GOLDEN_DIR.

**One of section 19.9's four missing links is in**, the one to catalog families. The catalog names
the test groups that exercise each family, the evidence says which group a check came from, and the
report joins them. A catalog naming a group no test run reports is a failure; a group that ran with
nothing tagged is counted rather than failed, which is the honest difference and took a second pass
to get right. The units group is that case today.

**Proof-obligation coverage is reported on its own**, which is a separate sentence of section 19.9
and the one thing in it that needed a change to the derivation rather than a new tool. An obligation
was prose on a step, so nothing could count it; it is a ProofObligation with an id and that text now,
the four emit sites name theirs, and the fixtures record both halves. The catalog names the
obligations each family raises and the coverage tool joins the two the same way it joins rules: one
the catalog names and no fixture raises fails, and so does one a fixture raises that the catalog does
not name. Five obligations across three families, all five raised. The derivative family raises none,
which the report states rather than leaving the reader to infer it from an absent row.

Falsifying that join found a defect in the catalog parser it shares with the traceability tool: a
second test_group_ids or proof_obligation_ids line for one family replaced the first list instead of
adding to it, where a repeated rule line appends. Both append now.

Both tools take nps_flags, so they compile under the sanitizers and were run there. 714 host checks,
0 failed, 9 ctest targets green.

## Changed 2026-09-04: five defects a review found in the planner and the rounding

All five were confirmed by reading the code before anything was changed, and the first one had been
sitting in the device evidence above: the two-hop run reported the same 4 giac calls as the one-hop
run. A hop's rearrangement is two calls, a solve and an is_zero, and the bridge's own cross-check is
two more, so two hops should read 6.

**Backend calls were assigned, not added.** giac_rearrangement ended with
`ctx.backend_calls = adapter.call_count()`, and each hop builds its own Adapter, so the second hop's
two calls replaced the first hop's rather than joining them. Now counted once on the way out of each
path, which is the whole of what that call spent. Tested on a two-hop solve with a scripted backend.

**The plan claimed applicability the solver never granted.** Once a route was found, every remaining
equation was labelled from whether its quantities were known, so the two-hop plan told the reader
that `v^2 = v0^2 + 2*a*x` was "also applicable" for v. It is quadratic in v and the linear solver
refuses it. That contradicted the rule this file states twice, that applicability is the solver's
verdict rather than a table's, and it was frozen in a golden fixture. The label now comes from the
same probe the search uses, against what was known when the route finished, and the fixture says
"this equation is not linear in v".

**A backend that declined left no trace.** With no backend the plan says so; with a backend that
answered with anything but an exact form, the rearrangement step was simply absent and nothing said
why. Now a check step records the tag Giac gave, with a NotAttempted verification, so the two cases
no longer look identical.

**The rounding step displayed a transformation with nothing verifying it.** MVP criterion 4 wants
every displayed transformation to carry a passing typed verification. It now carries one: the
reported value is within half a unit in the last place of the exact one, compared in exact integer
arithmetic, with the unit built from the place the value was rounded at rather than counted off the
printed characters. A rounding that cannot be shown to be within that bound is not displayed at all
and the exact value is reported instead. The predicate is precision_rounding_valid, which
units_tests exercises on values it should reject as well as ones it should accept. The earlier
text-derived predicate named here had no test of its own, which is why the sentence claiming it did
has gone. The step's `after` node is the decimal as written now, not the fraction 33/100 the action
text did not mention.

**Iterative deepening was per branch rather than per route.** Each missing quantity got the full
remaining depth, so a level-three search could build a route of five hops (four in practice, since
the table has four equations and each is used once). The limit is now on the route, and the hops
already committed are counted against it, which is what the "a shorter route always wins" claim in
the header needs to be true.

714 host checks, 0 failed, sanitizers clean, 2,000-case fuzz clean.

**Verified on the calculator**, where the count was wrong in the first place:

```
!kv=?,x=20m,t=4s,a=3m/s/s          v = 11 m/s   67 nodes  15 steps  6 giac  28 rows
!kv=?,v0=0m/s,a=9.8m/s/s,t=2.5s    v = 25 m/s   38 nodes   9 steps  4 giac  17 rows
```

probe/c27a.png and c27b.png. The first read 4 giac before this change with every other number the
same, and the second is a one-hop solve whose count was right all along. The second also exercises
the rounding step's new decimal node in the viewer, which renders.

## Changed 2026-09-04: significant figures, so an answer says what it is entitled to

Section 13 is an M1 requirement and none of it was here. A decimal given became an exact rational
and how it was written was lost, so 20 m and 20.0 m were the same input and the answer carried
whatever digits the arithmetic happened to produce.

**An integer is a count, a decimal is a measurement.** That split is section 13.1's own first two
categories, and it is the one a scanner can actually see. parse_quantity now counts the significant
digits of a literal written with a point, from the first non-zero digit onwards, so 0.0450 is three
and 20.0 is three and 20 is exact. Precision rides on the Quantity beside the value.

Section 13.2 also asks for the decimal place of the last significant digit and for an explicit
uncertainty. Neither is here, because nothing would read them: the reporting rule below needs digit
counts, and no input spelling can express an uncertainty.

**The answer is reported to the fewest figures among the measured givens the route used**, and that
is the only rounding in the whole solve. Everything above it stays an exact rational, which is
section 13.2's ask and R29's, and the exact value stays in the record beside the reported one. A
given that is exact limits nothing, and a given that was not used limits nothing.

The rule is the multiplication one, applied to the route rather than to each operation, and it can
overstate where a subtraction cancels leading digits: 20.0 minus 24.0 is -4.0, two figures, and this
reports three. Tracking it properly means precision inside every fold in the canonical folder and
the linear solver, which is a change to two engines rather than to this one. Stated rather than
hidden, and textbooks have the same hole.

Rendering is exact integer arithmetic, half away from zero, in rounded_text. It is also what shows a
given as it was written: 1.0 m/s prints as 1.0 rather than 1, so a step that says it has two figures
is not printing it with one.

699 host checks, 0 failed, sanitizers clean, 2,000-case fuzz clean. The record is pinned as
tests/golden/fixtures/kinematics_significant_figures.txt, where 1/3 is reported as 0.33 and the
exact fraction is still in the step above it.

**Verified on the calculator.** The two shapes side by side, and the extra step is the difference:

```
!kv=?,v0=0m/s,a=9.8m/s/s,t=2.5s   v = 25 m/s   38 nodes  9 steps  4 giac  17 rows
!kv=?,v0=5m/s,a=3m/s/s,t=4s       v = 17 m/s   28 nodes  8 steps  4 giac  15 rows
```

probe/c26a.png and c26b.png. The exact value of the first is 24.5, and 9.8 and 2.5 are two figures
each. The second is every given an integer, so nothing rounds and its counts are what they were
before this change. Both say "Giac's own solve agrees".

Typing these through keysvc found one thing worth writing down: the 2D editor opens a superscript
box on `^` and only an arrow key leaves it, which keysvc cannot press, so `a=9.8m/s^2` typed over the
link becomes `a=9.8m/s^(2,t=2.5s)` and is refused. Spelling the unit `m/s/s` avoids the box
entirely and is the same dimension.

## Changed 2026-09-04: the planner, so a solve can take more than one hop

Section 15 of the pack calls a planner a MUST, and the tree refused a shape M1 names. The archetype
is worded twice: ARCH §13 item 4 calls it "two-stage/catch-up kinematics requiring an intermediate
event equation", and tasks/M1_VERTICAL_SLICE.md calls it "two-stage catch-up/equal-position
kinematics" and asks for a small linear system to go with it. What is built here is the first
wording. The second needs two bodies and a constraint that their positions are equal, and
KinematicsProblem has one body and no entities, so that half is still absent.

The refusal was measured rather than theoretical and is written up in ki-v4-plan.md:

```
find v; x = 20 m; t = 4 s; a = 3 m/s^2
  no applicable equation
```

The answer exists two hops away: `x = v0*t + (1/2)*a*t^2` gives v0 = -1 m/s, then `v = v0 + a*t`
gives v = 11 m/s. src/physics/kinematics.cc now finds it.

**Backward chaining, deepened one hop at a time.** Each equation is used at most once on a path, so
the depth is bounded by the table, and a branch is dropped before any solving when it needs more
quantities than there are hops left. That is the dead-end filter. Deepening rather than plain
depth-first is what keeps a one-hop problem a one-hop derivation: every route of length one is tried
before any of length two, so nothing that worked before takes a longer road now. Three tests hold
that line, including one where the unknown is reachable both ways. The first route that works is the
one taken, and within a depth that means table order decides a tie.

The candidate is still offered to the linear solver on a scratch derivation, so a quadratic or a
square root is refused by the engine that would have to do the isolation rather than by a table that
guesses. That was true of the one-hop selection and it stayed true.

**Each hop is a whole solve.** The dimensions checked on the symbolic equation, the known values
substituted, and the linear solver's own derivation underneath with its own substitution check. An
intermediate answer joins the known quantities and appears in the next hop's substitution line, so
v0 = -1 m/s is shown being found before it is used rather than arriving from nowhere. The record is
pinned as a golden fixture, tests/golden/fixtures/kinematics_two_hop.txt, because what is worth
having here is the steps: a route that quietly collapsed back to one hop would still print 11 m/s.

The plan's alternatives are per hop now and labelled with the quantity being looked for, since
"already used earlier on this route" says nothing without knowing which hop said it.

**The refusal message changed and is more honest.** It used to say "needs v, which is not given",
which was the whole truth for a solver that only looked. Now that it searches, the truth is "needs
v, and no equation reaches v". Two golden fixtures moved by that one line and nothing else.

**The search is metered.** Every probe counts against the rewrite budget. Every Meter polls once on
entry and every 64 rewrites after that. The bound is tested: a solve given max_rewrites of 1 halts
with "rewrite limit" rather than running to the end. One-hop and two-hop solves now both observe an
existing cancellation before recording work, and return no answer or partial derivation.

779 host checks, 0 failed, sanitizers clean, 10,000-case fuzz clean.

**Verified on the calculator.** The one-hop and two-hop cases run side by side and the difference is
visible in the record rather than only in the answer:

```
!kv=?,t=4s,x=44m,v0=5m/s    v = 17 m/s   45 nodes   8 steps  4 giac  15 rows  Rearrange for v
!kv=?,x=20m,t=4s,a=3m/s^2   v = 11 m/s   67 nodes  15 steps  4 giac  28 rows  Rearrange for v0
!t                          typed check: 0 disagreements
```

probe/c25e.png and c25g.png. The last column is the proof that deepening works: the one-hop solve
rearranges for v, the two-hop solve rearranges for v0 first, and the one-hop counts are what they
were before the planner existed. Both say "Giac's own solve agrees", so the cross-check ran on every
hop. The new case is in the Physics menu of both documents.

**A defect in the CMake conversion, found by this work.** `make unified` after editing a source
recompiled the object and did not relink the packaged .tns. The link's dependencies named the object
library targets, which ninja records as order-only edges: they say when to build, not when to
rebuild. Naming `$<TARGET_OBJECTS:...>` as inputs is what makes the link depend on their contents.
The old Makefile did not have this hole, and my byte comparison did not find it because every check
I ran built from a wiped tree. Tested now the other way round: touch a core source and each of the
three artefacts relinks, and an unchanged tree still rebuilds nothing.

## Changed 2026-09-04: the section 28 layout and the nps names

Section 28 of the pack sketches a tree and section 5.2 names build targets. Both are taken now, which
closes the last of the five conflicts from the reconciliation. The project directory is nps rather
than stepcas, headers live under include/nps by area, sources under src by the same areas, tests
split by kind, and the C++ namespace is nps.

```
include/nps/{core,steps,physics,units,cas}    headers, spelled nps/<area>/x.h at every use
src/{core,steps,physics,units}                the platform-neutral half
src/cas/giac                                  the adapter and the typed path
src/platform/nspire                           the Lua module
benchmarks                                    device_probe.cc, section 5.2's item 6
tests/{unit,property,golden,target}           by kind. No corpus/ until there is a corpus
```

Names, old to new, since the older entries below were written under the old ones:

| Was | Is | What it is |
|---|---|---|
| `stepcas_core` | `nps_core` | the platform-neutral object library, section 5.2 item 1 |
| `run_tests` | `nps_host` | the host test harness, item 2 |
| `ki.luax.tns` | `nps_nspire.luax.tns` | the Ndless application, item 3, Ki V4's module |
| `stepcas.luax.tns` | `nps_split.luax.tns` | the same core without Giac, for the two-image Ki V2 and V3 |
| `stepcas-probe.tns` | `nps_bench.tns` | the device profiling program, item 6 |
| `ki_v2/3/4.tns` | `nps_v2/3/4.tns` | the documents |
| `namespace stepcas` | `namespace nps` | |
| `STEPCAS_*` | `NPS_*` | macros, cache variables and the two environment variables |
| `kitrace.txt.tns` | `nps_trace.txt.tns` | the on-device trace file |

Two of the spec's six targets are still absent: `nps_kb_compiler` and `nps_corpus_audit` belong to
milestones that have not happened, and an empty target named for one would read as coverage.

`nps_nspire.luax.tns` keeps the `.luax` infix because it is load-bearing rather than decorative:
ndless-sdk/ndless/src/resources/luaext.c:53 builds the module filename as `name .. ".luax.tns"`, so
a Lua module named plain `.tns` cannot be found by nrequire at all. The name also has to stay under
30 characters (luaext.c:48).

**How the move was checked.** The layout was done first, on its own, and the artefacts compared
against the pre-move build: every one came back byte-identical once the link order was restored. The
first attempt was not identical, by 288 bytes on the largest artefact, and the cause was not the move
but that grouping the source list by area had reordered the objects on the link line. Putting the old
order back gave byte-identical output with every file in its new place, which is the proof that the
move changed nothing. The area-grouped order is what shipped, since the difference is padding.

The naming pass then changed the bytes for real: `nps_nspire.luax.tns` is 3866905 against 3867041,
`nps_split.luax.tns` 125101 against 125276. Every mangled symbol lost four characters when the
namespace went from stepcas to nps.

All 22 golden fixtures were rewritten, and the review that `regold` asks for found exactly one
changed line in each: `application version: stepcas 0.2` to `nps 0.2`. Nothing else moved.

**crosspath.cc was compiled for the first time.** It sat in the tree with its own main and no build
target, which is how device_probe.cc once rotted. It still compiles clean, so it is a target now
rather than a file nobody would notice breaking.

**Verified on the calculator, because no byte comparison can cover a rename that reaches the
device.** The module's filename is what nrequire searches for and the registered table name is what
the document calls into, so both had to be exercised rather than inspected. Fresh boot,
nps_nspire.luax.tns deployed to /ndless and fetched back to confirm it matched the build byte for
byte, nps_v4.tns opened:

```
first paint          Giac 1.9.0 : OK.
!t                   typed check: 0 disagreements
!d x^2               (2 * x)       12 nodes  2 steps  2 giac
!i 1/x               (C + ln(x))   24 nodes  5 steps  2 giac  assumes x > 0
!s 2x+5=13           4             16 nodes  4 steps  2 giac
!kv=?,t=4s,x=44m,v0=5m/s
                     v = 17 m/s    45 nodes  8 steps  4 giac  15 rows
```

probe/c24g.png through c24l.png. Every count matches what the same five inputs produced before the
rename. The first line carries the most: "Giac 1.9.0 : OK." is the document having called
nps_nspire.caseval, so nrequire found the file under its new name, main registered the table under
its new name, and the document reached it. Nothing about that could be checked on the host.

## Changed 2026-09-03, night: the build is CMake and Ninja

Section 5.2 of the pack asks for CMake and Ninja with a checked-in toolchain file driving nspire-g++,
nspire-ld and genzehn, and for that to be proven from a clean environment. CMakeLists.txt and
cmake/toolchains/ndless-arm926ej-s.cmake are that. Three trees, one per compiler: build/host,
build/san and build/device.

The Makefile is still there and holds no build logic. It maps the target names the notes already use
onto cmake invocations, so `make -C nps unified` and the cmake command underneath are the same
build. Artefact paths did not move.

**The conversion is verified by comparing bytes, not by the build succeeding.** Every artefact the
Makefile produced was kept, the tree was wiped, and everything was rebuilt from nothing:

```
nps_nspire.luax.tns        3867041   byte-identical
nps_split.luax.tns    125276   byte-identical
nps_v2.tns             4350   byte-identical
nps_v3.tns            18626   byte-identical
nps_v4.tns            19123   byte-identical
nps_bench.tns   219972 -> 220112
```

The probe is the one that moved and it is not the build system. Its old .tns was packaged at 22:23
and src/giac_adapter.cc changed at 22:51 for the typed path, so the kept copy predates the change.
Nothing rebuilds the probe except `make probe`, and nobody ran it. The chain that proves this: the
probe and nps_split.luax.tns link the same stepcas_core objects, and nps_split.luax.tns comes out
byte-identical, so the core cannot differ between the two build systems.

Two things the conversion found and fixed, neither of them new:

**A rebuilt Giac object relinked nothing.** In the Makefile the packaged .tns depended only on this
project's objects. The sub-make that builds Giac ran inside the recipe, so an up-to-date .tns kept
the recipe from running at all and the stale objects stayed linked in. The Giac objects are named as
inputs of the link now, and touching one does trigger the relink.

**The compile commands carried the caller's whole PATH.** nspire-g++ needs nspire-tools on PATH, and
the first version folded `$ENV{PATH}` into the launcher, which put it in every command ninja records.
Building from a shell with a different PATH rebuilt all 17 edges. Configure time still takes the
caller's PATH, because CMake searches it for ninja, but the launcher now gets a fixed value. Measured
after the fix: a build under a deliberately different PATH rebuilds 0 edges.

The stamp file that forced a rebuild on a DIAG change is gone. Ninja tracks the compile command line,
so changing -DSTEPCAS_DIAG rebuilds what read it without help, and the define is now set only on
src/lua_module.cc, which is the only file that reads it.

The resident-exit lint is still a gate and was proven to be one rather than assumed. It is named in
each packaging step's own dependencies, which ninja records as an order-only edge (`|| resident_exit_lint`
in `ninja -t query nps_nspire.luax.tns`), so it completes before the link. Tested by turning the module's
`_exit(0)` back into `return 0`: the lint failed, the link never ran, and the .tns already on disk
kept its hash. Reverting gave a byte-identical nps_nspire.luax.tns again, which is also how the revert was
checked. Being always out of date does not force a relink, which is what the old Makefile used an
order-only prerequisite for: a second build with nothing changed rebuilds 0 edges.

Not done, and still declined: the §28 tree layout under include/nps and src/core, and the nps_core,
nps_host and nps_nspire.tns target names. See the reconciliation note.

## Changed 2026-09-03, night: the adapter hands Giac objects

PRD section 12.3 and the supplied pack's section 17.2 both ask for the engine to be called with
objects rather than a command string. src/giac_typed.cc is that: the AST becomes a giac::gen, the
operation is an ordinary C++ call, and the answer gen is walked into the arena. It is the only file
that includes both this project's headers and Giac's, and src/giac_typed.h names no Giac type, so
the core still builds with Giac nowhere on the include path.

Backend gained one virtual, `typed`, defaulting to false, and Adapter::run falls back to `eval` when
a backend declines. That keeps both arrangements on one adapter: Ki V4 answers typed, Ki V3 reaches
Giac over a Lua call that cannot carry an object and keeps the string path. build_command runs first
either way, so the allowlist cannot become two lists that disagree.

Two things the types settle that the text could not. Giac here is built with -DNO_STDEXCEPT, so a
failure comes back as an undef gen and is_undef finds it structurally, instead of the adapter
matching "Error" and "Time limit" as whole words in the answer. And a result this project has no
node for is refused on its type tag, by name, rather than by failing to parse its printed form.

Evidence is on the calculator, since the typed path cannot be covered on the host:
typed_differential_check runs 16 allowlisted calls down both paths and compares canonical forms.
All 16 agree, including 12345678901234567890 + 1 through mpz both ways, 1/3 + 1/6 arriving as a
_FRAC that the AST spells as a power of minus one, and evalf as the one case tagged approximate.
The four modes were then run again and match the counts recorded before the change, node for node.

The device found two defects no host test could reach. The calculator's Lua has no `io` library, so
the module writes the report with fopen. And lua_pushnumber cannot work from an Ndless program at
all: the syscall marshalling in ndless-sdk/include/syscall.h takes int arguments and passes them in
r0 and r1, while the OS reads r2 and r3 for a soft float double, so the count arrived as
6.4393932879873e-219, an uninitialised register pair. lua_pushinteger is correct and is what the
rest of the module already used. Same family as the lua_toboolean syscall table defect below.

## Changed 2026-09-03, night: a Physics group, and the defect it uncovered

the maintainer asked for a Physics section in the tool palette, in the same style as Ki V1's own groups. Every
Ki V1 entry is a label showing the call and an action inserting it, with no separators and no mode
switches, so the Physics entries are seven kinematics lines that each run as they stand, one per
unknown plus a blank template. They sit in box 1, before Algebra, in both documents. the maintainer then asked
for the solver controls to come out of the palette and stay in the editor view, so the Steps group
went and its capabilities moved to input line prefixes.

Choosing the first one on the emulator refused with "could not read v0 = 5 ((m)/(s))". The 2D math
box takes the inserted text, renders m/s as a fraction, and hands it back bracketed: 5 ((m)/(s)) and
3 ((m)/(s^(2))). The unit scanner read a flat sequence of symbols and operators and rejected a
bracket outright, so nothing typed with the division key mid line had ever been readable. The
earlier keysvc runs missed it only because they put the one division at the end of the line, where
the fraction has nothing after it to bracket.

Brackets are now scanned rather than skipped. Skipping them would have been two lines and wrong: it
reads m/(s*kg) as metres per second times kilograms, turning a refusal into a wrong answer, which is
what PRD section 17 exists to prevent. src/units.cc has a small recursive scanner instead, where a
division applies to the one factor after it and a bracketed group is one factor. An exponent may be
bracketed too, since the editor writes s^(2). Fourteen checks in units_tests.cc cover the editor's
spellings, the grouped denominator, an exponent on a group, and every unclosed bracket.

Verified on the emulator after the fix: menu, Physics, entry 1, enter gives v = 17 m/s, "solved |
Giac's own solve agrees", 10 ms, 28 nodes, 8 steps, 4 Giac calls (probe/p2.png, p3.png, p7.png).
One cosmetic residue is left: the plan's facts echo the unit as the editor spelled it, so a line
reads v0 = 5 ((m)/(s)) rather than v0 = 5 m/s. The arithmetic and the dimensions are right and the
answer carries the SI unit; only the echo of the user's own input keeps the brackets.

## Changed 2026-09-03, night: Ki V4, one module with Giac linked in

At the maintainer's direction, since the Lua round trip to reach Giac was never the design. src/lua_module.cc
compiles a second time with STEPCAS_GIAC set, gaining a DirectGiacBackend whose eval calls
giac_caseval and copies the reply, and the module registers as ki rather than stepcas with a caseval
in the table for the shell's own use. One typedef chooses between the two backends and both call
sites use it, so the builds cannot drift into asking Giac at different points. Reasoning in
../.Internal/ki-v4.md.

- **make unified** links stepcas's twelve device objects, lua_module.cc built with the define, and
  Giac's 55 objects plus luabridge.o, with luagiac's own link line. The object list is asked of
  Makefile.ki at link time rather than copied, because a copy goes stale the first time a source is
  added there and the failure is an undefined symbol a long way from the cause. luagiac.o is left
  out: it has its own main. A wildcard over that directory would be simpler and wrong, since
  giacprobe.o sits beside the others with a main of its own.
- **nps_nspire.luax.tns is 3862524 bytes**, against 3814775 plus 122935 for the pair it replaces. About 75 KB
  smaller, which is the C runtime that is no longer there twice and what gc-sections can drop once
  it can see the whole program.
- **genzehn warns that both screen APIs are present** and assumes uses-lcd-blit false. The flag has
  no reader: ndless-src mentions USES_LCD_BLIT in zehn.h and genzehn only, and zehn_loader.cpp's
  switch falls through to default for it. The flag the loader does read is RUNS_ON_HWW, which
  240x320-support sets, passed true here exactly as the stepcas.luax build passes it.
- **test/ui_smoke_v4.lua** is the V3 harness against lua/nps_v4.lua with one change that is the whole
  point of the file: the fake nrequire answers ki and raises for every other name. Run the same
  harness against lua/nps_v3.lua and it fails, which was confirmed rather than assumed. 259 checks.

Emulator, fresh session after a flash save and a restart. nps_nspire.luax.tns fetched back and compared byte
for byte after the transfer, which took the two extension name. First paint drew "Giac 1.9.0 : OK.",
read from the module rather than hardcoded, so the direct call is proven before anything else runs.
Then, all through one module: expand((x+1)*(x+2)) gave x^2+3*x+2 in the 2D history box
(probe/v4d.png); !d x^2 gave (2 * x), "Giac agrees", 2 Giac calls (probe/v4e.png); !i 1/x gave
(C + ln(x)), "Giac's derivative agrees", "assumes: x > 0" (probe/v4f.png); !s 2x+5=13 gave 4,
"Giac agrees" (probe/v4g.png); and the kinematics line gave v = 17 m/s, "Giac's own solve agrees",
10 ms, 45 nodes, 8 steps, 4 Giac calls (probe/v4i.png). Every answer, tag and cost matches what Ki V3
gave for the same inputs, which is what a change of plumbing should look like. The flash image was
saved with both files on it.

## Changed 2026-09-03, night: Milestone 4, units and one dimensional kinematics

The physics slice. A kinematics problem is typed as a structured line, converted to SI, checked for
dimensional consistency, rearranged by Giac, substituted, and then handed to the linear solver,
which contributes its own steps and its own check. 638 host checks, 256 Ki V3 UI checks, 98 bridge
checks, a clean 10,000 case fuzz and a clean sanitizer build.

- **Units and dimensions, src/units.h and src/units.cc.** A Dimension of length, mass and time, a
  scanner written by hand rather than as a pattern, and exact rational conversion to SI. The scale
  of km per hour squared is 1 over 12960, and it prints in lowest terms, which is what the first
  expectation got wrong.
- **Kinematics, src/kinematics.h and src/kinematics.cc.** Which equation applies is the linear
  solver's verdict rather than a table's: each candidate that contains the unknown and only known
  quantities is offered to solve_linear on a scratch derivation, and the first it solves is used.
  The others carry the solver's own words into the plan's alternatives, so a quadratic is refused by
  the engine that would have had to do it. the maintainer's direction was that we do not roll our own maths,
  and this is what that means in practice.
- **Arena references now stay valid, src/ast.h.** Arena::at handed out a reference into a vector
  that the next node added could reallocate, and both kinematics substitute and integrate linear_in
  held one across a recursive call that built nodes. AddressSanitizer named the read. Five kinematics
  cases were failing on it, and the symptom was not a crash: the top level Equals came back as
  freed memory, so the linear solver reported that it had not been given an equation. Node storage
  is chunked now and addresses are stable for the arena's life, which makes all twenty nine sites
  that hold a Node reference correct rather than the two that were broken. A check in
  test/run_tests.cc holds a reference across two thousand additions, and it reports a use after free
  against the old storage.
- **canonical.cc folded zero to a vast power by multiplying.** The loop is bounded by overflow,
  which is about sixty turns for any base of magnitude two or more, but zero never overflows, so
  0 to the power 9223372036854775807 ran once per unit of the exponent. The 10,000 case fuzz sat in
  it for half an hour. linear.cc had found the same hazard earlier and guarded zero, one and minus
  one. canonical.cc guarded one and minus one only. Fixed where the helper lives, with the three
  degenerate bases and both sides of the overflow boundary checked in canonical_tests.cc. This one
  was not Milestone 4's, it was reachable from any expression.
- **The linear solver said "not linear" when exact arithmetic ran out.** Analysis returned one
  refusal for two different situations, so a caller trying several equations was told the wrong
  reason for the one it skipped. It carries which of the two happened now, and reports the arithmetic
  limit with a resource limit status. The outcome stays NotLinear on purpose, because a caller
  probing four equations wants that candidate refused rather than the whole search halted.
- **A halted kinematics solve reported its reason as "running".** The budget runs out inside the
  nested linear solve, whose meter is not the kinematics meter, and the reason was read off the one
  that had never counted anything. The nested reason and cost are carried out now.
- **The solution context claimed no unit conversion** while Milestone 4 converts. The unit policy was
  one build wide constant. An engine that converts states its own policy now, the way it already
  states its requested method.
- **The conversion step printed the symbol twice**, as v0 = 18 km per hour = v0 = 5 m per s. Reading
  the regold output is what caught that and the two above.
- **Ki V3 gains a !k mode.** Steps menu entry, help line and restore list, and the trust label now
  names what Giac actually did. It read the presence of a method as meaning a derivative, so a
  kinematics cross check would have been labelled "Giac's derivative agrees" when Giac had solved an
  equation. Unlisted methods fall back to plain Giac rather than claiming a check that did not run.

Emulator, real Giac, fresh session (probe/e45.png, probe/e46.png). Typed through keysvc as
"!kv=?,t=4s,x=44m,v0=5m/s", which avoids the space key and keeps the only division at the end of the
line, since keysvc cannot press an arrow to leave a fraction. Result: v = 17 m/s, "solved | Giac's
own solve agrees", 10 ms, 45 nodes, 8 steps, 4 Giac calls, 506 KB Lua heap, 15 rows in the viewer.
Giac's rearrangement came back as v = (((-t) * v0) + (2 * x)) * t^-1, and enter opened step 1 of 8,
the plan, with its goal and its reason. One thing learned about the tooling: the debugger's ln svc
takes a command line of at most 255 bytes, so 24 key records in one packet are refused with no reply
while 12 are accepted. keysvc itself reads 512 bytes.

## Changed 2026-09-03, evening: Milestone 3 and Ki V3

- **Integration by rule.** src/integrate.cc follows differentiate.cc's shape: one step per rule,
  nested for the parts, a plan at the root, the constant of integration as its own step, then a
  Check step that differentiates the answer by rule and compares canonical forms (VER-005). The
  check was falsified before it was trusted: a scratch copy with the power rule's exponent off by
  one (build/scratch/integrate_bad.cc) returns "verification failed", keeps the record, and
  withholds the answer. A budget the check itself runs out of halts the whole solve and rewinds,
  tested. 42 checks in test/integrate_tests.cc, four golden fixtures.
- **Canonical form folds rational coefficients.** The check needs 3 * x^2 * 3^-1 to be the same
  form as x^2, so a product's integer factors and its constant reciprocals now fold into one
  fraction in lowest terms, x^1 is x, and a constant to a positive constant power is its value.
  The fold reduces and retries, because cancelling a factor can make a leftover fit. Idempotence
  and the print-reparse round trip held over the 10,000 case fuzz. Old golden fixtures unchanged
  (they print the rules' own forms, not canonical ones).
- **`depends_on` lives in ast.h**, one copy for both rule engines.
- **The bridge's integrate cross-check is Giac differentiating our answer**, not integrating for
  itself: antiderivatives can differ by a constant or by ln(x) against ln(abs(x)) and be right.
  The result says which check it was (`giac_method`), and the UI words the label accordingly.
- **Ki V3 is the Ki V1 shell plus step modes**, at the maintainer's direction, not the small document Ki V2
  was. lua/nps_v3.lua is khicas53/khicas.lua with three hooks and an appended section; every
  KhiCAS feature stays. Detail in ../.Internal/ki-v3.md. The host smoke stubs D2Editor, the
  class library and the tool palette and drives the whole thing, 239 checks.
- **The OS validates the tool palette at registration.** Three menu entries named functions
  defined later in the file and were nil at that moment; the emulator reset the page with
  "expected function in menu item 6 of tool box 1". Closures now, and the smoke checks every entry.
- **D2Editor.setVisible(false) does not hide an editor** on the emulator; the viewer parks them
  off screen the way destroyD2Editor does and reposME brings them back.
- **luagiac is on the emulator**, our 3814775 byte build, so the emulator now cross-checks too.
  The link took the two-extension name for that file; nps_split.luax.tns still went over as
  stepcasx.tns and was renamed, then fetched and cmp'd.
- **PRD amended** to Draft v0.6 for the settled decisions: native-core split, bundled Giac, OS
  baseline, cooperative cancellation, Ki V3's definition. open-questions.md items 5 and 8 closed.

## Changed 2026-09-03

A review pass over the core under ASan and UBSan, then the emulator. Every item was reproduced
before it was fixed and has a check that fails without the fix.

- **Integer arithmetic at the int64 boundary.** The linear solver's gcd, negation and subtraction
  overflowed on INT64_MIN and refused nothing; they now work on unsigned magnitudes and refuse with
  "not linear in the unknown" when a value cannot be represented. `small_integer` and
  `integer_text` in src/ast.h are the one place a node becomes a number or a number becomes text.
- **Leading zeros.** `007` and `x^007` reached the canonical form as distinct nodes from `7`; the
  Integer case strips them.
- **A refused input no longer poisons the arena.** The parser's depth and length limits report
  their own status instead of marking the arena failed, so the next call on the same arena works.
- **Negation records a step.** `-sin(x)` derives through the constant multiple rule with a visible
  step rather than silently, and an unsupported form inside a sum rewinds the record to where it
  started, so a refusal leaves no steps.
- **The Lua bridge.** `luaL_checkstring` unwinds past C++ destructors, so the argument checks now
  run before the GC pause is constructed; a string literal can no longer land in a boolean field;
  a step tree too deep for the Lua stack reports `steps_truncated` instead of overflowing.
- **Ki V2** lost its diagnostic toggles and row cap; the title says what tab and enter do.
- **Ndless: `file_each` overran its name buffer.** libndls counted the bytes it had used but never
  advanced the count, so a directory whose names totalled more than 5000 bytes, which /ndless does
  on the emulator, wrote past the buffer and `nrequire` of any module reset the calculator. Fixed
  in ndless-src/ndless-sdk/libndls/file_each.c, proven under ASan on the host in
  probe/file_each_host and on the emulator, resources rebuilt and flashed onto the emulator image.
- **keysvc blocked the task that processed its keys.** Opening a document through it reset the
  calculator every time. It now replies and logs before posting, with nothing blocking after. The
  reply is a receipt for acceptance. Detail in ../.Internal/tooling.md.
- **The emulator's power routine warnings were a debugger artefact**, not a hang; the fork models
  the PMU low block now and an idle document survives.
- **Firebird fork hardening.** A trailing value option (`--gdb` alone) segfaulted; a debugger line
  over 255 bytes arrived as two commands; a lone quote in `ln s` or `ln os` indexed before the
  string; a negative scan count held a key forever; a stuck `ln svc` left its callback armed for a
  late reply; an exec run at once left an older queued one armed. All in firebird-src, rebuilt.
- **Giac message words are matched as words.** A symbol such as `undef_val` or `timeout2` in an
  answer was read as the message inside it and the answer thrown away.
- **The context scanner is swept.** Every single byte edit of a real blob, replaced, inserted or
  deleted, runs through `parse_context` under the test and the sanitizer builds. Whatever it
  accepts has to print back as the same bytes, since the format allows one blob per context.
  Disabling the trailing bytes refusal fails the sweep, so it is a guard rather than coverage on
  paper. No defect found in the scanner.
- **The Lua bridge runs on the host.** test/luax_host.cc builds src/lua_module.cc as a shared
  library against LuaJIT's 5.1 headers, with test/host_os/os.h standing in for Ndless, and
  test/luax_host.lua drives it with a scripted Giac: clean runs, every refusal, a caseval that
  raises, one that returns a number, no luagiac at all, and the collector restarting after an early
  return. 56 checks under ASan and UBSan as part of make san. It found one thing: a numeric reply
  from Giac was converted to text and accepted, where the code meant to refuse anything but a
  string. It refuses now.
- **keysvc calls the TI_NN_StartService syscall.** Its comment said the syscall table only named
  it on OS 3.x, and it carried the address by hand for two OS versions. The CX II tables do name
  it, and the hand-carried address is a one instruction thunk onto the function the table holds,
  checked by disassembly. The per-OS list is gone; the rebuilt keysvc registered at boot on the
  emulator and is in the handheld's startup folder for its next boot.
- **libndls config.c declared a 10000 byte cap and never checked it.** Its pair offsets are 16
  bit, so a larger ndless.cfg.tns would have been indexed wrongly. The cap is enforced now, proven
  by probe/config_host under ASan against the real source (a copy without the check accepts the
  oversized file), libndls and the resources rebuilt.
- **libnspire, the handheld transfer path.** A null pointer written before it was tested, a freed
  directory list left with the caller, a file read that could write past the caller's buffer, and
  a scanner that reported success with outputs unset, a receive checksum summed over the wrong
  length, and a 16 bit byte swap that was undefined behaviour on every packet. Fixed, rebuilt,
  and every nsptool action rerun against the calculator, then again from a build of the whole
  library under ASan and UBSan, which reported nothing. `info` also gained the battery line its usage promised and reads
  the CX II's run level. Detail in ../.Internal/tooling.md.

## Changed 2026-09-04: cancellation reaches every native entry point

Every Meter polls its Budget when a solve starts, then every 64 rewrites. All four engines have a
short cancellation check, and kinematics covers both one-hop and two-hop routes. Cancelled work
returns no answer and rewinds its derivation. The ARM Lua bridge gives every native entry point a
direct Escape keypad poll and does not start a Giac cross-check after a local halt.
The sanitizer bridge harness sends a pressed key through the same entry points and proves all
four return cancellation with no answer, no steps and zero Giac calls.
The same harness forces a real differentiation step-budget halt and proves that path also returns
without steps or a Giac call.

The 3,881,057 byte unified module cross compiled, transferred to the emulator and fetched back byte
for byte. A one-hop solve still returns 17 m/s with Giac agreeing. The emulator could not prove the
live gesture: holding Escape before Enter opened the OS Press-to-Test dialog, while a shorter tap was
not observed. The physical CX II was inspected on OS 6.20.333 but was not changed in this pass.

## Changed 2026-09-04: per-artifact capability manifests

Four manifests now identify the host, probe, split and unified artifacts. The device manifests
support exactly the CX II non-CAS on OS 6.2.0.333 and 6.4.0.74 with Ndless r2022. The unified
manifest records bundled Giac 1.9.0 and five compiled modules. Each identifier is generated from the
build inputs rather than maintained by hand.

The manifest object occupies 1,165 bytes, with zero data bytes, zero BSS bytes and no constructors.
Every packaged module and document has an externally verified SHA-256 sidecar.

**The unified module verifies its own package before it registers anything.** main takes the path
Ndless hands it as argv[0], derives the adjacent nps_nspire.luax.sha256.tns, hashes the package and
compares. Anything but a match registers a stripped table carrying only the reduced manifest and the
typed status, and nps_v4.lua then refuses every native surface including plain Giac, which is what
PLAT-012 asks for.

**That check could not be executed by any test until 2026-09-05.** hash_package used Giac's sha256
and so compiled only into the unified image, which nm confirms: the symbol was in nps_nspire.elf and
in no host binary. The parser and the Lua gate were covered, but the step that reads the bytes was
not, which is the shape this file calls worse than an honest gap. SHA-256 is
src/platform/nspire/sha256.cc now, one implementation for both targets, 1,453 bytes of ARM text with
zero data and zero BSS. Keeping it to one implementation is the point: a Giac path on the device and
a native path on the host could diverge, and the host test would stop being evidence about the
device.

The digest is checked against the published vectors and against a byte-at-a-time feed at every length
from 53 to 66, which straddles the padding block. Our digest, the sidecar cmake writes with
file(SHA256), and coreutils shasum agree on all three real device artifacts, which is what would
catch a wrong implementation rather than one that merely agrees with itself. Refusal is then proven
by making it happen rather than by reading the code: a flipped bit at seven positions, a truncation,
an appended byte and a foreign digest each read as a mismatch, a missing sidecar and a missing
package each read as missing, a sidecar naming another package is refused on its name, and a sidecar
that is not a digest record is refused as malformed. Falsified two ways, since a check nothing can
fail proves nothing: a hash returning a constant and a hash reading only the first block are both
caught, and one flipped bit in a single round constant fails every vector.

Two defects this found. integrity.cc called strnlen at six sites and compiled for ARM only because
Giac's include chain happened to declare it, so building it into any device target without Giac would
have failed, which is exactly what verifying the split module needs. And main gated on argc being
exactly one, where ld_exec appends an associated document after argv[0], so a file association would
have turned a valid install into a refusal reading path-invalid.

Still open. Only the unified artifact verifies itself: nps_split.luax.tns and nps_m0_probe.tns carry
sidecars nothing reads. The document sidecar has no reader and likely cannot have one, since the
Nspire Lua sandbox gives a document no file access. And the sidecar has to be deployed beside the
module, or a valid install refuses to load with "integrity: missing".

Firebird displayed the final unified fingerprint ee65a3bb59d1...60e975148537. The deployed module
and document fetched back byte for byte. The allocator headroom function estimates the largest
contiguous allocation on demand. It is not a total free-memory or peak-memory measurement, and its
target latency and heap perturbation remain unverified. M0 is not complete.

## What does not work yet

- **Memory is measured on the emulator and nowhere else.** The with-everything-resident heap reading
  exists as of 2026-09-05 and is in [benchmarks/BUDGETS.md](benchmarks/BUDGETS.md): about 18.4 MiB
  free at launch with the module, Giac and the Ki V4 document up, 13.8 MiB of it in one contiguous
  block, against about 28.4 MiB free with nothing of ours loaded. PRD section 19.6 wants the
  qualifying numbers from hardware, and none of these come from hardware. Two of PERF-010's ten
  budgets are still unmeasured, end-to-end peak memory and the branch count, and BUDGETS.md says
  which. The allocator headroom estimate no longer has an unverified perturbation: it restores the
  heap it took, checked over 100 consecutive calls and across taking and releasing every free block
  twice. Its latency is measured only on the emulator, so on target it is still unverified.
- **The verifier is a cross-check, not a proof.** Giac answering the same question is evidence, and
  section 12.1 says so. For an integral the two derivatives (ours by rule, Giac's) are independent
  of each other but both start from our answer. There is no third oracle.
- **Canonical equality is not mathematical equality.** It handles ordering, bracketing, constant
  and rational-coefficient folding. It does not distribute or factor, so `2*(a + b)` and
  `2*a + 2*b` are different canonical forms. Folding stops at the int64 boundary, and
  include/nps/core/canonical.h says exactly what that costs.
- **Integration stops at one substitution.** A linear argument or base is handled; products of two
  varying factors (parts), non-linear substitutions, rational exponents, tan, ln and sqrt refuse
  with a reason. That is the Milestone 3 envelope, not a defect.
- **A Giac call cannot be cancelled once it starts.** Local work polls Escape cooperatively, but
  Giac exposes no interrupt hook here. The automated emulator gesture also remains unverified because
  a held Escape opened the OS Press-to-Test dialog before the application observed it.
- **Physics is one dimensional kinematics and nothing else.** Constant acceleration along one axis,
  five quantities, four equations. No forces and no energy. Vectors exist as values now, but no
  family solves with them, so two dimensional motion is still out.
- **Seven requirements of a hundred and sixty five have evidence, so the MVP cannot be accepted on
  its own terms.** The traceability report and the coverage catalog exist now and say exactly this.
  MVP criterion 15 wants every P0 and applicable INV requirement linked to passing evidence, and
  section 28 says a problem is not supported until that report is clean. Every host check passing is
  not the same thing, which is why the report counts requirements rather than checks.
- **The traceability report carries two of section 19.9's five links.** A requirement links to the
  tests that evidence it and to the catalog families those tests exercise. Implementation components,
  device evidence and release status are not linked, and the report says so in its own header rather
  than leaving the reader to assume otherwise. Proof-obligation coverage is reported separately now,
  as the same section asks, which took giving the obligations identifiers.
- **Significant figures follow the multiplication rule and nothing else.** The reported answer takes
  the fewest figures among the measured givens the route used, which is what a textbook teaches for
  a product or a quotient. A sum or a difference should take the last significant decimal place
  instead, so 20.0 minus 24.0 is -4.0 to two figures where this reports three. Tracking it means
  carrying precision through every fold in the canonical folder and the linear solver.
- **A problem is one body.** KinematicsProblem is an unknown and a list of symbol-value knowns, with
  nothing to name a second body or say that two of them are at the same place at the same time. That
  is the half of M1's archetype 4 the planner does not reach, and it needs a problem model rather
  than a longer search.
- **The kinematics search takes the first route it finds, and looks no further than three hops.**
  Candidates are tried in table order at each depth, so the shortest route wins but a tie goes to
  whichever equation is written first. Nothing scores a route or explores two and prefers one, which
  is what section 15.3's strategy points want, and there is nothing yet to prefer between. The two
  filters section 15.2 asks for before a candidate is tried, forward closure of the givens and
  dimensional rejection, are also absent: on four equations, trying a candidate is one solve_linear
  call on a scratch derivation and costs less than filtering it would.
- **The viewer's step expressions are plain text.** The input and history are 2D through KhiCAS's
  editors; the derivation is not (MATH-003 is half met).
- **The viewer's arrow keys are checked on the host only.** keysvc cannot press an arrow (its codes
  do nothing even on the home screen), so focus movement and scrolling have no on-device evidence
  yet; enter, esc and shift+tab do (probe/e22.png to e31.png).
- **Ki V3 is not yet verified on the handheld.** The handheld left the USB bus during the first
  Ki V3 run (after two keys and two screenshots, so not the back-to-back pattern below) and has
  not re-enumerated; it needs a restart by hand. The build on it is the one before the palette and
  editor fixes.
- **Forcing a reboot over the link takes the handheld off the bus.** The rebuilt Ndless
  resources, keysvc and stepcas module went on over USB on 2026-09-03, old files saved in
  probe/handheld-*.tns, and the reboot was forced through a document loading a module that faults
  on purpose. The calculator reset but did not re-enumerate on USB until the maintainer restarted it by hand.
  After that: Ndless loaded, the new keysvc registered, Ki V2 differentiated and solved with Giac
  agreeing (probe/hw6.png, probe/hw7.png), and keys sent back to back were all accepted with the
  bus staying up. A reboot still needs a hand on the calculator.

## The loop

Host first, then the emulator, then the handheld. All three without a person at the keypad.

```
make -C nps test
make -C nps unified document

nspire deploy  file=nps/build/device/nps_nspire.luax.tns target=/ndless        # retry on refusal
nspire fetch   path=/ndless/nps_nspire.luax.tns local=/tmp/check.tns           # then cmp, always
nspire deploy  file=nps/build/device/nps_nspire.luax.sha256.tns target=/ndless # or the module will not load
nspire deploy  file=nps/build/device/nps_v4.tns target=/
nspire dbg     ln svc 0x4B45 0D 10 00 00      # enter, opens the Document Received dialog

scripts/deploy-device.sh build/device/nps_nspire.luax.tns   # handheld: sends the sidecar too, and cmps both
tools/nsptool/nsptool.new key enter
nspire device  screenshot local=/tmp/hw.png
```

**A module deployed without its sidecar produces a working-looking install that does nothing.**
Since the integrity gate landed, the unified module verifies its package against
`nps_nspire.luax.sha256.tns` sitting beside it and registers a stripped table when that file is
absent. The document then reads "StepCAS unavailable (integrity: missing)" and "Giac : NO.", with
every native surface off and no crash. Measured on the emulator 2026-09-05, and adding the 86-byte
sidecar alone, with the module untouched, brings the whole thing back. `scripts/deploy-device.sh`
refuses to send a `.luax.tns` that has no sidecar beside it, and reads back and compares everything
it sends.

Some things about that loop cost time to find, so they are worth knowing.

- **A stale copy of a module anywhere under `/documents` shadows the one you just deployed.**
  `nrequire` is `file_each(get_documents_dir(), ...)` matching on basename alone
  (ndless-src/ndless/src/resources/luaext.c:53), so it walks the whole tree.
  `images/nspire-os.img` carried `/documents/emulator/nps_nspire.luax.tns` from an old session and it
  won over a correct deploy into `/ndless`, which reads as an incompatible build rather than a stale
  file elsewhere. Taken out of the seed on 2026-09-05.
- **`deploy` can report "transfer complete" on a truncated file.** Measured 2026-09-05: a send whose
  output carried both "Link transfer complete." and "Link transfer failed." returned success, and the
  module on the calculator was 57344 bytes of 3996377. The mechanism was read out of the tool rather
  than guessed: tool_deploy in nspire.py searches an accumulated buffer for the completion line
  before the failure line, and send in session.py prepends any output the transport has already
  labelled as belonging to an earlier command. So a completion line from a previous transfer can be
  read as this one's, and completion wins whenever both are present. Fetch it back and `cmp` it,
  every time. That is what `scripts/deploy-device.sh` does for the handheld path.
- **A filename that does not end in `.tns` cannot be transferred at all**, on either target.
  `nps_split.luax.tns.sha256` was refused by the emulator link ("Didn't get 04", then "Link transfer
  failed") and by the handheld's file service ("write: Invalid input"), while the same bytes under a
  `.tns` name went over first time. That is why the unified sidecar is spelled
  `nps_nspire.luax.sha256.tns` rather than `nps_nspire.luax.tns.sha256`.

- **The emulator's link refuses a name with two extensions, intermittently.** `nps_split.luax.tns`
  fails with "Link transfer failed" while the same bytes as `stepcasx.tns` transfer. `ln mv` does not
  rescue it: this OS refuses the rename even for `.tns` to `.tns`, measured twice on 2026-09-03. What
  works is sending the two-extension name again, unchanged, because the refusal is intermittent. And
  a send can fail while printing "Link transfer complete", so fetch it back and `cmp` before
  believing it. The handheld's file service takes the real name, so `device put` needs no dance.
- **A document that is open cannot be replaced over the link.** Sending `nps_v2.tns` while the
  calculator has it open fails the same way. Send it under a new name or close it first.
- **The keysvc reply is the receipt.** `Link svc reply (2 bytes): 6b 01` means the key was accepted
  and is posted right after the reply. No reply line means the OS never answered and the key did
  nothing.
- **Do not send keys to the handheld back to back.** Two chained `nsptool.new key` calls, and later
  nine, took the calculator off the USB bus entirely, twice, needing a manual restart both times.
  Every sequence that survived had a screenshot between the presses. Since then, fourteen keys at
  five and six second gaps have run without a drop. See ../.Internal/open-questions.md.
- **`lua_toboolean` was the wrong function in Ndless's syscall table.** It reported false for a Lua
  `true` inside a .luax on 6.2.0.333, which silently skipped the Giac cross-check and is why the
  module has `solve_local` rather than a flag. Every CX II table in
  ndless-src/ndless/src/tools/MakeSyscalls/idc named an address about 0xC700 bytes past the real
  one, a string switch that happens to return something. Found by disassembling both on the
  emulator, fixed in the six tables, resources rebuilt, and probe/boolprobe shows true 1, false 0,
  nil 0 on both the emulator and the restarted handheld where both showed 0, 1, 1 before. Detail
  in ../.Internal/tooling.md.
- **The emulator does not idle into a power routine.** That was the 2026-09-02 reading; the resets
  were keysvc's, and with the 2026-09-03 keysvc the emulator sat idle in Ki V2 for two minutes and
  stayed up.
- **`make device` used to compile nine of the eleven sources.** `device_probe.cc` and
  `lua_module.cc` were left to the link targets, so a signature change went a whole session
  without either being recompiled, and the probe kept linking an object built against the old
  header. The target now names both objects.

## Where to start reading

`include/nps/core/ast.h` for the shape of everything, then `src/steps/differentiate.cc`, which is the
clearest example of what a rule looks like: it decides which rule applies, records a step saying so,
and recurses for the parts, so the record is the explanation rather than a log of it.
`src/steps/integrate.cc` is the same shape with a Check step at the end. Then `tests/golden/` for
what a finished derivation actually contains, and the "Ki V3" section at the end of `lua/nps_v3.lua`
for how the document shows one.
