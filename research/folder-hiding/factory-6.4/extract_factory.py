import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile
import zlib


OUTPUT = Path(__file__).resolve().parent
PACKAGE = OUTPUT.parents[1] / "images" / "TI-NspireCXII-6.4.0.74.tco2"


def fingerprint(contents):
    return {
        "size": len(contents),
        "crc32": f"{zlib.crc32(contents):08x}",
        "sha256": hashlib.sha256(contents).hexdigest(),
    }


def preserve(relative_path, contents):
    destination = OUTPUT / relative_path
    if destination.exists():
        if destination.read_bytes() != contents:
            raise ValueError(f"existing fixture differs: {relative_path}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(contents)


def main():
    package_bytes = PACKAGE.read_bytes()
    entries = []
    archives = {}
    with zipfile.ZipFile(io.BytesIO(package_bytes)) as package:
        for archive_name in ("resources.zip", "samples.zip"):
            archive_bytes = package.read(archive_name)
            archives[archive_name] = fingerprint(archive_bytes)
            with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
                names = archive.namelist()
                assert len(names) == len(set(names))
                if archive_name == "resources.zip":
                    documents = []
                    for member in archive.infolist():
                        if member.is_dir() or not member.filename.startswith("documents/"):
                            continue
                        relative = PurePosixPath(member.filename)
                        assert not relative.is_absolute() and ".." not in relative.parts
                        documents.append((member.filename, member.filename[10:], ""))
                else:
                    mapping = archive.read("copysamples")
                    preserve("copysamples", mapping)
                    lines = mapping.decode("utf-8").splitlines()
                    assert len(lines) % 2 == 0
                    documents = []
                    for source, name in zip(lines[::2], lines[1::2]):
                        relative = PurePosixPath(source)
                        assert len(relative.parts) == 2 and ".." not in relative.parts
                        assert PurePosixPath(name).name == name and name not in (".", "..")
                        locale = relative.parts[0]
                        documents.append((f"locales/{source}", name, locale))
                    assert {member for member, _, _ in documents} == {
                        member.filename for member in archive.infolist()
                        if not member.is_dir() and member.filename.startswith("locales/")
                    }
                for member, path, locale in documents:
                    contents = archive.read(member)
                    extracted = f"samples/{locale}/{path}" if locale else f"documents/{path}"
                    preserve(extracted, contents)
                    entries.append({
                        "path": f"Examples/{path}" if locale else path,
                        "locale": locale,
                        "source_archive": archive_name,
                        "source_member": member,
                        "extracted_path": extracted,
                        **fingerprint(contents),
                    })
    entries.sort(key=lambda entry: (entry["locale"], entry["path"]))
    manifest = {
        "schema_version": 1,
        "source_package": {"name": PACKAGE.name, **fingerprint(package_bytes)},
        "source_archives": archives,
        "path_semantics": {
            "resources": "Paths are relative to /documents.",
            "samples": "Basenames come from copysamples. Examples/ is a candidate installation folder requiring device verification, because the archive does not specify it.",
            "scope": "Only individual listed files are factory bytes. Parent directories may contain user files.",
        },
        "files": entries,
    }
    preserve("factory_manifest.json", (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode())
    header = [
        "#ifndef CX2_FACTORY_64_MANIFEST_H",
        "#define CX2_FACTORY_64_MANIFEST_H",
        "",
        "#include <stdint.h>",
        "",
        "struct factory_file {",
        "    const char *path;",
        "    const char *locale;",
        "    uint32_t size;",
        "    uint32_t crc32;",
        "};",
        "",
        "static const struct factory_file factory_files[] = {",
    ]
    for entry in entries:
        header.append("    { %s, %s, UINT32_C(%d), UINT32_C(0x%s) }," % (
            json.dumps(entry["path"]), json.dumps(entry["locale"]), entry["size"], entry["crc32"]))
    header.extend([
        "};",
        "",
        "#define FACTORY_FILE_COUNT (sizeof factory_files / sizeof factory_files[0])",
        "",
        "#endif",
        "",
    ])
    preserve("factory_manifest.h", "\n".join(header).encode())
    for entry in entries:
        assert fingerprint((OUTPUT / entry["extracted_path"]).read_bytes()) == {
            key: entry[key] for key in ("size", "crc32", "sha256")
        }
        print(f'{entry["extracted_path"]}\t{entry["size"]}\t{entry["crc32"]}\t{entry["sha256"]}')
    print(f"Verified {len(entries)} factory files, {sum(entry['size'] for entry in entries)} bytes")


if __name__ == "__main__":
    main()
