# macOS ARM64 course selector port

## Workstream disposition

Implementation and local/offline validation completed. Archived on 2026-09-27 at the user-requested planning split. Remaining live OBS, real-terminal, and supported-OS acceptance transferred to [the active verification plan](../active/macos-port-verification.md). This is not a claim of full live-service acceptance.

## Goal and authorization

Port the existing Windows course registration helper to native macOS while preserving authentication, course add/drop semantics, configuration, timing, flags, and setup behavior. The user approved the detailed conversation plan and requested implementation on 2026-09-26. Git mutations and live registration are not authorized.

User choices: macOS 14+, ARM64 only, C++17/Apple Clang, CMake 3.20+, Apple SDK/system libcurl, CLI archive without Developer ID signing or notarization. No Windows compatibility layer, Intel support, GUI, or unrelated redesign.

## Execution gate and current state

Implementation resumed after successful native preflight on 2026-09-27.

- Host macOS 26.6.2 ARM64; Apple Clang 21 and active Xcode SDK.
- SDK/system libcurl C++17 ARM64 compile/link/init passed with deployment target 14; otool shows system libraries only.
- Python 3.12.3 HTTP/TLS/PTY stdlib modules usable.
- Project-local `cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake --version` passed (4.4.3).
- No downloads needed. All future downloaded dependencies must stay project-local and be gitignored; warn before unavoidable global installs.
- No PLANS.md found. Existing untracked AGENTS.md, .DS_Store, and local CMake preserved.
- User authorized continuing the port once prerequisites passed. Git mutations and real registration remain unauthorized; credentials needed only for live JWT acceptance.

## Architecture and compatibility discoveries

- At the start of the port, all application networking used WinHTTP. Authentication owns one persistent session; clock sampling and registration share a second. Preserve those scopes.
- Authentication: OBS root GET with redirects; effective login URL and ASP.NET fields; form credential POST with landing Referer; optional first identityGuid link; /ogrenci/ GET; /ogrenci/auth/jwt GET.
- Preserve __VIEWSTATE, __VIEWSTATEGENERATOR, __EVENTVALIDATION, existing ctl00 credential/button field names and order, and the Giriş / Login value.
- Registration endpoint remains /api/ders-kayit/v21; ECRN/SCRN arrays preserve string values/order, with missing scrn becoming an empty array.
- Existing drop-result display is a TODO; do not invent an unverified response schema.
- The original root-output setup executable would collide with the setup/ source directory on macOS. Use build/bin/main and build/bin/setup.
- Configuration paths are relative to working directory. Keep computer-local timezone semantics and legacy millisecond aliases and credential fallbacks.
- --dry-run still performs earlier waits/authentication/preparation but skips final wait/submission. --test skips scheduled waits but sends unless paired with --dry-run. --local disables only clock sampling. --logs must remain useful with sensitive data removed.

## Implementation sequence

### 1. Build and transport foundation

- Configure macOS ARM64, deployment target 14, C++17, Apple Clang, and CMake 3.20+. Reject unsupported platforms/architectures.
- Remove Windows includes, APIs, linker libraries/pragmas, static MinGW flags, and executable suffix assumptions from owned code/build paths.
- Resolve libcurl headers/library only from the active Apple SDK and link CURL::libcurl; prevent accidental Homebrew discovery.
- Put executables in build/bin, retain vendored JSON, and add CTest support.
- Add RAII HttpSession with one persistent easy handle, global curl initialization, memory-only cookies, and prepare/perform separation. Prepared headers/body remain owned and preparation performs no I/O.
- HttpRequest carries GET/HEAD/POST, URL, headers, and body. HttpResponse carries status, final headers/body, and effective URL. Distinguish transport errors from HTTP status errors.
- Reset request options between transfers while retaining connections/cookies. Consume bodies completely and select final response headers after redirects.
- Enable cookies with CURLOPT_COOKIEFILE=""; no cookie jar. Verify TLS/hostname; production HTTPS only; ten redirects maximum; native redirect method behavior and cross-host Authorization protection.
- Timeouts: ten-second connection, thirty-second total; clock samples five-second total. No application-level registration retries.

### 2. Authentication, registration, and flags

- Preserve the exact authentication sequence, endpoints, form names/order, identity choice, and AJAX JWT headers.
- Resolve form actions against effective login URL without losing queries; use libcurl percent encoding, including UTF-8 and reserved bytes.
- Fail clearly for missing required fields, transport/HTTP failures, failed login, and invalid JWT response. Avoid a broad HTML parser rewrite.
- Retain existing registration payload and header semantics, replacing Windows platform metadata with macOS metadata.
- Preserve response message mappings and unknown-code fallback. Return nonzero for fatal transport/authentication/HTTP/malformed-response failures.
- Remove passwords, bearer tokens, cookies, session URL parameters, and authenticated raw-body dumps from diagnostics.
- Preserve credential precedence: primary process environment, primary .env, legacy process/environment-file values, then account configuration fallback.

### 3. Clock and scheduling

- Replace HINTERNET clock interface with HttpSession&. Preserve seven HEAD root attempts, 300 ms between attempts, Date +500 ms midpoint, lowest RTT with first-sample tie behavior, and zero-offset fallback.
- Parse dates using curl_getdate and reject failures. Measure RTT with steady_clock and calculate epoch midpoint from wall-clock start plus half RTT.
- Retain initial sampling, existing 90-second resync and 60-second token phases, final steady deadline, sleep thresholds, and final five-millisecond spin.
- Remove Windows timer/priority APIs. Best-effort USER_INITIATED QoS during final two seconds through submission, restoring previous QoS afterward; failure is not fatal.
- Keep local timezone, milliseconds and lead semantics; set tm_isdst=-1 in both target conversions to correct forced-standard-time behavior.

### 4. Terminal and setup

- Replace console input with scoped termios; preserve menu arrows/wraparound/Enter, layout, ANSI, and UTF-8.
- Password mode disables echo but retains line editing. Fail if echo cannot be disabled or interactive input lacks a TTY.
- Restore terminal on success, EOF, exceptions, SIGINT, SIGTERM using interruptible input and scoped signal handling.
- Replace Windows file operations with same-directory temporary file, checked write/fsync, atomic rename and failure cleanup. Preserve destination on pre-rename failure; credential mode 0600.
- Preserve path arguments, .env replacement/unrelated lines, date/time validation, CRN parsing, and config output.

### 5. Verification and distribution

- CTest with focused C++ tests plus Python stdlib HTTP/TLS/PTY fixtures; Python is development-only. Narrow internal endpoint/transport/time seams, no production CLI override or endpoint changes.
- CI on macOS 14 and 15 ARM64, asserting Apple Clang/architecture; offline tests on pushes/PRs. Build archive on 14 and smoke-test that same archive on 15.
- Release archives contain main, setup, data/example_config.json, README, and SHA-256 checksums; preserve executable permissions and normal linker signatures. No personal config/secrets/logs.
- Tag-triggered publishing only with appropriate release permissions; actual publication requires authorization.
- README: exact build/output paths, setup/archive usage, local timezone, safe dry-run commands, unsigned-distribution handling, awake-Mac requirement, timing uncertainty, and --test sends warning.
- Set example lead to zero; keep user-configured positive leads supported.

## Validation and acceptance

- Transport fixtures: method transitions, intermediate cookies and isolation, persistent connection reuse, redirect methods, headers/effective URL, chunking, timeouts/TLS failures, secret/header isolation.
- Auth fixtures: exact sequence with/without identity, ASP fields/actions, encoding, missing fields, rejected login, HTML/empty JWT, transport/HTTP failure.
- Payload fixtures: add-only, drop-only, mixed and missing SCRN; original JSON semantics and result messages.
- Clock tests: seven attempts, minimum RTT/ties, invalid dates, all-failure fallback, offset/lead/milliseconds, past deadlines, DST, wall-clock changes after monotonic deadline creation.
- Flag orchestration: all flags/combinations; zero submissions for every dry-run. Registration-capable automated cases must use loopback fixtures only.
- PTY/files: hidden password bytes, restoration including interrupts, no-TTY failure, menu behavior, .env round-trip/duplicates/unrelated lines, permissions and failed atomic writes.
- Logs must omit synthetic secrets and sensitive response data. TLS fixtures use test-only CA trust, never weakened production TLS.
- Native acceptance: configure/build with Apple Clang; ARM64/deployment target/system-only dynamic linkage; no required Windows dependencies; actual terminal setup and timing smoke checks.
- Live gate on an authorized Mac/account: --test --dry-run --local for JWT, then --test --dry-run for sampling. Keep secrets and traffic local. No real course registration without explicit authorization.
- If live authentication is unavailable, state that clearly; offline fixtures do not prove OBS compatibility. Full completion requires native checks and the live JWT gate.

## Resume and closeout

The original environment blocker was resolved and implementation completed. Remaining acceptance is now owned by `../active/macos-port-verification.md`; resume there rather than restarting this implementation sequence. This archived plan preserves design decisions and prior evidence. Git mutations remain unauthorized.

## Implementation milestone (2026-09-27)

- Added SDK-only libcurl HttpSession and native ARM64/macOS14 CMake. CMake remains project-local and ignored; no tool/library downloads or global installs.
- Ported persistent OBS auth, request preparation/submission, seven-sample clock/monotonic wait and scoped QoS, termios setup and atomic 0600 file writes.
- Kept exact OBS endpoints/field order/payload semantics; diagnostics omit credentials/tokens/session URLs/raw response bodies. Test-only HTTP sessions restrict sockets to loopback; production HTTPS-only.
- Added CTest suites for transport/TLS, 14 auth scenarios, core/clock, all16 flag combinations plus8 future scheduling cases, setup files and PTY behavior. Initial all-six run passed (16.85s); expanded core/application suites passed separately (27.56s).
- Independent bounded reviews found/fixed QoS lifetime extending through the exit prompt; added future-time scheduling and credential fallback/roundtrip tests. Auth/transport review found no additional concrete defects; compiler initializer warnings fixed.
- Native inspection passed ARM64-only binaries, macOS14 deployment target, system-only dynamic linkage, valid linker signatures, safe missing-config/nonTTY startup.
- Updated README, example lead=0, Git ignores, allowlisted archive/checksum script, macOS14/15 ARM64 CI and same-archive cross-version smoke. Official GitHub runner reference checked for current label architecture.
- At this intermediate milestone, final combined tests and archive smoke were still pending; both passed in the final validation below. No local .env or data/config.json exists, so live JWT acquisition/real OBS cookies and redirects are unverified. No live request sent. macOS14/15 execution requires CI; local host is macOS26.6.2.
- Added a narrow wait-loop clock seam and runtime tests with forward/backward wall-clock jumps; the steady deadline remains unchanged.

## Final local validation and handoff (2026-09-27)

Implementation is complete for local/offline scope. Live JWT and cross-version acceptance remain outstanding and have been transferred to the active verification plan.

- Final Release configure/build passed with project-local CMake 4.4.3/Apple Clang21; CMake selected an already installed Python3.14 interpreter for CTest (manual fixture tooling also ran with existing Python3.12). No installation occurred. Development Python minimum explicitly 3.12 for archive extraction API.
- Final `ctest --test-dir build --output-on-failure --parallel 3`: all6 suites passed, 27.33s. Covers transport/TLS,14 authentication fixtures,16 flag combinations plus8 future scheduling cases, account/env compatibility, clock sampling/DST/deadlines/wall jumps, setup file/PTY paths. Evidence: `build/Testing/Temporary/LastTest.log` (synthetic fixture data only).
- `python3 scripts/package.py` generated `dist/itu-ders-bot-macos-arm64.tar.gz` and adjacent checksum. `python3 tests/native_artifacts.py dist/itu-ders-bot-macos-arm64.tar.gz` and `shasum -a 256 -c ...sha256` passed. Archive payload allowlist/checksums, executable modes, ARM64, minimum macOS14, system-only libraries, linker signatures, missing-config and nonTTY smoke checked.
- `git diff --check` passed; owned source/build/workflow scans found no required Windows API dependencies. Pre-existing untracked AGENTS.md retained; .DS_Store and local CMake now ignored. No Git mutations performed.
- Binaries: `build/bin/main`, `build/bin/setup`. README documents project-local CMake, safe dry-runs, real-send flag semantics, local timezone/timing limits, unsigned distribution and packaging.
- Cannot run live JWT acceptance because no local credentials/config are present. User next step: run `./build/bin/setup` locally; then authorized checks `./build/bin/main --test --dry-run --local` and `./build/bin/main --test --dry-run`. Never send a real registration request without explicit permission.
- CI definition includes macOS14/15 ARM64 and macOS14-built archive smoke on15, but workflow has not been run/pushed. Local host26.6.2 establishes native build behavior, not actual14/15 runtime compatibility.
- No live OBS calls, global/project dependency installations, Git mutations, or release publication occurred.
