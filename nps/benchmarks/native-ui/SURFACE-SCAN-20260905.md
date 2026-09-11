# Native UI surface scan, emulated CX II non-CAS, OS 6.4.0.74, 2026-09-05

Captured from the real TI framework on the emulator, not drawn or described. The numbered files in
this directory are this scan. Files named final- or review- are from an earlier session and are not
mine.

| File | What it shows |
|---|---|
| 01-home.png | Home screen, Scratchpad and Documents columns, the seven application icons |
| 02-scratchpad.png | Scratchpad Calculate, empty |
| 03-calc-history.png | Four history entries. 2^10 as a raised superscript, 144/9 as a stacked fraction with a vinculum, results right-aligned, alternating row shading |
| 04-menu-top.png | The menu open over history. Eight numbered categories, per-item glyphs in a left gutter, submenu arrows |
| 05-menu-actions.png | Actions submenu, showing how a submenu sits beside its parent |
| 06-number.png | Number submenu |
| 07-algebra.png | Algebra submenu. Three items on non-CAS: Numerical Solve, Solve System of Linear Equations, Polynomial Tools |
| 08-calculus.png | Calculus submenu. Every item numerical: Numerical Derivative at a Point, Numerical Integral, Sum, Product, Numerical Function Minimum, Numerical Function Maximum |
| 09-probability.png | Probability submenu |
| 10-statistics.png | Statistics submenu |
| 11-matrix.png | Matrix and Vector submenu, ten items with a down-chevron showing it scrolls |
| 12-finance.png | Finance submenu |
| 13-catalog.png | The Catalog. A six-tab strip, a scrollable list with a scrollbar, a Wizards On checkbox, and a signature line at the bottom |
| 14-doc.png | The document menu: File, Edit, View, Insert, Page Layout, Refresh Libraries, Settings and Status, Login |
| 15-settings.png | Settings and Status submenu |
| 16-docsettings.png | The Change Language dialog. Title, one labelled dropdown with a chevron, OK and Cancel |
| 17-docsettings.png | Document Settings. Six right-aligned labels with dropdowns, a scrollbar, OK and Cancel. This is the native form idiom |
| 18-thirdlevel.png | A third-level submenu, Number then Fraction Tools. It opens leftward and overlaps its parent columns |
| 20-dialog-msgbox.png | The framework message box called from inside a Lua document, drawn over it |
| 21-dialog-number.png | The framework numeric input, with title, subtitle, label, spinner, OK and Cancel |
| 22-module-surface.png | The module surface the document actually loaded, used to catch a shadowed module |
| 23-esc-at-top-level.png | Where esc lands from the Scratchpad entry line, which is the home screen |
| 24-submenu-open.png | A submenu opened with enter, the parent still visible beside it |
| 25-ctrl-esc-undo.png | After ctrl-esc, the evaluated row back to an unevaluated entry |
| 28-arrow-named-not-general.png | One arrow press, named handler at 1 and on.arrowKey still at 0 |
| 29-editor-owns-the-clipboard.png | A focused editor doing its own select all, copy and paste |
| 30-ctrl-esc-closes-the-document.png | Where ctrl-esc lands from a Lua document, which is the home screen |
| 31-confirm-reversed-labels.png | The confirm with Cancel first, the reading that showed esc follows position |
| 32-confirm-three-buttons.png | A three-button dialog, focus on the first and escape on the last |
| palette-english-44-to-60.png | Tool palette label truncation, English text at 44 to 60 characters |
| palette-widest-16-to-25.png | The same for all-W text at 16 to 25 characters |

## What the palette measurement says

English text renders complete at 44 and 47 characters and is cut at 50. All-W text renders complete
at 16, 19 and 22 and is cut at 25. The limit is a rendered width, not a character count, so a
47-character cap works for average English and fails for a label leaning on wide glyphs. At 47 the
trailing marker sits against the panel edge with a character or two to spare, so there is no
headroom. Truncation is silent: the text is cut at the edge with no ellipsis.

The probe that measured it is nps/benchmarks/palette_probe.lua, so this can be rerun rather than
taken on trust.

## What the native keys actually do

Question (g), measured in Scratchpad Calculate on this build with one history entry present. Every
line below is a key sent through keysvc and a screenshot taken in a separate call.

esc is a back-one-level key, and at the outermost level it leaves the application. From a submenu it
returns to the parent menu with the parent still open. From an open top-level menu it closes the menu
and leaves the cursor on the entry line. From the entry line with no menu open it drops to the home
screen, and the scratchpad history survives that and is still there on return. So the answer to
whether esc ever drops the document is yes, at the top level, and that is the native convention
rather than a mistake to design around.

ctrl-esc is undo, not a stronger escape. It turned an evaluated history row back into an unevaluated
entry line, and pressing enter re-evaluated it to the same answer, which is how it was told apart
from a repaint. Anything in Ki that treats ctrl-esc as an exit would be fighting the platform.

Inside a menu, enter opens a submenu and right does not. Right was pressed twice at the top level of
the menu with no effect, then enter opened Actions immediately. Worth knowing before wiring arrow
keys to menu navigation on the assumption that right descends.

delete is a backspace: it removed the character before the cursor. clear, which is ctrl and delete,
emptied the entry line and left the history untouched.

Two results are weaker than the rest and are recorded as such. tab and ctrl-tab both produced no
visible change at the Scratchpad top level. The ctrl path itself is known to work, because ctrl-esc
changed state through the same mechanism, so ctrl-tab reaching the OS and doing nothing is the more
likely reading, but a single-pane scratchpad may simply have nothing to switch between and this does
not settle what ctrl-tab does in a document with pages. The up arrow did not move focus off the entry
line, so history recall was not observed here.

One thing found on the way out rather than looked for: pressing home from a Lua document whose title
still carried its unsaved marker left it with no save prompt.

Captures: 23-esc-at-top-level.png, 24-submenu-open.png, 25-ctrl-esc-undo.png.

## The OS dialogs work from inside a Lua document

Question (h), answered by calling them rather than by reading the headers. Two bindings were added to
the module for the experiment, os_msgbox over show_msgbox and os_number_input over
show_1numeric_input, and driven from benchmarks/dialog_probe.lua.

Both dialogs draw over the running document and both return to it. The message box returned 0 for its
single button. The numeric input came back with 7 after that digit was typed, and with nil after a
cancel, so a value crosses the boundary in both directions. The document was still painting
afterwards, 589 frames by the end against 3 before the first dialog opened, so the event loop is not
consumed and the document is not torn down. That makes the native form idiom in 17-docsettings.png
reachable from Ki rather than something to imitate in our own drawing code.

Two things worth knowing before building on it. The numeric input needs two presses of esc to
cancel, because the first only moves focus out of the spinner, so a single esc is not a dismissal.
And on the emulator the module load itself takes tens of seconds, because the integrity gate hashes
the whole 4 MB package and the headless build reports it could not init JIT. That is emulator
arithmetic, not a handheld figure.

The scope of the answer is these two dialogs. It is evidence that the framework's modal surface
composes with a document, and it is not a claim about doMenu, which is question (f) and still open.

### The confirm dialog, and the trap in it

os_msgbox now takes optional button labels, so a two-button confirm is available for destructive
actions. Every return value below was pressed and read, including the one that matters most.

Two rules govern it, and together they mean a two-button confirm cannot be made safe.

Focus starts on the first button, filled blue before any key is pressed, which
26-confirm-default-focus.png shows. Escape returns the last button, whatever that button says. The
second rule took three readings to pin down, because with two buttons "esc returns a literal 2" and
"esc returns the last index" are the same observation and neither is ruled out. A reversed pair
settled it: with Cancel first and Clear History second, esc returned 2, which is Clear History. A
three-button dialog agreed by returning 3. Escape follows the position and not the word.

So with two buttons the two most reflexive keys on the keypad land on the first and the last, and
whichever position a destructive action takes, one of them reaches it. Putting the safe label first
fixes enter and breaks escape. Putting it second does the reverse.

Three buttons is the way out, ordered Keep History, Clear History, Cancel. Enter takes button 1 and
escape takes button 3, both harmless, and the destructive answer is 2, which is neither focused nor
reachable by escape. It costs one deliberate tab: measured, one tab from the default focus lands on
button 2 and enter there returns 2.

Callers must test equality with the confirming index. Testing for anything other than zero reads an
escape as a yes.

Buttons are reached with tab. The arrow keys do not move focus between them, which is the same thing
the menus do, and right was tried twice before tab was.

## The framework list menu works from inside a Lua document

os_menu takes a title and a table of item strings and puts up the framework's own list menu, the one
Giac draws for its own menus. It numbers the items itself, so a table of four comes back as "1 Alpha"
through "4 Delta". All of this was measured on the emulator with nps/benchmarks/dialog_probe.lua.

Focus starts on item 1, so a caller that wants a safe default puts it first.

A choice returns its one-based index. Enter on the opening frame returned 1, and the digit 3 returned
3, which is what tells an index apart from the constant the framework returns for "something was
selected".

Escape returns nil, in one press. It does not need the second press a framework dialog needs, and
on.escapeKey did not fire in the document, so the menu consumed it.

A letter opens a keyword filter in the top right corner rather than choosing or dismissing. It is
inert here: the box showed b, the four items stayed, and the highlight stayed on 1. Pressing g and
then enter still returned 1 and not 3, so a stray letter cannot change what enter selects.

The call is synchronous, so the document's event loop cannot run until the student answers. That is
the same shape as the dialogs above and it is why a menu must not be opened from anywhere that has
work in flight.

One harness fact that costs an afternoon otherwise: keysvc cannot drive this menu at all. Giac's
getkey polls the keypad hardware directly, so a keysvc key is accepted, the reply says one record,
and the menu never sees it. Firebird's own key command is the only way in. See keycodes.md.

## What closes a document, and what a close does to unsaved work

Measured with nps/benchmarks/save_probe.lua, whose lifecycle handlers each record that they ran and
whose on.save carries that record into the saved state, so a reopen reports what happened rather than
only what survived.

Ctrl+Esc does not close a document and does not serialize one. It backgrounds it. The handlers that
run are loseFocus and deactivate, then activate and getFocus on return, and on.save is not among
them. Every piece of state survived, including text left sitting in an input line without being
committed, because it is the same live Lua state rather than a restored copy. The save counter did
not move and the restore message was still the one from the previous open.

Ctrl+S calls on.save, writes the state into the .tns and clears the asterisk from the title. Opening
the document again calls on.restore with what was saved. That chain works, so a document that saves
explicitly keeps its work.

A real close is where the work goes. Ctrl+W on a document with unsaved changes closed it silently, no
prompt, and reopening showed only the state from the last explicit save. One committed entry and the
uncommitted input line were both gone.

So the loss is not in Ctrl+Esc, which is the key that looks like it should cause it. The loss is in
any genuine close after the last save, and nothing warns the student. Anything that keeps work has to
either save on a schedule or save on the way out, and Ctrl+Esc is not the hook to hang it on because
nothing fires there that a document can see.

Two details that matter for a fix. The OS marks the document dirty on its own, without markChanged
being called anywhere: the asterisk appeared in the browser after Ctrl+Esc even though the probe
never calls it. And it still closed without prompting despite being dirty.

A document cannot ask the OS to serialize it. The document table holds exactly one key on this OS,
markChanged, and none of the 48 globals is a save. So a checkpoint cannot reuse the on.save path that
Ctrl+S drives; it has to write its own file. api_probe.lua dumps the surface if it needs checking
again on another OS version.

markChanged buys nothing either, which is worth knowing because it looks like the obvious lead. With
document.markChanged() called and three history entries in the document, Ctrl+W still closed silently
and still discarded: only the two entries from the last Ctrl+S came back. No prompt, no save.

What fires during a real close was not measured. The trace list dies with the document, so seeing a
handler run during a teardown needs a side effect that outlives it, which save_probe.lua now has: 3
arms it and on.deactivate writes the marker file. Ctrl+Esc is the control, since deactivate is known
to fire there. Two attempts were spent on invalid runs rather than results, both because a Document
Sent dialog raised by the marker setup swallowed the keys, so send the marker before opening the
document. The answer decides whether a checkpoint can be written on the way out or has to be written
on every change, which is worth the next session rather than a guess.

A write from inside a document callback works. The module's typed_check writes
/documents/ndless/typedcheck.txt.tns with plain fopen, and after calling it from on.charIn the file
fetched off the calculator at 1397 bytes. Whether it survives a power cycle was not measured, because
the emulator restores its flash from a snapshot on every boot and would report a false negative.

## Ctrl+menu on a focused history entry

There is no context menu on that key, at least not here. In Scratchpad Calculate with the result of
1+2 focused (ctrlmenu-focused-entry.png), ctrl+menu left the screen untouched: the frame before and
the frame after hash identically, so nothing was drawn and nothing was dismissed.

That is worth two checks before believing it, because a key that does nothing and a key sent wrongly
look the same. Plain menu in the same spot opened the Scratchpad tool palette, so the queue and the
focus were both fine. And sending it the other way, as the plain menu code with the ctrl modifier
byte, lit the ctrl indicator in the status bar and then opened the same tool palette
(ctrlmenu-yields-tool-palette.png), which is the OS confirming it saw a ctrl and still had no context
menu to show.

So a Ki context menu on ctrl+menu has no native behaviour to copy. Whatever goes there is our choice
rather than a match.

## Driving the emulator to reproduce any of this

See nps/benchmarks/keycodes.md. One correction that cost time here: a key and a screenshot in the
same debugger call races the UI and captures the frame before the key lands. One key per call, then
the screenshot in its own call.

## Not captured

The Graph scratchpad, a wizard dialog from the Catalog with Wizards On, an error or warning dialog,
and the Insert menu, which was greyed out in Scratchpad. Say if any of those decide something and
they are one session away.
