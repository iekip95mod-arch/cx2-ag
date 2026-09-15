#!/usr/bin/env python3
"""Boot the headless emulator, drive it, and capture what is actually on its screen.

This is the route an agent on CI has to the calculator. It is not the MCP server: that lives on the
maintainer's machine and nothing in a hosted job can reach it, which is why the driver is vendored
here beside this file.

Three things cost a session each to find, so they are encoded rather than documented.

`ln c` goes first. Until the link is up every ln command answers "the link is not up, so this will
be dropped rather than sent" and otherwise looks like it worked.

`stop` is not a pause. core/debug.cpp:832 sets exiting = true, so it quits the emulator. Any other
command breaks in on its own, so there is never a reason to send it.

A screenshot has to be checked on disk. Writing into a dead emulator's stdin succeeds and the read
just times out empty, so a command that returned without raising is not a file that exists.
"""
from __future__ import annotations

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import screen
from session import Session

# Printed once the OS has finished coming up. Waiting for this beats sleeping for a fixed span,
# which is either slower than the boot or shorter than it on a loaded runner.
BOOTED = "Setting theDoc.filename to NULL"

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADLESS = os.path.join(REPO, "vendor", "firebird-src", "headless", "firebird-headless")


class Missing(Exception):
    """Something the emulator needs is not on this machine. Not a failure of the thing under test."""


# Written by git-lfs where the real bytes would be. actions/checkout does not fetch LFS content
# unless asked, which prepare-review.sh has to work around for the toolchain, so a runner sees a
# file of this shape rather than an image. Handing one to the emulator gets a confusing failure
# instead of the honest answer, which is that the image was never fetched.
LFS_POINTER = b"version https://git-lfs.github.com/spec/v1"


def is_lfs_pointer(path: str) -> bool:
    try:
        with open(path, "rb") as handle:
            return handle.read(len(LFS_POINTER)) == LFS_POINTER
    except OSError:
        return False


def resolve(boot1: str | None, flash: str | None) -> tuple:
    boot1 = boot1 or os.environ.get("NPS_EMU_BOOT1") or os.path.join(REPO, "images", "boot1.img")
    flash = flash or os.environ.get("NPS_EMU_FLASH") or os.path.join(REPO, "images", "nspire-os.img")
    absent = [p for p in (HEADLESS, boot1, flash) if not os.path.exists(p)]
    unfetched = [p for p in (boot1, flash) if p not in absent and is_lfs_pointer(p)]
    if absent or unfetched:
        note = "; ".join(absent)
        if unfetched:
            fetched = " ".join("--include " + p for p in unfetched)
            note += ("; " if note else "") + "{} are LFS pointers rather than images, so run: git lfs pull {}".format(
                ", ".join(unfetched), fetched)
        raise Missing(note)
    return boot1, flash


class Emulator:
    def __init__(self, boot1: str, flash: str):
        self.session = Session(HEADLESS, boot1, flash, extra=["--debug-on-start"])
        self.booted_after = None

    def boot(self, deadline: float = 180.0) -> str:
        """Bring the OS up with the link connected, and return the boot log."""
        started = time.monotonic()
        log = self.session.start()
        # The link has to be connected while the OS is still coming up, and c is what lets it run.
        log += self.session.send(["ln c", "c"], timeout=5)
        while time.monotonic() - started < deadline:
            log += self.session.read(timeout=5, quiet=2.0, budget=20.0)
            if BOOTED in log:
                self.booted_after = time.monotonic() - started
                return log
            if not self.session.alive():
                raise RuntimeError("the emulator exited while booting:\n" + log[-2000:])
        raise RuntimeError("no boot banner after {:.0f}s. Last output:\n{}".format(
            deadline, log[-2000:]))

    def shot(self, path: str) -> tuple:
        """Capture the screen and return (width, height, pixels). Raises if no file appeared."""
        path = os.path.abspath(path)
        before = os.path.getmtime(path) if os.path.exists(path) else None
        out = self.session.send(["screenshot " + path], timeout=30)
        if not os.path.exists(path):
            raise RuntimeError("screenshot wrote no file. The emulator said:\n" + out[-800:])
        if before is not None and os.path.getmtime(path) == before:
            raise RuntimeError("screenshot left the previous file untouched:\n" + out[-800:])
        return screen.read_ppm(path)

    def svc(self, sid: str, payload: str, timeout: float = 25.0) -> str:
        return self.session.send(["ln svc {} {}".format(sid, payload)], timeout=timeout)

    def resume(self, settle: float = 0.0) -> None:
        self.session.send(["c"], timeout=3)
        if settle:
            time.sleep(settle)

    def stop(self) -> str:
        return self.session.stop()


def smoke(args) -> int:
    """Boot, screenshot, and insist the frame is a screen rather than a blank panel."""
    boot1, flash = resolve(args.boot1, args.flash)
    emu = Emulator(boot1, flash)
    try:
        emu.boot(deadline=args.deadline)
        width, height, pixels = emu.shot(args.out)
        colors = screen.distinct_colors(pixels)
        if args.png:
            screen.write_png(args.png, width, height, pixels)
        print("booted in {:.1f}s, screen {}x{}, {} distinct colors -> {}".format(
            emu.booted_after, width, height, colors, args.out))
        if colors < args.min_colors:
            print("FAIL: {} distinct colors is under the {} a rendered screen carries. A blank "
                  "panel is what a package that never drew looks like.".format(colors,
                                                                               args.min_colors),
                  file=sys.stderr)
            return 1
        return 0
    finally:
        emu.stop()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--boot1")
    parser.add_argument("--flash")
    parser.add_argument("--out", default="screen.ppm")
    parser.add_argument("--png")
    parser.add_argument("--deadline", type=float, default=180.0)
    parser.add_argument("--min-colors", type=int, default=16)
    parser.add_argument("--skip-when-absent", action="store_true",
                        help="Report a missing image as a skip rather than a failure, for CI "
                             "where the images are not present.")
    args = parser.parse_args(argv)

    try:
        return smoke(args)
    except Missing as absent:
        if args.skip_when_absent:
            note = "emulator smoke skipped, these are absent: {}".format(absent)
            print(note)
            summary = os.environ.get("GITHUB_STEP_SUMMARY")
            if summary:
                with open(summary, "a") as handle:
                    handle.write(note + "\n")
            return 0
        print("emulator smoke cannot run, these are absent: {}".format(absent), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
