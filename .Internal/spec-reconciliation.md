# Reconciling the agent pack with the tree that exists

This is the retained September 3 and 4 reconciliation. Absence claims, source locations and catch-up limitations describe those sessions. The supplied requirements and recorded personal-distribution decisions remain source material. Current implementation is described in [the codebase map](../docs/codebase-map.md), [menu walkthrough plan](../docs/menu-walkthrough-plan.md) and [MVP scorecard](mvp-scorecard.md).

The pack in .Internal/agent-pack was written for a greenfield repository called nspire-physics-solver.
This tree is not that repository. It is further along in some places than the pack assumes and behind
it in others, so the pack cannot be executed as written. This document says which is which, with the
evidence, so nobody has to re-derive it.

The associated historical work plan is [ki-v4-plan.md](ki-v4-plan.md). Current build status is in [nps/STATUS.md](../nps/STATUS.md).

## What the pack contains

Six documents and two task briefs. Nspire_Physics_Solver_Standalone_Agent_Prompt.md is not a seventh:
its first 1537 lines are byte identical to MASTER_AGENT_PROMPT.md, and the rest is
ARCHITECTURE_SPEC.md, M0_FEASIBILITY.md and REFERENCES.md concatenated. Verified with diff. Read the
master prompt and the three separate files, and skip the standalone.

The normative surface is MASTER_AGENT_PROMPT.md sections 1 to 34, ARCHITECTURE_SPEC.md sections 1 to
14, and the two task briefs. RFC 2119 keywords are used properly throughout, so MUST and SHOULD
carry their usual weight.

## The corpus audit reproduces, with one heuristic difference

Not taken on trust. The EPUB at <workspace>/../FUNDAMENTALS-OF-PHYSICS-62-pages.epub was
opened as a ZIP and every count in CORPUS_AUDIT.md section 3 and section 4 was recomputed from the
markup with a structural parser.

```
sha256 8bb0f9ef0a84425af44ea85664039714f49a61b3bdd768b9d614ba2a385bc5dd
1861 zip entries, OEBPS/content.opf, OEBPS/nav.xhtml, 62 chapter files
```

nav.xhtml maps the 62 files to 7 front-matter pages, 44 numbered chapters (chap8 through chap51),
appendices A to G, an answers section, an index, a formulas page and the licence. The 62 in the
filename is the file count, not a page count.

Reproduced totals: 44 chapters, 270 numbered sections (h2), 282 sample problems (h4 beginning
"Sample Problem"), 203 checkpoints (h4 beginning "Checkpoint"), 1782 images, 1782 of them with
nonempty alt. Every per-chapter row in the audit's section 4 table matches as well.

One figure differs. Counting any heading containing "proof" or "derivation" gives 25 rather than the
audit's 23. The two extra hits are chapter 17's "Formal Derivation of Eq. 17-3" and chapter 34's
numbered section heading "34-6 Three Proofs". Restricting to h3 and h4 and to headings that begin
with the word reproduces 23 exactly. The audit already says its problem-paragraph detection is
heuristic, so this is a documented class of difference rather than an error.

Conclusion: the audit is trustworthy and its method is reproducible. Treat the counts as facts.

## Where the tree already satisfies the spec

These are not aspirations. Each is in the tree now.

- Project-owned semantic AST, no Giac type anywhere near it (§7, §8, ARCH §14 first rejection
  criterion). nps/cas/giac_adapter.h includes ast.h and nothing else. The core has no Giac header.
- Immutable arena-owned nodes addressed by a fixed-width ID (§9.1, §9.2). nps/core/ast.h NodeId is a
  uint32_t and Node is written once.
- Full hash consing on every node (§9.2, listed there as optional after measurement). src/core/ast.cc
  add_node keys an intern table on kind, text and child IDs before allocating, so structurally equal
  subtrees are one node.
- Interned symbols and strings (§9.2). Arena::intern, one string table per session.
- Explicit node-count and depth limits with a typed failure (§9.2, §5.3). nps/core/budgets.h Limits and
  nps/core/ast.h Status: DepthExceeded, SizeExceeded, InputTooLong, SyntaxError.
- A cooperative budget with a typed halt rather than a hang (§5.3). nps/core/budgets.h Budget, Meter and
  Halt. The meter counts rewrites, steps and backend calls, and polls for cancel on a stride.
- Typed outcomes on every public path (§25). DerivationStatus has 14 values, KinematicsOutcome 8,
  ResultTag 9, Halt 5, Status 5. Nothing returns a bare bool for a solve.
- The derivation is a record with provenance, not a list of strings (§19, ARCH §14 second rejection
  criterion). nps/steps/derivation.h Step carries rule_id, rule_name, phase, goal, assumptions before and
  after, domain_restrictions, claim, proof_obligations and a list of VerificationRecord. The payload
  is a tagged union so a Plan record cannot be read as a Transformation.
- Verification distinguishes what a step claims from whether it was checked (§18.1). ClaimType and
  VerificationOutcome are separate enums, and derivation.h says outright that conflating them is the
  bug being prevented.
- A CAS operation allowlist, so no caller can pass free text through (§17.2 second bullet, §26 first
  bullet). nps/cas/giac_adapter.h Op is a closed enum of ten operations and Request names one.
- Dimensional checking as a derivation step with its own Check record (§12.3). check_method is
  "dimensional analysis of both sides".
- Exceptions off on target (§5.1). Makefile line 27 DEV_FLAGS carries -fno-exceptions.
- Host sanitizer and fuzz profiles (§26, ARCH §11). make san and make fuzz, both clean.
- Semantic and CAS spellings kept apart (§8.3). nps/core/print.h has print and print_giac as separate
  functions, and says why.
- A planner that searches rather than selects (§15.1, §15.2, added 2026-09-04).
  src/physics/kinematics.cc chains backward from the unknown, deepening one hop at a time, and drops
  a branch that needs more quantities than there are hops left. §15.2's forward closure and
  dimensional pruning are not in it: see the absent list below for why.

That is most of ARCH sections 3, 4 and 14, and most of master sections 8, 9, 17, 19 and 25. The
architecture the pack asks for is largely the architecture already here.

## Where the tree conflicts with the spec

Five real conflicts. Four are closed, and the fifth is closed in part: the build system converted,
the layout and the target names did not.

### 1 and 2. C++11 and RTTI, both closed 2026-09-03

The spec requires C++20 with exceptions and RTTI off on target (§5.1). The tree was C++11 with
-fno-exceptions and no -fno-rtti. Both are now taken. Written up in ki-v4.md.

Four places pinned the standard, not two: Makefile HOST_FLAGS and DEV_FLAGS, tools/tidy.sh's
clang-tidy invocation and tools/lint-resident-exit.sh's clang-query translation unit. All four moved
together, and -fno-rtti went to the three that build target code.

No source changed. The reason to take it was never conformance: std::span over a contiguous child
pool is the natural shape for fixing conflict 3 below, and consteval tables are the natural shape for
the knowledge packs in §14.3.

### 3. Per-node heap allocation, closed 2026-09-03

Node held a std::vector<NodeId>, so every node with children cost its own allocation, against §5.1's
ban on per-expression new and delete and §9.2's compact contiguous child pool. It is now an offset
and a count into one pool the Arena owns, read through a ChildView, which is the ExprNode layout the
spec sketches in §9.1. Measured in ki-v4.md.

The number worth carrying forward: on the device the node did not shrink. sizeof(Node) is 40 before
and 40 after, because on 32-bit ARM the 12-byte vector and the 8-byte pair both round to the same
struct size once int64_t small has set the alignment. The host shrank from 56 to 40 and that figure
is about the wrong machine. What the target actually gained is between 3 and 16 per cent fewer
allocations, and the loss of a whole class of dangling-reference bug: five sites used to copy the
child list defensively and no longer need to.

### 4. The Giac adapter builds command strings, closed 2026-09-03

The spec (§17.2 first bullet) says convert the AST to CAS objects directly and do not build and
parse arbitrary command strings. src/cas/giac/giac_typed.cc now does that for the unified build: the AST
becomes a giac::gen, the operation is an ordinary C++ call, and the answer gen is walked into the
arena. Written up in ki-v4.md.

One qualification, and it is deliberate rather than unfinished. The string path stays, as the
fallback a backend gets when it declines the typed call. Ki V3 is two images and reaches Giac across
a Lua call, which can carry text and cannot carry a gen, so removing the string path would remove
Ki V3. Both go through Adapter::build_command first, so the allowlist stays single sourced.

What the change actually bought, beyond the two conversions per call: Giac's own trouble is now read
structurally rather than by matching "Error" and "Time limit" as whole words in its answer text, and
a result with no node in this project is refused on its type tag by name. Both were places where
Giac's reply spelling had already caused a defect.

### 5. Build system converted 2026-09-03, layout and naming 2026-09-04

Three separate asks, and they got three different answers.

**CMake and Ninja with a checked-in toolchain file (§5.2): done.** nps/CMakeLists.txt and
nps/cmake/toolchains/ndless-arm926ej-s.cmake. Three trees, one per compiler, and the Makefile
stays as a forwarder holding no build logic, because `make -C nps <target>` is written into
about twenty-five places across these notes and there was no reason to break all of them.

I had recommended declining this and the maintainer took it anyway, which was the better call: the conversion
found two real defects that the Makefile had been carrying. A rebuilt Giac object relinked nothing,
because the sub-make that builds Giac ran inside a recipe an up-to-date .tns kept from running. And
the DIAG stamp file existed only to work around make not tracking flags, which ninja does. Both are
written up in STATUS.md.

The evidence that the conversion is faithful is byte comparison rather than a green build: five of
the six artefacts come out byte-identical to what the Makefile produced, from a wiped tree. The
sixth is nps_bench.tns, whose kept copy predates the typed adapter change to giac_adapter.cc by
half an hour. It differs because it was stale, not because the build differs, and the proof is that
nps_split.luax.tns links the same core objects and does match.

**The §28 tree layout: done 2026-09-04.** I had recommended declining it as conformance to a
document. the maintainer took it. Headers live under include/nps by area and are spelled nps/<area>/x.h at
every use, sources sit under src by the same areas, tests are split by kind, and the project
directory is nps. tests/corpus stayed uncreated: there is no corpus yet, and an empty directory
named for one reads as coverage.

The move was checked by rebuilding and comparing bytes rather than by the build going green, and
every artefact came back byte-identical to the pre-move build. STATUS.md has the detail, including
the one thing that did move and why it was not the layout.

**The nps_core, nps_host and nps_nspire.tns names: done 2026-09-04, with one deviation.** My
objection was that §5.2 names one Ndless application where this project ships three. That is still
true, and the mapping is how it was resolved: nps_nspire is the unified image the spec means,
nps_split is the same core for the two-image arrangement, and nps_bench is the profiling program,
which §5.2's own item 6 already provides for. STATUS.md carries the full old-to-new table.

The deviation is the extension. §5.2 writes nps_nspire.tns; the artefact is nps_nspire.luax.tns,
because ndless-sdk/ndless/src/resources/luaext.c:53 builds a module's filename as
name .. ".luax.tns". A Lua module named plain .tns cannot be found by nrequire, so the spec's own
spelling would produce a file nothing can load. nps_bench.tns is a standalone program rather than a
module and keeps the plain extension.

Two of §5.2's six targets are still absent. nps_kb_compiler and nps_corpus_audit belong to
milestones that have not happened, and a target named for work that does not exist is scaffolding.

## What is absent, and which milestone it belongs to

Ordered by what blocks what.

- **Precision and significant figures (§13, partly done 2026-09-04).** Quantity carries a Precision
  now: an integer literal is exact, a decimal literal is measured, and its significant digits are
  counted from the first non-zero one. The kinematics answer is reported to the fewest among the
  measured givens its route used, as the last step of the solve, with the exact value kept beside it.
  Two of §13.1's six categories are modelled and four are not: an exact defined constant, a symbolic
  constant, an approximate decimal result and a value with explicit uncertainty. §13.2's last
  significant decimal place and explicit uncertainty are absent for the same reason, that nothing
  reads them. The combination rule is multiplication's applied to the whole route, which overstates
  where a subtraction cancels leading digits, and fixing that means precision inside the canonical
  folder and the linear solver.
- **Vectors and frames (§11, M1 requirement, partly done 2026-09-04).** nps/units/units.h has a
  Vector: components, a rank of two or three, one shared unit and a Frame, with addition,
  subtraction, scaling, dot, cross and an exact magnitude. Every operation refuses a frame it was not
  given, which is §11.2's last line and §14's "treats vectors as unframed lists" rejection. Entry
  reads both "3 i + 4 j m/s" and "(3, 4) m/s". The plan's step 24 asked which family comes first and
  the pack's §13 answers it: archetype 5 is 2-D vector addition, ahead of the dot product and
  projectile motion. What is still absent is a basis transformation, so a second frame can be refused
  but not converted between; a vector in nps/core/ast.h, so these are values and not expressions; and
  any physics family that uses them, so archetype 5 has a core and nothing that solves with it.
- **Seven base dimensions with rational exponents (§12.1).** nps/units/units.h Dimension has three integer
  exponents for length, mass and time, and its own comment says the others join when a family needs
  them. Rational exponents are not merely for completeness: a wave or oscillation family produces
  square roots of dimensions and integer exponents cannot hold them. Affine temperature and the
  semantic angle tag are absent too.
- **Declarative physics knowledge (§14, MUST).** Laws are a C array in src/physics/kinematics.cc, not data
  compiled from an authoring schema by a host tool. Four SUVAT equations do not justify a knowledge
  compiler, but the second family will, and §14.3's list of what the compiler must validate is worth
  keeping for when it does.
- **The planner (§15, partly done 2026-09-04).** The backward search is in and is listed above.
  Four of §15.2's nine steps are not: forward-chaining cheap consequences (step 5), rejecting a
  candidate as dimensionally impossible before trying it (step 6), scoring candidates (step 8) and
  best-first order (step 9). The search is first-fit under iterative deepening instead, so the
  shortest route wins and ties go to table order. Steps 5, 6 and 8 all want a table where trying a
  candidate costs more than filtering it, and on four equations trying one is a solve_linear call on
  a scratch derivation. §15.1's planner state and §15.4's cost model are absent for the same reason:
  the state is three vectors and a used-flag array, and there is nothing yet to choose between.
- **Strategy points (§15.3).** BranchPayload exists with siblings_exhaustive, so the record shape is
  ready. The planner still produces none. It takes the first route that works at the shallowest depth
  that has one, and records each equation it passed over as a plan alternative with the reason. A
  branch record wants two routes both explored and one preferred, which is the shape §15.3 asks for
  and this search does not yet have.
- **Scene model and free-body diagrams (§20).** Absent entirely, and correctly deferred.
- **Three-level step grouping (§16.4).** The spec asks for Compact, Standard and Detailed, and says
  the grouping must be driven by the rules rather than by summarising prose. lua/nps_v4.lua line 1972
  has two levels named "standard" and "beginner", cycled by shift+tab or help. They choose
  which text a step shows, not which steps collapse into one, so no grouping happens at any level.
  nps/core/context.h detail_projection would be where a level was recorded. Every engine sets it to the
  literal "standard" and nothing reads it back. Closer to a stub than a partial.
- **Knowledge packs (§22).** No pack boundary, no load or activate, nothing measured. Premature.
- **Corpus manifest and archetype coverage (§23).** No manifest, no archetype IDs, no coverage
  reporting. The audit numbers above are the first piece of it.
- **Traceability report (§19.9) and the coverage catalog (§27), both started 2026-09-04.** Both
  generate now: nps/tools/traceability.cc joins the PRD's own requirement tables against evidence the
  test run wrote, and nps/tools/coverage.cc checks nps/catalog/families.md against the golden
  fixtures. What they report is the useful part: seven of a hundred and sixty five prioritised
  requirements have evidence, so MVP criterion 15 is not close, and §28's rule that no problem is
  supported until the report is clean now has a report to be clean against. The mutation check
  §19.9's last sentence asks for is in too: each golden fixture loses its claim line and the suite has
  to fail while it is gone, which all 24 do. Proof-obligation coverage is reported separately, as the
  same section asks: an obligation carries an id now instead of being prose on a step, so the catalog
  can name the ones a family raises and the tool can join the two in both directions. Three of
  §19.9's five links are still missing (implementation components, device evidence, release status).
  Nine of §27's twenty-eight fields are not carried. All of that is listed in the generated reports rather than in prose here, which is
  the point.

## What the pack changes about the plan

Three things, and only three.

1. **The two-hop gap is now a specified requirement, not an observation. Closed 2026-09-04.** ARCH
   §13 item 4 and M1 item 4 both name "two-stage catch-up kinematics requiring an intermediate event
   equation" as a first-milestone archetype. The probe in ki-v4-plan.md showed the tree refusing
   exactly that shape, which moved the chaining search from "worth doing before the second family" to
   "required by M1". It was written next, and the probe's own case now answers 11 m/s on the
   calculator through v0.

   Half of archetype 4, not all of it. The archetype is worded twice and the two wordings ask for
   different things. ARCH §13 item 4 says "requiring an intermediate event equation", which is what
   the search does. tasks/M1_VERTICAL_SLICE.md item 4 says "catch-up/equal-position" and its rule
   list asks for "solve a small linear system if needed by catch-up form", which is two bodies whose
   positions are set equal. KinematicsProblem has one body, no entities and no way to say that two
   of them meet, so that half is untouched. It belongs with §11's vectors and frames, which need the
   same thing: a problem model with more in it than five symbols.

2. **Precision metadata joins the critical path. Done 2026-09-04.** It was not on the plan at all and
   M1 requires it. A physics answer that cannot say how many figures it is entitled to is not
   finished, and the corpus teaches significant figures from chapter 1. What went in is the input
   side and the reporting side; the combination rule is the textbook one rather than a per-operation
   one, and the absent list above says where that costs a figure.

3. **C++20 is nearly free and unlocks the child-pool fix.** Verified above. Worth taking early
   rather than late, because every traversal written under C++11 is one more to convert later.

Everything else in the pack either describes what is already here or describes work the plan already
sequences. The pack is a better-argued version of the same architecture, and the places it disagrees
with the tree are the places worth arguing about.

## Gates the pack raises that this project has not answered

- **Giac licensing (§6.1, M0 blocking).** Already answered, and the pack does not know it. The
  standing decision in .Internal/README.md is that licensing is out of scope because this is a
  personal mod rather than a distributed product, which is why luagiac is bundled instead of
  required as a separate install. The pack's M0 gate exists because it assumes distribution. The
  gate is discharged by the distribution model, not by an ADR. If that ever changes, §6.1's three
  options are a good starting list and this is the paragraph to come back to.
- **Corpus content hygiene (§6.2, MUST NOT).** The EPUB stays outside this tree. Nothing derived from
  it may carry problem text, worked solutions, figures or an answer mapping. The audit numbers and
  chapter titles above are metadata and are allowed. Anything written later from the corpus needs
  this rule read first.

## Where the files are

- .Internal/agent-pack, the pack as extracted.
- .Internal/references, the Andes papers.
- The EPUB stays at <workspace>/../FUNDAMENTALS-OF-PHYSICS-62-pages.epub, outside the
  tree, as README.md and §6.2 both require.
