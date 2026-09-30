#!/usr/bin/env python3
"""Require exactly the three versioned release archives and hash them together."""
import argparse
import hashlib
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
parser.add_argument("--tag", required=True)
args = parser.parse_args()
if not re.fullmatch(r"v\d+\.\d+\.\d+", args.tag):
    raise SystemExit("Expected a vMAJOR.MINOR.PATCH tag")
version = args.tag[1:]
names = {f"itu-ders-bot-{version}-{target}.{extension}" for target, extension in
         (("windows-x64", "zip"), ("macos-arm64", "tar.gz"), ("linux-x64", "tar.gz"))}
actual = {path.name for path in args.directory.iterdir() if path.suffix == ".zip" or path.name.endswith(".tar.gz")}
if actual != names:
    raise SystemExit(f"Release archives differ from expected set: {sorted(actual)}")
lines = []
for name in sorted(names):
    path = args.directory / name
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    expected = (args.directory / (name + ".sha256")).read_text(encoding="utf-8").split()[0]
    if digest != expected:
        raise SystemExit(f"Archive checksum mismatch: {name}")
    lines.append(f"{digest}  {name}\n")
(args.directory / "SHA256SUMS").write_text("".join(lines), encoding="utf-8", newline="\n")
