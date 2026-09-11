import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Build nsptool from the current libnspire sources")
    parser.add_argument("--output", type=Path, default=root / "tools/nsptool/nsptool.new")
    options = parser.parse_args()
    output = options.output.expanduser().absolute()
    cc = shlex.split(os.environ.get("CC", "clang"))
    cxx = shlex.split(os.environ.get("CXX", "clang++"))
    usb_cflags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "libusb-1.0"], text=True))
    usb_libs = shlex.split(subprocess.check_output(
        ["pkg-config", "--libs", "libusb-1.0"], text=True))
    library = root / "vendor/libnspire-src/src"
    sources = [library / name for name in (
        "data.c", "error.c", "init.c", "packet.c", "service.c", "usb.c", "cx2.cpp",
        "services/devinfo.c", "services/dir.c", "services/file.c", "services/os.c",
        "services/raw.c", "services/screenshot.c", "services/key.c",
    )]
    sources.append(root / "tools/nsptool/nsptool.c")
    with tempfile.TemporaryDirectory(prefix=".nsptool-build-", dir=output.parent) as directory:
        build = Path(directory)
        (build / "config.h").write_text(f"#define ENDIAN_{sys.byteorder.upper()} 1\n")
        includes = [f"-I{build}", f"-I{library}", f"-I{library / 'api'}", *usb_cflags]
        objects = []
        for source in sources:
            obj = build / f"{source.parent.name}-{source.stem}.o"
            compiler = cxx if source.suffix == ".cpp" else cc
            standard = "gnu++20" if source.suffix == ".cpp" else "gnu11"
            subprocess.run([*compiler, f"-std={standard}", "-O2", "-pthread", *includes,
                            "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        candidate = build / "nsptool"
        subprocess.run([*cxx, "-pthread", *objects, *usb_libs, "-o", str(candidate)], check=True)
        version = subprocess.run([str(candidate), "session-version"], capture_output=True,
                                 text=True, check=True, timeout=5)
        if version.stdout.strip() != "1":
            raise RuntimeError(f"Unexpected session protocol: {version.stdout!r}")
        os.replace(candidate, output)
    print(output)


if __name__ == "__main__":
    main()
