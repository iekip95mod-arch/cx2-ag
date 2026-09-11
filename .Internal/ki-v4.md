# Ki V4: one module, with Giac linked into it

This is the retained September implementation log. Build flags, module names, measurements and completion claims below apply to those sessions. Use [the codebase map](../docs/codebase-map.md) and [nps/README.md](../nps/README.md) for current architecture and commands.

Started 2026-09-03. Ki V3 is the Ki V1 shell plus step modes, running against two separate images:
luagiac.luax.tns, which holds Giac and exposes one function, and nps_split.luax.tns, which holds the
step engines. A backend call inside a derivation therefore left C++, went out through the Lua
interpreter, and came back. the maintainer asked the obvious question: why call it indirectly, and why two
modules. V4 is the answer. Giac is linked into the stepcas module, so a backend call is an ordinary
call.

## What actually changes

Not much, on purpose. The shell, the step modes and the viewer are Ki V3's. Two things differ.

The document asks for one module. lua/nps_v4.lua does nrequire("nps_nspire") and nothing else, where
V3 does nrequire("luagiac") and nrequire("nps_split"). The table that comes back carries both halves: the step
entries, and a caseval for the shell's own use.

The backend behind a derivation is a direct call. src/lua_module.cc gains DirectGiacBackend under
STEPCAS_GIAC, whose eval calls giac_caseval and copies the reply, beside the LuaGiacBackend the
non-unified build keeps. One typedef picks between them, and both call sites use the typedef, so
the two builds cannot drift into asking Giac at different points. That matters more than it looks:
the Giac-first, table-after ordering exists because a re-entrant call into Lua while a half built
table is on the stack took the calculator down, and the unified build has no re-entrancy at all. It
keeps the ordering anyway, so a defect in one build shows up in the other.

## The trust boundary moves, slightly, and it is worth saying so

lua_module.cc's header used to claim there is no way for Lua to reach Giac with a string of its own.
That was true of V3's module and is not true of V4's, because caseval is in the table. The claim is
now stated where it belongs: no step entry will carry a Giac command that Lua wrote, and every step
entry still parses the user's text into a validated AST before anything acts on it. caseval is the
shell's channel, the one Ki V1 had through luagiac, and the shell exists to evaluate what the user
typed. Every KhiCAS feature outside the step modes runs on it.

## The build

The target make -C nps unified produces build/device/nps_nspire.luax.tns. It links stepcas's twelve device
objects, lua_module.cc compiled a second time with -DSTEPCAS_GIAC=1, and Giac's 55 objects plus
luabridge.o from khi-src/src, with luagiac's own link line.

Three things about that list are worth knowing.

It is Makefile.ki's list, asked for at link time rather than copied into nps/Makefile. A copy
would go stale the first time a source is added there, and the failure would be an undefined symbol
a long way from the cause. nps/tools/giac-objs.mk exists only to print it.

luagiac.o is left out deliberately. It has its own main and registers its own caseval table, and
this build supplies both. Linking it gives two mains.

A wildcard over khi-src/src would be simpler and wrong: giacprobe.o sits beside the others and has a
main of its own.

lua_module.cc is compiled twice, into lua_module.o and lua_module_giac.o, because the two builds
differ by a define and one shared object would silently give whichever was built last.

The two builds register different names, "stepcas" and "ki". That is not decoration: a document
written for one cannot quietly load the other and find half of what it expects.

genzehn warns "Using both the old (SCREEN_BASE_ADDRESS) and new (lcd_blit) API! Assuming
'--uses-lcd-blit false'", because Giac's display code uses one and stepcas the other. The warning
names a real ambiguity and decides a flag nothing reads: ndless-src has USES_LCD_BLIT in zehn.h and
in genzehn only, and zehn_loader.cpp's flag switch falls through to default for it. The flag the
loader does read is RUNS_ON_HWW, set by --240x320-support, which this passes as true exactly as the
stepcas.luax build does. So the warning is cosmetic on this Ndless. It would stop being cosmetic if
a later Ndless started reading the flag.

## Size

nps_nspire.luax.tns is 3862524 bytes, against 3814775 for luagiac.luax.tns and 122935 for
nps_split.luax.tns. So the one image is about 75 KB smaller than the pair, which is the C runtime
that is no longer there twice plus what --gc-sections can drop once it can see the whole program.

## Testing

test/ui_smoke_v4.lua is ui_smoke_v3.lua against lua/nps_v4.lua, with the fake nrequire changed from
"answers anything" to "answers ki and raises for everything else", plus checks that exactly one
module was asked for and that one load answered for both halves. That change is the point of the
file: run the same harness against lua/nps_v3.lua and it fails at the first of those checks and then
collapses, which was confirmed rather than assumed. 259 checks.

Nothing on the host builds lua_module.cc under STEPCAS_GIAC, because giac_caseval only exists on the
device. So test/luax_host.lua exercises the LuaGiacBackend path and the unified path has no host
coverage at all. What makes that tolerable is that the define changes one class and the name the
module registers under, and both call sites go through the typedef rather than naming a backend. The
emulator is the only evidence the unified path works, and the run below is it.

## Emulator, 2026-09-03

Fresh session after a flash save and a restart, since a third Lua document loading Giac in one OS
session never finishes loading. nps_nspire.luax.tns sent to /ndless, which took the two extension name, then
fetched back and compared byte for byte. nps_v4.tns to My Documents.

First paint drew "Giac 1.9.0 : OK." (probe/v4c.png). That label is read from the module at first
paint rather than hardcoded, so it is already evidence the direct call works.

- Plain line: expand((x+1)*(x+2)) gave x^2+3*x+2 in the 2D history box (probe/v4d.png). That is the
  shell's own path through the module's caseval.
- !d x^2 gave (2 * x), "differentiated | Giac agrees", 0 ms, 12 nodes, 2 steps, 2 Giac calls,
  472 KB Lua heap (probe/v4e.png).
- !i 1/x gave (C + ln(x)), "integrated | Giac's derivative agrees", "assumes: x > 0", 0 ms, 24
  nodes, 5 steps, 2 Giac calls (probe/v4f.png).
- !s 2x+5=13 gave 4, "solved | Giac agrees", 0 ms, 16 nodes, 4 steps, 2 Giac calls (probe/v4g.png).
- The kinematics line gave v = 17 m/s, "solved | Giac's own solve agrees", 10 ms, 45 nodes, 8 steps,
  4 Giac calls, 15 rows (probe/v4i.png).

Every answer, tag and cost matches what Ki V3 produced for the same inputs, which is what a change
of plumbing should look like. The flash image was saved with nps_nspire.luax.tns and nps_v4.tns on it.

## The Physics group, and what it caught

A Physics box first in the tool palette, written in the shape Ki V1 uses for its own groups: seven
entries whose labels are kinematics lines that run as they stand, one per unknown plus a blank
template, each inserting its own label. No separators and no mode switches, because Ki V1 has
neither. The solver controls live on the input line rather than in the palette.

It failed the first time it was tried on the emulator, and the failure was not in the menu. The 2D
math box renders the inserted text and hands it back with the fraction bracketed: "5 m/s" comes out
as "5 ((m)/(s))" and "3 m/s^2" as "3 ((m)/(s^(2)))". The unit scanner read a flat sequence and
rejected a bracket outright, so nothing with a division in the middle of a line had ever been
readable. The keysvc runs before this had all put the one division at the end of the line, which is
where a fraction has nothing after it to bracket, so none of them met it.

src/units.cc scans brackets now rather than skipping them. Skipping is the two line version and it
is wrong: it reads m/(s*kg) as metres per second times kilograms, which turns a refusal into a wrong
answer. A division applies to the one factor after it and a bracketed group is one factor, so both
spellings mean what they say. The exponent may be bracketed too, since the editor writes s^(2).

One cosmetic thing is left. Unit.text keeps the input as given, so the plan's facts echo
"v0 = 5 ((m)/(s))". The dimension, the scale and the answer's SI unit are all right; only the echo
of the user's own text carries the editor's brackets.

## C++20 and no RTTI, 2026-09-03

The agent pack requires C++20 with exceptions and RTTI both off on target (its section 5.1). The tree
was C++11 with -fno-exceptions and nothing about RTTI. the maintainer asked for the switch and it is done.

Four places pinned the standard, not the two in the Makefile that a grep of CXXFLAGS suggests:

```
Makefile:21   HOST_FLAGS  -std=c++11               -> -std=c++20
Makefile:27   DEV_FLAGS   -std=c++11               -> -std=c++20 ... -fno-rtti
tools/tidy.sh:26         clang-tidy -- -std=c++11  -> -std=c++20 ... -fno-rtti
tools/lint-resident-exit.sh:69  clang-query -std=c++11 -> -std=c++20 ... -fno-rtti
```

Missing either of the last two would have left the checks reading the sources under a different
language than the compiler does, which is the kind of gap that stays quiet until a C++20 construct
lands and only one of them complains.

-fno-rtti went to the three that build or analyse target code. HOST_FLAGS keeps RTTI, matching how
-fno-exceptions is already handled: the target profile is enforced on the target, and make device
cross compiles every source, so the gap is one make target wide.

No source file changed. Not one line. The whole core, the twelve engine sources plus device_probe.cc
and lua_module.cc, compiles clean at -Wall -Wextra under the new flags on both the host and
arm-none-eabi-g++ 14.2.0, with no new warnings.

Everything passes, from a build tree deleted first rather than an incremental one:

```
make test     652 host checks, 16 Ki V2 UI, 286 Ki V3 UI, 289 Ki V4 UI, lint ok, clang-tidy ok
make device   all 14 objects cross compile, no warnings
make san      652 checks plus 2000 fuzz cases under ASan and UBSan, 98 bridge checks
make fuzz     10000 cases, 0 failures
```

Size, which is the number that could have moved:

```
nps_nspire.luax.tns        3862524 -> 3863175   +651 bytes, 0.017 per cent
nps_split.luax.tns    122935 -> 124875   +1940 bytes, 1.6 per cent
```

The unified image barely notices because Giac is almost all of it and Giac did not change. The
standalone module carries the same absolute growth over a much smaller base, so the percentage looks
worse and means the same thing. Neither matters against 90 MB of storage.

The C++20 objects link against Giac's own objects, still built as C++11 by khi-src/src/Makefile.ki,
with no ABI trouble. That is expected for one GCC, and it was worth linking rather than reasoning
about, because a silent mismatch there would surface as a reset on the calculator rather than as a
link error.

### Verified on the emulator

Fresh boot, nps_nspire.luax.tns and nps_v4.tns sent over, flash saved after. Every mode run and every number
compared against what the C++11 build produced.

First paint drew "Giac 1.9.0 : OK." (probe/c20c.png). That string is read from the module rather than
hardcoded, so it is already proof the unified image loaded and the direct Giac call works with RTTI
off.

```
!d x^2              (2 * x)      differentiated | Giac agrees     0 ms  12 nodes  2 steps  2 giac
!i 1/x              (C + ln(x))  integrated | Giac's derivative agrees, assumes x > 0
                                                                 10 ms 24 nodes  5 steps  2 giac
!s 2x+5=13          4            solved | Giac agrees             0 ms  16 nodes  4 steps  2 giac
!kv=?,t=4s,x=44m,v0=5m/s
                    v = 17 m/s   solved | Giac's own solve agrees 10 ms 45 nodes  8 steps  4 giac
                                                                 15 rows
```

probe/c20d.png, c20f.png, c20g.png and c20e.png. Every answer, tag, node count, step count, Giac call
count and row count is identical to the C++11 run recorded above.

Two numbers moved and neither is the flags. The integral reported 10 ms where it reported 0 ms
before, which is the emulator's timer granularity on a boundary rather than a slowdown, and the same
run reported 0 ms for the derivative and the solve. Lua heap readings differ throughout (517k, 446k,
419k, 478k against the 472 KB and 506 KB recorded before) because they are taken at different points
in a session, which is what that counter measures.

### The two-extension name failed silently, again

deploy of nps_nspire.luax.tns printed "Send complete" and "Link transfer complete", and the file was not
there: a fetch of /ndless/nps_nspire.luax.tns was refused. The same bytes went over as ki.tns and fetched
back byte identical, so the link was healthy the whole time.

ln mv is refused by this OS, confirmed twice, which matches tooling.md's later reading and
contradicts the earlier line there saying the rename works. The rename dance is not a workaround
here, it is unavailable.

What did work was sending nps_nspire.luax.tns a second time, unchanged. So the two-extension refusal is
intermittent rather than deterministic, which is what tooling.md already suspected when luagiac.luax
went over under its own name in the same minute that stepcas.luax failed.

The lesson is the one tooling.md already states and this session proved again: "Link transfer
complete" means nothing. Fetch it back and compare, every time. Both artefacts here were confirmed
that way.

## The child pool, 2026-09-03

The agent pack forbids per-expression new and delete on target (its section 5.1) and asks for child
IDs in a compact contiguous pool (its 9.2). A Node held a std::vector<NodeId>, so every node with
children cost its own allocation. That is now an index and a count into one pool the Arena owns.

```cpp
uint32_t child_offset;
uint32_t child_count;
```

Read through Arena::children, which hands back a ChildView. The view keeps a pointer to the pool
object rather than to its buffer, and resolves on every access, so a view taken before more nodes are
built still reads correctly after the pool has grown. That is not a nicety. Five sites in
differentiate.cc and integrate.cc used to copy the child list into a local vector precisely because
the old vector could be invalidated mid-recursion, and integrate.cc's linear_in iterates children
while calling a.nary inside the loop. Those copies are gone, and the hazard they guarded against is
gone with them rather than being guarded again.

The children are appended below the intern lookup, not above it, so a subexpression the arena already
holds costs no pool slots either. The census confirms it: 95 nodes over seven expressions, 159 child
links, and 159 slots held. No waste.

### Measured

Host, with a counting operator new around four representative solves:

```
                                    allocations        bytes allocated
parse + canonicalize, 5 inputs      476 -> 399         25032 -> 21284
differentiate x^3 sin x + x^2/(x+1) 256 -> 223         35456 -> 31556
integrate 3x^2 + 1/x + sin 2x       736 -> 642         59597 -> 55741
kinematics v from v0, a, t          268 -> 259         36240 -> 32264
```

Between 3 and 16 per cent fewer allocations, 6 to 15 per cent fewer bytes. sizeof(Node) on the host
fell from 56 to 40.

On the device it did not fall at all. sizeof(Node) reads 40 in the probe output now and ki-v2.md
line 92 records 40 before, because on 32-bit ARM the vector was 12 bytes, the pair of uint32s is 8,
and int64_t small forces the struct to an 8-byte multiple either way. Both layouts round to 40. So
the honest statement for the target is that the node did not shrink and the allocations went away.
Quoting the host's 29 per cent would be quoting the wrong machine.

Code grew slightly, which is the accessor: nps_nspire.luax.tns 3863175 to 3863312 (+137), nps_split.luax.tns
124875 to 125185 (+310), nps_bench.tns 219812 to 219972 (+160). Trading 300 bytes of flash for
one malloc per node built is the right way round on a device whose heap is the scarce thing.

clang-tidy found the change had a second effect before I did: Node became trivially copyable, so
every std::move on it was dead. Seven of them, now gone, along with the rvalue parameters that
existed to steal a vector that is no longer there.

### Verified

652 host checks, 16 Ki V2 UI, 286 Ki V3 UI, 289 Ki V4 UI, resident-exit lint and clang-tidy all
clean. Sanitizers clean over 652 checks, 2000 fuzz cases and 98 bridge checks. The 10,000 case fuzz
reports exactly the same census as before the change, down to each bucket: 5794 parsed, 2229 syntax
errors, 1228 depth exceeded, 360 size exceeded, 389 input too long, 6755 twins compared, 5790
rewrites. A pure change of representation should move none of those, and it moved none of them.

The device probe ran on the emulator and every line matches what it printed before, including the
Giac round trip, the four-step solve, the four-step derivative and every refusal.

Then the whole document, fresh boot with the new module, all four modes (probe/c21a.png through
c21e.png):

```
!kv=?,t=4s,x=44m,v0=5m/s   v = 17 m/s    45 nodes  8 steps  4 giac  15 rows
!d x^2                     (2 * x)       12 nodes  2 steps  2 giac
!i 1/x                     (C + ln(x))   24 nodes  5 steps  2 giac  assumes x > 0
!s 2x+5=13                 4             16 nodes  4 steps  2 giac
```

Every count identical to the run before the change, and every tag still says Giac agrees. The Lua
heap readings sit in the same range and are not compared, because they depend on where in a session
the reading is taken rather than on the arena.

One keysvc packet was dropped with no reply and had to be resent, which is the parked-frame hazard
already recorded rather than anything to do with this change. The retry went through unchanged.

## The typed adapter, 2026-09-03

PRD section 12.3 and the agent pack's 17.2 both say to hand the engine objects rather than a command
string. Until now the adapter printed the AST, Giac parsed the text, computed, printed its answer,
and our parser read it back. Four conversions per call, two of them Giac's. Now there are none: the
AST becomes giac::gen directly, the operation is an ordinary C++ call, and the answer gen is walked
into the arena.

src/giac_typed.cc is the only file in the tree that includes both this project's headers and Giac's,
and src/giac_typed.h names no Giac type, so the core still builds with no Giac on the include path.

### How it hangs off the existing adapter

Backend gained one virtual with a default of false:

```cpp
virtual bool typed(const Request &request, Arena &arena, TypedResult *out);
```

Adapter::run asks for the typed path and falls back to eval when the backend says no. That keeps
both arrangements working from one adapter: Ki V4 has Giac in the image and answers typed, while
Ki V3 reaches it over a Lua call that cannot carry an object and keeps the string path. The
allowlist check runs before either, so the two paths cannot drift into two different allowlists.

### What the types buy beyond the two conversions

The string path had to read Giac's own trouble out of its answer text, matching "Error" and
"Time limit" as whole words because a user symbol called undef_val would otherwise look like a
message about itself. That whole mechanism is gone from the typed path. Giac here is built with
-DNO_STDEXCEPT, so a failure returns an undef gen and is_undef finds it structurally.

Refusals also get sharper. A result Giac hands back that this project has no node for is refused on
its type tag, by name, rather than by failing to parse its printed form. The same goes for solution
sets: peeling a one element list is now a look at _VECT and its size rather than bracket counting
over text.

### Two defects the device found and no host test could

**The calculator's Lua has no io library.** The first version had the document write the report
file. `attempt to index global 'io' (a nil value)`. The module writes it now, with fopen, which is
what device_probe.cc already did.

**lua_pushnumber cannot work from an Ndless program.** The disagreement count came back as
6.4393932879873e-219. That is not a wrong count, it is an uninitialised register pair read as a
double. Ndless routes the Lua API through wa_syscall2 in ndless-sdk/include/syscall.h, which takes
its arguments as int and passes them in r0 and r1:

```c
static inline int wa_syscall2(int nr, int p1, int p2)
```

and the stub at libsyscalls/stubs.cpp:438 calls it as `syscall<e_lua_pushnumber, void>(p1,p2)`,
where the template at syscall.h:90 casts each argument to int. So the value is truncated and put in r1 while the OS reads r2 and
r3 for a soft float double. It cannot carry any value at all. lua_pushinteger takes a lua_Integer,
which is an int, and marshals correctly, which is why every other number in this module already
went through it.

This is the same family as the lua_toboolean defect in STATUS.md, and worth knowing before the next
person reaches for a Lua API call with a double in it. The mechanism matches the magnitude, which is
how it was confirmed rather than guessed: a tiny denormal is what leftover register contents read as.

### Verified by running both paths against each other

The typed path has no host coverage and cannot have any, because Giac is not linked on the host. So
the evidence comes from the calculator: typed_differential_check runs each allowlisted operation
through the typed path and the string path on the same input, canonicalises both answers, and
compares. Reached with the !t prefix, which writes the report to /ndless/typedcheck.txt.tns.

Sixteen cases, all agreeing, fetched from the emulator:

```
simplify (3*x + 6)/3: agree, exact (x + 2)
simplify x + x + x: agree, exact (3 * x)
expand (x + 1)*(x + 2): agree, exact ((x^2) + (3 * x) + 2)
factor x^2 + 3*x + 2: agree, exact ((x + 1) * (x + 2))
differentiate x^2*sin(x): agree, exact ((2 * x * sin(x)) + ((x^2) * cos(x)))
differentiate 1/x: agree, exact (-((x^2)^-1))
integrate 2*x: agree, exact (x^2)
integrate cos(x): agree, exact sin(x)
solve 2*x + 5 = 13: agree, exact 4
solve 3*x = 7: agree, exact (7 * (3^-1))
simplify 12345678901234567890 + 1: agree, exact 12345678901234567891
simplify 1/3 + 1/6: agree, exact (1 * (2^-1))
approximate 1/4: agree, approximate 0.25
simplify sqrt(4): agree, exact 2
differentiate exp(2*x): agree, exact (2 * exp((2 * x)))
simplify x^0: agree, exact 1
```

Three of those carry more weight than the rest. The big integer goes out through mpz_init_set_str
and comes back through mpz_get_str, so both wide paths ran. 1/3 + 1/6 returns a _FRAC, which this
project has no node for and spells as a power of minus one, and the two paths still agree on it.
The evalf case is the only one tagged approximate, which is the decimal detection working on the
gen tree rather than on printed text.

Each case gets its own pair of arenas, so a canonical form left over from an earlier case cannot be
the reason two answers compare equal.

### Nothing else moved

The typed path is now what every allowlisted call goes through, so the four modes were run again on
a fresh boot and compared against the C++20 figures recorded above. The screen reads "typed check:
0 disagreements" (probe/c23e.png), so lua_pushinteger marshals correctly where lua_pushnumber could
not.

```
!t                         0 disagreements
!d x^2                     (2 * x)       12 nodes  2 steps  2 giac
!i 1/x                     (C + ln(x))   24 nodes  5 steps  2 giac  assumes x > 0
!s 2x+5=13                 4             16 nodes  4 steps  2 giac
!kv=?,t=4s,x=44m,v0=5m/s   v = 17 m/s    45 nodes  8 steps  4 giac  15 rows
```

probe/c23f.png, c23l.png, c23p.png and c23q.png. Every answer, tag, node count, step count and Giac
call count is what the string path produced. The tags are the same too: Giac agrees on the
derivative, Giac's derivative agrees on the integral, Giac agrees on the solve, and Giac's own solve
agrees on the kinematics. Only the millisecond readings differ, all now 0 where two were 10 before,
which is the emulator's timer granularity and not this change.

### The build

giac_typed.o has to be compiled under Giac's own defines, not just with its headers on the path.
Several of them change the layout of gen, and a mismatch there is not a compile error, it is a value
that arrives wrong at the boundary. The Makefile asks Makefile.ki for them the same way it asks for
the object list:

```
-DHAVE_CONFIG_H -DIN_GIAC -DTIMEOUT -DNSPIRE_NEWLIB -DKHICAS -DNO_PHYSICAL_CONSTANTS
-DSTATIC_BUILTIN_LEXER_FUNCTIONS -DGIAC_BINARY_ARCHIVE -DNO_UNARY_FUNCTION_COMPOSE
-DNO_STDEXCEPT -D_GNU_SOURCE
```

nps_nspire.luax.tns grew from 3863312 to 3867041 bytes, which is 3729 for the conversion both ways plus the
differential check.

One thing to know if this file is edited: the include of giac_typed.h in lua_module.cc sits with the
other includes at the top, not down beside the class that uses it. An include inside the anonymous
namespace there nests namespace stepcas inside it, and the names are present but unreachable.

## The build is CMake and Ninja, 2026-09-03

Section 5.2 of the pack asks for it, and asks for the toolchain file to be checked in and proven from
a clean environment. nps/CMakeLists.txt and nps/cmake/toolchains/ndless-arm926ej-s.cmake.
STATUS.md carries the outcome and the byte comparison; this is the part that would cost the next
person an hour to work out again.

### What the Ndless toolchain file needs that a normal one does not

**Two different values for PATH, on purpose.** nspire-g++ is a shell wrapper that runs
`nspire-tools path` to find the SDK, so nspire-tools has to be on PATH whenever the compiler runs,
and that is two different processes. Configure time is this process and its try_compile children,
fixed with `set(ENV{PATH} ...)`. Build time is ninja, started later, inheriting nothing, fixed with
`CMAKE_CXX_COMPILER_LAUNCHER` set to `cmake -E env PATH=...`.

The two values differ and that is the whole point. Configure time keeps the caller's PATH, because
CMake also searches it for ninja, and truncating it there fails with "unable to find a build program
corresponding to Ninja". The launcher must not keep it, because the launcher's arguments are part of
every compile command ninja records, so folding `$ENV{PATH}` in there rebuilds all seventeen edges
whenever a shell with a different PATH runs the build. Both of those were observed, in that order.

**CMAKE_TRY_COMPILE_TARGET_TYPE is not needed here, which is worth writing down.** The usual advice
for an embedded toolchain is to set it to STATIC_LIBRARY, because the compiler check cannot link a
bare executable. nspire-gcc supplies the Ndless crt itself, so it can: configured without the
setting, ABI detection links and CMake reports the compiler check skipped. The line was in the first
draft on the strength of the usual advice and came out again after the experiment, since a
configuration nobody has shown to do anything reads as understanding and is not.

**CMAKE_AR and CMAKE_RANLIB do have to be named, though nothing archives yet.** The binutils here are
prefixed (arm-none-eabi-ar), and configured without them CMake caches /usr/bin/ar and
/usr/bin/ranlib, the host's. Every target in this build is an object library or a custom command, so
no archive is ever created and the host tool is never run. They are set so that a static library
added later is archived by the cross ar rather than silently by the host's.

**Giac's object list and defines are queried at configure time.** The Makefile asked its sub-make for
both at build time, which is the property worth keeping: a source added or a flag changed over in
khi-src cannot leave this linking a stale set or reading Giac's headers under different defines from
the ones Giac was compiled with. CMake does it with `execute_process` plus
`CMAKE_CONFIGURE_DEPENDS` on Makefile.ki, so editing that file re-runs the queries.

### What genzehn does not need

The old recipe redirected genzehn's stdin from /dev/null. genzehn reads no stdin
(ndless-sdk/tools/genzehn/genzehn.cpp has no cin, getline or scanf), so the redirect is gone rather
than carried across, and the packaged bytes are identical either way.

## Not done here

The handheld is off the USB bus and needs a restart by hand, so none of this is verified on real
hardware. Everything above is a claim about the emulator.

Memory with everything resident is still unmeasured, and V4 is where that measurement gets easier:
one image rather than two, so PERF-011's with-everything-resident reading is one number about one
program. The Lua heap figures on the measurement line are Lua's own and are not it.

The typed adapter at Giac's gen level is now reachable and was not before. With Giac linked in, the
adapter could hand over a giac::gen rather than a command string, which removes the print and
reparse on both sides of every backend call and is what the CartesianVector idea in the vector work
needs. It is an open item rather than a plan.
