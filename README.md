# İTÜ Course Picker for macOS

A native C++ course registration helper for **Apple Silicon Macs running macOS 14 or later**. It authenticates to İTÜ OBS, prepares add/drop requests, and can submit one request at a configured time. Windows and Intel Macs are not supported by this edition.

Use only with your own authorized account and in accordance with university registration rules.

## Build

Required tools are Apple Clang and the macOS SDK from Xcode or Command Line Tools, plus CMake 3.20 or later. macOS supplies libcurl; the build resolves its headers and library only from the Apple SDK. JSON is vendored. Python 3.12+ and `/usr/bin/openssl` are used only for offline tests and packaging.

Keep downloaded build tools within this project. For the local CMake distribution already in this workspace:

```sh
CMAKE="$PWD/cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake"
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build --parallel 3
"${CMAKE%/cmake}/ctest" --test-dir build --output-on-failure --parallel 3
```

If using a different project-local CMake version, adjust `CMAKE` to its executable. CMake distributions under `cmake-*-macos-*/`, `.deps/`, `build/`, and `dist/` are ignored by Git. No Homebrew installation or global dependency installation is needed when the Apple toolchain is already available. To omit development tests and their Python requirement, configure with `-DBUILD_TESTING=OFF`.

Executables are `build/bin/main` and `build/bin/setup`. Run them from the **project root** so `.env` and `data/config.json` resolve correctly.

## Setup

```sh
./build/bin/setup
```

Use the arrow keys and Enter to choose credentials or course/time configuration. Password input is hidden; input requires a terminal. Files are replaced atomically with owner-only permissions. Optional paths:

```sh
./build/bin/setup --config-path data/config.json --env-path .env
```

You can also create `.env` yourself (ignored by Git):

```dotenv
ITU_USERNAME=your_itu_username
ITU_PASSWORD=your_itu_password
```

Protect manually created credentials with `chmod 600 .env`. Primary process environment values take precedence over primary `.env` values, then legacy `ITU_OBS_USERNAME`/`ITU_OBS_PASSWORD` process/file values, then legacy `account` entries in the configuration. Keep all real credentials local.

Use `data/example_config.json` as the configuration shape; example CRNs and dates are placeholders. `data/config.json` is ignored by Git.

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

Time uses your Mac's **local timezone**, including daylight saving time. `crn` lists courses to add; `scrn` lists courses to drop and defaults to empty when absent. CRNs remain strings, including any leading zeroes. Legacy `milisecond` and `lead_milisecond` keys remain accepted.

Keep `lead_millisecond` at **0** unless you deliberately want to send before the estimated opening time. A positive lead can cause rejection or cooldown. The existing add-result messages are preserved; drop-result display remains unimplemented because its response schema has not been verified.

## Safe verification and registration

Authenticate and prepare a request without submitting courses or waiting for the configured time:

```sh
./build/bin/main --test --dry-run --local
```

Also verify server clock sampling:

```sh
./build/bin/main --test --dry-run
```

These commands contact OBS and use your credentials, but do **not** send the registration request. Offline automated tests use loopback fixtures with synthetic credentials; they cannot prove compatibility with the live OBS service. Live JWT acquisition must be verified separately with an authorized account.

Flags can be combined:

- `--dry-run`: authenticate, build the payload, and prepare the request, then exit before the final wait or submission. Earlier scheduling waits still occur unless combined with `--test`.
- `--test`: skip scheduled waits. **This sends a real registration request unless paired with `--dry-run`.**
- `--local`: disable server clock sampling; use local time.
- `--logs`: show safe diagnostic stages and counts. Passwords, tokens, cookies, session URLs, and raw authenticated response bodies are omitted.

For an intentional scheduled registration after reviewing your configuration:

```sh
./build/bin/main
```

The program maintains an authentication session with cookies and redirects, and a separate persistent clock/registration session. It samples seven HTTP `Date` headers, selects the lowest-RTT offset, resamples at 90 seconds before the target when time permits, and starts token acquisition 60 seconds before the target. It prepares the registration request before the final wait and makes one submission attempt, with no application retry. HTTP, authentication, and malformed-response failures exit nonzero.

A registration transfer has a 30-second total timeout (including connection setup), with a 10-second connection limit. If the transfer times out or disconnects, **the registration outcome is unknown**: OBS may already have applied some or all changes. Check your registered courses before retrying. The program reports the curl error code, elapsed milliseconds, last observed HTTP status (`0` if none), and received body byte count without printing response contents. It does not automatically resend the batch. A completed response with mixed accepted/rejected CRNs is displayed using the normal per-course result messages.

## Timing limits

The final deadline uses a monotonic clock, so later wall-clock adjustments do not change it. The last five milliseconds spin; during the final two seconds through submission, the process makes a best-effort request for user-initiated thread QoS, then restores the previous setting.

Keep the Mac awake, connected to power, and online; lid closure and sleep can interrupt timing. HTTP dates have whole-second precision (roughly ±500 ms), and scheduling, network latency, TLS setup, and server queues prevent any guaranteed arrival time. A successful dry-run proves preparation, not future registration success.

## Release archive

To package a local build and verify its architecture, deployment target, system linkage, and archive contents:

```sh
python3 tests/native_artifacts.py build/bin
python3 scripts/package.py
python3 tests/native_artifacts.py dist/itu-ders-bot-macos-arm64.tar.gz
```

The archive contains `main`, `setup`, `data/example_config.json`, this README, and `SHA256SUMS`. An adjacent `.sha256` checks the archive itself. It excludes local credentials, configuration, logs, test binaries, and downloaded build tools.

After extraction, run `./setup` and `./main` from the extracted directory instead of the build paths above. Verify `shasum -a 256 -c SHA256SUMS` first. Releases retain normal linker signatures but are not Developer ID signed or notarized. macOS may block downloaded executables; use the per-app **Open Anyway** control in System Settings → Privacy & Security only after verifying the source and checksums. Do not disable Gatekeeper globally.

CI builds/tests on macOS 14 and 15 ARM64, packages on 14, and smoke-tests that same archive on 15. Only `v*` tag pushes publish release assets. Local builds on newer macOS still target macOS 14; cross-version execution is validated by CI, not by a deployment-target flag alone.

## Acknowledgments

Original behavior and result mappings were adapted from [AtaTrkgl/itu-ders-secici](https://github.com/AtaTrkgl/itu-ders-secici). This project uses the vendored [nlohmann/json](https://github.com/nlohmann/json) header library.
