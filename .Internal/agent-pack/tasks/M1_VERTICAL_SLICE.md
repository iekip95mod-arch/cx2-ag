# Task M1 — Foundation vertical slice

## Preconditions

M0 has passed and its ADRs are approved. Use its measured budgets and pinned dependency revisions.

## Objective

Implement the first complete solver path from structured problem input through physics/math planning, recorded derivation, CAS verification, units, numerical reporting, and Nspire rendering.

## Required archetypes

Use independently worded synthetic problems linked to corpus archetypes. Implement at least:

1. chain-link unit conversion involving area or volume;
2. density relation solved for mass, volume, and density variants;
3. 1D constant-acceleration solve requiring symbolic isolation;
4. two-stage catch-up/equal-position kinematics;
5. vector addition in Cartesian unit-vector form;
6. dot-product work from component vectors;
7. one magnitude-angle/components conversion; and
8. a deliberate dimensional mismatch that is rejected with a useful explanation.

At least one solve must stay symbolic until the final result. At least one must interpret a negative component/direction.

## Required implementation

### Expression/derivation kernel

- immutable ID-addressed expression arena;
- typed symbols, exact rational, measured decimal;
- equations, sums/products/powers/functions, vectors;
- structured derivation DAG;
- stable rule IDs and explanation IDs;
- compact serialization or generated fixture format.

### Units/precision

- SI base dimensions;
- length, time, mass, velocity, acceleration, force, energy/work, density;
- exact prefixes/conversions;
- dimension checking;
- significant-figure metadata and final-only rounding.

### Rules/tactics

- substitute;
- simplify exact arithmetic;
- combine like terms;
- add/subtract both sides;
- multiply/divide both sides with nonzero conditions;
- isolate a linear variable;
- solve a small linear system if needed by catch-up form;
- vector component addition;
- dot product;
- magnitude and direction;
- unit conversion/reduction.

### Physics knowledge

- definitions of average/instantaneous quantities needed by the selected tests;
- constant-acceleration kinematics variants;
- density;
- displacement/vector component relations;
- work as a dot product;
- explicit applicability and quantity semantics.

### UI

- structured input fixture browser or minimal guided editor;
- step list/card with Compact/Standard/Detailed levels;
- rendered equations at 320×240;
- cancellation and typed error view;
- developer provenance view behind a debug option.

## Verification

- every algebra step has a rule and conditions;
- every physics equation has a law/definition provenance node;
- every candidate is substituted into original equations;
- all equations pass dimension checks;
- final unit and precision policy are tested;
- host property tests generate parameter variants;
- target smoke tests cover at least four archetypes.

## Prohibited shortcuts

- direct `giac.solve()` followed by fabricated step text;
- hard-coded problem IDs or answers;
- copied textbook wording;
- strings as the only step representation;
- unit suffixes with no dimension semantics;
- passing vector lists without frame/basis metadata.

## Exit evidence

Report:

- rule and archetype coverage;
- rule sequence for each reference solve;
- host test count/results;
- `.elf`/`.tns` sizes;
- peak memory and runtime per target solve;
- screenshots or framebuffer captures of representative steps;
- unsupported cases discovered; and
- next single milestone recommendation.
