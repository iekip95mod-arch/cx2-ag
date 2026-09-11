import argparse
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="clang")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = Path(tempfile.mkdtemp(prefix="libnspire-diagnostics-"))
print(f"Test artifacts: {build}", flush=True)
includes = [f"-I{root}", f"-I{root / 'src'}", f"-I{root / 'src/services'}", f"-I{root / 'src/api'}"]
includes += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libusb-1.0"], text=True))
flags = ["-std=gnu11", "-g", "-O1", "-ffunction-sections", "-fdata-sections"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
decoder = build / "data.o"
subprocess.run([args.cc, *flags, *includes, "-Ddata_read=unused_data_read", "-Ddata_write=unused_data_write",
                "-Ddata_write_special=unused_data_write_special", "-c", str(root / "src/data.c"), "-o", str(decoder)], check=True)
binary = build / "diagnostics-tests"
strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
sources = [str(root / source) for source in ("src/service.c", "src/services/file.c", "src/services/dir.c", "tests/diagnostics_test.c")]
subprocess.run([args.cc, *flags, *includes, *sources, str(decoder), strip, "-o", str(binary)], check=True)
run = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
(build / "output.txt").write_text(run.stdout)
print(run.stdout, end="")
raise SystemExit(run.returncode)
