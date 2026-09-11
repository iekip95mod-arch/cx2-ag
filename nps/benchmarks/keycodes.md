# Driving the emulated calculator through keysvc

In the retained Firebird harness experiments, debugger keypad input did not reach the OS key queue. ON reached the hardware wake line and triggered a reboot. This harness therefore sends OS and Lua document input through keysvc over the link. Recheck the behavior when changing the emulator or guest image.

It does reach anything that reads the keypad itself, and that is not the same set. Giac's getkey in
khi-src/src/k_csdk.c polls the keypad hardware and the real time clock in a busy loop, so keysvc
cannot drive Giac's own menus and dialogs at all: a keysvc key is accepted, the reply says one
record, and doMenu never sees it. Firebird's `key` is the only way in there. Tap it as a hold rather
than a tap, because a tap is released after a fixed number of keypad sweeps and the guest has to be
running to sweep at all:

    nspire dbg commands='key enter +; c'
    nspire dbg commands='key enter -; c'

So the harness has two key paths and they reach different halves of the machine. Sending to the wrong
one looks exactly like a key that does nothing.

    nspire dbg commands='ln svc 4B45 <hex>; c'

One four-byte record per key, in the order code_lo, code_hi, modifiers, action. Action 0 is a raw
tap and the codes are the plain column of tools/keysvc/protocol.h. Records concatenate, so several
keys go in one packet, and the reply's second byte is how many were accepted: `6b 02` is two.

The ones this harness uses, already byte-swapped:

| Key | Code | Record |
|---|---|---|
| esc | 0x961B | `1B960000` |
| enter | 0x100D | `0D100000` |
| menu | 0x3600 | `00360000` |
| home | 0xFD00 | `00FD0000` |
| doc | 0xFE00 | `00FE0000` |
| cat | 0x9100 | `00910000` |
| tab | 0x9509 | `09950000` |
| up | 0x7100 | `00710000` |
| right | 0x7300 | `00730000` |
| down | 0x7500 | `00750000` |
| left | 0x7700 | `00770000` |
| a | 0x6661 | `61660000` |
| 1 | 0x7331 | `31730000` |
| 2 | 0x5332 | `32530000` |
| 3 | 0x3333 | `33330000` |
| 4 | 0x7234 | `34720000` |
| 5 | 0x5235 | `35520000` |
| 6 | 0x3236 | `36320000` |
| 7 | 0x7137 | `37710000` |
| 8 | 0x5138 | `38510000` |
| 9 | 0x3139 | `39310000` |
| 0 | 0x5030 | `30500000` |
| plus | 0x112B | `2B110000` |
| minus | 0x122D | `2D120000` |
| mult | 0x132A | `2A130000` |
| div | 0x142F | `2F140000` |
| pow | 0x935E | `5E930000` |

Letters follow the same rule and are in the same table, so any of them can be swapped in. The three a
probe usually needs, byte-swapped: k is `6B240000`, m is `6D630000`, n is `6E430000`.

Three things that cost time before they were written down.

Inside a framework dialog, esc is not a dismissal. The first press moves focus out of the field and
the second cancels, so a probe that sends one esc and screenshots sees a dialog that looks stuck.

In the historical dialog experiment, a transfer dialog coincided with read-back and key-service timeouts. A later call succeeded and the received file was intact. The proposed explanation was that the dialog blocked the task serving the link. That scheduling cause was not established. Current [transport observations](BUDGETS.md#physical-package-check-2026-09-08) also include timeouts that a bounded serial probe did not reproduce. Inspect the visible state and the outcome of a failed mutation before retrying it.

The menu key does nothing on a blank Press-menu document page, which reads as a dead key. It works
in the Calculator and in any document that registers a tool palette.

The two arrow families in the table above are not interchangeable and neither one drives everything.
The 0x7100 column moves a native menu. The 0xF300 family that keysvc_navigation_code maps it to moves
a Lua document, and it also moves the file browser, which is the surface that catches you out: four
downs in the 0x7100 form left the browser selection where it was, and the same four as 0xF400 walked
it down the list. When a key looks dead, send it in the other family before believing it.

A `put` leaves a Document Received dialog whose Open button is already selected, so enter opens what
was just sent. The deploy tool reads its file back to verify it, and that read leaves a Document Sent
dialog instead, which has no Open. To open a document, send it with `ln st` and `ln s` through the
debugger rather than through deploy.
