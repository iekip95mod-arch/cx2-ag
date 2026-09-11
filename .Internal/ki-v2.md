# Ki V2: the StepCAS native core

Started 2026-09-02. Ki V1 is the giac 1.9 cross build that runs on the calculator, in
[ki-port.md](ki-port.md). It is a CAS and nothing more. V2 is the first tree that carries StepCAS
itself. Its former stepcas directory is now [nps](../nps). The remaining design notes describe the September 2 implementation.

## What V2 is, and what decision it does not pre-empt

Open question 6 is undecided: Lua-first against native-core. PRD section 12.2 says a vertical
prototype of both settles it, not an argument. This tree is the native-core arm.

It is also the only arm that can answer section 12.3, which requires user input to become a
validated AST with enforced limits before an allowlisted set of typed operations touch it. Lua-first
would still need those checks, and it has nowhere to put them except the same native code, so
nothing here is wasted whichever way the comparison goes.

## Built and tested, 2026-09-02

360 host checks pass, the same sources cross compile for the calculator, and every artifact below
has now run on the physical handheld.

```
make -C nps test      360 checks, 0 failed
make -C nps fuzz      10,000 generated expressions through the parser and the rules
make -C nps regold    rewrite the golden fixtures, then fail on purpose so they get read
make -C nps device    every object, through the ndless toolchain
make -C nps probe     nps_bench.tns, the core with no Giac and no Lua
make -C nps luax      nps_split.luax.tns, the native module the UI calls
make -C nps document  nps_v2.tns, the UI document
```

| Piece | What it does |
|---|---|
| src/ast.h, src/ast.cc | Arena of immutable nodes, hash consed, so structural equality is index equality |
| src/budgets.h | Limits the AST enforces, plus the Budget, Halt and Meter a solve runs under |
| src/context.cc | SolutionContext from section 10, built at every solve exit and serialized |
| src/canonical.cc | The comparison form: flattening, ordering and constant folding |
| src/parser.cc | Scanner and recursive descent, limits enforced while building rather than checked after |
| src/print.cc | Canonical form for round tripping, and the separate Giac spelling |
| src/giac_adapter.cc | The typed operation allowlist and the nine result tags from section 12.1 |
| src/derivation.cc | The step record from section 10, with a payload per kind |
| src/linear.cc | Linear equations in one unknown, solved through recorded steps |
| src/differentiate.cc | Differentiation by rule, one recorded step per rule, nested for the parts |
| src/lua_module.cc | The narrow bridge: Lua passes text, this parses and validates before anything acts |
| src/device_probe.cc | Runs all of the above on the calculator and writes what it found |
| lua/nps_v2.lua | The UI half: draws the answer, its trust label and the step list, and takes keys |

## Decisions taken, and why

**Subtraction and division do not exist in the AST.** `a - b` is `a + (-b)` and `a / b` is
`a * b^(-1)`. Two spellings of the same thing double the rule engine's cases for no gain, and the
printer puts the familiar form back.

**The parser rejects a chained relation.** `a = b = c` has two readings and picking one puts an
unstated assumption into a derivation before any rule has run.

**Juxtaposition multiplies only after a number.** `2x` and `3sin(x)` are products; `x y` stays an
error. Silently reading two adjacent names as a product is how an input typo becomes a wrong answer
that looks right.

**Numbers keep their text.** The core has no bignum, and Giac has GMP behind it, so narrowing an
integer here would lose digits the backend could have handled. A decimal also has to come back
exactly as written for PERF-007.

**Limits are enforced during construction.** A depth check run after the tree exists has already
spent the memory it was meant to bound.

**A backend answer that will not parse never becomes a value.** Section 12.1 says text that cannot
become the expected typed representation must not enter a derivation state, so `Response::value`
stays empty for every tag except exact, conditional and approximate, and `usable()` is the only way
to reach it.

## Two defects found by the tests rather than by reading

**A resource limit was being reported as a syntax error.** When a limit fires mid expression the
descent unwinds and leaves tokens unread, and the parser checked for leftover tokens before it
checked the arena's status. Every refusal for depth or size came back as "unexpected input", which
names the wrong cause and hides which limit was hit. The arena check now runs first.

**The approximate tag was reading the whole arena.** A result was marked approximate if any decimal
had ever been parsed in that session, not if the answer contained one. The arena is shared across
every expression a session touches, so this was wrong for any session that had seen a decimal
earlier. It now walks the result subtree, and there is a test that parses `3.14` first and then
checks an integer answer still comes back exact.

## It runs on the calculator, 2026-09-02

Deployed and launched through the MCP with no keypress, and the result fetched back:

```
start
sizeof(Node) = 40
canonical = ((x^2) * sin(x))
giac = ((x)^(2)*sin(x))
nodes = 5 depth = 3
round trip is a fixed point = yes
chained relation refused = yes
command = diff(((x)^(2)*sin(x)),x)
tag = exact
value = (((2 * x) * sin(x)) + ((x^2) * cos(x)))
size limit fires = yes (size exceeded)
done
```

Every line matches what the host tests assert, so nothing about the ARM ABI moved under the arena's
size fields, the interning key packing or the printer's recursion. `sizeof(Node)` is 40 bytes on
target, which is the first real number for the AST size budget PERF-010 has to freeze.

The adapter's backend here is canned rather than Giac, so this shows a reply becoming an AST on
device, not that Giac was called. Wiring it to luagiac is a separate step and needs a Lua document,
which still needs a keypress.

### A transfer failure that was not what it looked like

Three deploys in a row failed with "Link transfer failed" on a 137 KB file, which read as a size
limit. It was not: `paint20.tns` at 95 KB had transferred earlier in the same session and failed too
when retried. The link degrades over a long session and a fresh boot fixes it. The falsifying test
was one deploy of the file already known to work, and it cost less than the size hunt would have.

## Canonical form, 2026-09-02

src/canonical.cc. The parsed tree stays exactly as written, because section 12.1 wants it lossless
enough to preserve the user's grouping, and a derivation that silently reorders the input has lost
the thing it is meant to explain. Comparison uses a second form built beside it, and because the
arena interns, two expressions are equal when their canonical ids are equal. That is one comparison
rather than a walk.

It flattens nested Add and Mul, turns negation into a factor of minus one, sorts operands under a
total order with numbers first, and folds integer constants.

### Two defects the tests caught, both in code that read fine

**A negative constant lost its sign.** `to_digits` built the magnitude and never put the minus back,
so `-x` canonicalised to `1 * x` and `2 - 3` to `5`. Both wrong answers, silently.

**The canonical form depended on how a sum was bracketed.** `small_valid` capped at 18 digits, so
folding two 18 digit numbers produced a 19 digit result that was no longer foldable, and the
leftovers depended on the shape of the tree. A sum of ten large integers came out as five partial
sums. That is precisely what a canonical form must not do. The cap now means "fits in an int64",
checked per digit, and there is a test that brackets the same long sum two ways and requires one
answer.

The first was found by a test that asserted the printed form. The second only showed up because I
printed what it actually produced rather than trusting the check that had just passed for the wrong
reason: my overflow test was unreachable, since an 18 digit literal cannot overflow an int64 on its
own.

### A header name that shadows the standard library

The budget constants were in `src/limits.h`. Any translation unit compiled with `-I src` then
resolves `#include <limits.h>` to that file, and the failure is a page of errors about `CHAR_BIT`
from inside the standard library, nowhere near the cause. Renamed to `src/budgets.h` before the
device build, where giac's headers make it far more likely to bite.

## Second device run, 2026-09-02

```
sizeof(Node) = 40
canonical = ((x^2) * sin(x))
giac = ((x)^(2)*sin(x))
nodes = 5 depth = 3
round trip is a fixed point = yes
chained relation refused = yes
command = diff(((x)^(2)*sin(x)),x)
tag = exact
value = (((2 * x) * sin(x)) + ((x^2) * cos(x)))
canonical form = (1 + ((x^2) * sin(x)))
two spellings agree = yes
wide integer folds = yes
size limit fires = yes (size exceeded)
done
```

The two that matter here are the last three lines. 64 bit folding is correct on a 32 bit ARM target,
and two differently written expressions reach the same canonical node on device as on host.

## The derivation record, 2026-09-02

src/derivation.h follows section 10's envelope and its four payloads. Two things are enforced rather
than left to discipline.

**A record cannot carry the wrong payload.** Section 10 says fields that do not apply to a kind are
not populated with dummy expressions or generic verified values. The payload is a tagged union and
`transformation(id)` returns null on a plan record, so a caller that reaches for the wrong one gets
nothing instead of a default constructed thing that looks real.

**A claim with no verification record is not verified.** `Step::verified()` returns false for a
claim nobody checked, and a single failed check outweighs any number of passed ones. `NoClaim` is
the only kind that is verified without a record, because there is no assertion to check. Section 17
requires unverified results to be labelled prominently, and that is only possible if the record can
answer the question.

## Linear equations, 2026-09-02

src/linear.cc solves `a*x + b = c*x + d` by inverse operations and records every move. The
derivation is the product and the answer is a by-product, which is why it writes into a `Derivation`
rather than returning a number.

The arithmetic is exact rationals as a pair of int64 with every operation overflow checked, done in
the engine rather than delegated. A step that says "divide both sides by 2" and then asks Giac what
the answer is has not explained anything, and the point of the product is the explanation.

It refuses what it cannot do, which section 17 requires: a quadratic, a second unknown, and the
unknown in a denominator all come back as `not linear in the unknown` with a reason, rather than as
a plausible wrong answer. It also distinguishes the two ways an unknown can vanish, no solution and
true for every value, which a solver that just reports "no answer" conflates.

The last recorded step substitutes the answer back into the collected equation and reports what it
found. If that substitution does not reduce to zero the result is withdrawn rather than offered,
so the check is a gate and not a decoration.

### A refusal that was too broad

`x/2 = 3` was refused as non linear. Division parses to `Mul(x, Pow(2, Neg(1)))`, and the exponent
test only recognised an `Integer` node, so the `Neg(1)` fell through to the catch-all refusal. Every
division by a constant was affected. The exponent is now read through a helper that accepts both
spellings, and a negative power of a constant base inverts the fraction instead of being rejected.

Worth noting how it presented: not as a wrong answer, but as a correct-looking refusal. A rule that
says "I cannot do this" is the hardest kind of bug to notice, because the output is exactly what a
genuine limitation looks like.

### On the calculator, 2026-09-02

```
solve outcome = solved
solution = 4
steps = 4
  [plan] Isolate x | Solve by undoing what was done to x | verified=yes
  [transformation] Collect the terms in x | Moving every term to one side keeps both sides equal | verified=yes
  [transformation] Isolate x | Dividing both sides by a non-zero number keeps them equal | verified=yes
  [check] Check the answer | Put the answer back into the collected equation | verified=yes
quadratic refused = not linear in the unknown
```

That is Milestone 1's exit condition on target, apart from the browsing, which needs a UI: a linear
equation solved through genuine verified steps, and an unsupported case stopping explicitly.

## Differentiation, 2026-09-02

Rules for constant, variable, sum, constant multiple, product, integer power, chain, and sin, cos,
tan, exp, ln and sqrt. Each one records the step that applied it and recurses for the parts, so the
record of `x² sin x` is the product rule with the two derivatives it needed hanging off it rather
than an unexplained jump. On the calculator:

```
diff outcome = differentiated
derivative = (((2 * (x^1)) * sin(x)) + ((x^2) * cos(x)))
derivative canonical = (((x^2) * cos(x)) + (2 * (x^1) * sin(x)))
diff steps = 4
  [plan] plan | Differentiate ((x^2) * sin(x)) with respect to x
  [transformation] Product rule | Differentiate ((x^2) * sin(x))
  [transformation] Power rule | Differentiate (x^2)
  [transformation] Trigonometric derivative | Differentiate sin(x)
variable exponent refused = unsupported form
```

`x^x` stops rather than guessing, and says it would need logarithmic differentiation.

### The exec crash, and two defects behind it

An `exec` reset the calculator with `Error (1800ee2c): LDRD/STRD with odd-numbered data register`.
Our image contains no odd-register LDRD or STRD at any of its 242 sites, so the CPU was not running
our code. The address sits in the task stack, right where `armloader_load_snippet` puts the snippet
it runs.

`cpu_thumb_loop` calls `debugger()`, so the debugger can break in while the guest is in Thumb state.
`armloader_ndless_context` checked mode, interrupts and sp but not the T bit, and
`armloader_load_snippet` writes `arm.reg[15]` directly without touching it. Loading a snippet from a
Thumb break-in therefore leaves T set, `emu_loop` dispatches to the Thumb loop, and the 60-byte ARM snippet
gets decoded as Thumb: garbage, executed on the stack. The Thumb loop also never checks
`RF_ARMLOADER_CB`, so even a snippet that survived could not return. Both now refuse Thumb outright.

Separately, `virt_mem_ptr` translates the first page only, and the loader used it to copy 60 bytes of
code and a path of up to 512, so a range crossing a page boundary wrote past the end of the first
one. `virt_mem_write` in debug.cpp walks the pages, checking the whole range before copying anything.

The snippet loader now logs pc, sp and cpsr. The absence of any log line is what made the original crash hard
to read: the debugger's `exec` printed nothing when it ran on the spot.

### A stale result that looked like a broken deploy

The first differentiation run came back with output from an older build, and I read that as the
deploy silently refusing to replace an existing document. It was not. A 172804-byte probe sent and
fetched back compared byte identical, and a 20-byte file sent over that name replaced it. `exec`
returns when the program is loaded, not when it finishes, and this headless build has no JIT on
Apple Silicon, so the fetch had simply arrived before the program wrote anything. The old file was
intact because the probe opens its output with `"wb"` and had not reached the `fopen`.

## It runs on the handheld and Giac agrees, 2026-09-02

Milestone 0's exit condition, met on the physical CX II running 6.2.0.333 with no host process
beyond a keypress sent over USB:

```
x^2*sin(x)
(((x^2) * cos(x)) + (2 * (x^1) * sin(x)))
differentiated | Giac agrees
Differentiate by rule: Differentiate ((x^2) * sin(x)) with respect to x
  Product rule: Differentiate ((x^2) * sin(x))
    Power rule: Differentiate (x^2)
    Trigonometric derivative: Differentiate sin(x)
```

"Giac agrees" is the whole point of the line. The adapter asks Giac the same question independently,
parses the reply back into an AST, subtracts it from what the rules produced and asks Giac whether
that simplifies to zero. Giac is a second opinion on our answer rather than the source of it, which
is what section 12.1 asks for and what section 23 lists as the critical common-mode risk.

The same document on the emulator says "not cross-checked", because that image has no luagiac. So
the emulator proves the bridge and the rules, and only the handheld proves the cross-check.

### Two defects the hardware found that no host test could

**Giac's answer to a solve was being thrown away.** `solve((((2*x)+5))=(13),x)` comes back as
`[[4]]`, a list of one solution set of one solution. The adapter handed that to our parser, which
refuses brackets, so every solve reported "malformed result" and the cross-check never happened. The
answer was right and the check was silently absent, which is the same shape as the withheld-answer
defect: the failure looks like a limitation. The adapter now peels a single element list repeatedly
and refuses a set of any other size with a typed reason rather than taking the first element, since
it has no representation for a solution set.

Worth recording how the first reading went wrong: I read `[[4]]` off a 320 by 240 screenshot as
`[(4)]` and built the first fix around that. One peel is enough for `[(4)]` and not for `[[4]]`, so
the fix passed its tests and changed nothing on device. The line that settled it was putting the raw
reply on screen, which is now what the UI does whenever a reply cannot be used.

**A boolean argument silently turned the cross-check off.** I added an optional third argument to
`solve` and `differentiate` to skip the Giac call while diagnosing. On device, passing `true` for it
skipped the check: `lua_toboolean` reported false for a Lua `true` inside a .luax on 6.2.0.333.
Measured, not inferred, by running both functions with two arguments and with three in the same
document: two arguments cross-checked, three did not, for both.

The cause was found on 2026-09-03: Ndless's syscall table named the wrong OS function as
`lua_toboolean` on every CX II OS, a string switch some 0xC700 bytes past the real one. Fixed in
the MakeSyscalls idc files and the resources rebuilt, proven with probe/boolprobe on the emulator;
the account is in tooling.md. The flag stays gone all the same. What it turned off is the check
section 17 says must be visible, so a flag that can misread is worse than no flag. The opt-out is
a separate function, `solve_local` and `differentiate_local`, where nothing has to be converted to
decide.

### Four things about the loop that cost time to find

- **The emulator's link refuses a name with two extensions.** `nps_split.luax.tns` failed three times
  with "Link transfer failed" while `stepcasx.tns`, the same bytes, went over immediately. The
  falsifying test was one deploy of a file already known to work, which is the same test that
  settled the earlier size hunt. The module now goes over under a plain name and `ln mv` renames it
  in place. The handheld's file service takes the real name directly.
- **A document the calculator has open cannot be replaced over the link.** Same "Link transfer
  failed", and the same shape of wrong first guess: it reads as link degradation and it is not.
- **The keysvc reply is the receipt.** `6b 01` means the key was accepted; since 2026-09-03 it is
  posted after the reply. No reply line at all means the OS never answered, and the key did nothing.
- **The emulator does not idle into a power routine.** That was the 2026-09-02 reading and it was
  wrong. The resets came from keysvc blocking the task that processed its key, and the open crash
  below this document had on the emulator was the same defect. See the keysvc section in tooling.md.

### The Lua defect that reset the calculator four times

`nps_v2.lua` took the machine down on open with `attempt to index a function value` and no line
number. The cause was one missing line: `platform.apilevel = '2.0'`. Without it the OS runs the
script at its oldest API level, where `platform.window` is not the object the rest of the file
assumes. khicas.lua declares it at line 19, which is the evidence that settled it, and probing
`type(platform.window)` from a document that did declare it returned `userdata` rather than the
function the error was complaining about.

Two habits came out of this. A diagnostic document that answers every question at once, with each
call wrapped, is worth the five minutes it takes to write: it replaced four reboots of guessing with
one run that printed nine facts. And every handler in nps_v2.lua now goes through a guard that turns
an error into a line on screen, because a reset leaves nothing to read and no message to read it by.

## The bridge and the UI, first written 2026-09-02

src/lua_module.cc and lua/nps_v2.lua are the native-core split of section 12.2 drawn at the language
edge: Lua draws and takes keys, and every expression is parsed inside the native module under the
arena's limits. Lua has no way to reach the adapter with a string of its own, which is where section
12.3's trust boundary lands when the UI is in another language. The module's backend calls
luagiac.caseval, so it is the first code that can actually reach Giac.

Both are now loaded and driven on the emulator and on the handheld, with keysvc supplying the
keypress that was the last blocker.

## The quotient rule, added 2026-09-02

Division is `Mul(u, Pow(v, -1))` in the AST, by the same decision that removed subtraction, so `u/v`
already differentiated correctly through the product, power and chain rules. The answer was right
and the derivation said "product rule" to a student who was taught the quotient rule and was looking
for it. That is section 4.2 and section 28 failing while every test passes, which is the shape of
defect this tree keeps producing: a correct-looking output covering a wrong explanation.

`d.quotient` now fires when a product has a varying numerator and a varying denominator, and only
then. A constant denominator stays a constant multiple, and a bare reciprocal stays the power rule,
because both of those explain themselves better than a quotient rule with a numerator of one would.
The tests assert which rule fired, not just the answer, since the answer was never the problem.

## What the fuzzer found, 2026-09-02

The generator produces bounded expressions from a tree and only then spells them, which is what
makes it possible to ask whether two spellings of one expression agree. Three real defects came out
of the first 10,000 cases, and all three were in code that reads fine.

**An overflow guard the compiler was entitled to delete.** `mul_overflows` detected overflow by
performing the multiplication and inspecting the result. Signed overflow is undefined, so at the
shipped -O2 the compiler assumed it could not happen and folded the check away: `9223372036854775807
* 2` canonicalised to `-2`. I reproduced it before touching it, which matters, because the same code
is correct at -O0 and a reading pass would have called it fine either way. It now decides before
multiplying.

**A printer that lost a sign.** A folded constant carries its sign in its own text, and unary minus
binds looser than a power, so `(-7)^23` printed as `(-7^23)` and reparsed as `-(7^23)`. Only the base
of a power is affected, and only `print` rather than `print_giac`, which already bracketed it.

**A canonical form that was not a normal form.** Constant folding depended on the order the terms
arrived in, so `6 + BIG` and `BIG + 6` reached different answers. Three schemes were tried before one
held, and the failures are worth keeping because each looked correct in isolation:

- Sorting the terms and stopping at the first overflow, with the folded total written first. The
  total then sat out of order with the leftovers, and the next pass moved it.
- Flushing the accumulator on overflow and starting another. The accumulators a first pass produced
  regrouped differently on a second.
- Refusing to fold at all when anything overflowed. That left a foldable pair sitting in the
  canonical form, which print emits flat and the parser reads back left nested, so a reparse folded
  the pair the first pass had declined.

What holds is a maximal prefix: fold from the smallest upwards while the total stays inside an
int64, then stop. Nothing foldable is left behind for another pass to take. Measured rather than
argued: 50,000 cases at one seed and 10,000 at four more, with round-trip and idempotence clean and
only the two-spellings property still excluded at the fold boundary. src/canonical.h says exactly
what that exclusion costs.

## A withheld answer that still read as verified

The golden fixtures caught this, which is the argument for having them. When the linear solver's
substitution check fails it refuses the answer, and every step in that derivation still answered yes
to `verified()` and no to `has_failed_verification()`. The failure existed only in the outcome enum
and in the prose of `observed_result`. A UI or a diagnostics walker using the typed accessors, which
is exactly what section 17 and MVP criterion 4 point them at, saw a complete verified solution for an
answer that was never offered. That is criterion 11's false claim, reachable through the typed API.

The check step now carries a claim and a failed VerificationRecord, so the typed channel says what
the prose says. The cause was subtler than a missing record: the step was `ClaimType::NoClaim`, and
a step that claims nothing counts as verified without one.

## Cancellation and resource budgets, 2026-09-02

PERF-003 wants symbolic work to be cancellable, and the toolchain is built without threads. There is
no worker to interrupt and nothing can be stopped from outside, so the solver has to ask. `Budget`
carries the three counts and a poll function, and one `Meter` per solve does the counting. Counting
in the meter rather than inside each rule is the point: a rule added next year is bounded by
construction rather than bounded by whoever remembers to count.

Each Meter polls once when the solve starts, so a short solve can stop before it records work. It
then polls every 64 rewrites rather than on every one, because a poll can be a keypad scan and
PERF-001's responsiveness has to trade against the cost of asking.

A halt does not leave a half-written explanation behind. Both solvers take a mark before they start
and `rewind_to` it when the meter stops, so a cancelled solve yields no steps rather than a partial
record a reader would take for a finished one. `budget_policy` writes the limits into the
SolutionContext, since a different budget can produce a different derivation and reproducing one
means knowing which budget it had.

## SolutionContext, 2026-09-02

Section 10 wants every solve to say what it assumed. `make_context` is the only way to build one, so
two solves cannot disagree about how a field is filled, and any field this build does not record
becomes `kContextUnknown` rather than staying blank. An empty string reads as a blank value, which
is a different claim from "not recorded".

The wire format is versioned, and the version is the schema: field order, count and kind are fixed
per version, which is what PERF-007 needs to reload a saved derivation years later. `parse_context`
treats a save as data and never as code, per section 17, and assigns `*out` only after the whole
blob is accepted. It refuses with a typed reason and the byte offset it stopped at, so a corrupt
save can be pointed at rather than merely rejected.

## Where the milestones stand, 2026-09-02

**Milestone 0** is met on its functional half and open on its measurement half. The derivative of
x squared sin x was entered, evaluated through Giac and displayed on the physical CX II with the
cross-check agreeing. What it still owes is numbers: PERF-005, PERF-006, PERF-010 and PERF-011.

**Milestone 1** is met on both targets. A linear equation solves through recorded verified steps
that can be browsed in the UI, and Giac agrees with the answer.

**Milestone 2** is built and runs on the handheld. Twelve named rules, each recording its own step:
constant, derivative of the variable, sum, constant multiple, product, quotient, power, chain, and
the four function derivatives, trigonometric, exponential, logarithmic and square root. Those last
four cover six functions, since sin, cos and tan share one name.

**Milestones 3 and 4**, integration and then units and physics, are not started.

## Next

1. Measure. PERF-005, PERF-006, PERF-010 and PERF-011 have no numbers yet, and PERF-011 wants them
   with everything resident. `sizeof(Node)` at 40 bytes on target is still the only real one.
2. Integration, then units and physics: Milestones 3 and 4.
3. An independent oracle. Giac agreeing with us is evidence, and section 23 says a backend that both
   generates and checks is a common-mode risk. A host-side second system would close it.
