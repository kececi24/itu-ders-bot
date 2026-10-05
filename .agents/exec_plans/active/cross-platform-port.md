# Verify and close the unified Windows, macOS, and Linux port

Status: BLOCKED — Windows and external acceptance evidence pending
Created: 2026-09-30
Last updated: 2026-10-05 21:40 Europe/Istanbul
Owner: primary agent for implementation/fixes/integration; teammate for Windows verification and plan results only
Primary scope: shared core, native adapters, CMake/bootstrap, offline tests and native release artifacts
Queued successor: 2026-10-04-registration-polling.md; requires port completion AND separate explicit user authorization

## Purpose / Big Picture

Deliver one codebase/version and one release with Windows x64, macOS ARM64 and Linux x64 archives. Planned implementation and Windows test coverage are present. The CP-32 correction passed available Mac/Linux verification; actual MSVC scheduling remains to be confirmed. The teammate's remaining assignment is to execute the existing Windows checks and update the plans, not implement tests or features. If verification exposes a defect, record it as the highest-priority open fix for the primary agent.

## Scope

### In scope

- Supported targets: Windows10/11 x64 (MSVC2022 or native MinGW-w64 GCC11+), macOS14+ ARM64, Ubuntu22/24 x64.
- Verify both Windows compiler paths independently; preserve separate project-local dependency prefixes and one Windows release asset.
- Record missing runtime/desktop/CI and release acceptance separately from source completion.

### Out of scope

Polling, registration retries/redesign, global dependency/tool installation, personal credential/configuration reads, live OBS requests, Git mutation, remote workflows except the explicitly authorized retry of Windows job111917284395, and publication. Polling remains stopped even after this plan closes until separately authorized.

## Repository Orientation

- Application: src/main.cpp, token.cpp, clock.cpp and http.cpp; native boundary: include/platform.hpp and src/platform_*.cpp; setup: setup/main.cpp and include/console.hpp.
- Build: CMakeLists.txt, CMakePresets.json, cmake/Dependencies.cmake, dependencies.lock.json and scripts/bootstrap.py.
- CP-32: cmake/BuildProvenance.cmake, ProvenanceInputs.cmake, BinaryReceipt.cmake, GenerateManifest.cmake; scripts/provenance.py; tests/build_provenance_tests.py.
- Acceptance: tests/windows_console_tests.cpp, shared offline fixtures, tests/native_artifacts.py, scripts/package.py and release_checksums.py.
- CI: .github/workflows/release.yml; native jobs upload three platform assets, MinGW verifies without a fourth asset, archive-smoke tests newer OS versions, and tag release depends on all jobs.

## Invariants and Acceptance Criteria

1. Preserve endpoints, authentication/cookies/redirects/JWT, ordered ECRN/SCRN payloads, configuration precedence, seven-sample lowest-RTT clock behavior and current flags.
2. Preserve one registration submission, 30-second timeout and unknown-outcome/manual-check advice. Automated tests use synthetic credentials and loopback only.
3. Keep native APIs in adapters; retain Unicode, hidden passwords, cancellation/restoration and private atomic credential writes.
4. Keep TLS verification enabled; production code excludes test CA/socket/revocation seams. Use SDK curl on macOS and locked project-local static libraries on Windows/Linux.
5. Require CMake3.25+, C++17, Python3.12+ for tests/bootstrap/package and existing native compilers. Tests-disabled production builds must not require Python. Stop failed preflight; never substitute global dependencies.
6. Provenance binds actual source/build/dependency inputs and executable bytes. Invalid outputs require real rebuilding; no-link events must not bless replacements.
7. Archives contain only native executables, README, sanitized packaging/example_config.json, required licenses, manifest and checksums. Keep deleted source data/example_config.json deleted.
8. Verify native architecture/linkage, sanitized extraction/startup/checksums and Linux ABI ceilings (GLIBC2.35, GLIBCXX3.4.30, CXXABI1.3.13).
9. Missing execution evidence is not PASS. Close only after acceptance or explicit dispositions are recorded; publication and polling activation are separately authorized actions.

## Baseline / Starting Evidence

Current Desktop source is `ff6ee7b42f91a5020f94bdd7485695e466606edc` (user committed CP-38) plus the uncommitted workflow-trigger/documentation and planning update. Preserve the newer committed CP-32 production helpers; CP-38 changed only tests/build_provenance_tests.py; workflow-trigger changes remain uncommitted. The latest request authorized retrying only the existing MSVC Windows job; the connector rejected the attempt with API403 and no new run started. No commit/push/publication was performed.

The supplied Windows2022/MSVC run (revision/run URL absent) passed8/9 in29.45s. Only build_provenance failed: after replacing main's receipt input fingerprint with zeroes, the manifest build unexpectedly succeeded. The log did not prove whether linking occurred. The correction below supersedes the old instruction to investigate before choosing an implementation. Corrected-source Windows execution has not yet been supplied.

Historical evidence retained for context:

| Source / environment | Recorded evidence | Location / limitation |
|---|---|---|
| 3a2606a plus corrections later committed as44ce24c, Mac/Linux | Full10/10 Mac and9/9 Linux; native/archive checks and identical Ubuntu24 smoke | .deps/verify-3a2606a/macos-final/ and linux-final/; superseded by current local receipts below |
| 44ce24c plus then-dirty MinGW changes, native Windows | GCC15.2/CMake4.2.3/Python3.13.11; affected5/5, full9/9 and ZIP checks recorded by teammate | .deps/verify-mingw-20261004/ and dist/mingw/ absent here; not current CP-32 acceptance |
| Teammate WSL Ubuntu22 | Tests-disabled production build recorded PASS | Not a full-suite or archive receipt |

Planning-only audit on2026-10-05 used usable rg/git/Python3.12.3. It inspected source/test wiring and documentation; no native Windows build/test execution was claimed.

## Milestones

1. [x] Implement unified platform/compiler paths and CP-32 correction.
2. [x] Verify corrected source on available Mac/Linux, including archives and Ubuntu24 smoke.
3. [ ] Run existing MSVC/MinGW suites, ZIP/seam checks and desktop acceptance; records results.
4. [ ] Resolve remaining external acceptance, finalize outcome and move completed plan out of active. Stop; do not start polling.

## Progress

- [x] 2026-10-05 21:40 Europe/Istanbul — Read latest ff6ee7b CI run37355657214/job111917284395: CP-38 relink case succeeds, production build and8/9 suites PASS; missing-snapshot fixture recovery FAILS at591 (CP-39). Linux/macOS14/MinGW jobs SUCCESS; archive-smoke/release SKIPPED.
- [ ] 2026-10-05 21:40 Europe/Istanbul — User-authorized existing MSVC job retry BLOCKED: connector returned API403 `Resource not accessible by integration`. No run started and no new local tests/source/workflow changes made.

- [x] 2026-10-05 21:29 Europe/Istanbul — User requested stopping CI on every push. Restricted push event to `v*` tags, retained pull_request, added workflow_dispatch; README/manual run instructions updated. YAML parses and all non-trigger workflow fields/jobs/permissions compare identical. SOURCE COMPLETE/static PASS; no remote run or publication.

- [x] 2026-10-05 21:20 Europe/Istanbul — CP-38 fixture correction SOURCE COMPLETE; focused provenance PASS109.78s, same-input real relink changes both binary hashes, full local MinGW9/9 PASS in106.01s. Production helpers and registration behavior unchanged.
- [ ] Native MSVC full provenance acceptance — latest ff6ee7b job passes CP-38 but fails CP-39; local cl unavailable and authorized remote job retry denied by integration permissions.

- [x] 2026-10-04 — CP-32 correction and regression coverage completed; independent review's negative-test control issue corrected.
- [x] 2026-10-04 — Available native/emulated verification completed; detailed current receipts are in Validation. Verification container restored to stopped.
- [x] 2026-10-05 — Windows readiness audit found all nine CTest cases and both compiler presets/CI paths present; added missing explicit seam-check handoff.
- [ ] Corrected-source Windows and external acceptance — BLOCKED on execution/evidence, not known unimplemented test work.

## Surprises & Discoveries

- 2026-10-05 — [Latest MSVC job](https://github.com/kececi24/itu-ders-bot/actions/runs/37355657214/job/111917284395) builds successfully in paths with spaces and prints the successful same-input real-relink check. It fails at tests/build_provenance_tests.py:591 after deliberately deleting a configure snapshot. MSBuild runs provenance_start, which correctly rejects the absent snapshot. The fixture assumes automatic regeneration by a normal build; a generator-specific recovery mismatch is supported by the location of the failure. Changing runtime linkage or shortening paths does not address that demonstrated test step.
- 2026-10-05 — User chose retrying the existing Windows job after being told it repeats all Windows tests. The GitHub integration returned403 `Resource not accessible by integration`; no retry was dispatched. Other jobs were not rerun by this attempt.

- 2026-10-05 — User supplied [MSVC CI job](https://github.com/kececi24/itu-ders-bot/actions/runs/37275634354/job/111651921885). Checkout was `2df7c3bdc246803240c57b94d60ef2de9833fff9`, Windows Server2022, CMake3.31.6, MSVC19.44.35229, Python3.12.10. Production build and eight suites PASS; build_provenance FAIL at line249, `assert first == validate()`, after an unchanged build. SDK selection is a normal configure message; logs do not report an SDK error or manifest field differences.
- 2026-10-05 — Full-manifest equality incorrectly demands identical executable hashes even after a legitimate same-input relink. Current production PRE_LINK/POST_BUILD admission permits real relinks and requires refreshed receipts. Exact MSVC changed fields remain an inference because that log omits them; the corrected assertion reports any changed metadata/input groups on a native rerun.
- 2026-10-05 — Local native MinGW regression recompiles/touches only synthetic fixture sources with unchanged contents and a retained compile-time token. Both main/setup hashes change while validated inputs/metadata and generated-header timestamp stay fixed. Direct/no-link receipt preservation, finalizer rejection, tamper/failed/partial/cache/dependency/staged-copy checks remain enforced; production provenance code is unchanged.
- 2026-10-05 — Local MSVC preflight BLOCKED: missing `cl`; no compiler/SDK was installed or substituted. MinGW is an independent validation path, not MSVC acceptance. Work location remains `C:/Users/ASUS/Desktop/itu ders bot`.

- POST_BUILD can be scheduled independently of a demonstrated link. Existing receipt overwrite was therefore insufficient evidence of successful rebuilding.
- A genuine same-input relink can change executable bytes. Build-start admission plus PRE_LINK output removal supports that case while requiring POST_BUILD to preserve admitted bytes or consume rebuild admission.
- The native artifact checker validates PE/imports and archive safety but does not inspect test-seam symbols. Windows must use the explicit source/build/symbol check below.
- Windows Server CI, desktop-console checks, macOS runtime versions and AMD64 emulation establish different evidence.

## Decision Log

- 2026-10-05 — Limit this request to the authorized Windows job retry and evidence recording. Do not apply Copilot's path/runtime suggestions or add a test-only workflow. Record the denied invocation and leave CP-39 as the next demonstrated fix; preserve spaces coverage and fail-closed checks.

- 2026-10-05 — Run native CI on pull requests, manual dispatch, or `v*` tag pushes. Stop ordinary branch-push runs while preserving PR checks and the release pipeline. Manual dispatch remains verification-only through the existing push/tag publication guard; workflow file must reach the default branch for GitHub manual dispatch.

- 2026-10-05 — Correct the test invariant, preserving all production checks: compare unchanged source/build/dependency metadata after validating each current executable; do not require identical binary bytes after a legitimate relink. Keep exact receipt byte/mtime preservation for explicit POST_BUILD-without-link controls. Add a real recompile/relink regression and field-level mismatch diagnostics. Do not add deterministic-linker flags or skip MSVC cases.

- Preserve MinGW support, isolated compiler prefixes, static GNU runtimes/notices and MSVC as the Windows release compiler.
- Invalid/missing receipts or replaced binaries are removed before target scheduling and recovered by actual linking. Direct finalizer rejection, no-link rejection, no-op stability and partial/failed-build cases remain tested.
- Teammate owns running checks and recording results only. Primary agent owns any implementation fixes revealed by those checks; do not weaken assertions to obtain PASS.

## Defects / Findings Ledger

Each finding appears once. Historical results retain their original scope; current acceptance is recorded in Validation.

| ID | Component | Priority / status | Evidence and remaining disposition |
|---|---|---|---|
| **CP-38** | Unchanged-build fixture manifest equality | **P2; FIXED, local MinGW and native MSVC case PASS** | ff6ee7b MSVC job passes the same-input real-relink check and advances beyond the corrected comparisons; both binary hashes refresh. Full provenance acceptance remains blocked by separate CP-39. |
| **CP-39** | Missing configure-snapshot fixture recovery | **P2; OPEN, native MSVC FAIL** | ff6ee7b job fails at591 after deliberate snapshot deletion; normal MSBuild build does not recreate it before provenance_start rejects it. Explicit configure recovery is a candidate fix; no correction applied or rerun executed in this request. Preserve fail-closed probes and paths with spaces. |
| **CP-32** | Build/package provenance | **P1; production correction implemented; CP-38 fixture corrected, native MSVC acceptance pending** | Build-start admission removes invalid outputs; PRE_LINK supports legitimate relinks; POST_BUILD preserves valid receipts or consumes rebuild admission. Corrected Mac/Linux full provenance PASS. Latest MSVCff6ee7b production build and8/9 suites pass; CP-38 case passes, missing-snapshot recovery fails under CP-39. New local MinGW full9/9 passes; corrected MSVC rerun and artifact/seam acceptance remain required. |
| **CP-35** | POSIX PTY timing | FIXED; macOS14 CI job success at2df7c3b | Complete keys, observed selection and output draining; current macOS26/Linux setup_terminal PASS; supplied run37275634354 macOS14 native job concluded success (before CP-38 fixture change). |
| CP-36 | MinGW support/distribution | SOURCE COMPLETE; teammate native PASS | Separate preset/prefix/stamp, static GNU runtimes/notices; retain compiler isolation and one Windows asset. |
| CP-37 | Windows TZ/Ctrl-C/TLS fixtures | FIXED; MinGW and supplied MSVC suite PASS | Early TZ, private-console Ctrl-C, guarded test CA and precise certificate errors. Recheck production seams with new artifacts. |
| CP-18 | Windows setup integration | FIXED; MinGW and supplied MSVC setup_terminal PASS | Child credentials/config/Unicode/readiness/secrecy/cancellation coverage; desktop acceptance separate. |
| CP-34 | Source inventory parity | FIXED; current Mac/Linux affected PASS | Ignore only named cache/metadata while tracking legitimate hidden inputs; retained provenance regression. |
| CP-16 | Provenance freshness | Correction tracked under CP-32 | Preserve timestamp-independent binding; no duplicate remaining work. |
| CP-33 | POSIX ACL errors | FIXED; current macOS full PASS | Valid-empty/no-ACL, errno/validation and mandatory controls; no exhaustive security claim. |
| CP-31 | Manifest paths/spaces | FIXED; current Mac/Linux affected PASS | Real preset/tests-disabled/actual manifest coverage; historical MinGW and supplied MSVC results retain their scope. |
| CP-13 | Inherited credential ACL | FIXED; historical macOS PASS | Clear/verify before writes; strict error handling under CP-33. |
| CP-14 | Windows static curl linkage | SOURCE FIXED; native binaries exercised | iphlpapi present; clean MSVC package remains a separate gate. |
| CP-15 | Release revisions | FIXED; contract coverage | Matching nonempty version/revision; clean three-target aggregation pending. |
| CP-17 | Linux ABI gate | FIXED; current ELF/archive PASS | Current identical archive passed Ubuntu22/24 ABI/startup under AMD64 emulation. |
| CP-19 | Direction-key repeats | FIXED; MinGW/supplied MSVC setup coverage | Broader desktop acceptance separate. |
| CP-20 | macOS dependency metadata | FIXED; historical PASS | SDK curl metadata retained; validate new manifests. |
| CP-21 | Windows child assertions | FIXED; MinGW/supplied MSVC setup coverage | Hard process/wait/exit assertions and successful child flows retained. |
| CP-22 | Inactive control signals | FIXED; MinGW/supplied MSVC setup coverage | Inactive handler returns FALSE; desktop evidence distinct. |
| CP-23 | Repeated Enter | FIXED; MinGW/supplied MSVC setup coverage | Repeat buffering limited to directions. |
| CP-24 | Linux PTY EIO | FIXED; historical Linux PASS | Preserve EOF/EIO and restoration checks. |
| CP-25 | setup argv sentinel | FIXED | Null argv[argc] retained. |
| CP-26 | Checksums/Git SHA256 | FIXED; contract coverage | Missing checksum rejection and40/64 hex revisions retained. |
| CP-27 | HTML hex entities | FIXED; auth coverage | Preserve uppercase &#X...; decoding. |
| CP-28 | POSIX file owner | FIXED; historical setup PASS | Verify private mode and current UID. |
| CP-29 | Artifact test discovery | FIXED; artifact coverage | Checksum/revision/dirty cases remain discoverable. |
| CP-30 | fsync ordering | FIXED; historical setup PASS | Synchronize only successful writes/checks. |
| CP-01 | CI/release wiring | SOURCE COMPLETE; MSVC job supplied, other outcomes unknown | Three uploaded platforms; MinGW gates release; archive smoke needs native. |
| CP-02 | Support/docs | SOURCE COMPLETE | MSVC/MinGW support documented; plans reconciled here. |
| CP-03 | Native artifact checker | FIXED; current Mac/Linux archive PASS | Corrected Windows ZIP execution remains pending. |
| CP-04 | OpenSSL spaced paths | FIXED; historical bootstrap PASS | build/preflight/bootstrap.log; no hash/TLS bypass. |
| CP-05 | Windows static nghttp2 | SOURCE FIXED; native test executables exercised | Static definitions retained; fresh release artifacts pending. |
| CP-06 | POSIX private mode | FIXED; historical PASS | Unsupported enforcement refuses writes; actual owner/mode checks retained. |
| CP-07 | Fixture platform header | FIXED; application coverage | Native expectation rather than macOS hardcoding. |
| CP-08 | SDK symlinks | FIXED; historical configure PASS | Explicit validation resolves SDK location. |
| CP-09 | Noninteractive terminals | FIXED; startup coverage | Terminal input/output required to avoid redirected-output hangs. |
| CP-10 | macOS QoS | FIXED; historical core PASS | Scoped elevation/restoration; no timing guarantee. |
| CP-11 | POSIX CRLF | FIXED; historical core/terminal PASS | Strip trailing CR at newline/EOF. |
| CP-12 | Missing prefix warning | FIXED | Existence checked before REAL_PATH. |

## Validation Plan and Results

### Current gates

| Gate | Status | Evidence / remaining action |
|---|---|---|
| macOS26.6.2 ARM64 | PASS | Configure/build; affected5/5 in80.72s; full10/10 in74.13s; native/extracted archive and production seams |
| Ubuntu22 AMD64 emulation | PASS | Configure/build; affected5/5 in264.81s; full9/9 in257.46s; native/extracted archive and seams; tests as UID1000 |
| Identical Ubuntu24 archive | PASS | Same Ubuntu22-built bytes, network-disabled AMD64 emulation, ABI/checksums/sanitized startup |
| Corrected Windows MSVC | CI FAIL at ff6ee7b; CP-38 case PASS, CP-39 OPEN | Run37355657214 production build and8/9 suites PASS; missing-snapshot recovery fails at591. cl unavailable locally. Authorized existing-job retry rejected with API403; no new run started. Full9/9/archive/seams remain pending. |
| Corrected Windows MinGW | Local configure/build and full9/9 PASS | Desktop2df7c3b plus CP-38 fixture correction, GCC15.2/CMake4.2.3/native Python3.14.4. Focused provenance109.78s; full106.01s. `.deps/verify-msvc-noop-20261005/`; new ZIP/seam checks not rerun for this test-only change. |
| Windows10/11 desktop | NOT RUN | Teammate synthetic setup/console checks; Server CI alone is insufficient |
| Native Linux x64 / remote job chain | Linux/macOS14/MinGW jobs SUCCESS at ff6ee7b; archive-smoke/release SKIPPED | GitHub run37355657214 job summaries read; MSVC failed, so dependent jobs skipped. Windows retry attempt denied; no new execution. |
| Clean three-platform aggregate | NOT RUN | Three matching clean revision/version archives and release_checksums.py; local dirty verification archives are not publication candidates |
| Live OBS | NOT RUN / not authorized | Historical user reports only; synthetic fixtures cover offline authentication. Real registration is unnecessary for automated acceptance |

### Windows teammate runbook

All planned automated Windows tests are implemented: transport, authentication, core_and_clock, application_flags, setup_terminal, setup_files, artifact_integrity, manifest_paths and build_provenance. The Windows console fixture covers the real setup child, Unicode, hidden password input, repeated keys, cancellation and restoration; shared fixtures cover networking/authentication/flags, ACL/files, packaging and provenance.

1. Use the complete corrected working tree, including all three modified provenance helpers and tests/build_provenance_tests.py. Record revision/dirty state and tool versions. Use existing native compiler tools; keep dependencies local. Run each command separately and stop at the first failure.
2. Preflight MSVC from an x64 VS2022 developer PowerShell with `python scripts/bootstrap.py --target windows-x64 --preflight-only`. Preflight MinGW from a native PowerShell/cmd environment with `python scripts/bootstrap.py --target windows-x64 --toolchain mingw --preflight-only`. Never use MSYS/Cygwin GCC or mix prefixes. If prerequisites fail, record the blocker and stop. If only locked dependencies need preparation, use the corresponding bootstrap command without --preflight-only after successful tool preflight.
3. Run the commands below for MSVC, then repeat in the MinGW environment with `$preset = "windows-mingw-x64"`. The matching build/test presets select Release correctly. Expected affected result5/5; full result9/9.

```powershell
$preset = "windows-x64"
cmake --preset $preset
cmake --build --preset $preset
ctest --preset $preset -R 'transport|core_and_clock|artifact_integrity|manifest_paths|build_provenance' --parallel 3
ctest --preset $preset --parallel 3
python tests/native_artifacts.py "build/$preset/bin" --target windows-x64
python scripts/package.py "build/$preset/bin" --target windows-x64 --output "dist/$preset"
python tests/native_artifacts.py "dist/$preset/itu-ders-bot-1.0.0-windows-x64.zip" --target windows-x64
Get-FileHash "dist/$preset/itu-ders-bot-1.0.0-windows-x64.zip" -Algorithm SHA256
```

4. **Production seam separation:** inspect generated compile/link rules: production itu_core must exclude ITU_ENABLE_TEST_SEAMS and src/http_socket_windows.cpp; itu_core_test must include them. main/setup must link production libraries, not itu_core_test. MSVC rules are the generated .vcxproj files; MinGW rules are the corresponding CMakeFiles flags/link/build files. Inspect symbols with existing compiler tools as below. Production results must contain no TestOptions/itu_open_loopback matches; the test library must provide a positive control. Missing tools or an inconclusive symbol check is BLOCKED, not PASS.

| Compiler | Production symbol command | Positive control |
|---|---|---|
| MSVC | `dumpbin /symbols build/windows-x64/Release/itu_core.lib` | Same command on itu_core_test.lib |
| MinGW | `nm -C build/windows-mingw-x64/libitu_core.a build/windows-mingw-x64/bin/main.exe build/windows-mingw-x64/bin/setup.exe` | `nm -C build/windows-mingw-x64/libitu_core_test.a` |

5. **Desktop acceptance:** on each available Windows10/11 desktop, run setup interactively with synthetic credentials and explicit --env-path/--config-path under an ignored .deps verification directory. Check Unicode display/input, hidden password, menu navigation, save/reopen, cancellation and usable terminal afterward. Do not read personal files or run an authenticated production main/dry-run; loopback application_flags covers that flow safely. Record OS/terminal and each observed result; unavailable desktop versions stay pending.
6. **Record results:** keep command logs below ignored .deps/verify-windows-<date>/<preset>/; update this plan's gate/ledger with source identity, compiler, test counts, failure details, ZIP hash and desktop/seam results, then remove completed tasks from current_plan.md. CP-32 closes only after corrected MSVC and MinGW acceptance. On failure, stop dependent checks, record the concrete defect as the highest-priority open fix and hand it back to the primary agent. No test/source implementation is assigned to the teammate.

CP-32 assertions intentionally prove invalidation plus real rebuild recovery rather than demanding every corrupted-receipt build fail. They also require direct finalizer rejection with a valid control, absent output after invalidation, failed POST_BUILD without linking, stable validated inputs for unchanged builds, exact receipt preservation for explicit no-link events, legitimate same-input relinks, late-replacement rejection, preserved-mtime/configuration binding and partial/failed/staged-copy checks. Capture diagnostic MSBuild Link/PreLinkEvent/PostBuildEvent output if a native-only failure occurs; do not skip or weaken these cases.

### Current local receipts

Both artifacts are schema2/version1.0.0/revisione5108e89a85639c70e0452b0bfbc5c3d20380ee4 with dirty=true. They are local verification artifacts, not clean release assets. Logs include exact configure/build/affected/full/native/package/archive commands and seam evidence.

| Target | Logs / receipt | Archive | SHA256 |
|---|---|---|---|
| macOS | .deps/verify-e5108e8/macos/ | build/verify-e5108e8/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz | bf736560b4151fd7b5b744baa69edbae2da81c0fa3a8ac109adf1a947f0d832e |
| Linux | .deps/verify-e5108e8/linux/ (includes ubuntu24-archive.log) | build/linux-review-20261001/verify-e5108e8/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz | 291ab22126c0bdef16ae89fec94cffc53ebaf8c366d14f783b0b6b53c511fbd5 |

Preflight used AppleClang21/SDK26.5/local CMake4.4.3/Python3.12.3 and system curl; Ubuntu22 used GCC11.4/local CMake3.31.6/Python3.12.14 with locked curl8.22.0/zlib1.3.2/nghttp2 1.70.0/OpenSSL3.5.9. Ubuntu24 used the same local Python and readelf2.38. No global installs, live OBS calls, Git mutations or remote workflows were performed.

## Files / Artifacts Changed

- cmake/BuildProvenance.cmake, ProvenanceInputs.cmake, BinaryReceipt.cmake: build admission, output invalidation and guarded receipt generation.
- tests/build_provenance_tests.py: corruption/no-link/recovery and finalizer-control regression coverage.
- This plan and current_plan.md: verification-only Windows handoff and consolidated status. The queued polling plan retains its separate authorization gate.
- Preserve all uncommitted changes, teammate implementation, ignored tools/logs/archives and deleted source example.

## Handoff Snapshot

- Work location: Desktop cross-platform checkout at ff6ee7b. Preserve pending workflow-trigger/README/plans and queued polling plan.
- Last verified state: latest MSVC CP-38 case PASS but full provenance FAIL at missing-snapshot recovery (CP-39); Linux/macOS14/MinGW CI jobs SUCCESS. No local tests executed for this retry request.
- Next actions: fix the demonstrated CP-39 fixture recovery under appropriate authorization, run affected provenance, then complete remaining Windows/archives/external acceptance.
- Blockers: local MSVC unavailable; explicit Windows retry denied by GitHub integration permissions (API403). Account with Actions write permission can retry the job; an unchanged retry may reproduce CP-39.
- No commit/push/publication or live account access. User authorized the existing-job retry only; no test-only workflow added.

## Outcomes & Retrospective

Implementation is ready for the Windows verification assignment. Local suites and artifacts passed; overall port acceptance remains BLOCKED until outstanding evidence/dispositions are recorded. Do not describe unrun native tests as successful, move the plan prematurely, or start polling automatically.

## Revision Notes

- 2026-10-05 21:20 Europe/Istanbul — Read exact user-supplied Windows CI failure and commit; corrected no-op fixture equality, demonstrated real same-input binary changes with retained integrity guards, completed Desktop MinGW focused/full suite, recorded missing MSVC and remote job outcomes. No runtime/production provenance changes or workflow invocation.

- 2026-10-05 09:46 Europe/Istanbul — Audited Windows test readiness; consolidated duplicate status/history, removed superseded implementation/pause instructions and stale line references, added executable Windows handoff and seam checks; teammate role is verification/plan updates only.
- 2026-10-04 — Implemented CP-32 admission fix and completed current Mac/Linux verification; retained historical evidence separately.
- 2026-09-30 — Created unified port workstream, superseding macOS-only plans.

Local CP-38 command evidence: `.deps/verify-msvc-noop-20261005/{configure,build,affected,full,msvc_preflight}.log`; CI assertion excerpt copied there as `ci-failure-excerpt.log`. Build/test outputs were generated in the Desktop checkout with existing project-local dependencies; no live service access or global installation.

- 2026-10-05 21:29 Europe/Istanbul — Restricted ordinary branch-push triggers under user authorization; kept PR/tag gates, added manual dispatch, statically validated unchanged jobs/permissions and synchronized docs/plans. No workflow invocation or Git mutation.

- 2026-10-05 21:40 Europe/Istanbul — Recorded latest ff6ee7b MSVC snapshot-recovery failure, successful CP-38 case and other CI jobs; attempted user-authorized existing Windows job retry, rejected403 by GitHub integration. No new run/tests/source changes.
