# Architecture specification

## 1. Purpose

This document turns the master prompt into implementation invariants. It is deliberately narrower than a user-facing product description. Changes to a MUST-level invariant require an ADR and corresponding test/tooling update.

## 2. Quality-attribute priorities

In order:

1. **Correctness and provenance** — no ungrounded step or law application.
2. **On-device feasibility** — bounded memory, search, stack, and runtime.
3. **Determinism and reproducibility** — identical input/config/pack produces identical selected plan and semantic derivation.
4. **Pedagogical fidelity** — ordered work that resembles competent student reasoning.
5. **Extensibility** — new domains/rules without rewriting core planning.
6. **Developer/agent legibility** — explicit boundaries, schemas, commands, and tests.

These attributes conflict. Record tradeoffs rather than silently preferring convenience.

## 3. Component model

### 3.1 `core-expression`

Owns immutable expression DAGs, symbols, types, exact literals, traversal, structural hashing, and serialization. It knows no physics, UI, Giac, or ndl.

### 3.2 `units`

Owns dimensions, units, conversions, quantity-kind compatibility, precision metadata, and unit derivation steps.

### 3.3 `physical-model`

Owns entities, quantities, relations, frames, states/events, givens, constraints, assumptions, and goals.

### 3.4 `knowledge-runtime`

Loads generated compact knowledge packs. It provides typed rule indexes and matching; it does not parse authoring formats on-device.

### 3.5 `planner`

Converts goals into a plan certificate and solution hypergraph through bounded search. It chooses physics strategies, not algebra microsteps.

### 3.6 `step-engine`

Transforms equations/expressions through explicit tactics and records derivation nodes. It preserves teaching form.

### 3.7 `cas-adapter`

Converts between project AST and Giac, invokes whitelisted operations, captures diagnostics, enforces budgets/cancellation, and returns project-owned results.

### 3.8 `derivation`

Stores facts, equations, transformations, assumptions, dependencies, verification, explanation IDs, and alternative paths.

### 3.9 `renderer`

Maps derivation views to layout tokens and the 2D math layout tree. It cannot create new semantic steps.

### 3.10 `platform-host` / `platform-nspire`

Own filesystem, time/tick, input, framebuffer, logging, and target packaging. Core code depends only on narrow platform service interfaces.

## 4. Memory model

A solve session SHOULD use monotonic arenas:

```text
Session
├── expression node arena
├── child-index arena
├── symbol/value intern tables
├── fact/equation arena
├── planner-state arena or bounded pool
├── derivation-node arena
└── temporary CAS conversion arena
```

The session can be discarded as a unit. Reclaim temporary planner branches with checkpoints/rewinds or pools; do not rely on thousands of individual frees.

All arenas MUST report current, peak, capacity, and failed-allocation counts. The device UI MUST be able to abort before uncontrolled exhaustion.

## 5. Serialization

Use a versioned little-endian format with:

- magic;
- major/minor schema version;
- pack ID/version;
- feature flags;
- payload lengths checked before allocation;
- checksum/hash;
- fixed-width offsets, never native pointers;
- deterministic ordering; and
- bounds validation before use.

Generated C++ arrays are acceptable for M1. A separate binary pack format can follow once measured.

## 6. Knowledge matching

Compile authoring predicates into compact bytecode or table-driven operations. A possible instruction set:

```text
MATCH_ENTITY_KIND
MATCH_QUANTITY_KIND
MATCH_RELATION
MATCH_STATE
BIND_ENTITY
BIND_FRAME
REQUIRE_FACT
REQUIRE_ASSUMPTION
EMIT_SUBGOAL
EMIT_FACT
EMIT_EQUATION
EMIT_CONDITION
```

The interpreter MUST have an instruction budget and validate all operands.

## 7. Planner algorithm

Default recommendation:

- backward-chain from the requested quantity;
- forward-close cheap definitions/constraints after each state expansion;
- use indexed candidates keyed by output fact/quantity kind;
- use a deterministic min-heap for best-first search;
- memoize normalized `(goals, known facts, strategy choices)` signatures;
- prune dimensionally impossible states;
- prune states whose equation-variable rank cannot plausibly close within remaining candidates;
- defer alternate strategies after finding a satisfactory best path;
- retain a compact dependency graph rather than copying full state data.

Do not use unrestricted equality saturation on-device. A host research spike may compare an e-graph for local algebraic normalization, but it must prove bounded value over the tactic engine before adoption.

## 8. Equation-system analysis

Before invoking Giac, determine:

- unknown symbols and semantic quantity IDs;
- equation count and independence estimate;
- dimensions of each equation;
- state/event scope;
- constraints versus definitions;
- target symbols;
- candidate degeneracies and zero denominators.

The planner SHOULD avoid introducing equations that do not help close a target unless they are pedagogically required.

## 9. Rule correctness contract

Every rule implementation includes:

```text
Descriptor
Matcher tests
Positive applicability tests
Negative applicability tests
Transformation tests
Side-condition tests
Verification tests
Explanation-template tests
Target-size contribution report for generated tables
```

Physics rule tests must include a near-miss case. Example: kinetic-friction rule with contact but no sliding MUST NOT fire.

## 10. Display fidelity

Store semantic and presentation forms separately. A display node may annotate:

- preferred equation orientation;
- operand order;
- term grouping;
- highlighted changed region;
- terms canceled in this step;
- unit factor expansion;
- basis labels; and
- step-specific line-break hints.

These annotations cannot alter semantics.

## 11. Host versus target feature table

| Feature | Host | Nspire |
|---|---:|---:|
| Core solve and derivation | Required | Required |
| Giac adapter | Required | Required if M0 passes |
| YAML/JSON knowledge authoring | Required | Forbidden |
| EPUB audit/parser | Required | Forbidden |
| ASan/UBSan/fuzzing | Required in CI profiles | Unsupported |
| Full debug provenance | Required | Optional compact mode |
| Alternative-strategy enumeration | Full/bounded | Lazy/bounded |
| Screenshot/golden layout tests | Required | Smoke/manual capture |
| Network | Not needed | Forbidden |
| LLM | Optional development aid only | Forbidden |

## 12. Initial API seams

```cpp
Result<ProblemModel, ErrorCode> ValidateProblem(
    const ProblemInput& input,
    ValidationContext& context) noexcept;

Result<PlanCertificate, ErrorCode> BuildSolutionPlan(
    const ProblemModel& model,
    GoalId goal,
    PlannerContext& context) noexcept;

Result<DerivationGraph, ErrorCode> ExecutePlan(
    const ProblemModel& model,
    const PlanCertificate& plan,
    SolverContext& context) noexcept;

Result<RenderedStepView, ErrorCode> RenderStep(
    const DerivationGraph& graph,
    StepId step,
    DetailLevel level,
    RenderContext& context) noexcept;
```

The concrete types should be move-only/session-owned views where copying would be expensive.

## 13. First supported archetypes

M1 should include at least:

1. chain-link unit conversion with powers;
2. density solve for a non-default unknown;
3. 1D constant-acceleration solve requiring algebraic rearrangement;
4. two-stage/catch-up kinematics requiring an intermediate event equation;
5. 2D vector addition in Cartesian unit-vector form;
6. dot-product work from component vectors;
7. projectile or relative-motion component solve; and
8. one dimension-error rejection case.

At least one problem must remain symbolic until the last stage, and at least one must produce a physically meaningful negative sign/direction interpretation.

## 14. Architectural rejection criteria

Reject a proposal that:

- uses `giac::gen` as the entire domain model;
- stores explanations only as strings generated after solving;
- embeds raw textbook content;
- parses YAML/JSON on the target;
- assumes a desktop heap/stack/thread model;
- treats vectors as unframed lists;
- treats units as suffix text;
- uses `double` for all numbers immediately;
- performs unbounded rewrite search;
- silently drops conditions or branches;
- cannot be cross-compiled before physics implementation; or
- lacks measured target evidence.
