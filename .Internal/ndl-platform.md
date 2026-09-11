# ndl platform and SDK

Current commands and paths are documented in [nps/README.md](../nps/README.md). The active dependency is ndl-src, with ndl-sdk and ndl subdirectories. Use NDL_SDK and nps/scripts/bootstrap-ndl.sh. The release inventory below is retained history and keeps the upstream names.

## What it is

Ndless is a resident program plus utilities that open the TI-Nspire to third-party C and assembly
development. It is licensed under the Mozilla Public License. It does not replace the TI OS, which
is what PRD requirement PLAT-002 needs.

- Wiki: https://www.hackspire.org/Ndless/
- Releases: https://github.com/ndless-nspire/Ndless
- Issues: https://github.com/ndless-nspire/Ndless/issues

## Recorded release inventory, 2026-09-01

The latest-release claim, checkout names and supported-version list below describe the original inspection. They were not requalified by the September 8 documentation audit. Current loaded-device evidence is in [BUDGETS.md](../nps/benchmarks/BUDGETS.md).

Latest release is **r2022**, titled "Ndless r2022 for OS 6.4.0.74", published 2026-06-08, single
asset ndless-r2022.zip at 1338182 bytes. Checked via the GitHub releases API.

The local ndless-r2022/ directory is that release unpacked. The clone in ndless-src/ is the tip of
main at commit 9484d8da7c7a4dde9766138c2e42e1d1e3acfcd4, dated 2026-07-06, which is slightly ahead
of the r2022 tag.

## Supported OS versions

Quoted from http://ndless.me/:

```
Clickpad/Touchpad/CX: 3.1.0.392 3.6.0.546 3.9.0.463 3.9.1.38
CX:                   4.0.3.93 4.2.0.532 4.4.0.532 4.5.0.1180 4.5.3.14 4.5.4.48 4.5.5.79
CX II/CX II-T:        5.2.0.771 5.3.0.564 6.2.0.333 6.4.0.74
```

Only four OS builds are supported on CX II at all. 6.2.0.333 is one of them. PRD open question 2
asks which TI OS and Ndless versions define the supported baseline, and the honest answer is that
the CX II gives almost no choice: it is 5.2.0.771, 5.3.0.564, 6.2.0.333 or 6.4.0.74.

## Building the SDK

Documented at https://www.hackspire.org/C_and_assembly_development_introduction/.

Host dependencies on macOS: git, GCC with C++ support, binutils, GMP, MPFR, MPC,
boost-program-options, zlib, wget.

**Plus php, which the hackspire page does not mention.** `make` at the repo root fails at
ndless/src/tools/MakeSyscalls/mkSyscalls.php, which generates ndless-sdk/include/syscall-addrs.h
and is not checked in. No php was present on this machine. `brew install php`, 8.5.10 here.

```
git clone --recursive https://github.com/ndless-nspire/Ndless.git
cd ndless-sdk/toolchain/ && ./build_toolchain.sh
export PATH="[repo]/ndless-sdk/toolchain/install/bin:[repo]/ndless-sdk/bin:${PATH}"
make      # from the repo root, builds Ndless itself
```

PREFIX in build_toolchain.sh defaults to the script's own install subdirectory. Success is
signalled by "Done!" or by echo $? returning 0. Verify by running nspire-gcc with no arguments and
seeing "arm-none-eabi-gcc: fatal error: no input files".

### Toolchain component versions

From ndless-src/ndless-sdk/toolchain/build_toolchain.sh lines 21 to 24:

```
TARGET  = arm-none-eabi
BINUTILS = binutils-2.44
GCC      = gcc-14.2.0
NEWLIB   = newlib-4.5.0.20241231
GDB      = gdb-16.2
```

GCC 14.2 means C++23 is largely available if the native-core option in PRD section 12.2 wins.

### Build flags that constrain the design

From the same script, lines 27 to 34:

```
CFLAGS_FOR_TARGET  = -DHAVE_RENAME -DMALLOC_PROVIDED -DABORT_PROVIDED -DNO_FORK
                     -mcpu=arm926ej-s -ffunction-sections -O3
OPTIONS_GCC        = ... --enable-languages=c,c++ --with-newlib --disable-threads
                     --disable-tls --disable-shared --with-float=soft ...
OPTIONS_NEWLIB     = ... --enable-newlib-io-long-long --enable-newlib-io-float
                     --disable-newlib-supplied-syscalls --with-float=soft ...
```

Four things fall out of this and they all matter to StepCAS:

1. **Soft float.** No FPU on the part, and the toolchain is built for it. Floating point is slow.
   Exact rational and symbolic arithmetic is not just pedagogically right here, it is also the
   fast path.
2. **No threads and no TLS.** Cancellation of a long Giac call, which PERF-003 and PERF-009
   require, cannot be done by killing a worker thread. It has to be cooperative, either a Giac
   interrupt hook or bounded work between polls. This is a real Milestone 0 question.
3. **Static only.** --disable-shared. Everything links in, so binary size is a hard budget item,
   which PERF-010 already wants frozen.
4. **MALLOC_PROVIDED.** The allocator is the OS heap reached through syscalls, not a private
   newlib heap. See target-hardware.md.

## The four-tool build chain

| Tool | Role |
|---|---|
| nspire-gcc | GCC wrapper, compiles to object files |
| nspire-ld | now just an alias for nspire-gcc, kept for compatibility |
| genzehn | converts the ELF into the Zehn container |
| make-prg | prepends a loader for older Ndless versions |

nspire-gcc is a four-line shell script. Its full body, from
ndless-src/ndless-sdk/bin/nspire-gcc:

```
exec arm-none-eabi-gcc -mcpu=arm926ej-s -D _TINSPIRE -fuse-ld=gold "$@" \
  -I "$home/.ndless/include" -I "${NDLESS}/include" -I "${NDLESS}/include/freetype2"
```

So _TINSPIRE is the predefined macro to gate device-only code on, and gold is the linker.

Canonical build sequence, from the hackspire tutorial:

```
nspire-gcc -Wall -W -marm -Os -c hello-sdl.c
nspire-ld hello-sdl.o -o ./helloworld-sdl.elf
genzehn --input ./helloworld-sdl.elf --output ./helloworld-sdl.tns --name "helloworld-sdl"
make-prg ./helloworld-sdl.tns ./helloworld-sdl.prg.tns
```

A new project skeleton comes from:

```
nspire-tools new [program_name]
```

## Samples worth reading

In ndless-src/ndless-sdk/samples/:

```
helloworld-cpp   minimal C++, uses nspire-g++, -Wl,--nspireio
newlib-c++       C++ against newlib
luaext           a Lua extension module, the pattern luagiac itself uses
zehn             Zehn container specifics
helloworld-sdl, link-sdl, colors, particles, ngc, freetype
```

The C++ samples confirm C++ is a supported first-class option, contradicting nothing in the PRD
but settling that half of the section 12.2 architecture question: native-core in C++ is buildable,
so the comparison is a measurement exercise rather than a feasibility one.

## Linker script

ndless-src/ndless-sdk/system/ldscript. Entry point is _start. Text starts at address 0x0 and the
image is relocated by the Zehn loader. Sections are text, got, data, ARM.extab, ARM.exidx,
eh_frame and bss. The presence of ARM.exidx and eh_frame means C++ exception unwinding tables are
laid out, which matches the Zehn feature list claiming exception support.

## Documented limitations

From https://www.hackspire.org/Ndless_features_and_limitations/:

- Pure-assembly programs support only ARM-state main entry points.
- Assembly file extensions must be uppercase .S.
- Resident programs must not use argv after returning, and their memory blocks cannot be freed.
- Syscalls have minimum Ndless revisions. Calling one that the running Ndless does not have
  crashes the calculator. Guard with assert_ndless_rev().

The page does **not** document heap size, stack size, floating point or threading limits. Those
have to come from measurement, not from the wiki.

## Three macOS build traps

None of them announces itself, and the first one silently ate about two hours.

### The stage markers never match, so nothing is ever skipped

The script records a finished stage by writing the version string to a dot file, using echo with the
-n flag. Under macOS /bin/sh that flag is not supported and gets written out as literal text, so the
marker ends up holding this:

```
-n binutils-2.44
```

The guard compares that against binutils-2.44, never matches, and rebuilds. So **every invocation
rebuilds binutils and GCC step 1 from scratch**, roughly 40 minutes, before reaching whatever failed
last time. Three attempts died at Newlib without ever revealing this, because each spent its whole
budget redoing work that was already installed and correct.

Fixed locally by replacing the five uses of echo -n with printf %s, and rewriting the two existing
markers with the right contents.

### Homebrew on CPATH breaks the GCC build

Passing `CPATH=/opt/homebrew/include` so configure can find gmp, mpfr and mpc also exposes Homebrew's
`libintl.h`, which collides with GCC's own gettext handling and fails libcpp with a wall of
`expected unqualified-id` at `libintl.h:1091`.

The fix is a shim directory holding symlinks to only the headers that are actually needed, so
`libintl.h` is never visible:

```
tools/gcc-deps-include/   gmp.h gmpxx.h mpfr.h mpf2mpfr.h mpc.h
CPATH=<that dir>  LIBRARY_PATH=/opt/homebrew/lib
```

Adding `--disable-nls` to `OPTIONS_GCC` would also work but means editing the vendored options line.

### Newlib needs makeinfo

`libgloss/doc/porting.info` fails with `Error 127` when `makeinfo` is absent. `brew install texinfo`,
then put `/opt/homebrew/opt/texinfo/bin` on PATH for the build, since the formula is keg-only.

## The "Ndless successfully installed!" banner, removed 2026-09-02

install.c line 285, `ins_successsuccessmsg_hook`, drew it. The hook is not only a banner, which is
why it was patched rather than skipped: it also closes the resources document, and it counts six
frames so it can uninstall itself and clear the cache. Returning early from
`ins_install_successmsg_hook` would have been a one-line change that quietly dropped both.

So only the drawing came out, four `gui_gc_*` calls and the string. Every other path is untouched:
the `close_document` call, the frame counter, HOOK_UNINSTALL, `clear_cache` and
HOOK_RESTORE_RETURN.

Checked before it went near the calculator:

| | known good | patched |
|---|---|---|
| size | 196824 | 196404 |
| relocations | 455 | 450 |
| flags / extra data | 8 / 20 | 8 / 20 |
| entry point | 0x9c60 | 0x9bc4 |
| utf16 "Ndless" occurrences | 2 | 1 |

Same flags, same extra data, same version, same LCD capability lines, and the drop from two UTF-16
"Ndless" strings to one is the banner leaving. The survivor is the compatibility mode dialog at
lcd_compat.c line 227, which is a different message and was left alone.

**Only ndless_resources.tns changed.** ndless_installer and persistent rebuilt byte identical, md5
c241d9a3ca10e7beff8376d22134ece0 and a75ce7c08bc61c8e91c426cad649be56, so neither was re-pushed.

Recovery, if this ever misbehaves: ndless-known-good/ holds the three artifacts the device was
running before, and pushing ndless_resources.tns back is the whole fix. Ndless also backs out on
its own if ESC is held at boot, at persistency.c line 142 for the persistence install and
ploaderhook.c line 481 for the startup folder.
