from pathlib import Path
import os
import struct
import subprocess
import sys


root = Path(__file__).resolve().parents[1]
sdk = root / "ndl-sdk"
output = Path(sys.argv[1])
output.mkdir()
environment = dict(os.environ)
environment["PATH"] = str(sdk / "bin") + os.pathsep + str(sdk / "toolchain/install/bin") + os.pathsep + environment["PATH"]
(output / "ld_diag_bad.luax.tns").write_bytes(struct.pack("<8I", 0x6e68655a, 2, 32, 0, 0, 0, 32, 0))
(output / "ld_diag_zlib.luax.tns").write_bytes(struct.pack("<9I", 0x6e68655a, 1, 44, 1, 0, 0, 52, 0, 3) + b"invalid!")
subprocess.run([str(sdk / "bin/nspire-ld"), str(root / "tests/loader_probe_entry.c"),
                "-Os", "-o", str(output / "ld_diag_entry.elf")], env=environment, check=True)
subprocess.run([str(sdk / "bin/genzehn"), "--input", str(output / "ld_diag_entry.elf"),
                "--output", str(output / "ld_diag_entry.luax.tns"), "--name", "ld_diag_entry",
                "--uses-lcd-blit", "true", "--240x320-support", "true"], env=environment, check=True)
subprocess.run([str(sdk / "tools/luna/luna"), str(root / "tests/loader_probe.lua"),
                str(output / "loader_probe.tns")], check=True)
