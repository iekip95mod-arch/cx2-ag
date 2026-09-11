import argparse
import os
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("--firebird-build", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--sanitize", action="store_true")
arguments = parser.parse_args()
root = Path(__file__).resolve().parents[1]
firebird = root.parent / "firebird-src"
sdk = root / "ndl-sdk"
toolchain = sdk / "toolchain/install/bin"
output = arguments.output.resolve()
output.mkdir()
environment = dict(os.environ)
environment["PATH"] = str(sdk / "bin") + os.pathsep + str(toolchain) + os.pathsep + environment["PATH"]
subprocess.run([str(sdk / "bin/nspire-gcc"), "-Os", "-marm", "-std=gnu11", "-c",
                str(root / "ndl/src/resources/ints.c"), "-o", str(output / "ints.o")],
               env=environment, check=True)
subprocess.run([str(sdk / "bin/nspire-g++"), "-O3", "-marm", "-std=c++11", "-ffunction-sections",
                "-fdata-sections", "-c", str(sdk / "libsyscalls/stubs.cpp"), "-o", str(output / "stubs.o")],
               env=environment, check=True)
subprocess.run([str(toolchain / "arm-none-eabi-ld"), "-Ttext=0x10040000", "-e", "ints_swi_handler",
                "--gc-sections", "--undefined=luaL_optnumber", "--undefined=luaL_error",
                "--undefined=usbd_do_request_flags",
                "--defsym=sc_ext_table=0x10003000", "--defsym=emu_sysc_table=0x10003000",
                "--defsym=ut_next_descriptor=0", str(output / "ints.o"), str(output / "stubs.o"),
                "-o", str(output / "ints.elf")],
               check=True)
subprocess.run([str(toolchain / "arm-none-eabi-objcopy"), "-O", "binary",
                str(output / "ints.elf"), str(output / "ints.bin")], check=True)
symbols = {}
for line in subprocess.check_output([str(toolchain / "arm-none-eabi-nm"), str(output / "ints.elf")],
                                    text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = fields[0]
objects = [str(path) for path in arguments.firebird_build.resolve().glob("*.o")
           if path.name not in ("main.o", "usblinktest.o")]
sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"] if arguments.sanitize else []
subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", *sanitizers, "-DNO_TRANSLATION", "-DSUPPORT_LINUX",
                "-I" + str(firebird), "-I" + str(firebird / "core"),
                str(root / "tests/swi_dispatch_harness.cpp"), *objects, "-lz", "-pthread",
                "-o", str(output / "swi-dispatch")], check=True)
try:
    completed = subprocess.run([str(output / "swi-dispatch"), str(output / "ints.bin"),
                                symbols["ints_swi_handler"], symbols["sc_addrs_ptr"], symbols["luaL_optnumber"],
                                symbols["luaL_error"], symbols["usbd_do_request_flags"]],
                               capture_output=True, text=True, timeout=30)
except subprocess.TimeoutExpired as timeout:
    transcript = (timeout.stdout or b"") + (timeout.stderr or b"")
    (output / "checks.log").write_bytes(transcript)
    print(transcript.decode(errors="replace"), end="")
    raise SystemExit("syscall execution exceeded 30 seconds")
(output / "checks.log").write_text(completed.stdout + completed.stderr)
print(completed.stdout, end="")
print(completed.stderr, end="")
raise SystemExit(completed.returncode)
