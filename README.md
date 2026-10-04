# İTÜ Course Picker

A native C++ course registration helper for Windows, macOS, and Linux. It authenticates to İTÜ OBS, prepares add/drop requests, and can submit one request at a configured time. One codebase and version produce three native release archives.

Use only with your own authorized account and in accordance with university registration rules.

## Build

Supported targets:

| Target / preset | Toolchain | HTTP / TLS dependencies |
|---|---|---|
| `windows-x64` — Windows 10/11 x64 | Visual Studio 2022 MSVC and Windows SDK | Static libcurl, nghttp2, zlib; native Schannel |
| `windows-mingw-x64` — Windows 10/11 x64 | MinGW-w64 GCC/G++ 11+ and mingw32-make | Same Windows dependencies; static GNU runtimes |
| `macos-arm64` — Apple Silicon, macOS 14+ | Apple Clang and macOS SDK from Xcode / Command Line Tools | Apple SDK/system libcurl |
| `linux-x64` — Ubuntu 22.04/24.04 x64 | GCC 11+, make, Perl | Static libcurl, nghttp2, zlib, OpenSSL; system CA bundle and dynamic C/C++ runtime |

Other compatible glibc distributions are best effort. Intel Macs, ARM Windows/Linux, musl, and 32-bit targets are outside the support matrix.

CMake 3.25+ is required. Python 3.12+ is required for the Windows/Linux dependency bootstrap, offline tests, and packaging. JSON is vendored. Bootstrap requires an existing native compiler and SDK; it never installs them. All downloaded tools, sources, dependency builds, and installations must stay within ignored `.deps/`. Downloads use TLS verification and pinned SHA-256 values in `cmake/dependencies.lock.json`; a failure stops bootstrap without an insecure fallback.

On macOS, reuse project-local CMake (adjust the path for your version):

```sh
CMAKE="$PWD/cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake"
"$CMAKE" --preset macos-arm64
"$CMAKE" --build --preset macos-arm64
"${CMAKE%/cmake}/ctest" --preset macos-arm64 --parallel 3
```

For Windows with MSVC, use an **x64 Visual Studio 2022 developer prompt**. In PowerShell, point `CMAKE` to your existing or project-local executable:

```powershell
$CMAKE = 'cmake' # Or the full path to .deps/.../bin/cmake.exe
python scripts/bootstrap.py --target windows-x64 --cmake $CMAKE
& $CMAKE --preset windows-x64
& $CMAKE --build --preset windows-x64
& (Join-Path (Split-Path (Get-Command $CMAKE).Source) 'ctest.exe') --preset windows-x64 --parallel 3
```

For Windows with MinGW-w64, use **PowerShell or cmd** with existing native x64 `gcc`, `g++` and `mingw32-make` on PATH. For an existing MSYS2 UCRT64 toolchain, prepend its `ucrt64\bin` directory to PATH in PowerShell. Use native Windows Python 3.12+; `python --version` must meet that floor. MSYS/Cygwin GCC and 32-bit MinGW are unsupported. This preset uses CMake's [MinGW Makefiles generator](https://cmake.org/cmake/help/latest/generator/MinGW%20Makefiles.html).

```powershell
$CMAKE = 'cmake'
$PYTHON = 'python' # Or the full path to your native Python 3.12+ executable
& $PYTHON scripts/bootstrap.py --target windows-x64 --toolchain mingw --cmake $CMAKE
$pythonPath = (& $PYTHON -c "import sys; print(sys.executable)")
& $CMAKE --preset windows-mingw-x64 "-DPython3_EXECUTABLE=$pythonPath"
& $CMAKE --build --preset windows-mingw-x64
& (Join-Path (Split-Path (Get-Command $CMAKE).Source) 'ctest.exe') --preset windows-mingw-x64 --parallel 3
```

MinGW builds use `build/windows-mingw-x64/bin` and isolated `.deps/windows-mingw-x64` dependencies. They retain the Windows Schannel TLS backend and statically link GNU runtimes; no MinGW DLLs need to accompany the executables. MSVC libraries cannot be reused for this preset. MinGW archives include GCC runtime licensing and the MinGW-w64/winpthreads notices. To package it, run `python scripts/package.py build/windows-mingw-x64/bin --target windows-x64 --output dist/mingw`, then `python tests/native_artifacts.py dist/mingw/itu-ders-bot-1.0.0-windows-x64.zip --target windows-x64`. The archive target remains `windows-x64`; use a separate output directory when comparing both compilers.

On Linux, with existing GCC, make, Perl, and Python 3.12+:

```sh
CMAKE=cmake # Or "$PWD/.deps/tools/.../bin/cmake"
python3 scripts/bootstrap.py --target linux-x64 --cmake "$CMAKE"
"$CMAKE" --preset linux-x64
"$CMAKE" --build --preset linux-x64
"$(dirname "$(command -v "$CMAKE")")/ctest" --preset linux-x64 --parallel 3
```

When Python is project-local, also pass `-DPython3_EXECUTABLE=/full/path/to/python3` to configure. CMake distributions under `cmake-*-macos-*/`, `.deps/`, `build/`, and `dist/` are ignored by Git. No global dependency installation is needed. To omit development tests and their Python requirement during configuration, add `-DBUILD_TESTING=OFF`; Windows/Linux bootstrap still needs Python.

Executables are `build/<preset>/bin/main` and `setup` (`main.exe` and `setup.exe` on Windows). Run them from the **project root** so `.env` and `data/config.json` resolve correctly. The examples below use `macos-arm64`; substitute your preset and Windows executable suffix as needed.

## Setup

```sh
./build/macos-arm64/bin/setup
```

Use the arrow keys and Enter to choose credentials or course/time configuration. Password input is hidden; input requires a terminal. Files are replaced atomically with owner-only permissions (POSIX mode `0600`, or an owner-restricted Windows DACL). Optional paths:

```sh
./build/macos-arm64/bin/setup --config-path data/config.json --env-path .env
```

You can also create `.env` yourself (ignored by Git):

```dotenv
ITU_USERNAME=your_itu_username
ITU_PASSWORD=your_itu_password
```

On macOS/Linux, protect manually created credentials with `chmod 600 .env`. On Windows, use `setup` to create the owner-restricted file. Primary process environment values take precedence over primary `.env` values, then legacy `ITU_OBS_USERNAME`/`ITU_OBS_PASSWORD` process/file values, then legacy `account` entries in the configuration. Keep all real credentials local.

The source template is `packaging/example_config.json`; archives place it at `data/example_config.json`. It contains empty course lists and zero lead. Set your own date and courses through `setup`; `data/config.json` is ignored by Git. The shape below uses placeholder values.

```json
{
  "time": {
    "year": 2026, "month": 10, "day": 1,
    "hour": 14, "minute": 0, "second": 0,
    "millisecond": 0, "lead_millisecond": 0
  },
  "courses": {"crn": ["10000"], "scrn": []}
}
```

Time uses your computer's **local timezone**, including daylight saving time. `crn` lists courses to add; `scrn` lists courses to drop and defaults to empty when absent. CRNs remain strings, including any leading zeroes. Legacy `milisecond` and `lead_milisecond` keys remain accepted.

Keep `lead_millisecond` at **0** unless you deliberately want to send before the estimated opening time. A positive lead can cause rejection or cooldown. The existing add-result messages are preserved; drop-result display remains unimplemented because its response schema has not been verified.

## Safe verification and registration

Authenticate and prepare a request without submitting courses or waiting for the configured time:

```sh
./build/macos-arm64/bin/main --test --dry-run --local
```

Also verify server clock sampling:

```sh
./build/macos-arm64/bin/main --test --dry-run
```

These commands contact OBS and use your credentials, but do **not** send the registration request. Offline automated tests use loopback fixtures with synthetic credentials; they cannot prove compatibility with the live OBS service. Live JWT acquisition must be verified separately with an authorized account.

Flags can be combined:

- `--dry-run`: authenticate, build the payload, and prepare the request, then exit before the final wait or submission. Earlier scheduling waits still occur unless combined with `--test`.
- `--test`: skip scheduled waits. **This sends a real registration request unless paired with `--dry-run`.**
- `--local`: disable server clock sampling; use local time.
- `--logs`: show safe diagnostic stages and counts. Passwords, tokens, cookies, session URLs, and raw authenticated response bodies are omitted.

For an intentional scheduled registration after reviewing your configuration:

```sh
./build/macos-arm64/bin/main
```

The program maintains an authentication session with cookies and redirects, and a separate persistent clock/registration session. It samples seven HTTP `Date` headers, selects the lowest-RTT offset, resamples at 90 seconds before the target when time permits, and starts token acquisition 60 seconds before the target. It prepares the registration request before the final wait and makes one submission attempt, with no application retry. HTTP, authentication, and malformed-response failures exit nonzero.

A registration transfer has a 30-second total timeout (including connection setup), with a 10-second connection limit. If the transfer times out or disconnects, **the registration outcome is unknown**: OBS may already have applied some or all changes. Check your registered courses before retrying. The program reports the curl error code, elapsed milliseconds, last observed HTTP status (`0` if none), and received body byte count without printing response contents. It does not automatically resend the batch. A completed response with mixed accepted/rejected CRNs is displayed using the normal per-course result messages.

## Timing limits

The final deadline uses a monotonic clock, so later wall-clock adjustments do not change it. The last five milliseconds spin. During the final two seconds through submission, macOS requests user-initiated thread QoS; Windows requests higher thread priority and 1 ms timer resolution. These are best effort and scoped, with previous settings restored. Linux keeps normal priority.

Keep the computer awake, connected to power, and online; lid closure and sleep can interrupt timing. HTTP dates have whole-second precision (roughly ±500 ms), and scheduling, network latency, TLS setup, and server queues prevent any guaranteed arrival time. A successful dry-run proves preparation, not future registration success.

## Release archive

To package and verify a local build, substitute your preset for `macos-arm64` below (`python` on Windows):

```sh
TARGET=macos-arm64
python3 tests/native_artifacts.py "build/$TARGET/bin" --target "$TARGET"
python3 scripts/package.py "build/$TARGET/bin" --target "$TARGET"
python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.0-"$TARGET".tar.gz --target "$TARGET"
```

Windows produces `.zip`; macOS/Linux produce `.tar.gz`. Archive names include the project version. The artifact checker verifies native architecture, allowed system linkage, payload and outer checksums, sanitized configuration, and startup from a clean directory without credentials or networking.

The allowlisted archive contains native `main`/`setup` executables, `data/example_config.json`, README, required third-party licenses, `build-manifest.json`, and `SHA256SUMS`. An adjacent `.sha256` checks the archive itself. It excludes local credentials, configuration, logs, test binaries, and downloaded tools. The build manifest records target, version, revision, compiler, and dependency provenance. For a tag build, pass `--tag v1.0.0` to packaging; the tag must match the project version.

After extraction, run `./setup` and `./main` (`.\setup.exe` and `.\main.exe` on Windows) from the extracted directory. Verify the archive's adjacent checksum before extraction and the payload's `SHA256SUMS` after extraction. macOS/Linux can use `shasum -a 256 -c SHA256SUMS` or `sha256sum -c SHA256SUMS`; Windows can compute each hash with PowerShell `Get-FileHash -Algorithm SHA256` and compare it with the listed value.

macOS archives retain normal linker signatures but are not Developer ID signed or notarized. macOS may block downloaded executables; use the per-app **Open Anyway** control in System Settings → Privacy & Security only after verifying the source and checksums. Do not disable Gatekeeper globally.

CI defines native builds and the complete offline suite on Windows Server 2022/MSVC, macOS 14 ARM64, and Ubuntu 22.04/GCC. A separate Windows MinGW job reuses the runner's existing GCC toolchain, executes the offline suite and verifies its ZIP; it must pass before release but does not publish another Windows asset. Each platform job verifies its extracted archive; macOS 15 and Ubuntu 24.04 smoke the identical archives produced on the older OS. Only `v*` tag pushes publish, after every native and archive job passes and all three matching versioned archives are aggregated into one release.

CI definitions alone are not execution evidence. Windows Server CI does not establish Windows 10/11 desktop-console acceptance; deployment-target and glibc checks do not replace execution on the supported OS. Current validation evidence and pending native/manual gates are recorded in `.agents/exec_plans/active/cross-platform-port.md`.

## Acknowledgments

Original behavior and result mappings were adapted from [AtaTrkgl/itu-ders-secici](https://github.com/AtaTrkgl/itu-ders-secici). This project uses the vendored [nlohmann/json](https://github.com/nlohmann/json) header library.
