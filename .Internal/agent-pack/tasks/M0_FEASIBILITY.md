# Task M0 — Nspire/Giac/corpus feasibility gate

## Objective

Prove or disprove the foundational assumptions before broad implementation. This task ends with a go/no-go recommendation and measured evidence. It does not implement general physics solving.

## Required workstreams

### A. Reproducible ndl toolchain

- Pin the ndl repository revision.
- Build or install the documented toolchain from a clean environment.
- Record Binutils/GCC/Newlib/GDB versions and target flags.
- Build the official-style C++ hello-world path into a `.tns` file.
- Create `cmake/toolchains/ndl-arm926ej-s.cmake` and a minimal CMake target using the supported packaging tools.
- Verify on Firebird and one physical TI-Nspire CX II with recorded OS/ndl versions.

### B. C++ runtime profile

Compile probes for the intended C++20 subset. Confirm:

- `std::span`, `std::string_view`, fixed-width integers, constexpr/consteval basics;
- no exceptions/RTTI target mode;
- static initialization behavior;
- allocation/newlib behavior;
- stack/heap measurement approach;
- timer/tick and cancellation input hooks;
- framebuffer and keypad/touchpad APIs.

Document unsupported or unexpectedly expensive facilities.

### C. Giac proof of integration

- Pin the Giac source revision or current KhiCAS-compatible source package.
- Identify all required Nspire patches/configuration and their provenance.
- Build the smallest callable Giac subset into a `.tns` application.
- Use direct API construction, not command strings, for at least:
  - exact rational arithmetic;
  - simplify `(3*x + 6) / 3`;
  - solve a linear equation;
  - simplify a trigonometric component expression;
  - add two 3D vectors or equivalent component lists;
  - one derivative;
  - one integral;
  - one small linear system.
- Measure cold/startup and per-operation runtime, `.elf`/`.tns` sizes, static sections, and peak available memory/arena use.
- Add cancellation/timeout repros for at least one deliberately expensive query.
- Minimize and record any crash/hang.

Do not claim “Giac fits” solely because KhiCAS exists. Reproduce the build and measurements in this project.

### D. CAS/license ADR

Create `ADR-0001-cas-and-license.md` with:

- GPLv3/commercial-license facts and source links;
- intended project distribution model;
- options and consequences;
- owner/counsel approval status;
- chosen temporary M0 path;
- explicit prohibition on distribution until approved.

### E. Corpus metadata audit

- Hash the EPUB.
- Reproduce chapter/sample/checkpoint/image counts.
- Generate a copyright-safe metadata-only manifest.
- Implement parser warnings/confidence and tests using a tiny synthetic EPUB fixture.
- Do not commit extracted problem text.

### F. Math-layout/UI spike

On a 320×240 target, render:

- a two-line explanation;
- a fraction;
- subscript/superscript;
- a square root;
- a 2D unit-vector equation;
- a two-equation system; and
- a scrollable step card.

Measure framebuffer/render latency and memory. Demonstrate keypad navigation and cancellation indicator.

### G. Measurement harness

Provide reusable instrumentation for:

- section/binary size;
- current/peak arena use;
- failure counts;
- solve ticks/time;
- expression/rule/planner counters; and
- device/build identity.

## Required ADRs

- ADR-0001 CAS and licensing
- ADR-0002 host/target build and C++ profile
- ADR-0003 project-owned AST versus Giac-native representation
- ADR-0004 knowledge authoring/compiled-pack format
- ADR-0005 target UI/math-rendering approach

## Acceptance commands

The agent must replace placeholders with exact commands that work from a clean checkout:

```bash
./scripts/bootstrap-host.sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure

./scripts/bootstrap-ndl.sh
cmake -S . -B build/nspire -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake \
  -DNPS_TARGET_NSPIRE=ON
cmake --build build/nspire --target nps_m0_probe.tns

./build/host/bin/nps_corpus_audit --metadata-only "$PHYSICS_CORPUS_EPUB"
./scripts/report-size.sh build/nspire/nps_m0_probe.elf
```

## Exit criteria

M0 passes only if:

- clean host and target builds are reproducible;
- the `.tns` probe runs on Firebird and physical CX II;
- Giac operations above succeed or an evidence-backed replacement path is chosen;
- target memory/runtime are measured rather than guessed;
- a viable 320×240 math layout is demonstrated;
- corpus metadata generation is copyright-safe and reproducible;
- licensing is explicitly gated; and
- the architecture docs/ADRs are updated with measured findings.

## Final output

Produce a table:

| Gate | Evidence | Result | Blocker/next action |
|---|---|---|---|
| ndl build | ... | pass/fail | ... |
| Physical device | ... | pass/fail | ... |
| Giac integration | ... | pass/fail | ... |
| Memory | ... | pass/fail | ... |
| UI layout | ... | pass/fail | ... |
| Corpus pipeline | ... | pass/fail | ... |
| Licensing path | ... | approved/blocked | ... |

Stop after M0. Do not begin M1 in the same task.
