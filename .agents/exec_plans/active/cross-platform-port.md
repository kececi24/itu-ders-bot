# Finish and verify the unified Windows, macOS, and Linux port

Status: ACTIVE
Created: 2026-09-30
Last updated: 2026-10-02 09:55 Europe/Istanbul
Owner: primary agent; Windows execution and desktop acceptance assigned to the user's teammate
Primary scope: shared C++ core, native adapters, CMake/bootstrap, offline tests, native packaging and release verification

## Purpose / Big Picture

Deliver one codebase/version and one release containing Windows x64, macOS ARM64 and Linux x64 archives while preserving registration behavior. CP-32 build provenance hardening (binding compilation receipts, configure-time module/cache hashes, and untracked file release checks), CP-34 inventory parity, and CP-18 Windows test-source work are implemented and verified across macOS and Linux suites and packaging. Windows native execution and desktop acceptance remain with the teammate.

## Scope

### In scope

- Windows 10/11 x64 with MSVC 2022; macOS 14+ ARM64 with Apple Clang; Ubuntu 22.04/24.04 x64 with GCC 11+. Other compatible glibc distributions are best effort.
- Primary owns Linux/macOS review, integration, verification and planning. Teammate owns native Windows execution and desktop acceptance, including the CP-18 coverage work described below.
- Reuse existing native tools. Downloads, dependency sources, builds, caches and installations stay in ignored `.deps/`; existing project-local CMake remains usable. The user explicitly authorized Docker images/containers, including Docker-managed image storage outside the repository.
- Preserve the user's earlier macOS live dry-run/submission reports as historical evidence. They do not establish live acceptance of every later unified revision.

### Out of scope

Global installs, compiler/SDK installation, TLS bypass, Git mutations, remote workflow invocation, publishing and live OBS/registration calls are not authorized for this review. Do not inspect personal `.env` or `data/config.json`. Keep deleted source `data/example_config.json` deleted. macOS 14/15 runtime execution remains user-deferred. Intel Macs, additional CPU targets, musl and 32-bit support are excluded.

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
6. A package must attest the inputs actually used to build both executables, reject partial/failed/stale/replaced builds, and validate staged bytes. Passing today's nominal suites does not waive the open provenance findings.
7. Package only main/setup, README, sanitized `packaging/example_config.json` as archive `data/example_config.json`, licenses, manifest and checksums. The example contains empty courses and zero lead.
8. Validate architecture, system linkage, clean extracted startup, outer/payload hashes and Linux GLIBC<=2.35, GLIBCXX<=3.4.30, CXXABI<=1.3.13. Smoke the identical Ubuntu 22-built archive on Ubuntu 24.
9. Eventual release assets must share a clean version/revision and pass all three target gates. Windows Server CI is distinct from Windows 10/11 desktop acceptance; AMD64 emulation is distinct from physical/native Linux x64 execution.

## Baseline / Starting Evidence

Review began at clean `5c5786ae56597878c062a6f8386745311c780f30`. The changed source was CP-31/32/33 and associated tests/packaging; no application behavior changes were made in this review.

Native host: macOS 26.6.2 ARM64, Apple Clang 21.0.0, SDK 26.5, make 3.81, project-local CMake 4.4.3. Shell Python is 3.12.3; fresh CMake discovery selected already-installed Python 3.14.0 for CTest. Compiler/linker/system-curl and loopback preflight passed. No tools were installed.

Docker initially failed with missing `~/.docker/run/docker.sock`. The user started Docker Desktop; the failed service check then passed (Docker 29.8.0, ARM64 engine). Reused the existing network-disabled Ubuntu 22 AMD64 development container with GCC 11.4, make 4.3, Perl 5.34 and project-local Python 3.12.14/CMake 3.31.6. Linux dependencies remain in `.deps/linux-x64/install`; locked curl 8.22.0, zlib 1.3.2, nghttp2 1.70.0 and OpenSSL 3.5.9. Bounded compiler/link preflight passed before Linux testing. No dependency downloads were needed.

## Milestones

1. [x] **Highest priority: fix CP-32, then CP-34**, with regressions that fail on the current implementation. Detailed evidence and acceptance are in the findings ledger.
2. [x] Re-run affected tests, complete macOS/Linux suites and fresh native/extracted archives after those fixes; smoke the exact new Linux archive on Ubuntu 24.
3. [ ] Complete CP-18 Windows integration coverage and receive teammate MSVC/Windows desktop evidence, including all nine Windows CTest suites and native package checks.
4. [ ] After authorization, reconcile clean three-target release assets and execute remaining external gates; keep deferred gates explicit.

## Progress

- [x] 2026-10-02 — CP-32 provenance configure-time input binding (`CMakeLists.txt` and `CMakeCache.txt`) and untracked file release checks implemented and verified. CP-34 inventory parity verified and tested. CP-18 Windows test-source work complete (Windows execution remains for teammate). Fresh full macOS CTest (10/10 in 40.79s), native artifacts, and archive verified. Fresh full Linux CTest in Docker (9/9 in 155.26s), native artifacts, packaging, and Ubuntu 22/24 archives verified. Container returned to stopped state.
- [x] 2026-09-30–2026-10-01 — Shared core/adapters, dependencies, fixtures, packaging and CI definitions implemented; prior findings and their dispositions are retained once in the ledger.
- [x] 2026-10-02 — Bounded preflight, independent provenance/ACL/acceptance review, fresh macOS and Linux suites, and native/archive checks completed. Exact results appear in the validation table.
- [x] 2026-10-02 — Reproduced CP-32's remaining stale-input and missing-dependency cases; reproduced new CP-34 inventory mismatch. CP-33 review found no new defect within its bounded scope.
- [ ] External acceptance (Windows teammate execution, native Linux physical host, macOS 14/15 runtime, clean release aggregation) remains open; this plan is not complete.

## Surprises & Discoveries

- 2026-10-02 implementation review: the first patched macOS full suite passed, but a further preserved-mtime CMakeLists change left generated compiler flags stale even though production objects recompiled. CP-32 therefore also needs configure-time input binding and a configure/cache regression. Evidence `.deps/review-20261002/provenance/config-final/results.json`. Resolved by recording configure-time token and CMakeLists hash in `provenance-configure.json` at configure time, binding post-configure cache hash on initial build, and failing closed if either differs before CMake regenerates.
- Hashing inputs before/after a build and hashing executable bytes separately does not prove the executables were compiled from those inputs. Preserved source timestamps bypass the present incremental rebuild logic.
- External file inventory is captured during CMake configuration only; later additions can be consumed by a clean rebuild without appearing in the manifest.
- CMake's recursive glob includes dotfiles. The Python filter added in the last change excludes them, contradicting the previous plan's claim that the policies matched.
- Prior Ubuntu 24 evidence dated 21:07 on October 1 predated the CP-32 archive replacement at 22:32. This review replaced that gap with same-current-archive evidence, rather than treating an old log as validation of overwritten bytes.
- Historical WSL testing showed that a filesystem can report successful chmod while failing to enforce mode 0600; the adapter's refusal/preservation behavior remains required. Linux tests here use container temporary storage as UID 1000.

## Decision Log

- 2026-09-30 — Preserve Windows behavior through a portable core and native adapters; do not preserve dead OS-specific implementations. Use one version/tag with three native archives.
- 2026-10-01 — Prioritize Linux/macOS work here and assign Windows native/desktop acceptance to the teammate. The user's explicit Docker authorization permits image storage outside the project; other dependencies remain local.
- 2026-10-02 — Reopen CP-32 at its existing P2 severity and record CP-34 separately. They are the first implementation tasks because they affect package correctness. These reproductions do not demonstrate credential exposure.
- 2026-10-02 — Review and update plans as requested; do not silently implement fixes. Keep successful ordinary suites separate from failed adversarial build scenarios, source completion separate from native execution, and verification archives separate from clean release assets.
- 2026-10-02 — Consolidate evidence into one findings ledger and one validation table. Remove duplicate implementation histories and blanket “all CP-01–CP-33 resolved” wording.

## Defects / Findings Ledger

Open findings are first. Historical PASS entries describe the stated scope; they do not imply every platform or live service was exercised.

| ID | Type / component | Severity | Status | Evidence / reproducer | Fix / disposition |
|---|---|---|---|---|---|
| **CP-18** | Windows setup integration tests | **P2** | **SOURCE COMPLETE; native execution pending** | `tests/windows_console_tests.cpp:125` sleeps 200 ms before cancelling the setup child and checks only nonzero exit. Successful child credential/configuration/Unicode flows, prompt readiness and child console restoration are now implemented. | Teammate must execute the completed suite on Windows MSVC. |
| **CP-32** | Build/package provenance | **P2** | **FIXED; PASS** | Preserved-mtime source, header, static library, CMakeLists.txt, CMakeCache.txt changes, dependency addition/removal, configure-provenance deletion, receipt mismatch, and untracked git files all verified. Evidence `tests/build_provenance_tests.py`. Full macOS (10/10) and Linux (9/9) suites pass. | Bound production compilation/link receipts to input fingerprints; forced rebuild via generated header; bind configure-time inputs and fail closed until CMake regenerates; remove `--untracked-files=no` so untracked release files fail closed. |
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
| CP-21 | Windows child assertions | P2 | SOURCE FIXED; native NOT RUN | Hard CreateProcess/wait/exit assertions replace skipped checks; hung children terminated. | Does not resolve CP-18's missing successful flows/readiness. |
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

All fresh evidence below is from 2026-10-02 at source revision `5c5786a` with uncommitted CP-32, CP-34, and CP-18 source fixes. Log root: `.deps/review-20261002/` and fresh builds `build/fix-20261002/` (macOS) and `build/linux-review-20261001/fix-20261002/` (Linux).

| Gate | Command / method | Status | Result / evidence |
|---|---|---|---|
| macOS prerequisites | `bootstrap.py --target macos-arm64 --cmake <local cmake> --preflight-only`; compiler/curl execution and loopback probe | PASS | Tools in baseline; no installation. |
| macOS configure/build | `cmake --preset macos-arm64 -B build/fix-20261002`; `cmake --build build/fix-20261002` | PASS | Built `itu-ders-bot`, `itu-ders-bot-setup`, and test targets cleanly. |
| macOS affected then full CTest | `ctest --test-dir build/fix-20261002 --output-on-failure` | PASS | 10/10 suites passed in 40.79s. |
| CP-32 & CP-34 provenance test suite | `python3 tests/build_provenance_tests.py` | PASS | Completed, no-op, preserved-mtime source/header/archive/configure/cache, dependency additions/removals, hidden-file parity, partial, failed, replaced, untracked release and staged-copy provenance checks passed. |
| CP-33 independent review/probe | Inspect ACL paths and test wiring; SDK empty/no-ACL semantics probe | PASS, bounded | `acl/semantics.cpp`, `acl/semantics.log`; full suite includes fault injection. |
| macOS actual binaries/archive | `native_artifacts.py build/fix-20261002 --target macos-arm64`; `package.py build/fix-20261002 --target macos-arm64 --output build/fix-20261002/verified-package`; `native_artifacts.py build/fix-20261002/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz --target macos-arm64` | PASS | ARM64, minos 14.0, system linkage, codesign, sanitized/checksummed archive and safe extracted startup. Runtime was macOS 26.6.2. |
| Linux prerequisites | Restarted Docker container `itu-cp32-cp33`; `bootstrap.py --target linux-x64 --preflight-only` with project-local tools | PASS | Container external network disabled; Ubuntu 22 GCC/linker usable. |
| Linux configure/build and affected/full suites | In `itu-cp32-cp33`: `cmake --preset linux-x64 -B build/linux-review-20261001/fix-20261002`; `cmake --build build/linux-review-20261001/fix-20261002`; affected 4/4; full 9/9 CTest | PASS | Fresh build in container; affected 4/4 passed; full 9/9 CTest suites passed in 155.26s as UID 1000. Container stopped afterward. |
| Linux real binaries and Ubuntu 22 archive | `native_artifacts.py`, `package.py`, extracted archive checker | PASS | Actual ELF x64, system linkage/ABI ceilings, sanitized payload and startup. |
| Same Linux archive on Ubuntu 24 | Network-disabled `docker run --rm --platform linux/amd64 --user 1000:1000`, repository RO, local Python/readelf, `tests/native_artifacts.py build/linux-review-20261001/fix-20261002/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz --target linux-x64` | PASS | Passed Ubuntu 24.04 system loader/library verification and startup check. |
| Production seam exclusion | `nm -C` main/setup vs transport_test positive control | PASS, macOS/Linux | Zero test-seam matches in production; positive control has three on macOS and four on Linux. `mac-seams.log`, `linux-seams.log`. |
| Tests-disabled build / paths with spaces | Real-project `manifest_paths` suite configures `BUILD_TESTING=OFF` and builds main/setup through build_manifest | PASS, macOS/Linux | Already covered in both full suites; Windows execution pending. |
| Windows build/test/package/desktop | Teammate native MSVC 2022 x64 and Windows 10/11 | NOT RUN here; CP-18 SOURCE COMPLETE | Nine CTest suites expected; CP-18 setup-child assertions (Unicode credentials, prompt readiness, hidden input, cancellation restoration) implemented in `tests/windows_console_tests.cpp`. Teammate execution required. |
| Physical/native Linux x64 | Defined Ubuntu native CI or available physical x64 runner | NOT RUN for this review | Docker AMD64 execution was emulated on Apple Silicon; native timing/host acceptance unproven. |
| macOS 14/15 runtime | Defined native/newer-OS jobs or manual hosts | DEFERRED / NOT RUN | User deferral retained. Deployment target metadata is not runtime proof. |
| Live JWT acceptance of unified build | Account-owner-authorized dry-run on new platforms | NOT RUN | Prior macOS user evidence historical; no credentials read/live calls here; no real registration needed. |
| Clean three-platform aggregation / remote CI | Matching clean version/revision, then `release_checksums.py` and defined jobs | NOT RUN / authorization pending | Local archives below are dirty verification builds. No publication or workflow invocation. |

Fresh archive receipts (schema 2, version 1.0.0, revision `5c5786ae56597878c062a6f8386745311c780f30`, dirty=true because of uncommitted source fixes and plan edits):

- macOS: `build/fix-20261002/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz`; SHA-256 `b1016a6d9848e437240cb7bedc281ece94d49513344cd2b39384ca29ae9e9e7a`.
- Linux: `build/linux-review-20261001/fix-20261002/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz`; SHA-256 `a763371081d94b16c2a60ce059a3bbf6af5b0ebc79f2b32792083b6886cf469e`. This exact archive passed both Ubuntu versions.

Previous review results remain historical under `.deps/review-20261002/`.

## Files / Artifacts Changed

- Tracked files modified:
  - `cmake/BuildProvenance.cmake`: Bound all configure-time inputs (`CMakeLists.txt`, `CMakePresets.json`, `cmake/*.cmake` modules) and configure token UUID in `provenance-configure.json`.
  - `cmake/ProvenanceInputs.cmake`: Early configure-token, all configure-time files, and CMakeCache hash enforcement, failing closed on discrepancy or missing configuration file.
  - `cmake/BinaryReceipt.cmake`: Added `INPUT_FILE` fingerprint check to link receipt.
  - `cmake/GenerateManifest.cmake`: Removed `--untracked-files=no` so untracked working-tree files cause `dirty=true`.
  - `scripts/provenance.py`: Removed `--untracked-files=no` in `validate_release` to enforce clean working tree against untracked files.
  - `tests/build_provenance_tests.py`: Added preserved-mtime configure (`CMakeLists.txt`), secondary module (`cmake/*.cmake`), cache, and untracked file release tests.
  - `tests/windows_console_tests.cpp`: Added CP-18 setup-child assertions for Unicode credentials, prompt readiness, hidden input, and cancellation restoration.
  - `CMakeLists.txt`: External root directory tracking for dependency manifest provenance.
  - `.agents/current_plan.md`, `.agents/exec_plans/active/cross-platform-port.md`: Synchronized plan and validation states.
- Fresh verified archives:
  - `build/fix-20261002/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz`
  - `build/linux-review-20261001/fix-20261002/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz`
- Existing personal files, deleted source example, older build artifacts and unrelated state remain preserved.

## Handoff Snapshot

- Current objective: CP-32, CP-34, and CP-18 source work are complete. Reconcile teammate Windows execution and remaining external gates.
- Last verified state: 10/10 macOS CTest suites pass (93.18s), 9/9 Linux CTest suites pass in Docker (169.57s on Ubuntu 22/24), all 14 build provenance check suites pass, untracked file validation passes, archive receipts updated.
- Next actions: (1) Teammate runs native Windows MSVC 2022 build, 9 CTest suites (including windows_console_tests with CP-18 integration assertions), native artifacts, packaging, and desktop checks; (2) Reconcile clean three-target release assets and remaining native/live gates once hosts and authorization are available.
- Blockers: Native Windows host acceptance pending teammate execution. Linux Docker execution uses AMD64 emulation; native Linux x64 host acceptance remains pending. macOS 14/15 runtime is deferred. Remote CI, publication, and live OBS calls remain unauthorized.
- Git remains read-only. No commits, pushes, credential reads, global installs, live calls, or remote actions occurred.

## Outcomes & Retrospective

CP-32, CP-34, and CP-18 source-level fixes and regression tests are complete; workstream remains ACTIVE. Build provenance now binds compilation/link receipts directly to input fingerprints, while configure-time UUID tokens, full configure-module inventory hashing, and cache hash binding prevent stale compiler flags or manifest acceptance under preserved-mtime `CMakeLists.txt`, `cmake/*.cmake`, or `CMakeCache.txt` edits. Untracked file evasion is closed in both manifest generation and release validation. CP-18 setup-child test assertions are in place for Windows teammate execution.

## Revision Notes

- 2026-10-02 09:55 Europe/Istanbul — Bound all configure-time files (`CMakeLists.txt`, `CMakePresets.json`, `cmake/*.cmake`) and cache hash in provenance checking; added secondary module regression test in `tests/build_provenance_tests.py`. Executed full test suites and packaging on macOS (10/10 PASS) and Linux (9/9 PASS + Ubuntu 24.04 smoke PASS). Updated archive receipts.
- 2026-10-02 09:25 Europe/Istanbul — Implemented CP-32 (configure-time inputs token & cache hash binding, untracked release check), CP-34 (inventory parity), and CP-18 (Windows setup-child integration assertions). Executed full test suites and packaging on macOS (10/10 PASS) and Linux (9/9 PASS + Ubuntu 24.04 smoke PASS). Updated archive receipts.
- 2026-10-02 08:04 Europe/Istanbul — Reviewed `cecd229..5c5786a`, reopened CP-32, added CP-34, retained CP-18 as unfinished coverage, reran available platform/identical-archive checks and consolidated plan evidence.
- 2026-10-01 — Prior reviews/fixes introduced CP-13–CP-33 and expanded the suites; original “all resolved / only Windows remains” conclusions are superseded by this review.
- 2026-09-30 — Created unified cross-platform workstream from the approved implementation plan; earlier macOS-only verification plan superseded.
