import os
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "ndl-sdk"
RUNTIME = ROOT / "ndl"


class NamingArtifacts(unittest.TestCase):
    def test_installer_payloads_and_freshness(self):
        for name, package in (("installer-6.2", "ndl_installer_4.5.5-6.2.0-6.4.0.tns"),
                              ("persistent-6.4", "persistent_6.2.0-6.4.0.tns")):
            with self.subTest(installer=name):
                directory = RUNTIME / "src" / name
                binary = (directory / "ndl_installer.bin").read_bytes()
                self.assertIn(b"A:\\documents\\ndl\\ndl_resources.tns\0", binary)
                self.assertNotIn(b"\\ndless\\", binary)
                encoded = subprocess.check_output(["luajit", "-"], input=
                    (directory / "installer_payload.lua").read_bytes() + b"\nio.write(installer)\n")
                self.assertEqual(encoded, binary)
                output = RUNTIME / "calcbin" / package
                sources = ["stage0.S", "installer.lua", "template.sed", "Problem1_template.xml", "Problem1.xml"]
                if name == "installer-6.2":
                    sources += ["gui.lua", "ipc.lua"]
                for source in sources:
                    self.assertGreaterEqual(output.stat().st_mtime_ns, (directory / source).stat().st_mtime_ns, source)

    def test_template_inputs_invalidate_generated_xml(self):
        environment = dict(os.environ)
        environment["PATH"] = str(SDK / "bin") + os.pathsep + str(SDK / "toolchain/install/bin") + os.pathsep + environment["PATH"]
        for name in ("installer-6.2", "persistent-6.4"):
            sources = ["installer.lua", "template.sed", "Problem1_template.xml"]
            if name == "installer-6.2":
                sources += ["gui.lua", "ipc.lua"]
            for source in sources:
                with self.subTest(installer=name, source=source):
                    command = subprocess.check_output(["make", "-n", "-W", source, "Problem1.xml"],
                        cwd=RUNTIME / "src" / name, env=environment, text=True)
                    self.assertIn("sed -f template.sed", command)

    def test_resource_literals(self):
        elf = RUNTIME / "src/resources/ndl_resources_zehn.tns.elf"
        binary = elf.read_bytes()
        for value in (b"./ndl/startup", b"ndl.cfg.tns", b"/documents/ndl/persistent.tns", b"/documents/ndl/currentdoc.tns"):
            self.assertTrue(value + b"\0" in binary, value.decode())
        self.assertNotIn(b"/documents/ndless/", binary)
        self.assertNotIn(b"ndless.cfg.tns", binary)
        package = RUNTIME / "calcbin/ndl_resources.tns"
        self.assertGreaterEqual(package.stat().st_mtime_ns, elf.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
