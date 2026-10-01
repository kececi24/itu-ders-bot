"""Build the real manifest target with spaces in native build/source paths."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile


def main():
    cmake, source, target, deps_prefix = sys.argv[1:]
    source = Path(source).resolve()
    revision = None
    if shutil.which("git"):
        result = subprocess.run(["git", "log", "-1", "--format=%H"], cwd=source,
                                capture_output=True, text=True)
        if result.returncode == 0:
            revision = result.stdout.strip()
    with tempfile.TemporaryDirectory(prefix="itu manifest paths ") as temporary:
        root = Path(temporary)
        # POSIX can exercise a source path containing spaces without copying any
        # private configuration. Windows does not require symlink privileges.
        if os.name != "nt":
            alias = root / "source with spaces"
            alias.symlink_to(source, target_is_directory=True)
            source = alias
        build = root / "build with spaces"
        command = [cmake, "--preset", target, "-S", str(source), "-B", str(build),
                   "-DBUILD_TESTING=OFF"]
        if deps_prefix:
            command.append(f"-DITU_DEPS_PREFIX={deps_prefix}")
        subprocess.run(command, cwd=source, check=True)
        subprocess.run([cmake, "--build", str(build), "--target", "build_manifest",
                        "--config", "Release"], check=True)
        manifest = json.loads((build / "bin/build-manifest.json").read_text(encoding="utf-8"))
        dependencies = json.loads((build / "dependency-manifest.json").read_text(encoding="utf-8"))
        assert manifest["target"] == target, manifest
        assert manifest["dependencies"] == dependencies, manifest
        assert manifest["compiler"]["id"] and manifest["compiler"]["version"], manifest
        assert manifest["schema_version"] == 2, manifest
        suffix = ".exe" if target == "windows-x64" else ""
        for name in ("main", "setup"):
            assert manifest["binaries"][name] == hashlib.sha256(
                (build / "bin" / (name + suffix)).read_bytes()).hexdigest()
        if revision:
            assert manifest["revision"] == revision, manifest
        print("Manifest target preserves source/build paths and dependency metadata with spaces.")


if __name__ == "__main__":
    main()
