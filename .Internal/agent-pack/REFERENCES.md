# Research references

The dates and repository states below were checked on **2026-09-03**. Pin exact revisions in the project; do not rely on moving `master` branches.

## Coding-agent specification and repository guidance

- **R1 — OpenAI, “Custom instructions with AGENTS.md.”** Codex reads `AGENTS.md` before work and supports layered, directory-scoped guidance.  
  https://developers.openai.com/codex/agent-configuration/agents-md
- **R2 — OpenAI, “Harness engineering: leveraging Codex in an agent-first world.”** Recommends making repository knowledge the system of record, increasing agent legibility, and enforcing architecture with executable checks.  
  https://openai.com/index/harness-engineering/
- **R3 — OpenAI, “Using PLANS.md for multi-hour problem solving.”** Defines an execution-plan pattern for long-running, multi-stage engineering work.  
  https://developers.openai.com/cookbook/articles/codex_exec_plans
- **R4 — Anthropic, “How Claude remembers your project.”** Documents `CLAUDE.md`, scoped rules, and keeping persistent project instructions focused.  
  https://docs.anthropic.com/en/docs/claude-code/memory
- **R5 — GitHub, “Adding repository custom instructions for GitHub Copilot.”** Recommends repository instructions that explain how to understand, build, test, and validate the project.  
  https://docs.github.com/copilot/customizing-copilot/adding-custom-instructions-for-github-copilot
- **R6 — RFC 2119 and RFC 8174 / BCP 14.** Defines the normative meanings of `MUST`, `SHOULD`, and `MAY`, with RFC 8174 clarifying that the special meanings apply to uppercase forms.  
  https://www.rfc-editor.org/rfc/rfc2119  
  https://www.rfc-editor.org/rfc/rfc8174
- **R7 — Carnegie Mellon Software Engineering Institute, “Views and Beyond Collection.”** Treats architecture documentation as a record of decisions and views rather than an after-the-fact narrative.  
  https://www.sei.cmu.edu/library/views-and-beyond-collection/

## TI-Nspire CX II and ndl

- **R8 — Texas Instruments, TI-Nspire CX II specifications.** TI lists 64 MB operating memory, 90+ MB storage, and a 320×240, 16-bit-color display.  
  https://education.ti.com/en/products/calculators/graphing-calculators/ti-nspire-cx-ii-cx-ii-cas/specifications
- **R9 — Texas Instruments, 2019 TI-Nspire CX II launch information.** TI lists a 396 MHz processor, 64 MB operating memory, and the 320×240 display.  
  https://education.ti.com/en/about/press-center/3-7-2019-nspire-cx-ii
- **R10 — ndl project site.** Lists supported CX II/CX II-T OS versions, including 6.4.0.74 at the time of this audit.  
  https://ndless.me/
- **R11 — ndl repository.** Native C/C++ SDK and homebrew/runtime project.  
  https://github.com/ndless-nspire/Ndless
- **R12 — ndl toolchain build script.** At audit time it pins Binutils 2.44, GCC 14.2.0, Newlib 4.5.0.20241231, and GDB 16.2; targets ARM, uses soft-float, and disables threads, TLS, and shared libraries.  
  https://github.com/ndless-nspire/Ndless/blob/master/ndless-sdk/toolchain/build_toolchain.sh
- **R13 — ndl C++ sample Makefile.** Shows the `nspire-g++`/`nspire-ld`/`genzehn`/`make-prg` path and `.tns` packaging.  
  https://github.com/ndless-nspire/Ndless/blob/master/ndless-sdk/samples/helloworld-cpp/Makefile
- **R14 — Firebird emulator.** Community TI-Nspire emulator supporting CX II-family targets; emulator results must still be verified on physical hardware.  
  https://github.com/nspire-emus/firebird

## Giac / Xcas / KhiCAS

- **R15 — Xcas calculator ports.** The Giac/Xcas project states that Giac is ported to TI-Nspire CAS and non-CAS calculators under the KhiCAS name.  
  https://xcas.univ-grenoble-alpes.fr/calculatrices/en.html
- **R16 — Giac/Xcas project page and licensing.** Giac/Xcas is GPLv3, with the project advertising the possibility of a commercial dual license.  
  https://xcas.univ-grenoble-alpes.fr/en.html  
  https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac.html
- **R17 — Giac source mirror.** Contains explicit `NSPIRE` and `NSPIRE_NEWLIB` conditional paths, which are evidence of calculator-oriented builds but not a substitute for reproducing a current build.  
  https://github.com/geogebra/giac
- **R18 — χCAS for TI-84 Plus CE.** Describes an extra-light Giac configuration and explicitly notes that functionality was removed to fit a more constrained calculator; this supports a feature-pack/lazy-loading strategy.  
  https://xcas.univ-grenoble-alpes.fr/backup/ti/khicas84.html

## Physics tutoring and step derivations

- **R19 — Schulze et al., “Andes: An Intelligent Tutor for Classical Physics.”** Describes a physics tutor whose knowledge base solves problems and supports step-level feedback.  
  https://doi.org/10.3998/3336451.0006.110
- **R20 — Schulze et al., “A CLIPS Problem Solver for Newtonian Physics Force Problems.”** Describes planning physics solutions, strategy points, dependencies, and generation of a solution graph from rule traces.  
  https://www.researchgate.net/publication/239328061_A_CLIPS_problem_solver_for_Newtonian_physics_force_problems
- **R21 — VanLehn et al., “The Andes Physics Tutoring System: Lessons Learned.”** Reports that the interaction grain is a complete derivation made of individual steps such as coordinate systems, vectors, variables, and equations, with feedback after each step.  
  https://journals.sagepub.com/doi/abs/10.3233/IRG-2005-15%283%2902
- **R22 — SymPy manual integration documentation.** `integral_steps()` returns a rule tree with substeps and is explicitly designed to mirror hand integration.  
  https://docs.sympy.org/latest/modules/integrals/integrals.html#sympy.integrals.manualintegrate.integral_steps
- **R23 — Google Mathsteps.** A well-known step-by-step algebra project whose API records before/change/after plus substeps; it was archived on 2024-08-29 and should be treated as design evidence, not a production dependency.  
  https://github.com/google/mathsteps
- **R24 — CLIPS official site.** CLIPS was developed at NASA Johnson Space Center, is implemented in C for portability, and provides a production-rule expert-system model. Use it as a reference or benchmark candidate; do not assume the complete runtime fits the Nspire without measurement.  
  https://www.clipsrules.net/
- **R25 — egg/e-graphs.** Equality saturation compactly represents many equivalent expressions and is valuable for host-side experiments, but unconstrained saturation is not the default recommendation for the device due to memory/search growth.  
  https://egraphs-good.github.io/

## Units, dimensions, and numerical reporting

- **R26 — BIPM, SI Brochure, 9th edition (updated 2026).** Authoritative definition and presentation of the International System of Units.  
  https://www.bipm.org/en/publications/si-brochure
- **R27 — UCUM specification.** Defines an unambiguous machine-readable unit syntax and computational semantics, including equality and commensurability.  
  https://unitsofmeasure.org/ucum
- **R28 — NIST, unit conversion and dimensional analysis.** Describes dimensional analysis as the systematic basis of unit conversion.  
  https://www.nist.gov/pml/owm/metric-si/unit-conversion
- **R29 — NIST, numerical reporting guidance.** Recommends carrying available digits through calculations and rounding at final reporting rather than repeatedly rounding intermediates.  
  https://www.itl.nist.gov/div898/handbook/pmd/section5/pmd512.htm
