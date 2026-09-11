# Corpus audit and requirements extraction

## 1. Source

Local file used for this audit:

```text
/mnt/data/FUNDAMENTALS-OF-PHYSICS-62-pages(1).epub
```

The EPUB package metadata identifies the title as `FUNDAMENTALS OF PHYSICS`. It does not expose reliable author/edition fields in the package metadata; this document does not infer them.

The source is private/copyrighted material. This audit records aggregate structure and derived capabilities; it is not a redistribution of the book.

## 2. Audit method

The EPUB was opened as a ZIP container. `META-INF/container.xml` points to `OEBPS/content.opf`; `OEBPS/nav.xhtml` maps content files to 44 numbered chapters plus appendices, answers, index, mathematical formulas, and license material. Chapter XHTML was parsed structurally with Beautiful Soup. Counts below are markup-derived and reproducible from the audit script.

Problem-paragraph detection remains heuristic. Do not use a raw problem count as a release metric until parser confidence is implemented.

## 3. Aggregate findings

| Item | Count |
|---|---:|
| Numbered chapters | 44 |
| Numbered instructional sections | 270 |
| Sample-problem headings | 282 |
| Checkpoint headings | 203 |
| Explicit proof/derivation headings | 23 |
| Images | 1782 |
| Images with nonempty alt text | 1782 |

The answer section is labeled for checkpoints and odd-numbered questions/problems.

## 4. Chapter inventory

| Ch. | Title | Sections | Samples | Checkpoints | Proof/derivation headings | Images |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Measurement | 3 | 2 | 0 | 0 | 18 |
| 2 | Motion Along a Straight Line | 7 | 6 | 5 | 0 | 45 |
| 3 | Vectors | 4 | 7 | 5 | 0 | 58 |
| 4 | Motion in Two and Three Dimensions | 7 | 8 | 5 | 1 | 81 |
| 5 | Force and Motion—I | 3 | 7 | 5 | 0 | 57 |
| 6 | Force and Motion—II | 3 | 6 | 2 | 0 | 49 |
| 7 | Kinetic Energy and Work | 6 | 9 | 3 | 0 | 47 |
| 8 | Potential Energy and Conservation of Energy | 5 | 6 | 5 | 1 | 60 |
| 9 | Center of Mass and Linear Momentum | 9 | 9 | 9 | 1 | 123 |
| 10 | Rotation | 8 | 11 | 7 | 3 | 43 |
| 11 | Rolling, Torque, and Angular Momentum | 9 | 6 | 7 | 1 | 32 |
| 12 | Equilibrium and Elasticity | 3 | 6 | 3 | 1 | 40 |
| 13 | Gravitation | 8 | 6 | 5 | 1 | 33 |
| 14 | Fluids | 7 | 7 | 4 | 1 | 38 |
| 15 | Oscillations | 6 | 6 | 6 | 0 | 52 |
| 16 | Waves—I | 7 | 6 | 6 | 1 | 37 |
| 17 | Waves—II | 8 | 7 | 4 | 2 | 37 |
| 18 | Temperature, Heat, and the First Law of Thermodynamics | 6 | 7 | 7 | 0 | 39 |
| 19 | The Kinetic Theory of Gases | 9 | 10 | 5 | 1 | 42 |
| 20 | Entropy and the Second Law of Thermodynamics | 4 | 6 | 5 | 0 | 40 |
| 21 | Coulomb’s Law | 3 | 4 | 4 | 0 | 40 |
| 22 | Electric Fields | 7 | 5 | 4 | 0 | 29 |
| 23 | Gauss’ Law | 6 | 7 | 4 | 0 | 36 |
| 24 | Electric Potential | 8 | 7 | 5 | 0 | 34 |
| 25 | Capacitance | 6 | 6 | 3 | 0 | 32 |
| 26 | Current and Resistance | 5 | 6 | 5 | 0 | 39 |
| 27 | Circuits | 4 | 5 | 5 | 1 | 49 |
| 28 | Magnetic Fields | 8 | 7 | 5 | 0 | 36 |
| 29 | Magnetic Fields Due to Currents | 5 | 4 | 3 | 2 | 32 |
| 30 | Induction and Inductance | 9 | 8 | 7 | 0 | 36 |
| 31 | Electromagnetic Oscillations and Alternating Current | 6 | 8 | 8 | 0 | 27 |
| 32 | Maxwell’s Equations; Magnetism of Matter | 8 | 4 | 6 | 0 | 40 |
| 33 | Electromagnetic Waves | 7 | 3 | 5 | 0 | 32 |
| 34 | Images | 6 | 4 | 4 | 1 | 34 |
| 35 | Interference | 5 | 7 | 5 | 1 | 44 |
| 36 | Diffraction | 7 | 6 | 5 | 3 | 38 |
| 37 | Relativity | 6 | 7 | 4 | 1 | 37 |
| 38 | Photons and Matter Waves | 9 | 6 | 5 | 0 | 35 |
| 39 | More About Matter Waves | 5 | 8 | 5 | 0 | 37 |
| 40 | All About Atoms | 7 | 4 | 3 | 0 | 20 |
| 41 | Conduction of Electricity in Solids | 3 | 7 | 2 | 0 | 18 |
| 42 | Nuclear Physics | 8 | 9 | 3 | 0 | 37 |
| 43 | Energy from the Nucleus | 6 | 5 | 2 | 0 | 18 |
| 44 | Quarks, Leptons, and the Big Bang | 4 | 7 | 3 | 0 | 31 |

## 5. Derived pedagogical requirements

The corpus uses recurring worked-example structures that separate principle/model selection from calculation. The implementation should preserve these conceptual phases:

- key idea or governing principle;
- physical system and assumptions;
- coordinate frame/sign convention;
- equations/constraints;
- symbolic calculation;
- numerical substitution;
- unit handling;
- interpretation/caution.

Notable archetype evidence from the audit includes:

- Chapter 2: staged motion/catch-up problems, graph slope/area, and derivatives/integrals.
- Chapter 3: magnitude-angle/components, unit-vector arithmetic, dot and cross products.
- Chapters 4–6: projectile/relative motion, free-body modeling, coordinate components, friction and circular motion.
- Chapter 7: work selected as a dot product when vectors are given in components, followed by interpretation of sign.
- Chapters 8–14: conservation strategies, systems, rotation/torque, equilibrium, gravity, and fluids.
- Chapter 27: multiloop circuits in which current directions may be chosen provisionally and negative solutions must be interpreted afterward.
- Later chapters: symmetry-based field laws, calculus over distributed sources, optics geometry, relativistic transformations, probability/quantum relations, and nuclear reaction bookkeeping.

## 6. Required manifest schema

Create `docs/coverage/corpus_manifest.json` with metadata only:

```json
{
  "corpus_sha256": "...",
  "schema_version": 1,
  "chapters": [
    {
      "chapter": 3,
      "title": "Vectors",
      "samples": [
        {
          "external_id": "FOP-C03-SP04",
          "title_hash": "...",
          "archetypes": ["vectors.add_cartesian_components"],
          "skills": ["unit_vector_notation", "component_addition"],
          "diagram_semantics": ["displacement_vectors"],
          "answer_available": true
        }
      ]
    }
  ]
}
```

Do not include prompt text, solution text, or images.

## 7. Archetype extraction protocol

For each sample and representative exercise:

1. assign a stable external ID;
2. identify entities and states;
3. identify givens and requested quantities;
4. identify diagram facts;
5. identify laws/definitions/constraints;
6. identify strategy points;
7. identify algebra/calculus/vector/unit tactics;
8. identify expected result type and interpretation;
9. identify likely failure categories;
10. create one or more independently worded synthetic/isomorphic tests.

Multiple source problems may map to one archetype. One source problem may exercise several archetypes.

## 8. Unsupported-reason taxonomy

Use a controlled enum:

```text
missing_problem_representation
missing_scene_primitive
missing_physics_law
failed_applicability_match
missing_strategy
underdetermined_model
missing_algebra_tactic
missing_calculus_tactic
missing_vector_operation
missing_unit_semantics
cas_limitation
resource_budget
renderer_limitation
corpus_parse_uncertain
```

## 9. Copyright-safe evaluation

The test repository should contain synthetic parameterized problems, not source wording. A private local benchmark runner MAY read the external EPUB and compare final answers where available, but it must not emit a distributable dataset containing the book’s text or figures.
