#!/usr/bin/env python3
"""Read the PPM the debugger's screenshot command writes, and turn it into a PNG.

core/debug.cpp writes a binary P6 at 320x240. Nothing here shells out to sips or ImageMagick,
because a screenshot that only works on one of the three platforms this repository builds on is
not evidence anybody else can reproduce.
"""
from __future__ import annotations

import struct
import zlib


class NotAScreen(Exception):
    pass


def read_ppm(path: str) -> tuple:
    """Return (width, height, rgb_bytes) for a binary P6 file."""
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise NotAScreen("{} is not a binary PPM, so it is not what screenshot writes".format(path))

    fields = []
    at = 2
    while len(fields) < 3:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b"#":
            while at < len(data) and data[at:at + 1] != b"\n":
                at += 1
            continue
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        if at == start:
            raise NotAScreen("the header of {} ended before its three numbers".format(path))
        fields.append(int(data[start:at]))
    at += 1  # the single whitespace byte that ends the header

    width, height, maxval = fields
    if maxval != 255:
        raise NotAScreen("{} has {} levels per channel, not 255".format(path, maxval))
    pixels = data[at:]
    wanted = width * height * 3
    if len(pixels) < wanted:
        raise NotAScreen("{} holds {} bytes of pixels for {}x{}, which wants {}".format(
            path, len(pixels), width, height, wanted))
    return width, height, pixels[:wanted]


def distinct_colors(pixels: bytes) -> int:
    return len({pixels[at:at + 3] for at in range(0, len(pixels), 3)})


def differing_pixels(one: bytes, two: bytes) -> int:
    """How many pixels two frames disagree on. Frames of different sizes never match."""
    if len(one) != len(two):
        return max(len(one), len(two)) // 3
    return sum(1 for at in range(0, len(one), 3) if one[at:at + 3] != two[at:at + 3])


def write_png(path: str, width: int, height: int, pixels: bytes) -> None:
    raw = b"".join(b"\x00" + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        handle.write(chunk(b"IEND", b""))
