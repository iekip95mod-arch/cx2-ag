import argparse
from pathlib import Path
import subprocess
import tempfile
import time


def selected_row(screenshot):
    magic, dimensions, maximum, pixels = screenshot.read_bytes().split(b"\n", 3)
    assert (magic, dimensions, maximum) == (b"P6", b"320 240", b"255")
    assert len(pixels) == 320 * 240 * 3
    selected = []
    for y in range(41, 240):
        blue = 0
        for x in range(20, 240):
            offset = (y * 320 + x) * 3
            red, green, blue_channel = pixels[offset:offset + 3]
            blue += red < 80 and 70 < green < 180 and blue_channel > 150
        if blue > 100:
            selected.append(y)
    assert selected, "No document-browser selection found"
    assert selected[-1] - selected[0] + 1 == len(selected), "Multiple selection bands"
    assert len(selected) == 27, "Expected a complete document-browser row"
    return selected[0]


def main():
    parser = argparse.ArgumentParser(description="Check down from My Documents on the connected handheld")
    parser.add_argument("--nsptool", type=Path,
                        default=Path(__file__).resolve().parents[2] / "nsptool" / "nsptool.new")
    options = parser.parse_args()

    def run(*arguments):
        completed = subprocess.run([str(options.nsptool), *arguments],
                                   capture_output=True, text=True, timeout=30)
        if completed.returncode:
            raise RuntimeError(completed.stderr.strip() or completed.stdout.strip())
        time.sleep(0.5)
        return completed.stdout

    def key(name):
        receipt = run("key", name)
        assert "keysvc accepted 1 of 1" in receipt, receipt

    with tempfile.TemporaryDirectory(prefix="keysvc-navigation-") as directory:
        before = Path(directory) / "before.ppm"
        after = Path(directory) / "after.ppm"
        released = Path(directory) / "released.ppm"
        run("screenshot", str(before))
        first_row = selected_row(before)
        assert first_row == 41, "Start with My Documents selected"
        key("down")
        run("screenshot", str(after))
        assert selected_row(after) == first_row + 27, "down did not select the next row"
        key("up")
        run("screenshot", str(before))
        assert selected_row(before) == first_row, "up did not restore My Documents"
        try:
            key("+down")
            run("screenshot", str(after))
        finally:
            key("-down")
        run("screenshot", str(released))
        assert selected_row(after) == first_row + 27, "press did not select the next row"
        assert selected_row(released) == first_row + 27, "release changed the selection"
        print("PASS: down tap and explicit press/release select the next document row")


if __name__ == "__main__":
    main()
