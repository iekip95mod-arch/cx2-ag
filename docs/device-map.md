# Device map

What the calculator is, how code reaches it, and what it can do once there. [docs/codebase-map.md](codebase-map.md)
maps the repository. This maps the target, because an agent that has read every source file still cannot
say whether a change will render on a handheld, and the answer is not in the sources.

Read the section you need. Every section names the files it describes on a `covers` line, which
`.github/scripts/device-map-drift.mjs` reads: a pull request that changes a covered file and leaves this
document alone fails the fast gate. That check exists because a map nobody updates is worse than no map,
since the next agent believes it.

Each claim below says how it is known. `source` means it was read out of a file in this tree and the
file and line are given. `measured` means somebody ran it against hardware or a runner and the command is
given. A claim with neither is not a claim and does not belong here.

## What the target is

<!-- covers: tools/nsptool -->

`measured` on 2026-09-15 with `./tools/nsptool/nsptool.new info`:

    name          TI-Nspire CX II
    hw type       Nspire CX II non-CAS (0x1d)
    os version    6.40.74
    boot1         5.0.42
    boot2         6.20.7
    storage       90957824 free of 96862208
    ram           32037420 free of 36082816
    lcd           320x240 16bpp

Non-CAS matters: a question that needs symbolic algebra has to be answered by StepCAS or Giac rather
than by the built-in OS, because this hardware has no CAS to fall back on.

The OS reports as 6.40.74 through nsptool, which is the same build TI writes as 6.4.0.74. An earlier
reading of 6.2.0.333 recorded elsewhere was wrong.

The free figures move between runs, so treat them as an order of magnitude rather than a constant. Two
readings minutes apart in one session differed by two megabytes of storage.

Run `info` rather than trusting any of the above. A handheld can be reflashed, and every claim in this
file about what renders was measured against this one.

## The verification ladder

<!-- covers: nps/CMakeLists.txt -->

Six stages, and each one is a separate fact. Most wrong claims in this repository come from reporting a
lower stage as if it were a higher one.

| Stage | What it proves | What it does not |
| --- | --- | --- |
| Compiled | The translation unit is well formed | Nothing about linking |
| Linked | Symbols resolve | Nothing about the package |
| Packaged | A `.tns` exists with the right bytes | Nothing about loading |
| Transferred | The file is on the device | Nothing about it being read |
| Loaded | The runtime accepted the module | Nothing about behavior |
| Visible effect | A person looking at the screen sees it | The only stage that settles a UI claim |

`source`: this ladder is the contract AGENTS.md states under Contracts to preserve, worded there as
"A compiled object, linked ELF, packaged TNS, loaded module and visible device effect are separate
verification stages."

The gap that keeps biting: a host build does not compile every file. See the bridge section.

## Getting something onto the device

<!-- covers: tools/nsptool, vendor/ndl-src/ndl-sdk/tools/luna -->

Packaging a Lua document and sending it:

    ./vendor/ndl-src/ndl-sdk/tools/luna/luna nps/lua/ti_voc.lua out/ti_voc.tns
    ./tools/nsptool/nsptool.new put out/ti_voc.tns /PyLib/ti_voc.tns
    ./tools/nsptool/nsptool.new ls /PyLib
    ./tools/nsptool/nsptool.new screenshot out/screen.png

`measured`: all four ran against the handheld on 2026-09-15 while deploying `ti_voc.tns`. `put` reported
the byte count, `ls` listed the file, and `screenshot` wrote a 320x240 16bpp PNG.

`nsptool.new` is the locally built binary. A system-installed `nsptool` can lack commands the local
source has, so invoke the path rather than the name.

Screen geometry is **320 by 240, 16 bits per pixel**. `measured`: every `nsptool screenshot` in that
session reported `screenshot 320x240 16bpp`.

Sending keys is a separate mechanism from seeing them take effect. `key-os` and `type-os` report
`transport acknowledged N of N, visible effect unverified`, and that wording is honest: `measured` on
2026-09-15 an `enter` was acknowledged and the screen had not changed when the next screenshot was taken,
then the same key worked on a second attempt. Take a screenshot to confirm, and do not treat an
acknowledged transport as a visible effect.

## How the runtime finds a module

<!-- covers: vendor/ndl-src/ndl/src/resources/luaext.c -->

`nrequire` matches on the **bare basename at any depth**, not on a path.

`source`: vendor/ndl-src/ndl/src/resources/luaext.c:42 compares
`strcmp(strrchr(path, '/') + 1, state->filename)`, which takes everything after the last `/` and tests it
against the requested name. The registration is at luaext.c:86.

The consequence is a real hazard rather than a curiosity: two files with the same basename in different
directories are indistinguishable to a require, and which one wins depends on enumeration order. If a
module appears to be stale after a deploy, look for a second copy before looking at the code.

## The Lua bridge surface

<!-- covers: nps/src/platform/nspire/lua_module.cc -->

The bridge is one long anonymous namespace ending in a `lib[]` table of name to function pairs. Only what
that table lists is callable from Lua.

`source`: read on 2026-09-15, the table registers `caseval`, `canonical`, `solve`, `differentiate`,
`integrate`, `kinematics`, `catch_up`, `planar_kinematics`, `relative_motion`, `forces`, `density`,
`optics`, `unit_conversion` (lua_module.cc:3994), `vector_addition` (lua_module.cc:3997),
`components_to_magnitude_angle`, `magnitude_angle_to_components`, `math_display`, `giac`, and a set of
platform entry points for memory, tracing, integrity and the OS dialogs.

Two things an agent adding a binding needs to know, both learned from a review that caught them:

- **Helpers must be defined above the binding that calls them.** The namespace is compiled top to bottom
  and C++ has no implicit declaration. `measured`: on branch `claude/issue-364` before its fix, building
  `nps_luax` failed with `use of undeclared identifier 'optional_name'` at lua_module.cc:3380, 3444 and
  3451 against definitions at 3525 and 3537.
- **`set_field` has `bool` and `int` overloads and no `double`.** `measured`: on branch
  `claude/issue-279` before its fix, `set_field(L, "rank", static_cast<double>(value.rank))` at
  lua_module.cc:3265 failed as an ambiguous call.

**An engine existing is not the same as it being callable, and being callable is not the same as being
reachable.** A family needs three separate things: the engine, a `lib[]` entry, and a menu entry.

## What the shell can reach

<!-- covers: nps/lua/nps_v4.lua -->

`source`: read on 2026-09-15, nps/lua/nps_v4.lua names `catch_up`, `density`, `forces`, `kinematics`,
`optics`, `relative_motion`, `unit_conversion` and `vector_addition`, and nothing else.

So `planar_kinematics` is bound and not reachable, and `position_motion` and `ranking` are neither bound
nor reachable, despite all three having working engines on main. That gap is #382. When it closes, this
paragraph is wrong and has to change with it.

## What renders on screen

<!-- covers: nps/lua/nps_v4.lua -->

Two drawing paths with different capabilities.

**`gc:drawString` in the sansserif face.** `measured` against a CX II on 2026-09-15 by drawing candidate
glyphs and reading the screenshot back:

- Renders: subscript zero as in `v₀`, superscripts `²` and `³`, Greek letters, `√`, `∫`, `∑`, the
  relations `≤ ≥ ≠ ≈`, `·`, `×`, `°`, `±`, and the unit vectors `î` and `ĵ`.
- Fails: combining diacritics, so `k̂` written as k plus a combining circumflex comes out as a missing
  glyph. Write it plainly instead.
- Fails: letter subscripts. `vₓ` and `vᵧ` do not render. Write `vx` and `vy`.

**`D2Editor` rich text**, the typeset path, used at nps/lua/nps_v4.lua:1435.

    local box = D2Editor.newRichText()
    box:setExpression("\\0el {" .. expr .. "}", 0)
    box:setReadOnly(true):setBorder(0):setFocus(false)
    box:setSizeChangeListener(function(editor, w, h) ... end)

It stacks a fraction under a vinculum, raises a superscript, draws a radical and italicizes unit vectors.
Inside an expression a Unicode subscript renders and an underscore does not, so write `v₀*t` rather than
`v_0*t`.

**A box smaller than its content draws nothing at all.** Not clipped, not truncated: blank. `measured`
on hardware while building ti_info. The height has to come from the size-change listener, which means an
unmeasured formula needs a provisional height and a plain-text spelling until the listener has run.

## Scrolling a page that contains typeset rows

<!-- covers: nps/lua/ti_info.lua -->

A page of uniform text rows can compute its scroll limit by counting rows. A page containing D2Editor
boxes cannot, because a typeset formula is about three text rows tall.

`measured`: a walk over the ti_info reference document on branch ti-info-app found the bottom of every
card carrying a formula unreachable. The fix is to fill the viewport backwards from the last row,
accumulating real heights, rather than dividing a total by a row height.

`measured` on 2026-09-15 with `luajit nps/tests/target/ti_info_cards.lua`, run from `nps`: replacing
`clamp()`'s backward fill at nps/lua/ti_info.lua:1924 with the uniform-row division it replaced,
`limit = #list - math.floor(viewport / L)`, fails the card walk with `card 'Magnitude and angle, or
back again' has no STEPS`. The last section of a card carrying a formula becomes unreachable, which is
the defect this section describes. Restoring the backward fill passes it again.

`ti_info_cards.lua` is the regression that holds this claim. `ti_info_search.lua` does not: it stays
green under that same mutation, because its below-the-fold case searches a plain-text topic where
every row is the uniform height and the backward fill and the division agree. That test guards the
search landing scroll instead, which is a different fix.

A third regression, `ti_info_numbers.lua`, holds the arithmetic rather than the layout. It recomputes
each worked example from the inputs that example prints and requires the printed intermediates to
agree, so a transcription slip in a number cannot pass while the boxed answer still looks right. It
also holds the sign convention, that g is a magnitude and the sign lives on a.

`measured` on 2026-09-16 with `luajit nps/tests/target/ti_info_numbers.lua`, run from `nps`: against
the content before this branch it fails with `the cliff example prints vf = 27.3 but its own vfx = 21.0
and vfy = -12.5 give 24.44`, which is the defect it was written for. Adding a row to a card also
changes that card's height, so the backward fill above is what keeps the new row reachable.

## Where files live on the device

<!-- covers: tools/nsptool -->

`measured` on 2026-09-15 with `nsptool ls`: `/PyLib` holds the TI-supplied libraries `ti_hub.tns`,
`ti_image.tns`, `ti_plotlib.tns`, `ti_rover.tns`, `ti_system.tns`, alongside the documents deployed there.

A transferred document that is already open is not reloaded by writing over it. Reopening it from the
Recent list picks up the new bytes. `measured` while iterating on ti_info and ti_voc.

Native calculator filenames and transfer names are different namespaces, so a name that works in one
place is not evidence for the other.

## What the host build does not cover

<!-- covers: .github/workflows/check.yml, nps/CMakeLists.txt -->

`nps_luax` is the only host target that compiles the bridge, and it configures only when luajit and its
headers are both present.

`source`: nps/CMakeLists.txt:1322 guards it with `if(LUAJIT_EXECUTABLE AND LUAJIT_FOUND)`. The other two
targets that compile lua_module.cc, `nps_split_module` at line 715 and `nps_nspire_module` at line 921,
are in the device branch behind the ARM toolchain.

Search for the quoted text rather than trusting the number. These three were already four lines stale
when this file was first reviewed, which is what a line citation does as soon as anything above it
moves. The quoted symbol is the durable half of the reference and the number is the convenience.

`measured`: a host configure without luajit produces 1281 targets, none of them `nps_luax`, and
lua_module.cc appears in the build graph only as a phony source node rather than a compile rule.

So on a machine or runner without luajit, **a change to the Lua bridge is never compiled**. Check
`ninja -t targets all | grep nps_luax` before believing a green build.

What gates those suites is a separate question from what they compile. `full` and `emulator` wait on
`fast` alone. They used to wait on `review-ready` as well, which was right while an approving review
was required to merge and became a deadlock when the maintainer removed that requirement for a
delivery window, because `review-ready` waits for a verdict that no longer has to arrive.

`source`: .github/workflows/check.yml gives both jobs `needs: [fast]`, and
.github/scripts/test-ci-scheduling.rb asserts exactly that pair rather than trusting it. Whoever
restores the approving review to the merge gate has to put `review-ready` back into both lists and
into that assertion, or the suites will start again without waiting for the verdict that gate exists
to collect.
