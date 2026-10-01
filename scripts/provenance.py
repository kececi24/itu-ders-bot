"""Validate completed native builds without reading personal configuration."""
import hashlib
from pathlib import Path
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_inputs(root):
    paths = {"CMakeLists.txt", "CMakePresets.json", "README.md"}
    for directory in ("src", "setup", "include", "cmake", "scripts", "packaging", "third_party"):
        paths.update(path.relative_to(root).as_posix() for path in (root / directory).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc"
                     and not path.name.startswith("."))
    return paths


def validate_provenance(manifest, root, bin_dir, *, binary_dir=None):
    """binary_dir can be a staging directory; build inputs remain at bin_dir."""
    if manifest.get("schema_version") != 2:
        raise SystemExit("Missing completed-build provenance; perform a full build")
    inputs = manifest.get("inputs", {})
    if set(inputs.get("source", {})) != source_inputs(root):
        raise SystemExit("Build source inventory changed; perform a full build")
    if set(inputs.get("build", {})) != {"CMakeCache.txt", "dependency-manifest.json", "provenance-dependencies.cmake"}:
        raise SystemExit("Missing build input provenance")
    for group, base in (("source", root), ("build", bin_dir.parent), ("external", Path("/"))):
        for relative, expected in inputs.get(group, {}).items():
            path = Path(relative) if group == "external" else base / relative
            if not path.is_file() or sha256(path) != expected:
                raise SystemExit(f"Build input changed: {path}; perform a full build")
    suffix = ".exe" if manifest["target"] == "windows-x64" else ""
    if set(manifest.get("binaries", {})) != {"main", "setup"}:
        raise SystemExit("Missing binary provenance")
    for name, expected in manifest["binaries"].items():
        path = (binary_dir or bin_dir) / (name + suffix)
        if not path.is_file() or path.is_symlink() or sha256(path) != expected:
            raise SystemExit(f"Binary differs from completed build: {path}")


def validate_release(manifest, root):
    revision = manifest.get("revision", "")
    import re
    if not re.fullmatch(r"([0-9a-f]{40}|[0-9a-f]{64})", revision) or manifest.get("dirty") is not False:
        raise SystemExit("Ambiguous release provenance: invalid revision or modified working tree")
    try:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True,
                              text=True, check=True).stdout.strip()
        status = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                                cwd=root, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Cannot verify release Git provenance: {error}") from error
    if head != revision or status:
        raise SystemExit("Release checkout differs from completed build")
