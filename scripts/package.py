#!/usr/bin/env python3
"""Create one allowlisted native archive without reading personal configuration."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import zipfile

from provenance import validate_provenance, validate_release

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ("windows-x64", "macos-arm64", "linux-x64")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin_dir", type=Path)
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--tag", help="When releasing, must equal v<manifest version>")
    args = parser.parse_args()
    bin_dir = args.bin_dir.resolve()
    manifest_path = bin_dir / "build-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    version = manifest["version"]
    if manifest["target"] != args.target or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("Build manifest target/version mismatch")
    if args.tag:
        if args.tag != f"v{version}":
            raise SystemExit("Release tag must match project version")
        validate_release(manifest, ROOT)
    validate_provenance(manifest, ROOT, bin_dir)
    suffix = ".exe" if args.target == "windows-x64" else ""
    files = [(bin_dir / (name + suffix), name + suffix) for name in ("main", "setup")]
    files += [(ROOT / "packaging/example_config.json", "data/example_config.json"),
              (ROOT / "README.md", "README.md"), (manifest_path, "build-manifest.json"),
              (ROOT / "third_party/licenses/nlohmann-json.txt", "licenses/nlohmann-json.txt")]
    example = json.loads((ROOT / "packaging/example_config.json").read_text(encoding="utf-8"))
    if example["courses"] != {"crn": [], "scrn": []} or example["time"]["lead_millisecond"] != 0:
        raise SystemExit("Release template must contain empty courses and zero lead")
    if set(example) != {"time", "courses"}:
        raise SystemExit("Unexpected release template keys")
    if args.target != "macos-arm64":
        names = ["curl", "nghttp2", "zlib"] + (["openssl"] if args.target == "linux-x64" else [])
        prefix = ROOT / ".deps" / args.target / "install"
        lock = json.loads((ROOT / "cmake/dependencies.lock.json").read_text(encoding="utf-8"))["dependencies"]
        for name in names:
            if manifest["dependencies"][name]["source_sha256"] != lock[name]["sha256"]:
                raise SystemExit("Build manifest dependency hash differs from lock")
            files.append((prefix / "licenses" / f"{name}.txt", f"licenses/{name}.txt"))
    for path, _ in files:
        if not path.is_file() or path.is_symlink():
            raise SystemExit(f"Missing or symlinked package input: {path}")
    name = f"itu-ders-bot-{version}-{args.target}"
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="itu-package-") as directory:
        stage = Path(directory) / name
        for source, relative in files:
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            destination.chmod(0o755 if relative in ("main", "setup", "main.exe", "setup.exe") else 0o644)
        # Validate the bytes actually copied, not just the earlier source read.
        staged_manifest = json.loads((stage / "build-manifest.json").read_text(encoding="utf-8"))
        if staged_manifest != manifest or not manifest_path.is_file():
            raise SystemExit("Build provenance changed while staging package")
        if json.loads(manifest_path.read_text(encoding="utf-8")) != manifest:
            raise SystemExit("Build provenance changed while staging package")
        validate_provenance(manifest, ROOT, bin_dir, binary_dir=stage)
        if args.tag:
            validate_release(manifest, ROOT)
        paths = sorted(path for path in stage.rglob("*") if path.is_file())
        sums = "".join(f"{sha256(path)}  {path.relative_to(stage).as_posix()}\n" for path in paths)
        (stage / "SHA256SUMS").write_text(sums, encoding="utf-8", newline="\n")
        if suffix:
            archive = args.output / f"{name}.zip"
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        output.write(path, path.relative_to(stage.parent).as_posix())
        else:
            archive = args.output / f"{name}.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                output.add(stage, arcname=name)
        archive.with_name(archive.name + ".sha256").write_text(f"{sha256(archive)}  {archive.name}\n", encoding="utf-8", newline="\n")
        print(archive.resolve())


if __name__ == "__main__":
    main()
