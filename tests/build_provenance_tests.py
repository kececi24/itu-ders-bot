"""Exercise real CMake scheduling with tiny binaries and production provenance code."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest import mock


def main():
    cmake, repository, target, _deps = sys.argv[1:]
    repository = Path(repository)
    sys.path.insert(0, str(repository / "scripts"))
    from provenance import validate_provenance, validate_release

    def rejected(callback):
        try:
            callback()
        except (SystemExit, FileNotFoundError):
            return
        raise AssertionError("stale/incomplete provenance was accepted")

    with tempfile.TemporaryDirectory(prefix="itu provenance ") as temporary:
        root = Path(temporary) / "source with spaces"
        build = Path(temporary) / "build with spaces"
        root.mkdir()
        for directory in ("cmake", "src", "setup", "include"):
            (root / directory).mkdir()
        for name in ("BuildProvenance.cmake", "GenerateManifest.cmake", "ProvenanceInputs.cmake", "BinaryReceipt.cmake"):
            shutil.copyfile(repository / "cmake" / name, root / "cmake" / name)
        (root / "README.md").write_text("fixture\n")
        (root / "CMakePresets.json").write_text("{}")
        (root / "src/lib.cpp").write_text("int helper() {return 0;}\n")
        for name in ("main", "setup"):
            (root / "src" / f"{name}.cpp").write_text("int main() {return 0;}\n")
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(ProvenanceFixture VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
set(ITU_TARGET "''' + target + '''")
file(WRITE "${CMAKE_BINARY_DIR}/dependency-manifest.json" "{}")
file(WRITE "${CMAKE_BINARY_DIR}/provenance-dependencies.cmake"
    "set(PROVENANCE_EXTERNAL_FILES [==[${CMAKE_CXX_COMPILER}]==])\\n")
add_library(itu_platform STATIC src/lib.cpp)
add_library(itu_core STATIC src/lib.cpp)
target_link_libraries(itu_core PUBLIC itu_platform)
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE itu_core)
add_executable(setup src/setup.cpp)
target_link_libraries(setup PRIVATE itu_platform)
include(cmake/BuildProvenance.cmake)
''')
        configure = [cmake, "-S", str(root), "-B", str(build)]
        if target == "windows-x64":
            configure += ["-G", "Visual Studio 17 2022", "-A", "x64"]
        subprocess.run(configure, check=True)
        manifest_path = build / "bin/build-manifest.json"

        def run(target_name="build_manifest", success=True):
            result = subprocess.run([cmake, "--build", str(build), "--config", "Release",
                                     "--target", target_name, "--parallel", "3"],
                                    capture_output=True, text=True)
            if (result.returncode == 0) != success:
                raise AssertionError(result.stdout + result.stderr)

        def validate():
            manifest = json.loads(manifest_path.read_text())
            validate_provenance(manifest, root, build / "bin")
            return manifest

        run()
        first = validate()
        run()  # A legitimate no-op build is eligible.
        assert first == validate()
        for partial in ("main", "setup", "itu_core", "itu_platform"):
            run(partial)
            assert not manifest_path.exists(), partial
            rejected(validate)
            run()
            validate()
        # Already dirty before compilation fails: a boolean dirty flag cannot
        # distinguish these input states. Old executables remain on disk.
        source = root / "src/main.cpp"
        good_source = source.read_text()
        source.write_text(good_source + "\n#error intentional failed build\n")
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        rejected(validate)
        source.write_text(good_source)
        run()
        validate()
        # A failure in shared code must also invalidate before compilation.
        library = root / "src/lib.cpp"
        good_library = library.read_text()
        library.write_text("#error failed shared dependency\n")
        run(success=False)
        assert not manifest_path.exists()
        library.write_text(good_library)
        run()
        manifest = validate()
        suffix = ".exe" if target == "windows-x64" else ""
        binary = build / "bin" / ("setup" + suffix)
        original = binary.read_bytes()
        binary.write_bytes(original + b"replaced")
        rejected(validate)
        run(success=False)  # No-op builds cannot re-attest replaced bytes.
        assert not manifest_path.exists()
        binary.write_bytes(original)
        run()
        validate()
        # Simulate replacement during packaging: source binary still valid,
        # copied binary no longer matches the manifest.
        stage = Path(temporary) / "stage"
        stage.mkdir()
        for name in ("main", "setup"):
            shutil.copyfile(build / "bin" / (name + suffix), stage / (name + suffix))
        (stage / ("main" + suffix)).write_bytes(b"raced replacement")
        rejected(lambda: validate_provenance(manifest, root, build / "bin", binary_dir=stage))
        (root / "include/new.hpp").write_text("// new input\n")
        rejected(validate)
        # Both unavailable Git and Git reporting an error must fail closed.
        release = dict(manifest, revision="a" * 40, dirty=False)
        for error in (FileNotFoundError("git"), subprocess.CalledProcessError(128, "git")):
            with mock.patch("provenance.subprocess.run", side_effect=error):
                rejected(lambda: validate_release(release, root))
        with mock.patch("provenance.subprocess.run", side_effect=[
                subprocess.CompletedProcess([], 0, "a" * 40),
                subprocess.CompletedProcess([], 0, "")]):
            validate_release(release, root)
        print("Completed, no-op, partial, failed, stale, replaced and staged-copy provenance checks passed.")


if __name__ == "__main__":
    main()
