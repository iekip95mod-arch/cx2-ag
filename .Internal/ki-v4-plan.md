# Handover: finish Milestone 4, build Ki V4 as one module, fold in SymPy's lessons and vectors

This archived plan describes the September 3 checkout, including its former lack of Git history. It is not the current work queue. Use [the codebase map](../docs/codebase-map.md), [menu walkthrough plan](../docs/menu-walkthrough-plan.md) and [project instructions](../AGENTS.md) for current work.

Written 2026-09-03, refined the same day. Every fact below was checked in the session that wrote
it unless marked unverified. The tree is not a git repository: undo is the reclaim trash
(mcp__reclaim__restore with the stamp each patch call printed).

## The goal

the maintainer, 2026-09-03: "Our critical goal here is to get this being able to handle physics problems."
Everything below serves that. The calculus and algebra step modes exist because physics
problems need them; the measure of every step in this plan is whether a student can type a
physics problem into Ki and get a checked, explained answer with units. Milestone 4 (1-D
kinematics, steps 1 to 13) is the first physics slice, vectors (steps 24 to 28) the second,
and the unified module (14 to 18) is what makes Giac usable for the physics work without a
Lua round trip. When a step competes with physics coverage for time, physics wins.

## Direction, in the maintainer's words

Later instructions override older docs.

- "Make a Ki_V3 and then execute our entire plan into that." Plan: PERF-005/006 on the handheld
  (done), PRD and open-questions amended (done), Milestone 3 integration with derivative check
  (done), Ki V3 UI (done), Milestone 4 units and 1-D kinematics (in progress), independent
  oracle (not started).
- "KiV3 is just an extension of KhiCAS. KiV1 is the updated version of KhiCAS. Use KiV1, only
  use KhiCAS as a reference. Khi src is where the source for KiV1 is." Ki V3 and V4 are the
  whole Ki V1 document (khicas53/khicas.lua) with step modes appended. Never a cut-down shell.
- "Use the emulator for testing this. Make sure the emulator also has luagiac too." The
  emulator with Giac resident is the verification target while the handheld is off the bus.
  An emulator result is a claim about the emulator only.
- "Use our proper math libs. Don't roll your own math solvers." No private evaluators or
  isolation tables. Kinematics hands its equation to solve_linear and to Giac. The first
  kinematics.cc had its own rearrangement table and evaluator and was discarded for this.
  Shared arithmetic lives once, in src/rational.h.
- "Take heavy advantage of Giac." Giac does what the core cannot (symbolic rearrangement
  today) as a recorded, checked step, and still cross-checks every answer. The typed adapter
  (src/giac_adapter.h) is the only route: never build a Giac command string elsewhere.
- "Why are we calling it indirectly? Why separate modules? Make a V4 that is unified." The Lua
  round trip to luagiac.caseval is history, not design. V4 links Giac into the stepcas module:
  one artefact, direct calls, no Lua re-entrancy. PRD PLAT-010 and section 12.2 change, with
  date and reason.
- "Make our representation of unit vectors in standard Cartesian unit-vector form." Vectors
  are shown as a i + b j + c k with hats, never as a tuple, column or Giac list. Components
  inside, hats at the display. Steps 24 to 28.
- Fold in SymPy's step machinery. Steps 19 to 23.

Unchanged: every step recorded with its verification, unsupported techniques stop explicitly, a
failed check withholds the answer, every backend reply tagged. The PRD is the specification;
STATUS.md, .Internal/ki-v3.md and open-questions.md are the record. Update them as you go.

## State of the tree

`make -C nps test`: 623 host checks, 14 failing, all in the new Milestone 4 code. Nothing
older regressed. Ki V3 is verified on the emulator for differentiate, integrate and solve
(probe/e34 to e38). The handheld is not enumerated on USB and needs the maintainer to restart it.

Milestone 4 so far, all new:

- src/rational.h: shared exact int64 fractions. linear.cc and canonical.cc include it instead
  of private copies; their tests pass.
- src/units.h/.cc: Dimension (L, M, T), hand-written unit scanner (m, km, cm, mm, s, ms, min,
  h, kg, g with ^, / and *), Quantity, to_si. test/units_tests.cc.
- src/kinematics.h/.cc: structured entry ("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s"), four
  constant-acceleration equations, selection by probing each candidate with solve_linear on a
  scratch Derivation, plan with alternatives, unit conversion step, dimensional Check on the
  symbolic equation, optional Giac rearrangement step (Backend* argument), substitution step,
  then solve_linear on the real Derivation. test/kinematics_tests.cc.
- src/lua_module.cc: kinematics and kinematics_local. kinematics passes a LuaGiacBackend into
  the solve, then cross-checks by asking Giac to solve the substituted equation. No luax_host
  checks and no Ki V3 mode yet.
- Registered in Makefile SRCS and TEST_SRCS, tools/tidy.sh, test/run_tests.cc,
  test/adapter_tests.h, test/golden_tests.cc (kinematics_record and three fixtures, unrecorded).

## Steps

0. Read ~/.claude/CLAUDE.md, this file, nps/STATUS.md and .Internal/ki-v3.md. Run
   `make -C nps test` and confirm 623 checks with 14 failures, all in units_tests,
   kinematics_tests and the three kinematics golden fixtures. Copy test/golden to
   build/golden_before_m4 with mcp__reclaim__copy.

1. test/units_tests.cc: the km/h^2 scale is 1000/3600^2 = 1/12960, not 5/64800. Fix the
   expectation.

2. Diagnose the five solved cases that return no answer: x from v0, v, a and a from v0, v, x
   (equation 3), x from v0, v, t and t from v0, v, x (equation 4), and v from v0 = 0 m/s,
   a = -9.8 m/s^2, t = 2.5 s (equation 1, negative fraction node). Write
   nps/build/scratch/kin_probe.cc (compile with -Isrc) that parses
   "17^2 = 5^2 + 2*3*x" and "x = (1/2)*(5 + 17)*4", calls solve_linear, and prints outcome and
   detail; also run those two kinematics inputs and print result.detail. Suspects in order:
   Arena::integer("-49") not marked small_valid (ast.cc), linear.cc analyse's Pow case with a
   substituted constant base, arena.nary for Pow or Equals inside kinematics.cc substitute().
   Do not guess.

3. Fix the cause of step 2 where it lives, with a test in the file that owns it (ast tests,
   linear_tests or kinematics_tests). If the fix is in rational_node, the linear solver's
   expected node shapes are Integer, Neg, Add, Mul, Pow(constant, integer) only.

4. Print the detail for "v from v0, a, x" (square root case) and for the int64 overflow case,
   then set the two expectations to the truth. If the overflow case reports "not linear",
   linear.cc analyse is conflating overflow with non-linearity: make analyse report
   ResourceLimitReached for overflow, add a linear_tests.cc case, and keep the kinematics
   expectation on the honest wording.

5. test/kinematics_tests.cc: the Giac rearrangement prints as "((v + (-v0)) * (a^(-1)))"
   because a parsed reply carries Neg(1). Fix the expectation.

6. Resolved 2026-09-04. Every Meter polls once when constructed, while the rewrite stride stays at
   64. Short cancellation tests now cover all four engines and require no answer or partial steps.

7. `make -C nps regold`. Diff build/golden_before_m4 against test/golden: only
   kinematics_final_velocity, kinematics_quadratic_refused and kinematics_step_budget_halt
   may be new. Read the three. final_velocity must show the kinematics plan, convert-units,
   the dimensional check, substitute, then the linear solver's plan and its check.

8. `make -C nps san` and `make -C nps fuzz`. Both were clean before Milestone 4.

9. test/luax_host.lua: add kinematics checks in the style of the integrate ones. A kinematics
   call with Giac makes up to four calls: solve(symbolic), is_zero, then the cross-check's
   solve and is_zero. Script "[[...]]" replies for solves and "0" for is_zero. Check outcome,
   result "v = 17 m/s", rearranged, agrees, giac_calls, that kinematics_local makes no Giac
   call, and that invalid input returns outcome "invalid input" with an empty steps table.
   Run `make -C nps luaxhost`.

10. lua/nps_v3.lua: add `k = { key = "kinematics", label = "kinematics" }` to STEP_MODES, a
    Steps menu entry beside the other three as a closure, the help line in runSteps, and
    "kinematics" in on.restore's mode list. runSteps passes steps.variable as a second
    argument that kinematics ignores; the history line is r.result.

11. test/ui_smoke_v3.lua: add a kinematics stub to the fake stepcas table and checks that
    "!k find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s" calls it, opens the viewer and puts r.result
    in the history. `make -C nps uitest`.

12. `make -C nps document`. Flash-save and restart the emulator. Deploy
    build/device/nps_split.luax.tns as stepcas.tns then `ln mv` it to nps_split.luax.tns in
    /ndless; deploy build/device/nps_v3.tns to My Documents. Open it, type the !k line from
    step 11, enter, screenshot to probe/. Record the run in STATUS.md and .Internal/ki-v3.md.

13. Docs for Milestone 4: STATUS.md Milestone 4 section, .Internal/ki-v3.md !k mode, PRD
    section 24 and the Milestone 4 row. Repo markdown and comments: no semicolons, backticks,
    em dashes or arrows outside fenced blocks.

14. Makefile: add a `unified` target producing build/device/nps_nspire.luax.tns. Objects: $(DEV_OBJS),
    build/device/lua_module.o built with -DSTEPCAS_GIAC=1, the 63 OBJS from
    khi-src/src/Makefile.ki (rebuild with `make -C ../khi-src/src -f Makefile.ki` if stale)
    and khi-src/src/luabridge.o. Not luagiac.o: it has its own main and caseval. Link with
    nspire-ld and luagiac's LDFLAGS `-Wl,--nspireio,--gc-sections -L$(PREFIX)/lib -lmpfi
    -lmpfr -lgmp`, then genzehn --compress --name ki. Leave the existing luax target as is so
    Ki V3 keeps building.

15. src/lua_module.cc under STEPCAS_GIAC: `class DirectGiacBackend : public Backend` whose
    eval calls giac_caseval(command.c_str()) (declared in giac-src/src/luabridge.h) and copies
    the reply; a `caseval` Lua entry in the lib table; register the table as "ki". Each
    *_into picks DirectGiacBackend when STEPCAS_GIAC, LuaGiacBackend otherwise. Keep the
    Giac-first, table-after order in every entry so both builds behave the same. Keep _exit(0)
    in main: the resident-exit lint enforces it and Giac's globals must never be destroyed.

16. lua/nps_v4.lua: mcp__reclaim__copy of lua/nps_v3.lua. Replace nrequire("luagiac") and every
    luagiac.caseval with the ki module (hasSteps = pcall(nrequire, "ki"), ki.caseval); the V1
    caseval site is khicas53/khicas.lua line 1393, plus the version read at first paint. V4
    must not nrequire luagiac. Add DOCUMENT_V4 to the Makefile beside DOCUMENT_V3.

17. test/ui_smoke_v4.lua: copy of ui_smoke_v3.lua whose fake module exposes caseval and is
    returned for nrequire("nps_nspire"). Add it to the uitest target. `make -C nps uitest`.

18. Verify V4 on the emulator: flash-save and restart, deploy nps_nspire.luax.tns to /ndless (send as
    ki.tns and `ln mv` if the two-extension name is refused) and nps_v4.tns to My Documents.
    Run a plain line, `!d x^2`, `!i 1/x`, `!s 2x+5=13` and the !k line. Screenshot each.
    Then: STATUS.md, new .Internal/ki-v4.md, PRD PLAT-010 and section 12.2 amended to one
    module with the date and reason (no separate luagiac, no Lua re-entrancy, one artefact),
    open-questions.md gains the typed gen adapter as an open item.

19. Answer without steps, labelled (Gamma's DontKnow behaviour). When the core refuses and
    Giac has an answer, every *_into sets `answer_only = true` with the answer and the Giac
    tag; the viewer renders one labelled line ("no steps for this one") and never a
    derivation. PRD 17 still holds: the technique stops explicitly.

20. Substitution as a nested derivation in its own variable (SymPy's URule). Add a payload
    field naming the substitution variable; children are the substep's steps in u; the closing
    Transformation is the back-substitution; the viewer says "Let u = ...". Applies to
    anything beyond integrate.cc's inline i.linear-substitution, and to parts.
    NEEDS DECISION: the maintainer to confirm steps 20 to 22 are wanted before the oracle (23) and the
    vector work (24 to 28), since each reshapes Derivation payloads. Given the goal above,
    the default is vectors first (24 to 28), then 19, then 23, and 20 to 22 only when a
    physics family needs them.

21. Alternatives with steps. When a real second route exists (parts either way round, two
    kinematics equations that both fit), record it as a Branch under the plan with its own
    substeps; the viewer collapses it as Gamma's "Method #2". Never synthesise alternatives.

22. A contains-refusal predicate on Derivation, the dual of all_verified_from, so a composite
    rule can report partial success the way SymPy's AddRule does when one term is unknown.
    Today a refusal anywhere refuses the whole integral.

23. SymPy as the independent oracle (plan item 6). SymPy is not installed here (python3:
    no module named sympy). Create a venv under nps/build, install sympy there, write
    tools/oracle.py to run the golden fixtures and a generated corpus through SymPy's diff,
    manualintegrate and solve, compare values by asking SymPy whether the difference
    simplifies to zero, and compare our rule names against manualintegrate's Rule tree.
    Record disagreements as findings, not test failures.

24. Vectors, core. DONE 2026-09-04. The decision is answered by the pack's section 13 rather
    than needing a call: archetype 5 is 2-D vector addition in Cartesian unit-vector form, and
    it comes before the dot product (6) and projectile or relative motion (7). `Vector` in
    units.h carries x, y, z, a rank of 2 or 3, one shared Unit and a Frame, since section 14
    rejects unframed vectors and every operation refuses a frame it was not given. Addition,
    subtraction, scaling, dot, cross and a magnitude that answers only when the root is exact.
    `vector_text` prints as planned. One deviation: the unit printed is the vector's own
    spelling rather than always the SI one, so the numbers and the unit agree; an operation
    normalises to SI first, so a computed vector prints the SI spelling anyway.

25. Vectors, entry. DONE 2026-09-04. parse_vector, not parse_quantity, since a vector and a
    scalar refuse for different reasons and one function would have had to guess which was
    meant. Reads "3 i + 4 j m/s", "(3, 4) m/s" and its own printed form, and never echoes the
    tuple form.

26. Vectors, Giac: add Dot, Cross and Norm to the adapter's Op allowlist, mapped to Giac's
    `dotprod` (misc.cc line 2146; TI spelling `dotP`, ti89.cc line 1356), `cross`
    (vecteur.cc line 14685) and `l2norm` (misc.cc line 1814), all under giac-src/src.
    build_command writes components as a list and interpret reads a list reply back. Do not
    define i, j, k as Giac variables: `i` is Giac's imaginary unit and an assignment is global
    state in the resident engine. Do not flip Giac's global angle mode (`angle_radian`,
    prog.cc line 6206): convert degrees to radians in the adapter and say so in the step.
    Approximate forms come from Op::Approximate and carry the Approximate tag. Cross-check
    each op with is_zero on the difference. adapter_tests for each.

27. Vectors, display. Check on the emulator with a one-line document whether the Nspire Lua
    font renders î (U+00EE), ĵ (U+0135) and k with a combining circumflex (U+006B U+0302;
    there is no precomposed form). If not, the viewer draws bold or italic i, j, k in the same
    a i + b j + c k order. The fallback never enters the core.

28. Vectors, evidence: a golden fixture for the first vector problem and a smoke check that
    the viewer draws whichever glyph strategy step 27 chose. The `struct CartesianVector
    { giac::gen X, Y, Z; }` idea no longer waits: the typed gen adapter landed 2026-09-03 as
    src/giac_typed.cc, so a component can be held as a gen rather than as text.

Parallelism: steps 1, 5 and 6 are independent of 2 to 4. Steps 9, 10 and 11 are independent
of each other. Steps 14 and 15 are independent of 16 and 17. Steps 24 to 26 are independent of
each other once 24's decision is made.

## SymPy background for steps 19 to 23

Read from the sources below, not run. SymPy has step machinery for integration only
(sympy/integrals/manualintegrate.py); SymPy Gamma adds differentiation (app/logic/diffsteps.py).
Neither has step-by-step equation solving: Gamma's app/logic holds diffsteps.py, intsteps.py,
stepprinter.py, resultsets.py, logic.py, nlcommand.py and utils.py.

- The derivation is a tree of Rule objects. `integral_steps(integrand, symbol)` returns a
  Rule; `manualintegrate(f, var)` calls its `eval()`. Every Rule has `integrand`, `variable`,
  `eval()` and `contains_dont_know()`. Atomic rules have no substeps; composites carry them:
  ConstantTimesRule, AddRule(substeps), URule(u_var, u_func, substep), PartsRule(u, dv,
  v_step, second_step), RewriteRule(rewritten, substep), AlternativeRule(alternatives),
  PiecewiseRule, DontKnowRule.
- Refusal is a value: DontKnowRule.eval returns the unevaluated Integral and every composite's
  contains_dont_know is true if any substep's is. Extension recipe from the docs: subclass
  Rule, implement eval, write a function from IntegralInfo to a Rule or None.
- AlternativeRule keeps every method that worked; eval uses the first; Gamma prints the rest
  as collapsible "Method #N".
- URule.eval evaluates the substep in u and substitutes back; Gamma prints "Let u = ...",
  renames with replace_u_var, then the back-substitution.
- Recording, evaluation and rendering are separate: rule tree, eval, and Gamma's
  IntegralPrinter and DiffPrinter dispatching on rule type (print_U, print_Parts,
  print_Alternative, print_DontKnow) over stepprinter.py's append, new_level, new_step,
  new_collapsible. diffsteps.py uses namedtuple rules and an `evaluators` dictionary.
- print_DontKnow writes "Don't know the steps in finding this integral. But the integral is"
  and then SymPy's black-box result.
- Cost bounding. Read in full 2026-09-03 and the earlier note here was wrong: SymPy does bound its
  search, in four ways at once. IntegrationSolver.__init__ takes max_depth with a default of 100.
  The set _active holds the subintegrals currently on the stack, so its size is the depth and its
  membership is the loop check, one field doing both. _parts_u_count caps each candidate u at 2
  tries. The memo _solved is deliberately not written when the depth budget was hit, and the comment
  says why: a result that bottomed out is a depth artifact rather than the true answer, and reusing
  it for the same subproblem reached elsewhere with more budget left would be wrong. That last one
  is the interesting one for us, because it is the failure mode a naive cache has and nobody notices.
- The dispatcher, which was an open question here, is sympy.strategies combinators rather than a
  hand written chain: do_one over null_safe wrappers, a switch keyed on the integrand head for Pow,
  Symbol, Add and Mul, alternatives(..., branch=self.branch) which is what produces Gamma's
  Method #2 tree, and w(fallback_rule) last.

StepCAS already shares: rules as data with the answer derived (linear.h says so), rendering
separate from recording, typed refusal, a rule id per step. Not to copy: Python object trees
and isinstance dispatch, unbounded search (the Meter stays), Gamma's HTML printer, the
special-function tail.

One difference worth naming, because it is a real choice rather than an accident. Gamma's rule tree
does not carry results. Every printer calls diff(rule) again at the node it is about to display, so
the printed value cannot drift from the tree that justifies it. StepCAS carries the result in the
TransformationPayload, which is right for us (nothing is cheap to recompute on this device, and the
record has to survive a save and reopen without the engine), but it means the invariant Gamma gets
for free has to be held by complete_transformation refusing to overwrite an answer already there.
That refusal is the whole guarantee. It should never be relaxed.

The claim that neither SymPy nor Gamma does step-by-step equation solving was checked directly
against the git trees rather than assumed: sympy has no file named for steps outside
sympy/integrals/manualintegrate.py, sympy/solvers has no steps module, and Gamma's app/logic has
diffsteps.py and intsteps.py and nothing for solving. sympy_gamma is not archived (last push
2024-04-20). There is no sympy_beta under the sympy org. So for the part of StepCAS that matters
most, showing the steps of an equation solve, there is no prior art in this lineage to copy.

Sources: [manualintegrate.py](https://github.com/sympy/sympy/blob/master/sympy/integrals/manualintegrate.py),
[SymPy integrals docs](https://docs.sympy.org/latest/modules/integrals/integrals.html),
[intsteps.py](https://github.com/sympy/sympy_gamma/blob/master/app/logic/intsteps.py),
[diffsteps.py](https://github.com/sympy/sympy_gamma/blob/master/app/logic/diffsteps.py),
[stepprinter.py](https://github.com/sympy/sympy_gamma/blob/master/app/logic/stepprinter.py),
[app/logic listing](https://github.com/sympy/sympy_gamma/tree/master/app/logic).

## Andes and solution_tracer: prior art for the physics half

Read 2026-09-03. Two papers are in .Internal/references and one repository was read from source.
This is the lineage SymPy does not cover, because Andes is about physics problems specifically and
about checking a student's own derivation rather than printing the system's.

### Color by numbers, which is the cheapest correctness check in the literature

Andes precomputes, per problem, a solution point: every variable in the problem paired with its
value once the problem is solved. To check any equation a student writes, it substitutes the
solution point in, which leaves a statement with numbers and no variables, and asks whether that
balances. Shapiro proved this is equivalent to the equation being algebraically derivable from the
givens and the fundamental principle applications, so a numeric test at one point stands in for a
derivation search.

Three details that make it work and are easy to miss.

- Symbolic parameters that the problem never gives a number for get an ugly generated value, and so
  do quantities that cancel out of the solution. Without that, an equation mentioning a cancelling
  mass would balance by accident.
- It needs high precision. Andes requires about 9 digits from students entering numbers, and turns
  the entry red when they round, which also has the side effect of pushing students to write
  symbolic equations and use the solve tool.
- It has one known hole. An incorrect subexpression multiplied by something that evaluates to zero
  at the solution point survives the check. Measured rather than feared: in the Fall 2001 run,
  5766 problem solutions were entered and exactly one equation was marked green that a human grader
  would have marked wrong.

StepCAS already does the one-variable case of this and calls it "substitute the solution into the
collected form" in linear.cc. The generalisation, pinning every variable in a problem to a number
once and reusing that point to check anything, is not something we do, and it is the natural
verifier for a multi-equation physics problem where no symbolic engine is attached.

### How Andes generates the solutions, which is the part we do not have

A state is three sets: the equations generated so far, the quantities known, and the quantities
sought. Initially there is one equation per given, the givens are known, and the problem's questions
are sought. Each move picks a Problem Solving Method that mentions a sought quantity and applies it,
which generates equations. The indy check then asks whether the new equations are independent of the
ones already in the state, and the move is rejected if they are not, so the state never accumulates
an equation derivable from the others. The search runs to exhaustion, giving every solution the
knowledge base permits, and the final states have N independent equations in N knowns, which the
equation solver turns into the solution point. Andes has 356 problems over 550 physics rules.

The indy check itself is numeric too. The gradient of an equation is its partial derivative with
respect to each variable, evaluated at the solution point, taken as a vector of numbers. Independence
of a set of equations is then linear independence of their gradients, and the question "which of the
reference equations did the student combine" becomes "which subset of gradients sums, with weights,
to the student's gradient". Their worked example finds S equals A minus 0.342 times B. Andes1 solved
the same problem by precomputing every algebraic combination of the steps, which stopped scaling and
was called insurmountable for months before Shapiro's algorithm replaced it.

### Their node types against ours

The CLIPS paper lists five: GOAL, FACT, EQUATION, EQUIVALENT and STRATEGY. Ours are four: Plan,
Transformation, Branch and Check. GOAL is our Plan, EQUATION and EQUIVALENT are Transformation,
STRATEGY is Branch. The two that do not line up are the interesting ones. They have FACT as a
first-class node, where we keep problem facts as strings inside PlanPayload.matched_problem_facts,
which means nothing downstream can depend on a fact and be shown to. We have Check, which they have
no equivalent of, because Andes generates the reference solution and never has to justify it.

A STRATEGY node is a decision point with its choices enumerated, and every equation generated
downstream is tagged with the decision and choice number that produced it. Our BranchPayload has
siblings_exhaustive, which is the same claim that the enumeration is complete, but our downstream
tagging is implicit in the parent chain rather than carried on the record.

### What Andes tried and threw away

Andes1 used a Bayesian network for plan recognition, to work out which plan the student was
following and hint the next step in it. They evaluated it against three physics instructors on 40
episodes. The instructors agreed with each other on 21 of them, and Andes agreed with that consensus
on 3 of the 21. The replacement in Andes2 is deliberately dumb: walk the solution graph depth-first
top to bottom and hint the first step the student has not done, and at a branch take the branch with
the largest proportion of steps already done. The paper's sentence for why is worth keeping: they
prefer step selections that are occasionally pedantic to ones that are occasionally confusing.

The same paper reports the Bayesian student model was accurate and was not the source of Andes'
effectiveness, and they removed it. What produced the learning gain was the grain size of feedback,
checking each step rather than the final answer, which is the thesis StepCAS is built on.

### solution_tracer

github.com/indra-uolles/solution_tracer, Python over SymPy, small and readable. It exists because
the author judged the Andes dependency check too complicated, and the README says so in those terms.

The piece to take is build_solutions_tree(calc_relations, sought_variable, known_variables). Each
relation declares which variable it computes and which variables it needs. The search chains
backward from the sought variable, and a path that stops advancing without reaching the known set is
a dead end and gets deleted. What comes out is every route from the givens to the answer, as a tree.
ProgressCalculator then marks which relations along each route the student has used and reports the
best route's fraction complete, which is a definition of partial progress that does not need a
student model.

Its equivalence check (common/equiv.py) is simplify(a - b) equals zero with rounding fallbacks, which
is the thing color by numbers exists to avoid. Do not copy that half.

### What this means for StepCAS

Written 2026-09-03 and acted on 2026-09-04. The backward chain with the dead-end filter is now in
src/physics/kinematics.cc and the probe's own case answers 11 m/s on the calculator. Two things in
the last paragraph below were not built, and both for the same reason. The Equation table did not
become relations that declare what they produce and need, because what an equation needs is derivable
from the symbols in it and a declaration would be a second copy to keep true. And the search finds
one route rather than every route, so nothing became a Branch record: the equations it passed over
are plan alternatives with the reason, which is what alternatives_considered already held. The
reading is kept as it was written.

Our kinematics solver picks one equation. It substitutes the knowns into each of the four
constant-acceleration equations in turn and takes the first the linear solver can solve. That is a
one-hop search, and the refusal is real rather than theoretical. Measured with a scratch probe on
2026-09-03:

```
find v; x = 20 m; t = 4 s; a = 3 m/s^2
  no applicable equation
  detail: no constant-acceleration equation reaches v from what is given; v = v0 + a*t: needs v0,
  which is not given; x = v0*t + (1/2)*a*t^2: does not contain v; v^2 = v0^2 + 2*a*x: needs v0,
  which is not given; x = (1/2)*(v0 + v)*t: needs v0, which is not given
```

The answer exists. Equation two gives v0 as minus 1 m/s, then equation one gives v as 11 m/s. Two
hops, and we refuse. Note that the refusal message is honest and per-equation, which is the right
behaviour for a refusal we cannot avoid, so this is a missing capability rather than a defect.

For pure SUVAT this costs almost nothing, because with four equations over five quantities any
single unknown with three others given is reachable in one hop. It starts costing the moment a
second family of equations is added, which is exactly what step 24 is about. Whoever takes step 24
should write the chaining search before the second family, not after, because adding forces or
energy to a one-hop solver produces a solver that refuses most of the problems it now nominally
covers.

Concretely, in our terms: the Equation table becomes relations that declare what they compute and
what they need, the backward chain from the unknown produces every route, the dead-end filter drops
the routes that never reach the givens, and each surviving route is a Branch record whose siblings
are the other routes. The plan's alternatives_considered already holds the shape of this, one line
per equation with the solver's own reason, so the record does not need a new kind. What it needs is
for the search to continue past the first hop.

Two smaller things worth taking whether or not step 24 happens.

- The solution point as a verification method. Once a problem is solved, pinning every variable to
  its value and checking any candidate equation against it is O(size of the expression) and needs no
  backend, which matters here because Giac is the expensive dependency and the handheld may not have
  it. Shapiro's proof is what makes it a check rather than a heuristic, and the failure mode is
  known and measured rather than open ended.
- FACT as a record kind. Our problem facts are strings on the plan, so a step cannot cite the fact
  it depends on. Andes needed the dependency links to build anything downstream. If we ever want to
  answer "why is this step allowed", the fact has to be a node.

Not to take: the Bayesian student model (they removed it and said it was not the source of the
gain), plan recognition (measured worse than a fixed ordering), and per-problem precomputation
(they kept the two-phase build for maintainability, not speed, and we have neither their problem
count nor their authoring team).

Sources: [Andes lessons learned, VanLehn et al 2005, IJAIED 15(3)](references/9012975.pdf),
[Schulze et al, A CLIPS Problem Solver for Newtonian Physics Force Problems](references/A_CLIPS_problem_solver_for_Newtonian_physics_force.pdf),
[solution_tracer](https://github.com/indra-uolles/solution_tracer).

## Build facts for steps 14 to 18

- luagiac is built by khi-src/src/Makefile.ki: 63 OBJS plus luagiac.o and luabridge.o,
  GCCFLAGS include -fno-exceptions -DNO_STDEXCEPT -DKHICAS, LDFLAGS
  `-g -Wl,--nspireio,--gc-sections -L$(PREFIX)/lib -lmpfi -lmpfr -lgmp` with the libraries
  under ndless-sdk/toolchain/install/lib (built from deps/). Its main calls
  nl_lua_getstate(), luaL_register(L, "luagiac", lualib), then _exit(0) (khi-src/src/luagiac.c).
- stepcas's module: DEV_FLAGS `-std=c++11 -Os -marm -fno-exceptions`, nspire-ld, genzehn
  --compress. main registers "stepcas" (src/lua_module.cc line 822) and
  tools/lint-resident-exit.sh fails the build if main returns.
- giac_caseval(const char*) is giac::caseval with vx_var set to x (giac-src/src/luabridge.cc).
- luagiac needs 8432788 bytes to load (.Internal/ki-port.md, giacprobe section); about 22 MiB
  is free in one block from Documents and 4928 KiB from the startup folder. The unified module
  cannot be a startup program.

## Hazards

Unified build:

- Both images' static constructors run in one process, and neither side's globals may ever be
  destroyed: keep _exit(0).
- Giac is -fno-exceptions -DNO_STDEXCEPT and stepcas is -fno-exceptions. Do not enable
  exceptions on one side only.
- Two mains: link luabridge.o, never luagiac.o.
- Undefined giac symbols at link mean stale or differently flagged Giac objects: rebuild with
  Makefile.ki, which depends on itself for exactly this reason.
- A document that also loads luagiac.luax.tns holds two Giacs.
- A third Lua document loading Giac in one emulator session never finishes loading:
  flash-save and restart between documents.

Emulator and tooling:

- `emu restart boot=true debug_on_start=true debug_on_warn=true print_on_warn=true`, boot
  about 75 s. Every dbg command stops the guest: end batches with `; c`. Screenshots:
  `screenshot <path.ppm>` then sips to png.
- keysvc packets `ln svc 0x4B45 <records>`, 4 bytes each (code_lo, code_hi, modifiers,
  action): enter `0D 10 00 00`, esc `1B 96 00 00`, '!' `21 04 04 00`; letters per
  tools/nsptool keydefs. Arrow codes do nothing through keysvc on any screen; the viewer takes
  on.arrowKey and the named handlers with a 40 ms debounce, verified only by the host smoke
  test. A key that opens a document corrupts keysvc's parked frame: reply and log before
  posting, nothing blocking after.
- Two-extension names are sometimes refused by the link: send as name.tns and `ln mv`.
- /tmp is invisible to the emulator link. Reconnecting the MCP drops the emulator session. An
  exec of a service program kills the link.
- The handheld drops off USB after a few screenshot round trips and is currently not
  enumerated. It needs a physical restart, and still owes one for the config-cap resources
  and keysvc.
- D2Editors are OS widgets: setVisible(false) does nothing; park them at -10000,-10000 and
  wrap reposME, which size-change listeners call.
- Palette and menu entries must be closures; a later-defined bare function name is nil at
  registration and the OS reports "expected function in menu item".
- "2^-1" is a literal -1 exponent, "2^(-1)" a parsed Neg(1). Different trees, both right.
- Bash cwd drifts: start commands with `cd <workspace>/stepcas`.
- Auto mode inserts "prefer Bash for reading and editing"; the guards refuse it. Read with
  Read, edit with Edit or mcp__patch__replace, delete, move or copy with mcp__reclaim__*.
- The clang-format hook reformats touched lines; if an Edit stops matching, re-read first.

## Learned this session

- Handheld, everything resident (probe/hw_perf1.png, hw_perf2.png): d/dx of x^2*sin(x) 20 ms
  cold, solve 2x+5=13 10 ms, both with the Giac cross-check. PERF-005/006 are two orders of
  magnitude away for worked examples (open-questions item 8, PRD).
- Emulator, real Giac, Ki V3: `!i 1/x` gives (C + ln(x)), Giac's derivative agrees, assumes
  x > 0, 10 ms; `!d x^2` gives (2 * x); `!s 2x+5=13` gives 4 (probe/e34 to e38).
- canonical.cc folds rational coefficients, x^1 to x and int^positive int, which lets tests
  compare x^3/3 with x^3 * 3^-1. Overflow is a boundary: 9223372036854775807 * 2 / 2 stays.
- integrate.cc ends with a Check that differentiates the antiderivative and compares canonical
  forms. Falsified with a wrong rule (build/scratch/integrate_bad.cc, falsify.cc): it reports
  "verification failed" and withholds the answer. Do the same for every new check.
- lua_module.cc keeps nothing of ours on the Lua stack while Giac runs, because the call
  re-enters Lua. Every *_into asks Giac first and builds the table after.
- luagiac exposes one function, caseval, string in and out. Every typed guarantee in PRD 12.1
  and 12.3 is the adapter's to keep; a solve reply arrives as a list like [[4]].
- The linear solver reads Integer, the unknown Symbol, Add, Mul, Neg and Pow with a constant
  base and integer exponent, not Decimal. Fractions go in as Mul(n, Pow(d, -1)) and come back
  as Mul(n, Pow(d, Neg(1))).
- A Derivation can hold two root plans (kinematics adds one, solve_linear adds its own). The
  viewer renders by depth; check V4's viewer shows the second plan sensibly.
- The Meter polls once on entry and every 64 rewrites after that.
- Ki V3 is Ki V1's khicas.lua plus the step code; editor parking and the reposME wrapper make
  the overlay work. test/ui_smoke_v3.lua (247 checks) stubs class, D2Editor, toolpalette,
  platform.withGC and an advancing timer.
- Subagents only when the maintainer asks. None were used.

## Open questions to carry

- ki_v1.tns (12179 bytes on the handheld) is undecoded; V3 and V4 fork the source, not the .tns.
- How the touchpad posts arrows, so keysvc can press them.
- Typed Giac (gen level) adapter, after V4.
- Answered 2026-09-04: the chaining search came before step 24's second family, as the Andes and
  solution_tracer section argued. What the second family will still owe it is the two filters a
  four-equation table cannot justify, forward closure and dimensional rejection before a candidate
  is tried.
