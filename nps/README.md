# nps, the native core and Ki documents

StepCAS records mathematical transformations in a native derivation engine and presents them through the Ki V4 Lua document on TI-Nspire CX II. The unified module also links Giac from khi-src for ordinary calculator expressions and controlled backend operations.

Read [the project instructions](../AGENTS.md) before changing the workspace. The [family catalog](catalog/families.md) defines native solver coverage. The [product requirements](../docs/StepCAS_Product_Requirements_Document.md) define the intended product, including requirements beyond the current implementation.

## Source layout

| Location | Responsibility |
| --- | --- |
| include/nps/core and src/core | Arena-owned AST, lexer, parser, canonicalization, printing, evaluation and context serialization |
| include/nps/steps and src/steps | Derivation records, rule schemas, equation solving, rewriting, integer methods, differentiation and integration |
| include/nps/physics and src/physics | Kinematics, catch-up, relative motion, density, unit conversion, vectors and work |
| include/nps/units and src/units | Dimensions, unit parsing, exact scale conversion and measured precision |
| include/nps/cas and src/cas/giac | Controlled backend requests, result classification and typed Giac integration |
| src/platform/nspire | Lua bridge, native menus, package integrity and target instrumentation |
| lua | Ki documents, with nps_v4.lua as the unified UI |
| tests | Native unit, property, golden, bridge, UI and tooling checks |
| catalog | Supported family declarations and proof obligations |
| tools | Coverage, corpus, traceability, package auditing and build helpers |
| benchmarks | Target probes and retained measurements |

## Build and test

The runtime is named ndl. The SDK lives at vendor/ndl-src/ndl-sdk, the CMake override is NDL_SDK and the readiness command is nps/scripts/bootstrap-ndl.sh. Configure a fresh build directory after migrating names. Retained build caches, recovery artifacts and dated reports keep their original paths.

The handheld runtime uses /ndl with ndl_resources.tns, ndl_installer_4.5.5-6.2.0-6.4.0.tns, persistent.tns and startup/keysvc.tns. Keep the StepCAS document and sidecar under /nps, and the native module and its sidecar under /nps/runtime. Runtime paths, installers and startup files must be updated together.

Capability schema 2 uses ndl_version in supported targets and ndl.build-inputs for the SDK component. Device identity reports ndl_revision. The SDK preserves existing binary revision entry points, upstream URLs and license attribution.

Use CMake with Ninja. Run these commands from the cx2 workspace root. Keep host, sanitizer and ARM configurations in separate build directories. ndl and Giac retain their own dependency build systems.

The host build needs a C++20 compiler, CMake, Ninja, Python 3 and GMP. LuaJIT enables the native bridge checks. nps/scripts/bootstrap-host.sh inspects host prerequisites. The full check target also requires the pinned ndl SDK and PHP for its syscall generator regression.

~~~sh
cmake -S nps -B nps/build/host -G Ninja
cmake --build nps/build/host --target check
~~~

check builds its test dependencies and runs CTest. After building those dependencies, select a focused check through CTest:

~~~sh
ctest --test-dir nps/build/host --output-on-failure -R ui_smoke
~~~

CTest supplies the source working directory and evidence fixture ordering. Native unit and bridge checks execute real C++ code. UI smoke uses calculator stubs. None establishes physical-device behavior.

The luax_profile test compiles the diagnostic Lua boundary with allocator fixtures. The nspire_allocation_probe test exercises the native report writer separately. Both run through check. Diagnostic reports distinguish failed requests and unavailable measurements.

The bootstrap_scripts check also runs the SDK persistence fault regression. It covers boot-file staging, publication, rollback, cleanup and uninstall failures. These host checks do not establish persistent installation on a handheld.

Matrix walkthroughs require the Giac event changes retained in patches/giac-matrix-events.patch. The current khi-src and retained host Giac tree already contain them. For an unpatched KhiCAS tree, inspect the dry run before applying the patch:

~~~sh
giac_matrix_patch="$PWD/nps/patches/giac-matrix-events.patch"
patch --dry-run -p1 -d vendor/khi-src -i "$giac_matrix_patch"
patch -p1 -d vendor/khi-src -i "$giac_matrix_patch"
~~~

Rebuild Giac through its dependency Make rules before packaging. The shared dispatch header also requires rebuilding its dependent objects. The unified CMake target invokes Makefile.ki with tools/giac-objs.mk for the selected object directory. NPS_GIAC_OBJECT_DIR can select a fresh cache while preserving retained objects. Rebuild a host Giac archive with its own configured Make build. The matrix_capture test detects missing final-normalization events.

The 64-bit host representation also requires patches/giac-value-alignment.patch. Apply it before building a fresh host Giac archive. It aligns compact values to their existing word accesses and alias structures. The calculator declaration is unchanged. Rebuild every host library object and C++ consumer together because enclosing member offsets change. Retained archives built with the old header are incompatible.

The optional Giac host tests link a configured host Giac archive. Set GIAC_HOST_ROOT to its matching source tree. GIAC_HOST_ARCHIVE can override its src/.libs/libgiac.a archive. GIAC_ROOT still selects the original calculator Giac source. The tests need GMP, GMP C++ bindings, MPFR, MPFI and LuaJIT. On macOS they also link Accelerate. For an archive built with ASan and UBSan, add -DNPS_SANITIZE=ON to the CMake command below. NPS_SANITIZE does not instrument a prebuilt dependency.

~~~sh
giac_tests=$(mktemp -d /private/tmp/cx2-giac-tests-XXXXXX)
: "${GIAC_HOST_ROOT:?Set GIAC_HOST_ROOT to the rebuilt host Giac tree}"
cmake -S nps -B "$giac_tests" -G Ninja \
  -DGIAC_HOST_ROOT="$GIAC_HOST_ROOT" \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build "$giac_tests" --target matrix_capture determinant_capture guard_cleanup typed_differential matrix_cancellation lua_matrix value_layout
ctest --test-dir "$giac_tests" --output-on-failure -R '^(matrix_capture|determinant_capture|guard_cleanup|typed_differential|matrix_cancellation|lua_matrix|value_layout)$'
~~~

These checks execute Giac reduction, typed row callbacks, guard cleanup, typed and string comparisons and the Lua matrix bridge. value_layout checks compact-value placement, aliases, ownership and arithmetic through the real library. The typed adapter also checks the supported host and calculator layouts at compile time. typed_differential covers shell mode on and off at step-info levels 0, 1 and 2 with independent finite-root expectations and context restoration. matrix_cancellation checks pending interruption, evaluator interruption and recovery on the next solve. Host shims replace calculator OS calls. Handheld execution and timing need separate qualification. Giac retains its dependency build system.

determinant_capture compares actual Giac row traces and native scalar results against independent GMP permutation certificates. It also corrupts determinant factors and checks cancellation, resource limits and recovery. lua_matrix writes verified determinant records for the V4 UI test. CTest orders that fixture before testing recorded Do, Write and Why, full text and hint disclosure.

## Sanitizers and generated cases

~~~sh
cmake -S nps -B nps/build/san -G Ninja -DNPS_SANITIZE=ON
cmake --build nps/build/san --target nps_host nps_fuzz
ctest --test-dir nps/build/san --output-on-failure -R '^unit$'
cmake --build nps/build/san --target luaxhost
~~~

On macOS, luaxhost injects the sanitizer runtime required by the instrumented module. Use that target for the sanitized bridge check. Direct nps_host execution requires the nps source directory as its working directory because fixtures use source-relative names.

On POSIX hosts, the canonicalization regression runs 2048-level expression shapes and ordering checks in a worker with a 128 KiB native stack. This prevents CTest's enlarged process stack from hiding recursive normalization failures. CMake links the host thread library for that guard. Other hosts execute the same shape checks without the stack-size qualification.

Set a standalone fuzz run's size and seed through CMake cache options:

~~~sh
cmake -S nps -B nps/build/host -G Ninja -DFUZZ_CASES=10000 -DFUZZ_SEED=1
cmake --build nps/build/host --target fuzz_run
~~~

Use bounded generated cases that exercise the changed behavior. Repeat or broaden checks when failures or unresolved concerns justify it.

## Calculator packages

The ndl toolchain file drives nspire-g++, nspire-ld and genzehn. nps/scripts/bootstrap-ndl.sh checks the SDK revision, compiler inputs and compiled Lua-number ABI. The bootstrap_scripts test runs the SDK-owned generator, file-opening, naming and compiler-wrapper regressions. These check syscall signatures, dispatch, repeat generation, creation flags, file preservation, directory contracts, binary compatibility and compiler queries that do not create user directories in temporary fixtures. The giac_runtime_names test compiles Giac's actual installer-name predicate and copy logic. The file-opening regression uses the host C++ compiler with address and undefined-behavior sanitizers. Inspect the configured source and dependency roots before building.

~~~sh
cmake -S nps -B nps/build/device -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake
cmake --build nps/build/device --target unified
~~~

unified packages the native module and Ki V4 document with integrity sidecars. Build output does not establish deployment or device behavior.

| Target | Purpose |
| --- | --- |
| device | Compile the configured calculator objects |
| unified | Package the native module with bundled Giac and Ki V4 |
| luax | Package the split native module |
| document | Package Lua documents |
| d2_callbacks_document | Package the native editor measurement and callback timing probe |
| probe | Package target measurement programs |
| resident_exit_lint | Check resident initialization teardown conventions |
| tidy | Run the configured clang-tidy checks |
| regold | Rewrite golden fixtures for explicit review |

Set NPS_DIAG during configuration to enable diagnostic instrumentation. regold changes source fixtures and requires reviewing their diff before a normal test run. Historical notes may show retired nps Makefile commands. Use the CMake commands here for the current workflow.

## Native contracts

SolveTask provides persistent native ownership for linear solving and rearrangement. It owns request provenance, the arena, records, meter and coroutine frames. Construction does no parsing or solving. advance performs cooperative checkpoints and advance(0) does no work. Borrowed records must not span advance, cancel or close. Cancellation preserves published moves and their conditions without an answer. close releases owned state. Caller Budget callbacks are refused because their borrowed context could expire. Resource reporting measures coroutine frames exactly, with AST and work counters separately. It does not measure all retained allocations or guarantee elapsed slice times. The synchronous wrappers drain the same engines. Lua still uses synchronous calls, so this API does not establish production UI incrementality. See [budgets and interruption](../docs/codebase-map.md#budgets-and-interruption) for the full boundary.

Ordinary top-level solve, diff, int, simplify, factor, expand and rearrange commands request walkthroughs through the native command dispatcher. Integer walkthroughs cover iquo, irem, gcd, factorial, perm, comb, is_prime, nextprime, ifactor and powmod within the literal bounds in the family catalog. GMP supplies exact division, products and Bezout certificates. Their accepted mathematics is limited to those declared families. Extra derivative orders, integral limits and unsupported options produce explicit refusals. Unhandled commands retain ordinary Giac evaluation.

The native parser preserves bracketed lists and rectangular matrix shape. Scalar walkthroughs refuse list operands before algebra or backend work. Top-level ref and rref commands request recorded row-reduction walkthroughs in Exact mode. The det command requests a determinant walkthrough for an exact square matrix.

~~~text
ref([[2,4,6],[0,3,6]])
rref([[2,4,6],[0,3,6]])
det([[0,2],[3,4]])
~~~

The controlled backend has Ref and Rref requests for exact rational matrices with at most four rows and six columns. Every intermediate cell and row-operation coefficient must also fit the checked rational bounds. A reduction exceeding those bounds is unsupported even when its input cells fit. It preserves the requested row-list shape, including a single-cell matrix. Shared admission checks approximate root and row identities as well as every cell. A Matrix response cannot be extracted as a scalar. Unsupported options, extra arguments, empty or ragged rows, symbolic cells and decimal input are refused.

The typed backend captures actual Giac swaps, nonzero scales and row additions, including final pivot normalization. Each transition must pass exact verification against every before and after cell before the recorder publishes a RowEquivalent transformation. Pivot and completion observations and verified identities do not count as transformations. The final-form checker separately verifies REF or RREF conditions. A successful walkthrough requires an unbroken verified trace from input to final matrix. Cancellation or a Meter limit preserves the verified prefix without a final answer. Arena failure discards records whose node references are no longer usable. Giac callback capture is synchronous and does not make reduction resumable.

Determinants accept square matrices within that row-reduction envelope. The walkthrough tracks how each changing row operation affects the determinant, multiplies the final triangular diagonal and corrects for accumulated factors. Write retains the original determinant value as the current matrix determinant divided by its factor. GMP keeps the factor and scalar answer exact beyond machine integer range, subject to the separate determinant bit budget. This does not widen the input or intermediate matrix cell bounds. An unchanged row event does not silently alter the displayed factor. Unsupported input and interrupted work have no final determinant answer.

The existing !d, !i, !s and !k prefixes still select explicit step operations. A bare !g selects plain CAS and bypasses automatic walkthroughs. Choosing Full walkthrough or Hint walkthrough restores automatic command recognition. The selection is saved with the document.

Actions, Read Full Text opens a wrapped reader for the focused input or history entry and current request details. T opens it from a walkthrough or guided physics browser. HELP (Ctrl+Trig on CX II) opens it for shell status. Use the arrow keys to scroll and Escape to return. Hint mode keeps unrevealed work and answers hidden in the reader.

Escape from Full Text returns to the view it covered. In the shell it restores focus to the selected editor. Choosing a walkthrough or guided physics closes the other primary view. Templates and command menus leave pending input unchanged while a reader or walkthrough hides it. Clear History preserves the unentered expression and its cursor position. Ctrl+Del clears a focused input. In history it invokes entry deletion. Remote type-os input consists of keystrokes, so fractions and powers require cursor navigation out of native templates before further arguments or terms.

Walkthrough, physics browser and Full Text painting share an error boundary. A paint failure displays a diagnostic and retains the selected content for another repaint. Input, history and solver state remain unchanged.

History keeps two-dimensional mathematics when it fits. Oversized entries use the native editor's wrapped text display and show HELP: full text when the preview cannot show everything. Tall input stays within half the screen so history remains reachable. HELP reads the complete expression without changing the editable input or saved history.

Step Info shows Do, Write and Why for a verified transformation, followed by its starting expression and conditions. Beginner detail adds the fuller explanation and verification information. Plans show strategy, checks show expected and observed values and cases show their conditions. Unverified expressions are labeled instead of presented as instructions to copy. Hint mode withholds a parent step's completed work until its child steps are revealed. Read Full Text uses the same recorded guidance.

Changing a step, available width or equation font clears the previous native measurement. A fresh synchronous measurement is used in that same paint. Wrapped text remains available while the new dimensions are pending. Equation fitting uses [TI's supported handheld font sizes](https://education.ti.com/html/eguides/nspire/EG_Nspire/EN/content/eg_lua/m_libraries/2deditorlib/setfontsize.HTML). Full Text remains available when an expression cannot fit.

The d2_callbacks_document target packages benchmarks/d2_callbacks.lua and its sidecar in a calculator build. Open the document through the calculator UI. It reports native measurement callbacks after expression, font and width changes, including identical text and successive changes in one timer event. A synchronous callback is marked sync true. Record the package identity and screenshot with the actual OS version. This probe does not qualify StepCAS rendering or physical performance by itself.

Linear and pure-square quadratic instructions name the constant to add or subtract and the coefficient to divide by. Quadratic checks compare both sides of the original equation. A zero square has one root case, while a positive square has two signed cases within the supported exact envelope.

Quotient-rule instructions name the numerator and denominator, the derivatives to multiply and the subtraction order before dividing by the denominator squared. Child steps compute those derivatives. Write shows the completed expression after the children are revealed.

Giac solve responses preserve the complete finite solution set. Quadratic cross-checks compare every exact rational root, including the zero-root and empty-set cases. An approximate, missing or extra root cannot verify a native result. Ordinary Giac answers remain separate from native teaching steps.

- Parse with the existing lexer and recursive descent. Preserve source offsets on refusal.
- Enforce budgets during construction. Arena failure remains sticky.
- Keep NodeId values and references within their owning Arena's lifetime.
- Record the mathematical operation and its evidence as it executes.
- Keep backend answers, native derivations and verification strength distinct.
