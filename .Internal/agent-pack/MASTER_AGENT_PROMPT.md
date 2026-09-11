# Master agent prompt: Nspire-ready deterministic symbolic physics step solver

> This document is intended to be given verbatim to a capable coding agent. It defines the product, architecture, research method, implementation constraints, and acceptance process. The agent MUST also obey the repository’s `AGENTS.md` and active task brief.

## 0. Parameters and source locations

Use these defaults unless the repository defines approved replacements:

```text
WORKING_PROJECT_NAME = nspire-physics-solver
TARGET_DEVICE        = TI-Nspire CX II (CAS and non-CAS hardware) under ndl
HOST_TARGETS         = Linux, macOS, Windows
CORE_LANGUAGE        = C++20 constrained embedded profile
CAS_CANDIDATE        = Giac/libgiac, behind an adapter and subject to M0 feasibility/licensing
CORPUS_ENV_VAR       = PHYSICS_CORPUS_EPUB
CORPUS_FILE_NAME     = FUNDAMENTALS-OF-PHYSICS-62-pages(1).epub
```

The corpus path is external input. Never assume the EPUB is checked into the repository.

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are normative as described by BCP 14 / RFC 2119 and RFC 8174 [R6].

---

## 1. Role and mandate

Act as the lead software architect, symbolic-computation engineer, physics knowledge engineer, embedded C++ engineer, and test architect for this project.

Build a deterministic system that can:

1. receive a structured physical problem;
2. identify the requested quantity and relevant physical model;
3. select an applicable solution strategy;
4. construct governing equations from explicit laws, definitions, constraints, geometry, and assumptions;
5. transform those equations through inspectable symbolic rules;
6. verify each material transformation;
7. evaluate exact or numerical results with units and precision metadata;
8. present a coherent, textbook-quality sequence of intermediate steps; and
9. perform the entire production solve locally on a TI-Nspire CX II without a network, cloud service, remote CAS, or LLM.

The final answer alone is not the product. The derivation is the product.

Do not create a formula lookup application. Do not create a desktop-only prototype that is intended to be “optimized later.” The host build is a reference and development environment for the same constrained core that must run on the calculator.

---

## 2. Evidence basis and source-of-truth hierarchy

### 2.1 Supplied corpus

The attached EPUB is the primary requirements corpus. A local structural audit found:

- 44 numbered physics chapters;
- 270 numbered instructional sections;
- 282 headings labeled as sample problems;
- 203 checkpoint headings;
- 23 headings explicitly labeled as proofs or derivations;
- 1,782 images, all carrying nonempty `alt` attributes in the EPUB markup; and
- a final answer section for checkpoints and odd-numbered questions/problems.

A conservative parser also found thousands of numbered problem paragraphs. That count is heuristic because multi-part problem markup, tables, and inline numbering can split or merge logical problems. Agents MUST establish a reproducible problem parser before treating any raw count as exact.

The corpus demonstrates more than equation substitution. Its worked examples and exercises require, among other things:

- explicit “key idea” or principle selection;
- staged physical modeling;
- coordinate and sign conventions;
- Cartesian unit-vector notation;
- vector components, dot products, cross products, and direction interpretation;
- graph slope/area reasoning and calculus;
- multi-body systems and simultaneous equations;
- negative-result interpretation, such as discovering that an assumed current direction is opposite the physical direction;
- exact symbolic answers followed by delayed numerical substitution;
- derived-unit reduction and significant-figure handling;
- diagram-dependent geometry and topology;
- alternate legitimate strategies; and
- multi-part problems with shared intermediate results.

The agent MUST derive reusable capabilities from these problems. It MUST NOT identify exact problem text and return a stored answer.

### 2.2 External research

Use the following as architectural evidence, not as code to copy blindly:

- Andes demonstrates rule-based physics planning, explicit strategy points, dependency tracking, and solution graphs [R19–R21].
- SymPy’s manual integration demonstrates a structured rule tree with substeps that attempts to mirror hand work [R22].
- Google Mathsteps demonstrates a useful `before → rule/change → after` step record with nested substeps, but it is archived and limited in scope [R23].
- CLIPS demonstrates a portable production-rule architecture and was used in Andes, but the full CLIPS runtime is not presumed suitable for the Nspire until benchmarked [R20, R24].
- Giac has direct calculator provenance and a TI-Nspire port under KhiCAS, but its build, integration boundary, memory behavior, and license MUST be proven in M0 [R15–R18].
- SI/BIPM and UCUM provide the reference semantics for units, dimensions, commensurability, and machine-readable representation [R26–R28].

### 2.3 Repository source-of-truth order

When sources disagree, use this order:

1. executable tests and checked-in schemas;
2. approved Architecture Decision Records (ADRs);
3. this specification and its normative requirements;
4. the active milestone/task brief;
5. derived corpus manifests tied to external corpus IDs;
6. primary technical references;
7. comments and informal notes.

A test that contradicts an approved requirement is not automatically correct. Stop, identify the conflict, and update either the requirement/ADR or the test deliberately.

---

## 3. Product definition

### 3.1 Required output

For a supported problem, the engine MUST produce a structured derivation that can be rendered at multiple levels of detail:

```text
Problem model
  → known quantities and requested quantity
  → assumptions and coordinate/reference-frame choice
  → applicable physical principles
  → selected strategy
  → governing equations and constraints
  → symbolic rearrangement/substitution/elimination
  → exact result
  → numerical evaluation
  → unit reduction
  → final rounding/significant figures
  → direction, sign, or physical interpretation
```

Not every problem needs every category. The renderer MUST omit empty boilerplate.

### 3.2 Synthetic reference behavior

Given a structured model for a block sliding down a rough incline, the system should be able to create a derivation of this form:

1. Choose axes: `+x` down the incline, `+y` normal to the surface.
2. Identify forces: weight, normal force, kinetic friction.
3. Resolve weight into the selected basis.
4. Apply the normal-contact constraint: `a_y = 0`.
5. Apply Newton’s second law in `y`, derive `N = m g cos(theta)`.
6. Apply kinetic friction, derive `f_k = mu_k N`.
7. Apply Newton’s second law in `x`, derive `m a = m g sin(theta) - f_k`.
8. Substitute the normal-force and friction relations.
9. Divide by nonzero mass while recording `m != 0`.
10. Obtain the exact result `a = g(sin(theta) - mu_k cos(theta))`.
11. Substitute values only after the symbolic result exists.
12. Evaluate units, precision, sign, and direction.

Every displayed sentence MUST be generated from a rule that was actually applied. The engine MUST NOT ask a language model to invent this sequence after solving.

### 3.3 Definition of support

A problem archetype is **supported** only when all of the following hold:

- the structured problem can be represented without hidden data;
- the planner selects a physically valid strategy;
- every applied law’s preconditions are satisfied;
- the equation set is sufficient for the target quantity;
- the algebra/calculus engine emits valid, ordered steps;
- side conditions and branch restrictions are tracked;
- dimensions and units validate;
- the final result is correct;
- the derivation is readable at Nspire screen size;
- host acceptance tests pass; and
- designated Firebird and physical-device smoke tests pass.

Knowing a formula is not support.

---

## 4. Explicit non-goals and scope boundaries

The following are not part of the first production release unless an ADR changes the scope:

- unrestricted natural-language understanding of arbitrary textbook prose on-device;
- general diagram computer vision on-device;
- an LLM or neural model in the core solve path;
- cloud APIs or external CAS calls;
- theorem proving over arbitrary advanced mathematics;
- arbitrary user code execution through Giac;
- numerical simulation as a substitute for a derivation;
- a complete tutor/student model, classroom management system, or Bayesian learner model;
- automatic reproduction of copyrighted textbook solutions; and
- support claims for all 44 chapters in the first milestone.

The production ingestion path SHOULD be a guided structured editor and/or authored problem package. A future desktop authoring tool MAY parse prose or diagrams into the same structured model, but the deterministic solver must not depend on it.

---

## 5. Target-device constraints

The design MUST treat the TI-Nspire CX II as the baseline, not an afterthought.

Texas Instruments lists a 396 MHz processor, 64 MB operating memory, 90+ MB storage, and a 320×240 16-bit-color display [R8, R9]. “64 MB operating memory” is total device memory, not an application allocation guarantee. All usable-memory budgets MUST therefore be measured on actual target hardware.

The current ndl toolchain builds native C/C++ applications for an ARM target and, at the time of this research, pins GCC 14.2.0 and Newlib while disabling threads, TLS, and shared libraries and configuring soft-float [R11–R13]. The architecture MUST consequently assume:

- one process and one solver thread;
- no runtime dynamic linking;
- no thread-local storage;
- no background worker that keeps the UI responsive;
- expensive floating-point operations relative to a desktop;
- constrained stack and heap;
- explicit cooperative cancellation/yield points;
- small display and keypad-first interaction; and
- no dependence on desktop filesystem or locale behavior.

### 5.1 C++ profile

The core MUST compile as C++20 with a restricted profile:

- exceptions disabled on target;
- RTTI disabled on target;
- no `std::thread`, futures, atomics requiring OS support, or TLS;
- no shared libraries or plug-in loading;
- no `std::filesystem` in the target core;
- no `std::regex` in the target core;
- no locale-dependent parsing or formatting;
- avoid iostreams in target code;
- no unbounded recursion;
- no function-local static requiring thread-safe dynamic initialization;
- no per-expression `new`/`delete`; and
- no hidden allocations in hot paths.

Use C++20 language features where they compile cleanly under the pinned toolchain, but prefer predictable data layout and generated tables over template-heavy metaprogramming.

### 5.2 Build targets

The repository MUST produce:

1. `nps_core` — platform-neutral solver library;
2. `nps_host` — host CLI/test harness;
3. `nps_nspire.tns` — ndl application;
4. `nps_kb_compiler` or equivalent host-only knowledge compiler;
5. `nps_corpus_audit` — host-only corpus analysis tooling; and
6. benchmark/smoke-test applications for device profiling.

The Nspire target SHOULD use CMake/Ninja with a checked-in toolchain file that invokes `nspire-g++`, `nspire-ld`, `genzehn`, and `make-prg`, matching the supported ndl packaging path [R13]. M0 MUST prove this from a clean environment.

### 5.3 Resource safety

Every potentially explosive operation MUST accept a `SolveBudget` containing at least:

```cpp
struct SolveBudget
{
    std::uint32_t MaxExpressionNodes;
    std::uint32_t MaxRuleApplications;
    std::uint32_t MaxPlannerStates;
    std::uint32_t MaxDerivationSteps;
    std::uint16_t MaxRecursionDepth;
    std::uint16_t MaxBranchCount;
    std::uint32_t DeadlineTicks;
};
```

The exact defaults are provisional until M0/M1 benchmarks. Reaching a budget MUST produce a typed, recoverable result such as `eSearchBudgetExceeded`, not a hang or silent truncation.

Long operations MUST contain cooperative interruption checks. The UI MUST be able to cancel without corrupting the solver session.

---

## 6. Licensing and corpus-content gates

### 6.1 Giac

Giac/Xcas is published under GPLv3 and advertises a commercial dual-license route [R16]. Linking and distributing Giac is therefore an architectural and business decision, not a detail.

M0 MUST create `docs/adr/ADR-0001-cas-and-license.md` that records one approved path:

- distribute the combined project under GPL-compatible terms and meet source/notice obligations;
- obtain an appropriate commercial Giac license; or
- replace Giac with another backend after a measured capability/size comparison.

Do not provide legal conclusions beyond the sources. Mark this as requiring owner/counsel approval.

### 6.2 Textbook corpus

The EPUB is a private requirements and evaluation source. The repository MUST NOT contain:

- copied problem statements;
- copied worked solutions;
- copied figures or answer pages;
- large extracted text fragments; or
- an encoded mapping from exact problem text to answers.

The repository MAY contain:

- chapter/section/sample identifiers;
- derived skill and archetype labels;
- independently authored synthetic or isomorphic problems;
- hashes for locating external source entries;
- abstract diagram topology;
- expected laws and output types; and
- aggregate coverage statistics.

The corpus extractor MUST have a “metadata-only” mode suitable for committed output.

---

## 7. Required architecture and dependency direction

Use this logical architecture:

```text
┌───────────────────────────────────────────────────────────────┐
│                        Presentation                            │
│ Nspire UI · Host CLI · Step renderer · 2D math layout         │
└───────────────────────────────┬───────────────────────────────┘
                                │ read-only views
┌───────────────────────────────▼───────────────────────────────┐
│                    Derivation / Explanation                    │
│ solution DAG · step grouping · explanation templates          │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│                      Physics planning                          │
│ problem model · knowledge rules · strategy search · equations │
└───────────────────────────────┬───────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────┐
│                 Symbolic step transformation                   │
│ tactics · rewrite rules · conditions · candidate checking      │
└───────────────┬────────────────────────────────┬──────────────┘
                │                                │
┌───────────────▼──────────────┐  ┌──────────────▼──────────────┐
│ Project-owned semantic AST   │  │ CAS adapter                 │
│ units · frames · precision   │  │ Giac initially              │
└───────────────┬──────────────┘  └──────────────┬──────────────┘
                │                                │
┌───────────────▼────────────────────────────────▼──────────────┐
│ arenas · symbol interning · generated knowledge packs · limits│
└───────────────────────────────────────────────────────────────┘
```

Hard dependency rules:

- `core` MUST NOT include ndl headers.
- `core` MUST NOT include Giac headers.
- physics knowledge MUST NOT call UI code.
- UI MUST NOT infer physics or mutate derivation semantics.
- corpus tooling MUST be host-only.
- host-only parsing libraries MUST NOT leak into target code.
- target knowledge packs MUST be generated artifacts, not parsed YAML/JSON.

---

## 8. Three distinct representations

The project MUST keep these representations separate:

### 8.1 Semantic expression

Project-owned immutable expression DAG used for matching, derivation, units, and stable serialization.

### 8.2 Presentation expression

A render plan that preserves pedagogical order, grouping, chosen symbols, equation orientation, and explicitly displayed operations. It may intentionally show an unsimplified intermediate such as subtracting the same term from both sides.

### 8.3 CAS expression

A temporary normalized representation sent through the CAS adapter. Giac may reorder products or sums and may simplify more aggressively than desired. CAS output MUST NOT silently replace the student-facing representation.

For example, the semantic equality of `F = m a` and `F = a m` does not mean the renderer should discard the textbook convention `F = m a`.

---

## 9. Expression kernel

### 9.1 Node model

Use immutable, arena-owned nodes addressed by fixed-width IDs. A suitable starting point is:

```cpp
using ExprId = std::uint32_t;
using SymbolId = std::uint32_t;
using TypeId = std::uint16_t;

enum class ExprKind : std::uint8_t
{
    eInvalid,
    eInteger,
    eRational,
    eDecimalLiteral,
    eSymbol,
    ePhysicalConstant,
    eSum,
    eProduct,
    ePower,
    eFunction,
    eEquality,
    eInequality,
    eVector,
    eMatrix,
    eDerivative,
    eIntegral,
    ePiecewise
};

struct ExprNode
{
    std::uint32_t ChildOffset;
    std::uint32_t Payload;
    std::uint32_t StructuralHash;
    TypeId Type;
    ExprKind Kind;
    std::uint8_t ChildCount;
};
```

This is illustrative, not a mandate for exact field order. The final layout MUST be benchmarked for target alignment, size, and traversal cost.

### 9.2 Required properties

- immutable nodes;
- one session-owned arena;
- child IDs stored in a compact contiguous pool;
- interned symbols and explanation strings;
- deterministic structural hashing;
- optional hash-consing after measurement;
- explicit node-count limits;
- no raw owning pointers in serialized structures;
- stable opcodes/IDs with schema versioning;
- type/shape metadata for scalar, vector, matrix, Boolean, and equation forms;
- real/complex domain information; and
- source-span or origin metadata in host builds, removable from target builds.

### 9.3 Canonicalization

Separate semantic equivalence from display canonicalization.

The internal matcher MAY normalize associative/commutative forms, but the derivation record MUST preserve the exact before/after presentation forms. Do not use one global `simplify()` policy for every context. Define named normalization levels, such as:

- `ePreserveTeachingForm`;
- `eCollectNumericFactors`;
- `eCombineLikeTerms`;
- `eRationalCanonical`;
- `eCasCanonical`; and
- `eNumericEvaluation`.

---

## 10. Core physical data model

The solver MUST represent the physical system explicitly.

```cpp
using EntityId = std::uint16_t;
using QuantityId = std::uint16_t;
using FrameId = std::uint16_t;
using FactId = std::uint32_t;
using GoalId = std::uint16_t;

struct QuantityRef
{
    QuantityId Kind;
    EntityId Subject;
    FrameId Frame;
    std::uint16_t Qualifier;
};

struct KnownValue
{
    QuantityRef Quantity;
    ExprId Value;
    UnitId Unit;
    PrecisionId Precision;
};
```

The model MUST support:

- entities: particle, rigid body, fluid region, field source, circuit node/branch/component, optical element, wave source, thermodynamic system, nucleus/particle, and abstract system;
- relations: contact, attachment, connection, containment, ordering, coincidence, parallel/perpendicular, before/after state, same potential, same speed, and conservation boundary;
- frames and bases;
- knowns, unknowns, requested goals, and intermediate quantities;
- constraints and boundary conditions;
- assumptions and approximations;
- event/state phases for multi-stage problems; and
- diagram topology independent of pixels.

A quantity MUST be semantically typed. Two length-valued symbols are not interchangeable merely because their dimensions match: radius, displacement, wavelength, focal length, and path difference have different roles.

---

## 11. Coordinate frames and vectors

Vectors MUST be first-class and frame-aware.

### 11.1 Canonical representation

Internally use components plus basis/frame identity:

```cpp
struct VectorExpr
{
    ExprId X;
    ExprId Y;
    ExprId Z;
    FrameId Frame;
    std::uint8_t DimensionCount;
};
```

Render standard Cartesian unit-vector form when appropriate:

```text
A = A_x i-hat + A_y j-hat + A_z k-hat
```

### 11.2 Required operations

- addition and subtraction in compatible frames;
- magnitude and normalization;
- dot and cross product;
- scalar projection and vector projection;
- magnitude/angle to components;
- components to magnitude/direction;
- basis transformations;
- relative-position and displacement vectors;
- differentiation/integration of vector components;
- force, velocity, acceleration, momentum, angular momentum, torque, field, and wave-vector semantics; and
- 2D and 3D rendering.

An implicit frame conversion MUST NOT occur. Either prove the frames are identical or insert an explicit `TransformVectorBasis` derivation step.

### 11.3 Coordinate strategy

Coordinate selection is a planner decision and MUST be recorded. The cost model SHOULD prefer axes that reduce equation complexity, while allowing conventional textbook axes and alternative valid choices.

---

## 12. Units, dimensions, and quantity semantics

Use BIPM SI as the authoritative physical unit basis and UCUM as design guidance for machine-readable syntax [R26, R27].

### 12.1 Dimensions

Represent the seven SI base dimensions with rational exponents:

```cpp
struct RationalExponent
{
    std::int16_t Numerator;
    std::uint16_t Denominator;
};

struct Dimension
{
    RationalExponent Length;
    RationalExponent Mass;
    RationalExponent Time;
    RationalExponent Current;
    RationalExponent Temperature;
    RationalExponent Amount;
    RationalExponent LuminousIntensity;
};
```

Angle and solid angle are dimensionless in SI but MUST retain semantic tags so the solver does not confuse a raw ratio with an angle.

### 12.2 Units

A unit record MUST contain:

- dimension;
- exact scale to a canonical unit when possible;
- offset/affine semantics where applicable;
- display symbol and aliases;
- prefix policy;
- exactness of conversion factor;
- quantity-kind restrictions where needed; and
- named-derived-unit decomposition.

Absolute temperature and temperature differences MUST be distinguished. Affine units MUST NOT be multiplied or exponentiated as ordinary multiplicative units without conversion to an absolute scale.

### 12.3 Equation validation

Every equation with known dimensions MUST be checked for commensurability. Addition/subtraction requires compatible dimensions and quantity semantics. Multiplication/division/powers produce derived dimensions. A dimension error MUST identify the conflicting subexpressions and the law/rule that introduced them.

### 12.4 Unit steps

Unit conversion and reduction are derivation steps, for example:

```text
kg·m/s² → N
J/s → W
cm → 10⁻² m
```

The renderer SHOULD display the unit work at standard detail when it teaches a relevant concept and collapse routine conversions when it does not.

---

## 13. Exact numbers, measured values, uncertainty, and significant figures

### 13.1 Number categories

Distinguish:

- exact integer/rational;
- exact defined constant;
- symbolic constant;
- measured decimal literal;
- approximate decimal result; and
- value with explicit uncertainty.

Do not convert all input immediately to binary `double`.

### 13.2 Precision metadata

```cpp
struct PrecisionInfo
{
    PrecisionKind Kind;
    std::uint16_t SignificantDigits;
    std::int16_t LastSignificantDecimalPlace;
    ExprId ExplicitUncertainty;
};
```

Textbook significant-figure policies SHOULD be configurable per problem/domain. Preserve full exact or high-precision intermediates and round only for the reported result, consistent with NIST guidance against repeated intermediate rounding [R29].

### 13.3 Numerical backend

The CAS MAY supply arbitrary-precision arithmetic, but the adapter MUST expose explicit precision and rounding controls. The target MUST have a bounded fallback and a clear error when requested precision exceeds resources.

---

## 14. Declarative physics knowledge

Physics laws MUST be data, not ad hoc branches scattered through UI or solver code.

### 14.1 Rule classes

Support at least:

- definitions;
- empirical/constitutive laws;
- fundamental laws;
- conservation laws;
- geometric identities;
- kinematic constraints;
- contact/connection constraints;
- boundary and initial conditions;
- approximations;
- domain conventions; and
- strategy/meta-rules.

### 14.2 Authoring schema

Use a human-reviewable host-side schema, then compile it offline to compact tables. YAML below is illustrative:

```yaml
id: mechanics.newton_second_law.component
kind: physical_law
version: 1
bind:
  body: entity.rigid_or_particle
  axis: frame.axis
requires:
  - inertial_frame(axis.frame)
  - mass_defined(body)
  - force_inventory_complete(body)
produces:
  equation: sum(component(force, axis) for force in forces_on(body))
            = mass(body) * component(acceleration(body), axis)
principle: newton_second_law
assumptions:
  - classical_mechanics
explanation:
  standard: "Apply Newton's second law to {body} along the {axis} axis."
cost:
  pedagogical: 10
  symbolic: 4
  target_runtime: 3
verification:
  mode: construction_and_dimension_check
```

Kinetic friction must encode sliding as a precondition; static friction must encode an inequality and must not be replaced automatically by its maximum value except at impending slip.

### 14.3 Build-time compilation

The knowledge compiler MUST:

- validate unique stable IDs;
- type-check variables and predicates;
- validate unit dimensions of equation templates;
- detect missing explanation templates;
- detect direct dependency cycles where prohibited;
- build indexes by producible quantity/fact;
- emit deterministic compact C++/binary tables;
- produce a human-readable manifest; and
- embed a schema/version fingerprint.

The Nspire application MUST NOT parse YAML or JSON.

---

## 15. Solution planner

Use a hybrid of backward goal-directed search and forward fact closure.

### 15.1 State

A planner state should contain:

- unsatisfied goals;
- available facts and known quantities;
- active assumptions;
- selected model/strategy choices;
- generated equations;
- introduced unknowns;
- dependency edges;
- accumulated cost; and
- search-budget counters.

### 15.2 Search

For each requested quantity:

1. index candidate laws/definitions that can produce it;
2. unify entities, states, frames, and quantity roles;
3. check hard applicability predicates;
4. convert unmet preconditions into subgoals;
5. forward-chain cheap deterministic consequences;
6. reject dimensionally impossible or contradictory states;
7. estimate whether the generated equation system is determined;
8. score candidate strategies; and
9. continue a bounded deterministic best-first search.

### 15.3 Strategy points

Represent choices explicitly, as Andes did [R20]:

- separate bodies versus compound system;
- standard axes versus incline-aligned axes;
- Newtonian force route versus energy route;
- momentum/impulse route versus force/time integration;
- loop-current choices in circuits;
- Gaussian surface selection;
- direct integration versus symmetry argument;
- ray construction versus lens equation; and
- exact relation versus approved approximation.

On-device, generate the best-ranked strategy first. Generate alternatives lazily only when requested.

### 15.4 Cost model

A candidate plan’s cost SHOULD include:

- number of new unknowns;
- equation count and expected solve complexity;
- expression-tree complexity;
- number/strength of assumptions;
- number of coordinate transforms;
- numerical conditioning;
- pedagogical conventionality;
- estimated target runtime/memory; and
- derivation length.

Tie-breaking MUST be deterministic.

### 15.5 Plan output

The planner MUST produce a `PlanCertificate` containing selected rules, bindings, prerequisites, equations, assumptions, strategy-point choices, and dependency edges. The algebra solver consumes this certificate; it must not rediscover the physics.

---

## 16. Algebra and calculus step engine

Do not attempt to write a second full CAS. Build a pedagogical tactic engine that controls explicit transformations and uses the CAS behind a narrow adapter.

### 16.1 Step record

```cpp
using StepId = std::uint32_t;
using RuleId = std::uint16_t;
using ConditionId = std::uint32_t;

enum class VerificationStatus : std::uint8_t
{
    eUnchecked,
    eVerifiedByConstruction,
    eVerifiedByCas,
    eVerifiedNumerically,
    eConditional,
    eFailed
};

struct DerivationStep
{
    StepId Id;
    RuleId Rule;
    ExprId Before;
    ExprId After;
    std::uint32_t PremiseOffset;
    std::uint16_t PremiseCount;
    std::uint32_t ConditionOffset;
    std::uint16_t ConditionCount;
    ExplanationId Explanation;
    VerificationStatus Verification;
    DetailLevel DefaultDetail;
};
```

Some physics nodes produce facts/equations rather than transform one expression; represent those with a common derivation-node envelope rather than abusing null expressions.

### 16.2 Algebra rule descriptor

Each rule MUST define:

- stable ID and version;
- typed pattern/preconditions;
- transformation constructor;
- reversible/one-way classification;
- side-condition generator;
- verification strategy;
- explanation templates;
- complexity delta;
- aggregation behavior; and
- tests.

Example:

```yaml
id: algebra.divide_both_sides
match: equation(lhs, rhs)
parameter: divisor
requires:
  - scalar(divisor)
produces:
  - equation(lhs / divisor, rhs / divisor)
conditions:
  - nonzero(divisor)
logical_relation: equivalent_if_conditions_hold
explanation:
  standard: "Divide both sides by {divisor}."
```

### 16.3 Tactics

Implement named tactics that sequence rules according to a pedagogical goal:

- normalize numeric literals;
- simplify units;
- combine like terms;
- isolate a term;
- isolate a variable in a linear equation;
- substitute a known relation;
- eliminate a variable from a linear system;
- solve a quadratic with explicit discriminant/candidate checking;
- solve rational equations with denominator conditions;
- apply exponent/log rules with domain restrictions;
- resolve vectors into components;
- differentiate by rule;
- integrate by rule; and
- evaluate definite integrals with bounds.

SymPy’s rule-tree approach is a useful model for calculus [R22]. Mathsteps’ structured before/change/after/substeps model is useful for algebra [R23]. Neither should be embedded as-is.

### 16.4 Step granularity

Store microsteps, but group them into pedagogical steps. Support at least:

- `Compact` — major physics and final algebra transitions;
- `Standard` — expected student work;
- `Detailed` — expanded arithmetic, unit, and calculus substeps.

Step grouping MUST be rule-driven and deterministic, not prose summarization.

---

## 17. CAS adapter

### 17.1 Boundary

Define a backend-neutral interface; do not pass CAS-native objects through the project:

```cpp
struct CasRequestContext
{
    AssumptionSetView Assumptions;
    SolveBudget* Budget;
    CancellationToken* Cancellation;
    std::uint32_t PrecisionBits;
};

class CasBackend
{
public:
    virtual ~CasBackend() = default;

    virtual Result<ExprId, ErrorCode> Simplify(
        ExprId expression,
        SimplifyPolicy policy,
        CasRequestContext& context) noexcept = 0;

    virtual Result<SolutionSet, ErrorCode> Solve(
        EquationSystemView equations,
        SymbolSpan targets,
        CasRequestContext& context) noexcept = 0;

    virtual Result<EquivalenceResult, ErrorCode> CheckEquivalence(
        ExprId left,
        ExprId right,
        CasRequestContext& context) noexcept = 0;
};
```

A function-table/static-polymorphism implementation MAY replace virtual dispatch for target builds; the semantic boundary is mandatory.

### 17.2 Safety

- Convert AST to CAS objects directly; do not build and parse arbitrary command strings.
- Whitelist functions/operators.
- Set CAS context explicitly per solve.
- Capture warnings/errors as typed data.
- Apply cancellation/timeout hooks where available.
- Guard recursion and large expression creation.
- Treat CAS crashes/hangs found in M0 as blockers with minimized reproducers.

### 17.3 CAS role

The CAS may:

- simplify a transformation proposed by a tactic;
- solve a generated equation system;
- differentiate/integrate when the step engine has selected the mathematical strategy;
- verify equivalence under assumptions;
- evaluate numerically; and
- provide exact arithmetic.

The CAS may not silently choose the physical law or fabricate a derivation trace.

---

## 18. Verification and proof obligations

Verification is not one Boolean `simplify(before - after) == 0`.

### 18.1 Verification relations

Distinguish:

- syntactic rewrite by construction;
- equality of scalar expressions;
- equivalence of equations under side conditions;
- forward implication only;
- reverse implication only;
- equality of solution sets;
- subset/superset relation of solution sets;
- dimensional validity;
- numerical residual check; and
- physical-law applicability.

### 18.2 Side conditions

Track conditions such as:

```text
m != 0
t != 0
r > 0
argument(log) > 0
radicand >= 0
reference frame is inertial
object remains in contact
rope is taut and inextensible
```

Conditions can be proven, assumed by the problem, split into cases, or left unresolved. The final solution MUST surface unresolved material conditions.

### 18.3 Non-equivalent transformations

- Squaring may introduce extraneous roots; mark it as forward implication and check candidates in the original equation.
- Dividing by a symbolic expression can lose a zero branch; either prove nonzero or branch.
- Multiplying/dividing inequalities requires sign knowledge and may reverse the inequality.
- Applying square roots/logarithms requires domain and branch handling.
- Inverse trigonometric transformations require range/periodicity handling.

### 18.4 Independent checking

A rule’s constructor is the primary source of provenance. The CAS acts as an independent checker where practical. For corpus acceptance, also use:

- exact substitution back into original equations;
- dimensional checks;
- numerical residuals at multiple admissible samples;
- differential testing against a separate host oracle where legally/practically acceptable; and
- invariant/property tests.

Never label a step `Verified` merely because the final answer matches an answer key.

---

## 19. Derivation graph and provenance

A complete solution is a DAG, not merely a vector of formatted strings.

Each derivation node MUST record:

- stable rule/principle ID;
- inputs/premises;
- output facts or expressions;
- entity/frame/state bindings;
- assumptions and side conditions;
- verification relation and status;
- source category (`given`, `definition`, `law`, `constraint`, `algebra`, `calculus`, `unit`, `numeric`, `interpretation`);
- explanation-template ID;
- default display grouping; and
- optional alternative-strategy membership.

The graph MUST allow shared subderivations and multiple strategy paths. The renderer selects a linear topological presentation without destroying the graph.

Provenance output SHOULD support a developer mode like:

```text
Step 12
Rule: mechanics.newton_second_law.component@1
Premises: 2, 4, 7, 9
Bindings: body=block, axis=incline_x
Conditions: inertial(frame_1), m>0
Verification: construction + dimensions + CAS
```

---

## 20. Diagrams and structured scenes

The EPUB contains 1,782 images with alt attributes; many physics problems depend on geometry or topology. Image understanding is separate from solving.

### 20.1 Scene model

Define a non-pixel scene graph supporting:

- bodies, particles, surfaces, points, lines, rays, axes, regions;
- vectors and force arrows;
- distances, angles, radii, heights, paths;
- ropes, pulleys, contacts, joints;
- circuit nodes, branches, sources, resistors, capacitors, inductors;
- charges and field-source geometry;
- mirrors, lenses, interfaces, apertures, slits;
- before/after states; and
- graph/plot data when slope or area is part of the problem.

### 20.2 Input modes

Prioritize:

1. structured problem templates;
2. calculator UI for knowns/unknowns/entities/relations;
3. authored compact problem packages generated on a host;
4. constrained grammar parsing as a later enhancement; and
5. general prose/vision ingestion only as a separate optional host tool.

The solver MUST receive explicit geometry/topology. It MUST NOT infer hidden diagram facts from a bitmap at runtime.

### 20.3 Free-body diagrams

Mechanics packs MUST generate an internal force inventory for each selected body/system. Rendering an FBD is a view over that model. A free-body diagram must not be decorative; its forces must be the same force records used to build component equations.

---

## 21. Nspire user interface and math layout

### 21.1 Screen model

Design for 320×240 pixels and keypad/touchpad navigation [R8]. Do not port a desktop pane layout.

Recommended solve view:

```text
┌────────────────────────────────────┐
│ Problem / target             [1/4] │
├────────────────────────────────────┤
│ PRINCIPLE                          │
│ Apply Newton's second law along x. │
│                                    │
│   ΣF_x = m a_x                    │
│                                    │
│ ▼ Details    ◀ Prev      Next ▶    │
└────────────────────────────────────┘
```

Views SHOULD include:

- problem/model;
- plan/key ideas;
- equations;
- algebra/calculus;
- units/numerical work;
- final result; and
- optional provenance/developer view.

### 21.2 Rendering

Do not require a LaTeX engine on-device. Build a compact math-layout tree supporting:

- baselines and glyph runs;
- fractions;
- superscripts/subscripts;
- roots;
- parentheses with scalable variants;
- sums/integrals;
- vectors and hats/arrows;
- matrices/systems;
- line breaking at operator boundaries; and
- horizontal scrolling as a fallback.

Layout MUST be measured against actual Nspire fonts/framebuffer APIs during M0/M1. Cache layout for visible steps only.

### 21.3 Interaction

- one-step-at-a-time default;
- collapse/expand substeps;
- deterministic back/forward navigation;
- visible progress and cancellation during long solves;
- no loss of work if rendering fails;
- errors identify missing data/unsupported capability rather than “cannot solve”; and
- final answers display units and direction clearly.

---

## 22. Knowledge packs and feature partitioning

The full 44-chapter scope is too broad to load and search indiscriminately on a 64 MB device. Compile domain packs with explicit dependencies:

```text
foundation
  ├─ algebra-basic
  ├─ units-si
  ├─ vectors-cartesian
  └─ calculus-basic

mechanics-kinematics
mechanics-forces
mechanics-energy-momentum
mechanics-rotation-fluids-gravity
waves-thermodynamics
electricity-circuits
magnetism-em
optics
modern-relativity-quantum-nuclear
```

The application SHOULD load or activate only the relevant pack/index. Shared immutable tables MAY remain resident. Pack boundaries, binary sizes, and peak memory MUST be measured.

Do not copy χCAS’s exact feature cuts, but use its existence as evidence that deliberate calculator-specific trimming is normal [R18].

---

## 23. Corpus engineering and evaluation

### 23.1 Reproducible audit

Create a host-only extractor that records:

- EPUB hash and metadata;
- chapter/section IDs and titles;
- sample/checkpoint/problem identifiers;
- image references and alt-text presence;
- equation and table markers;
- answer-key availability;
- problem-part structure when reliably parsed; and
- parser confidence/warnings.

Never commit extracted prose by default.

### 23.2 Archetype manifest

For each derived archetype, record:

```yaml
id: mechanics.kinematics.constant_acceleration.catch_up
corpus_evidence:
  - external_id: FOP-C02-SP04
required_entities: [vehicle_a, vehicle_b]
required_quantities: [position, velocity, acceleration, time]
required_strategies: [piecewise_motion, equal_position_event]
required_math: [linear_equation, quadratic_equation, substitution]
required_units: [length, time, velocity, acceleration]
diagram_semantics: none
expected_result_form: numeric_with_units
status: planned
```

Do not store the original prompt or answer.

### 23.3 Development and holdout policy

Before implementing a domain pack, deterministically assign corpus IDs to:

- **analysis/development** — used to identify laws and archetypes;
- **regression** — encoded as independently authored isomorphic tests; and
- **holdout** — not used to write the specific rules, then evaluated after implementation.

Because agents can access the full corpus, a strict ML-style blind test is not guaranteed. Preserve integrity by forbidding exact-text matching and requiring generated parameter variants and metamorphic tests.

### 23.4 Coverage

Report at least:

- sample-problem archetypes supported/identified;
- end-of-chapter archetypes supported/identified;
- physics laws implemented/required;
- algebra/calculus tactics implemented/required;
- diagram-scene primitives implemented/required;
- host pass rate;
- device smoke pass rate; and
- unsupported-reason histogram.

Coverage MUST be by archetype and capability, not formula count.

---

## 24. Testing strategy

### 24.1 Test layers

1. **AST tests** — construction, hashing, interning, serialization, limits.
2. **Unit tests** — commensurability, conversions, affine units, derived units.
3. **Algebra rule tests** — before/rule/after, conditions, reversibility.
4. **Calculus rule tests** — rule tree and candidate verification.
5. **Physics-law tests** — applicability and equation generation.
6. **Planner tests** — strategy selection, dependencies, cycle/budget behavior.
7. **End-to-end synthetic archetype tests** — complete derivations.
8. **Corpus-linked tests** — external IDs and derived capabilities only.
9. **Differential tests** — Giac plus an independent host oracle where appropriate.
10. **Property/metamorphic tests** — invariants under equivalent transformations.
11. **Fuzz tests** — parsers, serializers, rewrite matching, malformed packs.
12. **Target tests** — Firebird then real Nspire hardware.

### 24.2 Required properties

Examples:

- changing compatible display units does not change the physical result;
- rotating a fully represented vector problem and target frame produces a correspondingly rotated solution;
- rescaling all lengths/times by consistent factors preserves dimensionless results;
- every reported candidate satisfies the original equations and conditions;
- every rendered step maps to an existing derivation node;
- every derivation node’s premise IDs exist and precede it topologically;
- no physics rule fires after a precondition is negated;
- standard and detailed views produce the same final semantic result;
- serialization round trips deterministically; and
- exceeding a budget returns a typed error without corrupting state.

### 24.3 Golden tests

Golden tests SHOULD assert stable rule/principle IDs and semantic before/after expressions, not fragile English wrapping or exact whitespace. UI screenshot goldens MAY be used separately at fixed framebuffer dimensions.

### 24.4 Device evidence

For every milestone, capture:

- `.elf` and `.tns` size;
- static section sizes;
- peak measured heap/arena usage;
- maximum observed stack where measurable;
- solve time by archetype;
- rendered frame latency;
- cancellation behavior; and
- physical device/OS/ndl versions.

Firebird is useful but is not a performance oracle for the physical calculator [R14].

---

## 25. Reliability, determinism, and error handling

All public core functions MUST return typed outcomes. Define errors such as:

```text
eInvalidProblemModel
eMissingKnownQuantity
eContradictoryGivens
eDimensionMismatch
eNoApplicablePhysicsModel
eUnderdeterminedEquationSystem
eOverdeterminedInconsistentSystem
eUnsupportedTransformation
eUnresolvedSideCondition
eExtraneousCandidatesRejected
eCasFailure
eCasTimeout
eSearchBudgetExceeded
eExpressionBudgetExceeded
eCancelled
eKnowledgePackVersionMismatch
eOutOfMemory
```

A failure result SHOULD include:

- phase;
- relevant rule/law/quantity IDs;
- missing or contradictory facts;
- budget counters;
- recoverability; and
- a user-safe explanation ID.

Do not catch a broad failure and return a plausible-looking result.

---

## 26. Security and robustness

Even though this is an offline calculator app:

- never evaluate arbitrary Giac command text;
- bound all parsed lengths/counts/depths;
- validate knowledge-pack checksums and versions;
- avoid integer overflow in offsets/sizes;
- validate all IDs before indexing;
- fuzz host parsers/serializers;
- avoid undefined behavior and aliasing violations;
- use host ASan/UBSan builds;
- ensure cancellation leaves arenas/session in a valid state; and
- keep imported problem packages declarative and non-executable.

---

## 27. Domain roadmap derived from the 44-chapter corpus

Do not attempt this in one release. Use capability dependency order.

### Stage A — Foundation and translational mechanics

Chapters 1–4:

- unit conversion, order of magnitude, density, significant figures;
- 1D position/velocity/acceleration and graph interpretation;
- vectors, unit vectors, components, dot/cross products;
- projectile, circular, and relative motion.

### Stage B — Classical mechanics

Chapters 5–14:

- Newtonian force models, friction, drag, circular force problems;
- work/energy/power and variable-force integration;
- potential energy and conservation;
- center of mass, impulse, momentum, collision;
- rotation, torque, rolling, angular momentum;
- equilibrium, elasticity, gravitation, fluids.

### Stage C — Oscillations, waves, and thermodynamics

Chapters 15–20:

- SHM and energy;
- traveling/standing waves, sound, interference, Doppler;
- heat, first law, kinetic theory, entropy and engines.

### Stage D — Electricity and magnetism

Chapters 21–33:

- Coulomb force and fields;
- Gauss symmetry strategies;
- potential/capacitance;
- current, resistance, multiloop circuits;
- magnetic forces and fields;
- induction, inductance, LC/RLC/AC;
- Maxwell relations and electromagnetic waves.

### Stage E — Optics and modern physics

Chapters 34–44:

- image formation, interference, diffraction;
- relativity;
- photons and matter waves;
- quantum states and atoms;
- solids, nuclear physics, fission/fusion, particle physics.

Each stage MUST begin with a corpus-derived archetype inventory and end with a measured support report.

---

## 28. Repository structure

Create or converge on:

```text
/
├── AGENTS.md
├── MASTER_AGENT_PROMPT.md
├── CMakeLists.txt
├── cmake/toolchains/ndl-arm926ej-s.cmake
├── docs/
│   ├── index.md
│   ├── ARCHITECTURE_SPEC.md
│   ├── CORPUS_AUDIT.md
│   ├── adr/
│   ├── plans/
│   └── coverage/
├── tasks/
├── include/nps/
│   ├── core/
│   ├── physics/
│   ├── steps/
│   ├── units/
│   └── cas/
├── src/
│   ├── core/
│   ├── physics/
│   ├── steps/
│   ├── units/
│   ├── cas/giac/
│   ├── platform/host/
│   └── platform/nspire/
├── kb/
│   ├── source/
│   ├── generated/
│   └── schemas/
├── tools/
│   ├── corpus_audit/
│   └── kb_compiler/
├── tests/
│   ├── unit/
│   ├── property/
│   ├── golden/
│   ├── corpus/
│   └── target/
└── benchmarks/
```

Directory-local `AGENTS.md` files MAY add narrowly scoped rules, following the layered instruction model supported by Codex/GitHub [R1, R5]. Do not duplicate the entire master specification in every directory.

---

## 29. Agent-operating protocol

The project documentation is part of the implementation. Official coding-agent guidance consistently emphasizes persistent repository instructions, configured environments, reliable tests, and exact validation commands [R1–R5]. Follow this protocol:

1. Read root `AGENTS.md`, this document, the architecture/corpus docs, active task, and relevant ADRs.
2. Inspect the current repository before proposing changes.
3. For work longer than a small patch, create/update an execution plan in `docs/plans/` [R3].
4. State the bounded scope, assumptions, and acceptance criteria.
5. Implement the smallest vertical change that satisfies the active task.
6. Add tests and instrumentation with the code.
7. Run exact build/test/lint/size commands from a clean or reproducible environment.
8. Update manifests/ADRs/docs when behavior or architecture changes.
9. Review the diff for unsupported claims, hidden allocations, desktop leakage, and missing conditions.
10. Report evidence; do not merely say “works.”

Do not ask broad design questions that the specification already resolves. Where a genuinely unresolved tradeoff exists, create an ADR with options, measurements, recommendation, and consequences.

---

## 30. Milestones

### M0 — Feasibility and architecture gates

Complete `tasks/M0_FEASIBILITY.md`. This is blocking. It must prove the toolchain, basic Nspire app, Giac integration candidate, licensing path, host/target boundary, corpus metadata pipeline, UI rendering spike, and measurement harness.

### M1 — Foundation vertical slice

Complete `tasks/M1_VERTICAL_SLICE.md`. It must solve multiple structurally different early-chapter problems end to end, with real structured steps, units, vectors, and target rendering.

### M2 — Kinematics and force mechanics

Add problem families for Chapters 2–6, including multi-body constraints and free-body models. Require planner alternatives and linear systems.

### M3 — Energy, momentum, rotation, gravity, fluids

Chapters 7–14. Add variable-force calculus, conservation strategies, torque, and state transitions.

### M4 — Waves, thermodynamics, E&M

Chapters 15–33 in domain-sized increments. Do not combine all into one agent task.

### M5 — Optics and modern physics

Chapters 34–44, after the foundation supports piecewise/complex quantities, boundary conditions, probability, and quantum/nuclear notation.

---

## 31. Quality gates for every milestone

A milestone cannot close unless:

- all normative requirements in its task brief are met or explicitly waived by ADR;
- host debug and release builds pass;
- tests pass with no ignored failures;
- target cross-build succeeds;
- designated emulator/device tests pass;
- binary/section/heap/stack/runtime measurements are recorded;
- no unsupported corpus text is committed;
- no user-facing step lacks provenance;
- no new physics rule lacks applicability tests;
- no non-equivalent algebra operation is labeled equivalent;
- coverage manifests are updated; and
- known limitations are explicit.

---

## 32. Required agent deliverable format

At the end of each task, return:

```text
1. Scope completed
2. Repository state inspected
3. Architecture/ADR decisions
4. Files changed
5. Behavior implemented
6. Tests and exact command results
7. Host benchmarks
8. Nspire build, binary size, memory, runtime, and device evidence
9. Corpus/archetype coverage delta
10. Known limitations and unresolved risks
11. Next single blocking task
```

Do not claim measurements that were not taken.

---

## 33. Immediate instruction

Your first action is **not** to implement the entire solver.

1. Read `tasks/M0_FEASIBILITY.md`.
2. Audit the repository and environment.
3. Create the M0 execution plan.
4. Complete only M0.
5. Stop after producing M0 artifacts, measurements, ADRs, and the recommended go/no-go path.

Do not begin broad rule authoring or chapter implementation until M0 passes.

---

## 34. References

See `REFERENCES.md`. Citation labels `[R1]` through `[R29]` in this document refer to that file.
