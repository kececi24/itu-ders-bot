# Unified Windows, macOS, and Linux port

Status: ACTIVE
Created: 2026-09-30
Last updated: 2026-09-30 21:13 Europe/Istanbul
Owner: primary agent
Primary scope: shared C++ core, native adapters, tests, bootstrap, packaging, CI, README and AGENTS

## Purpose / Big Picture

Created 2026-09-30 from the comprehensive plan approved by the user. Implement one GitHub codebase, one version/tag, and one release containing native Windows x64, macOS ARM64, and Linux x64 archives. Setup may differ by OS. Status: source implementation, documentation and locally available Linux validation are complete. Ubuntu 22.04 WSL GCC11.4 configure/build, affected checks, all seven offline CTest suites, native artifact inspection and sanitized archive verification PASS. Windows native build is BLOCKED by missing MSVC2022; macOS execution is unavailable on this Windows host. Remote native CI, newer-OS smoke, desktop/manual/live gates remain NOT RUN. No release has been published.

## Scope

### In scope and support constraints

- Windows 10/11 x64 with MSVC2022; macOS ARM64/AppleClang with deployment target14+; Ubuntu22.04/24.04 x64 GCC11+. Other compatible glibc distributions are best effort. IntelMac, other CPU targets, musl and32bit are outside scope.
- Preserve endpoints, OBS authentication order, cookies/redirects/session reuse, JWT, one ordered ECRN/SCRN batch, config/env precedence and schema, seven lowest-RTT clock samples, target scheduling and four existing flags. Preserve30second timeout, unknown-outcome guidance and no automatic retry.
- All downloads, tools, sources, installations and caches stay below ignored project `.deps/` (existing local CMake may be reused). No global installs. Missing compiler/SDK stops dependent work for user action; bootstrap must not install them.
- User authorized a local continuation checkpoint commit on `cross-platform` on 2026-09-30. No push, additional branch/tag, release publication or workflow invocation is authorized. Offline loopback fixtures only; no live OBS calls or real registration. Do not inspect `.env` or personal config.
- User-deleted `data/example_config.json` stays deleted. Add a new sanitized packaging template with empty courses and zero lead; never derive it from personal configuration.
- macOS14/15 runtime checks remain user-deferred. Existing user-reported macOS dry-run/submission and HTTP200 capacity-rejection evidence is retained, not invented for other platforms.

### Out of scope

Global installs; installing a compiler/SDK; TLS bypass; inspecting personal credentials/config; live OBS/real registration; automatic submission retries; pushes, tags, releases and remote workflow invocation. The user-authorized local continuation checkpoint is in scope. Intel Mac, additional CPUs, musl and 32-bit support are excluded. macOS14/15 runtime acceptance remains user-deferred.

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

1. Integrate shared/native source and project-local build, fixture and package infrastructure — source complete.
2. Execute available native prerequisites, dependency build and integrated configure/build — Linux PASS; macOS/Windows pending.
3. Run affected checks, full offline suites, archive and security-property checks — Linux PASS; additional native runners pending.
4. Update docs/plans and preserve reviewable artifacts — complete for this continuation; keep the plan active for external acceptance.

## Progress

Primary owns integration, native validation, repository policy/docs, planning and final evidence. Previous parallel source changes are integrated. This continuation is proceeding in the existing cross-platform worktree without new worker delegation.

- [x] Read planning/instructions/manifests and bounded native preflight.
- [x] Add platform interfaces/adapters and keep native macOS regression baseline. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Implement Windows terminal/Unicode/files/timing and portable loopback sockets. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Implement Linux POSIX reuse, dependency recipe and native build rules. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Port existing fixtures; Windows console/DACL coverage; deterministic test-only PEM fixtures. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Pin and implement project-local dependency bootstrap and presets. — SOURCE COMPLETE; Linux validated, remaining native acceptance pending.
- [x] Unify allowlisted packages/checksums/manifests/licenses and one-release CI. — SOURCE COMPLETE; Linux packaging and static workflow checks PASS; remote CI execution pending.
- [x] Run affected then integrated local native checks, archive clean-directory smoke and inspect dependency locality. — PASS on Ubuntu22 WSL; seven CTest suites and sanitized archive verified.
- [x] Check production test-seam exclusion and actual unsupported private-permission filesystem refusal. — PASS; `build/preflight/properties.log`.
- [ ] Configure/build/test/archive on macOS with its project-local CMake; historical preflight only — BLOCKED on current Windows host.
- [ ] Native Windows MSVC build/CTest/archive and Windows10/11 desktop acceptance — BLOCKED at missing `cl`; no compiler installed.
- [ ] Execute defined remote native/newer-OS CI and manual/live gates after appropriate authorization — NOT RUN.
- [x] Update README/AGENTS/support matrix and actual bootstrap/package/preset commands. — SOURCE COMPLETE; docs name unverified native/manual gates.

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
- Current host is Windows x64, not the earlier Mac. Native macOS configure/build cannot run here. Windows has CMake 4.2.3 and Python 3.13.11, but no available MSVC 2022 `cl`; bootstrap preflight stops before downloads.
- Existing WSL Ubuntu 22.04 has GCC 11.4.0, make, Perl, CA bundle, and readelf. Its CMake 3.22.1/Python 3.10.12 are too old. Verified CMake 3.31.6 and standalone Python 3.12.14 were extracted only into `.deps/tools/linux-x64`; URLs/hashes are in that directory's `provenance.json`.
- The checked-in CI was still macOS-only and used obsolete artifact/package commands. It now defines three native targets, each extracted archive check, newer-OS smoke gates, and one-release aggregation. It has not been invoked.


- All four dependency archives were retrieved with default TLS verification and independently matched committed hashes. zlib/nghttp2 built first. OpenSSL initially failed on unescaped absolute Makefile prerequisites containing the checkout's spaces; generated prerequisites are now relativized while verified upstream source archives remain unchanged. The complete bootstrap then PASSed.
- The integrated Linux build PASSed. Initial full CTest had one failure: `application_fixture.py` expected macOS in `sec-ch-ua-platform` on Linux. It now expects the native host's platform; affected application fixture and complete suite then PASSed.
- On this NTFS-backed WSL mount, chmod returns success but mode0600 is not enforced. The built setup refused a synthetic replacement, preserved the old file and removed its temporary. On Linux temporary storage, existing setup-file/PTY tests passed private mode checks.

## Decision Log

- 2026-09-30 — Continue Linux execution using existing WSL GCC and project-local verified tools; stop Windows-dependent builds at missing compiler and keep macOS execution pending. No compiler/SDK/global installs or TLS bypass.
- 2026-09-30 — Native archive verification now checks the adjacent outer checksum, the exact license allowlist, and sanitized example contents in addition to payload hashes and startup.

- 2026-09-30 — Keep generated OpenSSL Makefile source prerequisites relative to the local build, to support checkout paths containing spaces without relocating dependencies or altering archive hashes.
- 2026-09-30 — Verify POSIX file mode/owner after chmod before writing credentials; reject unsupported enforcement rather than reporting a private-file success.
- 2026-09-30 — Windows curl requires `NGHTTP2_USE_STATIC_LIBS=ON` so headers use static linkage rather than DLL imports; source fixed, native MSVC gate pending.

## Defects / Findings Ledger

| ID | Component | Status | Evidence / disposition |
|---|---|---|---|
| CP-01 | CI | SOURCE FIXED; NOT RUN | macOS-only matrix and obsolete package arguments replaced; workflow YAML/matrices/release dependencies and bash syntax PASS, remote execution NOT RUN. |
| CP-02 | README / AGENTS | SOURCE FIXED | Replaced macOS-only policy/build/release assumptions with the supported matrix and pending-evidence distinction. |
| CP-03 | Native archive checker | FIXED; PASS | Outer checksum, exact licenses, empty-course/zero-lead checks; eight contract tests and actual Linux archive gate PASS. |
| CP-04 | Linux OpenSSL bootstrap | FIXED; PASS | `make` initially split `/mnt/.../itu ders bot`; generated source prerequisites made relative; complete rebuild/bootstrap PASS, evidence `build/preflight/bootstrap.log`. |
| CP-05 | Windows static nghttp2 recipe | SOURCE FIXED; NOT RUN | Upstream curl finder applies `NGHTTP2_STATICLIB` only when `NGHTTP2_USE_STATIC_LIBS=ON`; set it for Windows. MSVC unavailable. |
| CP-06 | POSIX private-file enforcement | FIXED; PASS | Verify mode0600 and effective owner before body writes; actual ignored-chmod mount refusal preserved a synthetic original, `properties.log`. |
| CP-07 | Application fixture platform header | FIXED; PASS | Hardcoded macOS assertion replaced with native host expectation; affected fixture and full CTest PASS. |

## Validation Plan and Results

Commands below used `.deps/tools/linux-x64/cmake-3.31.6-linux-x86_64/bin/{cmake,ctest}` and `.deps/tools/linux-x64/python/bin/python3` in existing Ubuntu22 WSL. All logs are ignored `build/preflight/` files. The bounded `run_linux_validation.py` helper saves commands/exit codes and uses existing Windows Git only for read-only revision queries in this Windows-managed worktree.

| Gate | Command / method | Status | Evidence |
|---|---|---|---|
| Historical Mac preflight | User handoff; baseline above | PASS (historical) | Apple Clang/SDK curl/loopback only; integrated Mac build NOT RUN. |
| Windows prerequisites | `python scripts/bootstrap.py --target windows-x64 --preflight-only` | BLOCKED | Missing `cl`; no compiler installation attempted. |
| Linux tool/dependency locality | Official CMake/standalone Python archive checksums; lock archive hashes; `scripts/bootstrap.py --target linux-x64 --cmake <local cmake>` | PASS | `.deps/tools/linux-x64/provenance.json`; complete `bootstrap.log` exit0; curl8.22.0, zlib1.3.2, nghttp2 1.70.0, OpenSSL3.5.9 static below `.deps/linux-x64/install`. |
| Integrated Linux configure | `cmake --preset linux-x64 -DPython3_EXECUTABLE=<local python>` | PASS | `configure.log` exit0; GCC11.4.0; native x64. |
| Integrated Linux build | `cmake --build --preset linux-x64` | PASS | `build.log` exit0; main/setup and all test binaries. Non-fatal missing aggregate-field initializer warnings in transport fixture. |
| Affected core/setup tests | `ctest --preset linux-x64 -R 'core_and_clock\|setup_files\|setup_terminal'` | PASS | `affected.log`; 3/3 tests. |
| Affected application test after fix | `ctest --preset linux-x64 -R '^application_flags$'` | PASS | `application.log`; 1/1, 27.63s. Initial failure retained in `test-first.log`. |
| Complete offline CTest | `ctest --preset linux-x64 --parallel 3 --output-on-failure` | PASS | `test.log`; 7/7, 27.59s. Includes original transport/auth/core/app/setup/terminal suites plus archive integrity. |
| Archive contract tests on Windows | `python tests/artifact_integrity_tests.py` | PASS | 8 tests, Windows Python3.13.11; opaque fixtures only, not Windows native execution. |
| Linux native binary/linkage/startup | `python tests/native_artifacts.py build/linux-x64/bin --target linux-x64` | PASS | `artifacts.log`; ELF x64, allowed dynamic runtime only, no RPATH/RUNPATH, glibc<=2.35, clean-directory missingconfig/nonTTY startup. |
| Tests-off configure without Python discovery | `cmake --preset linux-x64 -B build/linux-x64-production -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE` | PASS | `production-configure.log`; Python discovery not used (option reported unused); separate production configuration only. |
| Sanitized Linux package | `python scripts/package.py build/linux-x64/bin --target linux-x64` | PASS | `package.log`; `dist/itu-ders-bot-1.0.0-linux-x64.tar.gz`, 3,397,271 bytes. |
| Actual extracted Linux archive | `python tests/native_artifacts.py dist/itu-ders-bot-1.0.0-linux-x64.tar.gz --target linux-x64` | PASS | `verify.log`; outer/payload hashes, exact allowlist/licenses, empty courses/zero lead, native linkage/startup. SHA256 `7f26ab1ee192e4d23c9456f8f5571fba2ef7c6ad811b81932d1b02d35922b9b5`. |
| Production seams/private-permission refusal | `run_linux_validation.py properties` | PASS | `properties.log`; nm control finds TestOptions/loopback socket only in test binary; actual setup refuses ignored-chmod mount and preserves original/no temp leak. |
| Workflow source gates | YAML parse + matrix/release-needs assertions; `bash -n` for run steps | PASS (static) | `audit.log`; remote jobs not invoked. |
| Native Mac / Windows build-test-archive | supported native toolchains | BLOCKED | Mac unavailable on current host; MSVC missing. |
| macOS14/15, Ubuntu24, Windows desktop/live | defined CI / account-owner acceptance | NOT RUN | Deferred/manual/external gates retained; no workflow or live service invocation. |

## Files / Artifacts Changed

- `.github/workflows/release.yml`: complete three-target native/archive/release gates.
- `tests/native_artifacts.py`: outer checksum and exact sanitized allowlist checks.
- `tests/artifact_integrity_tests.py`, `CMakeLists.txt`: eight archive contract tests registered as the seventh CTest suite.
- `tests/application_fixture.py`: native platform header expectation.
- `scripts/bootstrap.py`: OpenSSL path-space fix, Windows nghttp2 static import definition, source download/extraction progress.
- `src/platform_posix.cpp`: fail before credential writes when private mode/owner is not enforced.
- `README.md`, `AGENTS.md`: unified platform policy and usable preset/bootstrap/package instructions.
- `.agents/current_plan.md` and this plan: source-complete status and host-specific evidence.
- Ignored `.deps/`: verified tools/source archives, provenance, local static dependencies. Ignored `build/preflight/`: logs and bounded continuation helpers. Ignored `dist/`: Linux native archive and adjacent checksum; no credentials, personal config or test binaries.

## Handoff Snapshot

- Current objective: finish remaining Mac/Windows native builds, external CI/newer-OS archive execution, and desktop/manual acceptance.
- Last verified state: Linux local prerequisites/dependencies/configure/build, affected checks, 7/7 offline CTest, sanitized archive, production seam exclusion and private-permission refusal all PASS. Docs and source/static CI checks updated.
- Next actions: on Mac run project-local CMake with `--preset macos-arm64`, build, affected/full CTest, then matching package/artifact commands; on Windows provide existing MSVC2022 x64 developer environment and run bootstrap/preset build/CTest/archive. No compiler install is authorized. Execute remote CI only after user authorization.
- Blockers: Mac unavailable here; Windows `cl` missing; Ubuntu24/macOS14/15/desktop/live execution unverified. Source-only Windows nghttp2 setting still needs native execution.
- Preserve: deleted source `data/example_config.json`, integrated sources and continuation fixes included in this checkpoint, ignored dependency caches/logs/archive. Never inspect personal ignored credentials/config. Primary checkout shares `cross-platform`; only the local continuation checkpoint was authorized.
- Artifact revision identifies base commit `7780a30d6a991df8c5d2c2ba78a494bba74a82c6`; the local archive was built before this checkpoint and includes the continuation fixes. It is a local validation artifact; rebuild from the committed revision before release.

## Outcomes & Retrospective

Locally available Linux implementation and validation are complete, with a reviewable sanitized archive. Integration exposed and fixed a real OpenSSL path-space build failure and a stale macOS-only fixture assertion. Additional checks proved archive rejection rules and private-filesystem failure behavior. Current host limitations prevent completing the Mac/Windows native gates; keep this plan active and do not claim a verified three-platform release. The user authorized a local checkpoint preserving the continuation fixes, tests, CI, documentation and synchronized plans. No global installs, TLS bypass, live OBS calls, registration, pushes/tags, workflow invocation or publication occurred.

## Revision Notes

- 2026-09-30 19:42 Europe/Istanbul — Resumed on cross-platform, synchronized source completion with unrun gates, recorded host availability, repaired CI/docs, and started Linux validation.
- 2026-09-30 20:39 Europe/Istanbul — Recorded completed Linux dependencies/build/7 suites/archive/security checks, fixed native build and fixture failures, added archive contract tests, aligned required ExecPlan sections, and retained external native/manual blockers.

- 2026-09-30 21:13 Europe/Istanbul — Recorded user authorization for the local continuation checkpoint on cross-platform and clarified the pre-checkpoint archive revision; native validation blockers remain pending.
