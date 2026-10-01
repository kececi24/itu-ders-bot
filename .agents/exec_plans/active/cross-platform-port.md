# Unified Windows, macOS, and Linux port

Status: ACTIVE
Created: 2026-09-30
Last updated: 2026-10-01 15:34 Europe/Istanbul
Owner: primary agent
Primary scope: shared C++ core, native adapters, tests, bootstrap, packaging, CI, README and AGENTS

## Purpose / Big Picture

Created 2026-09-30 from the comprehensive plan approved by the user. Implement one codebase/version and one release containing native Windows x64, macOS ARM64, and Linux x64 archives. Post-fix review at `e1aea0712fbc95f28b74b75291b64485e5567071` found useful corrections, including the native macOS inherited-ACL fix, but also reproduced CP-31/CP-32 and identified CP-33 robustness/coverage gaps. The current macOS build, seven suites and real archive checks pass within their coverage. Linux execution for this revision remains pending; the older Linux pass is historical. The user assigns Windows testing to their teammate and requires our implementation priority to be Linux/macOS. This plan stays ACTIVE; it does not declare all findings or all platforms verified.

## Highest Priority — Open Implementation Defects

Fixes come before additional acceptance or release work. This is a review and plan update; no production correction was applied in this turn.

- [ ] **P1 / CP-31 — Fix manifest-command quoting for paths containing spaces.** `CMakeLists.txt:69–76` embeds quotes in `-D` arguments without `VERBATIM`; the Unix Makefiles command sends literal backslashes in space-containing paths. The unchanged manifest target block configured successfully but failed at `GenerateManifest.cmake:49` with build exit2 under `build/review2-20261001/provenance with spaces/`. Use properly quoted whole CMake arguments and `VERBATIM`, then regress actual checkout/build paths containing spaces on macOS and Linux. This is a reproduced build defect, not a missing dependency.
- [ ] **P2 / CP-32 — Bind provenance to both successfully built executables.** `CMakeLists.txt:81–82` makes the manifest a prerequisite of `main` and `setup`, so it can stamp current clean HEAD before a failed build or while only one executable is rebuilt. A disposable target failed after manifest generation; unchanged stale executable bytes remained, but `scripts/package.py --tag v1.0.0` accepted them with the new clean revision. Finalize metadata only after both binaries succeed, or use validated per-binary build stamps/identities, and have packaging reject stale, partial or failed-build outputs. Add regressions for all three cases. CP-16 remains PARTIAL until this is fixed.
- [ ] **P2 / CP-33 — Make ACL verification and its regression checks reject errors.** `tests/setup_file_tests.cpp:46–47` silently skips the inherited-ACL regression when `chmod` fails; `tests/test_helpers.hpp:89–95` accepts failed ACL reads. An injected `acl_get_file` EIO passed the helper. Require successful test setup (or an explicit non-PASS unsupported result) and reject unexpected ACL-read/enumeration errors. The production `acl_get_entry` check at `src/platform_posix.cpp:182` also treats every error as an empty ACL; injected unexpected EIO was accepted. That last case is fault-injection evidence only: the ACL had actually been cleared, with no naturally occurring failure or credential exposure reproduced. Add failure-path tests while preserving the now-working CP-13 creation/replacement behavior.

CP-13's original inherited-everyone-read exposure is fixed in the tested native macOS paths. CP-15, CP-17, CP-20 and the other listed shared fixes have the bounded evidence below. Windows source fixes are not native test results; their remaining execution belongs to the teammate.

## Scope

### In scope and support constraints

- Latest ownership: prioritize Linux/macOS implementation and verification here. The user's teammate owns Windows native build/tests/archive and Windows10/11 desktop acceptance. Keep shared changes portable and incorporate their results when provided; do not block Linux/macOS work on unavailable Windows execution.
- Windows 10/11 x64 with MSVC2022; macOS ARM64/AppleClang with deployment target14+; Ubuntu22.04/24.04 x64 GCC11+. Other compatible glibc distributions are best effort. IntelMac, other CPU targets, musl and32bit are outside scope.
- Preserve endpoints, OBS authentication order, cookies/redirects/session reuse, JWT, one ordered ECRN/SCRN batch, config/env precedence and schema, seven lowest-RTT clock samples, target scheduling and four existing flags. Preserve30second timeout, unknown-outcome guidance and no automatic retry.
- All downloads, tools, sources, installations and caches stay below ignored project `.deps/` (existing local CMake may be reused). No global installs. Missing compiler/SDK stops dependent work for user action; bootstrap must not install them.
- Git is read-only for this review. Historical 2026-09-30 checkpoint authorization is not permission for another commit, branch/tag, push, publication or workflow invocation. Offline loopback fixtures only; no live OBS calls or real registration. Do not inspect `.env` or personal config.
- User-deleted `data/example_config.json` stays deleted. Add a new sanitized packaging template with empty courses and zero lead; never derive it from personal configuration.
- macOS14/15 runtime checks remain user-deferred. Existing user-reported macOS dry-run/submission and HTTP200 capacity-rejection evidence is retained, not invented for other platforms.

### Out of scope

Global installs; installing a compiler/SDK; TLS bypass; inspecting personal credentials/config; live OBS/real registration; automatic submission retries; Git mutations, releases and remote workflow invocation. This turn reviews implementation and updates plans; production fixes are prioritized rather than applied. Intel Mac, additional CPUs, musl and 32-bit support are excluded. macOS14/15 runtime acceptance remains user-deferred.

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

1. Correct Linux/macOS build and verification defects CP-31–CP-33; add regressions that expose the reproduced failures.
2. Verify corrected macOS build, affected/full suites, production artifacts and actual archive. Current ordinary-path baseline passes; failing edge cases remain open.
3. Verify the corrected revision on Ubuntu22.04 x64 and smoke the identical archive on Ubuntu24.04. Prior Linux results do not cover the current changes.
4. Keep Windows execution with the teammate, reconcile their results and complete authorized combined-release acceptance separately.

## Progress

- [ ] 2026-10-01 — User authorized necessary Docker image pulls and container execution. Preparing Ubuntu22 AMD64 development and Ubuntu24 smoke containers; host remains ARM64, so execution is emulated. No host-global tools/dependencies will be installed. Current source will be tested without closing CP-31–CP-33.

Primary owns Linux/macOS integration, validation, planning and evidence. Windows execution is teammate-owned. The post-fix review uses bounded read-only adapter, build/provenance and Linux/test reviews; only planning files change, with synthetic artifacts in ignored build directories. Older checked items below are historical milestones, not proof that current findings or every native platform passed.

- [x] Read planning/instructions/manifests and bounded native preflight.
- [x] 2026-10-01 15:34 — Review clean `e1aea07`; fresh macOS configure/build, 7/7 suites in28.28s, native binary checks and actual archive PASS. Native ACL creation/replacement and selected failure probes PASS; manifest edge-case failures reproduced.
- [ ] 2026-10-01 15:34 — Correct CP-31/CP-32, then CP-33, before sign-off — OPEN; no fixes applied by this review.
- [ ] 2026-10-01 15:34 — Execute the latest Linux revision and same-archive Ubuntu24 smoke — BLOCKED: Docker now reachable but no images/toolchain environment available.
- [ ] 2026-10-01 15:34 — Receive teammate Windows native/desktop results — TEAMMATE OWNED; NOT RUN here.
- [x] 2026-10-01 — Review current clean source at `dcf4d04b0af4b6e0dceaf15ce7ef7a4082ca05a8`; fresh macOS configure/build, seven offline suites and native binary/startup checks — PASS. Review found open defects despite this result.
- [x] 2026-10-01 — Fix highest-priority CP-13 and CP-14 — FIXED; native reproduction PASS for CP-13, transitive link added for CP-14.
- [x] 2026-10-01 — Source changes for CP-15–CP-19 were added; macOS suites passed. Later review leaves CP-16 PARTIAL (CP-31/CP-32) and Windows runtime/integration coverage pending with the teammate.
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

1. **Linux/macOS fixes first:** CP-31 path-safe manifest generation; CP-32 complete/successful-binary provenance; CP-33 mandatory ACL tests and explicit error handling. Add failing regressions, apply fixes, then rerun affected and complete native checks.
2. **Current Linux acceptance:** Ubuntu22.04 x64 configure/build, updated seven suites (including actual Linux PTY behavior), real ELF/ABI inspection and verified sanitized archive; smoke that exact archive on Ubuntu24.04. Linux tests recorded before `e1aea07` cannot close this gate.
3. **Linux environment:** after the user started Docker, client/server29.8.0 and LinuxARM64 were reachable, but `docker image ls` was empty. No Ubuntu x64 development image or alternate Linux runtime/toolchain was available. Stop Linux-dependent execution until an appropriate environment is provided or a concrete setup/storage exception is authorized. Docker image downloads would use storage outside the project; none were made. Do not install compilers/SDKs or pull global images silently.
4. **Windows handoff to teammate:** native MSVC2022 x64 dependency/bootstrap/build/CTest/PE/archive checks and Windows10/11 terminal acceptance. Verify successful real `setup.exe` Unicode credential/config paths and process-level cancellation/restoration, not only adapter calls and nonzero child exits. Current source changes provide some child coverage but do not prove every successful setup path.
5. **Deferred/external acceptance:** macOS14/15 runtime remains deferred. Remote native/newer-OS workflows and combined release verification require authorization; publication remains outside this review. Teammate ownership does not waive Windows evidence for the eventual three-platform release.
6. **Live acceptance:** account-owner dry-runs on newly supported platforms when authorized. Keep earlier macOS user evidence; no additional real registration is needed.

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
| **CP-31 / P1** | **Manifest command in space-containing paths** | **OPEN; build failure reproduced** | `CMakeLists.txt:69–76` and missing VERBATIM cause literal backslashes in generated paths; unchanged target block fails at `GenerateManifest.cmake:49`, exit2. Evidence `build/review2-20261001/provenance with spaces/evidence.log`. Correct argument quoting and verify actual Mac/Linux space-path builds. |
| **CP-32 / P2** | **Provenance precedes binary success** | **OPEN; stale-output acceptance reproduced** | `CMakeLists.txt:81–82` generates current/clean metadata before executable completion. A deliberately failing synthetic main target left stale main/setup bytes that `package.py --tag v1.0.0` accepted. Evidence `build/review2-20261001/provenance-order/evidence.log`. Tie manifests to successful complete binaries and test failed/partial/stale cases. |
| **CP-33 / P2** | **ACL error handling and regression enforcement** | **OPEN; source and fault-injection evidence** | `setup_file_tests.cpp:46–47` silently skips when chmod fails; `test_helpers.hpp:89–95` accepts ACL-read failure (injected EIO passed). `platform_posix.cpp:182` also conflates unexpected enumeration failure with empty ACL; injected EIO accepted, but no natural trigger/exposure reproduced. Require explicit expected-empty handling and non-PASS on unsupported/failing verification. Evidence `build/review2-20261001/acl-review-evidence.txt`. |
| **CP-13 / P1** | **Private credential-file ACLs** | **FIXED; PASS** | `src/platform_posix.cpp`: inherited extended ACLs stripped with `acl_init(0)`/`acl_set_fd` and verified with `acl_get_fd` before writing credentials. Regression tests in `tests/setup_file_tests.cpp` and `tests/test_helpers.hpp` confirm creation, replacement, failure preservation, and temporary cleanup. |
| **CP-14 / P1** | **Windows static curl link** | **FIXED; SOURCE CONFIRMED** | `cmake/Dependencies.cmake`: added required `iphlpapi` library to `itu_curl` interface link libraries on WIN32, restoring transitive linkage matching upstream curl. Native MSVC link remains pending Windows environment. |
| **CP-15 / P2** | **Release revision consistency** | **FIXED; PASS** | `scripts/release_checksums.py`: inspects `build-manifest.json` across archives, validating version, target, and ensuring all release archives share an identical non-empty git revision. Regression tests in `tests/artifact_integrity_tests.py` PASS. |
| **CP-16 / P2** | **Build provenance freshness** | **PARTIAL; CP-31/CP-32 OPEN** | Build-time git/dirty capture improves freshness but runs before binary success and fails with spaces. Normal-path manifest was correct for the fresh successful build; this does not close failed/partial-build provenance. |
| **CP-17 / P2** | **Linux C++ runtime compatibility gate** | **SOURCE FIXED; mocked boundaries PASS; native Linux PENDING** | Numeric GLIBCXX<=3.4.30/CXXABI<=1.3.13 checks reject higher versions; targeted unit plus seven independent mocked-readelf boundary cases passed. Current real Linux ELF/archive still needs execution. |
| **CP-18 / P2** | **Windows setup integration/cancellation tests** | **PARTIAL; teammate execution/coverage pending** | Source adds actual child nonterminal/cancellation checks and in-process prompt synchronization. Successful setup child credential/config/Unicode flows are not demonstrated. Current child cancellation still waits200ms and asserts nonzero exit without proving readiness/restoration. Teammate should close these properties on Windows. |
| **CP-19 / P3** | **Windows menu repeated-key events** | **SOURCE FIXED; native NOT RUN here** | Directional repeat buffering and a native test are present. Execution belongs to teammate; macOS CTest does not compile/run this Windows-only target. |
| **CP-20 / P1** | **macOS dependency manifest tracking** | **FIXED; PASS** | `CMakeLists.txt`: writes `${CMAKE_BINARY_DIR}/dependency-manifest.json` from `ITU_DEPENDENCY_MANIFEST`, ensuring macOS build manifest correctly includes Apple SDK curl dependency rather than empty `{}`. PASS. |
| **CP-21 / P2** | **Windows console child process assertions** | **FIXED; SOURCE CONFIRMED** | `tests/windows_console_tests.cpp`: converted silent `if (CreateProcessW)` conditionals to hard assertions; added child wait timeout checks with process termination; ensured `STILL_ACTIVE` does not falsely pass. |
| **CP-22 / P2** | **Windows on_control inactive signal swallowing** | **FIXED; SOURCE CONFIRMED** | `src/platform_windows.cpp`: `on_control` now returns `handled` (FALSE when `reader_active` is false) so control signals during teardown or between readers are handled by subsequent or default handlers. |
| **CP-23 / P3** | **Windows menu repeat key restriction** | **FIXED; SOURCE CONFIRMED** | `src/platform_windows.cpp`: restricted `MenuInput::read()` key repeat buffering strictly to directional keys (`up`, `down`), preventing duplicate `enter` actions. |
| **CP-24 / P2** | **Linux PTY test EIO crash on child close** | **SOURCE FIXED; Mac PASS; updated Linux NOT RUN** | EIO/empty-read drain handling preserves the subsequent restoration assertion; unrelated errors propagate. Fresh Linux PTY execution remains required. |
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
| CP-06 | POSIX private-file mode enforcement | Mode check FIXED; CP-13 native Mac fix verified | Historical mode/owner refusal evidence remains. Native Mac probes now verify inherited ACL removal too; CP-33 records remaining error/test robustness gaps. |
| CP-07 | Application fixture platform header | FIXED; PASS | Hardcoded macOS assertion replaced with native host expectation; affected fixture and full CTest PASS. |
| CP-08 | Apple SDK symlink resolution | FIXED; PASS | `xcrun` returned symlink `MacOSX26.5.sdk -> MacOSX.sdk`; resolved with `file(REAL_PATH)` in `cmake/Dependencies.cmake`; configure succeeded. |
| CP-09 | Non-interactive terminal detection | FIXED; PASS | `is_terminal()` previously checked only STDIN; in piped test runners stdin remained a TTY while stdout was piped, causing `std::cin.get()` to hang; checking both STDIN and STDOUT fixed it. |
| CP-10 | macOS QoS demotion & unspecified elevation | FIXED; PASS | `TimingGuard::activate()` on macOS previously demoted `QOS_CLASS_USER_INTERACTIVE` (33) to `QOS_CLASS_USER_INITIATED` (25) and skipped `QOS_CLASS_UNSPECIFIED` (0); fixed to only elevate if `current < QOS_CLASS_USER_INITIATED`; verified in `core_tests`. |
| CP-11 | POSIX read_line trailing CR preservation | FIXED; PASS | POSIX `read_line()` previously pushed `\r` into string on CRLF, corrupting passwords; added trailing `\r` stripping on newline and EOF matching Windows behavior. |
| CP-12 | Dependencies.cmake unbootstrapped warning | FIXED; PASS | Calling `file(REAL_PATH)` on non-existent `.deps` caused CMake author warnings; guarded with `NOT EXISTS` check before resolving. |

## Validation Plan and Results

Commands below used `.deps/tools/linux-x64/cmake-3.31.6-linux-x86_64/bin/{cmake,ctest}` and `.deps/tools/linux-x64/python/bin/python3` in Ubuntu22 WSL, and `cmake-4.4.3-macos-universal` with native Apple Clang 21 and Python 3.12.3 on macOS ARM64.

| Gate | Command / method | Status | Evidence |
|---|---|---|---|
| Native Mac toolchain & SDK preflight | `python3 scripts/bootstrap.py --target macos-arm64 --cmake <local cmake>` | PASS | Apple Clang 21.0.0, macOS 26.6 SDK, system/SDK libcurl 8.7.1; no downloads needed. |
| Windows prerequisites | `python scripts/bootstrap.py --target windows-x64 --preflight-only` | BLOCKED | Missing `cl`; no compiler installation attempted. |
| Linux tool/dependency locality | Official CMake/standalone Python archive checksums; lock archive hashes; `scripts/bootstrap.py --target linux-x64 --cmake <local cmake>` | PASS | `.deps/tools/linux-x64/provenance.json`; complete `bootstrap.log` exit0; curl8.22.0, zlib1.3.2, nghttp2 1.70.0, OpenSSL3.5.9 static below `.deps/linux-x64/install`. |
| Integrated Linux configure | `cmake --preset linux-x64 -DPython3_EXECUTABLE=<local python>` | PASS | `configure.log` exit0; GCC11.4.0; native x64. |
| Integrated Linux build | `cmake --build --preset linux-x64` | PASS | `build.log` exit0; main/setup and all test binaries. |
| Complete offline CTest on Linux | `ctest --preset linux-x64 --parallel 3 --output-on-failure` | PASS | `test.log`; 7/7, 27.59s. Transport, authentication, core, application, setup, terminal, and archive integrity. |
| Linux native binary/linkage/startup | `python tests/native_artifacts.py build/linux-x64/bin --target linux-x64` | PASS | `artifacts.log`; ELF x64, allowed dynamic runtime only, no RPATH/RUNPATH, glibc<=2.35, clean-directory missingconfig/nonTTY startup. |
| Sanitized Linux package & archive | `python scripts/package.py build/linux-x64/bin --target linux-x64` && `python tests/native_artifacts.py dist/...` | PASS | `dist/itu-ders-bot-1.0.0-linux-x64.tar.gz`, 3,397,271 bytes; SHA256 `7f26ab1ee192e4d23c9456f8f5571fba2ef7c6ad811b81932d1b02d35922b9b5`. |
| Integrated macOS configure | `cmake --preset macos-arm64 -DPython3_EXECUTABLE=$(which python3)` | PASS | Clean configure exit 0; Apple Clang 21.0.0; native ARM64; SDK curl 8.7.1 verified. |
| Integrated macOS build | `cmake --build --preset macos-arm64` | PASS | Clean build exit 0; main, setup, and all test binaries built. |
| Complete offline CTest on macOS | `ctest --preset macos-arm64 --output-on-failure` | PASS | 7/7 suites passed (37.86s): transport (1.71s), authentication (7.71s), core_and_clock (0.38s), application_flags (26.80s), setup_terminal (0.64s), setup_files (0.35s), artifact_integrity (0.26s). |
| macOS native binary/linkage/startup | `python3 tests/native_artifacts.py build/macos-arm64/bin --target macos-arm64` | PASS | Mach-O ARM64, minos 14.0, system linkage only, codesign verification, clean-directory missingconfig/nonTTY startup. |
| macOS tests-off production build & seam check | `cmake --preset macos-arm64 -B build/macos-arm64-production -DBUILD_TESTING=OFF` && `nm` inspection | PASS | Production binaries build clean; `nm` confirms zero test seams or loopback socket symbols in production `main`/`setup`. |
| Sanitized macOS package | `python3 scripts/package.py build/macos-arm64/bin --target macos-arm64` | PASS | `dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz`, 184,637 bytes. |
| Actual extracted macOS archive | `python3 tests/native_artifacts.py dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz --target macos-arm64` | PASS | Outer/payload hashes, exact allowlist/licenses, empty courses/zero lead, native linkage/startup. SHA256 `60e0670d334541ed1a85559ab1d9733e11b8f0853ed1f8f05d744fffadfdcba4`. |
| Archive contract tests | `python tests/artifact_integrity_tests.py` | PASS | 8 contract tests pass on both Linux/Windows and macOS Python. |
| Production seams/private-permission refusal (Linux) | `run_linux_validation.py properties` | PASS | `properties.log`; setup refuses ignored-chmod mount and preserves original/no temp leak. |
| Workflow source gates | YAML parse + matrix/release-needs assertions; `bash -n` for run steps | PASS (static) | `audit.log`; remote jobs not invoked. |
| Native Windows MSVC build-test-archive | supported native toolchains | BLOCKED | MSVC missing on non-Windows host. |
| macOS14/15 runtime, Ubuntu24, Windows desktop/live | defined CI / account-owner acceptance | NOT RUN | Deferred/manual/external gates retained; no workflow or live service invocation. |

### Fresh review evidence — 2026-10-01

Baseline: clean `dcf4d04b0af4b6e0dceaf15ce7ef7a4082ca05a8`, native DarwinARM64. Bounded preflight PASS: local CMake4.4.3, AppleClang21, make3.81, Python3.12.3 stdlib HTTP/TLS/PTY/archive modules and loopback bind, active Apple SDK curl8.7.1. No downloads, installation or credentials needed. Native Windows/Linux were not rerun; earlier results above are historical evidence.

| Gate | Command / method | Status | Result / limit |
|---|---|---|---|
| Fresh macOS configure | Local cmake `--preset macos-arm64 -B build/review-20261001 -DPython3_EXECUTABLE:FILEPATH=<existing Python3.12>` | PASS | Exit0, AppleClang21, Python3.12.3. |
| Fresh macOS build | Local cmake `--build build/review-20261001 --parallel 3` | PASS | All targets; existing test aggregate-initializer warnings, no build failures. |
| Seven offline suites | Local ctest `--test-dir build/review-20261001 --output-on-failure --parallel 3` | PASS | 7/7 in28.36s; log `build/review-20261001/Testing/Temporary/LastTest.log`. These suites missed the newly reproduced ACL defect. |
| Native binary inspection/startup | `python3 tests/native_artifacts.py build/review-20261001/bin --target macos-arm64` | PASS | Actual binaries inspected and started in a clean directory. No archive regenerated in this review. |
| Private access under inherited ACL | Compile/run `build/review-20261001/acl_probe.cpp` against current POSIX adapter after synthetic directory ACL setup | FAIL (CP-13 reproduced) | Success/mode0600 plus everyone-read inherited ACL. Evidence text records exact commands and observed ACL. No alternate-user read attempted. |
| QoS lifecycle probe | Native `build/review-20261001/qos_probe.cpp` | PASS for observed cases | Default QoS21→25→21; high QoS33 stays33. Explicit opt-out thread remains unspecified because elevation fails. A restore-to-unspecified failure was not reproduced; do not report that hypothesis as a bug. Existing core test only checks activation/lifecycle, not all QoS state transitions. |
| Release revision contract | In-memory execution of unchanged checksum aggregator with distinct synthetic archive revisions | FAIL (CP-15 gap reproduced) | Aggregation accepts different revisions. This is a mocked gate test, not a real multi-platform release. |
| Linux C++ ABI contract | Mock readelf output supplied to unchanged native `inspect()` | FAIL (CP-17 gap reproduced) | Newer GLIBCXX/CXXABI requirements accepted; synthetic file under `build/review/newer-libstdcxx.elf`. No current native Linux binary incompatibility asserted. |
| Windows compilation and terminal execution | Native MSVC/Windows environment | NOT RUN | Source review only; do not convert identified missing linkage or coverage into an invented Windows run. |
| Tracked source diff | `git diff --check` and `git status --short` | PASS | Source tree clean before plan edits; review creates only ignored scratch artifacts and updates these two planning files. |

## Files / Artifacts Changed

- Current post-fix review changes only `.agents/current_plan.md` and this ExecPlan. The source changes listed below belong to preceding implementation work. Current scratch probes, evidence and verified Mac package are below ignored `build/review2-20261001/`.
- `cmake/Dependencies.cmake`: resolved symlink on `ITU_APPLE_SDK` with `file(REAL_PATH)` for valid prefix matching; guarded unbootstrapped dependencies check.
- `src/platform_posix.cpp`: `is_terminal()` checks both STDIN and STDOUT handles; private file mode/owner enforcement on POSIX; fixed QoS elevate logic to prevent demotion; stripped trailing `\r` on CRLF input in `read_line()`.
- `src/platform_windows.cpp`: `is_terminal()` checks both STDIN and STDOUT console handles.
- `tests/core_tests.cpp`: added `TimingGuard` lifecycle and multiple activation tests.
- `.github/workflows/release.yml`: complete three-target native/archive/release gates.
- `tests/native_artifacts.py`: outer checksum and exact sanitized allowlist checks.
- `tests/artifact_integrity_tests.py`, `CMakeLists.txt`: eight archive contract tests registered as the seventh CTest suite.
- `tests/application_fixture.py`: native platform header expectation.
- `scripts/bootstrap.py`: OpenSSL path-space fix, Windows nghttp2 static import definition, source download/extraction progress.
- `README.md`, `AGENTS.md`: unified platform policy and usable preset/bootstrap/package instructions.
- `.agents/current_plan.md` and this plan: updated with Linux and macOS validation evidence.
- Ignored `dist/`: Linux native archive (`itu-ders-bot-1.0.0-linux-x64.tar.gz`) and macOS native archive (`itu-ders-bot-1.0.0-macos-arm64.tar.gz`) with SHA256 checksums.

## Handoff Snapshot

- Current objective: finish Linux/macOS; fix **CP-31 first**, then **CP-32/CP-33**. Windows testing belongs to the teammate, and is not a reason to stop available Mac/Linux work.
- Last verified state: clean baseline `e1aea0712fbc95f28b74b75291b64485e5567071`; fresh Mac configure/build, seven suites in28.28s, native binary inspection, package and extracted startup PASS. Original ACL exposure fixed in native probes. Manifest path-space and failed-build provenance defects reproduced; ACL helper failure acceptance observed with fault injection.
- Next actions: repair manifest command/path handling and successful-binary provenance with regressions; make ACL verification/test setup mandatory; rerun corrected Mac gates and current-revision Ubuntu22/24 checks once an appropriate Linux environment exists.
- Blockers: Docker engine now works but contains no images. Need a provided Ubuntu x64 development environment or an explicit setup/storage exception before Linux execution; do not pull images into global Docker storage silently. Windows execution/desktop acceptance is teammate-owned. Mac14/15 remains deferred; remote CI/live service/publication are unauthorized.
- Preserve: source `data/example_config.json` remains deleted; use sanitized packaging template. Existing source/archives and synthetic review evidence remain intact. Only plan edits made; no Git mutations, global dependency downloads or personal credentials/config reads.

## Outcomes & Retrospective

The follow-up changes resolve the reproduced inherited-ACL issue on Mac and improve revision checks, dependency metadata, ABI validation, setup argv handling, PTY handling and HTML entity decoding. Current ordinary-path Mac build/tests/archive verification passes. It is inaccurate to call every finding natively verified or claim complete test coverage: CP-31/CP-32 still break build/provenance requirements, CP-33 weakens ACL error verification, updated Linux execution is missing, and Windows tests have not run here. The user assigns Windows testing to a teammate. Linux/macOS correction and current-revision evidence remain our primary completion gates. No production fixes or downloads were performed in this review.

## Post-fix Review Evidence — e1aea07, 2026-10-01 15:34

Bounded preflight: MacARM64, local CMake4.4.3, AppleClang21, make3.81, active Apple SDK, Python3.12.3 HTTP/TLS/PTY/archive modules and loopback bind PASS. The initial Docker socket was absent. After the user said they would start Docker, the single relevant recheck reported client/server29.8.0 LinuxARM64; image inventory was empty. No alternate Linux tool/runtime directory was found in the inspected project paths. No images were downloaded and no compiler/SDK installed.

| Gate | Command / method | Status | Evidence / limit |
|---|---|---|---|
| Fresh Mac configure/build | Local cmake `--preset macos-arm64 -B build/review2-20261001 -DPython3_EXECUTABLE:FILEPATH=<existing Python3.12>` then `--build build/review2-20261001 --parallel 3` | PASS | All native targets; existing test aggregate-initializer warnings only. New manifest records e1aea07, dirty:false, Apple SDK curl8.7.1. |
| Complete Mac suite | Local ctest `--test-dir build/review2-20261001 --output-on-failure --parallel 3` | PASS | 7/7 in28.28s. `build/review2-20261001/Testing/Temporary/LastTest.log`. Does not exercise CP-31/CP-32. |
| Native binary/startup checks | `python3 tests/native_artifacts.py build/review2-20261001/bin --target macos-arm64` | PASS | Real Mach-O architecture/linkage/signature and clean-directory startup. |
| Actual Mac archive | `python3 scripts/package.py build/review2-20261001/bin --target macos-arm64 --output build/review2-20261001/verified-package --tag v1.0.0`, then native_artifacts on that archive | PASS | Local packaging validation only; no Git tag or publication. Actual extracted archive verified. SHA256 `dbb97faa122c45aa2f77ed361d2c52e9877c462190c93eea6e60ba32427913b3`. |
| ACL creation/replacement | Native probe against current adapter with inherited everyone-read ACL | PASS | File mode0600, current owner, no ACL after both writes, no temporary residue. `build/review2-20261001/acl-review-evidence.txt`. |
| ACL failure preservation | Probe intercepts acl_set_fd ENOTSUP, acl_get_fd EIO and a no-op ACL setter | PASS | Rejected before writing bytes; original preserved; no temp residue. Synthetic fault injection, not observed filesystem errors. |
| ACL test/error detection | Inject acl_get_file EIO into helper; inject unexpected acl_get_entry EIO into adapter | FAIL robustness expectations | Helper falsely passed; adapter treated error as empty ACL. ACL actually cleared in the latter probe, so no credential exposure asserted. CP-33. |
| Space-path manifest | Unchanged custom-target block in disposable Unix Makefiles project containing spaces | FAIL | Configure0/build2, literal backslashes in output path. `build/review2-20261001/provenance with spaces/evidence.log`; CP-31. |
| Failed-build provenance | Disposable main target fails after unchanged manifest target; package old synthetic executable bytes with --tag | FAIL | Build2 but package0, metadata current/clean. `build/review2-20261001/provenance-order/evidence.log`; CP-32. Synthetic artifact is not a working release. |
| Linux ABI validation logic | Targeted ABI unittest plus seven mocked-readelf boundaries | PASS (mocked) | Correct ceilings accepted; newer GLIBC/GLIBCXX/CXXABI rejected. Does not establish current native Ubuntu execution. |
| Current Ubuntu22/24 execution | Docker prerequisite and image inventory | BLOCKED | Engine available after user action; no image or usable Linux toolchain. Historical WSL results predate e1aea07. |
| Current Windows execution | Teammate's native MSVC/desktop verification | TEAMMATE OWNED; NOT RUN here | Source-only changes cannot be counted as native Windows PASS. |

Existing Linux ABI limits are consistent with the [GCC ABI table](https://gcc.gnu.org/onlinedocs/gcc-13.4.0/libstdc%2B%2B/manual/manual/abi.html) and [Ubuntu Jammy libstdc++ package](https://packages.ubuntu.com/jammy/libstdc%2B%2B6); these sources support the numeric check, not execution of our binaries.

## Revision Notes

- 2026-10-01 15:34 Europe/Istanbul — Reviewed changes from dcf4d04 to e1aea07; verified Mac/ACL fixes and real archive, reproduced CP-31/CP-32 and ACL error/test gaps CP-33, corrected blanket completion/native-PASS claims, assigned Windows tests to teammate, and made Linux/macOS fixes/current-revision execution primary. Docker is now available but image/toolchain prerequisites remain missing; no downloads or production source fixes.
- 2026-10-01 11:10 Europe/Istanbul — Implemented and verified fixes for CP-20 through CP-30. Preserved dependency manifest in macOS build provenance; asserted child process creation and timeout termination in `windows_console_tests.cpp`; fixed Windows `on_control` inactive signal swallowing; restricted menu key repeat buffering to directional keys; handled `errno.EIO` in `setup_pty_tests.py`; added terminating `nullptr` to `setup/main.cpp` `argv`; added missing checksum check and Git SHA-256 support to `release_checksums.py` and `package.py`; decoded uppercase hex HTML entities in `src/token.cpp`; added owner UID verification to POSIX `private_file()`; added `tests/` to `sys.path` and 4 new contract tests to `artifact_integrity_tests.py` (15/15 passed); guarded `fsync` on POSIX write failure. Re-verified build, all 7 offline CTest suites (35.58s), and native packaging on macOS ARM64. Updated planning documents.
- 2026-10-01 10:55 Europe/Istanbul — Implemented and verified fixes for CP-13 through CP-19. Stripped inherited ACLs on macOS before writing private credentials; added `iphlpapi` to WIN32 static curl; validated manifest revisions and targets in `release_checksums.py`; added `GenerateManifest.cmake` for build-time provenance and dirty-state tracking; enforced Ubuntu 22.04 `GLIBCXX <= 3.4.30` and `CXXABI <= 1.3.13` in `native_artifacts.py`; enhanced `windows_console_tests.cpp` with setup child process testing and prompt synchronization; handled `wRepeatCount` in Windows console menu input; verified all 7 offline CTest suites and native packaging on macOS ARM64. Updated planning documents.
- 2026-10-01 10:12 Europe/Istanbul — Reviewed current committed implementation, reran fresh macOS build/seven suites/native binary checks, reproduced inherited ACL defect, recorded P1 CP-13/CP-14 ahead of acceptance work and P2/P3 follow-ups, corrected stale completion/host statements, and explicitly listed unfinished implementation versus unexecuted acceptance. Updated current_plan; production sources unchanged.
- 2026-09-30 19:42 Europe/Istanbul — Resumed on cross-platform, synchronized source completion with unrun gates, recorded host availability, repaired CI/docs, and started Linux validation.
- 2026-09-30 20:39 Europe/Istanbul — Recorded completed Linux dependencies/build/7 suites/archive/security checks, fixed native build and fixture failures, added archive contract tests, aligned required ExecPlan sections, and retained external native/manual blockers.
- 2026-09-30 21:13 Europe/Istanbul — Recorded user authorization for the local continuation checkpoint on cross-platform and clarified the pre-checkpoint archive revision; native validation blockers remain pending.
- 2026-09-30 21:40 Europe/Istanbul — Completed native macOS ARM64 configure, build, all 7/7 offline CTest suites, artifact integrity, and packaging; fixed Apple SDK symlink resolution in CMake and dual-handle terminal detection in platform adapters.
- 2026-09-30 21:55 Europe/Istanbul — Fixed macOS QoS demotion and unspecified elevation in `TimingGuard` (CP-10), added POSIX `read_line` trailing `\r` stripping for CRLF parity (CP-11), guarded unbootstrapped dependencies in `Dependencies.cmake` (CP-12), and added `TimingGuard` coverage to `core_tests`. Re-verified build, CTest, packaging, and artifacts.
