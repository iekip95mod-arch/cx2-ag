# cx2 codebase map

Use this reference for the subsystem involved in the current task. Shared working instructions and the common build commands are in [AGENTS.md](../AGENTS.md). Filesystem names in the prose start at the cx2 workspace root unless stated otherwise.

This document describes implementation relationships. The [product requirements](StepCAS_Product_Requirements_Document.md) define the intended product and the [hardware debugger report](hardware-debugger-findings.md) preserves a separate research investigation. Neither a design requirement nor a retained observation establishes current execution on a connected calculator.

## Reading routes

| Task | Read |
| --- | --- |
| Find the owning source or dependency | [Workspace layout](#workspace-layout), then its own instructions |
| Trace an expression through the system | [Product execution](#product-execution) and [core expressions](#core-expressions-and-arithmetic) |
| Change a solver or its evidence | [Derivations](#derivations-evidence-and-restrictions), [solver families](#solver-families) and [test evidence](#tests-and-acceptance-evidence) |
| Change physics arithmetic | [Units, vectors and precision](#units-vectors-and-precision) and the implementing family |
| Change a native or Lua interface | [Giac boundary](#giac-boundary-and-runtime-lifetime), [Lua bridge](#native-lua-bridge) and [Ki UI](#ki-documents-and-ui-ownership) |
| Build or inspect a package | [Build structure](#build-and-package-structure) and [CMake commands](../nps/README.md#build-and-test) |
| Work with the calculator or emulator | [ndl](#ndl-platform), [physical tools](#physical-device-tools) and [Firebird](#firebird-automation-and-saved-state) |
| Work on hiding or moving application files | [Document hiding](#document-hiding-and-runtime-relocation) and research/folder-hiding/AGENTS.md |

## Workspace layout

| Location | Responsibility |
| --- | --- |
| nps/include/nps | Public native types and APIs grouped by core, steps, physics, units, CAS and platform |
| nps/src | Native implementations matching the public areas |
| nps/lua | Ki document versions and their UI logic |
| nps/tests | Unit, property, golden, bridge, UI and tooling checks |
| nps/catalog | Family coverage declarations consumed by reporting tools |
| nps/tools | Coverage, acceptance, traceability, integrity, auditing and build helpers |
| nps/scripts | Host and ndl readiness checks |
| nps/benchmarks | Target probes, measurements and UI research artifacts |
| docs | Product requirements and hardware debugger findings |
| .Internal | Architecture notes, port history, platform notes, recovery guidance and planning material |
| vendor/khi-src | Active KhiCAS/Giac fork used by the unified native module |
| vendor/giac-src | Older Giac checkout retained for comparison. It is not the default unified dependency |
| vendor/ndl-src | ndl runtime, SDK, syscall definitions, packaging tools and ARM toolchain |
| vendor/firebird-src | Emulator core, headless frontend, debugger and emulated USB link |
| vendor/libnspire-src | Physical USB and NavNet transport library |
| tools/nsptool | Physical-device CLI, persistent command session and host tests |
| tools/keysvc | Resident key-event and restart service for the calculator |
| research/folder-hiding | Standalone visible calculator, document storage and ndl relocation research |
| vendor/deps | Sources and scripts for numeric dependencies such as GMP, MPFR and MPFI |
| images | Boot, flash and saved execution images. Preserve their provenance and backing relationships |
| probe and smoke | Local diagnostic programs, packages and captured observations |
| vendor/ndless-known-good, vendor/ndless-r2022 and vendor/khicas53 | Retained runtime or distribution material. Establish provenance before using it |
| vendor/libnspire-install | Local transport installation output |
| ipc | Local runtime communication area. Inspect its current contents before use |
| .headcheck*, .lane-* and .lead-tree* | Alternate checkouts, build directories and coordination files |
| device | Additional generated device build directory. Inspect its CMake cache before use |

The dependency checkouts have their own Git state. They are not configured as ordinary submodules of the outer repository. Many dependencies and research files are ignored locally. An outer Git commit does not capture their changes or reproduce the whole workspace.

The root .gitignore selectively tracks source under tools/nsptool and tools/keysvc. Also inspect .git/info/exclude for ignored authored files. The folder-hiding prototype has no separate Git repository. Do not force-add dependency or research trees without authorization.

The user's global Git ignore file matches AGENTS.md, CLAUDE.md and .codex. The workspace's shared instructions and Codex configuration are explicitly versioned. Other files matching those patterns can still be local-only. Inspect the tracked-file list before assuming that an onboarding or settings file travels with a checkout.

The device/CMakeCache.txt inspected on the mapping date named .lead-tree/nps as CMAKE_HOME_DIRECTORY. Its compiler configuration also came from that checkout. A directory named device does not establish which source produced its artifacts.

## Product execution

Prefer existing library operations before adding an algorithm. Giac supplies symbolic computation and structured row-operation callbacks. GMP supplies exact integer arithmetic and certificates. The calculator supplies D2Editor text and mathematical widgets. StepCAS owns request bounds, recorded explanations, verification and interaction where those dependencies do not provide the required contract.

Giac's my_gprintf callback carries typed operands for small-matrix reduction. The reducer now emits a row-scale event at final pivot normalization. The required dependency change is retained in nps/patches/giac-matrix-events.patch and is already applied to current khi-src. The [README](../nps/README.md#build-and-test) covers unpatched source trees and dependency rebuilds. StepCAS captures those actual operations and verifies their matrix states. It does not derive operations from the final answer. Size and algorithm restrictions remain part of the [menu walkthrough plan](menu-walkthrough-plan.md).

Ki V4 starts in nps/lua/nps_v4.lua. It loads nps_nspire through ndl nrequire and validates module integrity, capabilities and the backend interface before enabling computation.

Outside explicit step modes, Ki V4 asks the native walkthrough entry to recognize top-level solve, diff, int, simplify, expand, factor, rearrange, ref, rref and the cataloged integer commands. The restricted native parser validates their complete arguments before existing solvers run. Unhandled commands reach the bundled Giac caseval interface once. Plain CAS selection bypasses command recognition. Other physics entries accept structured Lua tables and are exposed through the Physics browser examples.

The native bridge validates arguments, creates request-owned state and calls the appropriate solver. The solver records a derivation while doing the mathematics. Optional backend work supplies checks or operations outside the local rules. The bridge then copies result fields and flattened step records into Lua tables.

Ki keeps those Lua tables for the walkthrough. Hints, branch folding and detail levels change the displayed projection. They do not run the solver again. Saved document history is a separate mechanism from native solution-context serialization.

There is no general natural-language physics interpreter in this implementation. Family coverage comes from the typed problem models and implemented input parsers. The wider capabilities of the raw Giac shell do not extend the native step-by-step coverage catalog.

## Core expressions and arithmetic

### AST ownership

Read nps/include/nps/core/ast.h and nps/src/core/ast.cc.

Arena owns interned strings, nodes and child storage. NodeId identifies a node within that Arena. Equal structural nodes are interned to the same identity. NodeStore keeps node references stable across growth and ChildView accesses the child pool through its owner and offsets.

The AST supports literals, symbols, arithmetic, function calls and distinct relation kinds. Decimal and integer spellings are retained. Small integer fields are an optimization rather than the full literal representation.

Arena failure is sticky. Once a depth or size limit is reached, later construction refuses work. Never continue as though kNoNode were a valid expression. Node identities and references do not outlive their Arena.

Approximation marks belong to interned identities. If a backend approximation shares a spelling with another occurrence, the identity retains the weaker exactness classification. Do not assume precision metadata is per textual occurrence.

### Parsing, normalization and printing

| Source | Responsibility |
| --- | --- |
| core/parser.cc | Lexer and recursive-descent parser, implicit multiplication, source errors and input bounds |
| core/canonical.cc | Structural normalization, ordering, rational folding, exactness and mathematical restrictions |
| core/print.cc | Project expression spelling and the separate Giac expression spelling |
| core/evaluate.cc | Exact rational substitution and bounded sample agreement |
| core/context.cc | Reproducibility metadata and versioned context serialization |

These names are relative to nps/src. Headers are under nps/include/nps with matching area names.

Division is represented through multiplication and reciprocal powers. Normalization makes expressions comparable, but it does not produce the teaching record. Pedagogical transformations belong in the steps layer and carry their own rule and evidence information.

Relations remain distinct. Parsing an inequality does not mean the equation solvers support solving it. Chained relations are refused by the parser.

List nodes preserve element order and nesting through parsing, printing and normalization. A list inside a product, power or call keeps its collection shape. MatrixView in nps/include/nps/core/matrix.h reads a nonempty rectangular list of rows without copying its cells. It validates shape only. read_matrix_rational admits exact integer arithmetic through the checked rational evaluator. read_exact_matrix also checks root and row provenance and can copy exact cells into a bounded row-major span. Decimal syntax, approximate provenance and symbolic cells are refused.

contains_list reads the collection flag maintained by Arena construction. Scalar solvers and scalar backend requests refuse expressions containing lists before algebra or backend execution. The command dispatcher preserves commas inside bracketed operands. These refusals do not restrict the shell's ordinary Giac matrix evaluation.

Rational in nps/include/nps/core/rational.h exposes bounded integer numerator and denominator fields. The current implementation uses GMP for intermediate arithmetic and checks narrowing back into that representation. GMP-backed intermediates do not make every native result arbitrary precision. Overflow is a refusal condition.

Sample agreement is limited evidence. evaluate_rational handles the supported rational forms and skips samples it cannot evaluate. Matching samples must not be described as a symbolic proof.

### Budgets and interruption

nps/include/nps/core/budgets.h defines Limits, Budget, Cost, Halt and Meter. Read the current defaults there instead of copying them into new documentation.

Arena enforces structural limits. Canonical normalization, associative flattening and structural ordering use explicit traversal storage so accepted expression depth does not consume a native call frame per node. Meter accounts for rewrites, steps, branches, backend calls and repeated canonical states. It polls for cancellation on entry and during work. The native UI bridge supplies a direct Escape-key poll because solves execute synchronously.

nps/include/nps/core/task.h defines TaskContext, Coroutine and Task. A context owns coroutine frames in storage supplied by the caller. make_task binds the coroutine to that context. Checkpoints limit the cooperative work done by each resume. Cancellation and frame allocation failure remain distinct terminal states.

nps/include/nps/steps/solve_task.h supplies persistent native ownership for linear solving and rearrangement. SolveTask owns its request, arena, meter, records and frame storage. Construction seeds request provenance without parsing or solving. advance counts checkpoints and advance(0) does no work. Identifier admission uses the parser's validation and normalization rules. cancel preserves the published prefix and withholds the answer. Terminal context retains provenance and conditions for the surviving published moves, including resource failures. close releases owned state. External Budget callbacks are refused because their borrowed context could expire between advances.

Published mathematical payloads, conditions and existing ancestry edges stay unchanged. Parent child lists may grow and pending strategy evidence may resolve. A verified outer move does not prove that every remaining operation has an admitted inverse. Borrowed record references must not span advance, cancel or close. Views and resource queries do no solver work.

The synchronous linear and rearrangement wrappers drain these same engines. They allocate frame storage separately from the arena and offer explicit overloads for a caller-selected capacity. SolveResources reports exact frame usage, AST counts and metered work but does not measure all retained allocations. Parsing, canonicalization and sampling remain atomic within structural limits. Their duration and the wrapper frame allocation still need device-budget qualification. The Lua bridge remains synchronous and this foundation does not establish PERF-002.

Linear numeric-mode admission and final reporting use the same terminal classification as algebra and cache replay. Exhausting a reporting step withholds the answer and records a resource limit. Rearrangement records the original formula's domain conditions on its plan before publication. New inverse operations add conditions without rewriting earlier published records. Result restrictions and context assumptions retain conditions belonging to surviving work.

The integer family already uses integer_steps to generate work through checkpoints. Its integer_method wrapper drains the task before returning and the Lua bridge calls that wrapper. Reuse this implementation when connecting persistent request ownership to UI callbacks. Existing completed-run caching belongs to Derivation. Streaming unfinished records also requires settled restrictions and honest plan, composite and branch status.

These are cooperative work limits. They do not by themselves impose a hard wall-clock deadline inside an arbitrary Giac call or bound every allocation made by the process. A backend timeout tag also does not prove that an external deadline is enforced.

## Derivations, evidence and restrictions

Read nps/include/nps/steps/derivation.h, nps/src/steps/derivation.cc and nps/src/steps/schema.cc.

| Record | Meaning |
| --- | --- |
| Plan | Strategy selection and the preconditions it requires |
| Transformation | A before expression, an after expression and a concrete action |
| Branch | A mathematical case with its condition and resolution |
| Check | An expected relation and an observed validation result |

Derivation owns the record tree and kind-specific payload storage. Records carry rule identity, goals, explanations, proof obligations, verification records and conditions. ClaimType states what is asserted. VerificationOutcome states whether a check passed. EvidenceStrength states what that check can establish.

Record insertion refuses an absent parent before changing storage or charging a branch. The root sentinel creates a root record. A parent removed by rewind cannot become the new record's parent merely because its numeric ID is next in sequence.

Step::verified requires passing verification for a claim. Derivation::outcome_from combines record outcomes and gives a failed verification precedence over inconclusive work. A returned value and a verified derivation are separate facts.

Linear solving can replay a completed derivation for the same engine, Arena, subject and unknown. Kinematics route probes share this cache with the final solve. Replayed records retain their checks and consume the normal step budget. A remembered result is valid only within its owning Arena and derivation lifetime.

schema.cc declares the permitted claims, obligations, evidence alternatives and failure behavior for named rules. tests/step_invariants.h checks emitted records against those declarations. The runtime verified predicate does not replace that schema-conformance audit.

Plan preconditions must be associated with their verification records and completed before the plan can be considered verified. Mathematical restrictions derived from expressions belong with the affected transformations. Physical modeling assumptions describe the problem setup. Preserve that distinction through serialization, catalog evidence and UI labels.

keep_verified_prefix retains only completed, verified work. A plan alone is insufficient. rewind_to trims payload arrays and repairs roots and child references. It does not reset Arena or undo its resource failure. Restrictions are settled before retaining a prefix so surviving work keeps the conditions it needs.

Unsupported input, cancellation, resource exhaustion and failed verification have different outcomes. Do not collapse them into one generic failure or manufacture an answer from an unfinished transformation.

## Solver families

The supported envelope is declared in nps/catalog/families.md. Use its family IDs when joining tests, capabilities and reports. The table below identifies the implementing concepts. Sources are relative to nps/src.

| Family | Source | Implemented scope |
| --- | --- | --- |
| Linear equations | steps/linear.cc | Collect and isolate a single unknown, inspect degenerate coefficients and check candidates by substitution |
| Pure-square quadratics | steps/quadratic.cc | Isolate the square, record real-root cases and check reconstruction within the exact rational envelope |
| Formula rearrangement | steps/rearrange.cc | Isolate a single occurrence through supported inverse operations and carry restrictions |
| Polynomial rewriting | steps/rewrite.cc | Recorded simplification, expansion and supported factoring rules |
| Rational matrix reduction | steps/matrix.cc, steps/matrix_row.cc and steps/matrix_form.cc | Actual Giac row events with exact transition certificates, complete trace checks and separate REF or RREF guarantees |
| Differentiation | steps/differentiate.cc | Recursive derivative rules with completed subwork retained on supported partial outcomes |
| Indefinite integration | steps/integrate.cc | Registered antiderivative rules, supported linear inner forms and a derivative check of the result |
| Constant-acceleration kinematics | physics/kinematics.cc | Structured one-dimensional knowns, equation selection, dependency search, exact solving and unit checks |
| Catch-up events | physics/catch_up.cc | Constant-velocity position laws, delayed starts, shared event domain and substitution into both laws |
| Relative motion | physics/relative_motion.cc | Cartesian relative velocity with explicit compatible frames and component checks |
| Unit conversion | physics/unit_conversion.cc | A recorded chain of compatible multiplicative unit conversions |
| Density | physics/density.cc | Solve the mass, volume and density relation within its typed envelope |
| Vector addition | physics/vector_addition.cc | Framed planar component addition with compatible dimensions |
| Magnitude and components | physics/vector_components.cc | Planar component and magnitude-angle conversion with explicit angle conventions |
| Work | physics/work.cc | Constant-force dot-product work with dimensions, frames and final reporting |

Do not expand these labels into broader product claims. Pure-square quadratics exclude a linear term and irrational or complex roots. Catch-up does not implement general accelerated pursuit. Work does not implement variable-force integration. The vector primitives support operations beyond the specific UI family envelopes.

Kinematics deserves a separate read. Its equation table and iterative-deepening dependency search choose a route through available knowns. Candidate equations are tested with the existing exact solver. The chosen route is then recorded through conversion, rearrangement, substitution and verification. Each linear solve adopts its new roots under the physics plan immediately after that hop's substitution, so later hops consume intermediates only after their derivation and check. Search planning and displayed derivation are related but distinct operations.

Catch-up uses the same derivation ownership rule. Its linear solve belongs under the active physics plan before shared-domain and position checks. The displayed and hinted sequence derives the meeting time before substituting it into either body's position law, including preserved prefixes after cancellation or a resource halt.

Differentiation and integration preserve partial composite work by completing the parent expression with unresolved derivative or integral terms where appropriate. Completed derivative composites collect restrictions from the resulting expression as well as the input. A square-root chain rule therefore excludes a zero denominator, including in retained partial work. Integration checks an antiderivative by differentiating it. Conditions such as a logarithm's domain remain attached to the relevant work.

The Lua solve entry tries the pure-square family before the linear family. Separate C++ families do not necessarily have separate Lua exports. The walkthrough dispatcher exposes rearrangement and polynomial rewriting through ordinary commands. These commands retain the existing family envelopes and do not add general rational normal forms or broader factorization methods.

## Units, vectors and precision

Read nps/include/nps/units/units.h and nps/src/units/units.cc.

Dimension stores length, mass and time exponents. Unit adds its spelling and exact scale to SI. Quantity combines a Rational value, a Unit and Precision. Vector carries components, rank, a named frame, a common unit and precision.

The unit table defines the accepted vocabulary. Compatible conversion is multiplicative. This is not an affine temperature-conversion system or a full uncertainty engine. Dimension operations check exponent overflow.

A decimal quantity is measured. Its significant digits and last significant decimal place survive exact SI conversion and intermediate arithmetic. Product and sum rules propagate precision differently. Rounding happens in the final report and is validated against the exact value.

This differs from algebraic numeric mode. steps/numeric_mode.cc reads supported decimal literals as exact fractions. Decimal reporting rewrites terminating fractions without rounding recurring fractions. For example, algebra can retain a third as a fraction even in decimal mode. Physics can report a measured result rounded to the precision its inputs permit.

Vector operations check frame, rank and dimensions before combining components. There is no implicit frame transformation. The exact magnitude primitive refuses a non-rational square root. A caller requesting approximation must make that choice explicit and preserve its classification.

## Giac boundary and runtime lifetime

| Source | Responsibility |
| --- | --- |
| nps/include/nps/cas/giac_adapter.h | Operation requests, result tags and backend interface |
| nps/src/cas/giac/giac_adapter.cc | Request validation, controlled command construction, response interpretation and accounting |
| nps/src/cas/giac/giac_typed.cc | Conversion between native AST nodes and Giac gen objects, direct operations and differential checks |
| vendor/khi-src/src/luabridge.cc | Bundled Giac entry used by the calculator shell and typed initialization |
| vendor/khi-src/src/Makefile.ki | Calculator feature definitions and Giac object selection |
| nps/tools/giac-objs.mk | Query and isolated build rules for those objects |

The adapter exposes a controlled set of operations. Validate requests before either string or typed dispatch. Exact, approximate, unevaluated, unsupported, malformed and resource results have different meanings. A usable backend expression is not automatically an independently verified derivation.

Giac translation refuses a whole expression when any descendant lacks faithful syntax. The adapter also rejects empty operand translations before dispatch. Returned scalar expressions, vector components and solution members cannot contain List nodes.

The unified build links Giac into nps_nspire and uses typed requests. The split build reaches a separately loaded luagiac module through Lua. The raw caseval calculator interface is intentionally broader than the step solver's adapter.

TypedGiacBackend initializes and caches Giac's resident context through caseval. That context outlives a native solve and is shared with the shell. Do not assume all process state is recreated with each Arena.

The direct-operation guard follows the calculator bridge's interrupt and graphics-context handling. reset_gc there refers to the OS drawing context. It is not Lua garbage collection.

Response distinguishes scalar expressions, ordered vectors, finite solution sets and matrices. Solve stores every root in values, including an exact empty set. single_value accepts a scalar or a usable one-root solution set. Scalar consumers reject zero or multiple roots instead of selecting one. Both typed and string producers preserve this shape and refuse conditional or malformed solution members.

Ref and Rref requests use a rectangular row-list AST in target and return that shape in value. Admission is shared by the adapter and direct typed backend. It requires exact rational cells in a matrix of at most four rows and six columns with no extra arguments or options. Root and row provenance are checked alongside cell arithmetic. Every intermediate matrix cell and event coefficient must fit the checked rational representation. Exceeding that arithmetic envelope is UnsupportedForm, not failed mathematical verification. Response validation preserves the requested dimensions. A Matrix response never yields a scalar, including a single-cell matrix. Approximate output remains tagged Approximate.

MatrixRowSink in nps/include/nps/cas/matrix_events.h receives typed operations from Giac capture. The capture owns copied matrix states, fixes the reduction step-info mode and restores the previous callback and context settings on exit. Nested capture is refused. Event 23 is admitted as an elementary row addition only under the qualified Gauss-Jordan contract. Final normalization emits event 35 without renumbering earlier events. Pivot and completion observations do not count as transformations.

nps/src/steps/matrix_row.cc checks each supplied swap, nonzero scale or add-multiple event against every before and after cell using exact GMP arithmetic. record_matrix_row publishes only verified changing events within the cancellation and step budget. Its RowEquivalent claim has a distinct proof obligation. Do names the rows and coefficient, Write contains the resulting matrix and Why explains reversibility. nps/src/steps/matrix_form.cc separately verifies the requested REF or RREF conditions. nps/src/steps/matrix.cc records the plan, checks trace continuity and publishes a final answer only after both the complete trace and final form pass. Cancellation and Meter limits retain a verified prefix. Arena failure discards records with unusable node references. This synchronous callback integration does not implement resumable Giac reduction.

matrix_determinant reuses the same checked row stream for square matrices. It records determinant effects with an exact GMP factor, verifies the triangular diagonal product and recovers the original scalar by dividing by that factor. Its transformations relate determinant expressions, not supposedly equal row matrices. Public certificates in nps/include/nps/steps/matrix_determinant.h check the factor, diagonal product and correction. The determinant bit budget is separate from the unchanged rational cell envelope. nps/tests/giac/determinant_capture.cc uses an independent bounded permutation oracle and corrupted certificates. The actual Lua bridge supplies determinant records to V4 tests through a CTest fixture.

The bridge compares complete exact rational solution sets with the native result, ignoring root order and duplicate members during comparison. Missing, extra, approximate or unequal roots cannot verify the native solution. The differential probe checks zero, one and two roots against independently specified expected sets as well as comparing typed and string interfaces. Both interfaces still use Giac, so agreement between them does not establish independence between mathematical engines.

## Native Lua bridge

nps/src/platform/nspire/lua_module.cc is the application boundary. Important entry points include main, l_walkthrough, solve_into, rewrite_into, the differentiation and integration adapters, structured physics adapters, ask_giac, push_step, push_steps and the lib registration table.

nps/src/steps/command.cc recognizes and validates native commands without evaluating their arguments. An unhandled command returns nil from the bridge. Recognized invalid or unsupported signatures return a refusal record. request_expression preserves the full command while original_expression and normalized_expression describe the operand when a solver has established that context. Preflight refusals carry the command without publishing an incomplete operand context.

Argument checks run before GcPause. Lua raises can use longjmp and skip C++ destructors. GcPause stops Lua collection around native work and result construction. Backend work runs before constructing the output table to avoid holding a partially built result across a reentrant Lua call.

Structured physics inputs permit inherited fields. field_value contains each lookup in a protected Lua call and turns lookup errors into typed input refusals. This keeps already copied native strings alive until normal cleanup, including builds without C++ exceptions. Merely moving GcPause after admission does not protect those strings. Allocation failure outside these protected lookups remains a separate lifetime concern.

scalar_string_argument rejects embedded NUL bytes before passing calculation arguments to C-string interfaces. variable_argument uses the parser's is_identifier predicate. A variable must occupy the whole input without whitespace. Keep lexical rules in the parser. Temporary C++ objects used during validation must be destroyed before raising a Lua argument error. normalize_identifier shares identifier spelling between parsing and variable binding.

Native AST and Derivation objects are request-owned. set_field copies strings into Lua. push_steps traverses the record tree into a flat array carrying depth and presentation fields. Stack exhaustion can produce a truncated array with an explicit marker. UI code must retain that distinction.

The bridge keeps local solver outcomes separate from backend comparisons. cross_checked_status can downgrade a locally solved result to unchecked, dependency unavailable, resource limited or verification failed. Unsupported local work can receive a separately labeled backend answer. It must not be presented as a native walkthrough of that answer.

ask_giac requires the derivation status and declines backend work after cancellation or resource exhaustion. Check both the reported backend count and observed calls when changing this boundary.

main verifies the ndl Lua number ABI and the loaded package's sidecar. An integrity failure registers a limited diagnostic surface. The complete solver interface is registered only after validation succeeds.

The module remains resident after main. It terminates initialization through _exit. Returning through normal C runtime teardown would destroy static state while Lua still holds registered functions. Read the resident-exit lint before changing these entry points.

## Ki documents and UI ownership

nps/lua/nps_v4.lua is the unified product UI. Older nps_v2.lua and nps_v3.lua remain separate documents with different loading and deployment assumptions. Preserve the Ki V1-derived shell behavior when working on V4. KhiCAS is a dependency and reference, not permission to replace the native-Nspire UI conventions.

The V4 file contains the following connected areas:

- Module loading, integrity checks, manifest compatibility and backend version checks.
- View, Widget, editor and history behavior inherited from the calculator shell.
- Native dialogs and menus exposed by the module.
- Step mode selection and structured physics examples.
- Result classification, hint exposure, branch folding and detail projection.
- Native math-box sizing and canvas rendering.
- Event routing and document save/restore.

requiredSolvers checks export names against capability IDs. STEP_MODES controls free-input modes. PHYSICS_FIXTURES defines structured examples. These are maintained separately from the native registration table and coverage catalog.

The final event handlers wrap earlier shell handlers held in baseOn. Events route to the full-text reader, Physics browser, step viewer or shell according to active state. Read the later wrapper before changing an earlier on handler.

Steps and Physics are exclusive primary views. Full Text temporarily covers its originating view and returns to it when closed. Switching primary views closes the reader and releases hidden editor focus. Menu insertion requires the shell entry to be visible. Clear History removes history widgets while preserving pending input and restores entry focus if the selected history was deleted.

buildRows derives visible rows from the retained result. Focus identifies a canonical step while scrolling uses rendered rows. Hints reveal more of the existing result. Standard and beginner views change explanation detail.

D2Editor objects are native widgets above the canvas. Repainting the canvas does not remove them. The UI parks inactive editors off screen and manages visibility explicitly. Math boxes await size callbacks, try supported font sizes and fall back to wrapped text when content cannot fit. A host stub cannot prove the real OS honors these layout operations.

on.save stores history text, the unentered expression and UI preferences. on.restore queues editor reconstruction until the first paint creates the view. It does not persist a complete native Derivation or expose serialize_context through Lua. Numeric mode in the native API is also separate from the saved UI fields.

### Result and walkthrough presentation

answer_only identifies the source of the displayed answer. It does not determine whether native steps exist. answerWithoutSteps combines that flag with canonicalStepCount. Retained checked steps remain available through hints, folding, keyboard navigation and detail views beside the labeled CAS answer.

paintStepsHeader bounds the summary so the walkthrough and footer retain space. A TAB result notice opens a separate scrollable result view with the complete answer, trust text, assumptions and interpretation. Hint mode exposes that view only after the final hint. Navigation reads the existing result table without another solver call.

Actions, Read Full Text opens a wrapped reader for the current shell result, walkthrough or guided physics content. T opens it from the walkthrough and physics browser. Help opens it from the shell, including while a native math editor has focus. Arrows scroll and Escape returns to the previous view. It reads retained text and preserves hint withholding.

Parked MathEditor widgets can still receive native events. Every registered editor filter first checks whether the walkthrough, physics browser or full-text reader owns the screen. It forwards that surface's events to the guarded handlers and consumes editing keys without changing hidden input or history. Ordinary editor filters retain their behavior when the shell owns the screen.

MathEditor measures history at its allocated width. Oversized mathematics uses the native editor's wrapped text mode while historyExpression retains the original for Enter, saving and the full reader. fitInput bounds the editable native math frame to half the viewport. A shared HELP cue identifies incomplete previews. Font and viewport changes remeasure history before showing it.

projectedStep applies hint disclosure to the list, detail view and full reader. Completed parent fields stay hidden while descendants remain unrevealed. stepInfo supplies both readers with recorded action, after-expression, reason and conditions. Verified transformations use Do and Write labels. Plans, checks, cases and unverified transformations keep distinct labels. Presentation does not run another solve or change the retained records.

fitMath measures a native expression box before showMath places it. Expression, width and font changes clear its cached dimensions. A fresh synchronous measurement can render the equation during that same paint. Wrapped text remains visible while a replacement measurement is pending. Captured callbacks from an earlier measurement cannot update the current box. The font ladder uses supported handheld editor sizes. paintDetail expands expressions that cannot fit into wrapped text items before pagination. Both step and result details use that renderer. Keep full text reachable through scrolling and park unused native widgets when the view changes.

paintOverlay protects painting for the walkthrough, physics browser and Full Text reader. An overlay paint failure displays a diagnostic and leaves the selected content available for a later repaint. It does not alter shell input or history or run another solve.

The d2_callbacks_document target packages nps/benchmarks/d2_callbacks.lua for native callback qualification. It tests identical-expression font and width changes and successive requests in one timer event while the editor is parked. Its measurements complement host callback models. Run it through the OS document UI and retain the tested package and live OS identity.

## Build and package structure

nps/CMakeLists.txt owns build logic. Use CMake directly. Native host, sanitizer and ARM configurations use separate build directories and compilers. The command reference is [nps/README.md](../nps/README.md#build-and-test).

The cross toolchain is nps/cmake/toolchains/ndl-arm926ej-s.cmake. It configures the ndl compiler wrappers, compiler-launcher environment and ARM archive tools. Device C++ is built without exceptions and RTTI. Use the actual wrapper configuration rather than substituting an unrelated ARM compiler.

The unified package links the native core, capability manifest, Lua bridge, integrity support, typed adapter, native menu wrapper and selected Giac objects. Giac definitions must match between its objects and the typed adapter because gen layout crosses that boundary. The private nps/src/cas/giac/giac_layout.h checks value and alias layouts in the supported host and calculator configurations. The host alignment repair is retained in nps/patches/giac-value-alignment.patch. It requires rebuilding the complete host archive and its consumers together.

The default Giac root is vendor/khi-src. The build queries Makefile.ki rather than duplicating its object list. Numeric dependency scripts are vendor/deps/build-gmp.sh and vendor/deps/build-mpfr-mpfi.sh, and they cross-build GMP, MPFR and MPFI into the toolchain prefix rather than beside it, because that is where nps/CMakeLists.txt:249 and :336 read them from. Host and device dependency discovery differ. Read the configured roots before changing include or link settings.

Set GIAC_HOST_ROOT in a fresh host configuration to enable matrix_capture, determinant_capture, guard_cleanup, typed_differential, matrix_cancellation, lua_matrix and value_layout against a separately configured host Giac archive. The backend targets reuse nps_core. value_layout directly exercises Giac representation and arithmetic. GIAC_ROOT continues to identify the calculator source. The [README commands](../nps/README.md#build-and-test) include the host prerequisites and focused CTest invocation. Host OS shims limit this evidence to host behavior.

| Build target | Main purpose |
| --- | --- |
| check | Host test and evidence workflow |
| luaxhost | Actual native bridge exercised with host Lua and scripted Giac replies |
| probe | Target feasibility and benchmark packages |
| luax | Split native module and its document |
| unified | nps_nspire.luax.tns, its sidecar and the Ki V4 document |
| document | Lua documents packaged by Luna with sidecars |
| resident_exit_lint | Resident-entry teardown guard |
| regold | Rewrite expected golden fixtures. This changes source evidence |

Typical commands from the workspace root are:

~~~sh
cmake -S nps -B nps/build/host -G Ninja
cmake --build nps/build/host --target check
ctest --test-dir nps/build/host --output-on-failure -R ui_smoke
~~~

These commands are choices, not an instruction to run every target on every task. The build-producing commands write artifacts. regold changes expected answers and must not be used merely to make a failing comparison pass. Build-tree deletion remains subject to deletion review.

scripts/bootstrap-host.sh and scripts/bootstrap-ndl.sh under nps check readiness. They are not dependency installers. The ndl script checks a specific revision, compiler inputs, packaging tools and the syscall archive's Lua-number ABI. The bootstrap_scripts test also runs the SDK-owned PHP regression against temporary generated files. It checks signatures, syscall dispatch, agreement with checked-in outputs and repeat generation. These are host checks. Resident-runtime compatibility and physical-device behavior require separate evidence. Inspect a revision mismatch before changing the pin.

### Identity and integrity

Capability manifests record build-input identities and deployment information. CMake hashes selected source, SDK, compiler, Giac and configuration inputs. The linked package is also hashed independently. Source identity and package-byte identity answer different questions.

Packaging audits the ELF, invokes ndl packaging tools and writes package-bound records. Lua documents are packaged through Luna. The native module and UI document each have their own SHA-256 sidecar.

integrity.cc derives the sidecar next to the actual loaded package, validates its format and basename, hashes the package bytes and compares digests. A sidecar demonstrates byte consistency with that record. It is not a digital signature or proof of a trusted publisher.

Move a module and its required sidecar together. Also preserve the document sidecar used by the build and evidence workflow. Do not infer that the Lua document is checked by the same native startup call that checks the module.

### Platform diagnostics and measurement

| Location | Responsibility |
| --- | --- |
| nps/include/nps/platform/nspire/measurement.h | Allocation accounting, headroom probes and measurement result types |
| nps/src/platform/nspire/allocation_probe.cc | Wrapped native allocations, operation intervals and diagnostic reports |
| nps/src/platform/nspire/device_identity.cc | Hardware and OS identification plus consistency checks |
| nps/lua/nps_v4.lua | Starts an operation profile and collects viewer timing, Lua heap size and solver counters |

NPS_DIAG enables low-level diagnostic instrumentation. NPS_RESOURCE_PROFILE enables resource profiling and is restricted to device builds. Both default to zero. Device identity describes the observed platform. It does not establish compatibility or qualify a release by itself.

The V4 launch screen reads device identity once per document and displays the model, CAS variant, OS version and ndl revision. A separate warning reports a model and OS combination the native module does not recognize.

Native allocation reports count requested bytes observed through the module's wrapped allocation APIs. They exclude allocator metadata, fragmentation, raw syscall allocations, Lua allocations, stack memory and whole-process memory. The bounded tracker exposes validity and overflow flags. Check those before interpreting its counters.

Beginning an interval preserves tracked live pointers while resetting interval counters and establishing the baseline and peak. Resident allocations therefore remain represented across operations. The interval peak is an absolute tracked live-byte peak, not simply the additional memory requested by that operation.

Ki V4 starts profiling before computation and finishes when the result or error screen paints. Failed requests carry a failure status. Missing solver counters are reported as unavailable. Native typesetting callbacks can arrive later. The Lua heap measurement is a live snapshot at that boundary, not a high-water mark. Arena occupancy is also reported separately from process memory.

The contiguous-headroom probe performs trial allocations and frees them. The total-free probe temporarily holds multiple allocated blocks before releasing them. These are allocation experiments with lower bounds, resolution and ceiling or hold-cap limits. They are not operating-system process memory counters and can perturb the allocator being measured.

## Tests and acceptance evidence

| Location | What it establishes |
| --- | --- |
| nps/tests/unit | Native operation behavior, refusal cases, context framing, integrity parsing and related invariants |
| nps/tests/property | Generated expression and solver checks with reproducible seeds |
| nps/tests/golden | Expected derivation records and family evidence |
| nps/tests/step_invariants.h | Cross-family structure, rule schema, conditions and outcome consistency |
| nps/tests/target/luax_host.* | Actual C++ bridge conversion and status behavior with a scripted backend |
| nps/tests/target/ui_smoke*.lua | Lua interaction and rendering behavior against calculator stubs |
| nps/tests/giac | Actual host Giac matrix reduction, typed callbacks, guard cleanup, differential comparisons and Lua bridge with calculator OS shims |
| nps/tests/tools | Measurement, allocation, bootstrap and helper-specific checks |
| nps/benchmarks | Emulator and physical measurement programs plus retained observations |

The UI smoke harness substitutes nps_nspire. The bridge harness substitutes Giac behavior. Passing them independently does not prove every real bridge response is rendered correctly. When changing their shared contract, also feed a real bridge response into the UI harness.

UI tests can share the global on table across isolated document environments. Paint the environment being judged before loading another one. The harness also sits close to LuaJIT's top-level local-variable limit. Use small scopes for additional checks rather than adding unrelated top-level locals.

CMake orders evidence fixtures through unit execution, bridge checks, V4 smoke, acceptance corpus, coverage, available device audit records and traceability. Evidence appenders require prerequisite groups. Running a consumer against an unrelated old evidence file can produce a misleading result or a deliberate refusal.

Important reporting tools are:

- tools/acceptance_corpus.cc, which executes corpus cases and records invariants, distinctness and family populations.
- tools/corpus_audit.py, which inspects the declared corpus inputs.
- tools/coverage.cc and tools/catalog.h, which join family declarations to emitted rules, assumptions and fixture evidence.
- tools/traceability.cc, which joins requirements, evidence groups and catalog information.
- tools/device_evidence.cc, whose --sweep mode discovers audit and handheld-run records when the test executes, validates their package digests and appends accepted records in a stable order.
- tools/device_run.cc and tools/device_readings.h, which serialize observed handheld readings without deciding whether they pass.
- tools/evidence-mutation.sh and tools/corpus-mutation.sh, which challenge the validators with deliberately damaged evidence.
- tools/oracle.py, a separate SymPy-based comparison tool for golden and generated cases. Its report must be inspected rather than inferred from process success alone.

These tool names are relative to nps. Corpus population gates, semantic agreement, schema conformance and requirement coverage are separate checks. Do not replace one with a passing count from another.

device_evidence consumes package-bound offline audit records and handheld reading records. Schema 1 carries offline checks. Schemas 2 and 3 carry measurements and a manifest-header reference, with acceptance decisions made by the reader. Current writers emit schema 3 with ndl-revision. The reader still accepts the earlier revision field and rejects duplicate readings across its two spellings. CMake discovers .offline-audit.txt and .device-run.txt records separately. Offline ELF analysis is not physical execution evidence. Inspect the current CMake glob and accepted schema before claiming a target run was ingested.

traceability can report unmet MVP requirements even when its process exits successfully. Read the generated report. STATUS.md, scorecards and old build reports are useful leads but may lag current source or describe a different checkout. Do not copy their pass counts into a release claim.

headcheck.sh builds an explicit commit in a separate worktree while using configured external dependency roots. It exists because a dirty shared checkout can accidentally supply declarations absent from a commit. Inspect its selected worktree and ownership first. It force-updates that checker worktree and its selftest writes temporary Git objects, so neither is a read-only command.

## ndl platform

ndl spans both the SDK used to compile and the runtime already resident on the calculator. Replacing a host tool or rebuilding an application does not replace the resident runtime.

| Location under vendor/ndl-src | Responsibility |
| --- | --- |
| ndl-sdk/include | Public calculator and ndl interfaces |
| ndl-sdk/libsyscalls | Syscall wrappers and ABI-sensitive calls |
| ndl-sdk/libndls | ndl utility library including configuration and directory enumeration |
| ndl-sdk/tools | Packaging and Lua document tools |
| ndl-sdk/toolchain | Cross-compiler sources, build script and installed toolchain |
| ndl/src/resources | Runtime resources, hooks and native Lua module loading |
| ndl/src/persistent-6.4 | OS-specific persistence support |
| ndl/src/installer-6.2 | OS-specific installation support |

Lua number ABI wrappers and syscall table correctness are material dependencies of the bridge. A header signature matching a caller is not sufficient when a syscall routes to the wrong OS function. Keep SDK, runtime and OS identity aligned.

resources/luaext.c implements nrequire discovery and loading. The ordinary implementation scans the document namespace. The relocation patch changes lookup and runtime-root selection. Verify which runtime source and artifact are actually installed rather than assuming every ndl tree contains the patch.

## Physical-device tools

tools/nsptool/nsptool.c implements direct commands and a persistent framed session. tools/nsptool/build.py builds against the current in-tree libnspire. Verify the binary being invoked. A system-installed nsptool can lack the commands present in the local source. rm deletes a file and rmdir checks that a directory is empty before requesting deletion through libnspire without traversing its contents or retrying the operation.

A persistent session runs requests sequentially in one process. libnspire reuses an active CX II connection and reopens it before a new service when the last USB transfer was at least one second ago. This avoids the first-request timeout observed after idle on the handheld. Preparation happens before sending the application request. Refresh retains the original libusb device object and refuses a disconnected target, including a replacement on the same USB port. A failed refresh leaves the library handle disconnected until it is freed and reopened. Framing carries argument lengths and captured command output. Validation rejects malformed frames before dispatch. An ordinary command error can drop the connection so the next request reconnects. The session does not silently replay a possibly applied mutation.

NSPIRE_TRACE=1 enables metadata diagnostics in the current libnspire sources. The records identify file read, write and stat phases, confirmed transfer offsets, USB status, ACK state and service-close errors. Handshake records distinguish completion, an elapsed deadline, an I/O timeout and packet failure. Validated stream headers add NavNet addresses, services, sizes and sequence numbers without application bytes. They contain no file contents or paths. Tracing changes timing and does not repair a timeout or prove remote persistence. DEBUG is a separate legacy option that dumps packet contents.

The CX II handshake waits for the validated time exchange within one absolute deadline. Unrelated frames and retries do not extend it or impose a packet-count cutoff. Accepted stream retries are acknowledged without delivering the same frame twice. Packet assembly retains bytes returned with a USB timeout while that read operation still has time to finish. These transport rules do not replay application commands or establish that a key event changed the screen.

Run the transport checks from the workspace root with Python 3 and the libusb development package:

~~~sh
python3 vendor/libnspire-src/tests/run_diagnostics_tests.py --sanitize
python3 vendor/libnspire-src/tests/run_transport_tests.py --sanitize
python3 vendor/libnspire-src/tests/run_file_tests.py --sanitize
python3 vendor/libnspire-src/tests/run_service_tests.py --sanitize
python3 vendor/libnspire-src/tests/run_key_tests.py --sanitize
python3 vendor/libnspire-src/tests/run_screenshot_tests.py --sanitize
python3 tools/nsptool/tests/session_test.py
~~~

These dependency checks build temporary host executables. They are separate from the nps CMake suite and do not access the handheld.

The per-user device lock coordinates cooperating nsptool clients. It does not coordinate every application that can own the USB interface. Inspect transport ownership when TI desktop software and a CLI compete for the calculator.

vendor/libnspire-src/src/usb.c handles interface acquisition. init.c initializes connection state. cx2.cpp handles CX II packet framing, sequence acknowledgments, padding and checksums. raw.c carries raw service exchanges. An Invalid packet message can result from framing or checksum failure and does not by itself identify a key service installation problem.

On macOS, CX II connection setup reports USB configuration unavailable when libusb does not expose configuration 1. It closes the handle before claiming an interface or sending a handshake. The Darwin libusb configuration cache can differ from IORegistry, so a listed device alone does not establish a usable interface. Compare LIBUSB_DEBUG=4 connection output with the device's IORegistry properties before attributing this refusal to StepCAS or keysvc. This diagnostic leaves the existing refusal policy in place and does not reset or reconfigure the device.

tools/keysvc/protocol.h defines the wire format. keysvc.c owns resident held-key state and validates a batch on copied state before posting events. A reply is written before events are applied. Host waits divide requests into batches while modifiers can remain held on the calculator.

keysvc-status sends only a version query. A transport acknowledgment does not prove the service exists or its callback ran. A failed helper check before key or type dispatch reports that no keys from that request were dispatched. Failure after dispatch can leave key state unknown. When the query times out while info and screenshot still work, first open a fresh StepCAS document and verify native module identity. This separates runtime loading from startup-helper availability. Ordinary ndl startup scans /documents/ndl/startup and skips helpers while Escape is held. Preserve the helper log before any relaunch, which truncates it. If hidden runtime resources exist, the helper writes /appdata/ndl/keysvc.txt.tns instead of the visible /documents/ndl log. Longer host timeouts do not repair a missing helper.

The helper log records callback entry, read status and byte count, and version-query reply status. These checkpoints help distinguish a blocked read from a failed reply write. Missing records alone cannot establish a missing callback because log storage can also fail. These records contain no key text or packet contents.

vendor/libnspire-src/src/services/key.c implements TiLP's standard OS key protocol on service 0x4042. nspire_send_key sends initialization and one packed key in the same session, waits for transport acknowledgments and closes through the shared completion path. It does not wait for an application reply. This path does not require keysvc or establish whether ndl is loaded.

nsptool key-os sends supported taps and type-os sends supported ASCII text through that standard service. Both validate the complete request before opening USB. List available names with key-os --list. Help on CX II uses Ctrl+Trig. key-os help selects that event, while question-mark remains punctuation. The resident equivalent is key ctrl+trig. Held-key actions and restart remain separate keysvc operations. Neither backend retries a possibly applied action or automatically switches to the other backend after a failure. Inspect the screen before deciding what to do after an uncertain result.

type-os sends key events rather than pasting a complete mathematical expression. Fractions and powers enter native templates. Move the cursor out of a template before entering another argument or term and inspect the complete input before execution. Ctrl+Del is key-os ctrl+back-space. In StepCAS it clears a focused input but invokes entry deletion when history is focused. Escape closes Full Text without clearing input.

~~~sh
tools/nsptool/nsptool.new key-os --list
tools/nsptool/nsptool.new key-os esc
tools/nsptool/nsptool.new type-os 'x+1'
tools/nsptool/nsptool.new key-os enter
~~~

Host interruption cleanup releases keys tracked by that request when possible. A failed acknowledgment can leave the resident state uncertain. A fresh host process does not inherently release those keys.

tools/keysvc/tests/device_navigation.py requires a running keysvc and My Documents selected in the handheld document browser. It checks taps and explicit press/release against 320 by 240 screenshots, then leaves the next row selected. Run it with ordinary Python assertions enabled. Separate service acceptance, key effects and reboot completion when validating the device.

tools/nsptool/usbprobe.c is a standalone connection diagnostic. It enumerates TI devices, then opens the first CX II matching USB ID 0451:e022. Running it claims interface 0, attempts driver detachment, sets configuration 1 and resets the USB device. It restores a driver it detached before continuing. Initialization or driver restoration failure stops the run with a nonzero exit status. It has no help mode and runs independently of the nsptool client lock. Inspect its printed operation results. Ordinary probe failures do not make its process exit nonzero.

Build the diagnostic from the workspace root with the installed libusb development package:

~~~sh
clang -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$(pkg-config --variable=includedir libusb-1.0)" \
  tools/nsptool/usbprobe.c $(pkg-config --libs libusb-1.0) \
  -o /private/tmp/cx2-usbprobe
~~~

The regression executable supplies libusb stubs and checks error handling on the host:

~~~sh
clang -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$(pkg-config --variable=includedir libusb-1.0)" \
  tools/nsptool/tests/usbprobe_test.c -o /private/tmp/cx2-usbprobe-test
/private/tmp/cx2-usbprobe-test
~~~

## Firebird automation and saved state

| Source under vendor/firebird-src | Responsibility |
| --- | --- |
| headless/main.cpp | Command input, command markers, startup arguments and headless event handling |
| core/debug.cpp | Debugger commands and pause/resume decisions |
| core/emu.cpp | Guest loop, scheduling, deferred execution and snapshot orchestration |
| core/usblink_queue.cpp | Serialized emulated transfer work and stalled-action handling |
| core/armsnippets_loader.c | Deferred native execution under a suitable guest task context |
| core/flash.cpp | Backing NAND image, modified blocks and flash persistence |

The input thread requests a break. Guest command execution happens on the emulator thread in the debugger. Commands that stay in the debugger leave the guest paused. Transfer work advances with guest scheduling.

The ln s upload command queues work but does not resume the guest. A following c is needed for progress. Other transfer commands can resume automatically. Check command return behavior before diagnosing an idle queue as a transport failure.

Deferred exec waits for suitable ARM, interrupt and task-stack state. It invokes the native loader for PRG, bFLT and Zehn programs, so it cannot open a Luna Lua document. A queued request is not immediate execution and is not proof that nested UI activity is safe. Open Lua documents through the calculator UI.

Debugger key commands modify the emulated keypad. Giac's own menus poll that hardware. OS screens and Lua documents use the resident key service in this harness. The [key-service guide](../nps/benchmarks/keycodes.md) explains the separate input mechanisms. Verify the helper and visible effects before navigating. A successful upload or service receipt does not prove that a document opened.

Query the live OS device-info service before qualifying an instance. A retained seed filename and the older boot banner do not establish its current OS identity. Record the loaded StepCAS module separately from the OS response.

flashsave writes modified flash blocks into the backing image. savestate records execution state and flash deltas with backing filenames. Restoring a snapshot reuses its embedded Boot1 and flash names and overlays saved modified blocks onto that backing image. A snapshot is not a standalone flash copy.

Headless stop or stdin EOF does not automatically save flash. Preserve both the snapshot and its backing image when moving an experiment. Do not reuse another task's emulator image or send commands into an unidentified session.

tools/emu-drive.sh is an older FIFO-based driver. Read its assumptions before combining it with the newer framed headless command workflow.

## Document hiding and runtime relocation

Read [research/folder-hiding/AGENTS.md](../research/folder-hiding/AGENTS.md) and its README before working there. Its operational requirements remain relevant, but its dated completion prose must be checked against later evidence.

The visible test_app.tns is a small ordinary calculator using TI math.evalStr. It does not depend on StepCAS for arithmetic. The exact expression 1+9+9+8, allowing spaces, lazily loads calc_helpers and requests restoration. Successful restoration displays 27. Preserve that interface and keep diagnostic restore wording out of the visible application.

| File under research/folder-hiding | Responsibility |
| --- | --- |
| test_app.lua | Visible calculator and concealed restore trigger |
| calc_helpers.c | Resident Lua helper calling the shared storage API |
| storage.h and storage.c | Inventory planning, manifests, hiding, collisions and restoration |
| storage_tool.c | Inventory, HideAll and RestoreAll native frontends |
| factory-6.4 | Factory-file extraction, provenance and per-file recognition data |
| relocate.c | Runtime move, original preservation and rollback |
| arm_boot.c | Current-document boot-file preparation, replacement and rollback |
| runtime_probe.c | Bridge replacement and runtime-layout switching |
| ndl-relocation.patch | Runtime-root selection, startup, configuration, persistence and module lookup changes |
| build-relocated-ndl.sh | Isolated patched runtime build |
| evidence and relocated-runtime | Retained observations and artifact identity records |

Native calculator storage is /appdata/test_app/store. Relocated ndl is /appdata/ndl. Both lie outside the normal document browser root. Factory recognition checks individual filenames and contents. A factory folder does not make every added file inside it a factory file.

storage_hide builds a plan, preflights moves and commits a closed manifest before renaming documents into indexed batch slots. It refuses another hide while an unfinished batch exists.

storage_restore reads and preflights all unfinished batches before moving anything. Different-file collisions and directory collisions refuse without overwriting. Identical regular files can remain at both locations and the stored copy is retained. Original names, manifests and completed-batch metadata remain available for recovery. This is recoverable move logic, not a guarantee against arbitrary concurrent edits or every power-loss scenario.

Relocation and boot arming are separate transactions with original files and reverse rollback. Inspect their logs before retrying. Do not run another installer over a still-resident runtime.

The relocation patch selects the external runtime when its resources file passes stat. Existence does not verify contents and does not guarantee a fallback after a corrupt external runtime fails to load. Keep configuration, startup, persistence and module lookup consistent with that selection.

Device transfer names and native filesystem names are different namespaces. Establish the accepted transfer destination from current observation. Do not blindly prepend /documents to a name understood by nsptool or an emulated link.

### Runtime provenance

The research Makefile defaults SDK to the workspace-root ndl-src/ndl-sdk and supports an override. build-relocated-ndl.sh locates the root ndl-src relative to its own directory. Source-root resolution is separate from a successful runtime build, deployment or restoration.

Retained logs include successful bridge replacement, runtime layout moves in both directions and physical boot arming. Those observations exceed parts of the README's completion table. They do not establish current boot persistence, complete physical restoration or cleanup. Fresh inventory, screen observations and byte comparisons are required for those claims.

## Agent environment

AGENTS.md holds shared workspace instructions. CLAUDE.md imports it and adds Claude-specific handoff guidance. GEMINI.md imports it for Gemini CLI and links it explicitly for clients without import expansion. Antigravity CLI discovers both AGENTS.md and GEMINI.md directly.

| Client | Configuration and startup |
| --- | --- |
| Claude Code | The local user settings already select permissions.defaultMode as auto. Launch explicitly with claude --permission-mode auto when needed. Keep this default in user settings, because project settings do not accept auto as a default mode |
| Codex | .codex/config.toml selects on-request approval, auto_review as the reviewer and workspace-write sandboxing. Start from this trusted repository. The explicit launch equivalent is codex --approve-for-me |
| Installed gemini command | This machine's executable is Antigravity CLI. Its user settings at ~/.gemini/antigravity-cli/settings.json select agentMode as accept-edits. The explicit launch equivalent is gemini --mode=accept-edits |

These modes have different boundaries. Claude and Codex evaluate approval requests automatically. Antigravity accept-edits approves file edits and creation while shell commands still follow its permission rules. User-level defaults apply to other repositories too. Restart a session to load changed startup settings and inspect its effective mode. Configuration parsing alone does not establish a successful remote session.

Use the current vendor references when changing these settings: [Claude permission modes](https://code.claude.com/docs/en/permission-modes#which-mode-a-session-starts-in), [Claude classifier configuration](https://code.claude.com/docs/en/auto-mode-config#where-the-classifier-reads-configuration), [Codex automatic review](https://learn.chatgpt.com/docs/sandboxing/auto-review) and [Antigravity modes](https://www.antigravity.google/docs/cli/modes/). Antigravity's documented persistent filename is user-level. Do not assume that the separate Gemini CLI's settings schema applies to it.

The documentation layout follows a practical reading strategy rather than a proven universal optimum. Repository-context research has mixed results: [Evaluating AGENTS.md](https://arxiv.org/abs/2602.11988v2) reports added cost without consistent success gains, while [On the Impact of AGENTS.md Files](https://arxiv.org/abs/2601.20404v2) reports efficiency gains without comprehensive correctness evaluation. [A Two-Agent Ablation Study](https://arxiv.org/abs/2607.27250v1) finds no measurable correctness improvement in its setting. Keep exact commands and unusual contracts in startup instructions, detailed architecture here and normative requirements in the PRD.

## Where to read next

| Question | Starting material |
| --- | --- |
| What should the product do? | docs/StepCAS_Product_Requirements_Document.md and nps/catalog/families.md |
| Why was the native engine structured this way? | .Internal/agent-pack/docs/ARCHITECTURE_SPEC.md and .Internal/spec-reconciliation.md |
| How did Ki evolve? | .Internal/ki-port.md, .Internal/ki-v2.md, .Internal/ki-v3.md and .Internal/ki-v4.md |
| Which platform assumptions matter? | .Internal/ndl-platform.md, .Internal/ndl-api-reference.md and .Internal/target-hardware.md |
| How is recovery organized? | .Internal/backup-and-recovery.md and the specific experiment's evidence |
| Why might a debugger or USB command stall? | docs/hardware-debugger-findings.md, .Internal/tooling.md and the transport source |
| What remains for acceptance? | Current generated traceability and coverage reports, checked against their build identity |

The map identifies integration points across the whole workspace. It does not enumerate every upstream Giac algorithm, compiler source or emulator peripheral implementation. When a task reaches those internals, trace the selected object or device subsystem in its own checkout and inspect its Git state before changing it.
