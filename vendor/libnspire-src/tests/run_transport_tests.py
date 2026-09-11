import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="clang")
parser.add_argument("--cxx", default="clang++")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--non-apple-usb", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = Path(tempfile.mkdtemp(prefix="libnspire-transport-"))
includes = [f"-I{root}", f"-I{root / 'src/api'}"]
includes += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libusb-1.0"], text=True))
flags = ["-g", "-O1"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
objects = []
for name in ("usb", "error", "init", "packet", "data", "service"):
    obj = build / f"{name}.o"
    platform = ["-U__APPLE__"] if args.non_apple_usb and name == "usb" else []
    subprocess.run([args.cc, "-std=gnu11", *includes, *flags, *platform, "-c", str(root / f"src/{name}.c"), "-o", str(obj)], check=True)
    objects.append(str(obj))
binary = build / "transport-tests"
platform = ["-DNSPIRE_TEST_NONAPPLE_USB"] if args.non_apple_usb else []
subprocess.run([args.cxx, "-std=gnu++20", *includes, *flags, *platform, str(root / "tests/transport.cpp"), *objects, "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=15)
print(f"Test artifacts: {build}")
