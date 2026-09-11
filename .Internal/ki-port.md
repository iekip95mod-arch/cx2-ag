# Ki: our fork of KhiCAS on giac 1.9

This is the retained Ki V1 implementation history. Current StepCAS architecture and build instructions are in [the codebase map](../docs/codebase-map.md) and [nps/README.md](../nps/README.md).

the maintainer's decision, 2026-09-01: stop treating the shipped 2024 luagiac binary as a black box, take
ownership of the source, and move to a newer giac. Renamed from KhiCAS to **Ki**. The first name
tried was Khi, dropped because upstream already ships a separate Numworks product called Khi and a
rename collided with it in hist.cxx.

## Where the source came from

Parisse's own host, www-fourier.univ-grenoble-alpes.fr, refused every connection from this machine,
so giac_stable.tgz and khicas.zip were both unreachable. Two mirrors filled the gap:

| Tree | Version | Use |
|---|---|---|
| giac-src/ | 1.4.9, shallow clone of github.com/dmaugis/giac | reference only, and the source of config.h.nspire |
| khi-src/ | 1.9.0.93, Debian giac_1.9.0.93+dfsg2.orig.tar.xz | our fork, what we build |

**giac 1.9 already contains the Nspire port**, so there was no porting work in the move-files sense:
src/ ships Makefile.nspire, README.nspire, giacnspire.cc, luabridge.cc, luabridge.h, luagiac.c and
khicas.lua. It is a bigger target than 1.4.9, 63 objects against 48, and adds KhiCAS proper, QuickJS
and MicroPython.

## Dependencies, cross-built here

README.nspire says to build GMP, MPFR and MPFI against the ndless toolchain and install them into
its prefix. Done, and the recipes are checked in so this is reproducible:

| Library | Version | Size | Recipe |
|---|---|---|---|
| GMP | 6.3.0 | 928K | deps/build-gmp.sh |
| MPFR | 4.2.2 | 2.9M | deps/build-mpfr-mpfi.sh |
| MPFI | git master | 873K | same |

GMP is configured `--disable-assembly`: its ARM paths assume a hosted ABI it cannot probe through
nspire-gcc, and a generic C build is fast enough for a calculator.

**Never executed on the device.** They compile and link, but no instruction of any of the three has
been run on hardware. That makes them a live suspect for the crash below.

## What we changed

khi-src/src/Makefile.ki, kept separate from upstream's Makefile.nspire so the diff stays legible.
Our toolchain paths, no MicroPython, no QuickJS, LTO off, and only the luagiac.luax.tns target.

- **MicroPython dropped.** Makefile.nspire links -lmicropy, but micropython-1.12/ports here has only
  javascript, minimal, stm32, unix and windows. No Nspire port ships, so Parisse builds it
  separately. Consequence: sha256.o has to go into OBJS, which upstream leaves commented out
  precisely because MicroPython supplied it.
- **QuickJS dropped, and not merely for convenience.** Its iterator tables declare `int *pdone`
  while the implementations take `BOOL *pdone`, and cutils.h line 54 makes BOOL the Ndless enum from
  nucleus.h. Measured with a static assertion through nspire-gcc: `sizeof(BOOL)` is **1**, because
  ARM EABI defaults to short enums. The callee writes one byte where the caller reads four. Older
  GCC warned, GCC 14 errors, which is the only reason it surfaced. Silencing it would have shipped a
  live bug.
- **Renamed to Ki.** 72 matches across kdisplay.cc, kadd.cc, k_csdk.c, k_csdk.h and khicas.lua,
  covering all three casings KhiCAS, Khicas and Khi. The `KHICAS` build define and the lowercase
  `khicas` filenames are deliberately untouched. History.cc and hist.cxx are desktop Xcas FLTK and
  are not in the Nspire objects, so they were left alone.
- **English by default.** `int lang=0` at kdisplay.cc line 99, and init_locale matches. Verified
  mechanism rather than a guessed flag: line 1742 selects `(lang==1)?completeCatfr:completeCaten`.
  Giac's own `_language_` at global.cc line 2069 was already 0, outside the 1=fr 2=en 3=es 4=el 6=it
  9=pt mapping in find_doc_prefix.

## Five things that blocked the build

All from Debian's +dfsg repack shipping autotools inputs without their generated outputs, except the
last two.

1. config.h absent. Seeded from giac-src 1.4.9's config.h.nspire. It declared VERSION "1.2.0", which
   was checked and is not the crash: the macros 1.9 expects and it lacks are all optional libraries,
   and the one that matters, SIZEOF_VOID_P, leaves first.h line 386 evaluating `0==8` false and
   SMARTPTR64 undefined, which is correct for ARM32.

   It was still wrong, and now fixed to 1.9.0. first.h line 33 defines GIAC_VERSION as VERSION, and
   usual.cc `version()` builds Giac's self-report out of it, so the binary was telling callers it
   was 1.2.0. 1.9.0 is what this tree says of itself at configure.ac lines 4 to 7 and configure line
   598. The .93 in the Debian tarball name appears nowhere inside the tree, so it is not claimed.
   Verified in the built ELF: "giac for TI Nspire CX " then "1.9.0", and no 1.2.0.
2. path.h absent. Four #defines, none reachable on a calculator. Written directly.
3. input_parser.h, input_parser.cc and input_lexer.cc absent. **Regenerated from this tree's own
   .yy and .ll, never copied from 1.4.9**, because those encode 1.4.9's grammar and token numbering
   and would compile then silently mis-parse. macOS bison is 2.3 and too old; installed 3.8.2.
4. The bison prefix is not optional. `AM_YFLAGS = -p giac_yy -d` in Makefile.am line 82. The grammar
   calls giac_yyerror and input_lexer.h declares giac_yylex, so a default-prefix bison emits calls to
   a yylex nothing defines. Flex needs the matching `-P giac_yy`.
5. `#define YYLEX_PARAM`, which Bison removed in 3.0, so the scanner never reached giac_yylex. Fixed
   with `%lex-param {void * scanner}` in input_parser.yy, not in the generated output.

Two self-inflicted ones worth remembering: a stale object survived a flag change, now prevented by
`$(OBJS): Makefile.ki`; and putting that rule above `all:` made sym2poly.o the default goal.

## Status: builds and installs. The crash is not known to be ours

Read the control section at the end before trusting anything in here that calls the restart a Ki
defect. The same restart reproduces with the stock 2024 binary in place.

```
luagiac.luax.tns   3814744 bytes   66566 relocations   8432788 bytes to load
stock 2024 build   4138210 bytes   83453 relocations   8908156 bytes to load
```

Pushed to /ndless/luagiac.luax.tns on the device. **Opening khicaslua restarts the calculator.**

Ruled out by measurement, not by reading:

- **Not memory.** The stock binary that works needs *more* to load, 8908156 against our 8432788.
  Flags, entry point and container layout are identical.
- **Not link order.** Diffed against upstream's OBJS: identical apart from the nine QuickJS objects.
- **Not config.h.** See point 1 above.

Still suspect, in the order worth testing: the three math libraries, which have never run a single
instruction on the device; then giac 1.9.0.93 itself against whatever the 2024 build used; then the
regenerated parser.

**All three of those are now cleared, by running them.** See the giacprobe result below.

The next step is a debugger rather than more guessing. The emulator now runs an OS, so
khi-src/src/luagiac.luax.elf can be attached to. See tooling.md.

## giacprobe, built and on the device. 2026-09-02

src/giacprobe.c links the same 63 giac objects with no Lua at all, so it splits the failure in half:
what survives to /documents/ndless/giac-probe.txt.tns says where the fault is. No file means the
fault is before main, in Giac's static constructors; "start" alone means the first giac_caseval;
and all four lines mean Giac core is fine and the fault is in the Lua bridge.

It builds now. `giacprobe.elf` linked against $(OBJS) alone and got undefined `giac_caseval`, which
lives in luabridge.cc line 71, so luabridge.o is in the rule. 3814716 bytes, within 28 bytes of
luagiac.luax.tns, which is the expected shape for the same objects minus the Lua entry points.

**It must not go in the Ndless startup folder, and the reason is the whole point of the probe.**
The heap measurement recorded 4928 KiB as the largest single block with startup=1 against 22208 KiB
with startup=0. This binary needs 8432788 bytes to load. From the startup folder it cannot load at
all, so the output file would be missing for want of memory and would read exactly like "faulted
before main". It is at /ndless/giacprobe.tns to be launched normally instead.

**The device is carrying the stock luagiac, not ours.** /ndless/luagiac.luax.tns is 4138210 bytes,
which is the stock 2024 figure, not our 3814744. Whatever is on the calculator right now is not the
build that crashes, so reproducing the original restart needs ours pushed again first.

### Result: Giac core is fine. 2026-09-02

the maintainer ran it. All four lines came back:

```
start
1+1 = 2
diff = 2*x*sin(x)+x^2*cos(x)
done
```

The derivative is right, so this is a real evaluation and not a stub answering. In one run that
clears every suspect the section above was going to work through:

- **The three math libraries execute.** Giac's arithmetic runs through GMP, MPFR and MPFI, so they
  are no longer code that has never run an instruction on the device.
- **Giac 1.9.0.93 itself is fine**, static constructors included, since a fault there would have
  left no file at all.
- **The regenerated parser is fine.** It parsed `diff(x^2*sin(x),x)` into the right expression,
  which is the thing that would have silently mis-parsed had the grammar or token numbering drifted.

So the fault is in the Lua bridge or the memory context it loads into, which is the fourth branch
the probe was written to distinguish.

luagiac.c is 33 lines and rules out the obvious bridge candidates: it calls nl_lua_getstate,
returns 0 when that is NULL, and registers a single `caseval`. It references nothing from
MicroPython or QuickJS, so dropping those is not what breaks it.

What is left is the difference between the two run contexts rather than anything in the code:
giacprobe is a standalone Ndless program, while luagiac is a .luax with 66566 relocations loaded
inside the OS Lua interpreter. The heap probe already measured that context to matter enormously,
4928 KiB largest block under the startup folder against 22208 KiB standalone, and this binary needs
8432788 bytes to load. Nobody has yet measured the largest block available inside the Lua app.

Note this does not contradict the earlier "not memory" finding, and does not confirm it either.
That finding compared load sizes and observed the stock binary needs more, 8908156 against our
8432788. It says raw size is not the discriminator. It says nothing about how much is free where
the loading happens, which is the number still missing.

### Memory is ruled out, measured in the Lua context. 2026-09-02

probe/memprobe.c loads as a .luax and runs the same two measurements heap.c does, from inside the
OS Lua interpreter, before luagiac is required:

```
lua_state = present
largest single block = 22544384 bytes (22016 KiB)
total in 1MiB chunks = 25 MiB
luagiac needs 8432788 bytes to load
```

22.5 MB free in a single block against 8.4 MB needed, and `lua_state = present` confirms this was
measured inside the interpreter rather than in an accidental standalone run. The Lua context is as
roomy as standalone, and nothing like the 4928 KiB the startup folder gives. **Memory is not the
cause of the restart**, now on a measurement in the right context rather than on a size comparison.

**That run also rebooted the calculator, and the probe is the likely cause rather than a finding.**
The result file is complete through its last line, so memprobe ran to completion, which means
total_in_mib had just claimed 25 MiB in 1 MiB chunks and released them immediately before luagiac
tried to load 8.4 MB. Draining and returning the entire heap in front of a large load is a
fragmentation confound this probe introduced. It is not evidence about luagiac, and the nrequire
line has been taken back out of khicas.lua. Any future heap probe here should do the bisection
only and skip the exhaustion pass.

### The control, and it overturns the headline. 2026-09-02

Every "it reboots" observation had been on a device whose state was assumed rather than checked, and
the size discrepancy above shows that assumption had already failed once. So khicaslua2 was opened
with the **stock** luagiac in place, 4138210 bytes, ours nowhere on the device.

**It rebooted.** The restart is therefore not specific to our giac 1.9 build, and the section
heading above, "builds, installs, crashes", was crediting Ki with a failure that reproduces without
it. Nothing here has yet shown the Ki binary to be at fault at all.

What the two documents differ by is the other half of this. probe/screen-giac-ok.png, the one
capture of a working session, shows `*khicaslua`, the original, with its GPL3 banner and "Giac CAS
engine : OK." still in place. That is the stock Lua document. Our modified khicas.lua has never
been observed working.

And our version adds one thing that actually executes: the host channel probe, which fires
`luagiac.caseval` with Giac fopen/fprint/fclose at startup, before the UI is up. `pcall` around it
catches a Lua error but not a native fault inside Giac, so that block can take the calculator down
and the pcall would not show it. Consistent with the file listing: chan-out and chan-probe were
never created, only chan-in, which came from the host.

khicaslua4.tns is our document with that block behind `CHAN_PROBE = false`, to separate our Lua
edits from our binary.

**khicaslua4 works.** Our banner removal, English default and Ki renaming are all fine. The host
channel probe was the whole cause of the restart, and it took the calculator down through a pcall
because the fault is native, inside Giac, rather than a Lua error.

So the restart that has been attributed to the giac 1.9 port since it was first seen belongs to a
diagnostic block in khicas.lua that was added afterwards. Three suspects were investigated and
cleared, the math libraries, giac 1.9.0.93 and the regenerated parser, none of which was ever
implicated by evidence. The one measurement that would have caught it early was opening the stock
document once and comparing, which is a control that costs nothing.

Our build is now on the device at /ndless/luagiac.luax.tns, 3814744 bytes, verified by listing.
What remains is opening khicaslua4 against it.

If the channel probe is wanted later, the thing to find out first is which Giac file call faults.
`fopen` on a path that cannot be created is the obvious candidate, and giacprobe is the safe place
to try those spellings, since a standalone program that faults does not take a UI session with it.

**Ki runs.** khicaslua4 against our 3814744 byte build works on the device. The port is done: giac
1.9.0 cross-built here, English by default, renamed, no MicroPython and no QuickJS, and the restart
that stood against it for a day was never its fault.

## The version in the status line

The status line reads "Giac <version> : OK." with the version taken from Giac rather than written
into the Lua, so the label cannot drift from whichever binary is actually loaded. khicas.lua calls
`version()` and keeps the last word before the first comma out of "giac for TI Nspire CX 1.9.0,
(c) ...", scanned rather than pattern matched, and the width the "OK." is placed at now measures
the real label instead of the old fixed "Giac :".

It reads on first paint and caches, not at load. A caseval before the UI is up is precisely what
the channel probe was doing when it took the calculator down, and a cosmetic label is not worth
re-entering that window.

## Leaving the shell

`doc doc`, not `menu menu`. k_csdk.c line 1148 maps the physical DOC key to KEY_CTRL_MENU, so doc
opens the shell menu and doc again takes the Quit item.

The menu said so badly. Under NSPIRE_NEWLIB at kdisplay.cc line 19151 the French read "Quitter
(menu)", naming the keycode rather than the key on the case, and the English read plain "Quit" with
no hint at all, which is what our English default showed. Both now read "(doc doc)".
