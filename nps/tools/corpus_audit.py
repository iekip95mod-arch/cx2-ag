#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import sys
import zipfile
from pathlib import Path
from urllib.parse import unquote, urlsplit
from xml.etree import ElementTree


CONTAINER_PATH = "META-INF/container.xml"
CONTAINER_LIMIT = 1024 * 1024
PACKAGE_LIMIT = 16 * 1024 * 1024
NAVIGATION_LIMIT = 16 * 1024 * 1024
CONTENT_LIMIT = 32 * 1024 * 1024
TOTAL_CONTENT_LIMIT = 512 * 1024 * 1024
EPUB_NAMESPACE = "http://www.idpf.org/2007/ops"
CHAPTER_PREFIX_LABEL = re.compile(
    r"^chapter\s+(?P<number>[1-9][0-9]{0,2})\s*(?P<title>[^0-9\s].*)$",
    re.IGNORECASE,
)
CHAPTER_LABEL = re.compile(
    r"^(?P<number>[1-9][0-9]{0,2})(?:\s*[.:]\s*|\s+)(?P<title>\S.*)$",
    re.IGNORECASE,
)
SECTION_LABEL = re.compile(r"^[1-9][0-9]{0,2}\s*[-\u2013\u2014]\s*[0-9]+")
PROOF_LABEL = re.compile(r"^(?:proof|derivation)\b", re.IGNORECASE)


class CorpusAuditError(Exception):
    pass


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def normalized_text(element: ElementTree.Element) -> str:
    return " ".join(" ".join(element.itertext()).split())


def warning(code: str, message: str) -> dict[str, str]:
    return {"code": code, "message": message}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise CorpusAuditError(f"cannot read EPUB: {error}") from error
    return digest.hexdigest()


def read_entry(archive: zipfile.ZipFile, name: str, limit: int) -> bytes:
    try:
        entry = archive.getinfo(name)
    except KeyError as error:
        raise CorpusAuditError(f"required EPUB entry is missing: {name}") from error
    if entry.file_size > limit:
        raise CorpusAuditError(f"EPUB entry exceeds the metadata audit limit: {name}")
    try:
        return archive.read(entry)
    except (OSError, RuntimeError, zipfile.BadZipFile) as error:
        raise CorpusAuditError(f"cannot read EPUB entry {name}: {error}") from error


def parse_xml(content: bytes, description: str) -> ElementTree.Element:
    try:
        return ElementTree.fromstring(content)
    except ElementTree.ParseError as error:
        raise CorpusAuditError(f"malformed {description}: {error}") from error


def entry_path(base: str, href: str) -> str | None:
    parsed = urlsplit(href)
    if parsed.scheme or parsed.netloc:
        return None
    decoded = unquote(parsed.path)
    if not decoded:
        return None
    joined = posixpath.normpath(posixpath.join(base, decoded))
    if joined == ".." or joined.startswith("../") or joined.startswith("/"):
        return None
    return joined


def package_document(archive: zipfile.ZipFile) -> tuple[str, ElementTree.Element, list[dict[str, str]]]:
    root = parse_xml(read_entry(archive, CONTAINER_PATH, CONTAINER_LIMIT), "EPUB container metadata")
    rootfiles = [
        element.attrib.get("full-path", "").strip()
        for element in root.iter()
        if local_name(element.tag) == "rootfile" and element.attrib.get("full-path", "").strip()
    ]
    if not rootfiles:
        raise CorpusAuditError("EPUB container metadata has no package document")
    warnings: list[dict[str, str]] = []
    if len(rootfiles) > 1:
        warnings.append(warning("multiple-package-documents", "Multiple package documents were declared; the first was audited."))
    path = posixpath.normpath(rootfiles[0])
    if path == ".." or path.startswith("../") or path.startswith("/"):
        raise CorpusAuditError("EPUB package document path escapes the archive root")
    root = parse_xml(read_entry(archive, path, PACKAGE_LIMIT), "EPUB package metadata")
    return path, root, warnings


def package_structure(
    package_path: str,
    root: ElementTree.Element,
    warnings: list[dict[str, str]],
) -> tuple[str | None, dict[str, dict[str, str]], list[str], str | None]:
    title = next(
        (normalized_text(element) for element in root.iter() if local_name(element.tag) == "title" and normalized_text(element)),
        None,
    )
    if title is None:
        warnings.append(warning("package-title-missing", "The package metadata has no nonempty title."))

    base = posixpath.dirname(package_path)
    manifest: dict[str, dict[str, str]] = {}
    nav_candidates: list[str] = []
    for element in root.iter():
        if local_name(element.tag) != "item":
            continue
        item_id = element.attrib.get("id", "").strip()
        href = element.attrib.get("href", "").strip()
        if not item_id or not href:
            warnings.append(warning("manifest-item-incomplete", "A manifest item without both id and href was ignored."))
            continue
        path = entry_path(base, href)
        if path is None:
            warnings.append(warning("manifest-href-unsupported", "A manifest href was external, empty, or escaped the archive root."))
            continue
        if item_id in manifest:
            warnings.append(warning("manifest-id-duplicate", "A duplicate manifest id was ignored."))
            continue
        properties = element.attrib.get("properties", "").split()
        manifest[item_id] = {
            "path": path,
            "media_type": element.attrib.get("media-type", "").strip(),
        }
        if "nav" in properties:
            nav_candidates.append(path)

    spine: list[str] = []
    for element in root.iter():
        if local_name(element.tag) != "itemref":
            continue
        item_id = element.attrib.get("idref", "").strip()
        item = manifest.get(item_id)
        if item is None:
            warnings.append(warning("spine-item-missing", "A spine reference has no usable manifest item."))
            continue
        if item["path"] not in spine:
            spine.append(item["path"])

    navigation = nav_candidates[0] if nav_candidates else None
    if len(nav_candidates) > 1:
        warnings.append(warning("multiple-navigation-documents", "Multiple navigation documents were declared; the first was audited."))
    if navigation is None:
        warnings.append(warning("navigation-document-missing", "The package manifest has no EPUB navigation document."))
    if not manifest:
        raise CorpusAuditError("EPUB package metadata has no usable manifest items")
    if not spine:
        warnings.append(warning("spine-missing", "The package metadata has no usable spine entries."))
    return title, manifest, spine, navigation


def chapter_label(text: str) -> tuple[int, str] | None:
    if SECTION_LABEL.match(text):
        return None
    match = CHAPTER_PREFIX_LABEL.match(text) or CHAPTER_LABEL.match(text)
    if match is None:
        return None
    return int(match.group("number")), match.group("title").strip(" .:\t")


def navigation_chapters(
    archive: zipfile.ZipFile,
    navigation_path: str,
    warnings: list[dict[str, str]],
) -> list[dict[str, object]]:
    try:
        root = parse_xml(read_entry(archive, navigation_path, NAVIGATION_LIMIT), "EPUB navigation document")
    except CorpusAuditError as error:
        warnings.append(warning("navigation-document-unusable", str(error)))
        return []

    navigation_elements = [element for element in root.iter() if local_name(element.tag) == "nav"]
    toc = next(
        (
            element
            for element in navigation_elements
            if "toc" in element.attrib.get(f"{{{EPUB_NAMESPACE}}}type", "").split()
            or element.attrib.get("role", "") == "doc-toc"
        ),
        None,
    )
    if toc is None and navigation_elements:
        toc = navigation_elements[0]
        warnings.append(warning("toc-navigation-unlabelled", "No navigation element was labelled as the table of contents; the first was used."))
    if toc is None:
        warnings.append(warning("toc-navigation-missing", "The navigation document contains no navigation element."))
        return []

    base = posixpath.dirname(navigation_path)
    chapters: list[dict[str, object]] = []
    numbers: dict[int, str] = {}
    for anchor in toc.iter():
        if local_name(anchor.tag) != "a":
            continue
        parsed = chapter_label(normalized_text(anchor))
        if parsed is None:
            continue
        number, title = parsed
        path = entry_path(base, anchor.attrib.get("href", ""))
        if path is None:
            warnings.append(warning("chapter-href-unsupported", "A numbered chapter had no usable local content path."))
            continue
        prior = numbers.get(number)
        if prior is not None:
            if prior != path:
                warnings.append(warning("chapter-number-duplicate", "A chapter number referred to more than one content document."))
            continue
        numbers[number] = path
        chapters.append({"chapter": number, "title": title, "path": path})
    return sorted(chapters, key=lambda item: int(item["chapter"]))


def spine_chapters(
    archive: zipfile.ZipFile,
    spine: list[str],
    warnings: list[dict[str, str]],
) -> list[dict[str, object]]:
    chapters: list[dict[str, object]] = []
    numbers: set[int] = set()
    for path in spine:
        try:
            root = parse_xml(read_entry(archive, path, CONTENT_LIMIT), "spine content document")
        except CorpusAuditError:
            warnings.append(warning("spine-content-unusable", "A spine content document could not be parsed during fallback discovery."))
            continue
        headings = [element for element in root.iter() if local_name(element.tag) == "h1"]
        parsed = None
        for element in headings:
            parsed = chapter_label(normalized_text(element))
            if parsed is not None:
                break
        if parsed is None:
            continue
        number, title = parsed
        if number in numbers:
            warnings.append(warning("chapter-number-duplicate", "A chapter number referred to more than one spine document."))
            continue
        numbers.add(number)
        chapters.append({"chapter": number, "title": title, "path": path})
    if chapters:
        warnings.append(warning("chapter-discovery-spine-fallback", "Numbered chapters were inferred from spine h1 headings because navigation metadata was unavailable."))
    else:
        warnings.append(warning("chapter-discovery-failed", "No numbered chapters could be identified from navigation metadata or the spine."))
    return sorted(chapters, key=lambda item: int(item["chapter"]))


def empty_counts() -> dict[str, int]:
    return {
        "sections": 0,
        "samples": 0,
        "checkpoints": 0,
        "proof_derivation_headings": 0,
        "images": 0,
        "images_with_nonempty_alt": 0,
    }


def content_counts(root: ElementTree.Element) -> dict[str, int]:
    counts = empty_counts()
    for element in root.iter():
        name = local_name(element.tag).lower()
        if name == "h2":
            counts["sections"] += 1
        elif name == "h4":
            text = normalized_text(element)
            folded = text.casefold()
            if folded.startswith("sample problem"):
                counts["samples"] += 1
            if folded.startswith("checkpoint"):
                counts["checkpoints"] += 1
            if PROOF_LABEL.match(text):
                counts["proof_derivation_headings"] += 1
        elif name == "h3" and PROOF_LABEL.match(normalized_text(element)):
            counts["proof_derivation_headings"] += 1
        elif name == "img":
            counts["images"] += 1
            if element.attrib.get("alt", "").strip():
                counts["images_with_nonempty_alt"] += 1
    return counts


def audit_epub(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise CorpusAuditError("EPUB file does not exist")
    corpus_sha256 = sha256_file(path)
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        raise CorpusAuditError(f"not a readable EPUB ZIP container: {error}") from error

    with archive:
        warnings: list[dict[str, str]] = []
        duplicate_entries = len(archive.infolist()) - len({entry.filename for entry in archive.infolist()})
        if duplicate_entries:
            warnings.append(warning("zip-entry-duplicate", f"The ZIP contains {duplicate_entries} duplicate entry name(s)."))
        package_path, package_root, package_warnings = package_document(archive)
        warnings.extend(package_warnings)
        title, _, spine, navigation_path = package_structure(package_path, package_root, warnings)
        chapters = navigation_chapters(archive, navigation_path, warnings) if navigation_path else []
        used_spine_fallback = not chapters
        if used_spine_fallback:
            chapters = spine_chapters(archive, spine, warnings)

        total_content_size = 0
        audited_chapters: list[dict[str, object]] = []
        missing_alt = 0
        malformed_chapters = 0
        for chapter in chapters:
            path_in_archive = str(chapter["path"])
            counts = empty_counts()
            parsed = True
            try:
                entry = archive.getinfo(path_in_archive)
                total_content_size += entry.file_size
                if total_content_size > TOTAL_CONTENT_LIMIT:
                    raise CorpusAuditError("chapter content exceeds the metadata audit total limit")
                root = parse_xml(read_entry(archive, path_in_archive, CONTENT_LIMIT), "chapter content document")
                counts = content_counts(root)
            except (KeyError, CorpusAuditError):
                parsed = False
                malformed_chapters += 1
                warnings.append(warning("chapter-content-unusable", f"Chapter {chapter['chapter']} content could not be parsed structurally."))
            missing_alt += counts["images"] - counts["images_with_nonempty_alt"]
            audited_chapters.append(
                {
                    "chapter": chapter["chapter"],
                    "title": chapter["title"],
                    **counts,
                    "content_parsed": parsed,
                }
            )

        if missing_alt:
            warnings.append(warning("image-alt-missing", f"{missing_alt} image(s) have empty or absent alt metadata."))
        warnings.append(
            warning(
                "heading-classification-heuristic",
                "Sample, checkpoint, and proof counts depend on XHTML heading levels and stable label prefixes.",
            )
        )

        total_keys = tuple(empty_counts())
        totals = {key: sum(int(chapter[key]) for chapter in audited_chapters) for key in total_keys}
        totals = {"chapters": len(audited_chapters), **totals}
        confidence = {
            "corpus_sha256": "high",
            "chapters": "medium" if used_spine_fallback or not chapters or malformed_chapters else "high",
            "sections": "low" if malformed_chapters else "high",
            "samples": "low" if malformed_chapters else "medium",
            "checkpoints": "low" if malformed_chapters else "medium",
            "proof_derivation_headings": "low" if malformed_chapters else "medium",
            "images": "low" if malformed_chapters else "high",
            "images_with_nonempty_alt": "low" if malformed_chapters else "high",
        }
        return {
            "schema_version": 1,
            "mode": "metadata-only",
            "corpus_sha256": corpus_sha256,
            "book": {"title": title},
            "structure": {
                "zip_entries": len(archive.infolist()),
                "package_document": package_path,
                "navigation_document": navigation_path,
                "spine_documents": len(spine),
            },
            "totals": totals,
            "chapters": audited_chapters,
            "confidence": confidence,
            "warnings": warnings,
        }


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Audit copyright-safe structural metadata in an EPUB corpus")
    parser.add_argument("--metadata-only", action="store_true", required=True)
    parser.add_argument("epub", type=Path)
    return parser.parse_args()


def main() -> int:
    options = arguments()
    try:
        report = audit_epub(options.epub)
    except CorpusAuditError as error:
        print(f"corpus audit error: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
