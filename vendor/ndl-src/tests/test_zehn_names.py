import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "ndl-sdk"
GENZEHN = os.environ.get("GENZEHN", str(SDK / "bin/genzehn"))


class ZehnNames(unittest.TestCase):
    def test_wrappers_do_not_create_user_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            user = work / "user"
            environment = dict(os.environ, USERPROFILE=str(user))
            environment["PATH"] = str(SDK / "bin") + os.pathsep + str(SDK / "toolchain/install/bin") + os.pathsep + environment["PATH"]
            commands = [[str(SDK / "bin/nspire-tools"), "path"],
                        [str(SDK / "bin/nspire-gcc"), "-fsyntax-only", "-x", "c", "-"],
                        [str(SDK / "bin/nspire-g++"), "-fsyntax-only", "-x", "c++", "-"]]
            for command in commands:
                with self.subTest(wrapper=command[0]):
                    user = work / Path(command[0]).name
                    environment["USERPROFILE"] = str(user)
                    process = subprocess.run(command, input="int main(void) { return 0; }",
                                             env=environment, capture_output=True, text=True)
                    self.assertEqual(process.returncode, 0, process.stderr)
                    self.assertFalse(user.exists(), command[0] + " created an optional user directory")
            user = work / "linker-user"
            environment["USERPROFILE"] = str(user)
            source = work / "main.c"
            source.write_text("int main(void) { return 0; }\n")
            process = subprocess.run([str(SDK / "bin/nspire-ld"), str(source), "-o", str(work / "main.elf")],
                                     env=environment, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertFalse(user.exists(), "linker created an optional user directory")

    def test_revision_aliases_keep_binary_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "aliases.cpp"
            source.write_text('#include <os.h>\n#include <zehn.h>\n'
                'static_assert(static_cast<unsigned>(Zehn_flag_type::NDL_VERSION_MIN) == 0, "version min");\n'
                'static_assert(static_cast<unsigned>(Zehn_flag_type::NDL_VERSION_MAX) == 1, "version max");\n'
                'static_assert(static_cast<unsigned>(Zehn_flag_type::NDL_REVISION_MIN) == 2, "revision min");\n'
                'static_assert(static_cast<unsigned>(Zehn_flag_type::NDL_REVISION_MAX) == 3, "revision max");\n'
                'static_assert(Zehn_flag_type::NDL_VERSION_MIN == Zehn_flag_type::NDLESS_VERSION_MIN, "legacy");\n'
                'unsigned probe() { assert_ndl_rev(2022); return nl_ndl_rev(); }\n')
            output = work / "aliases.o"
            subprocess.run([str(SDK / "toolchain/install/bin/arm-none-eabi-g++"),
                            "-std=c++11", "-I", str(SDK / "include"), "-c", str(source),
                            "-o", str(output)], check=True)
            symbols = subprocess.check_output([str(SDK / "toolchain/install/bin/arm-none-eabi-nm"),
                                                "-u", str(output)], text=True)
            self.assertIn(" U assert_ndless_rev", symbols)
            self.assertIn(" U nl_ndless_rev", symbols)
            self.assertNotIn(" U assert_ndl_rev", symbols)
            self.assertNotIn(" U nl_ndl_rev", symbols)

    def test_version_options_preserve_wire_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "entry.c"
            source.write_text("void _start(void) {}\n")
            elf = work / "entry.elf"
            subprocess.run([str(SDK / "toolchain/install/bin/arm-none-eabi-gcc"),
                            "-nostdlib", "-fPIE", "-Wl,-Ttext=0", str(source),
                            "-o", str(elf)], check=True)
            outputs = []
            for prefix in ("ndl", "ndless"):
                output = work / (prefix + ".tns")
                subprocess.run([GENZEHN, "--input", str(elf), "--output", str(output),
                                "--" + prefix + "-min", "31",
                                "--" + prefix + "-max", "64",
                                "--" + prefix + "-rev-min", "2022",
                                "--" + prefix + "-rev-max", "3000"], check=True)
                outputs.append(output.read_bytes())
            self.assertEqual(outputs[0], outputs[1])
            header = struct.unpack_from("<8I", outputs[0])
            offset = 32 + header[3] * 4
            flags = dict((word & 255, word >> 8) for (word,) in
                         struct.iter_unpack("<I", outputs[0][offset:offset + header[4] * 4]))
            for flag, value in ((0, 31), (1, 64), (2, 2022), (3, 3000)):
                self.assertEqual(flags[flag], value)
            refused = work / "refused.tns"
            refused.write_bytes(b"retain")
            conflict = subprocess.run([GENZEHN, "--input", str(elf), "--output", str(refused),
                                       "--ndl-min", "31", "--ndless-min", "64"],
                                      capture_output=True, text=True)
            self.assertEqual(conflict.returncode, 1)
            self.assertIn("Conflicting", conflict.stderr)
            self.assertEqual(refused.read_bytes(), b"retain")


if __name__ == "__main__":
    unittest.main()
