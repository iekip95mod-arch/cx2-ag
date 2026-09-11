import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "ndl/src/resources"


class LoaderDiagnostics(unittest.TestCase):
    def test_prg_loader(self):
        header = (RESOURCES / "ndl.h").read_text()
        diagnostic = header[header.index("struct ld_diagnostic {"):header.index("void ld_set_resident")]
        source = (RESOURCES / "ploaderhook.c").read_text()
        source = source[source.index("static int ndl_load("):source.index("int ld_exec(")]
        harness = (ROOT / "tests/prg_loader_harness.c").read_text()
        with tempfile.TemporaryDirectory(prefix="ndl-prg-") as directory:
            work = Path(directory)
            translation_unit = work / "prg.c"
            translation_unit.write_text(harness.replace("DIAGNOSTIC_SOURCE", diagnostic).replace("PRG_SOURCE", source))
            binary = work / "prg"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(translation_unit), "-o", str(binary)], check=True)
            process = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_lua_diagnostics(self):
        header = (RESOURCES / "ndl.h").read_text()
        diagnostic = header[header.index("struct ld_diagnostic {"):header.index("void ld_set_resident")]
        source = (RESOURCES / "luaext.c").read_text()
        source = source[source.index("#define LUAEXT_MAX_MODULES"):source.index("static int ndl_uninstall")]
        harness = (ROOT / "tests/lua_loader_harness.c").read_text()
        with tempfile.TemporaryDirectory(prefix="ndl-lua-") as directory:
            work = Path(directory)
            translation_unit = work / "require.c"
            translation_unit.write_text(harness.replace("DIAGNOSTIC_SOURCE", diagnostic).replace("REQUIRE_SOURCE", source))
            binary = work / "require"
            flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "luajit"], text=True).split()
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(translation_unit), "-o", str(binary)] + flags, check=True)
            process = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_outer_loader(self):
        header = (RESOURCES / "ndl.h").read_text()
        diagnostic = header[header.index("struct ld_diagnostic {"):header.index("void ld_set_resident")]
        source = (RESOURCES / "ploaderhook.c").read_text()
        source = source[source.index("int ld_exec_with_args("):source.index("// To free the program")]
        harness = (ROOT / "tests/ploader_harness.c").read_text()
        with tempfile.TemporaryDirectory(prefix="ndl-exec-") as directory:
            work = Path(directory)
            translation_unit = work / "loader.c"
            translation_unit.write_text(harness.replace("DIAGNOSTIC_SOURCE", diagnostic).replace("LOADER_SOURCE", source))
            binary = work / "loader"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(translation_unit), "-o", str(binary)], check=True)
            process = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_compressed_allocation_failure(self):
        source = (RESOURCES / "zehn_loader.cpp").read_text()
        source = re.sub(r"^#include .*$", "", source, flags=re.MULTILINE)
        source = source.replace("reinterpret_cast<uint32_t>(base)",
                                "static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base))")
        harness = (ROOT / "tests/zehn_loader_harness.cpp").read_text()
        header = (RESOURCES / "ndl.h").read_text()
        diagnostic = header[header.index("struct ld_diagnostic {"):header.index("void ld_set_resident")]
        declarations = (RESOURCES / "zehn_loader.h").read_text()
        with tempfile.TemporaryDirectory(prefix="ndl-loader-") as directory:
            work = Path(directory)
            translation_unit = work / "zehn.cpp"
            translation_unit.write_text(harness.replace("LOAD_SOURCE", diagnostic + declarations + source))
            binary = work / "zehn"
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I", str(ROOT / "ndl-sdk/include"), str(translation_unit),
                            "-o", str(binary)], check=True)
            process = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)


if __name__ == "__main__":
    unittest.main()
