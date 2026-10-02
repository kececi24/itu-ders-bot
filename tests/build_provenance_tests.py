"""Exercise real CMake scheduling with tiny binaries and production provenance code."""
import json
import os
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
        external = Path(temporary) / "dependency with spaces"
        (external / "include").mkdir(parents=True)
        (external / "lib").mkdir()
        dependency_header = external / "include/value.hpp"
        dependency_header.write_text("#define DEPENDENCY_VALUE 0\n")
        dependency_source = Path(temporary) / "dependency source"
        dependency_source.mkdir()
        dependency_cpp = dependency_source / "value.cpp"
        dependency_cpp.write_text("int external_value() {return 0;}\n")
        (dependency_source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(DependencyFixture LANGUAGES CXX)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "''' + external.as_posix() + '''/lib/$<0:>")
add_library(fixture STATIC value.cpp)
''')
        dependency_build = Path(temporary) / "dependency build"
        generator = ["-G", "Visual Studio 17 2022", "-A", "x64"] if target == "windows-x64" else []
        subprocess.run([cmake, "-S", str(dependency_source), "-B", str(dependency_build)] + generator, check=True)

        def build_dependency():
            subprocess.run([cmake, "--build", str(dependency_build), "--config", "Release"], check=True)

        build_dependency()
        dependency_library = external / "lib" / ("fixture.lib" if target == "windows-x64" else "libfixture.a")
        root.mkdir()
        for directory in ("cmake", "src", "setup", "include"):
            (root / directory).mkdir()
        for name in ("BuildProvenance.cmake", "GenerateManifest.cmake", "ProvenanceInputs.cmake", "BinaryReceipt.cmake"):
            shutil.copyfile(repository / "cmake" / name, root / "cmake" / name)
        (root / "README.md").write_text("fixture\n")
        (root / "CMakePresets.json").write_text("{}")
        (root / "src/lib.cpp").write_text('''#include <value.hpp>
#if __has_include(<added.hpp>)
#include <added.hpp>
#else
#define ADDED_VALUE 0
#endif
int external_value();
int helper() {return DEPENDENCY_VALUE + ADDED_VALUE + external_value();}
''')
        for name in ("main", "setup"):
            (root / "src" / f"{name}.cpp").write_text("int helper(); int main() {return helper();}\n")
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(ProvenanceFixture VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
set(ITU_TARGET "''' + target + '''")
file(WRITE "${CMAKE_BINARY_DIR}/dependency-manifest.json" "{}")
file(WRITE "${CMAKE_BINARY_DIR}/provenance-dependencies.cmake"
    "set(PROVENANCE_EXTERNAL_FILES [==[${CMAKE_CXX_COMPILER}]==])\\n"
    "set(PROVENANCE_EXTERNAL_ROOTS [==[''' + external.as_posix() + '''/include;''' + external.as_posix() + '''/lib]==])\\n")
include_directories("''' + external.as_posix() + '''/include")
add_library(itu_platform STATIC src/lib.cpp)
target_link_libraries(itu_platform PUBLIC "''' + dependency_library.as_posix() + '''")
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
        header_time = (build / "provenance-inputs.h").stat().st_mtime_ns
        run()  # A legitimate no-op build is eligible.
        assert first == validate()
        assert (build / "provenance-inputs.h").stat().st_mtime_ns == header_time
        receipt_path, = build.glob("main-*.sha256")
        receipt = receipt_path.read_text()
        mismatched = json.loads(receipt)
        mismatched["inputs"] = "0" * 64
        receipt_path.write_text(json.dumps(mismatched))
        run(success=False)  # The executable hash alone cannot attest its inputs.
        assert not manifest_path.exists()
        receipt_path.write_text(receipt)
        run()
        validate()
        suffix = ".exe" if target == "windows-x64" else ""

        def result(expected):
            for name in ("main", "setup"):
                assert subprocess.run([str(build / "bin" / (name + suffix))]).returncode == expected

        # Source and dependency contents, not timestamp ordering, determine
        # whether compiled objects and their successful-link receipts are fresh.
        result(0)
        source = root / "src/lib.cpp"
        saved = source.read_text()
        stamp = source.stat()
        source.write_text(saved.replace("return DEPENDENCY_VALUE", "return 7 + DEPENDENCY_VALUE"))
        os.utime(source, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        rejected(validate)
        run()
        validate()
        result(7)
        source.write_text(saved)
        run()
        result(0)
        stamp = dependency_header.stat()
        dependency_header.write_text("#define DEPENDENCY_VALUE 3\n")
        os.utime(dependency_header, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        rejected(validate)
        run()
        validate()
        result(3)
        stamp = dependency_library.stat()
        dependency_cpp.write_text("int external_value() {return 2;}\n")
        build_dependency()
        os.utime(dependency_library, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        rejected(validate)
        run()
        validate()
        result(5)
        added = external / "include/added.hpp"
        added.write_text("#define ADDED_VALUE 4\n")
        rejected(validate)
        run()
        assert added.as_posix() in validate()["inputs"]["external"]
        result(9)
        added.unlink()
        rejected(validate)
        run()
        validate()
        result(5)
        # Named metadata/cache files are ignored identically by both inventory
        # implementations. Legitimate hidden inputs must remain accounted for.
        before_metadata = validate()
        header_time = (build / "provenance-inputs.h").stat().st_mtime_ns
        for location in (root / "src", external / "include"):
            (location / ".DS_Store").write_bytes(b"metadata")
            (location / "ignored.pyc").write_bytes(b"cache")
            (location / "__pycache__").mkdir()
            (location / "__pycache__/ignored").write_bytes(b"cache")
        run()
        assert before_metadata == validate()
        assert (build / "provenance-inputs.h").stat().st_mtime_ns == header_time
        for hidden in (root / "include/.hidden.hpp", external / "include/.hidden.hpp"):
            hidden.write_text("// legitimate hidden input\n")
            rejected(validate)
            run()
            validate()
        # Removing the generated header must recover normally.
        (build / "provenance-inputs.h").unlink()
        run()
        validate()
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
        # Configure-time input binding: modifying CMakeLists.txt or CMakeCache.txt
        # with preserved timestamps must fail closed and cannot attest stale generated flags.
        cmakelists = root / "CMakeLists.txt"
        saved_cmakelists = cmakelists.read_text()
        cmakelists_stamp = cmakelists.stat()
        cmakelists.write_text(saved_cmakelists + "\n# configure input change\n")
        os.utime(cmakelists, ns=(cmakelists_stamp.st_atime_ns, cmakelists_stamp.st_mtime_ns))
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        cmakelists.write_text(saved_cmakelists)
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()

        module = root / "cmake/BinaryReceipt.cmake"
        saved_module = module.read_text()
        module_stamp = module.stat()
        module.write_text(saved_module + "\n# secondary module configure change\n")
        os.utime(module, ns=(module_stamp.st_atime_ns, module_stamp.st_mtime_ns))
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        module.write_text(saved_module)
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()

        cache = build / "CMakeCache.txt"
        saved_cache = cache.read_text()
        cache_stamp = cache.stat()
        cache.write_text(saved_cache + "\nINJECTED_TEST_FLAG:STRING=some_value\n")
        os.utime(cache, ns=(cache_stamp.st_atime_ns, cache_stamp.st_mtime_ns))
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        cache.write_text(saved_cache)
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()

        (build / "provenance-configure.json").unlink()
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()

        # Both unavailable Git and Git reporting an error must fail closed.
        release = dict(manifest, revision="a" * 40, dirty=False)
        for error in (FileNotFoundError("git"), subprocess.CalledProcessError(128, "git")):
            with mock.patch("provenance.subprocess.run", side_effect=error):
                rejected(lambda: validate_release(release, root))
        with mock.patch("provenance.subprocess.run", side_effect=[
                subprocess.CompletedProcess([], 0, "a" * 40),
                subprocess.CompletedProcess([], 0, "?? untracked.txt\n")]):
            rejected(lambda: validate_release(release, root))
        with mock.patch("provenance.subprocess.run", side_effect=[
                subprocess.CompletedProcess([], 0, "a" * 40),
                subprocess.CompletedProcess([], 0, "")]):
            validate_release(release, root)
        print("Completed, no-op, preserved-mtime source/header/archive/configure/cache, dependency additions/removals, hidden-file parity, partial, failed, replaced, untracked release and staged-copy provenance checks passed.")


if __name__ == "__main__":
    main()
