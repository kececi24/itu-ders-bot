#!/usr/bin/env python3
"""Require exactly the three versioned release archives and hash them together."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import zipfile

EXPECTED_TARGETS = (("windows-x64", "zip"), ("macos-arm64", "tar.gz"), ("linux-x64", "tar.gz"))


def extract_manifest(archive_path: Path):
    if archive_path.suffix == ".zip":
        with zipfile.ZipFile(archive_path) as archive:
            manifest_names = [n for n in archive.namelist() if n.endswith("/build-manifest.json") or n == "build-manifest.json"]
            if len(manifest_names) != 1:
                raise SystemExit(f"Archive must contain exactly one build-manifest.json: {archive_path.name}")
            return json.loads(archive.read(manifest_names[0]).decode("utf-8"))
    elif archive_path.name.endswith(".tar.gz"):
        with tarfile.open(archive_path, "r:gz") as archive:
            manifest_members = [m for m in archive.getmembers() if m.name.endswith("/build-manifest.json") or m.name == "build-manifest.json"]
            if len(manifest_members) != 1:
                raise SystemExit(f"Archive must contain exactly one build-manifest.json: {archive_path.name}")
            extracted = archive.extractfile(manifest_members[0])
            if extracted is None:
                raise SystemExit(f"Failed to read build-manifest.json: {archive_path.name}")
            return json.loads(extracted.read().decode("utf-8"))
    else:
        raise SystemExit(f"Unsupported archive format: {archive_path.name}")


def release_checksums(directory: Path, tag: str):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise SystemExit("Expected a vMAJOR.MINOR.PATCH tag")
    version = tag[1:]
    target_map = dict(EXPECTED_TARGETS)
    expected_names = {f"itu-ders-bot-{version}-{target}.{extension}" for target, extension in EXPECTED_TARGETS}
    actual = {path.name for path in directory.iterdir() if path.suffix == ".zip" or path.name.endswith(".tar.gz")}
    if actual != expected_names:
        raise SystemExit(f"Release archives differ from expected set: {sorted(actual)}")

    expected_revision = None
    lines = []
    for target, extension in sorted(EXPECTED_TARGETS):
        name = f"itu-ders-bot-{version}-{target}.{extension}"
        path = directory / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        sha256_path = directory / (name + ".sha256")
        if not sha256_path.is_file():
            raise SystemExit(f"Missing checksum file: {name}.sha256")
        expected = sha256_path.read_text(encoding="utf-8").split()[0]
        if digest != expected:
            raise SystemExit(f"Archive checksum mismatch: {name}")

        manifest = extract_manifest(path)
        if manifest.get("version") != version:
            raise SystemExit(f"Archive version mismatch: {name} manifest version {manifest.get('version')} != {version}")
        if manifest.get("target") != target:
            raise SystemExit(f"Archive target mismatch: {name} manifest target {manifest.get('target')} != {target}")

        rev = manifest.get("revision")
        if not rev or rev == "unknown" or not re.fullmatch(r"([0-9a-f]{40}|[0-9a-f]{64})", rev):
            raise SystemExit(f"Invalid or unknown revision in manifest: {name} ({rev})")
        if manifest.get("dirty"):
            raise SystemExit(f"Release archive built from dirty working tree: {name}")

        if expected_revision is None:
            expected_revision = rev
        elif rev != expected_revision:
            raise SystemExit(f"Archive revision mismatch across release assets: {name} has {rev} vs {expected_revision}")

        lines.append(f"{digest}  {name}\n")

    (directory / "SHA256SUMS").write_text("".join(lines), encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    release_checksums(args.directory, args.tag)


if __name__ == "__main__":
    main()
