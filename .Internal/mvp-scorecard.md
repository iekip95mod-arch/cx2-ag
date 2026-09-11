# MVP scorecard and retained review

## Current acceptance boundary, 2026-09-08

Use PRD sections 19.9, 20.2, 22.1 and 27 with the current [family catalog](../nps/catalog/families.md), acceptance corpus report and traceability report. Generate the reports through the CMake check workflow in [nps/README.md](../nps/README.md). A reporting target can succeed while requirements remain unevidenced or a case population is empty. Complement corpus evidence with fixtures for populations it does not exercise.

The September 8 generated reports evidence 85 of 102 MVP requirements. The [menu inventory](../docs/menu-walkthrough-plan.md) identifies supported native cases in 19 of 139 mathematical entries. Section 27 metadata is complete for 7 of 18 catalog families with 85 field gaps. These populations measure different obligations. None is an MVP completion percentage.

Native SolveTask owns persistent linear and rearrangement work. The Lua bridge still solves synchronously, so UI incrementality remains unfinished. The [physical rendering check](../nps/benchmarks/BUDGETS.md#equation-opening-check-2026-09-08) qualifies narrow input, hint, detail and equation-opening behavior on the deployed rendering release. The later native task and hardening work has no handheld execution evidence. Runtime persistence remains unqualified.

Release acceptance still requires the complete configuration on physical hardware with UI and Giac, including interaction, mathematical display, cancellation and recovery, standalone solves and frozen performance budgets. Current host verification and the historical rows below are separate evidence. No current MVP completion verdict is asserted here.

## Historical scorecard, 2026-09-05

The rows, counts, ownership labels and source locations below are the review snapshot from that date. They are retained to explain the corrections and do not describe the latest implementation or test population.

The finish line is PRD section 20.2, which lists fifteen criteria and accepts the MVP when all
fifteen hold. This file tracks them against evidence rather than against nps/STATUS.md, which was
found stating three separate false things on 2026-09-05 and should be treated as unverified until
each number in it is checked against the thing it describes.

Every row below is either cited or marked as unchecked. A row marked unchecked is a claim nobody has
established, not a claim believed to be false.

## Where the MVP stands

| # | Criterion | State | Owner |
|---|---|---|---|
| 1 | Installs and launches on a supported non-CAS CX II | met, handheld deploy fixed | budgets |
| 2 | Solves the reference corpus without TI's CAS | blocked on the corpus | corpus |
| 3 | Every transformation generated from a recorded rule | no universal check | corpus |
| 4 | Every transformation has a typed verification record | no universal check | corpus |
| 5 | Any major step teaches its rule and how to apply it | no universal check | corpus |
| 6 | Exact results stay exact unless approximation is asked for | no universal check | corpus |
| 7 | Physics shows selection, isolation, substitution, units, check | met for 1-D kinematics | |
| 8 | Unsupported inputs fail clearly with no invented steps | no universal check | corpus |
| 9 | Usable within measured memory and response-time budgets | measured on the emulator, peak open | budgets |
| 10 | The complete MVP corpus passes | blocked on the corpus | corpus |
| 11 | The invalid corpus makes no false claim of a verified solution | blocked on the corpus | corpus |
| 12 | 10,000 fuzzed expressions complete without crash or hang | met, nps/CMakeLists.txt:668 and 720 | |
| 13 | Deployment contract fixed, Giac validated, isolated-device test | partial | integrity |
| 14 | Identical serialized contexts give identical derivations | met for algebra and calculus, not physics | lead |
| 15 | Every P0 and applicable INV requirement has linked evidence | 51 of 102 unevidenced | all three |

Eleven criteria are open. Only 7 and 12 are met outright, and 1 is met and at risk.

## Correction, 2026-09-05

An earlier version of this file marked criteria 3, 4, 5, 6 and 8 as met, with no citation, inferred
from the architecture and from nps/STATUS.md. That is the same method that produced three false
premises earlier the same day, and criterion 14 sat on the same believed-met list until somebody
looked and found no test at all. The rows are corrected above.

What those five have in common is the word "every". Each is a claim about all displayed
transformations, and nothing in the tree walks every produced derivation asserting the invariant.
nps/tests/unit/derivation_tests.cc:259 walks a record for parent and child consistency, which is a
different property. The step structure supports all five: nps/include/nps/steps/derivation.h:111-114
gives a step its rule_id, rule_name, explanation_short and explanation_detailed, and lines 120 and
126 give it its verification list and a verified() predicate. Structure supporting a claim is not the
claim being true of every step.

This matters for sequencing. A universal claim needs a population to quantify over, so criteria 3, 4,
5, 6 and 8 cannot be evidenced before the section 22.1 corpus exists, and they are then evidenced
almost for free by one invariant pass over it. That makes the corpus the critical path for eight of
the fifteen criteria rather than three, and it makes the invariant pass a deliverable of the corpus
work rather than a separate job.

The five invariants, stated as a check would have to state them:

- Criterion 3: every Transformation step has a non-empty rule_id naming a registered rule, and every
  plan or check step comes from a registered strategy or validation record.
- Criterion 4: every Transformation step has at least one verification record whose outcome passed,
  and its evidence strength is readable.
- Criterion 5: every major step has a non-empty rule_name and a non-empty explanation_detailed.
- Criterion 6: no step introduces an inexact value unless approximation was requested, and the
  significant-figures rounding step is the single sanctioned exception.
- Criterion 8: every refusal produces no Transformation step after the refusal point and makes no
  claim of a verified solution.

## The open six, and what each actually needs

**Criterion 15 is the one that measures the rest.** nps/build/host/traceability.md reports 51 of 102
MVP requirements evidenced. The 51 unevidenced ones are a finite list and are split by lane:

- budgets: PERF-001, PERF-002, PERF-005, PERF-006, PERF-008, PERF-010, PERF-011, PERF-012
- integrity: PLAT-001 to PLAT-004, PLAT-007 to PLAT-011, PLAT-013
- corpus: the remaining 33 across MATH, STEP, ALG, PHYS, VER and UI

Some of these are implemented and merely untagged. PERF-005 and PERF-006 have measured handheld
numbers from 2026-09-03 and are still unevidenced, which is a tagging gap rather than a measurement
gap. Others are genuinely absent. PERF-008 must stay untagged either way, and the two limits it is
missing are not the same kind of gap. Repeated canonical states are tracked nowhere, so that limit
cannot fire and it guards a hazard the fuzz run currently covers only statistically. Branch count is
different: no engine emits a branch at all, and the only caller of Derivation::add_branch in the tree
is nps/tests/unit/derivation_tests.cc:239, so a counter would read zero forever. Enforcing that half
would be a guard nothing can fire, which is the failure the project's own rule names.

**Criteria 2, 10 and 11 are one piece of work.** PRD section 22.1 asks for 820 minimum distinct
semantic cases: 150 linear, 300 derivative, 120 integral, 100 kinematics and 150 invalid or
adversarial. The tree has 37 golden fixtures and no corpus directory. The runner is being built as
nps_acceptance_corpus, because the name nps_corpus_audit is already taken by an unrelated EPUB
metadata audit at nps/tools/corpus_audit.py, wired at nps/CMakeLists.txt:684.

Section 22.1's wording, "not cosmetic variants with different numbers", is the hard part. The gate
counts distinct semantic signatures rather than case records, so padding the corpus with renumbered
copies cannot move the number.

**Criterion 9 has memory now, on the emulator only.** Latency is measured: 20 ms and 10 ms on the
handheld, 2026-09-03, and 400 ms on the emulator for the two-hop kinematics solve, 2026-09-05. The
hard case is the one to quote. It leaves a factor of 12.5 against PERF-006's five seconds where the
worked examples leave 500 and 250. Memory was measured 2026-09-05 and is in nps/benchmarks/BUDGETS.md: with the
unified module, Giac and the Ki V4 document resident, 18.4 MiB free at launch and 13.8 MiB of it in
one contiguous block, against 28.4 MiB free with nothing of ours loaded. Measured again from inside
the module on a clean boot, which is the only route the handheld will ever have: 19609 KB free and
13120 KB contiguous at launch, 19326 KB and 13120 KB after a linear solve, and the same two figures
again after the two-hop kinematics solve, which is the largest derivation on record at 100 nodes and
15 steps. A solve grows the heap once and not again, and moves the largest block by less than the
instrument can see.

Every one of those totals is a lower bound rather than a measurement. measure_total_free's inner
bisection stops while its interval is still one resolution wide and counts the understated end, so
the shortfall runs to one 64 KiB resolution per free run, and the probe reports 7 to 9 of those, for
at most 576 KB. It errs safe for a floor, so the budget rows hold, and it still swamps a small
difference: the 283 KB this file used to quote as the cost of a solve is inside the noise and has
been withdrawn.

Three things keep this criterion open. **The readings are from the emulator and PRD section 19.6
wants hardware.** That is not laziness about the handheld: only the debugger's exec keeps a Lua
document alive across running a program, the handheld has no exec, and opening a program from its
file browser tears the Lua state down first, measured both ways on 2026-09-05. So a handheld number
has to come from inside the release module. nps_nspire.heap_free is that call, added by the integrity
lane and displayed by nps_v4.lua, and the module and document are deployed and verified on the
handheld. The reading itself needs one keypress to open the document, which keysvc cannot supply
there. **End-to-end peak memory is unmeasured** and cannot be reached by
sampling free heap between operations. The only instrument for it is the wrapped-allocator build,
and nps/CMakeLists.txt:371 passes NPS_RESOURCE_PROFILE as the manifest's diagnostic_only flag, so
that build declares one diagnostic module and nps/lua/nps_v4.lua refuses to load it. **PERF-010's ten
budgets are proposed rather than frozen**, six with a measurement behind them and four saying so.

One correction to what this file said before. The 25 MiB from probe/heap.c does not answer PERF-011,
but not because luagiac may have been absent. That probe counts free memory in 1 MiB chunks, which
loses whatever does not divide the free runs. Against both a 64 KiB count and a repeated
largest-block walk it understates the same heap by 2.0625 MiB with nothing of ours loaded and by
2.5 MiB with everything resident. Those two agree with each other within 65 KB, but they share a
64 KiB granularity bound, so their errors are correlated and the agreement is a consistency check
rather than a proof. Rerunning the walk at a 512-byte resolution would make it one. The allocator headroom function's perturbation is also no longer unverified: it restores the
heap it took, over 100 consecutive calls and across taking and releasing every free block twice. Its
latency is measured only on the emulator.

**Criterion 13 is partial and its evidence is worth less than it looks.** The runtime does enforce
the unified package sidecar as of commit e5d46c2, which nps/STATUS.md:847 and
.Internal/open-questions.md:383 both still deny. But nps/src/platform/nspire/integrity.cc:216 puts
verify_unified_integrity and hash_package inside a NPS_INTEGRITY_GIAC guard that
nps/CMakeLists.txt:533 and 540 set only on the two unified device objects. The code that reads
package bytes and digests them therefore exists in one binary and no test on any target can execute
it. Missing and corrupt are proven at the sidecar-parser level and with a scripted mismatch string,
never by corrupting a real package. By the project's own rule, that is a guard left in the tree
without being shown to work.

Two of the three packaged components also carry sidecars that nothing reads.

**Criterion 14 holds for algebra and calculus and fails for physics.** It had no test at all until
2026-09-05, when nobody had named it: context_tests.cc proved a serialized context parses back and
refuses bad blobs, which is a different claim.

There is now a replay test at nps/tests/unit/context_tests.cc, evidencing PLAT-013. It solves a
problem, serializes the context, restores it twice with unrelated solves run in between, and requires
both replays to reproduce the original derivation and outcome rather than only the answer. Six cases
including a refusal, an unsupported integral and a domain-restricted one. Falsified by giving the
solver hidden call-order state, which PLAT-013 names as forbidden: all 13 checks fail and the
evidence tag flips to false. The first version of this test was a half measure that compared two
replays to each other and never to the original, so it would have passed on a replay that ignored the
restored model entirely.

Physics does not replay, and physics is the point of the project. src/physics/kinematics.cc:566
records a context whose normalized_problem_model is what solve_body returns through an out parameter
at lines 1204 and 1215: the selected, substituted equation, an output of the solve. The problem
itself, the unknown and the known quantities with their units and precision, is not in the context in
any form a replay could read. So reopening a saved kinematics solution cannot rebuild the equation
selection, the unit conversions, the dimensional check, the planner's route or the significant-figures
reporting. The other physics families that record a context need checking the same way rather than
assumed to differ. This is task #9 and criterion 14 is not met until it closes.

**Criterion 1 is met, and the risk it carried is closed for the handheld path.** The four modes have
run on the physical handheld and on the emulator. The risk arrived with e5d46c2: the unified module
refuses to load unless its SHA-256 sidecar sits beside it, and nothing deployed the sidecar.

Reproduced on the emulator 2026-09-05 and falsified in both directions. With the module on the
calculator byte-identical to the build and no sidecar beside it, the document reads "Giac : NO." and
"StepCAS unavailable (integrity: missing)", every native surface off and no crash. Sending the
86-byte sidecar alone, with the module untouched, brings the whole thing back to "Giac 1.9.0 : OK."
So the sidecar is the entire cause.

nps/scripts/deploy-device.sh is the fix for the handheld path: it derives the sidecar name the same
way the module does, refuses outright to send a .luax.tns that has none beside it, and reads back and
compares every file it sends. Both refusal branches and the mismatch branch were falsified with a
stub before the script was trusted, and the working path was run against the physical calculator.

The emulator path is not fixed and needs a decision. It goes through the nspire MCP deploy tool,
which lives outside this repository. That tool has two defects measured the same day: it does not
carry the sidecar, and it reported "transfer complete" for a send that left 57344 bytes of a 3996377
byte module on the calculator, because both the completion and the failure line appeared in its
accumulated output and it tests for completion first.

## What is not on the critical path

The 44 word-problem requirements are staged WP1 to WP3 rather than prioritised, and PRD section 24
makes word problems an explicit stretch goal. They are counted separately in the traceability report
so they cannot be quietly dropped, and they do not gate the MVP.

P1 and P2 requirements do not gate it either. Criterion 15 names P0 and applicable INV only.
