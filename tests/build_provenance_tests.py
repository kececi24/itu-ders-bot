"""Exercise real CMake scheduling with tiny binaries and production provenance code."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from unittest import mock


def verify_receipt_line_endings(cmake, repository, temporary):
    """Windows text-mode writes must bind the same inputs as the finalizer."""
    directory = Path(temporary) / "receipt newlines"
    directory.mkdir()
    binary = directory / "synthetic executable"
    binary.write_bytes(b"synthetic binary\x00\r\n\xff")
    inputs = directory / "inputs.json"
    receipt = directory / "receipt.json"
    admission = directory / "admission.json"
    command = [cmake, f"-DBINARY_FILE={binary}", f"-DINPUT_FILE={inputs}",
               f"-DRECEIPT_FILE={receipt}", f"-DADMISSION_FILE={admission}",
               "-P", str(repository / "cmake/BinaryReceipt.cmake")]

    def admit_rebuild(fingerprint):
        receipt.unlink(missing_ok=True)
        admission.write_text(json.dumps({
            "mode": "rebuild", "inputs": fingerprint, "binary": "", "receipt": "",
            "binary_path": hashlib.sha256(str(binary).encode()).hexdigest(),
            "receipt_path": hashlib.sha256(str(receipt).encode()).hexdigest(),
        }))
    original = json.dumps({"source": {"synthetic.cpp": "original input"}}, indent=2).encode()
    expected = hashlib.sha256(original).hexdigest()
    for newline in (b"\n", b"\r\n"):
        inputs.write_bytes(original.replace(b"\n", newline))
        admit_rebuild(expected)
        subprocess.run(command, check=True)
        actual = json.loads(receipt.read_text())
        assert actual["inputs"] == expected, (newline, actual)
        assert actual["binary"] == hashlib.sha256(binary.read_bytes()).hexdigest()
    changed = original.replace(b"original input", b"modified input")
    inputs.write_bytes(changed.replace(b"\n", b"\r\n"))
    admit_rebuild(hashlib.sha256(changed).hexdigest())
    subprocess.run(command, check=True)
    changed_hash = json.loads(receipt.read_text())["inputs"]
    assert changed_hash == hashlib.sha256(changed).hexdigest() and changed_hash != expected
    print("LF/CRLF receipt fingerprints agree; changed input and raw binary bytes remain bound.")


def verify_hidden_untracked_release(repository, temporary, validate_release):
    """Exercise real Git settings without changing a repository or its index."""
    try:
        git_dir = subprocess.run(["git", "rev-parse", "--absolute-git-dir"], cwd=repository,
                                 capture_output=True, text=True, check=True).stdout.strip()
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository,
                                  capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        print("SKIP real Git settings regression: checkout metadata unavailable")
        return
    worktree = Path(temporary) / "git worktree"
    worktree.mkdir()
    untracked = "untracked_source.cpp"
    (worktree / untracked).write_text("// synthetic release input\n")
    environment = dict(os.environ, GIT_DIR=git_dir, GIT_WORK_TREE=str(worktree),
                       GIT_OPTIONAL_LOCKS="0")
    command = ["git", "-c", "status.showUntrackedFiles=no", "-c", f"safe.directory={worktree}"]
    real_run = subprocess.run
    control = real_run(command + ["status", "--porcelain", "--", untracked], cwd=worktree,
                       env=environment, capture_output=True, text=True, check=True)
    assert not control.stdout.strip(), control.stdout
    statuses = []

    def configured_git(args, **kwargs):
        assert args[0] == "git", args
        args = command + args[1:]
        if "status" in args:
            # Filter out tracked files absent from this synthetic worktree, so
            # they cannot mask the missing-untracked-files regression.
            args += ["--", untracked]
        completed = real_run(args, env=environment, **kwargs)
        if "status" in args:
            statuses.append(completed.stdout.strip())
        return completed

    with mock.patch("provenance.subprocess.run", side_effect=configured_git):
        try:
            validate_release({"revision": revision, "dirty": False}, worktree)
        except SystemExit as error:
            assert str(error) == "Release checkout differs from completed build", error
        else:
            raise AssertionError("release ignored an untracked file hidden by Git settings")
    assert statuses == [f"?? {untracked}"], statuses
    print("Real Git status.showUntrackedFiles=no release regression passed.")



def same_input_build(before, after, context):
    """A legitimate relink may change bytes; validated inputs must stay fixed."""
    # Callers validate each completed manifest against the current inputs and
    # executable bytes first. Visual Studio can relink on an unchanged build,
    # so PE timestamps/PDB identities need not produce identical binary hashes.
    previous = {key: value for key, value in before.items() if key != "binaries"}
    current = {key: value for key, value in after.items() if key != "binaries"}
    changed = sorted(key for key in previous.keys() | current.keys()
                     if previous.get(key) != current.get(key))
    assert previous == current, f"{context}: build metadata/inputs changed: {changed}"


def main():
    cmake, repository, target, _deps = sys.argv[1:5]
    preset = sys.argv[5] if len(sys.argv) > 5 else target
    msvc = target == "windows-x64" and preset != "windows-mingw-x64"
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
        verify_receipt_line_endings(cmake, repository, temporary)
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
        (dependency_source / "alternate.cpp").write_text("int external_value() {return 11;}\n")
        (dependency_source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(DependencyFixture LANGUAGES CXX)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "''' + external.as_posix() + '''/lib/$<0:>")
add_library(fixture STATIC value.cpp)
add_library(alternate STATIC alternate.cpp)
''')
        dependency_build = Path(temporary) / "dependency build"
        generator = (["-G", "Visual Studio 17 2022", "-A", "x64"] if msvc else
                     ["-G", "MinGW Makefiles", "-DCMAKE_CXX_COMPILER=g++"] if preset == "windows-mingw-x64" else [])
        subprocess.run([cmake, "-S", str(dependency_source), "-B", str(dependency_build)] + generator, check=True)

        def build_dependency():
            subprocess.run([cmake, "--build", str(dependency_build), "--config", "Release"], check=True)

        build_dependency()
        dependency_library = external / "lib" / ("fixture.lib" if msvc else "libfixture.a")
        alternate_library = external / "lib" / ("alternate.lib" if msvc else "libalternate.a")
        dependency_config = external / "itu-dependencies.cmake"
        dependency_config.write_text(f'set(FIXTURE_LIBRARY "{dependency_library.as_posix()}")\n')
        dependency_metadata = external / "dependency-manifest.json"
        dependency_metadata.write_text('{"fixture": "initial"}')
        root.mkdir()
        for directory in ("cmake", "src", "setup", "include"):
            (root / directory).mkdir()
        for name in ("BuildProvenance.cmake", "GenerateManifest.cmake", "ProvenanceInputs.cmake", "BinaryReceipt.cmake"):
            shutil.copyfile(repository / "cmake" / name, root / "cmake" / name)
        (root / "README.md").write_text("fixture\n")
        (root / "CMakePresets.json").write_text("{}")
        (root / "cmake/modules").mkdir()
        nested_module = root / "cmake/modules/features.cmake"
        nested_module.write_text("add_compile_definitions(MODULE_VALUE=0)\n")
        (root / "src/lib.cpp").write_text('''#include <value.hpp>
#ifndef CONFIG_VALUE
#define CONFIG_VALUE 0
#endif
#ifndef CONFIG_LIST_VALUE
#define CONFIG_LIST_VALUE 0
#endif
#if __has_include(<added.hpp>)
#include <added.hpp>
#else
#define ADDED_VALUE 0
#endif
int external_value();
int helper() {return DEPENDENCY_VALUE + ADDED_VALUE + external_value() + CONFIG_VALUE + CONFIG_LIST_VALUE + MODULE_VALUE;}
''')
        for name in ("main", "setup"):
            (root / "src" / f"{name}.cpp").write_text(
                'int helper(); static const char* volatile compiled_at = __TIME__; '
                'int main() {(void)compiled_at; return helper();}\n')
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(ProvenanceFixture VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
set(ITU_TARGET "''' + target + '''")
set(ITU_CONFIGURE_INPUT_FILES "''' + dependency_config.as_posix() + '''" "''' + dependency_metadata.as_posix() + '''")
include("''' + dependency_config.as_posix() + '''")
include(cmake/modules/features.cmake)
file(READ "''' + dependency_metadata.as_posix() + '''" dependency_metadata)
file(WRITE "${CMAKE_BINARY_DIR}/dependency-manifest.json" "${dependency_metadata}")
file(WRITE "${CMAKE_BINARY_DIR}/provenance-dependencies.cmake"
    "set(PROVENANCE_EXTERNAL_FILES [==[${CMAKE_CXX_COMPILER}]==])\\n"
    "set(PROVENANCE_EXTERNAL_ROOTS [==[''' + external.as_posix() + '''/include;''' + external.as_posix() + '''/lib]==])\\n")
include_directories("''' + external.as_posix() + '''/include")
add_library(itu_platform STATIC src/lib.cpp)
target_link_libraries(itu_platform PUBLIC "${FIXTURE_LIBRARY}")
add_library(itu_core STATIC src/lib.cpp)
target_link_libraries(itu_core PUBLIC itu_platform)
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE itu_core)
add_executable(setup src/setup.cpp)
target_link_libraries(setup PRIVATE itu_platform)
include(cmake/BuildProvenance.cmake)
''')
        configure = [cmake, "-S", str(root), "-B", str(build)]
        configure += generator
        subprocess.run(configure + ["-DCMAKE_CXX_FLAGS=-DCONFIG_VALUE=0"], check=True)
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

        suffix = ".exe" if target == "windows-x64" else ""

        def result(expected):
            for name in ("main", "setup"):
                assert subprocess.run([str(build / "bin" / (name + suffix))]).returncode == expected

        def replace_preserving_time(path, before, after):
            stamp = path.stat()
            contents = path.read_text()
            assert before in contents
            path.write_text(contents.replace(before, after))
            os.utime(path, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))

        # A configure snapshot must already bind the final cache and imported
        # library choice before the FIRST build; neither may be learned later.
        cache = build / "CMakeCache.txt"
        replace_preserving_time(cache, "CMAKE_CXX_FLAGS:STRING=-DCONFIG_VALUE=0",
                                "CMAKE_CXX_FLAGS:STRING=-DCONFIG_VALUE=1")
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        replace_preserving_time(dependency_config, dependency_library.as_posix(), alternate_library.as_posix())
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        validate()
        result(12)
        replace_preserving_time(dependency_config, alternate_library.as_posix(), dependency_library.as_posix())
        subprocess.run(configure + ["-DCMAKE_CXX_FLAGS=-DCONFIG_VALUE=0"], check=True)

        run()
        first = validate()
        header_time = (build / "provenance-inputs.h").stat().st_mtime_ns
        run()  # A legitimate no-op build is eligible.
        same_input_build(first, validate(), "unchanged build")
        assert (build / "provenance-inputs.h").stat().st_mtime_ns == header_time
        receipt_path, = build.glob("main-*.sha256")
        build_config = receipt_path.stem.removeprefix("main-")

        # A real recompile/relink with identical source contents must remain
        # eligible even when compile/link timestamps change executable bytes.
        # __TIME__ in the tiny fixture makes that transition visible locally.
        # Content hashes stay fixed; no invalidation or receipt check is skipped.
        before_relink = validate()
        time.sleep(1.1)
        for name in ("main", "setup"):
            (root / "src" / f"{name}.cpp").touch()
        run()
        after_relink = validate()
        same_input_build(before_relink, after_relink, "same-input real relink")
        assert (build / "provenance-inputs.h").stat().st_mtime_ns == header_time
        result(0)
        changed_binaries = [name for name in ("main", "setup")
                            if before_relink["binaries"][name] != after_relink["binaries"][name]]
        print(f"Same-input real relink validated; refreshed binary hashes: {changed_binaries}")

        def script(name, arguments, success=True):
            completed = subprocess.run(
                [cmake] + [f"-D{key}={value}" for key, value in arguments.items()]
                + ["-P", str(root / "cmake" / name)], capture_output=True, text=True)
            assert (completed.returncode == 0) == success, completed.stdout + completed.stderr
            return completed

        def start():
            script("ProvenanceInputs.cmake", {
                "SOURCE_DIR": root.as_posix(), "BINARY_DIR": build.as_posix(),
                "START_BUILD": "ON", "BUILD_CONFIG": build_config,
                "MAIN_FILE": (build / "bin" / ("main" + suffix)).as_posix(),
                "SETUP_FILE": (build / "bin" / ("setup" + suffix)).as_posix(),
            })

        def post_build(name="main", success=True, prepare=False):
            script("BinaryReceipt.cmake", {
                "BINARY_FILE": (build / "bin" / (name + suffix)).as_posix(),
                "RECEIPT_FILE": (build / f"{name}-{build_config}.sha256").as_posix(),
                "ADMISSION_FILE": (build / f"{name}-{build_config}.admission.json").as_posix(),
                "INPUT_FILE": (build / "provenance-start.json").as_posix(),
                "PREPARE_LINK": "ON" if prepare else "OFF",
            }, success)

        def invalidated(name="main", expected=0):
            start()
            assert not manifest_path.exists()
            assert not (build / "bin" / (name + suffix)).exists()
            assert not (build / f"{name}-{build_config}.sha256").exists()
            post_build(name, success=False)  # An event without a link cannot attest anything.
            run()  # Missing output forces a real link and legitimate recovery.
            validate()
            result(expected)

        # Simulate generators scheduling POST_BUILD independently of linking.
        receipt_bytes = receipt_path.read_bytes()
        receipt_time = receipt_path.stat().st_mtime_ns
        start()
        post_build()
        post_build()
        assert receipt_path.read_bytes() == receipt_bytes
        assert receipt_path.stat().st_mtime_ns == receipt_time
        run()
        # PRE_LINK alone cannot attest an output either. A real link must
        # recreate it, including for unchanged-input rebuilds.
        start()
        post_build(prepare=True)
        assert not (build / "bin" / ("main" + suffix)).exists()
        post_build(success=False)
        run()
        validate()
        result(0)
        receipt = receipt_path.read_text()
        finalizer = {
            "SOURCE_DIR": root.as_posix(), "BINARY_DIR": build.as_posix(),
            "MAIN_FILE": (build / "bin" / ("main" + suffix)).as_posix(),
            "SETUP_FILE": (build / "bin" / ("setup" + suffix)).as_posix(),
            "BUILD_CONFIG": build_config,
            "OUTPUT_FILE": (build / "direct-manifest.json").as_posix(),
            "PROJECT_VERSION": "1.0.0", "ITU_TARGET": target,
            "COMPILER_ID": "fixture", "COMPILER_VERSION": "fixture",
            "DEPENDENCY_MANIFEST_FILE": (build / "dependency-manifest.json").as_posix(),
        }
        script("GenerateManifest.cmake", finalizer)
        mismatched = json.loads(receipt)
        mismatched["inputs"] = "0" * 64
        receipt_path.write_text(json.dumps(mismatched))
        # The finalizer must reject corruption before start can repair it.
        failure = script("GenerateManifest.cmake", finalizer, success=False)
        assert "main was linked against different inputs" in failure.stderr, failure.stderr
        invalidated()
        receipt_path.write_text("malformed JSON")
        invalidated()
        receipt_path.unlink()
        invalidated()
        # A replacement after admission must not be blessed by POST_BUILD.
        start()
        main_binary = build / "bin" / ("main" + suffix)
        main_binary.write_bytes(main_binary.read_bytes() + b"late replacement")
        post_build(success=False)
        invalidated()
        # Source and dependency contents, not timestamp ordering, determine
        # whether compiled objects and their successful-link receipts are fresh.
        result(0)
        source = root / "src/lib.cpp"
        saved = source.read_text()
        stamp = source.stat()
        source.write_text(saved.replace("return DEPENDENCY_VALUE", "return 7 + DEPENDENCY_VALUE"))
        os.utime(source, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        rejected(validate)
        start()
        post_build(success=False)
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
        same_input_build(before_metadata, validate(), "unchanged dependency metadata build")
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
        # distinguish these input states. Start must invalidate old outputs.
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
        invalidated("setup", expected=5)
        manifest = validate()
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
        cmakelists.write_text(saved_cmakelists.replace('set(ITU_TARGET',
                             'add_compile_definitions(CONFIG_LIST_VALUE=2)\nset(ITU_TARGET'))
        os.utime(cmakelists, ns=(cmakelists_stamp.st_atime_ns, cmakelists_stamp.st_mtime_ns))
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        result(7)
        cmakelists.write_text(saved_cmakelists)
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        result(5)

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

        # Recursive module inventory includes nested files and detects new or
        # deleted inputs even when CMake's generated dependency timestamps do not.
        replace_preserving_time(nested_module, "MODULE_VALUE=0", "MODULE_VALUE=6")
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        validate()
        result(11)
        replace_preserving_time(nested_module, "MODULE_VALUE=6", "MODULE_VALUE=0")
        subprocess.run(configure, check=True)
        run()
        validate()
        result(5)
        new_module = root / "cmake/modules/new.cmake"
        new_module.write_text("# new configuration input\n")
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        validate()
        new_module.unlink()
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        validate()

        replace_preserving_time(dependency_config, dependency_library.as_posix(), alternate_library.as_posix())
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        validate()
        result(14)
        replace_preserving_time(dependency_config, alternate_library.as_posix(), dependency_library.as_posix())
        subprocess.run(configure, check=True)
        run()
        validate()
        result(5)
        replace_preserving_time(dependency_metadata, "initial", "updated")
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run(configure, check=True)
        run()
        assert validate()["dependencies"] == {"fixture": "updated"}

        # Reconfiguring must replace the immutable cache snapshot, rather than
        # re-enable the old first-build learning gap.
        subprocess.run(configure, check=True)
        replace_preserving_time(cache, "CMAKE_CXX_FLAGS:STRING=-DCONFIG_VALUE=0",
                                "CMAKE_CXX_FLAGS:STRING=-DCONFIG_VALUE=4")
        rejected(validate)
        run(success=False)
        assert not manifest_path.exists()
        subprocess.run([cmake, "-S", str(root), "-B", str(build)] + generator, check=True)
        run()
        validate()
        result(9)
        subprocess.run(configure + ["-DCMAKE_CXX_FLAGS=-DCONFIG_VALUE=0"], check=True)
        run()
        validate()
        result(5)

        for missing_snapshot in ("provenance-configure.json", "provenance-configured-cache.txt"):
            (build / missing_snapshot).unlink()
            rejected(validate)
            # The start script must fail closed. Recovery explicitly reruns
            # configure: generators differ in whether a build regenerates a
            # deleted file(GENERATE) output before provenance_start executes.
            probe = subprocess.run([cmake, f"-DSOURCE_DIR={root}", f"-DBINARY_DIR={build}",
                                    "-DSTART_BUILD=ON", "-P", str(root / "cmake/ProvenanceInputs.cmake")],
                                   capture_output=True, text=True)
            assert probe.returncode != 0, probe.stdout + probe.stderr
            assert "Missing configure provenance; re-run CMake before building" in probe.stdout + probe.stderr
            assert not manifest_path.exists()
            subprocess.run(configure, check=True)
            assert (build / missing_snapshot).is_file(), missing_snapshot
            assert not manifest_path.exists()
            run()
            validate()
            result(5)
            print(f"Recovered missing configure snapshot: {missing_snapshot}")

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
        verify_hidden_untracked_release(repository, temporary, validate_release)
        print("Completed, unchanged-input builds and real relinks, no-link receipt preservation, preserved-mtime source/header/archive, generated cache/flags and imported-library binding before first build and after reconfigure, recursive configure inventories, dependency additions/removals, hidden-file parity, partial, failed, replaced, untracked release and staged-copy provenance checks passed.")


if __name__ == "__main__":
    main()
