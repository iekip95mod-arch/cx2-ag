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
build = Path(tempfile.mkdtemp(prefix="libnspire-key-"))
print(f"Test artifacts: {build}", flush=True)
(build / "config.h").write_text(f"#define ENDIAN_{sys.byteorder.upper()} 1\n")
includes = [f"-I{build}", f"-I{root / 'src'}", f"-I{root / 'src/api'}"]
includes += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libusb-1.0"], text=True))
flags = ["-std=gnu11", "-g", "-O1", "-Werror=implicit-function-declaration"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
packet = build / "packet.o"
subprocess.run([args.cc, *flags, *includes, "-Dpacket_recv=unused_packet_recv", "-c",
                str(root / "src/packet.c"), "-o", str(packet)], check=True)
sources = [root / name for name in ("src/data.c", "src/service.c", "src/services/key.c", "tests/key_test.c")]
binary = build / "key-tests"
subprocess.run([args.cc, *flags, *includes, *map(str, sources), str(packet), "-o", str(binary)], check=True)
if binary.stat().st_mtime_ns < max(path.stat().st_mtime_ns for path in sources):
    raise RuntimeError("key test binary predates its sources")
subprocess.run([str(binary)], check=True, timeout=15)
