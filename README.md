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

The commands below use existing `cmake` and `ctest` executables on PATH. If you keep CMake project-local, use the full paths to both tools under `.deps/` and pass the CMake path to bootstrap with `--cmake <path>`. Run all build commands from the project root.

### Windows (MSVC)

Use an **x64 Visual Studio 2022 developer prompt** with native Python 3.12+:

```powershell
python scripts/bootstrap.py --target windows-x64
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64 --parallel 3
```

### macOS

Use Apple Clang and the macOS SDK from Xcode / Command Line Tools:

```sh
python3 scripts/bootstrap.py --target macos-arm64 --preflight-only
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64 --parallel 3
```

### Linux

Use existing GCC, make, Perl, and Python 3.12+:

```sh
python3 scripts/bootstrap.py --target linux-x64
cmake --preset linux-x64
cmake --build --preset linux-x64
ctest --preset linux-x64 --parallel 3
```

### Windows (optional local MinGW build)

Use **PowerShell or cmd** with existing native x64 `gcc`, `g++` and `mingw32-make` on PATH. For an existing MSYS2 UCRT64 toolchain, prepend its `ucrt64\bin` directory to PATH in PowerShell. Use native Windows Python 3.12+; `python --version` must meet that floor. MSYS/Cygwin GCC and 32-bit MinGW are unsupported. This preset uses CMake's [MinGW Makefiles generator](https://cmake.org/cmake/help/latest/generator/MinGW%20Makefiles.html).

```powershell
$PYTHON = 'python' # Or the full path to native Python 3.12+
& $PYTHON scripts/bootstrap.py --target windows-x64 --toolchain mingw
$pythonPath = (& $PYTHON -c "import sys; print(sys.executable)")
cmake --preset windows-mingw-x64 "-DPython3_EXECUTABLE=$pythonPath"
cmake --build --preset windows-mingw-x64
ctest --preset windows-mingw-x64 --parallel 3
```

MinGW builds use `build/windows-mingw-x64/bin` and isolated `.deps/windows-mingw-x64` dependencies. They retain the Windows Schannel TLS backend and statically link GNU runtimes; no MinGW DLLs need to accompany the executables. MSVC libraries cannot be reused for this preset. MinGW archives include GCC runtime licensing and the MinGW-w64/winpthreads notices. To package it locally, run `python scripts/package.py build/windows-mingw-x64/bin --target windows-x64 --output dist/mingw`, then `python tests/native_artifacts.py dist/mingw/itu-ders-bot-1.0.0-windows-x64.zip --target windows-x64`. The archive target remains `windows-x64`; use a separate output directory when comparing both compilers. MinGW verification is local and is not a CI release gate.

When Python is project-local, also pass `-DPython3_EXECUTABLE=/full/path/to/python` to configure. Build outputs and `.deps/` are ignored by Git. No global dependency installation is needed. To omit development tests and their Python requirement during configuration, add `-DBUILD_TESTING=OFF`; Windows/Linux bootstrap still needs Python.

Executables are `build/<preset>/bin/main` and `setup` (`main.exe` and `setup.exe` on Windows). Run them from the **project root** so `.env` and `data/config.json` resolve correctly. Windows examples use MSVC; for a local MinGW build, replace `windows-x64` in executable paths with `windows-mingw-x64`.

## Setup

Run the command for your build:

| Platform | Command |
|---|---|
| Windows (PowerShell) | `.\build\windows-x64\bin\setup.exe` |
| macOS | `./build/macos-arm64/bin/setup` |
| Linux | `./build/linux-x64/bin/setup` |

Use the arrow keys and Enter to choose credentials or course/time configuration. Password input is hidden; input requires a terminal. Files are replaced atomically with owner-only permissions (POSIX mode `0600`, or an owner-restricted Windows DACL). To choose different file locations, append `--config-path data/config.json --env-path .env` to the setup command and replace the paths as needed.

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

| Platform | Command |
|---|---|
| Windows (PowerShell) | `.\build\windows-x64\bin\main.exe --test --dry-run --local` |
| macOS | `./build/macos-arm64/bin/main --test --dry-run --local` |
| Linux | `./build/linux-x64/bin/main --test --dry-run --local` |

To also verify server clock sampling, run the same command without `--local`, keeping `--test --dry-run`.

These commands contact OBS and use your credentials, but do **not** send the registration request. Offline automated tests use loopback fixtures with synthetic credentials; they cannot prove compatibility with the live OBS service. Live JWT acquisition must be verified separately with an authorized account.

Flags can be combined:

- `--dry-run`: authenticate, build the payload, and prepare the request, then exit before the final wait or submission. Earlier scheduling waits still occur unless combined with `--test`.
- `--test`: skip scheduled waits. **This sends a real registration request unless paired with `--dry-run`.**
- `--local`: disable server clock sampling; use local time.
- `--logs`: show safe diagnostic stages and counts. Passwords, tokens, cookies, session URLs, and raw authenticated response bodies are omitted.

For an intentional scheduled registration after reviewing your configuration:

| Platform | Command |
|---|---|
| Windows (PowerShell) | `.\build\windows-x64\bin\main.exe` |
| macOS | `./build/macos-arm64/bin/main` |
| Linux | `./build/linux-x64/bin/main` |

The program maintains an authentication session with cookies and redirects, and a separate persistent clock/registration session. It samples seven HTTP `Date` headers, selects the lowest-RTT offset, resamples at 90 seconds before the target when time permits, and starts token acquisition 60 seconds before the target. It prepares the registration request before the final wait and makes one submission attempt, with no application retry. HTTP, authentication, and malformed-response failures exit nonzero.

A registration transfer has a 30-second total timeout (including connection setup), with a 10-second connection limit. If the transfer times out or disconnects, **the registration outcome is unknown**: OBS may already have applied some or all changes. Check your registered courses before retrying. The program reports the curl error code, elapsed milliseconds, last observed HTTP status (`0` if none), and received body byte count without printing response contents. It does not automatically resend the batch. A completed response with mixed accepted/rejected CRNs is displayed using the normal per-course result messages.

## Timing limits

The final deadline uses a monotonic clock, so later wall-clock adjustments do not change it. The last five milliseconds spin. During the final two seconds through submission, macOS requests user-initiated thread QoS; Windows requests higher thread priority and 1 ms timer resolution. These are best effort and scoped, with previous settings restored. Linux keeps normal priority.

Keep the computer awake, connected to power, and online; lid closure and sleep can interrupt timing. HTTP dates have whole-second precision (roughly ±500 ms), and scheduling, network latency, TLS setup, and server queues prevent any guaranteed arrival time. A successful dry-run proves preparation, not future registration success.

## Release archives

Each native build is packaged from its `build/<preset>/bin` directory into `dist/`. Use the commands for your platform to create and verify a local archive. These examples use version `1.0.0`; replace it with the version of your build.

Windows (PowerShell):

```powershell
python tests/native_artifacts.py build/windows-x64/bin --target windows-x64
python scripts/package.py build/windows-x64/bin --target windows-x64
python tests/native_artifacts.py dist/itu-ders-bot-1.0.0-windows-x64.zip --target windows-x64
```

macOS:

```sh
python3 tests/native_artifacts.py build/macos-arm64/bin --target macos-arm64
python3 scripts/package.py build/macos-arm64/bin --target macos-arm64
python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz --target macos-arm64
```

Linux:

```sh
python3 tests/native_artifacts.py build/linux-x64/bin --target linux-x64
python3 scripts/package.py build/linux-x64/bin --target linux-x64
python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.0-linux-x64.tar.gz --target linux-x64
```

Windows produces `.zip`; macOS/Linux produce `.tar.gz`. Archive names include the project version. The artifact checker verifies native architecture, allowed system linkage, payload and outer checksums, sanitized configuration, and startup from a clean directory without credentials or networking.

The allowlisted archive contains native `main`/`setup` executables, `data/example_config.json`, README, required third-party licenses, `build-manifest.json`, and `SHA256SUMS`. An adjacent `.sha256` checks the archive itself. It excludes local credentials, configuration, logs, test binaries, and downloaded tools. The build manifest records target, version, revision, compiler, and dependency provenance. For a tag build, pass `--tag v1.0.0` to packaging; the tag must match the project version.

After extraction, run `./setup` and `./main` (`.\setup.exe` and `.\main.exe` on Windows) from the extracted directory. Verify the archive's adjacent checksum before extraction and the payload's `SHA256SUMS` after extraction. macOS/Linux can use `shasum -a 256 -c SHA256SUMS` or `sha256sum -c SHA256SUMS`; Windows can compute each hash with PowerShell `Get-FileHash -Algorithm SHA256` and compare it with the listed value.

macOS archives retain normal linker signatures but are not Developer ID signed or notarized. macOS may block downloaded executables; use the per-app **Open Anyway** control in System Settings → Privacy & Security only after verifying the source and checksums. Do not disable Gatekeeper globally.

### CI and GitHub Releases

CI builds and runs the complete offline suite on Windows Server 2022/MSVC, macOS 14 ARM64, and Ubuntu 22.04/GCC. Each native job packages and verifies its archive in `dist/`, then uploads it as an Actions artifact. macOS 15 and Ubuntu 24.04 smoke the identical archives produced on the older OS. MinGW remains available for local builds and tests.

Publishing is already automated for a version tag. After all three native jobs and the archive smoke checks pass, the release job downloads their `dist/` artifacts, verifies matching version/revision and checksums, and creates one [GitHub Release](https://github.com/kececi24/itu-ders-bot/releases) with:

- `itu-ders-bot-<version>-windows-x64.zip`
- `itu-ders-bot-<version>-macos-arm64.tar.gz`
- `itu-ders-bot-<version>-linux-x64.tar.gz`
- The adjacent `.sha256` files and aggregate `SHA256SUMS`.

To publish a new version from `main`:

1. Choose an unused version, for example `1.0.1`, and set the `project(... VERSION ...)` value in `CMakeLists.txt` to that version. Commit the version and all intended source/workflow changes, then push `main`.
2. Tag that committed version and push the tag. For the example version:

   ```sh
   git switch main
   git tag -a v1.0.1 -m "ITU Course Picker v1.0.1"
   git push origin v1.0.1
   ```

3. Wait for **Native Build, Test and Release** to finish, then open the repository's **Releases** page. A tag build reruns the build/test/package pipeline for that tagged commit; the tag must equal `v<project version>`.

Pull-request and manual branch runs provide downloadable Actions artifacts and do not publish a release. Existing tags still identify their original commits; use a new version tag for new source. A rerun of an existing release-tag build uploads its verified archives to that tag's existing release. See [GitHub CLI release publishing](https://cli.github.com/manual/gh_release_create).

CI definitions alone are not execution evidence. Windows Server CI does not establish Windows 10/11 desktop-console acceptance; deployment-target and glibc checks do not replace execution on the supported OS. Current validation evidence and pending native/manual gates are recorded in `.agents/exec_plans/active/cross-platform-port.md`.

## Acknowledgments

Original behavior and result mappings were adapted from [AtaTrkgl/itu-ders-secici](https://github.com/AtaTrkgl/itu-ders-secici). This project uses the vendored [nlohmann/json](https://github.com/nlohmann/json) header library.
