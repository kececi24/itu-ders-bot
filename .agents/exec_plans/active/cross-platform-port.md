# Finish and verify the unified Windows, macOS, and Linux port

Status: ACTIVE
Created: 2026-09-30
Last updated: 2026-10-02 23:46 Europe/Istanbul
Owner: primary agent; Windows execution and desktop acceptance assigned to the user's teammate
Primary scope: shared C++ core, native adapters, CMake/bootstrap, offline tests, native packaging and release verification

## Purpose / Big Picture

Deliver one codebase/version and one release containing Windows x64, macOS ARM64 and Linux x64 archives while preserving registration behavior. The review of `3a2606a` found remaining CP-32 configure/cache and release checks, followed by user-supplied macOS 14 and Windows 2022 CI failures. All identified corrections are implemented and available-platform verification passed. No validated Linux/macOS implementation defect remains open in this review. Native Windows execution/desktop acceptance and rerunning the supplied macOS 14 CI failure remain pending.

## Scope

### In scope

- Windows 10/11 x64 with MSVC 2022; macOS 14+ ARM64 with Apple Clang; Ubuntu 22.04/24.04 x64 with GCC 11+. Other compatible glibc distributions are best effort.
- Primary owns Linux/macOS review, integration, verification and planning. Teammate owns native Windows execution and desktop acceptance, including the CP-18 coverage work described below.
- Reuse existing native tools. Downloads, dependency sources, builds, caches and installations stay in ignored `.deps/`; existing project-local CMake remains usable. The user explicitly authorized Docker images/containers, including Docker-managed image storage outside the repository.
- Preserve the user's earlier macOS live dry-run/submission reports as historical evidence. They do not establish live acceptance of every later unified revision.

### Out of scope

Global installs, compiler/SDK installation, TLS bypass, Git mutations, remote workflow invocation, publishing and live OBS/registration calls are not authorized for this review. Do not inspect personal `.env` or `data/config.json`. Keep deleted source `data/example_config.json` deleted. macOS 14 execution requires the external CI rerun after the supplied failure; macOS 15 remains user-deferred. Intel Macs, additional CPU targets, musl and 32-bit support are excluded.

## Repository Orientation

- `src/main.cpp`, `src/token.cpp`, `src/clock.cpp`, `src/http.cpp`: shared application, authentication, scheduling and persistent libcurl session.
- `include/platform.hpp`, `src/platform_posix.cpp`, `src/platform_windows.cpp`: CMake-selected terminal, Unicode, private-file and timing adapters.
- `setup/main.cpp`, `include/console.hpp`: configuration/credential setup and interactive UI.
- `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `cmake/dependencies.lock.json`: native build and explicit dependency resolution.
- `cmake/{BuildProvenance,ProvenanceInputs,BinaryReceipt,GenerateManifest}.cmake`, `scripts/provenance.py`: manifest invalidation, snapshots, binary receipts and package validation.
- `scripts/bootstrap.py`, `scripts/package.py`, `scripts/release_checksums.py`, `tests/native_artifacts.py`: local dependency bootstrap, allowlisted archives and release/native checks.
- `tests/`: synthetic loopback fixtures, terminal/private-file tests and provenance/packaging regressions. `.github/workflows/release.yml`: defined native and newer-OS gates, not executed evidence.

## Invariants and Acceptance Criteria

1. Preserve OBS endpoints, authentication sequencing, cookies/redirects/session reuse, JWT acquisition, ordered ECRN/SCRN payloads, configuration/environment precedence, seven clock samples with lowest-RTT selection, target-time scheduling and `--dry-run`, `--test`, `--local`, `--logs`.
2. Preserve the 30-second registration timeout, unknown-outcome advice and exactly one submission attempt. Automated tests use synthetic credentials and loopback only; real registration is unnecessary for acceptance.
3. Keep native APIs in selected adapters and entry boundaries. Retain UTF-8 data/path behavior, hidden password input, cancellation/restoration and atomic private credential writes with failure preservation/cleanup.
4. Use TLS-verifying libcurl everywhere: Apple SDK/system curl on macOS, project-local static curl/Schannel on Windows, project-local static curl/OpenSSL on Linux. Test CA/socket seams must not enter production.
5. Require CMake 3.25+, C++17 and Python 3.12+ for bootstrap/tests/packaging; `BUILD_TESTING=OFF` must not require Python. Download only exact locked URL/version/SHA-256 sources with normal TLS verification; no global-library fallback.
6. A package must attest the inputs actually used to build both executables, reject partial/failed/stale/replaced builds, and validate staged bytes. Known adversarial build failures must be fixed before package acceptance.
7. Package only main/setup, README, sanitized `packaging/example_config.json` as archive `data/example_config.json`, licenses, manifest and checksums. The example contains empty courses and zero lead.
8. Validate architecture, system linkage, clean extracted startup, outer/payload hashes and Linux GLIBC<=2.35, GLIBCXX<=3.4.30, CXXABI<=1.3.13. Smoke the identical Ubuntu 22-built archive on Ubuntu 24.
9. Eventual release assets must share a clean version/revision and pass all three target gates. Windows Server CI is distinct from Windows 10/11 desktop acceptance; AMD64 emulation is distinct from physical/native Linux x64 execution.

## Baseline / Starting Evidence

Latest review began at clean `3a2606a` (user's “cmake bug fix”). Earlier review artifacts at `5c5786a` are historical under `.deps/review-20261002/`; their numbers and hashes are not evidence for the current working tree.

Bounded preflight PASS before execution: macOS 26.6.2 ARM64, Apple Clang 21.0.0, SDK 26.5, make 3.81, project-local CMake 4.4.3 and Python 3.12.3. Compiler/system-curl link/run, Python modules and loopback socket checks passed. No tools were installed.

Docker 29.8.0 was available. Restarted existing network-disabled Ubuntu 22 AMD64 container `itu-cp32-cp33`, using GCC 11.4, make 4.3, Perl 5.34 and project-local Python 3.12.14/CMake 3.31.6. Locked dependencies remain in `.deps/linux-x64/install`: curl 8.22.0, zlib 1.3.2, nghttp2 1.70.0, OpenSSL 3.5.9. Linux bootstrap preflight passed. Execution is AMD64 emulation on Apple Silicon.

User supplied native GitHub Actions failures without a revision: macOS 14 `setup_terminal` timed out in the first exit case; Windows 2022 compiled/linked production targets but rejected the manifest receipt. Archive-smoke/release jobs were skipped. These are failure evidence, not complete platform acceptance.

## Milestones

1. [x] Prioritize reproduced CP-32 configure/cache, dependency configuration, Git setting and Windows receipt defects; implement meaningful rejection and recovery regressions.
2. [x] Correct CP-35 PTY navigation timing and child-output draining; preserve hidden input, exit and terminal-restoration assertions.
3. [x] Finish affected/full macOS/Linux suites, actual binaries, extracted archives and identical Linux archive on Ubuntu 24 after all current corrections.
4. [ ] Receive native Windows build, nine CTest suites, artifact/package and Windows 10/11 desktop evidence. Rerun the user-supplied macOS 14 CI failure on corrected sources.
5. [ ] After explicit authorization and clean matching assets, execute remaining release/external acceptance gates recorded in Validation. Do not invoke or publish remotely from this review.

## Progress

- [x] 2026-10-02 — Reproduced remaining CP-32 gaps at `3a2606a`: cache edits before first build/after reconfigure and included dependency configuration changes could attest stale executable behavior. Evidence `.deps/verify-3a2606a/config-review/results.json`.
- [x] 2026-10-02 — Bound configuration at generation time, included recursive configuration and actual dependency metadata inputs, and forced untracked-file reporting regardless of Git settings. Independent review and focused macOS regressions PASS.
- [x] 2026-10-02 — Reproduced Windows receipt mismatch using CRLF inputs; normalized receipt text hashing while retaining byte-exact executable hashes. Focused LF/CRLF and complete provenance regression PASS.
- [x] 2026-10-02 — Reproduced delayed split-arrow navigation into credentials; CP-35 complete-key/observed-selection fix passes normal and deliberately delayed PTY checks. Independent source review found no blocker.
- [x] 2026-10-02 23:46 — Final corrected-source macOS and Linux pipelines and identical Ubuntu 24 archive verification PASS; exact results/receipts are in Validation. Restored the reused Linux container to stopped state. No remaining local implementation or verification task was identified; native Windows and macOS 14 CI reruns are pending.

## Surprises & Discoveries

- Input and binary hashes alone do not prove which input state was compiled. Production forced-include fingerprints and successful-link receipts must agree. Configure-time inputs need separate binding because recompiling against stale generated flags is insufficient.
- Learning the cache at first build leaves a configure-to-build gap. Generation-time cache/configuration snapshots close it; the current configure inventory also covers nested modules, lock data, dependency selection/metadata, and SDK curl version metadata.
- Git's `status.showUntrackedFiles=no` can hide inputs unless both manifest generation and release validation explicitly request untracked files. The regression uses real Git with a temporary worktree view and optional locks disabled; it does not mutate Git.
- CMake text writes use Windows CRLF while text reads normalize line endings. Receipt fingerprints must hash the same normalized text as the generated header and finalizer; executable and manifest input-file hashes remain byte-exact.
- A requested 20ms sleep between arrow bytes can exceed the parser's 100ms inter-byte timeout. The corrected PTY acceptance sends complete key events and observes menu selection before Enter; production key handling is unchanged.
- The first local full macOS run timed out in `application_flags` after its success line, while concurrent tests gained approximately 64s. Focused rerun passed in 26.788s. This is consistent with a shared execution delay; exact cause is unproven, so no application change or timeout inflation was made.
- CP-34 inventory filtering must ignore only named cache/metadata files and retain legitimate hidden inputs. Historical WSL evidence also requires verifying actual private-file mode/ownership rather than trusting chmod success.

## Decision Log

- 2026-09-30 — Preserve behavior through shared C++ logic and native adapters; use one version/tag with three native archives.
- 2026-10-01 — Assign Windows native/desktop acceptance to the teammate. Explicit Docker authorization permits Docker-managed image storage; other dependencies remain project-local.
- 2026-10-02 — Fix reproduced defects under the user's existing authorization, retain provenance rejection guarantees, and prioritize corrective work over completion claims. These findings do not demonstrate credential exposure.
- 2026-10-02 — Keep source completion separate from executed native results, and local dirty verification archives separate from publishable clean assets. macOS 14 CI rerun is pending after the supplied failure; earlier compatibility deferral is not a PASS.
- 2026-10-02 — Consolidate durable evidence in one findings ledger and validation table. Current plan contains only immediate work, with no duplicated completed task history.

## Defects / Findings Ledger

Open findings are first. Historical PASS entries describe the stated scope; they do not imply every platform or live service was exercised.

| ID | Type / component | Severity | Status | Evidence / reproducer | Fix / disposition |
|---|---|---|---|---|---|
| **CP-35** | macOS/Linux PTY fixture timing | **P2** | **FIXED; macOS/Linux PASS, macOS 14 rerun pending** | User macOS 14 CI failed the first exit case; a 200ms split-arrow delay reproduces navigation into credentials. | Send complete key events, observe selected option before Enter, drain output while awaiting exit with captured timeout diagnostics. Native macOS 14 CI rerun pending. |
| **CP-18** | Windows setup integration tests | **P2** | **SOURCE COMPLETE; native execution pending** | Successful child credential/configuration/Unicode flows, prompt readiness, password secrecy and child console restoration are implemented and reviewed; native execution remains unproven. | Teammate must execute the completed suite on Windows MSVC. |
| **CP-32** | Build/package provenance | **P2** | **FIXED; macOS/Linux PASS; Windows native pending** | Behavior regressions cover preserved-mtime source/header/archive/cache, flags/library choice before first build and after reconfigure, recursive configuration inputs, missing snapshots, altered receipts/binaries, and hidden untracked files. LF/CRLF probe reproduces the Windows CI receipt error. | Generate immutable cache/configuration snapshots; bind compilation/link receipts to input fingerprints; validate actual dependency configuration/metadata; normalize receipt text hashing; request `--untracked-files=normal` in both Git checks. |
| **CP-34** | CMake/Python source inventory | **P2** | **FIXED; PASS** | Inventory policies in `ProvenanceInputs.cmake` and `scripts/provenance.py` aligned to ignore only named cache/metadata (`.DS_Store`, `__pycache__`, `*.pyc`) while tracking legitimate hidden inputs. | Tested in `tests/build_provenance_tests.py`. |
| CP-16 | Provenance freshness | P2 | FIXED via CP-32 | Preserved-timestamp source, dependency, configure, and cache freshness enforced. | Resolved under CP-32. |
| CP-33 | ACL error handling/tests | P2 | FIXED; bounded macOS review and tests PASS | `platform_posix.cpp` and helper clear errno, validate ACLs, accept only valid empty ACL `-1/EINVAL` or no-ACL `ENOENT`, and decide before freeing. Mandatory spawned chmod and inherited control file prevent a skipped fixture. Eighteen cases across twelve modes exercise existing/new destinations and cleanup. | No new defect validated; no claim of exhaustive vulnerability absence or macOS 14/15 execution. |
| CP-31 | Manifest paths containing spaces | P1 | FIXED; macOS/Linux PASS | Whole-argument quoting and VERBATIM; `manifest_paths` uses the real preset, tests-off build and actual manifest with source/build spaces on POSIX. | Retain regression; Windows execution pending. |
| CP-13 | Inherited credential ACL | P1 | FIXED; macOS PASS | Clear/verify extended ACL before credential writes; creation/replacement/failure tests. | Strict error handling is recorded under CP-33. |
| CP-14 | Windows static curl linkage | P1 | SOURCE FIXED; native NOT RUN | `cmake/Dependencies.cmake` adds transitive `iphlpapi`. | Teammate MSVC link gate. |
| CP-15 | Release revision consistency | P2 | FIXED; contract tests PASS | `release_checksums.py` requires matching nonempty version/revision across archives. | Actual three-target clean aggregation still pending. |
| CP-17 | Linux runtime ABI gate | P2 | FIXED; actual ELF/archive PASS | Numeric GLIBC/GLIBCXX/CXXABI ceilings exercised on Ubuntu 22 and identical archive on Ubuntu 24. | Execution emulated AMD64; native host gate separate. |
| CP-19 | Windows repeated direction keys | P3 | SOURCE FIXED; native NOT RUN | Directional repeat buffering and test exist. | Teammate execution. |
| CP-20 | macOS dependency manifest | P1 | FIXED; PASS | Build-directory dependency manifest records SDK curl rather than empty metadata. | Retain actual manifest checks. |
| CP-21 | Windows child assertions | P2 | SOURCE FIXED; native NOT RUN | Hard CreateProcess/wait/exit assertions replace skipped checks; hung children terminated. | CP-18 now supplies successful flows/readiness; both need native execution. |
| CP-22 | Windows inactive control signals | P2 | SOURCE FIXED; native NOT RUN | Handler returns FALSE when no reader is active. | Teammate execution/restoration gate. |
| CP-23 | Windows repeated Enter | P3 | SOURCE FIXED; native NOT RUN | Repeat buffering limited to directional keys. | Teammate execution. |
| CP-24 | Linux PTY child-close EIO | P2 | FIXED; macOS/Linux PASS | `setup_pty_tests.py` handles EIO/EOF while retaining restoration assertions. | Tested Linux as UID 1000. |
| CP-25 | setup argv sentinel | P3 | FIXED; PASS | Null pointer appended for `argv[argc]`. | Preserved. |
| CP-26 | Checksum errors / Git SHA-256 | P2 | FIXED; contract tests PASS | Missing checksum fails explicitly; revision accepts 40/64 hexadecimal digits. | Preserved. |
| CP-27 | Uppercase HTML hex entities | P3 | FIXED; auth fixture PASS | `decode_html` accepts `&#X...;`. | Preserved. |
| CP-28 | POSIX file owner verification | P2 | FIXED; setup tests PASS | Helper requires current UID as well as private mode. | Preserved. |
| CP-29 | Artifact unit-test discovery | P2 | FIXED; artifact suite PASS | Correct import path and dirty/revision/checksum contract cases. | Preserved. |
| CP-30 | fsync after failed write/check | P3 | FIXED; setup tests PASS | fsync only follows successful write. | Preserved. |
| CP-01 | CI matrix/release wiring | — | SOURCE FIXED; remote NOT RUN | Three native jobs, newer-OS archive smoke and aggregate release dependencies defined. | Run only after authorization; not an executed acceptance result. |
| CP-02 | README / AGENTS support policy | — | SOURCE FIXED | Supported targets/build/package commands and evidence limits documented. | Keep aligned with remaining gates. |
| CP-03 | Native archive checker | — | FIXED; PASS | Exact payload/license allowlist, sanitized example and inner/outer hashes. | Fresh real archives exercised below. |
| CP-04 | OpenSSL bootstrap in spaced paths | — | FIXED; historical bootstrap PASS | Generated absolute Make prerequisites made relative; verified upstream sources unchanged. | Prior evidence `build/preflight/bootstrap.log`. |
| CP-05 | Windows static nghttp2 | — | SOURCE FIXED; native NOT RUN | Set `NGHTTP2_USE_STATIC_LIBS=ON`. | Teammate bootstrap/link gate. |
| CP-06 | POSIX permission enforcement | — | FIXED; PASS | fchmod/fstat mode and owner checked; unsupported enforcement refuses write. | Historical WSL refusal plus current private-file tests. |
| CP-07 | Fixture platform header | — | FIXED; PASS | Native platform expectation replaces hardcoded macOS. | Current application suites pass. |
| CP-08 | SDK symlink resolution | — | FIXED; configure PASS | Resolve SDK symlink before explicit dependency-prefix validation. | Current macOS configure passes. |
| CP-09 | Noninteractive terminal detection | — | FIXED; PASS | Require terminal stdin and stdout to avoid redirected-output hangs. | Current startup/terminal checks pass. |
| CP-10 | macOS QoS changes | — | FIXED; core suite PASS | Only elevate lower QoS; retain already-higher QoS and restore. | No physical Linux/Windows timing inference. |
| CP-11 | POSIX CRLF input | — | FIXED; PASS | Strip trailing CR on newline/EOF. | Current terminal/core checks retained. |
| CP-12 | Missing prefix configure warning | — | FIXED; PASS | Check prefix existence before REAL_PATH. | Preserved. |

## Validation Plan and Results

Current source is `3a2606a` plus this review's uncommitted fixes. Final log roots: `.deps/verify-3a2606a/macos-final/` and `.deps/verify-3a2606a/linux-final/`. Each stage log records its exact command. Builds: `build/verify-3a2606a/final-ci` and `build/linux-review-20261001/verify-3a2606a-ci`.

| Gate | Command / method | Status | Result / evidence |
|---|---|---|---|
| Available-platform preflight | `bootstrap.py --target <target> --preflight-only`; native compiler/curl and loopback probes | PASS | Tools and local dependency versions in Baseline; no installations. |
| CP-32 focused configuration behavior | `tests/build_provenance_tests.py <cmake> <repo> macos-arm64 <deps>` | PASS | `config-review/fixed-regression.log`; actual executable results prove regenerated flags/library selection. |
| Windows receipt newline regression | Same provenance fixture; raw CRLF scratch probe | PASS locally; native NOT RUN | `.deps/latest-windows-receipt/{repro.cmake,provenance-regression.log}`; changed inputs and binary tampering remain rejected. |
| CP-35 terminal correction | `python3 tests/setup_pty_tests.py <setup>`; injected 200ms pauses after each complete send | PASS, macOS 26 | Normal and delayed flows pass; user macOS 14 job needs rerun. |
| macOS configure/build | `cmake --preset macos-arm64 -B build/verify-3a2606a/final-ci -DPython3_EXECUTABLE:FILEPATH=<python3.12>`; build `--parallel 3` | PASS | `macos-final/{configure,build}.log`; main/setup and all test targets built. |
| macOS affected/full suites | `ctest --test-dir <mac-build> --output-on-failure --parallel 3`, affected filter first | PASS | Affected 6/6 in 67.43s; full 10/10 in 66.26s. Application fixture passed in 26.92s; PTY test passed. |
| macOS binaries and extracted archive | `tests/native_artifacts.py <bin-or-archive> --target macos-arm64`; `scripts/package.py <bin> --target macos-arm64 --output <build>/verified-package` | PASS | ARM64, minos 14.0, system linkage/codesign, sanitized contents/checksums and safe startup on macOS 26. |
| Linux configure/build and affected/full suites | Same pipeline with `linux-x64`, local tools in network-disabled `itu-cp32-cp33`; CTest as UID 1000 | PASS | Configure/build, affected 5/5 in 240.27s and full 9/9 in 240.01s; `linux-final/`. Tests ran as UID 1000. Container stopped afterward. |
| Linux binaries and Ubuntu 22 extracted archive | Same native/package pipeline, target `linux-x64` | PASS | ELF x64, linkage/ABI ceilings, sanitized contents/checksums and startup; `linux-final/{native,package,archive}.log`. |
| Identical Linux archive on Ubuntu 24 | Network-disabled AMD64 container, UID 1000, repository RO, local Python/readelf; `native_artifacts.py <exact archive> --target linux-x64` | PASS | Same archive SHA recorded below, system loader/libraries and extracted startup; `linux-final/ubuntu24.log`. Readelf wrapper scopes its libraries to inspection only. |
| Production seam exclusion | `nm -C` on main/setup; transport_test positive control | PASS, macOS/Linux | main/setup zero matches on both; transport_test positive controls three (macOS) and four (Linux); `seams.log` in both final log roots. |
| Tests-disabled build / spaced paths | Real-project `manifest_paths` fixture, `BUILD_TESTING=OFF`, actual build_manifest | PASS, macOS/Linux | Tests CMake argument quoting and production build without test dependencies; Windows execution pending. |
| Windows build/test/package/desktop | MSVC 2022 x64, all nine suites including `setup_terminal`, then native/package checks and Windows 10/11 console acceptance | NOT RUN here; user CI previously FAIL | CP-18 successful child Unicode/credentials/configuration, readiness, hidden password and cancellation/restoration source reviewed. Receipt fix requires native rerun. |
| macOS 14 / 15 runtimes | User's macOS 14 job / defined newer-OS archive job | macOS 14 FAIL supplied, rerun pending; 15 DEFERRED | macOS 26 results and deployment metadata do not establish compatibility. |
| Native physical Linux x64 | Defined Linux native CI or physical x64 runner | NOT RUN here | Docker tests execute real AMD64 Linux binaries under emulation; native host/timing evidence remains an external acceptance limit. |
| Live JWT acceptance | Explicitly account-owner-authorized dry-run | NOT RUN | Previous user macOS reports are historical; no credentials read or live calls here. Real registration not needed for automated verification. |
| Clean three-platform aggregation / remote CI | Matching clean version/revision; `release_checksums.py` and defined jobs | NOT RUN / authorization pending | Local archives are dirty verification builds. `archive-smoke` requires successful native jobs; publication additionally requires a pushed `v*` tag. No workflow change or invocation was needed. |

Final archive receipts: schema 2, version 1.0.0, revision `3a2606a6ef134e343631b5e7e64a7922e0892566`, dirty=true.

- macOS: `build/verify-3a2606a/final-ci/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz`; SHA-256 `83333bf75f93efac66385d68ae51a37977a03e63e773b92b0e5d6c5ad98fd070`.
- Linux: `build/linux-review-20261001/verify-3a2606a-ci/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz`; SHA-256 `ebe3597bfcff6051890c2c44b0439568bc3cd8270990278fb824009b1d02d6a9`. These exact bytes passed Ubuntu 22 and Ubuntu 24.

Intermediate logs `.deps/verify-3a2606a/{macos,linux}/` are retained as historical evidence; they are superseded by the final runs above.

## Files / Artifacts Changed

- `cmake/BuildProvenance.cmake`: generation-time configuration/cache snapshots, recursive configure inventory; removes first-build learning and unused UUID.
- `cmake/Dependencies.cmake`: records dependency configuration, metadata and Apple curl version header actually read during configure.
- `cmake/ProvenanceInputs.cmake`, `scripts/provenance.py`: enforce snapshot/cache/inventory binding at build and packaging; manifest hashes include both new snapshots.
- `cmake/BinaryReceipt.cmake`: normalized text input fingerprint; executable hashing remains byte-exact.
- `cmake/GenerateManifest.cmake`, `scripts/provenance.py`: explicit untracked files in Git cleanliness checks.
- `tests/build_provenance_tests.py`: behavioral configure/cache/library, recursive inputs, snapshot recovery, real Git setting and LF/CRLF regressions.
- `tests/setup_pty_tests.py`: deterministic menu navigation and output draining while waiting for exit.
- Both plan files: corrected evidence, priorities and handoff; no duplicated completion histories.
- Latest user's CP-18 Windows source changes remain preserved; this turn did not modify application behavior, personal files, the deleted source example or Git state.

## Handoff Snapshot

- Current objective: obtain native Windows and macOS 14 CI rerun evidence for the corrected sources; then reconcile remaining external acceptance.
- Last verified state: every final local pipeline gate and identical Ubuntu 24 archive check PASS. Exact logs, counts and hashes are in Validation; no known open implementation fix remains in reviewed scope.
- Next actions: (1) teammate runs Windows preset build, all nine CTest suites, binary/archive checks and Windows 10/11 desktop/CP-18 flows; (2) CI owner reruns macOS 14 on corrected sources; (3) record evidence and resolve authorized release/deferred gates.
- Blockers/evidence limits: Windows and macOS 14/15 hosts unavailable locally; Linux AMD64 execution was emulated. Live service, remote workflow invocation and publication remain unauthorized here. No native Windows PASS is claimed.
- Preserve the uncommitted corrections and both plans. Git remains read-only. No global installation, credential access or live registration occurred. The reused Linux container is stopped.

## Outcomes & Retrospective

All validated defects from this review are corrected. CP-32 now binds generation-time configuration, compiled inputs and normalized-text link receipts, and release checks explicitly include untracked files. CP-35 removes the PTY fixture's scheduler-dependent split key and drains output during exit. CP-34 and existing available-platform regression coverage pass.

No local Linux/macOS implementation or verification work remains identified in this review. The plan stays ACTIVE for Windows native/desktop acceptance, the supplied macOS 14 CI rerun, and explicitly deferred external gates. Passing macOS 26 and emulated Linux checks does not substitute for those missing native results.

## Revision Notes

- 2026-10-02 23:46 Europe/Istanbul — Reviewed clean `3a2606a`, fixed remaining CP-32 gaps and supplied CI failures, added CP-35, completed all local/identical-archive checks, and consolidated plans around actual receipts and remaining native/external gates.
- 2026-10-02 — Earlier review/fixes covered CP-32 compilation binding, CP-34 inventory parity and CP-18 source coverage; results retained as historical artifacts.
- 2026-10-01 — Prior reviews introduced CP-13–CP-33 and expanded native/offline checks.
- 2026-09-30 — Created unified cross-platform workstream; superseded macOS-only plan.
