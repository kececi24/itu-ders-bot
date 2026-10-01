# Unified Windows, macOS, and Linux port

Status: ACTIVE
Created: 2026-09-30
Last updated: 2026-10-01 22:50 Europe/Istanbul
Owner: primary agent
Primary scope: shared C++ core, native adapters, tests, bootstrap, packaging, CI, README and AGENTS

## Purpose / Big Picture

Created 2026-09-30 from the approved comprehensive plan. Implement one codebase/version and one release containing Windows x64, macOS ARM64 and Linux x64 archives. The user-authorized Docker campaign verified Linux, reproduced and fixed CP-31, and added a manifest-path regression. CP-32 (provenance bound to both executables with link receipts, schema 2 validation, and staged-copy checks) and CP-33 (ACL error handling, mandatory test setup, and 12-mode fault injection) are fully implemented, hardened, and verified with all 10 CTest suites on macOS ARM64 and all 9 CTest suites in Linux Docker, accompanied by native packaging and artifact verification. Windows testing belongs to the teammate; this plan remains ACTIVE awaiting Windows acceptance evidence without claiming physical Linux x64 or Windows acceptance.

## Highest Priority — Open Implementation Defects

All identified implementation defects and review findings through CP-33 are RESOLVED in source and verified in integrated test suites on macOS and Linux.

- [x] **P2 / CP-32 — Bind provenance to both successfully built executables.** FIXED; PASS. Implemented in `cmake/BuildProvenance.cmake`, `cmake/ProvenanceInputs.cmake`, `cmake/BinaryReceipt.cmake`, `scripts/provenance.py`, `tests/build_provenance_tests.py`, `CMakeLists.txt`, `cmake/GenerateManifest.cmake`, and `scripts/package.py`. The manifest is invalidated at build start (`provenance_start`), per-target binary receipts (`main-$<CONFIG>.sha256`, `setup-$<CONFIG>.sha256`) are generated POST_BUILD on successful link, and `build_manifest` generates `bin/build-manifest.json` (schema_version 2) only after both `main` and `setup` succeed. `package.py` and `provenance.py` validate source (excluding dotfiles/`.DS_Store`), build, external (resolving absolute paths), and binary hashes, plus staged copies and release tags. Hardened against path spaces in external dependency lists. Verified across all 10 macOS CTest suites, all 9 Linux CTest suites, and packaging on both platforms. PASS.
- [x] **P2 / CP-33 — Make ACL verification and its regression checks reject errors.** FIXED; PASS. Implemented in `src/platform_posix.cpp`, `tests/test_helpers.hpp`, `tests/setup_file_tests.cpp`, and `tests/setup_acl_error_tests.cpp`. `atomic_write_private` validates empty extended ACLs via `acl_valid` and Darwin's `result == -1 && errno == EINVAL`, resetting `errno = 0` and failing before credential write on any entry or unexpected errno. `test_helpers::private_file` rejects unexpected ACL read errors (`errno != ENOENT`). `tests/setup_file_tests.cpp` requires `posix_spawn` chmod setup to succeed and asserts inherited ACL presence with a control file. Verified in direct macOS runs and integrated CTest (`setup_acl_errors` and `setup_files` suites). PASS.

CP-31 is FIXED: whole-argument quoting and `VERBATIM` replace embedded quotes. An actual Linux build first reproduced exit2; `manifest_paths` now builds the real target with source/build spaces and verifies revision/dependency metadata on Linux and macOS. CP-13's original exposure remains fixed in tested macOS paths. Windows source fixes and the new Windows regression still need teammate execution.

## Scope

### In scope and support constraints

- Latest ownership: prioritize Linux/macOS implementation and verification here. The user's teammate owns Windows native build/tests/archive and Windows10/11 desktop acceptance. Keep shared changes portable and incorporate their results when provided; do not block Linux/macOS work on unavailable Windows execution.
- Windows 10/11 x64 with MSVC2022; macOS ARM64/AppleClang with deployment target14+; Ubuntu22.04/24.04 x64 GCC11+. Other compatible glibc distributions are best effort. IntelMac, other CPU targets, musl and32bit are outside scope.
- Preserve endpoints, OBS authentication order, cookies/redirects/session reuse, JWT, one ordered ECRN/SCRN batch, config/env precedence and schema, seven lowest-RTT clock samples, target scheduling and four existing flags. Preserve30second timeout, unknown-outcome guidance and no automatic retry.
- Tools, dependency downloads, sources, builds, installations and caches stay below ignored project `.deps/` (existing local CMake may be reused). The user explicitly authorized Docker image storage outside the project. Reuse the development image's compiler; no host-global installs or compiler/SDK installation.
- Git is read-only for this review. Historical 2026-09-30 checkpoint authorization is not permission for another commit, branch/tag, push, publication or workflow invocation. Offline loopback fixtures only; no live OBS calls or real registration. Do not inspect `.env` or personal config.
- User-deleted `data/example_config.json` stays deleted. Add a new sanitized packaging template with empty courses and zero lead; never derive it from personal configuration.
- macOS14/15 runtime checks remain user-deferred. Existing user-reported macOS dry-run/submission and HTTP200 capacity-rejection evidence is retained, not invented for other platforms.

### Out of scope

Host-global installs; installing a compiler/SDK; TLS bypass; inspecting personal credentials/config; live OBS/real registration; automatic submission retries; Git mutations, releases and remote workflow invocation. Authorized Docker images are the explicit storage exception. Intel Mac, additional CPUs, musl and 32-bit support are excluded. macOS14/15 runtime acceptance remains user-deferred.

## Repository Orientation

- Shared flow: `src/main.cpp`, `src/token.cpp`, `src/clock.cpp`, `src/http.cpp`.
- Native operations: `include/platform.hpp`, `src/platform_posix.cpp`, `src/platform_windows.cpp`; CMake selects one adapter. Test-only socket adapters compile only into `itu_core_test`.
- Setup: `setup/main.cpp`, `include/console.hpp`; credentials/config are relative to the working directory or explicit UTF8 paths.
- Build: `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `cmake/dependencies.lock.json`.
- Bootstrap/package/release hashes: `scripts/bootstrap.py`, `scripts/package.py`, `scripts/release_checksums.py`; `.github/workflows/release.yml` defines all native/manual CI gates.
- Sanitized source: `packaging/example_config.json`; package destination `data/example_config.json`. Do not restore the deleted source file.

## Invariants and Acceptance Criteria

Implementation and available offline/native checks must pass; record external native/manual gaps without implying a verified release. Preserve these architecture decisions and security invariants:

1. Shared registration/authentication/clock/HTTP core. OS implementations selected by CMake live in platform adapters; shared UI consumes semantic menu keys and UTF8 lines. Native APIs/OS headers stay in adapters and entry boundaries.
2. POSIX terminal behavior is retained. Windows uses native Unicode console input, cooked line/password editing, hidden input before prompt, cooperative CtrlC/Break cancellation and restoration. Scoped output/codepage modes restore. Redirected interactive setup fails explicitly.
3. Windows argv/environment are decoded UTF16 toUTF8; paths use filesystem UTF8 conversion. Environment preserves unset versus empty semantics. All three systems use the same JSON/env data.
4. Private atomic file write: same-directory0600/fsync/rename on POSIX; owner-restricted CREATE_NEW/DACL, checked WriteFile/FlushFileBuffers and same-volume replacement retaining temporary ACL on Windows. Failures preserve original and remove temporary; unsupported permission enforcement fails.
5. Shared monotonic deadline computation; scoped MacQoS, Windows best-effort thread priority/timer resolution, Linux normal priority. ARM yield/x64pause are adapter operations; restore after transfer. Local-time DST semantics retained.
6. libcurl on every OS, HTTPS/TLS verification retained. Mac SDK/systemcurl; Windows project-local static curl/Schannel/staticMSVC runtime; Linux static curl/OpenSSL with dynamicglibc and system UbuntuCA bundle. Pin zlib/nghttp2 too for compressedHTTP2 responses. API floor curl7.85, bundled supported curl8.x.
7. CMake3.25+/C++17, compiler-specific flags, platform presets, explicit dependency prefix, consistent bin/staging targets. BUILD_TESTING=OFF has no Python requirement. Bootstrap verified source archives from a committed exact URL/version/SHA256 lock; never Git package checkouts/global caches.
8. Three archives share version/revision: windows-x64.zip, macos-arm64.tar.gz, linux-x64.tar.gz. Allowlist main/setup, README, sanitized example, licenses, build manifest and payload checksums; outer archive hashes. Run from extracted root. No installer/updater/signing service in this iteration.
9. Native CI builds/tests all three. Ubuntu22 builds and Ubuntu24 smokes identical archive. Tag publishing aggregates all verified assets into one release only after all jobs pass. CI WindowsServer evidence is distinguished from Windows10/11 desktop acceptance.

## Milestones

1. [x] Complete and verify CP-31, CP-32, and CP-33 across shared core and native platform adapters.
2. [x] Verify corrected macOS build, all 10 CTest suites (including setup_acl_errors and build_provenance), production artifacts, and schema 2 packaging.
3. [x] Verify the corrected revision on Ubuntu 22.04 x64 in Docker (9 CTest suites), smoke identical archive on Ubuntu 24.04, and verify schema 2 packaging.
4. [ ] Keep Windows execution with the teammate, reconcile their results, and complete authorized combined-release acceptance separately.

## Progress

- [x] 2026-10-01 22:50 — Implement and verify CP-32 and CP-33. All 10 CTest suites on macOS ARM64 (54.77s), all 9 CTest suites on Linux Docker AMD64 (52.72s), and native packaging with schema 2 provenance PASS.
- [x] 2026-10-01 21:10 — Authorized Docker preflight/bootstrap and baseline Linux 7/7 PASS; real space-path build reproduced CP-31. Fixed quoting and added regression; patched Linux 8/8 (29.47s), macOS 8/8 (26.78s), native artifact checks, Ubuntu22/24 identical-archive verification and macOS extracted archive PASS. AMD64 container execution is emulated on Apple Silicon.

Primary owns Linux/macOS integration, validation, planning and evidence. Windows execution is teammate-owned. Downloaded tools/dependencies and logs are ignored project-local artifacts. Earlier review-only milestones below are historical, not current restrictions or proof of full acceptance.

- [x] Read planning/instructions/manifests and bounded native preflight.
- [x] 2026-10-01 15:34 — Review clean `e1aea07`; fresh macOS configure/build, 7/7 suites in 28.28s, native binary checks and actual archive PASS. Native ACL creation/replacement and selected failure probes PASS; manifest edge-case failures reproduced.
- [x] 2026-10-01 21:10 — Latest Linux source plus CP-31 patch and same-archive Ubuntu24 smoke — PASS in emulated AMD64 containers; the 15:34 environment blocker is resolved.
- [ ] 2026-10-01 15:34 — Receive teammate Windows native/desktop results — TEAMMATE OWNED; NOT RUN here.
- [x] 2026-10-01 — Review current clean source at `dcf4d04b0af4b6e0dceaf15ce7ef7a4082ca05a8`; fresh macOS configure/build, seven offline suites and native binary/startup checks — PASS. Review found open defects despite this result.
- [x] 2026-10-01 — Fix highest-priority CP-13 and CP-14 — FIXED; native reproduction PASS for CP-13, transitive link added for CP-14.
- [x] 2026-10-01 — Source changes for CP-15–CP-19 were added; macOS suites passed; CP-16 resolved by CP-32.
- [x] Add platform interfaces/adapters and keep native macOS regression baseline. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Implement Windows terminal/Unicode/files/timing and portable loopback sockets. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Implement Linux POSIX reuse, dependency recipe and native build rules. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Port existing fixtures; Windows console/DACL coverage; deterministic test-only PEM fixtures. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Pin and implement project-local dependency bootstrap and presets. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Unify allowlisted packages/checksums/manifests/licenses and one-release CI. — SOURCE COMPLETE; Linux packaging and static workflow checks PASS; remote CI execution pending.
- [x] Run affected then integrated local native checks, archive clean-directory smoke and inspect dependency locality. — PASS on Ubuntu22 WSL and macOS ARM64; seven CTest suites and sanitized archives verified.
- [x] Check production test-seam exclusion and actual unsupported private-permission filesystem refusal. — PASS; `build/preflight/properties.log` on Linux and `nm` control on macOS.
- [x] Configure/build/test/archive on macOS with its project-local CMake — PASS; all 7 CTest suites, artifact integrity, and sanitized archive verified.
- [ ] Native Windows MSVC build/CTest/archive and Windows10/11 desktop acceptance — TEAMMATE OWNED; no native result recorded for this revision.
- [ ] Execute defined remote native/newer-OS CI and manual/live gates after appropriate authorization — NOT RUN.
- [x] Update README/AGENTS/support matrix and actual bootstrap/package/preset commands. — SOURCE COMPLETE; docs name unverified native/manual gates.

## Explicitly Uncompleted Work

1. **Execution limits:** current Ubuntu22/24 container acceptance passes, including Linux PTY and real ELF/ABI checks. Execution uses AMD64 emulation on ARM64 Docker Desktop, not physical Linux x64. Physical native timing/host behavior remains unproven.
2. **Windows handoff to teammate:** native MSVC2022 x64 dependency/bootstrap/build/CTest/PE/archive checks and Windows10/11 terminal acceptance. Verify successful real `setup.exe` Unicode credential/config paths and process-level cancellation/restoration, not only adapter calls and nonzero child exits. Current source changes provide some child coverage but do not prove every successful setup path.
3. **Deferred/external acceptance:** macOS14/15 runtime remains deferred. Remote native/newer-OS workflows and combined release verification require authorization; publication remains outside this review. Teammate ownership does not waive Windows evidence for the eventual three-platform release.
4. **Live acceptance:** account-owner dry-runs on newly supported platforms when authorized. Keep earlier macOS user evidence; no additional real registration is needed.

## Baseline / Starting Evidence (2026-09-30)

Native host DarwinARM64; localCMake4.4.3 at `cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake`; AppleClang21; GNUmake3.81; SDK `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk`; Python3.12.3 with stdlib HTTP/TLS/PTY/archive modules; loopback bind and SDKcurl compile/link/run passed. Scratch evidence in ignored `build/preflight/`. PLANS.md absent. No credentials/service connection needed. Windows/Linux native runners are not available on this Mac; native execution is a CI/manual gate, not claimed as passing.

## Validation Coverage

Retain the original six suites covering transport TLS/cookies/redirects/reuse/loopback restriction, synthetic14case authentication, payload order and clock/DST/walljumps,16flag/eightfuture orchestration, secure setup files, terminal secrecy/restoration. Port platform assumptions with actual semantics; use explicit UTF8 subprocess decoding and native Windows console/ACL checks. Test applied-then-stalled and partial responses with exactly one attempt and unknown-outcome advice. Test-only CA/socket options cannot enter production binaries.

Verify PE/MachO/ELF architecture, system-only expected linkage, no developer curl/VCruntime dependency, Mac min14/linker signatures, LinuxUbuntu22glibc baseline, safe archives/checksums/manifests and missingconfig/nonTTY startup before network. PEM fixtures remain local tests, never truststore-installed or packaged, and have checked SAN/expiry. The seventh CTest suite, `artifact_integrity`, adds eight archive contract tests that reject tampering, personal configuration, missing/unlisted payloads, unsafe paths and symlinks before native execution. Its native-smoke boundary is mocked only for opaque contract fixtures; the real Linux binary/archive gates separately execute actual binaries.

Done means implementation and locally available verification are complete, three native jobs are defined, generated archives are reviewable, dependencies are confined to the project, one-release aggregation is ready, and residual native/manual/live verification is accurately recorded. No successful-native-release claim until all native runners execute. Live dry-run on new platforms requires account-owner action; real registration is unnecessary for acceptance.

## Historical Decisions and Evidence

- 2026-09-30: Existing macOS port edits are uncommitted and preserved. Prior verification plan closed as superseded, with incomplete/deferred gates transferred here. Latest HTTP200 normal rejection is user evidence only; previous timeout cause remains unknown.
- 2026-09-30: Native dependencies usable, no installs. Parallel implementation ownership assigned. Explicit cross-platform request supersedes old macOS-only repository policy; update it with this support matrix.

## Surprises & Discoveries

- Continuation started in a clean detached worktree at main `53008df`. The integrated sources and plans existed at `cross-platform` `7780a30`. A user-authorized, sandbox-approved switch attached this worktree to the existing branch. The primary checkout is also on that branch. The user subsequently authorized the local continuation checkpoint commit on 2026-09-30; no push or other remote action was authorized.
- During the earlier 2026-09-30 Windows/WSL continuation, the host was Windows x64. Windows had CMake 4.2.3 and Python 3.13.11 but no available MSVC2022 `cl`; bootstrap preflight stopped before downloads. The 2026-10-01 review is on macOS ARM64; that historical host statement does not describe the current environment.
- Existing WSL Ubuntu 22.04 has GCC 11.4.0, make, Perl, CA bundle, and readelf. Its CMake 3.22.1/Python 3.10.12 are too old. Verified CMake 3.31.6 and standalone Python 3.12.14 were extracted only into `.deps/tools/linux-x64`; URLs/hashes are in that directory's `provenance.json`.
- The checked-in CI was still macOS-only and used obsolete artifact/package commands. It now defines three native targets, each extracted archive check, newer-OS smoke gates, and one-release aggregation. It has not been invoked.


- All four dependency archives were retrieved with default TLS verification and independently matched committed hashes. zlib/nghttp2 built first. OpenSSL initially failed on unescaped absolute Makefile prerequisites containing the checkout's spaces; generated prerequisites are now relativized while verified upstream source archives remain unchanged. The complete bootstrap then PASSed.
- The integrated Linux build PASSed. Initial full CTest had one failure: `application_fixture.py` expected macOS in `sec-ch-ua-platform` on Linux. It now expects the native host's platform; affected application fixture and complete suite then PASSed.
- On this NTFS-backed WSL mount, chmod returns success but mode0600 is not enforced. The built setup refused a synthetic replacement, preserved the old file and removed its temporary. On Linux temporary storage, existing setup-file/PTY tests passed private mode checks.

## Decision Log

- 2026-10-01 21:10 — After the user requested continued testing/debugging, fix CP-31 reproduced by the real Linux manifest target. Add one cross-platform CTest regression using the real project/preset, with source/build spaces on POSIX and build spaces on Windows. Do not silently close the independent CP-32/CP-33 findings. Run Linux tests as UID1000 with external networking disconnected; preserve emulation limits.

- 2026-10-01 — User explicitly authorized necessary images and containers, superseding the Docker storage blocker below. Docker-managed images may live outside the project; dependency bootstrap/tools remain project-local. Use a prebuilt Ubuntu22 development image with its compiler, and Ubuntu24 for identical-archive smoke. Record emulation limits and do not claim physical x64 or live-service acceptance.

- 2026-09-30 — Continue Linux execution using existing WSL GCC and project-local verified tools; stop Windows-dependent builds at missing compiler and keep macOS execution pending. No compiler/SDK/global installs or TLS bypass.
- 2026-09-30 — Native archive verification now checks the adjacent outer checksum, the exact license allowlist, and sanitized example contents in addition to payload hashes and startup.

- 2026-09-30 — Keep generated OpenSSL Makefile source prerequisites relative to the local build, to support checkout paths containing spaces without relocating dependencies or altering archive hashes.
- 2026-09-30 — Verify POSIX file mode/owner after chmod before writing credentials; reject unsupported enforcement rather than reporting a private-file success.
- 2026-09-30 — Windows curl requires `NGHTTP2_USE_STATIC_LIBS=ON` so headers use static linkage rather than DLL imports; source fixed, native MSVC gate pending.
- 2026-09-30 — Resolve symlinks in `ITU_APPLE_SDK` in `cmake/Dependencies.cmake` with `file(REAL_PATH)` so `cmake_path(IS_PREFIX ...)` correctly verifies curl headers and library stay inside the active Apple SDK.
- 2026-09-30 — Check both STDIN and STDOUT in `is_terminal()` so non-interactive redirected output does not block indefinitely waiting for user Enter key input.
- 2026-09-30 — Prevent `TimingGuard::activate()` on macOS from demoting threads already at or above `QOS_CLASS_USER_INITIATED` (such as `QOS_CLASS_USER_INTERACTIVE`), and correctly elevate unspecified QoS threads.
- 2026-09-30 — Strip trailing `\r` in POSIX `read_line()` upon newline and EOF for CRLF safety and parity with Windows.
- 2026-09-30 — Check for dependency manifest existence before invoking `file(REAL_PATH)` to prevent CMake author warnings when dependencies are not yet bootstrapped.
- 2026-10-01 — Review only and persist prioritized findings as requested; do not silently implement fixes. Git remains read-only for this task, regardless of historical checkpoint authorization.
- 2026-10-01 — Distinguish actual native reproduction (CP-13), source-confirmed Windows linkage (CP-14), mock gate reproductions (CP-15/CP-17), and unexecuted Windows coverage. Reopen acceptance where the passing suites did not enforce the intended property.
- 2026-10-01 15:34 — Prioritize Linux/macOS corrections; assign Windows execution/desktop acceptance to the user's teammate. Remove blanket native-PASS claims where only source or mocked tests were observed.
- 2026-10-01 15:34 — Reuse verified Mac tools. Docker initially failed at a missing daemon socket; after user-start remediation, rechecked only Docker and found the engine available but no images. Do not download outside the project or install missing compilers to resolve this without explicit authorization.

## Defects / Findings Ledger

| ID | Component | Status | Evidence / disposition |
|---|---|---|---|
| **CP-31 / P1** | **Manifest command in space-containing paths** | **FIXED; Linux/macOS PASS** | Real Linux configure succeeded but manifest build failed with literal backslashes, exit2 (`.deps/linux-verification/logs/space-path.log`). Whole-argument quoting plus VERBATIM fixes it. New `tests/manifest_path_tests.py` configures the real preset and builds the real manifest target with spaces, checking dependency/compiler/revision metadata; Linux/macOS targeted and full suites pass. Windows execution remains teammate-owned. |
| **CP-32 / P2** | **Provenance precedes binary success** | **FIXED; PASS** | Invalidation of manifest at build start (`provenance_start`), link-time receipts (`main-$<CONFIG>.sha256`, `setup-$<CONFIG>.sha256`), and `build-manifest.json` generation (schema 2) only after both `main` and `setup` link successfully. `provenance.py` and `package.py` reject stale, partial, failed, and replaced binaries. Passes `build_provenance_tests.py`, all 10 macOS CTest suites, all 9 Linux CTest suites, and packaging on both platforms. |
| **CP-33 / P2** | **ACL error handling and regression enforcement** | **FIXED; PASS** | `src/platform_posix.cpp` checks `acl_valid` and Darwin's `result == -1 && errno == EINVAL` for empty extended ACLs, resetting `errno = 0` and failing before writing credentials on any entry or unexpected errno. `test_helpers.hpp` rejects non-ENOENT read errors. `setup_file_tests.cpp` requires `posix_spawn` chmod to succeed and uses a control file to verify ACL inheritance. Passes 12-mode fault injection in `setup_acl_error_tests.cpp` and all CTest suites. |
| **CP-13 / P1** | **Private credential-file ACLs** | **FIXED; PASS** | `src/platform_posix.cpp`: inherited extended ACLs stripped with `acl_init(0)`/`acl_set_fd` and verified with `acl_get_fd` before writing credentials. Regression tests in `tests/setup_file_tests.cpp` and `tests/test_helpers.hpp` confirm creation, replacement, failure preservation, and temporary cleanup. |
| **CP-14 / P1** | **Windows static curl link** | **FIXED; SOURCE CONFIRMED** | `cmake/Dependencies.cmake`: added required `iphlpapi` library to `itu_curl` interface link libraries on WIN32, restoring transitive linkage matching upstream curl. Native MSVC link remains pending Windows environment. |
| **CP-15 / P2** | **Release revision consistency** | **FIXED; PASS** | `scripts/release_checksums.py`: inspects `build-manifest.json` across archives, validating version, target, and ensuring all release archives share an identical non-empty git revision. Regression tests in `tests/artifact_integrity_tests.py` PASS. |
| **CP-16 / P2** | **Build provenance freshness** | **FIXED; PASS** | Resolved by CP-31 (path quoting) and CP-32 (link receipts and schema 2 provenance validation). |
| **CP-17 / P2** | **Linux C++ runtime compatibility gate** | **FIXED; real ELF/archive and mocked boundaries PASS** | Numeric GLIBCXX<=3.4.30/CXXABI<=1.3.13 and GLIBC<=2.35 checks passed on actual GCC11 Ubuntu22 binaries and their identical archive on Ubuntu24. Container startup is emulated AMD64; no physical x64 runtime claim. |
| **CP-18 / P2** | **Windows setup integration/cancellation tests** | **PARTIAL; teammate execution/coverage pending** | Source adds actual child nonterminal/cancellation checks and in-process prompt synchronization. Successful setup child credential/config/Unicode flows are not demonstrated. Current child cancellation still waits200ms and asserts nonzero exit without proving readiness/restoration. Teammate should close these properties on Windows. |
| **CP-19 / P3** | **Windows menu repeated-key events** | **SOURCE FIXED; native NOT RUN here** | Directional repeat buffering and a native test are present. Execution belongs to teammate; macOS CTest does not compile/run this Windows-only target. |
| **CP-20 / P1** | **macOS dependency manifest tracking** | **FIXED; PASS** | `CMakeLists.txt`: writes `${CMAKE_BINARY_DIR}/dependency-manifest.json` from `ITU_DEPENDENCY_MANIFEST`, ensuring macOS build manifest correctly includes Apple SDK curl dependency rather than empty `{}`. PASS. |
| **CP-21 / P2** | **Windows console child process assertions** | **FIXED; SOURCE CONFIRMED** | `tests/windows_console_tests.cpp`: converted silent `if (CreateProcessW)` conditionals to hard assertions; added child wait timeout checks with process termination; ensured `STILL_ACTIVE` does not falsely pass. |
| **CP-22 / P2** | **Windows on_control inactive signal swallowing** | **FIXED; SOURCE CONFIRMED** | `src/platform_windows.cpp`: `on_control` now returns `handled` (FALSE when `reader_active` is false) so control signals during teardown or between readers are handled by subsequent or default handlers. |
| **CP-23 / P3** | **Windows menu repeat key restriction** | **FIXED; SOURCE CONFIRMED** | `src/platform_windows.cpp`: restricted `MenuInput::read()` key repeat buffering strictly to directional keys (`up`, `down`), preventing duplicate `enter` actions. |
| **CP-24 / P2** | **Linux PTY test EIO crash on child close** | **FIXED; Linux/macOS PASS** | Fresh Linux `setup_terminal` passed as UID1000 (0.54s after CP-31); macOS passed too. EIO/empty-read handling preserves restoration assertions and propagates unrelated errors. Linux execution was in emulated AMD64 containers. |
| **CP-25 / P3** | **Setup main argv nullptr sentinel** | **FIXED; PASS** | `setup/main.cpp`: added `pointers.push_back(nullptr)` for standard C/C++ `argv[argc] == nullptr` compliance. |
| **CP-26 / P2** | **Release checksums missing file and Git SHA-256 support** | **FIXED; PASS** | `scripts/release_checksums.py`: added explicit check for missing `.sha256` files with clean `SystemExit`; updated revision regex to `r"([0-9a-f]{40}|[0-9a-f]{64})"` in `package.py` and `release_checksums.py`. |
| **CP-27 / P3** | **Uppercase hex HTML entity decoding** | **FIXED; PASS** | `src/token.cpp`: updated `decode_html` regex to `#[xX][0-9a-fA-F]+` and base 16 parsing for uppercase `&#X...;` entities. Regression test in `tests/auth_fixture.py` PASS. |
| **CP-28 / P2** | **POSIX private file owner UID verification** | **FIXED; PASS** | `tests/test_helpers.hpp`: added `&& info.st_uid == geteuid()` to `private_file()` on POSIX, matching Windows DACL owner SID assertions. PASS. |
| **CP-29 / P2** | **Artifact integrity unit test discovery and contract coverage** | **FIXED; PASS** | `tests/artifact_integrity_tests.py`: added `tests/` to `sys.path`; added 4 new unit tests covering dirty manifests, invalid revisions, missing checksum files, and SHA-256 hashes (15/15 passed). PASS. |
| **CP-30 / P3** | **POSIX redundant fsync on write failure** | **FIXED; PASS** | `src/platform_posix.cpp`: guarded `fsync(fd)` with `if (written)` in `atomic_write_private` to skip redundant sync when writes or permission checks fail. PASS. |
| CP-01 | CI | SOURCE FIXED; NOT RUN | macOS-only matrix and obsolete package arguments replaced; workflow YAML/matrices/release dependencies and bash syntax PASS, remote execution NOT RUN. |
| CP-02 | README / AGENTS | SOURCE FIXED | Replaced macOS-only policy/build/release assumptions with the supported matrix and pending-evidence distinction. |
| CP-03 | Native archive checker | FIXED; PASS | Outer checksum, exact licenses, empty-course/zero-lead checks; eight contract tests and actual Linux archive gate PASS. |
| CP-04 | Linux OpenSSL bootstrap | FIXED; PASS | `make` initially split `/mnt/.../itu ders bot`; generated source prerequisites made relative; complete rebuild/bootstrap PASS, evidence `build/preflight/bootstrap.log`. |
| CP-05 | Windows static nghttp2 recipe | SOURCE FIXED; NOT RUN | Upstream curl finder applies `NGHTTP2_STATICLIB` only when `NGHTTP2_USE_STATIC_LIBS=ON`; set it for Windows. MSVC unavailable. |
| CP-06 | POSIX private-file mode enforcement | FIXED; PASS | Mode/owner enforcement verified. Inherited ACL removal and strict error handling verified on macOS (CP-13, CP-33). |
| CP-07 | Application fixture platform header | FIXED; PASS | Hardcoded macOS assertion replaced with native host expectation; affected fixture and full CTest PASS. |
| CP-08 | Apple SDK symlink resolution | FIXED; PASS | `xcrun` returned symlink `MacOSX26.5.sdk -> MacOSX.sdk`; resolved with `file(REAL_PATH)` in `cmake/Dependencies.cmake`; configure succeeded. |
| CP-09 | Non-interactive terminal detection | FIXED; PASS | `is_terminal()` previously checked only STDIN; in piped test runners stdin remained a TTY while stdout was piped, causing `std::cin.get()` to hang; checking both STDIN and STDOUT fixed it. |
| CP-10 | macOS QoS demotion & unspecified elevation | FIXED; PASS | `TimingGuard::activate()` on macOS previously demoted `QOS_CLASS_USER_INTERACTIVE` (33) to `QOS_CLASS_USER_INITIATED` (25) and skipped `QOS_CLASS_UNSPECIFIED` (0); fixed to only elevate if `current < QOS_CLASS_USER_INITIATED`; verified in `core_tests`. |
| CP-11 | POSIX read_line trailing CR preservation | FIXED; PASS | POSIX `read_line()` previously pushed `\r` into string on CRLF, corrupting passwords; added trailing `\r` stripping on newline and EOF matching Windows behavior. |
| CP-12 | Dependencies.cmake unbootstrapped warning | FIXED; PASS | Calling `file(REAL_PATH)` on non-existent `.deps` caused CMake author warnings; guarded with `NOT EXISTS` check before resolving. |

## Validation Plan and Results

### Current Consolidated Validation Status

| Gate | Target / Method | Status | Evidence / Limits |
|---|---|---|---|
| Native Mac toolchain & SDK preflight | `python3 scripts/bootstrap.py --target macos-arm64 --cmake <local cmake>` | PASS | Apple Clang 21.0.0, macOS 26.6 SDK, system/SDK libcurl 8.7.1; no downloads needed. |
| Linux toolchain & dependency locality | Checksum-verified tools & archives; `scripts/bootstrap.py --target linux-x64` | PASS | `.deps/tools/linux-x64/provenance.json`; curl 8.22.0, zlib 1.3.2, nghttp2 1.70.0, OpenSSL 3.5.9 static in `.deps/linux-x64/install`. |
| Linux baseline build & 8/8 CTest (Docker AMD64) | `ctest --preset linux-x64` in Ubuntu 22 container | PASS (Historical CP-31 baseline) | 8/8 suites passed (29.47s) at 21:10; UID 1000, external network disconnected. Emulated AMD64 on Apple Silicon. |
| Linux native binary inspection & startup | `python3 tests/native_artifacts.py build/linux-review-20261001/bin --target linux-x64` | PASS | ELF x64, allowed system dependencies, no RPATH, GLIBC<=2.35, GLIBCXX<=3.4.30, CXXABI<=1.3.13; clean-directory startup. |
| Linux production seam isolation | `nm -C` production main vs transport-test positive control | PASS | Production lacks `TestOptions` and `itu_open_loopback`; transport test contains both. |
| Linux sanitized package & Ubuntu 22/24 archive | `package.py` and `native_artifacts.py` on Ubuntu 22 and identical archive on Ubuntu 24.04 | PASS | Ubuntu 22 and Ubuntu 24 smoke PASS; valid checksums, sanitized contents, actual ELF/ABI startup. |
| macOS baseline build & 8/8 CTest | `cmake --build --preset macos-arm64` && `ctest --preset macos-arm64` | PASS (Historical CP-31 baseline) | 8/8 suites passed (26.78s) at 21:10. |
| macOS native binary inspection & startup | `python3 tests/native_artifacts.py build/review2-20261001/bin --target macos-arm64` | PASS | Mach-O ARM64, minos 14.0, system linkage only, codesign verification, clean startup. |
| macOS production tests-off seam check | `cmake -DBUILD_TESTING=OFF` && `nm` inspection | PASS | Zero test seams or loopback socket symbols in production binaries. |
| macOS sanitized package & archive | `package.py` and `native_artifacts.py` on macOS archive | PASS | Actual extracted archive verified; SHA256 matches; sanitized contents. |
| ACL fault injection (CP-33) | `tests/setup_acl_error_tests.cpp` 12-mode harness | PASS | Injected read errors, entry errors, invalid ACLs, entry presence, free errors, set/init errors all rejected before write; original preserved, zero temporary leak. |
| Mandatory setup & ACL inheritance (CP-33) | `tests/setup_file_tests.cpp` (direct macOS run) | PASS | Posix_spawn chmod setup must succeed; child control file verified to inherit ACL; write_to_env strips extended ACL; directory overwrite rejected; cleanup verified. |
| Build provenance fixture (CP-32) | `tests/build_provenance_tests.py` fixture suite | PASS | Verified: complete build, no-op rebuild, partial builds (main, setup, itu_core, itu_platform reject manifest), failed compilation (#error deletes manifest), failed shared dependency (#error deletes manifest), replaced binary rejection, staged-copy replacement rejection, new input detection, and Git release validation. |
| Full integrated macOS CTest (10 suites) | `ctest --preset macos-arm64` after CP-32/CP-33 edits | PASS (54.77s) | 10/10 suites passed (transport, authentication, core_and_clock, application_flags, setup_terminal, setup_files, setup_acl_errors, artifact_integrity, manifest_paths, build_provenance). |
| Full integrated Linux CTest (9 suites) | `ctest --preset linux-x64` in Docker after CP-32/CP-33 edits | PASS (52.72s) | 9/9 suites passed in Ubuntu 22.04 container (`itu-cp32-cp33`, emulated AMD64); external network disconnected. |
| Integrated packaging with Schema 2 | `python3 scripts/package.py` and `tests/native_artifacts.py` | PASS | Validated on macOS (`dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz`) and Linux (`verified-package-fixed/itu-ders-bot-1.0.0-linux-x64.tar.gz`) with schema 2 provenance. |
| Windows native MSVC build-test-archive | MSVC 2022 x64 toolchain & Windows 10/11 desktop | TEAMMATE OWNED | Blocked on non-Windows host; assigned to teammate. |
| macOS 14/15 runtime & remote CI workflows | Defined CI and account-owner acceptance | DEFERRED / NOT RUN | Deferred/manual/external gates retained; remote workflows and live OBS calls unauthorized. |

### Historical Verification Evidence

#### 1. Review and Defect Reproductions — 2026-10-01 15:34
- **macOS Baseline**: clean `e1aea07` configured, built, and passed 7/7 suites in 28.28s; native binary checks and extracted archive PASS (`dbb97faa...`).
- **ACL Creation/Replacement**: Native probe against current adapter verified file mode 0600, current owner, no ACL after writes, no temp residue (`build/review2-20261001/acl-review-evidence.txt`).
- **ACL Defect Reproduction (CP-33)**: Injected `acl_get_file` EIO into helper; injected unexpected `acl_get_entry` EIO into adapter. Helper falsely passed and adapter treated error as empty ACL, demonstrating need for strict error validation.
- **Manifest Space-Path Defect Reproduction (CP-31)**: Unix Makefiles project containing spaces failed build with exit 2 due to unquoted command line (`build/review2-20261001/provenance with spaces/evidence.log`).
- **Failed-Build Provenance Defect Reproduction (CP-32)**: Disposable main target failed after manifest target; package succeeded with old synthetic executable bytes, proving manifest generation preceded binary success.

#### 2. Linux Docker Verification and CP-31 Resolution — 2026-10-01 21:10
- **Environment**: Docker 29.8 on ARM64 macOS (`--platform linux/amd64`); Ubuntu 22.04 development container (`mcr.microsoft.com/devcontainers/cpp@sha256:9fc38e...`); standalone verified tools in `.deps/tools/linux-x64/`.
- **CP-31 Fix & Regression**: Space-path manifest issue fixed via whole-argument quoting and `VERBATIM`. Added real-preset regression `tests/manifest_path_tests.py` verifying source/build spaces and dependency/compiler/revision metadata.
- **Full Linux CTest Suite (8/8 PASS in 29.47s)**: Transport, authentication, core_and_clock, application_flags, setup_terminal, setup_files, artifact_integrity, manifest_paths. UID 1000, external network disconnected.
- **Identical Archive Smoke on Ubuntu 24.04 PASS**: Fresh `ubuntu:24.04` container under AMD64 emulation verified checksums, sanitized contents, ELF/ABI limits, and missingconfig/nonTTY startup.
- **macOS Regression (8/8 PASS in 26.78s)**: Clean build, all 8 CTest suites passed on native macOS ARM64.

#### 3. CP-32 and CP-33 Candidate Implementation and Focused Verification — 2026-10-01 22:30–22:45
- **CP-33 ACL Robustness & Error Rejection**:
  - `src/platform_posix.cpp`: `atomic_write_private` verifies extended ACL is empty via `acl_valid` and Darwin's `result == -1 && errno == EINVAL`; explicitly sets `errno = 0` prior to calls and fails closed before writing credentials on any entry or unexpected errno (including `errno != ENOENT`).
  - `tests/test_helpers.hpp`: `private_file` rejects unexpected ACL read errors (`errno != ENOENT`) and requires empty valid extended ACL.
  - `tests/setup_file_tests.cpp`: Requires `posix_spawn` chmod setup to succeed and asserts child inherits extended ACL with a control file before verifying credential writing and cleanup.
  - `tests/setup_acl_error_tests.cpp`: 12-mode ACL syscall fault-injection harness verified (empty ACL, free errno, read errors [EIO, EACCES, ENOMEM, EOPNOTSUPP, EINVAL], entry errors [EIO, EPERM, ENOMEM], invalid ACL, read/entry without errno, unexpected entry status, and present entries all rejected before credential write with no temp leaks). PASS.
- **CP-32 Build Provenance Freshness & Integrity**:
  - `cmake/BuildProvenance.cmake`: Added `provenance_start` custom target (deletes `bin/build-manifest.json` before any compilation), attached `BinaryReceipt.cmake` POST_BUILD command to `main` and `setup` to record link-time SHA-256 receipts (`main-$<CONFIG>.sha256`, `setup-$<CONFIG>.sha256`), and added `build_manifest` target depending on both executables.
  - `cmake/ProvenanceInputs.cmake`: Hashes all source, build, and external dependencies into JSON format; writes `provenance-start.json`.
  - `cmake/GenerateManifest.cmake`: Checks input stability against `provenance-start.json`, checks binary receipts match linked executables (with stripped whitespace), and writes schema_version 2 manifest.
  - `scripts/provenance.py`: Validates schema 2, source inventory (excluding dotfiles/`.DS_Store` to match CMake globbing), build inputs, external tools (resolving absolute paths directly), binary receipts, staged copies, and clean release git state.
  - `scripts/package.py`: Enforces schema_version 2 provenance validation before and after staging archive contents.
  - `CMakeLists.txt`: Quoted and guarded `_curl_headers` and `_dependency_inputs` to prevent space splitting in paths; registered `build_provenance` in CTest.
  - `tests/build_provenance_tests.py`: Comprehensive test fixture covering complete build, no-op rebuild, partial target builds (`main`, `setup`, `itu_core`, `itu_platform`), failed compilation `#error`, failed dependency `#error`, tampered binary replacement, staged copy tampering, and git release validation. PASS.

#### 4. Integrated macOS and Linux Verification of CP-32 and CP-33 — 2026-10-01 22:50
- **Full macOS CTest Suite (10/10 PASS in 54.77s)**: `transport` (1.77s), `authentication` (7.79s), `core_and_clock` (0.35s), `application_flags` (27.16s), `setup_terminal` (0.68s), `setup_files` (0.36s), `setup_acl_errors` (0.36s), `artifact_integrity` (0.26s), `manifest_paths` (9.85s), `build_provenance` (6.19s). All passed cleanly.
- **Full Linux CTest Suite in Docker (9/9 PASS in 52.72s)**: `transport` (1.94s), `authentication` (8.32s), `core_and_clock` (0.05s), `application_flags` (28.96s), `setup_terminal` (0.61s), `setup_files` (0.04s), `artifact_integrity` (0.48s), `manifest_paths` (36.71s), `build_provenance` (41.22s). `setup_acl_errors` is Apple-only and correctly not registered on Linux.
- **Native Packaging & Artifact Verification**: Verified `package.py` creates valid archives with schema_version 2 provenance manifests. Native artifact inspection via `tests/native_artifacts.py` passed on both macOS ARM64 (`dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz`) and Linux x64 archives (`verified-package-fixed/itu-ders-bot-1.0.0-linux-x64.tar.gz`).

## Files / Artifacts Changed

- **CP-32 build provenance files**:
  - `cmake/BuildProvenance.cmake`: Invalidates manifest at compilation start via `provenance_start`; attaches `BinaryReceipt.cmake` POST_BUILD to `main` and `setup`; creates `build_manifest` target depending on both executables.
  - `cmake/ProvenanceInputs.cmake`: Hashes all source, build, and external dependencies into JSON format; writes `provenance-start.json`.
  - `cmake/BinaryReceipt.cmake`: Writes post-build link receipt `${_target}-$<CONFIG>.sha256` upon successful link.
  - `cmake/GenerateManifest.cmake`: Checks input stability against `provenance-start.json`, validates binary receipts against linked executables with stripped whitespace, and generates schema_version 2 manifest.
  - `scripts/provenance.py`: Validates source inventory (filtering dotfiles/`.DS_Store`), build inputs, external absolute tools, binary receipts, staged copies, and clean release git state.
  - `scripts/package.py`: Enforces schema_version 2 provenance validation before and after staging archive contents.
  - `CMakeLists.txt`: Safely quotes external input list expansions, wires `provenance-dependencies.cmake` and `cmake/BuildProvenance.cmake`, and registers `build_provenance` in CTest.
  - `tests/build_provenance_tests.py`: End-to-end fixture test exercising complete, no-op, partial, failed, stale, replaced, and staged-copy build provenance.
  - `tests/artifact_integrity_tests.py`: Aligned synthetic package fixture with `schema_version: 2`.
- **CP-33 ACL error handling and regression enforcement files**:
  - `src/platform_posix.cpp`: `atomic_write_private` verifies extended ACL is empty via `acl_valid` and Darwin's `result == -1 && errno == EINVAL`; explicitly clears `errno` before calls and fails before writing credentials on any entry or unexpected errno.
  - `tests/test_helpers.hpp`: `private_file` rejects unexpected ACL read errors (`errno != ENOENT`) and requires empty valid extended ACL.
  - `tests/setup_file_tests.cpp`: Requires `posix_spawn` chmod setup to succeed; asserts child inherits extended ACL with a control file.
  - `tests/setup_acl_error_tests.cpp`: 12-mode ACL syscall fault-injection harness verifying failure before credential write, original preservation, and cleanup.
- **Earlier CP-31 files**:
  - `CMakeLists.txt`: Quoting and `VERBATIM` for manifest target in space paths.
  - `tests/manifest_path_tests.py`: Real-preset spaces regression test.
- **Planning documents**:
  - `.agents/current_plan.md`: Aligned to ACTIVE state with current timestamps, candidate files, and concrete next actions.
  - `.agents/exec_plans/active/cross-platform-port.md`: Consolidated validation sections, eliminated duplicated historical tables and contradictory statements, and updated revision history.

## Handoff Snapshot

- **Current objective**: CP-32 and CP-33 are verified in full integrated macOS (10 suites) and Linux Docker (9 suites) CMake/CTest suites and packaging checks. Windows testing belongs to the teammate.
- **Last verified state**:
  - CP-31 verified fixed (Linux 8/8, macOS 8/8).
  - CP-33 verified fixed (macOS 10/10 CTest suites, direct setup-file test, and 12-mode fault injection pass).
  - CP-32 verified fixed (macOS 10/10 and Linux 9/9 CTest suites, `build_provenance_tests.py`, and schema 2 packaging pass).
  - Space-path list expansion quoting in `CMakeLists.txt`, dotfile filtering and external path resolution in `provenance.py`, receipt whitespace stripping in `GenerateManifest.cmake`, and schema 2 alignment in `artifact_integrity_tests.py` verified.
  - Working tree sanity: `git diff --check` and `git diff --cached --check` pass with zero errors.
- **Next actions**:
  1. Teammate to run native Windows MSVC build, CTest suite, and packaging checks.
  2. Coordinate full release testing and authorized CI workflow runs when all three native platform archives are ready.
- **Blockers**: Physical Linux x64 unproven (emulated AMD64 in Docker). Windows teammate owned. macOS 14/15 runtime deferred. Remote workflows, live calls, publication unauthorized.
- **Open hypotheses / risks**: Multi-config generator handling on Windows (`$<CONFIG>`) in `BinaryReceipt.cmake` is designed to generate `main-Release.sha256`, which requires verification by the Windows teammate.
- **Preserve**: Deleted `data/example_config.json`, personal files, and historical archives. No Git mutations or personal-config reads. Docker image storage is explicitly authorized; tools/dependencies remain in ignored `.deps/`.

## Outcomes & Retrospective

The Docker campaign verified Linux under AMD64 emulation, reproduced and fixed the manifest path defect CP-31, added a real-project regression, and passed all eight suites on Linux and native macOS, plus actual archive checks. CP-32 (build provenance tied to both executables) and CP-33 (strict ACL error handling and mandatory test setup) were fully implemented, hardened, and verified with all 10 CTest suites on macOS and all 9 CTest suites on Linux Docker, plus packaging with schema_version 2 provenance. Four latent robustness issues were identified and hardened: path space splitting in CMake external dependencies list, dotfile/`.DS_Store` inventory pollution in Python provenance validation, external absolute path resolution across platforms, and binary receipt whitespace sensitivity. Physical Linux x64 behavior remains tested via Docker AMD64 emulation, and Windows evidence belongs to the teammate. No live registration, Git mutation, remote workflow or publication occurred.

## Revision Notes

- 2026-10-01 22:50 Europe/Istanbul — Executed full integrated CTest suites on macOS ARM64 (10/10 passed in 54.77s) and Linux Docker AMD64 (9/9 passed in 52.72s). Verified native packaging and `native_artifacts.py` inspection with schema_version 2 provenance on both platforms. Verified `git diff --check` passes cleanly. Updated planning documents to record CP-32 and CP-33 as verified and closed.
- 2026-10-01 22:45 Europe/Istanbul — Audited CP-32 and CP-33 implementations. Hardened `CMakeLists.txt` list quoting against space-containing dependency paths; excluded dotfiles/`.DS_Store` from `source_inputs` in `scripts/provenance.py` to match CMake globbing; resolved external absolute paths directly in `provenance.py`; added whitespace stripping to binary receipt checking in `GenerateManifest.cmake`; aligned synthetic manifest in `artifact_integrity_tests.py` with schema 2. Fully consolidated `cross-platform-port.md` by eliminating duplicated validation sections and contradictory stale entries, and updated `current_plan.md`.
- 2026-10-01 22:30 Europe/Istanbul — Implemented and candidate-verified CP-32 and CP-33. Added build provenance tracking with tests in `build_provenance_tests.py`. Fixed POSIX ACL verification in `platform_posix.cpp` and `test_helpers.hpp`; enforced mandatory chmod setup in `setup_file_tests.cpp` and added 12-mode fault injection in `setup_acl_error_tests.cpp`.
- 2026-10-01 21:10 Europe/Istanbul — Used authorized Ubuntu AMD64 containers; completed pinned project-local bootstrap, baseline Linux verification and Ubuntu 24 smoke. Reproduced/fixed CP-31, added eighth CTest suite, passed Linux/macOS full suites and both archive checks.
- 2026-10-01 15:34 Europe/Istanbul — Reviewed changes from dcf4d04 to e1aea07; verified Mac/ACL fixes and real archive, reproduced CP-31/CP-32 and ACL error/test gaps CP-33, corrected blanket completion/native-PASS claims, assigned Windows tests to teammate, and made Linux/macOS fixes/current-revision execution primary.
- 2026-10-01 11:10 Europe/Istanbul — Implemented and verified fixes for CP-20 through CP-30.
- 2026-10-01 10:55 Europe/Istanbul — Implemented and verified fixes for CP-13 through CP-19.
- 2026-10-01 10:12 Europe/Istanbul — Reviewed current committed implementation, reran fresh macOS build/seven suites/native binary checks, reproduced inherited ACL defect, recorded P1 CP-13/CP-14 ahead of acceptance work and P2/P3 follow-ups.
- 2026-09-30 21:55 Europe/Istanbul — Fixed macOS QoS demotion (CP-10), POSIX CRLF (CP-11), Dependencies.cmake warning (CP-12).
- 2026-09-30 21:40 Europe/Istanbul — Completed native macOS ARM64 configure, build, all 7/7 offline CTest suites, artifact integrity, and packaging.
- 2026-09-30 20:39 Europe/Istanbul — Recorded completed Linux dependencies/build/7 suites/archive/security checks.
- 2026-09-30 19:42 Europe/Istanbul — Resumed on cross-platform, synchronized source completion with unrun gates.
