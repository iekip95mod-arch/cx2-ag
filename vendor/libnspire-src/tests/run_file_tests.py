import argparse
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="clang")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--baseline", action="store_true")
parser.add_argument("--case", choices=("read-tag", "dir-final", "short-status"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = Path(tempfile.mkdtemp(prefix="libnspire-file-"))
includes = [f"-I{root}", f"-I{root / 'src'}", f"-I{root / 'src/services'}", f"-I{root / 'src/api'}"]
includes += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libusb-1.0"], text=True))
flags = ["-std=gnu11", "-g", "-O1", "-ffunction-sections", "-fdata-sections"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
decoder = build / "data.o"
subprocess.run([args.cc, *flags, *includes, "-Ddata_read=unused_data_read", "-Ddata_write=unused_data_write",
                "-c", str(root / "src/data.c"), "-o", str(decoder)], check=True)
sources = []
for name in ("file", "dir"):
    source = root / f"src/services/{name}.c"
    if args.baseline:
        source = build / f"{name}.c"
        source.write_bytes(subprocess.check_output(["git", "show", f"HEAD:src/services/{name}.c"], cwd=root))
    sources.append(str(source))
binary = build / "file-tests"
strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
subprocess.run([args.cc, *flags, *includes, *sources, str(root / "tests/file_test.c"),
                str(decoder), strip, "-o", str(binary)], check=True)
subprocess.run([str(binary), *([args.case] if args.case else [])], check=True, timeout=15)
print(f"Test artifacts: {build}")
