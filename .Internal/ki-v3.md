# Ki V3: the Ki V1 shell with step-by-step modes

This is the retained Ki V3 implementation history. Current Ki V4 behavior is documented in [nps/README.md](../nps/README.md#native-contracts).

Started 2026-09-03. Ki V1 is the KhiCAS Lua document on our giac 1.9 build (ki-port.md). Ki V2 was
a separate small document that drove the stepcas module and nothing else (ki-v2.md). Ki V3 is the
decision that the product is one document: the Ki V1 shell, every feature it had, with the
step-by-step derivations as a mode inside it rather than a second document beside it.

## What the base is, and why that file

`nps/lua/nps_v3.lua` starts as a byte for byte copy of `khicas53/khicas.lua`, then hooks in at
three marked places and appends one section. That file, not `khi-src/src/khicas.lua`, because the
two differ only by additions and the additions are Ki V1's: diffed on 2026-09-03, khi-src's copy
is giac 1.9's document with the header renamed to Ki, and khicas53's is that plus the version
label read from Giac at first paint, the banner removed and the host channel probe behind
`CHAN_PROBE = false`, which is what ki-port.md records as khicaslua4 and khicaslua-ver. A luna
build of khicas53/khicas.lua is byte identical to khicas53/khicaslua-ver.tns.

One loose end: the ki_v1.tns on the handheld is 12179 bytes, and no Lua file on this machine
builds to that (khicas53's gives 11866, khi-src's 10902). A slightly later revision may exist only
on the calculator. It was fetched to nps/build/scratch/ki_v1_handheld.tns and not decoded.

## What Ki V3 adds

Everything KhiCAS had stays: the 2D math boxes for input and history, the command menus, font
size, borders and lines, copy and paste, `?command` help, the shell and script editor, save and
read variables, restart and clear. On top of that:

- **Step requests.** `!d expr`, `!i expr`, `!s equation`, `!k problem` send the text after the
  prefix to the stepcas module; `!v name` changes the variable, `!?` lists these. A mode chosen from the Steps
  menu makes every line a request until "plain Giac again". The answer goes into the history's
  result box like a Giac answer, so it can be reused the way KhiCAS reuses one, and the derivation
  opens in the viewer.
- **The viewer** (PRD section 9, UI-003, STEP-009, STEP-010, MATH-005, UI-013). A list of the
  steps with one focused; enter opens the focused step on its own with its goal, reason, the
  expression it started from and the one it produced; shift+tab switches between the standard and
  beginner levels, and beginner adds the fuller explanation, the rule's domain restriction, what
  checked it and the rule id. Left and right move between open steps, up and down scroll a long
  one. The header carries the answer, the outcome, the trust label, the answer's assumptions and
  the measurement line. Esc closes the step, then the viewer.
- **Trust labels** are per result and name what Giac actually did, since a cross-check that is not
  Giac answering the same question must not read as one. "Giac agrees" for a derivative or a
  solution, "Giac's derivative agrees" for an integral, because the integral's cross-check is Giac
  differentiating our answer rather than integrating for itself (see below), and "Giac's own solve
  agrees" for a kinematics answer, where Giac solves the substituted equation independently. The
  label comes from a table keyed on the method the bridge reports, so a method nobody has named
  falls back to plain Giac rather than claiming a check that did not run.
- **Save and reopen** (UI-010). `on.save` keeps the variable, the mode, the detail level and the
  text of every history entry; `on.restore` type checks and ranges each field and replays the
  history through addME on the next paint, which is when the view exists. KhiCAS itself kept
  nothing across a reopen.
- **The tool palette** gains a Physics group in front of KhiCAS's own. The OS validates the palette
  as it is registered and every entry has to be a function at that moment, which is why entries that
  name functions defined later in the file are wrapped in closures: the first build reset to
  "expected function in menu item 6 of tool box 1" on the emulator. The solver controls are not in
  the palette at all. They are prefixes on the input line, so the editor stays the one place a
  derivation is asked for.

What it does not do: the step expressions in the viewer are plain text, not 2D (MATH-003 is met
for the input and the history through KhiCAS's editors, not for the derivation).

## How the shell and the viewer share the screen

The history is D2Editor objects, OS widgets drawn over the Lua canvas. `setVisible(false)` alone
left them painted over the viewer on the emulator, so while the viewer is up they are moved to
-10000,-10000 the way `destroyD2Editor` parks one, and `reposME` puts them all back on close. The
input editor loses focus while the viewer is open so keys reach the `on.*` handlers, and gets it
back on close with `forcefocus`, which is the shell's own mechanism for the same thing.

Every `on.*` handler the shell defines is kept in `baseOn` and called when the viewer is closed,
so the shell's behaviour is untouched outside the viewer. Viewer handlers run under pcall: an
error closes the viewer and puts the message in the status line rather than unwinding into the OS.

## The integral's cross-check

The bridge (`stepcas.integrate`) asks Giac to differentiate our particular antiderivative and then
whether that minus the integrand simplifies to zero. Comparing antiderivatives instead would report
a disagreement for every answer that differs by a constant, and for ln(x) against Giac's
ln(abs(x)), neither of which is a wrong answer, and it would leave VER-005's derivative check with
no second opinion. So the module's own check (differentiate by rule, compare canonical forms) and
Giac's are two independent derivatives of the same answer.

## Testing

`make -C nps test` runs `test/ui_smoke_v3.lua`, which loads the document against stubs for the
class library, D2Editor, the tool palette and the graphics context, and drives it: first paint, a
plain Giac line, a `!i` request, the viewer's keys, both detail levels, the menu modes, `!v`, save,
a bad restore and a good one. It also checks every palette entry is a function, the thing the
emulator caught. What the host cannot check is anything about the real widgets, which is what the
emulator and the handheld are for.

Emulator, 2026-09-03: luagiac.luax.tns (our 3814775 byte build) sent to /ndless over the link,
which took the two-extension name this time, nps_split.luax.tns replaced and verified by fetch and
cmp, ki_v3 opened, "Giac 1.9.0 : OK." drawn, `!i sin(2x)` typed through keysvc as one packet (the
`!` is the ctrl column of the bar key, 0x0421), enter: "integrated | Giac's derivative agrees",
10 ms, 4 steps, 2 Giac calls (probe/e10.png). The editors-over-the-viewer defect above is from
that run. After the fixes, in a fresh session: `!i 1/x` typed with the division key, which the
math box shows as a fraction and hands over as `1/x`, gave "(C + ln(x))", "Giac's derivative
agrees", "assumes: x > 0", 10 ms, 2 Giac calls, with the viewer alone on screen (probe/e22.png,
e28.png); enter opened step 1 of 5 (e30.png), shift+tab switched the level (e24.png), esc twice
returned to the shell with the 2D history entry and the status label (e11.png, e32.png); `!d x^2`
typed with the power key gave "(2 * x)", "Giac agrees", 0 ms (e26.png). The final build, in a
fresh session: `!s 2x+5=13` gave "4", "solved | Giac agrees", 0 ms, the four linear steps in the
list (e36.png), the plan opened as step 1 of 4 (e37.png), and two escs left the shell showing
"!s 2x+5=13" against "4" in the history with "Giac agrees" in the status corner (e38.png). The
flash image was saved with luagiac, the module and this document on it.

What the emulator could not check: the arrow keys. keysvc's arrow codes do nothing even on the
home screen (tooling.md), so the focus and scrolling were exercised on the host only. The viewer
takes both of the OS's arrow events with a debounce, since which one a press reaches is unmeasured.

Session hazard while doing this: a third Lua document that loads luagiac in one OS session never
finishes loading (busy clock, keysvc unanswered). flashsave, restart the emulator, then open the
next document.

## The kinematics mode

The !k prefix takes a problem rather than an expression: an unknown and a list of known quantities separated
by semicolons, commas or newlines, as in "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s". The unknown can
also be written "v = ?", and displacement answers to d as well as x. Spaces are optional everywhere,
which matters for testing, since keysvc cannot press an arrow and so cannot leave a 2D fraction once
the division key has opened one. A line whose only division sits at the end needs no arrow at all.

The units come back from the 2D math box with the fraction bracketed, as "5 ((m)/(s))" rather than
"5 m/s", so the unit scanner in src/units.cc reads brackets as grouping. That was found by the
Physics menu entries below, and the detail is in ki-v4.md.

A Physics group sits first in the tool palette, before Ki V1's own groups. Its entries follow Ki V1's
shape: each label is a kinematics line that runs as it stands, and choosing it puts that line in the
input editor, the way "factor(expr)" puts "factor(" there. One entry per unknown, plus a blank
template.

The viewer needed nothing new. A kinematics derivation holds two root plans, its own and the linear
solver's, and the viewer already renders by depth. The second argument runSteps passes is the current
variable, which kinematics ignores, since its unknown comes from the problem text.

Emulator, real Giac, fresh session, 2026-09-03 (probe/e45.png, probe/e46.png). Typed as one keysvc
run of "!kv=?,t=4s,x=44m,v0=5m/s" and enter: "v = 17 m/s", "solved | Giac's own solve agrees",
"assumes: acceleration is constant; motion is along one axis", 10 ms, 45 nodes, 8 steps, 4 Giac
calls, 506 KB Lua heap, 15 rows. The list showed the plan, the dimensional check with "L on both
sides", Giac's rearrangement as v = (((-t) * v0) + (2 * x)) * t^-1, and the substitution. Enter
opened step 1 of 8 with its goal and the reason for choosing x = (1/2)*(v0 + v)*t.

The debugger's ln svc takes a command line of at most 255 bytes. Twenty four key records in one
packet exceed it and are refused with no reply line at all, which reads exactly like keysvc being
absent. Twelve fit. keysvc itself reads up to 512 bytes, so the limit is the debugger's, not its.
