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

Adding a translation unit to the physics source list, such as `src/physics/vector_cross.cc` or
`src/physics/scalar_product.cc`, changes which rules compile into the Compiled stage but does not
change what any of the six stages proves. The new family still needs its own Packaged, Transferred,
Loaded and Visible effect evidence before a device-visible claim can be made about it.

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

`source`: read on 2026-10-04, the table registers `caseval`, `canonical`, `solve`, `differentiate`,
`integrate`, `kinematics`, `catch_up`, `planar_kinematics`, `relative_motion`, `forces`, `density`,
`optics`, `unit_conversion` (lua_module.cc:5084), `gravitation`, `oscillation` and `wave`
(lua_module.cc:5087 to 5089), `pressure`, `hydrostatic`, `buoyancy`, `continuity`, `sensible_heat`,
`latent_heat` and `ideal_gas` (lua_module.cc:5090 to 5096), `modern` and `relativity`
(lua_module.cc:5097 and 5098), `vector_addition` (lua_module.cc:5099), `vector_cross`
(lua_module.cc:5100), `scalar_product` (lua_module.cc:5101),
`components_to_magnitude_angle`, `magnitude_angle_to_components`, `math_display`, `giac`,
`export_text` (lua_module.cc:5055), and a set of platform entry points for memory, tracing, integrity
and the OS dialogs.

scalar_product takes two vector strings and an optional third argument naming the angle unit. Left
out, it answers the product alone. Source, read on 2026-10-04: l_scalar_product at
lua_module.cc:4232 passes Giac to the engine only when GiacBackend::available says it is there, so the
split build without luagiac places the angle from the product's sign and the unified build also
measures it. nps/tests/target/luax_host.lua asserts both records.

Every record that carries a precision also says what it knows about an uncertainty. `source`, read on
2026-10-04: `set_precision` at lua_module.cc:1083 writes `kind`, `significant_digits`,
`last_significant_decimal_place` and `uncertainty_state` (lua_module.cc:1090), the last of them from
`uncertainty_state_name`, so a shell sees one of none, known, unstated, not propagated or too large on
every record rather than an absent field. `density` is the only binding that reads a stated
uncertainty out of its givens, through `parse_quantity_with_uncertainty` at lua_module.cc:3635 and
3641. Every other binding keeps `parse_quantity`, which refuses the text rather than dropping the
uncertainty, and nps/tests/target/luax_host.lua pins that refusal on `optics` beside a control that
the same binding still reads the quantity written without one.

`density` is also the only binding that reports one back. `source`: read on 2026-10-04, `l_density`
copies the engine's already checked `uncertainty_text` into an `uncertainty` field and splices the
same text into the answer line between the value and the unit, at lua_module.cc:3660 to 3665, so a
record reads `mass = 2.00 +/- 0.11 kg`. Both are omitted when that text is empty, which is every
state but known, and the bridge computes no uncertainty of its own. The shell has nothing to assemble
and names the reason for an absent one from `uncertainty_state` instead, at nps_v4.lua:3275.

All five of those states are reachable through that one binding, so none of them is a shell-only
spelling. measured on 2026-10-04, with env -u NPS_EVIDENCE luajit over build/host nps_split: givens
with no spread answer none, two stated spreads answer known, a measured given beside a stated one
answers unstated, a stated spread of zero answers not propagated, and a variance that outgrows the
exact arithmetic answers too large while the derivation still solves and verifies.
nps/tests/target/luax_host.lua drives all five from Lua rather than writing a state into a record.

`export_text` is the only entry that writes a file for the shell, because the shell's Lua has no io
library. `source`: read on 2026-10-04, it writes `/documents/ndl/<name>.txt.tns` for a name of 1 to 32
lower case letters, digits, `-` or `_` and at most 64 KiB of text, at lua_module.cc:4957, and
nps/tests/target/luax_host.cc points it at a host directory for the bridge tests.

`gravitation`, `oscillation`, `wave` and the seven PHYS-018 relations share one binding,
`relation_into` at lua_module.cc:3824, which reads the variable names against the model's own term
names (lua_module.cc:3810). Any engine built on
`RelationModel` can be exposed the same way with a one-line binding.

`modern` (lua_module.cc:3970) and `relativity` (lua_module.cc:4062) read their relation and variable
names against the engine's own name functions, so a caller passes "Time dilation" or "proper time"
exactly as the derivation prints them. Both families work in units the unit table does not carry (eV,
nm, MeV, u and fractions of c), so `declared_quantity` at lua_module.cc:3928 attaches the declared unit
to a bare number or to the number written with that unit, and leaves any other unit for the engine to
refuse.

`source`: read on 2026-10-04, `judge_attempt` is registered at lua_module.cc:5051 and defined at
lua_module.cc:1840. It takes the state, the attempt, the later route states and the variable, and
returns a verdict table without reading or writing any derivation.

`source`: read on 2026-10-04, `rule_definition` and `unit_definition` are registered at
lua_module.cc:5053-5054 and defined at lua_module.cc:1939 and 1977. The first reads `rule_schema`
and the second reads the unit table and `quantity_name`, so neither carries prose of its own.

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

A command family typed as text needs no `lib[]` entry of its own, because it arrives through the
`walkthrough` entry and `parse_command` picks the engine. `source`: `walkthrough` is registered at
lua_module.cc:5056, and `l_walkthrough` sends a separable `desolve` command to `separable_into` at
lua_module.cc:3364-3365 and a `linsolve` command to `system_into` at lua_module.cc:3361-3362. Its
menu entry is still needed. A desolve that solve_separable reports as unsupported or refused returns
nil at lua_module.cc:3274-3278, so the shell's walkthrough branch falls back to Giac at
nps_v4.lua:4324. A linsolve never
does: system_into answers every outcome with a table at lua_module.cc:2854-2902, and l_walkthrough
answers a malformed linsolve or decimal mode with a refusal table at lua_module.cc:3327-3340. Outside
exact mode, 4 equations, 5 unknowns (system.h:15-16) and rational coefficients the shell shows a
native refusal and Giac is not consulted.

`normal` and `partfrac` behave like linsolve. `source`: `l_walkthrough` sends both to `rational_into`
at lua_module.cc:3383-3386, which answers every outcome with a table at lua_module.cc:2804-2852. A
second symbol, a degree above 12, or a partfrac denominator with a repeated or irreducible factor shows
a native refusal and Giac is not consulted.

`powsimp` behaves the same way. `source`: `l_walkthrough` sends it to `power_into` at
lua_module.cc:3381-3382, which answers every outcome with a table at lua_module.cc:2658-2703 and
carries the derivation's active assumptions as the `assumptions` field. A second variable, a decimal,
a root of a sum or decimal mode shows a native refusal and Giac is not consulted.

`texpand` and `tcollect` do too. `source`: `l_walkthrough` sends both to `trig_into` at
lua_module.cc:3379-3380, which answers every outcome with a table at lua_module.cc:2759-2802. A constant
inside an angle, tan, a variable outside sin or cos, or a tcollect product of different angles shows a
native refusal and Giac is not consulted.

`bisect`, `newtonroot`, `trapsum` and `simpsum` do as well. `source`: `l_walkthrough` sends them to
`numeric_into` at lua_module.cc:3375-3376, which answers every outcome with a table at
lua_module.cc:2609-2656 and adds `error_bound`, `bound_certified` and `iterations`. A function that is
not a polynomial, a second variable or decimal mode shows a native refusal and Giac is not consulted.

## What the shell can reach

<!-- covers: nps/lua/nps_v4.lua -->

`source`: read on 2026-10-04, nps/lua/nps_v4.lua's guided physics browser (`PHYSICS_FIXTURES`) names
`buoyancy`, `catch_up`, `continuity`, `density`, `forces`, `gravitation`, `hydrostatic`, `ideal_gas`,
`kinematics`, `latent_heat`, `magnitude_angle_to_components`, `modern`, `optics`, `oscillation`,
`planar_kinematics`, `pressure`, `relative_motion`, `relativity`, `scalar_product`, `sensible_heat`,
`unit_conversion`, `vector_addition`, `vector_cross`, `wave` and `work`. Every relation the `modern`
and `relativity` bindings carry has a fixture, the Lorentz transformation included.

`density` has two fixtures. The second, at nps/lua/nps_v4.lua:3106-3116, states an uncertainty on both
givens and is PHYS-020's first worked example. It is last in the list on purpose: the smoke test walks
the browser by counting arrow presses, so a fixture inserted anywhere else renumbers every entry after
it. Source, read on 2026-10-04: the family id is the same `physics.density.mass-volume` the first
fixture already needs, declared at nps/src/core/capability_manifest.cc:59 and required of the loaded
module at nps/lua/nps_v4.lua:57, so this entry added no manifest row.

When a record's `precision.uncertainty_state` is unstated, not propagated or too large, the summary
draws one line under SPREAD saying why no uncertainty is given. `source`, read on 2026-10-04:
`uncertaintyNote` at nps/lua/nps_v4.lua:3275-3286 maps the three states to their reasons and answers
nil for none and known, and the summary draws it at nps/lua/nps_v4.lua:4736-4744, inside the block
that hint mode withholds along with the result. nps/tests/target/ui_smoke_v4.lua drives both halves,
the line on an unstated record and its absence on a known one.

The scalar_product fixture at nps/lua/nps_v4.lua:2736-2744 asks for the angle in degrees. Source,
read on 2026-10-04: the bridge writes the angle's placement, and its measured size when Giac answered,
into the record's interpretation field, which the summary draws under MEANING at nps/lua/nps_v4.lua:4755
with no code of its own for this family. The family id is declared at
nps/src/core/capability_manifest.cc:62 and required of the loaded module at nps/lua/nps_v4.lua:60.

`planar_kinematics` is now reachable from that menu, as two fixtures rather than one. The binding is a
single entry point and the family is chosen by an optional flag, so one menu entry per family is what
makes both of them reachable.

`source`: read on 2026-10-04. nps/lua/nps_v4.lua:2909 sends projectile true for the thrown ball, and
the problem table at nps/lua/nps_v4.lua:2920-2927 carries no projectile key at all, which is how the
ball in a sideways wind reaches the general family.
nps/src/physics/planar_kinematics.cc:169 reads that flag and reports either
physics.kinematics.constant-acceleration.projectile.two-dimension or
physics.kinematics.constant-acceleration.two-dimension. Both ids are declared at
nps/src/core/capability_manifest.cc:55-56 and required of the loaded module at
nps/lua/nps_v4.lua:85-86, so a build missing either one refuses to start rather than offering a
menu entry that cannot run.

`judge_attempt` is reachable from the entry line and from the Steps menu entry that types `!a`:
`source`, nps/lua/nps_v4.lua:3147 routes `!a` to the attempt mode and attemptFeedback at
nps/lua/nps_v4.lua:4205 calls the binding with the last revealed state and the rest of the route.

`export_text` is reachable the same way, from the entry line and from the Steps menu entry that types
`!x`: `source`, read on 2026-10-04, nps/lua/nps_v4.lua:2355 is that menu entry, nps/lua/nps_v4.lua:3154
routes `!x` to the export mode, and runSteps at nps/lua/nps_v4.lua:4261 composes the text with
derivationExportText and calls the binding under pcall.

Definitions are reachable from an open walkthrough: `source`, read on 2026-10-04, the `d` and `D`
reader at nps/lua/nps_v4.lua:5553 and the Actions entry at nps/lua/nps_v4.lua:2367 both open
definitionParagraphs (nps/lua/nps_v4.lua:5130) for the focused step, and `!u` at
nps/lua/nps_v4.lua:3146 defines a unit.

`position_motion` and `ranking` still have working
engines on main with no binding and no menu entry: `source`, neither name appears in nps/lua/nps_v4.lua or
nps/src/platform/nspire/lua_module.cc on 2026-09-24. #382 asked for all three and closed through #405
with only `planar_kinematics` wired, so no open issue tracks the other two. When either is wired, this
paragraph is wrong and has to change with it.

Nothing in that menu is reachable when the loaded module's manifest lists more than 128 modules: `source`,
manifestCompatibility refuses it as malformed at nps/lua/nps_v4.lua:125 and every StepCAS surface stays
off. The build fails first, at nps/src/core/capability_manifest.cc:98, if the compiled manifest outgrows
that ceiling, so a new family raises both numbers together.

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

**`D2Editor` rich text**, the typeset path. mathBox builds it at nps/lua/nps_v4.lua:3545, measureMath
sets the expression at :3579, and the history editor sets its expression at :1472.

    local box = D2Editor.newRichText()
    box:setExpression("\\0el {" .. expr .. "}", 0)
    box:setReadOnly(true):setBorder(0):setFocus(false)
    box:setSizeChangeListener(function(editor, w, h) ... end)

It stacks a fraction under a vinculum, raises a superscript, draws a radical and italicizes unit vectors.
Inside an expression a Unicode subscript renders and an underscore does not, so write `v₀*t` rather than
`v_0*t`.

**Every slash inside a box becomes a fraction bar.** `measured` on 2026-10-05 against OS 6.4.0.74 under
headless Firebird, driving the guided density entry that states an uncertainty. The answer mass = 2.00
+/- 0.11 kg drew as mass = 2.00 + followed by a fraction with an empty numerator over -0.11, then kg.
Rewriting that three character spelling to the plus-minus sign drew the same answer on one line with
its unit intact. `source`, read on 2026-10-05: the rewrite sits in mathModeExpression at
nps/lua/nps_v4.lua:1183, which is the only code in the shell that builds the wrapper above, so it runs
where a string becomes mathematics rather than in one caller. Both setters named above reach it, the
step and answer boxes through measureMath and the history row through fitHistory, and so does any box
added later. The plain text views build no wrapper and keep the engine's spelling, which
`gc:drawString` has no fraction to build out of.

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

<!-- covers: .github/workflows/check.yml, nps/CMakeLists.txt, nps/tools/device_evidence.cc -->

`nps_luax` is the only host target that compiles the bridge, and it configures only when luajit and its
headers are both present.

`source`: nps/CMakeLists.txt:1362 guards it with `if(LUAJIT_EXECUTABLE AND LUAJIT_FOUND)`. The other two
targets that compile lua_module.cc, `nps_split_module` at line 748 and `nps_nspire_module` at line 954,
are in the device branch behind the ARM toolchain.

Search for the quoted text rather than trusting the number. These three drift by a couple of lines
every time a family adds a source or test entry above them, which is what a line citation does as
soon as anything above it moves. The quoted symbol is the durable half of the reference and the
number is the convenience.

`measured`: a host configure without luajit produces 1281 targets, none of them `nps_luax`, and
lua_module.cc appears in the build graph only as a phony source node rather than a compile rule.

So on a machine or runner without luajit, **a change to the Lua bridge is never compiled**. Check
`ninja -t targets all | grep nps_luax` before believing a green build.

Two rows need a device build that a host configure never produces. device_evidence sweeps the device
build tree for offline audit and device run records, and report_size sizes the ARM image. Without one
they both report Skipped, so the absent device stage stays visible rather than reading as coverage of
the device requirements.

`source`: nps/CMakeLists.txt sets SKIP_RETURN_CODE 77 on device_evidence, which is what the sweep in
tools/device_evidence.cc returns when the directory holds no records, and the branch beside report_size
registers a row whose echoed text is the literal its SKIP_REGULAR_EXPRESSION matches.

The sweep answers 0 when every record was ingested, 1 when the gate refused one, 3 when a record was
accepted and its rows could not be appended to the evidence file, and 77 when there was nothing to
sweep. Only 77 is a skip, so an unwritable evidence file reports the row as Failed rather than hiding
behind the refusal count.

`measured` on 2026-09-25: with no device tree, ctest -R '^(device_evidence|report_size)$' reports two
Skipped rows and exits 0. Both Pass instead when the ndl SDK is present and NPS_DEVICE_BUILD_DIR names
a tree holding nps_nspire.elf, nps_nspire.offline-audit.txt and the nps_nspire.luax.tns that record's
digest is checked against. Stage only the audit record and device_evidence refuses it for a missing
artifact rather than passing. Sweeping that same tree with NPS_EVIDENCE naming a path under a
directory that does not exist reports 0 refused, 1 not written and exits 3.

What gates those suites is a separate question from what they compile. `full` and `emulator` wait on
`fast` alone. They used to wait on `review-ready` as well, which was right while an approving review
was required to merge.

`measured` on 2026-09-16: reviewers still run and still approve, so the reason is not that a verdict
never arrives. Pull request 406 was approved by `cx2-ag-claude-review-echo`. What changed is the
ruleset, id 23082438, which now requires zero approving reviews and names `fast` as its only required
status check. A branch therefore merges before `review-ready` resolves, so the two suites behind it
never produced evidence for anything. `review-ready` fails rather than hanging, in about three minutes,
at wait-for-review.mjs:252.

`source`: .github/workflows/check.yml gives both jobs `needs: [fast]`, and
.github/scripts/test-ci-scheduling.rb asserts exactly that pair rather than trusting it. Whoever
restores the approving review to the merge gate has to put `review-ready` back into both lists and
into that assertion, or the suites will start again without waiting for the verdict that gate exists
to collect.
