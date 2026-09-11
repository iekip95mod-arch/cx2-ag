#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


AUDITOR: Path | None = None


CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles><rootfile full-path="OPS/package.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""


def package(include_title: bool = True, include_navigation: bool = True) -> str:
    title = "<dc:title>Synthetic Physics</dc:title>" if include_title else ""
    navigation = (
        '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
        if include_navigation
        else ""
    )
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0">
  <metadata>{title}</metadata>
  <manifest>
    {navigation}
    <item id="c1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="chapter2.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c1"/><itemref idref="c2"/></spine>
</package>
"""


NAVIGATION = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
  <body><nav epub:type="toc"><ol>
    <li><a href="chapter1.xhtml">1 Measurement</a></li>
    <li><a href="chapter2.xhtml">2 Motion</a></li>
  </ol></nav></body>
</html>
"""


CHAPTER_ONE = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
  <h1>1 Measurement</h1>
  <h2>1-1 Units</h2>
  <h4>Sample Problem 1: Private synthetic launch</h4>
  <p>A private synthetic prompt must not escape the audit.</p>
  <h4>Checkpoint 1</h4>
  <h3>Proof of a synthetic identity</h3>
  <img src="one.png" alt="Private synthetic diagram description"/>
  <img src="two.png" alt=""/>
</body></html>
"""


CHAPTER_TWO = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
  <h1>2 Motion</h1>
  <h2>2-1 Position</h2>
  <h4><span>Sample Problem</span> 2</h4>
  <h4>Formal Derivation is intentionally excluded</h4>
  <img src="three.png" alt="Motion diagram"/>
</body></html>
"""


def write_epub(
    path: Path,
    *,
    include_container: bool = True,
    container: str = CONTAINER,
    include_title: bool = True,
    include_navigation: bool = True,
) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        if include_container:
            archive.writestr("META-INF/container.xml", container)
        archive.writestr("OPS/package.opf", package(include_title, include_navigation))
        if include_navigation:
            archive.writestr("OPS/nav.xhtml", NAVIGATION)
        archive.writestr("OPS/chapter1.xhtml", CHAPTER_ONE)
        archive.writestr("OPS/chapter2.xhtml", CHAPTER_TWO)


class CorpusAuditTest(unittest.TestCase):
    def run_audit(self, epub: Path, metadata_only: bool = True) -> subprocess.CompletedProcess[str]:
        assert AUDITOR is not None
        command = [str(AUDITOR)]
        if metadata_only:
            command.append("--metadata-only")
        command.append(str(epub))
        return subprocess.run(command, text=True, capture_output=True, check=False)

    def test_metadata_only_report_is_structural_and_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            epub = Path(directory) / "synthetic.epub"
            write_epub(epub)
            completed = self.run_audit(epub)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads(completed.stdout)

            self.assertEqual(report["schema_version"], 1)
            self.assertEqual(report["mode"], "metadata-only")
            self.assertEqual(report["corpus_sha256"], hashlib.sha256(epub.read_bytes()).hexdigest())
            self.assertEqual(
                report["totals"],
                {
                    "chapters": 2,
                    "sections": 2,
                    "samples": 2,
                    "checkpoints": 1,
                    "proof_derivation_headings": 1,
                    "images": 3,
                    "images_with_nonempty_alt": 2,
                },
            )
            self.assertEqual([chapter["title"] for chapter in report["chapters"]], ["Measurement", "Motion"])
            self.assertEqual(report["confidence"]["samples"], "medium")
            warning_codes = {item["code"] for item in report["warnings"]}
            self.assertIn("heading-classification-heuristic", warning_codes)
            self.assertIn("image-alt-missing", warning_codes)
            self.assertNotIn("Private synthetic launch", completed.stdout)
            self.assertNotIn("A private synthetic prompt", completed.stdout)
            self.assertNotIn("Private synthetic diagram", completed.stdout)

    def test_missing_metadata_uses_warned_spine_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            epub = Path(directory) / "missing-metadata.epub"
            write_epub(epub, include_title=False, include_navigation=False)
            completed = self.run_audit(epub)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads(completed.stdout)
            warning_codes = {item["code"] for item in report["warnings"]}
            self.assertEqual(report["book"]["title"], None)
            self.assertEqual(report["totals"]["chapters"], 2)
            self.assertEqual(report["confidence"]["chapters"], "medium")
            self.assertIn("package-title-missing", warning_codes)
            self.assertIn("navigation-document-missing", warning_codes)
            self.assertIn("chapter-discovery-spine-fallback", warning_codes)

    def test_missing_container_is_an_explicit_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            epub = Path(directory) / "missing-container.epub"
            write_epub(epub, include_container=False)
            completed = self.run_audit(epub)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("required EPUB entry is missing: META-INF/container.xml", completed.stderr)
            self.assertEqual(completed.stdout, "")

    def test_malformed_container_is_an_explicit_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            epub = Path(directory) / "malformed-container.epub"
            write_epub(epub, container="<container><rootfiles>")
            completed = self.run_audit(epub)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("malformed EPUB container metadata", completed.stderr)
            self.assertEqual(completed.stdout, "")

    def test_metadata_only_flag_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            epub = Path(directory) / "synthetic.epub"
            write_epub(epub)
            completed = self.run_audit(epub, metadata_only=False)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("--metadata-only", completed.stderr)


def main() -> int:
    global AUDITOR
    if len(sys.argv) != 2:
        print("usage: corpus_audit_test.py AUDITOR", file=sys.stderr)
        return 2
    AUDITOR = Path(sys.argv.pop()).resolve()
    if not AUDITOR.is_file():
        print(f"auditor not found: {AUDITOR}", file=sys.stderr)
        return 2
    return 0 if unittest.main(exit=False).result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
