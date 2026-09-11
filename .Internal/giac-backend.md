# Giac backend, as it exists on disk

This is the retained early September backend investigation. Artifact inventories, language settings and distribution decisions below describe those sessions. Current StepCAS uses the typed adapter and unified module described in [the codebase map](../docs/codebase-map.md#build-and-package-structure). Use [nps/README.md](../nps/README.md) for current C++20 builds and deployment.

PRD requirement PLAT-004 assumes Giac reached through a supported native or Lua bridge. That
bridge is already here.

## Artifacts in khicas53/ndless/

```
khicas.tns                          4138955   2024-07-06
luagiac.luax.tns                    4138210   2024-07-06
khicaslua.tns                         10895   2023-09-16
ndless_installer_4.5.0.tns          1106886
ndless_installer_4.5.3-4.5.4.tns       4598
ndless_installer_5.2.0-5.3.0.tns       4598
ndless_resources.tns                 180680
ptt.tns                              302320
shakeys.tns                            6693
upsilon.tns                         2270392
xcasnws.tns                         2882812
```

The two that matter are luagiac.luax.tns at 4.1 MB and khicaslua.tns at 10.9 kB. The size ratio
tells the story: luagiac is the entire Giac engine as a Lua extension, and khicaslua is a thin Lua
UI on top of it. That is precisely the split PRD section 12.2 describes as the Lua-first option,
already built and shipping.

The .luax naming confirms it uses the nl_lua_getstate mechanism documented in
ndl-api-reference.md.

## What the KhiCAS README states

Quoted from khicas53/README.khicaslua:

- KhiCAS is a free computer algebra system for TI-Nspire CX and CX II, OS 4.5.3 and 5.2.
- Install Ndless first, then send luagiac.luax.tns and khicaslua.tns to the calculator, then run
  khicaslua from Home, My Documents.
- "Giac (c) 2020 B. Parisse and R. De Graeve, GPL 3 license (see COPYING file)".
- "Linked with GMP/MPFR/MPFI".
- If the luagiac module fails to load, Ndless is probably not active, rerun ndless_installer.

The README claims OS 4.5.3 and 5.2, the local copy is from 2024, and the device runs 6.2.0.333.

## It loads on 6.2.0.333. Verified 2026-09-01

khicaslua run on the device prints `Giac CAS engine : OK.` The README's version claim is simply
stale. Two supporting facts checked in the source first:

- `interp_startup_addrs[44]` is 0x101866DC, nonzero, at ndless/src/resources/luaext.c lines 76 to
  95. Index 44 is 6.2.0.333 non-CAS CX II, at ndless/src/resources/utils.c line 220. So Ndless does
  install the hook that captures the OS Lua state on this OS.
- `nrequire` finds `<name>.luax.tns` anywhere under the documents directory and loads it with
  `ld_exec`, at ndless/src/resources/luaext.c lines 32 to 58. Nothing there is version-specific.
  The walk it uses, libndls `file_each`, overran its 5000 byte name buffer on any directory whose
  names totalled more than that, fixed 2026-09-03 in ndless-sdk/libndls/file_each.c; the handheld
  still runs resources built before the fix.

Installed on the device as /ndless/luagiac.luax.tns and /khicaslua.tns.

### The banner was trimmed

the maintainer found the startup banner annoying. khicas53/khicas.lua now drops the GPL3 attribution line, the
two "not allowed during exams" lines and the beta-version line, and shortens `Giac CAS engine :` to
`Giac :` in all three places it appears, including the two getStringWidth calls that position the
OK and NO text after it. All of them are plain drawString calls inside `if dispinfos`; nothing
functional changed, and Press-to-Test was not touched.

Rebuilt with luna to khicas53/khicaslua-nobanner.tns and deployed as /khicaslua2.tns, deliberately
under a new name so the working khicaslua.tns stays as shipped.

**Caveat.** khicaslua.tns is dated 2023-09-16 and khicas.lua 2023-03-16, so the shipped build may be
newer than the source it was rebuilt from. This could not be checked: a .tns is a TIMLP07 container
that is not plain zlib, and luna only packs. The 83 byte size drop after the first three deletions
is consistent with the source matching, but does not rule out drift.

GPLv3 obligations attach on conveying, so a private modification on the maintainer's own device is within the
license. Passing the modified build to anyone else would change that.

## Source, acquired 2026-09-01

giac-src/ is a shallow clone of https://github.com/dmaugis/giac, 274 MB. Parisse's own host,
www-fourier.univ-grenoble-alpes.fr, refused every connection from here, so the tarball at
giac/giac_stable.tgz could not be fetched; the GitHub mirror was the way in.

The Nspire port lives in src/: Makefile.nspire, config.h.nspire, giacnspire.cc, luabridge.cc,
luabridge.h, luagiac.c and khicas.lua. Makefile.nspire builds EXE = luagiac.luax.tns with
nspire-gcc and nspire-g++, links -lmpfi -lmpfr -lgmp, and compiles about 50 translation units.
GMP, MPFR and MPFI have to be cross-built first.

**The mirror is giac-1.4.9.** The binary on the device is from 2024 and ships with KhiCAS 5.3, so
this source is substantially older than what is running. Fine for reading the architecture, not a
basis for reproducing the current build.

## luagiac exposes exactly one function

From giac-src/src/luagiac.c:

```c
static const luaL_reg lualib[] = {
    {"caseval", caseval},
    {NULL, NULL}
};
```

`caseval` takes a string and returns a string, wrapping `giac_caseval`. That is the entire Lua-side
API. See open-questions.md item 10 for what it costs us.

## Language: a setting, not a translation job

khicas.lua is already entirely English. 1837 lines, two non-ASCII characters in the whole file: the
surname Andréani in a credit comment at line 10, and the ≟ glyph used as a delimiter at line 1269.
Nothing to translate there.

The French the maintainer sees is in the KhiCAS shell, which lives inside luagiac.luax.tns. That file is a PRG
wrapper around a single zlib stream at offset 0x54c28 that expands from 3791034 to 8038688 bytes,
which is why `strings` on the .tns finds nothing. Decompressed it holds 35401 strings.

The French is Giac's command help database, the `aide_cas` corpus, not UI chrome. Roughly 277 lines
carry French help text. **The same binary also carries English**, 532 strings of the "Returns the
..." form, plus German, Dutch, Spanish and Greek. It exports `set_language`, `show_language`,
`add_language` and `remove_language`, and the shell has a Config menu whose items read:

```
Config / Syntaxe (Xcas/Py/JS) / Radians (in Xcas) / Sqrt (in Xcas) /
Francais / Spanish&English / Greek&English / Deutsch&English / Digits (in Xcas): / Micropy/JS heap
```

So the fix is a setting, not a rebuild and certainly not a binary patch. Patching would be
hopeless anyway: the strings are inside the compressed payload and English help text is longer than
French, so nothing could be swapped in place.

Unconfirmed: the menu layout above is read off the string table, not seen on screen, and it is not
clear whether deselecting Francais yields English or whether English is a separate item. Needs one
look at the device. Config appears to persist in session.xw.tns, which already exists on the device
at /Xcas/session.xw.tns.

## Licensing: out of scope for this project

the maintainer has ruled licensing out as a constraint. This is a personal mod, not a distributed product, so
PRD section 18 and the licensing half of PLAT-010 do not drive any decision here. Recorded only as
fact, not as a constraint: Giac is GPL 3 per khicas53/README.khicaslua and khicas53/COPYING, and it
links GMP, MPFR and MPFI.

Practical effect: **bundle luagiac**. The PLAT-010 choice between bundling and requiring a
separately installed component was mostly a licensing question, and with licensing off the table
bundling wins on every remaining axis. It removes the dependency-missing failure mode that PLAT-012
has to handle, and with Giac absent the reduced capability manifest would be close to empty anyway,
which made that branch weak to begin with.

## Where this bears on the architecture question

PRD section 12.2 asks for a measured comparison between Lua-first and native-core. Two facts
narrow it before any measurement:

1. The Lua-first path has a working reference implementation on this exact class of device, in
   khicaslua. The risk there is known and small.
2. The Lua on device is 5.1 (see ndl-api-reference.md). Writing the derivation kernel, the rule
   engine, the unit algebra and the proof-obligation machinery in Lua 5.1 is possible but gives up
   the type checking that a codebase full of typed claims, envelopes and evidence kinds benefits
   from. GCC 14.2 with C++23 is available on the same device.

A plausible split, to be tested rather than assumed: kernel and rule engine in C++ as an Ndless
program, Giac reached through the same luagiac bridge or through a direct native link, UI in
whichever layer measures better. The measurement PERF-011 demands is the end-to-end resident peak,
so the answer cannot be inferred from these notes.
