# Manual accuracy review

Reviewed 9 September 2026 against the current working source at HEAD 0961d6e, including its uncommitted changes. The reviewed handbook supersedes the original edition. Original files are retained.

## Reviewed edition

- [Word handbook](nps-beginners-handbook-reviewed.docx)
- [PDF handbook](nps-beginners-handbook-reviewed.pdf)
- [Searchable illustrated HTML handbook](nps-beginners-handbook-reviewed.html)

The handbook has 81 pages and 78 sections. The feature table on page 5 compares the standard non-CAS CX II, CX II CAS and CX II with StepCAS/Giac across 13 features. Pages 6 and 7 explain the additions to each TI setup. Page 8 gives a worked comparison. There are 25 numbered figure placements plus the cover, using 22 unique embedded images.

## Corrections and clarifications

| Topic | Correction | Evidence |
| --- | --- | --- |
| First session | Reset the calculation mode, progression and variable before the example. A saved mode or variable could otherwise change its behavior. | nps/lua/nps_v4.lua command handling |
| Algebra menu | Expand is a typed command. It is not an item in the current Algebra menu. | nps/lua/nps_v4.lua menu definition, nps/src/steps/command.cc |
| History reuse | Enter inserts the selected history expression at the saved cursor or replaces a selection. It does not necessarily replace the whole entry. | MathEditor:addString and addToEntry |
| History deletion | Both Del and Ctrl+Del delete the selected input/output pair when history has focus. | Editor event handlers and backSpaceHandler |
| Transformation wording | Do and Write describe verified transformations. Unverified records use different wording. Explanations depend on the available record. | Current detail rendering |
| Horner command | Horner evaluates a polynomial at a supplied value. It is not simply a nested-expression display switch. | khi-src/doc/en/cascmd_en.tex, Horner section |
| Binomial arguments | Teach the documented order binomial(n,k,p). The current menu shows n,p,k, whose inferred interchange is ambiguous at probability 0 or 1. | Giac reference and khi-src/src/moyal.cc |
| Distribution definitions | State exponential rate convention and geometric first-success trial counting explicitly. | Local Giac reference |
| Collection membership | Contains returns zero when absent, otherwise a position indicator. It is not simply a Boolean result. | Local Giac reference |
| Integer restrictions | Replace approximate bounds with exact native limits, including modular exponentiation. | nps/src/steps/integer.cc |
| Plotting and text reader | Make the ordinary-Giac mode switch explicit and identify the entry screen as the location of Actions. | Current UI command and menu handling |
| Evidence descriptions | Separate host results, retained emulator captures and unperformed physical-device qualification. | Host probes and capture provenance |

The accompanying manual-review-changes.json records the 20 replacement operations. Some are wording or evidence clarifications rather than separate defects. New comparison material is in manual-reviewed-source.json.

## Comparison findings

The non-CAS model already supplies numerical calculus, numerical equation solving, matrices, graphs and mathematical templates. The table does not claim these are new StepCAS features. See the [TI non-CAS reference](https://education.ti.com/en/guidebook/details/en/0988AF42E3224FCAA5270457333FE14A/ti-nspirereferenceguide-2).

The CAS model already supplies symbolic derivatives, integrals, limits, equation solving and unit conversion. StepCAS adds its own recorded derivation, hint, detail and guided-physics workflows for implemented families. The comparison concerns the standard built-in experience without additional teaching programs. It does not claim the TI hardware could never implement similar workflows. See the [TI CAS reference, version 6.4](https://education.ti.com/en/guidebook/details/en/0AF942A4AC0E47ABA20853691FA6712E/TI-NspireCXCASReferenceGuide).

No general claim that Giac is more powerful or faster than TI CAS was established. Native StepCAS explanations have narrower coverage than the full command libraries of either CAS. Limits and definite integrals remain ordinary Giac operations without native walkthroughs.

## Validation

- Current UI host smoke test: 3742 checks, zero failures. This runs Lua UI behavior with host substitutes for the calculator environment.
- Separate native example probes: 43 requests spanning algebra, calculus, integers, matrices, unsupported cases and kinematics. Supported native values were checked against the mathematical examples.
- Some native results reported an unavailable backend check. Their returned mathematical value is not evidence of successful Giac agreement.
- The host configuration could not execute the three matrix traces or evaluate Giac-only commands. Matrix and limit descriptions were checked against implementation and reference material, not established by a successful host calculation.
- PDF structure: all section headings present, all contents page references checked, no detected text outside page bounds, no near-empty overflow pages.
- DOCX structure: images embedded, alternative descriptions present and section bookmarks retained. The complete menu inventory still contains 177 entries.
- Fourteen changed or navigation pages were rendered and visually inspected, including the feature table, comparisons, first session, history, corrected menu descriptions and source list.

The review did not upload a package, change the calculator OS or perform a new physical-handheld test. Retained emulator captures remain labeled as historical illustrations. This is a reviewed development manual, not certification that every listed operation works on the currently installed handheld package.
