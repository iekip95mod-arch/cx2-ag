# Menu walkthrough implementation

the maintainer requested walkthroughs for every mathematical menu option on 2026-09-07. This plan extends the MVP work. Existing performance, offline and handheld acceptance requirements remain open.

## Inspected inventory

The menu inventory inspected on 2026-09-09 has 183 selectable entries, excluding category headings and separators. Actions and Steps contain 14 application and progression controls, including Browse templates. Templates contains 20 input constructions. Physics contains 10 browser and example entries. The remaining 139 mathematical entries are listed below. Counts describe this inspected source snapshot.

| Category | Entries | Native walkthrough entries | Commands and constructions |
| --- | ---: | ---: | --- |
| Number | 25 | 6 | evalf, ifactor, iquo, irem, is_prime, nextprime, idivis, iegcd, iabcuv, euler, ichrem, powmod, abs, floor, ceil, sign, round, max, min, interval construction, conversion to interval, re, im, conj, arg |
| Algebra | 14 | 3 | solve, factor, normal, simplify, subst, convert, fsolve, rsolve, partfrac, tcollect, texpand, cfactor, cpartfrac, csolve |
| Polynomials | 22 | 2 | factor, cfactor, proot, degree, coeff, horner, canonical_form, pcoeff, lagrange, quorem, gcd, egcd, abcuv, symb2poly, poly2symb, resultant, gbasis, cyclotomic, hermite, tchebyshev1, tchebyshev2, randpoly |
| Calculus | 6 | 3 | diff, int, limit, sum, series, desolve |
| Probability | 18 | 3 | factorial, perm, comb, rand, binomial, binomial_cdf, binomial_icdf, normald, normald_cdf, normald_icdf, poisson, exponentiald, geometric, chisquared, uniformd, chisquaret, normalt, studentt |
| Statistics | 17 | 0 | seq, size, op, apply, append, concat, head, tail, sort, revlist, contains, suppress, remove, bar_plot, histogram, scatterplot, linear_regression_plot |
| Matrix and Vector | 28 | 3 | linsolve, det, inv, rref, ref, ker, image, bug, eigenvalues, eigenvects, jordan, matpow, dot, cross, identity, matrix, randmatrix, hilbert, vandermonde, l1norm, l2norm, linfnorm, cond, lu, qr, schur, svd, svl |
| Plots | 9 | 0 | plot, plotarea, plotparam, plotpolar, plotfield, plotode, plotdensity, plotcontour, plotimplicit |

Native walkthroughs cover supported cases for 20 mathematical entries representing 19 distinct commands because factor appears twice. The [family catalog](../nps/catalog/families.md) defines their bounds. In particular, gcd accepts signed integer literals rather than general polynomial arguments. The other 119 entries retain ordinary Giac evaluation without native walkthroughs. Expand and rearrange also have native command support but no dedicated menu entries. Plain CAS selection bypasses walkthrough dispatch.

Physics includes a guided browser, populated constant-acceleration commands and a blank editable command. Its guided examples cover unit conversion, density, kinematics, vector addition, work, components, catch-up and relative motion. One unit example deliberately refuses incompatible dimensions.

Templates insert fractions, powers, roots, derivatives, indefinite and definite integrals, finite and infinite limits, one-sided limits, equality and units. Qualification must complete and execute the inserted expression. Nonmathematical controls retain their application behavior.

The inherited bug(M) entry needs runtime investigation. No implementation was found in the bounded source inspection, but no runtime failure or intended replacement has been established.

## Implementation state and roadmap

1. Finish production incremental solving. The cumulative solver-budget repair is committed as a68225f. Native SolveTask now owns persistent linear and rearrangement requests with checkpoints in analysis, reduction and inverse operations. Published restrictions are settled and cancellation preserves verified moves without a final answer. Frame usage is measured but total retained storage and device budgets remain unqualified. The Lua owner and timer-driven viewer remain unfinished. Draining a coroutine or revealing an already completed solve does not qualify PERF-002.
2. Ordinary solve, diff, int, simplify and factor commands use existing recorded algorithms. Expand and rearrange use the same native dispatcher. The routing batch was independently reviewed and committed in 838f6cf. The ca4debf adapter also preserves complete finite Giac solution sets for exact quadratic comparisons. Full physical qualification of these supported envelopes remains pending.
3. Implement number and combinatoric methods with recorded division, Euclidean, factorization, primality, modular, rounding and counting rules. Add rational and complex operations under explicit domains and numeric policies.
4. Extend algebra and polynomial methods with coefficient representations, division, GCD, interpolation, elimination, substitution, rational forms and trigonometric identities. General solving must retain branch completeness and restrictions.
5. Native definite integration, supported limits and explicit first-derivative order are implemented in the calculus batch. Definite integration establishes interval validity before using endpoint values. Limits cover cataloged continuous expressions, rational removable discontinuities, one-sided poles and rational behavior at infinity. Physical samples passed, while full-envelope device qualification remains open. Sums, series, recurrences and differential equations remain future walkthrough work. Numerical methods need convergence and residual evidence.
6. Ordered List nodes and MatrixView preserve collection structure through parsing, printing and normalization. Scalar solvers refuse these inputs before algebra. Ref and Rref requests preserve matrix shape and share exact admission. Giac callback capture, exact row-operation certificates and separate final-form verification supply the matrix foundation. Ref and rref command walkthroughs are implemented and exercised through the actual host Giac Lua bridge. Handheld qualification remains pending. Other collection methods, factorizations and spectral methods need reconstruction, orthogonality and residual checks. Interval support remains unfinished.
7. Add distribution, statistical-test and plotting derivations. Retain plots alongside their construction steps. Record the actual random draws and context once for random operations. Never resample to explain an earlier answer.

The first number-method batch implements literal iquo, irem, factorial, perm, comb, is_prime, nextprime, ifactor and powmod under the bounds in the number.integer-method.literal catalog entry. Native, bridge, menu and semantic fixture checks pass. Independent review passed. The handheld returned 4 for menu-entered iquo(19,4), with verified hint disclosure and Full Text navigation. Full-envelope physical compatibility checks remain pending. Other number commands remain unfinished.

Signed literal gcd is also implemented through GMP division and a checked GMP Bezout certificate. Independent host review passed. Physical qualification remains pending. The tuple results of iegcd and iabcuv require structured result support and remain unfinished.

## Library reuse

The [calculus UI plan](calculus-ui-plan.md) tracks the current retained framework, antialiasing, ETL, EASTL, nRGBlib, nGC compatibility and LVGL qualification. MicroTex is being considered for formula layout ideas. Its integration is not assumed. New requirements and priorities belong in the [PRD](StepCAS_Product_Requirements_Document.md), with current evidence recorded separately.

Use Giac, GMP and native UI widgets before adding algorithms or layout machinery. A new implementation needs a demonstrated gap in those libraries or other available libraries, and should own only that gap. Library output must still pass the request, derivation and verification contracts below.

The matrix batch uses Giac's existing reduction in [vecteur.cc](../khi-src/src/vecteur.cc) and the ref entry in [ti89.cc](../khi-src/src/ti89.cc). The declared envelope is Exact mode with at most four rows and six columns. Empty, flat, ragged or deeper lists, symbolic or decimal cells, approximate containers, unsupported options and extra arguments are refused. Every input cell, intermediate matrix cell and event coefficient must fit the shared checked rational representation. An input within range can still produce an unsupported intermediate. This is a bounded arithmetic envelope, not completion for every matrix with int64 inputs. The my_gprintf callback supplies actual row operations and observations under RREF_GUESS with step information enabled.

The reducer now emits step_rrefscale when mdividebypivot actually normalizes a row. The event carries its row, exact multiplier and independent pre-operation matrix. Earlier event IDs are unchanged. The REF augmented-column rule is preserved. Typed conversion keeps ordered dimensions, including single-cell matrices.

Capture fixes step information at level 1 and restores the caller's setting and callback afterward. REF may return a different row-equivalent echelon matrix with step information enabled than ordinary Giac returns with it disabled. REF does not require unit pivots. RREF requires unit pivots with zeros elsewhere in their columns. The typed_differential host gate passes with shell mode on and off at step-info levels 0, 1 and 2. It checks independently specified finite root sets and context restoration. The compact-value alignment repair and a matching rebuilt host Giac archive pass the exercised ASan and UBSan checks. Consumers must use the matching headers and archive together. External numeric and system libraries remain outside that instrumentation.

Each recorded operation carries its actual operands and passes an exact row-operation check. The finished matrix must also satisfy the requested echelon conditions. Event 23 is accepted as an elementary addition under the qualified Gauss-Jordan contract. A general Bareiss event cannot be treated as the same operation because its operands omit the divisor. Pivot and completion observations do not count as transformations.

Callback capture is global and synchronous. Nested capture is refused and mutable matrix states are copied. Swaps, fractions, zero and dependent rows, rectangular inputs, final normalization and corrupted events need executable coverage. Cancellation or a Meter limit retains only the verified prefix and withholds the final result. Arena failure discards records whose node references are no longer usable. Capturing callbacks does not implement resumable Giac reduction or qualify PERF-002.

The matrix_row checker verifies every input and output cell, including unchanged rows. read_exact_matrix also rejects approximate root and row identities. record_matrix_row publishes a budgeted RowEquivalent transformation only after that check proves a changed matrix. Row records provide Do, Write and Why with an exact reversible-operation obligation. Verified identities do not count as progress. matrix_form verifies REF and RREF separately. matrix_method connects the complete verified trace to ordinary ref and rref commands. Exact identity and zero-matrix answers use a plan and final check without inventing transformations. Native fixtures and actual host Giac Lua bridge checks pass. This does not establish final ARM package or handheld qualification.

The det walkthrough uses the same Giac reduction for square matrices. It records each determinant factor, evaluates the triangular diagonal product and corrects for row swaps and scaling. GMP preserves exact factors and scalar answers beyond int64. The matrix cell envelope remains unchanged. Independent permutation certificates and corrupted factor tests check the mathematical trace, including singular matrices where a zero answer alone cannot validate a factor. Actual Lua records drive the V4 Do, Write and Why and hint tests. A physical example now has a matching loaded identity, exact answer and readable row-operation detail. Full-envelope physical qualification remains pending. The [qualification record](../nps/benchmarks/BUDGETS.md#physical-package-check-2026-09-08) identifies the tested bytes and remaining obligations.

Giac also exposes bounded Euclidean traces. GMP supplies exact division and gcdext certificates for the current integer family. StepCAS records bounded GMP operations because those APIs do not supply its resumable typed derivation contract. Structured Giac lists and matrices should cross the existing typed backend boundary with shape preserved before collection algorithms are exposed.

[FLINT rational matrices](https://flintlib.org/doc/fmpq_mat.html) provide exact result operations but no documented walkthrough events. [Mathsteps](https://github.com/google/mathsteps) exposes change records for simplification and equations, but has no demonstrated handheld matrix integration. Neither currently replaces an identified missing capability better than the dependencies already linked. Reassess external libraries when a concrete unsupported family needs one.

Each family remains unfinished until its menu entries, supported envelope and neighboring refusals are implemented and verified. A successful example does not qualify the command's full envelope. Unhandled commands retaining ordinary CAS behavior remain pending walkthrough work.

## Ownership and contracts

The native command owner maintains command parsing, solver dispatch and Lua bridge tests. The UI owner maintains menu execution, result projection and UI tests. The integration owner maintains build registration, capability manifests and catalog joins. New family owners receive separate source, header and test files. Shared AST, parser, printing and conversion changes have one owner before dependent family work begins.

All writers stop before independent review. Defects require an executable reproduction before repair. The reviewer does not implement the batch it judges.

Command recognition must not evaluate user input. Giac parsing is unsuitable as a general recognition probe because some grammar actions execute work or alter context. The restricted native parser owns recognition and validation.

Preserve the full raw command separately from the operand context. Define omitted-variable semantics explicitly. Preserve every supplied argument and option. Definite integration and higher derivatives must not become simpler operations silently. Normal form is distinct from polynomial simplification.

Every accepted mathematical transition must be generated forward by a registered rule with its preconditions, action, operands and validation evidence. Backend output or diagnostic text alone cannot become a walkthrough. Refusals preserve completed valid work without inventing an answer to the unresolved goal.

## Completion evidence

Track native implementation, bridge exposure, menu execution, host verification and physical qualification separately. Each mathematical entry needs supported cases and neighboring refusals, actual semantic rules, meaningful final checks, bounded computation and cancellation tests.

Host qualification includes native unit tests, the real Lua bridge and menu-to-bridge tests. The optional GIAC_HOST_ROOT configuration also executes matrix_capture, determinant_capture, guard_cleanup, typed_differential, matrix_cancellation, lua_matrix and value_layout against the actual host Giac library. Sanitizer runs cover request lifetimes and failures. Fresh ARM packages must pass integrity, sidecar, offline-audit and manifest checks. Reporting-tool success alone is insufficient.

The September 8 generated reports evidence 85 of 102 MVP requirements and exercise 1099 acceptance records representing 972 distinct cases. Section 27 metadata is complete for 7 of 18 catalog families with 85 field gaps. These separate populations cannot establish explanation approval or release acceptance. The deployed rendering release has narrow physical execution evidence. Native SolveTask and subsequent hardening have no handheld execution evidence. Complete release performance budgets and production incrementality remain unfinished.

Commit ea49c10 repairs native-editor input reaching hidden shell widgets while an overlay owns the screen. Independent host checks and the [handheld input check](../nps/benchmarks/BUDGETS.md#native-input-repair-check-2026-09-08) cover hint Tab, reader paging, T from list, detail and physics browser, guarded editing and return to normal calculation. Menu-entered linear solving and kinematics also have narrow visible evidence. These examples do not qualify every supported family or the remaining mathematical menu entries.

Commit 61d6f4b uses synchronous equation measurements on the first paint. Independent checks preserve delayed-callback safety and oversized text fallback. The [equation opening check](../nps/benchmarks/BUDGETS.md#equation-opening-check-2026-09-08) shows determinant matrices on the first opening of their handheld details. Familiar fraction notation remains unfinished where canonical multiplication by a reciprocal is displayed.

Physical qualification must identify the tested module and UI bytes. Exercise menu selection, input completion, steps, hints, detail, history, cancellation and recovery on the handheld. Retain complete release measurements for the frozen performance budgets and verify solving with USB physically disconnected. A key-service receipt or upload success does not prove visible execution.
