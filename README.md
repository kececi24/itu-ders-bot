# İTÜ Course Picker

A native C++ course registration helper for Windows, macOS, and Linux. It authenticates to İTÜ OBS, prepares add/drop requests, and can submit one request at a configured time. One codebase and version produce three native release archives.

Use only with your own authorized account and in accordance with university registration rules.

The released `v1.0.1` contains the cross-platform port. This `polling-ntp` working tree adds newer polling and local-clock behavior described below; those additions are not in the existing release, even though the project version retains the released baseline.

## Build

Supported targets:

| Target / preset | Toolchain | HTTP / TLS dependencies |
|---|---|---|
| `windows-x64` — Windows 10/11 x64 | Visual Studio 2022 MSVC and Windows SDK | Static libcurl, nghttp2, zlib; native Schannel |
| `windows-mingw-x64` — Windows 10/11 x64 | MinGW-w64 GCC/G++ 11+ and mingw32-make | Same Windows dependencies; static GNU runtimes |
| `macos-arm64` — Apple Silicon; macOS 14/15 experimental | Apple Clang and macOS SDK from Xcode / Command Line Tools | Apple SDK/system libcurl |
| `linux-x64` — Ubuntu 22.04/24.04 x64 | GCC 11+, make, Perl | Static libcurl, nghttp2, zlib, OpenSSL; system CA bundle and dynamic C/C++ runtime |

Other compatible glibc distributions are best effort. Intel Macs, ARM Windows/Linux, musl, and 32-bit targets are outside the support matrix.

CMake 3.25+ is required. Python 3.12+ is required for the Windows/Linux dependency bootstrap, offline tests, and packaging. JSON is vendored. Bootstrap requires an existing native compiler and SDK; it never installs them. All downloaded tools, sources, dependency builds, and installations must stay within ignored `.deps/`. Downloads use TLS verification and pinned SHA-256 values in `cmake/dependencies.lock.json`; a failure stops bootstrap without an insecure fallback.

Run build commands from the project root using existing tools. Keep downloaded tools/dependencies project-local. On macOS, reuse project-local CMake (adjust the path for your version):

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

MinGW builds use `build/windows-mingw-x64/bin` and isolated `.deps/windows-mingw-x64` dependencies. They retain the Windows Schannel TLS backend and statically link GNU runtimes; no MinGW DLLs need to accompany the executables. MSVC libraries cannot be reused for this preset. MinGW archives include GCC runtime licensing and the MinGW-w64/winpthreads notices. To package it, run `python scripts/package.py build/windows-mingw-x64/bin --target windows-x64 --output dist/mingw`, then `python tests/native_artifacts.py dist/mingw/itu-ders-bot-1.0.1-windows-x64.zip --target windows-x64`. The archive target remains `windows-x64`; use a separate output directory when comparing both compilers. MinGW remains a separate CI release gate on this branch.

On Linux, with existing GCC, make, Perl, and Python 3.12+:

```sh
CMAKE=cmake # Or "$PWD/.deps/tools/.../bin/cmake"
python3 scripts/bootstrap.py --target linux-x64 --cmake "$CMAKE"
"$CMAKE" --preset linux-x64
"$CMAKE" --build --preset linux-x64
"$(dirname "$(command -v "$CMAKE")")/ctest" --preset linux-x64 --parallel 3
```

When Python is project-local, also pass `-DPython3_EXECUTABLE=/full/path/to/python3` to configure. CMake distributions under `cmake-*-macos-*/`, `.deps/`, `build/`, and `dist/` are ignored by Git. No global dependency installation is needed. To omit development tests and their Python requirement during configuration, add `-DBUILD_TESTING=OFF`; Windows/Linux bootstrap still needs Python.

Executables are `build/<preset>/bin/main` and `setup` (`main.exe` and `setup.exe` on Windows). Run them from the **project root** so `.env` and `data/config.json` resolve correctly. Windows examples use MSVC; substitute `windows-mingw-x64` in executable paths for a MinGW build.

## Setup

| Platform | Command |
|---|---|
| Windows (PowerShell) | `.\build\windows-x64\bin\setup.exe` |
| macOS | `./build/macos-arm64/bin/setup` |
| Linux | `./build/linux-x64/bin/setup` |

Use the arrow keys and Enter to choose credentials or course/time configuration. Password input is hidden; input requires a terminal. Files are replaced atomically with owner-only permissions (POSIX mode `0600`, or an owner-restricted Windows DACL). Append `--config-path data/config.json --env-path .env` to the appropriate setup command to choose file locations, replacing those paths as needed.

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
| Windows (PowerShell) | `.\build\windows-x64\bin\main.exe --test --dry-run` |
| macOS | `./build/macos-arm64/bin/main --test --dry-run` |
| Linux | `./build/linux-x64/bin/main --test --dry-run` |

Inspect local system clock synchronization and health without credentials or network calls:

Run your platform's `main` command with only `--check-clock`, for example `./build/macos-arm64/bin/main --check-clock`.

To opt into legacy server clock sampling via HTTP `Date` headers in single-attempt mode:

Append `--server-time` to the dry-run command above. Removing `--local` does not enable server sampling; local time is the default on this branch.

These commands contact OBS (except `--check-clock`) and use your credentials, but do **not** send the registration request. Offline automated tests use loopback fixtures with synthetic credentials; they cannot prove compatibility with the live OBS service. Live JWT acquisition must be verified separately with an authorized account.

Flags can be combined:

- `--dry-run`: authenticate, build the payload, and prepare the request, then exit before the final wait or submission. Earlier scheduling waits still occur unless combined with `--test`.
- `--test`: skip scheduled waits. **This sends a real registration request unless paired with `--dry-run`.** With polling, only the `--test --dry-run` combination is accepted.
- `--server-time`: opt into legacy server HTTP `Date` clock sampling in single-attempt mode (the application now uses your system's local clock by default). Refused when polling is enabled.
- `--check-clock`: inspect system clock synchronization status, provider, and error bounds in read-only mode without contacting OBS or requiring credentials.
- `--local`: retain local clock timing (default; retained for backwards compatibility).
- `--logs`: show safe diagnostic stages and counts. Polling emits JSON Lines on stderr, including early validation failures, with human progress on stdout. Passwords, tokens, cookies, session URLs, and raw authenticated response bodies are omitted.

For an intentional scheduled registration after reviewing your configuration:

| Platform | Command |
|---|---|
| Windows (PowerShell) | `.\build\windows-x64\bin\main.exe` |
| macOS | `./build/macos-arm64/bin/main` |
| Linux | `./build/linux-x64/bin/main` |

In default single-attempt mode, the program maintains an authentication session with cookies and redirects, and a separate persistent registration session. When `--server-time` is enabled, it samples seven HTTP `Date` headers, selects the lowest-RTT offset, and resamples at 90 seconds before the target when time permits. Token acquisition starts 60 seconds before the target. It prepares the registration request before the final wait and makes one submission attempt, with no application retry. HTTP, authentication, and malformed-response failures exit nonzero.

A registration transfer has a 30-second total timeout (including connection setup), with a 10-second connection limit. If the transfer times out or disconnects, **the registration outcome is unknown**: OBS may already have applied some or all changes. Check your registered courses before retrying. The program reports the curl error code, elapsed milliseconds, last observed HTTP status (`0` if none), and received body byte count without printing response contents. It does not automatically resend the batch. A completed response with mixed accepted/rejected CRNs is displayed using the normal per-course result messages.

### Polling mode (opt-in, add-only)

When course registration opens under heavy load or staggered seat releases, an optional add-only polling mode can be configured in `data/config.json`:

```json
{
  "time": {
    "year": 2026, "month": 10, "day": 1,
    "hour": 14, "minute": 0, "second": 0,
    "millisecond": 0, "lead_millisecond": 0
  },
  "courses": {
    "crn": ["10001", "10002"],
    "scrn": []
  },
  "polling": {
    "enabled": true,
    "start": "2026-10-01T14:00:00+03:00",
    "end": "2026-10-01T14:05:00+03:00",
    "min_interval_ms": 3000,
    "max_interval_ms": 60000,
    "expected_interval_ms": 40000,
    "distribution": "beta",
    "beta_concentration": 6.0,
    "backoff_base_ms": 30000,
    "backoff_max_ms": 300000,
    "max_attempts": 10,
    "request_budget": {
      "count": 20,
      "window_seconds": 3600
    }
  }
}
```

Polling is disabled when the section is absent or `enabled` is false. The example values are explicit client limits, not a verified OBS operating rate. The documented registration cooldown is three seconds; the sustained count/window and any alleged 100-requests/hour limit remain unverified. See the [İTÜ registration notice](https://www.sis.itu.edu.tr/EN/student/course-schedules/202710/ungraduate-announcements.php) and [İTÜ's volume-block explanation](https://haberler.itu.edu.tr/haberdetay/2025/09/25/itu-kepler-obs-ogrencilerimize-daha-iyi-ders-kaydi-deneyimi-sunmak-icin-gelisimini-surduruyor). Other clients using the account/network are outside this process's budget.

Both timestamps require an explicit UTC offset. The beta distribution defaults to concentration 6, with its mean set by `expected_interval_ms` strictly between the bounds; `uniform` requires the mean to equal the midpoint. Samples are independent. The example mean is 40 seconds (nominally 90 attempts/hour before response time, backoff and budget waits), while its 20-requests/hour client budget also counts authentication and redirects, reducing actual registration throughput. `max_interval_ms`, `expected_interval_ms`, `max_attempts` and the request budget must be supplied when enabling polling. Backoff defaults to 30 seconds, capped at 300 seconds; a valid `Retry-After` can extend the wait beyond either interval bound.

Budget reservations and cooldowns persist privately in ignored `.itu-runtime/`, and a lock prevents overlapping polling processes in the same project directory. Transfers are charged at completion; an unresolved transfer after a crash stays charged for a full budget window after restart. Upgrading older version-1 state also imposes one full retained-history window before admitting requests. Preserve this directory between runs; deleting it discards local safety history. For analysis, redirect `--logs` stderr to a file under ignored `logs/`; neither logs nor runtime state enter release archives.

Polling reuses the JWT and authenticated session. A known expiry with less than 35 seconds remaining triggers renewal, limited to two JWT refresh attempts and one full reauthentication per run; generic HTTP 403 and ambiguous registration failures do not trigger login. Positive `Retry-After` on an authentication redirect stops the transfer before following it and persists the cooldown. These safeguards are covered by offline fixtures; the live OBS JWT lifetime and limits remain unverified.

Key polling invariants:

- **Add-only**: Polling applies only to adding CRNs. Any drops in `courses.scrn` cause validation to fail.
- **Strict budget and bounds**: Enforces client request budgets across registration and authentication. `min_interval_ms` floor is 3000 ms.
- **Conservative stop/replay rules**:
  - Unambiguous business rejections (quota full `VAL06`, registration-time restriction `VAL02`) retry only remaining unsatisfied courses without backoff. Prerequisite failure `VAL11` requires user action and stops polling.
  - Confirmed course adds (`successResult`, `VAL03`) are pruned from future attempts so satisfied courses are never resubmitted.
  - Action-required responses (`VAL04`, etc.) stop polling immediately while preserving confirmed additions.
  - Rate limiting (HTTP 429) stops polling immediately.
  - Proven pre-dispatch transport failures back off exponentially within configured bounds.
  - In-flight network timeouts or ambiguous POST disconnects stop immediately with an unknown outcome—never replayed.
- **Local clock**: Polling runs strictly on local monotonic and system time. `--server-time` and submission-enabled `--test` are rejected. `--dry-run` and `--logs` are supported.


## Timing limits

Authentication and final scheduled waits use a monotonic clock. A wall/monotonic discrepancy over one second stops submission instead of sending after a clock step or resume; ordinary small NTP slewing is tolerated. Cancellation and synchronization evidence are checked again before dispatch. An explicitly unsynchronized clock stops the operation; unavailable synchronization evidence produces a warning. The last five milliseconds of the single-attempt final wait spin. During the final two seconds through submission, macOS requests user-initiated thread QoS; Windows requests higher thread priority and 1 ms timer resolution. These are best effort and scoped, with previous settings restored. Linux keeps normal priority.

Keep the computer awake, connected to power, and online; lid closure and sleep can interrupt timing. HTTP dates have whole-second precision (roughly ±500 ms), and scheduling, network latency, TLS setup, and server queues prevent any guaranteed arrival time. A successful dry-run proves preparation, not future registration success.

## Release archives

Each native build is packaged from `build/<preset>/bin` into `dist/`. The examples use the current project version `1.0.1`; use the version recorded in your build manifest if it differs.

Windows (PowerShell):

```powershell
python tests/native_artifacts.py build/windows-x64/bin --target windows-x64
python scripts/package.py build/windows-x64/bin --target windows-x64
python tests/native_artifacts.py dist/itu-ders-bot-1.0.1-windows-x64.zip --target windows-x64
```

macOS:

```sh
python3 tests/native_artifacts.py build/macos-arm64/bin --target macos-arm64
python3 scripts/package.py build/macos-arm64/bin --target macos-arm64
python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.1-macos-arm64.tar.gz --target macos-arm64
```

Linux:

```sh
python3 tests/native_artifacts.py build/linux-x64/bin --target linux-x64
python3 scripts/package.py build/linux-x64/bin --target linux-x64
python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.1-linux-x64.tar.gz --target linux-x64
```

Windows produces `.zip`; macOS/Linux produce `.tar.gz`. Archive names include the project version. The artifact checker verifies native architecture, allowed system linkage, payload and outer checksums, sanitized configuration, and startup from a clean directory without credentials or networking.

The allowlisted archive contains native `main`/`setup` executables, `data/example_config.json`, README, required third-party licenses, `build-manifest.json`, and `SHA256SUMS`. An adjacent `.sha256` checks the archive itself. It excludes local credentials, configuration, logs, test binaries, and downloaded tools. The build manifest records target, version, revision, compiler, and dependency provenance. Release packaging uses `--tag v<version>` matching the project version and requires clean, committed inputs; omit it for local verification builds.

After extraction, run `./setup` and `./main` (`.\setup.exe` and `.\main.exe` on Windows) from the extracted directory. Verify the archive's adjacent checksum before extraction and the payload's `SHA256SUMS` after extraction. macOS/Linux can use `shasum -a 256 -c SHA256SUMS` or `sha256sum -c SHA256SUMS`; Windows can compute each hash with PowerShell `Get-FileHash -Algorithm SHA256` and compare it with the listed value.

macOS archives retain normal linker signatures but are not Developer ID signed or notarized. macOS may block downloaded executables; use the per-app **Open Anyway** control in System Settings → Privacy & Security only after verifying the source and checksums. Do not disable Gatekeeper globally.

CI defines native builds and the complete offline suite on Windows Server 2022/MSVC, macOS 14 ARM64, and Ubuntu 22.04/GCC. A separate Windows MinGW job reuses the runner's existing GCC toolchain, executes the offline suite and verifies its ZIP; it must pass before release but does not publish another Windows asset. Each platform job verifies its extracted archive; macOS 15 and Ubuntu 24.04 smoke the identical archives produced on the older OS. Only `v*` tag pushes publish, after every native and archive job passes and all three matching versioned archives are aggregated into one release.

CI runs on pull requests, manual dispatch, and `v*` tag pushes; ordinary branch pushes do not trigger it. PR/manual runs upload verified archives as Actions artifacts and do not publish releases. A release contains one `windows-x64.zip`, one `macos-arm64.tar.gz`, one `linux-x64.tar.gz`, their adjacent checksums and aggregate `SHA256SUMS`, all with the same version/revision.

For a future release, choose an unused version, update `project(... VERSION ...)` in `CMakeLists.txt`, and commit the intended source/workflow changes before creating its matching `v<version>` tag. `v1.0.1` already identifies the released port; do not reuse it for newer polling source. Tag pushes run the full build/test/package pipeline before publication. These instructions do not invoke a workflow or publish anything.

Local macOS verification currently runs on macOS 26 ARM64. macOS 14/15 support is experimental: existing CI coverage is useful evidence, but desktop verification on those versions is unavailable and is not an active-plan acceptance requirement.

CI definitions alone are not execution evidence. Windows Server CI does not establish Windows 10/11 desktop-console acceptance; deployment-target and glibc checks do not replace execution on the supported OS. Released-port acceptance is recorded in `.agents/exec_plans/closed/cross-platform-port.md`; validation of the newer polling source is tracked in `.agents/exec_plans/active/2026-10-04-registration-polling.md`.

## Acknowledgments

Original behavior and result mappings were adapted from [AtaTrkgl/itu-ders-secici](https://github.com/AtaTrkgl/itu-ders-secici). This project uses the vendored [nlohmann/json](https://github.com/nlohmann/json) header library.
