import os
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ("ndl-sdk" if (ROOT / "ndl-sdk").exists() else "ndless-sdk")
RUNTIME = ROOT / ("ndl" if (ROOT / "ndl").exists() else "ndless")


class NamingContract(unittest.TestCase):
    def test_layout(self):
        self.assertTrue((ROOT / "ndl-sdk").is_dir())
        self.assertTrue((ROOT / "ndl").is_dir())
        self.assertFalse((ROOT / "ndless-sdk").exists())
        self.assertFalse((ROOT / "ndless").exists())

    def test_boot_paths(self):
        for installer in ("installer-6.2", "persistent-6.4"):
            with self.subTest(installer=installer):
                source = (RUNTIME / "src" / installer / "stage0.S").read_text()
                self.assertIn(r'A:\\documents\\ndl\\ndl_resources.tns', source)
                self.assertNotIn(r'\\ndless\\', source)

    def test_startup(self):
        source = (RUNTIME / "src/resources/ploaderhook.c").read_text()
        self.assertIn('file_each("./ndl/startup"', source)

    def test_persistence(self):
        source = (RUNTIME / "src/resources/persistency.c").read_text()
        self.assertIn('"/documents/ndl/persistent.tns"', source)
        self.assertIn('"/documents/ndl/currentdoc.tns"', source)
        self.assertNotIn('"/documents/ndless/', source)

    def test_config(self):
        source = (SDK / "libndls/config.c").read_text()
        self.assertIn('locate("ndl.cfg.tns"', source)
        self.assertIn('"ndl/ndl.cfg.tns"', source)
        self.assertTrue((RUNTIME / "calcbin/ndl.cfg.tns").is_file())

    def test_wrapper_root(self):
        output = subprocess.check_output([str(SDK / "bin/nspire-tools"), "path"], text=True)
        self.assertEqual(Path(output.strip()).resolve(), ROOT / "ndl-sdk")

    def test_new_toolchain_override(self):
        environment = dict(os.environ, NDL_TOOLCHAIN_PATH="/qualified/compiler")
        environment.pop("_NDLESS_TOOLCHAIN_PATH", None)
        output = subprocess.check_output([str(SDK / "bin/nspire-tools"), "_toolchainpath"],
                                         env=environment, text=True)
        self.assertEqual(output.strip(), "/qualified/compiler")

    def test_packaging_names(self):
        source = (RUNTIME / "src/resources/Makefile").read_text()
        self.assertIn("../../calcbin/ndl_resources.tns", source)
        source = (RUNTIME / "src/installer-6.2/Makefile").read_text()
        self.assertIn("ndl_installer_4.5.5-6.2.0-6.4.0.tns", source)
        self.assertIn("../../../ndl-sdk/tools/luna/luna", source)

    def test_source_abi_aliases(self):
        self.assertIn("assert_ndl_rev", (SDK / "include/libndls.h").read_text())
        self.assertIn("nl_ndl_rev", (SDK / "include/nucleus.h").read_text())
        source = (SDK / "include/zehn.h").read_text()
        self.assertIn("NDL_VERSION_MIN", source)
        self.assertIn("NDLESS_VERSION_MIN", source)

    def test_submodule_locations_preserve_upstream(self):
        source = (ROOT / ".gitmodules").read_text()
        self.assertNotIn("path = ndless-sdk/", source)
        self.assertIn("path = ndl-sdk/tools/luna", source)
        self.assertIn("https://github.com/ndless-nspire/luna", source)


if __name__ == "__main__":
    unittest.main()
