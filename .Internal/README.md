# Internal reference notes

Current builds use ndl-src/ndl-sdk and NDL_SDK. Follow [nps/README.md](../nps/README.md) for current commands. Dated inventories and recovery paths below retain the names of the artifacts that were inspected.

These are working notes and retained session records for StepCAS on TI-Nspire CX II. Dated claims apply to the named build and observation, not automatically to the current checkout or connected handheld. Start with the current [codebase map](../docs/codebase-map.md), [family catalog](../nps/catalog/families.md) and [PRD](../docs/StepCAS_Product_Requirements_Document.md).

| File | Covers |
|---|---|
| [target-hardware.md](target-hardware.md) | Latest observed device identity and historical hardware inventory |
| [ndl-platform.md](ndl-platform.md) | What Ndless is, supported OS list, SDK layout, toolchain, build chain |
| [ndl-api-reference.md](ndl-api-reference.md) | libndls, syscalls, Zehn, the Lua extension mechanism |
| [giac-backend.md](giac-backend.md) | Historical KhiCAS, luagiac and backend investigation |
| [ki-port.md](ki-port.md) | Ki V1, our fork of KhiCAS on giac 1.9: sources, cross-built deps, what we changed, the crash |
| [ki-v2.md](ki-v2.md) | Historical Ki V2 core design. Current StepCAS source is in nps |
| [backup-and-recovery.md](backup-and-recovery.md) | What can and cannot be backed up, the recovery ladder, install procedure |
| [tooling.md](tooling.md) | Current tooling entry points and retained emulator and USB investigations |
| [spec-reconciliation.md](spec-reconciliation.md) | Historical comparison with the supplied specification pack |
| [open-questions.md](open-questions.md) | Current unresolved work and the retained question log |
| [surface-hygiene.md](surface-hygiene.md) | Historical tooling observations, superseded by the active project instructions |

The [agent pack](agent-pack/README.md) contains imported requirements, proposed layouts, task briefs and templates for a new repository. Preserve those source documents. Their setup instructions and completion criteria do not describe the current checkout. Active instructions are in [AGENTS.md](../AGENTS.md).

## Recorded personal-use decision, 2026-09-03

The decision below records the original distribution scope. It is not evidence that a product requirement or release gate passed. Current acceptance is tracked in the [MVP scorecard](mvp-scorecard.md).

- **Licensing is out of scope.** Personal mod, not a distributed product. PRD section 18 does not
  drive anything here. Consequence: bundle luagiac rather than requiring a separate install. The
  agent pack makes Giac licensing a blocking M0 gate because it assumes distribution, so that gate
  is discharged here rather than by an ADR. See spec-reconciliation.md.

## Local layout

```
<workspace>/
  docs/StepCAS_Product_Requirements_Document.md   product requirements
  docs/codebase-map.md                           current architecture
  ndl-src/ndl-sdk/                               local SDK and retained compiler installation
  ndl-src/ndl/                                   resident runtime and installer sources
  firebird-src/                                 emulator dependency
  khi-src/                                      active Giac fork
  giac-src/                                     older Giac reference
  deps/                                         cross-build dependency recipes
  nps/                                          StepCAS core, Ki V4 UI, tests and packaging
  tools/nsptool/                                physical USB command client
  tools/keysvc/                                 resident key service
  research/folder-hiding/                        separate concealment prototype
  images/                                       retained images and recovery material
  .Internal/                                    historical notes and supplied references
```

The physics corpus, FUNDAMENTALS-OF-PHYSICS-62-pages.epub, stays outside this tree at
<workspace>/../. The agent pack requires that and so does copyright: nothing derived
from it may carry problem text, worked solutions, figures or an answer mapping. Chapter titles,
structural counts and archetype labels are fine.

smoke/ and probe/ retain measurement programs and evidence. Current product build and deployment instructions belong to nps/README.md. A package build, upload and visible handheld execution are separate checks.

## Current installation evidence, 2026-09-08

The latest [physical checks](../nps/benchmarks/BUDGETS.md#equation-opening-check-2026-09-08) identify the loaded StepCAS rendering release on OS 6.40.74. Runtime persistence and full release acceptance remain unqualified. Use [current status](../nps/STATUS.md) before acting. The inventory below describes an earlier session and is not a current deletion or installation list.

## What is on the calculator, 2026-09-02

Cleaned back to what the product needs, with every probe result copied to probe/ on the host first.

```
/ki_v1.tns                    our Lua document, the one to open
/khicaslua.tns                the stock document, kept as the control that caught the last bug
/ndless/luagiac.luax.tns      our Ki build, 3814744 bytes
/ndless/persistent.tns        Ndless across reboots
/ndless/ndless_*              installer and resources
/ndless/polyDumper.tns, eMMUlate.tns, *.img.tns    the dump and recovery set
/ndless/startup/              empty
```

Gone: smoke, heap, giacprobe, memprobe and their result files, the chan-in leftover, and
khicaslua2 through khicaslua5. The six .img.tns dumps stay: host copies exist in images/ but they
are the recovery set and 3.2 MB of 83 MB free is not worth reclaiming.

## Device state, 2026-09-01

Ndless rev 2022 is live on the calculator with persistency installed, and the stock luagiac loads.
Both hands-off loops work: firebird-headless takes keys and stdin commands, and on real hardware the
Ndless startup folder runs programs with no keypress.

**Nothing needs the maintainer at the keypad any more.** keysvc supplies keys over USB on both targets, so a
document can be opened and driven from here, which is what nl_lua_getstate needs before Giac is
reachable. On 2026-09-02 that closed Milestone 0: StepCAS differentiated x squared sin x on the
physical handheld and Giac agreed with the answer. See nps/STATUS.md.

**The emulator now runs an OS.** images/nspire-os.img boots 6.4.0.74, which unblocks GDB. That was
a timing problem in when `ln os` is sent, not the protocol gap it looked like. See
open-questions.md and tooling.md. It is also the image the MCP defaults to, and it now carries
Ndless with persistency, so a fresh boot can be driven end to end with no keypress.

**Ki, our giac 1.9 fork, runs on the calculator.** The restart blamed on it for a day was the host
channel probe in khicas.lua, not the port: it reproduces with the stock 2024 binary, and our build
works once that block is off. See ki-port.md.

Firebird is not installed anywhere: both targets run from the build tree,
firebird-src/headless/firebird-headless and firebird-src/build-qt5/firebird-emu.app. The GUI is a
Qt 5 build. Two Qt 6 ports were made first and are still in the source, and are worth keeping since
they are correct on their own terms, but Qt 6 dropped QtQuick.Controls 1, which the GUI needs, so
Qt 5 is the only build. The dead build-gui/ tree has been removed.

Headless is driven through the nspire MCP now, and the whole loop works unattended: build, deploy,
exec, and fetch the program's own output back. Keys reach the emulated keypad registers correctly,
and the OS reads them and ignores them anyway, which is measured in detail in tooling.md and is
still open. That is the one thing between here and exercising Giac without the maintainer at the keypad.

## Sources used

- https://www.hackspire.org/Ndless/ and its subpages
- http://ndless.me/
- https://github.com/ndless-nspire/Ndless, cloned to ndless-src at commit 9484d8da7c7a
- The KhiCAS files already present in khicas53/
