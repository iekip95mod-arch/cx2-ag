#!/usr/bin/env python3
"""Checks for the emulator screen reader and the smoke runner's skip path.

Neither needs an emulator or a flash image, which is the point: the parts that decide whether a
screenshot is a screen, and whether a missing image is a skip or a failure, are the parts that have
to keep working on a runner that has neither.
"""
import os
import struct
import sys
import tempfile
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[3] / "tools" / "emu"
sys.path.insert(0, str(TOOLS))

import emurun
import screen


def ppm(width, height, pixels, maxval=255, magic=b"P6", comment=False):
    header = magic + b"\n"
    if comment:
        header += b"# written by a test\n"
    header += b"%d %d\n%d\n" % (width, height, maxval)
    return header + pixels


def write(tmp, name, data):
    path = os.path.join(tmp, name)
    with open(path, "wb") as handle:
        handle.write(data)
    return path


def refuses(call, because):
    try:
        call()
    except screen.NotAScreen:
        return
    raise AssertionError("accepted " + because)


def check_reading(tmp):
    pixels = bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])
    width, height, got = screen.read_ppm(write(tmp, "ok.ppm", ppm(2, 2, pixels)))
    assert (width, height, got) == (2, 2, pixels), (width, height, got)

    # A comment between the header fields is legal and the debugger could start writing one.
    _, _, got = screen.read_ppm(write(tmp, "comment.ppm", ppm(2, 2, pixels, comment=True)))
    assert got == pixels

    # Trailing bytes past the frame are ignored rather than treated as pixels.
    _, _, got = screen.read_ppm(write(tmp, "extra.ppm", ppm(2, 2, pixels + b"junk")))
    assert got == pixels

    refuses(lambda: screen.read_ppm(write(tmp, "p3.ppm", ppm(2, 2, pixels, magic=b"P3"))),
            "an ASCII PPM")
    refuses(lambda: screen.read_ppm(write(tmp, "deep.ppm", ppm(2, 2, pixels, maxval=65535))),
            "a 16 bit PPM")
    refuses(lambda: screen.read_ppm(write(tmp, "short.ppm", ppm(2, 2, pixels[:-3]))),
            "a frame three bytes short")
    refuses(lambda: screen.read_ppm(write(tmp, "empty.ppm", b"P6\n")),
            "a header that stops after the magic")


def check_measuring():
    assert screen.distinct_colors(bytes([0, 0, 0, 0, 0, 0])) == 1
    assert screen.distinct_colors(bytes([0, 0, 0, 1, 1, 1])) == 2

    black = bytes(12)
    assert screen.differing_pixels(black, black) == 0
    one = bytearray(black)
    one[3] = 9
    assert screen.differing_pixels(black, bytes(one)) == 1
    # A frame that changed size is wholly different rather than silently comparable.
    assert screen.differing_pixels(black, bytes(6)) == 4


def check_png(tmp):
    pixels = bytes(range(12))
    path = os.path.join(tmp, "out.png")
    screen.write_png(path, 2, 2, pixels)
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "no PNG signature"

    length = struct.unpack(">I", data[8:12])[0]
    assert data[12:16] == b"IHDR"
    width, height, depth, color = struct.unpack(">IIBB", data[16:16 + 10])
    assert (width, height, depth, color) == (2, 2, 8, 2), (width, height, depth, color)

    at = 8 + 8 + length + 4
    assert data[at + 4:at + 8] == b"IDAT"
    size = struct.unpack(">I", data[at:at + 4])[0]
    raw = zlib.decompress(data[at + 8:at + 8 + size])
    # Every row is a zero filter byte then its pixels, which is what the encoder claims to write.
    assert raw == b"\x00" + pixels[:6] + b"\x00" + pixels[6:], raw


def check_resolution(tmp):
    absent = os.path.join(tmp, "not-here.img")
    try:
        emurun.resolve(absent, absent)
    except emurun.Missing as missing:
        # Both paths are named, so a report cannot leave one of them to be guessed at.
        assert str(missing).count(absent) == 2, str(missing)
    else:
        raise AssertionError("resolve accepted two paths that do not exist")


def check_skip_path(tmp, capture):
    absent = os.path.join(tmp, "not-here.img")
    common = ["--boot1", absent, "--flash", absent]

    code = emurun.main(common + ["--skip-when-absent"])
    assert code == 0, "a skip has to pass, got {}".format(code)

    code = emurun.main(common)
    assert code == 2, "an unskipped missing image has to be distinct from a screen failure, got {}".format(code)

    # The skip has to reach the job summary, or nobody reading the run learns the stage never ran.
    os.environ["GITHUB_STEP_SUMMARY"] = capture
    try:
        emurun.main(common + ["--skip-when-absent"])
    finally:
        del os.environ["GITHUB_STEP_SUMMARY"]
    assert "skipped" in Path(capture).read_text(), "the skip never reached the step summary"


def main():
    with tempfile.TemporaryDirectory() as tmp:
        check_reading(tmp)
        check_measuring()
        check_png(tmp)
        check_resolution(tmp)
        check_skip_path(tmp, os.path.join(tmp, "summary.md"))
    print("emu screen and skip-path checks passed")


if __name__ == "__main__":
    main()
