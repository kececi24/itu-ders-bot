#!/usr/bin/env python3
"""Build pinned static dependencies under .deps; never install global software.

Requires Python 3.12+, CMake 3.25+, and the native compiler/build tools already
installed. Windows: x64 VS2022 prompt, or --toolchain mingw with x64 MinGW-w64
GCC11+ and mingw32-make on PATH in PowerShell/cmd. Linux: GCC11+, make,
Perl. macOS uses its SDK curl and needs no bootstrap downloads.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "cmake/dependencies.lock.json"


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def require(program):
    path = shutil.which(program)
    if not path:
        raise RuntimeError(f"Missing prerequisite: {program}. Install/provide it yourself, then rerun; no global install attempted.")
    return path


def archive_source(name, spec, cache, sources):
    archive = cache / Path(spec["url"]).name
    if not archive.exists():
        print(f"Downloading pinned {name} {spec['version']}...", flush=True)
        temporary = archive.with_suffix(archive.suffix + ".partial")
        try:
            # Default TLS verification is mandatory. Fail once; never retry insecurely.
            with urllib.request.urlopen(spec["url"], timeout=30) as response, temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != spec["sha256"]:
                raise RuntimeError(f"SHA256 mismatch: {name}")
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise RuntimeError(f"Cached archive SHA256 mismatch: {archive}. Remove it after inspection and rerun.")
    source = sources / f"{name}-{spec['version']}"
    marker = source / ".itu-verified-sha256"
    if source.exists():
        if not marker.exists() or marker.read_text().strip() != spec["sha256"]:
            raise RuntimeError(f"Unverified existing source directory: {source}")
        return source
    print(f"Verified {name} {spec['version']}; extracting project-local sources...", flush=True)
    with tempfile.TemporaryDirectory(dir=sources, prefix="extract-") as temporary:
        with tarfile.open(archive) as tar:
            tar.extractall(temporary, filter="data")
        roots = list(Path(temporary).iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise RuntimeError(f"Unexpected archive layout: {name}")
        roots[0].rename(source)
    marker.write_text(spec["sha256"] + "\n", encoding="utf-8")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("windows-x64", "linux-x64", "macos-arm64"), required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--toolchain", choices=("msvc", "mingw"), help="Windows compiler (default: msvc)")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()
    if sys.version_info < (3, 12):
        raise RuntimeError("Python 3.12+ required")
    if args.jobs < 1:
        raise RuntimeError("--jobs must be positive")
    host = {"Windows": "windows-x64", "Linux": "linux-x64", "Darwin": "macos-arm64"}.get(platform.system())
    if host != args.target or platform.machine().lower() not in (("arm64", "aarch64") if host == "macos-arm64" else ("x86_64", "amd64")):
        raise RuntimeError("Bootstrap is native only; target does not match this host")
    cmake = require(args.cmake)
    version = subprocess.check_output([cmake, "--version"], text=True)
    match = re.search(r"cmake version (\d+)\.(\d+)", version)
    if not match or tuple(map(int, match.groups())) < (3, 25):
        raise RuntimeError("CMake 3.25+ required")
    if args.toolchain and host != "windows-x64":
        raise RuntimeError("--toolchain is only supported for windows-x64")
    if host == "macos-arm64":
        run([require("xcrun"), "--sdk", "macosx", "--show-sdk-path"])
        print("macOS uses SDK/system libcurl; no dependencies downloaded or installed.")
        return
    windows = host == "windows-x64"
    mingw = windows and args.toolchain == "mingw"
    toolchain = ("mingw" if mingw else "msvc") if windows else "gcc"
    if windows:
        if mingw:
            cc, cxx, make = require("gcc"), require("g++"), require("mingw32-make")
            for compiler in (cc, cxx):
                triple = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
                version = subprocess.check_output([compiler, "-dumpfullversion"], text=True).strip()
                if triple != "x86_64-w64-mingw32" or int(version.split(".")[0]) < 11:
                    raise RuntimeError("MinGW requires native x64 MinGW-w64 GCC/G++ 11+; MSYS/Cygwin compilers are unsupported")
            generator = ["-G", "MinGW Makefiles", f"-DCMAKE_C_COMPILER={Path(cc).as_posix()}",
                         f"-DCMAKE_CXX_COMPILER={Path(cxx).as_posix()}", f"-DCMAKE_MAKE_PROGRAM={Path(make).as_posix()}"]
        else:
            require("cl")
            if os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x64":
                raise RuntimeError("Use the x64 Visual Studio 2022 developer command prompt")
            generator = ["-G", "Visual Studio 17 2022", "-A", "x64"]
    else:
        require("gcc"); require("g++"); require("make"); require("perl")
        if int(subprocess.check_output(["gcc", "-dumpversion"], text=True).split(".")[0]) < 11:
            raise RuntimeError("GCC 11+ required")
        generator = ["-G", "Unix Makefiles", "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++"]
        if not Path("/etc/ssl/certs/ca-certificates.crt").is_file():
            raise RuntimeError("Ubuntu system CA bundle missing: /etc/ssl/certs/ca-certificates.crt")
    deps = ROOT / ".deps"
    profile = "windows-mingw-x64" if mingw else host
    work = deps / profile
    cache, sources, prefix = deps / "cache", work / "sources", work / "install"
    for path in (cache, sources, prefix):
        path.mkdir(parents=True, exist_ok=True)
    # Prove the compiler and linker work before downloading or running dependency builds.
    preflight = work / "preflight"
    preflight.mkdir(exist_ok=True)
    (preflight / "main.c").write_text("int main(void) { return 0; }\n")
    (preflight / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(preflight C)\n'
        'if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)\nmessage(FATAL_ERROR "x64 required")\nendif()\n' +
        ('if(NOT MINGW OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_VERSION VERSION_LESS "11")\n'
         'message(FATAL_ERROR "MinGW-w64 GCC11+ required")\nendif()\n' if mingw else
         'if(WIN32 AND (NOT MSVC OR MSVC_VERSION LESS 1930))\nmessage(FATAL_ERROR "MSVC2022 required")\nendif()\n') +
        'add_executable(preflight main.c)\n')
    run([cmake, "-S", preflight, "-B", preflight / "build", *generator])
    run([cmake, "--build", preflight / "build", "--config", "Release"])
    if args.preflight_only:
        print("Native build prerequisites passed; no downloads attempted.")
        return
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    specs = lock["dependencies"]
    names = ["zlib", "nghttp2"] + ([] if windows else ["openssl"]) + ["curl"]
    # A failed download stops the entire operation immediately; no TLS bypass/fallback.
    source = {name: archive_source(name, specs[name], cache, sources) for name in names}
    (prefix / "licenses").mkdir(exist_ok=True)
    common = [f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"]
    if windows and not mingw:
        common += ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded", "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"]
    if mingw:
        common += ["-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++"]

    def build_cmake(name, options):
        build = work / "build" / name
        run([cmake, "-S", source[name], "-B", build, *generator, *common, *options])
        run([cmake, "--build", build, "--config", "Release", "--parallel", args.jobs])
        run([cmake, "--install", build, "--config", "Release"])

    build_cmake("zlib", ["-DZLIB_BUILD_SHARED=OFF", "-DZLIB_BUILD_STATIC=ON", "-DZLIB_BUILD_TESTING=OFF"])
    build_cmake("nghttp2", ["-DBUILD_SHARED_LIBS=OFF", "-DBUILD_STATIC_LIBS=ON", "-DENABLE_LIB_ONLY=ON",
        "-DENABLE_STATIC_CRT=ON", "-DENABLE_DOC=OFF", "-DENABLE_FAILMALLOC=OFF",
        "-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_Libngtcp2=ON",
        "-DCMAKE_DISABLE_FIND_PACKAGE_Libnghttp3=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_Systemd=ON",
        "-DCMAKE_DISABLE_FIND_PACKAGE_Jansson=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_Libevent=ON"])
    if not windows:
        build = work / "build" / "openssl"
        build.mkdir(parents=True, exist_ok=True)
        run(["perl", source["openssl"] / "Configure", "linux-x86_64", "no-shared", "no-tests", "no-module",
             f"--prefix={prefix}", "--libdir=lib", "--openssldir=/etc/ssl"], cwd=build)
        # OpenSSL emits absolute, unescaped configuration prerequisites in its
        # Makefile. Relative source paths keep checkouts with spaces buildable
        # without moving dependencies or changing the verified source archive.
        makefile = build / "Makefile"
        generated = makefile.read_text(encoding="utf-8")
        generated = generated.replace(str(source["openssl"]) + "/",
                                      Path(os.path.relpath(source["openssl"], build)).as_posix() + "/")
        makefile.write_text(generated, encoding="utf-8")
        run(["make", f"-j{args.jobs}"], cwd=build)
        run(["make", "install_sw"], cwd=build)
    # Explicit archive locations keep CMake from silently finding globally installed variants.
    zlib = prefix / "lib" / ("libzs.a" if mingw else "zs.lib" if windows else "libz.a")
    nghttp2 = prefix / "lib" / ("nghttp2.lib" if windows and not mingw else "libnghttp2.a")
    for path in (zlib, nghttp2):
        if not path.is_file():
            raise RuntimeError(f"Expected static archive missing: {path}")
    options = ["-DBUILD_SHARED_LIBS=OFF", "-DBUILD_STATIC_LIBS=ON", "-DBUILD_CURL_EXE=OFF",
        "-DBUILD_LIBCURL_DOCS=OFF", "-DBUILD_MISC_DOCS=OFF", "-DENABLE_CURL_MANUAL=OFF", "-DHTTP_ONLY=ON",
        "-DCURL_USE_LIBPSL=OFF", "-DCURL_USE_LIBSSH2=OFF", "-DCURL_USE_GSSAPI=OFF", "-DUSE_LIBIDN2=OFF",
        "-DCURL_BROTLI=OFF", "-DCURL_ZSTD=OFF", "-DCURL_ZLIB=ON", "-DUSE_NGHTTP2=ON", "-DUSE_NGTCP2=OFF",
        "-DCURL_USE_PKGCONFIG=OFF", "-DCURL_USE_CMAKECONFIG=OFF", "-DENABLE_THREADED_RESOLVER=ON",
        f"-DZLIB_INCLUDE_DIR={prefix.as_posix()}/include", f"-DZLIB_LIBRARY={zlib.as_posix()}",
        f"-DNGHTTP2_INCLUDE_DIR={prefix.as_posix()}/include", f"-DNGHTTP2_LIBRARY={nghttp2.as_posix()}"]
    if windows:
        # curl's finder otherwise declares nghttp2 functions as DLL imports,
        # even when NGHTTP2_LIBRARY explicitly points to the static archive.
        options += ["-DNGHTTP2_USE_STATIC_LIBS=ON", "-DCURL_USE_SCHANNEL=ON", "-DCURL_USE_OPENSSL=OFF",
                    "-DENABLE_UNICODE=ON", "-DCURL_STATIC_CRT=ON"]
    else:
        options += ["-DCURL_USE_OPENSSL=ON", "-DCURL_USE_SCHANNEL=OFF", "-DOPENSSL_USE_STATIC_LIBS=ON",
            f"-DOPENSSL_ROOT_DIR={prefix.as_posix()}", f"-DOPENSSL_INCLUDE_DIR={prefix.as_posix()}/include",
            f"-DOPENSSL_SSL_LIBRARY={prefix.as_posix()}/lib/libssl.a", f"-DOPENSSL_CRYPTO_LIBRARY={prefix.as_posix()}/lib/libcrypto.a",
            "-DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt", "-DCURL_CA_PATH=/etc/ssl/certs"]
    build_cmake("curl", options)
    curl = prefix / "lib" / ("libcurl.lib" if windows and not mingw else "libcurl.a")
    if not curl.is_file():
        raise RuntimeError(f"Expected static libcurl missing: {curl}")
    for name in names:
        shutil.copyfile(source[name] / specs[name]["license_file"], prefix / "licenses" / f"{name}.txt")
    manifest = {name: {"version": specs[name]["version"], "source_sha256": specs[name]["sha256"],
                       "linkage": "static"} for name in names}
    manifest["tls_backend"] = "Schannel" if windows else "OpenSSL"
    manifest["toolchain"] = toolchain
    (prefix / "dependency-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    stamp = f'set(ITU_INSTALLED_LOCK_SHA256 "{hashlib.sha256(LOCK_PATH.read_bytes()).hexdigest()}")\nset(ITU_INSTALLED_TARGET "{host}")\n'
    stamp += f'set(ITU_INSTALLED_TOOLCHAIN "{toolchain}")\n'
    for name, path in (("curl", curl), ("zlib", zlib), ("nghttp2", nghttp2)):
        stamp += f'set(ITU_{name}_LIBRARY "{path.relative_to(prefix).as_posix()}")\n'
    (prefix / "itu-dependencies.cmake").write_text(stamp, encoding="utf-8")
    print(f"Dependencies installed locally: {prefix}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"Bootstrap stopped: {error}")
