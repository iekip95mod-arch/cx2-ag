# AGENTS.md

## Mission

Build a deterministic, offline, step-by-step symbolic physics solver whose production target is the TI-Nspire CX II under ndl. The supplied `FUNDAMENTALS OF PHYSICS` EPUB is the requirements corpus. Correct intermediate reasoning is as important as the final answer.

## Read before changing code

1. `MASTER_AGENT_PROMPT.md`
2. `docs/ARCHITECTURE_SPEC.md`
3. `docs/CORPUS_AUDIT.md`
4. The active file in `tasks/`
5. Relevant ADRs in `docs/adr/`

The active task brief bounds the work. Do not implement later milestones opportunistically.

## Hard rules

- The production solver MUST run locally on the calculator without network services, cloud CAS, or an LLM.
- The core MUST compile for the ndl C++ toolchain and a host reference target.
- Use C++20 core-language features conservatively. Device code MUST NOT require exceptions, RTTI, threads, TLS, shared libraries, locale-heavy facilities, `std::filesystem`, or dynamic plug-ins.
- Do not expose `giac::gen` outside the Giac adapter. The project owns its semantic AST, units, frames, problem model, and derivation graph.
- Never solve first and invent prose afterward. Every displayed step MUST originate from a recorded rule application.
- Every rule application MUST record its rule ID, premises, outputs, side conditions, and verification status.
- A physics law MUST NOT fire unless its applicability predicates and assumptions are satisfied.
- Units are semantic data, not strings. Vectors always carry a coordinate frame/basis.
- General free-form NLP and image recognition are not part of the on-device MVP. Use structured problem input and authored problem packages.
- Do not copy textbook prose, images, or answer text into the repository. Store derived archetypes, stable external corpus IDs, and independently written synthetic/isomorphic tests.
- Do not claim a chapter or archetype is supported until its acceptance tests pass on the host and its designated smoke tests pass on target hardware.
- Giac licensing is a blocking M0 decision. Do not distribute a combined binary until the selected license path is documented and approved.

## C++ conventions

- C++20, `.hpp`/`.cpp`, no `using namespace std`.
- Public APIs return explicit `Result<T, ErrorCode>`-style values; no exceptions across core interfaces.
- Prefer fixed-width IDs, immutable arena-owned nodes, spans/views, interned strings, and generated compact tables.
- Avoid per-node heap allocation and per-step `std::string` storage.
- Avoid hidden global mutable state. A solver session owns its arenas, limits, assumptions, and cancellation token.
- Keep platform, UI, Giac, corpus tooling, and physics knowledge modules separated by interfaces.

## Build and validation

Until M0 establishes exact commands, use the task brief’s commands. Once established, update this file with commands that work from a clean checkout:

```bash
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure

cmake -S . -B build/nspire -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake \
  -DNPS_TARGET_NSPIRE=ON
cmake --build build/nspire --target nps.tns
```

For every change, run the narrow tests first, then the complete required suite. Report commands and results. Cross-compiling is not equivalent to running on Firebird or physical hardware.

## Change discipline

- Begin substantial work with an execution plan in `docs/plans/` using the active task’s acceptance criteria.
- Inspect existing interfaces before editing. Prefer small, reviewable changes.
- Add or update tests in the same change as behavior.
- Record significant tradeoffs in `docs/adr/`.
- Update the capability manifest when adding a rule, law, archetype, or target limitation.
- Do not perform unrelated refactors.

## Final report format

1. Scope completed
2. Files changed
3. Decisions and assumptions
4. Tests/builds run and exact outcomes
5. Host benchmarks and Nspire size/memory/runtime observations
6. Coverage changes
7. Known limitations and next blocking item
