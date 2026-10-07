# Verify and close the unified Windows, macOS, and Linux port

Status: COMPLETED — released-port acceptance reported by the user for v1.0.1
Created: 2026-09-30
Last updated: 2026-10-07 07:53 Europe/Istanbul
Owner: primary agent for fixes/integration/plans; teammate for Windows execution and results
Primary scope: native adapters, build/bootstrap, offline tests, provenance and three release archives
Successor: ../active/2026-10-04-registration-polling.md; newer polling source has separate Windows acceptance

## Purpose / Big Picture

One codebase/version produces Windows x64, macOS ARM64 and Linux x64 archives. The user reports that the teammate verified Windows, merged the cross-platform branch into main, all Actions jobs succeeded and v1.0.1 was released. The fetched annotated tag resolves to777e14773dc3ed2f9de8f21fd7aa8878970e34fb. This closes the released-port workstream; it does not attest the newer, uncommitted polling source. No remote release was invoked by this reconciliation.

## Scope

### In scope
- Windows10/11 x64 using MSVC2022 or native MinGW-w64 GCC11+, current macOS ARM64 and Ubuntu22/24 x64.
- Separate Windows compiler prefixes/presets, one Windows release asset, static GNU runtimes and SDK/project-local dependencies.
- Current source, native tests, archive safety/provenance and explicit unavailable execution gates.

### Out of scope
- Older experimental macOS runtime acceptance; no old-device testing requirement.
- Live OBS rate-limit/JWT work (explicitly deferred), personal credential/config reads, global installs, Git mutation, workflow invocation or publication.
- Polling feature semantics, tracked in the related plan. Its authorization is active; no obsolete wait-for-port gate applies.

## Repository Orientation

src/main.cpp/token.cpp/clock.cpp/http.cpp share application behavior; include/platform.hpp and src/platform_* isolate native operations. CMakeLists.txt/presets, cmake/Dependencies.cmake, lock data and scripts/bootstrap.py define builds. BuildProvenance.cmake, ProvenanceInputs.cmake, BinaryReceipt.cmake, GenerateManifest.cmake and scripts/provenance.py bind inputs to linked binaries. Tests cover platform setup/console, loopback networking, artifact contents, paths and provenance. release.yml defines native/MinGW/archive jobs without authorizing invocation/publication.

## Invariants and Acceptance Criteria

1. Preserve auth/cookies/redirects/JWT, ordered ECRN/SCRN payloads, config precedence, hidden passwords, Unicode and native cleanup. Polling's explicit exception does not permit replay of ambiguous POSTs.
2. Preserve 30-second registration timeout and unknown-outcome advice; automated tests use synthetic credentials and loopback only.
3. Keep TLS verification and production/test seam separation. Use macOS SDK curl; locked project-local static curl with Schannel on Windows and OpenSSL on Linux.
4. CMake3.25+, C++17, Python3.12+ for developer tools; tests-disabled builds require no Python. No global installation or silent dependency substitution.
5. Provenance binds actual source/build/dependency inputs and raw executable bytes. No-link events cannot bless replacements; real relinks may change bytes while inputs remain identical.
6. Archives allow only native main/setup, README, sanitized packaging example, licenses, manifest and checksums; never personal data. Keep deleted source data/example_config.json deleted.
7. Validate architecture/system linkage, extraction/startup/checksums and Linux ABI ceiling (GLIBC2.35/GLIBCXX3.4.30/CXXABI1.3.13).
8. Native Windows PASS requires actual execution. Clean release aggregation/publication is future release work, not an unimplemented feature.

## Baseline / Starting Evidence

Release acceptance is the user's report for v1.0.1/777e147. Fetched commits ff6ee7b and2cbbd92 fix the Windows provenance test's real-relink comparison and missing-configure-snapshot recovery. Incoming plan notes predate successful release and therefore still say pending; that status is superseded by the user's report. Individual Windows desktop/terminal results and a final CI run URL were not supplied, so no specific Windows10/11 matrix result is invented.

The local polling checkout remains HEAD502d2dd plus uncommitted changes. Its historical receipts below are distinct from released-port evidence; current reconciliation/verification belongs to the successor plan.

The supplied logs_100960878455 contain CI at revision2df7c3bdc246803240c57b94d60ef2de9833fff9, before current polling changes:

| Supplied job | Observed result |
|---|---|
| Native Windows2022/MSVC | Build PASS;8/9 tests PASS. build_provenance line249 failed assert first == validate() after a second successful build. |
| Native hosted Mac |10/10 plus native/package checks PASS; historical evidence only. |
| Native Ubuntu22/GCC |9/9 plus native/package checks PASS; historical evidence only. |
| Windows MinGW |9/9 plus native/package checks PASS; historical evidence only. |
| archive-smoke / release | No completed results supplied; failed native dependency prevented downstream acceptance. |

This is distinct from the older corrupted-receipt scheduling defect (CP-32). The new log shows strict whole-manifest equality failed; it does not show which fields changed. Genuine MSBuild relinking can change PE bytes, so byte reproducibility across actual relinks is an inference explaining the failure, not a directly logged fact. The corrected test prints field/hash differences and validates receipts.

## Milestones

1. [x] Unified native paths, dependency isolation, setup/security boundaries and packaging.
2. [x] CP-32 real-rebuild admission and no-link receipt protection.
3. [x] CP-38 repeated MSVC build test correction with stricter receipt validation.
4. [x] Finish current Mac/Linux suites/artifacts and identical Ubuntu24 smoke.
5. [x] Released-port Windows/Actions/release acceptance reported by user; close this plan and track newer polling verification in the successor.

## Progress

- [x] Earlier port fixes and CP-32 regression implementation retained; historical Mac/Linux/MinGW evidence preserved below.
- [x] 2026-10-05 — Inspected all supplied CI job logs and fixed only the newly failing test assumption. Standalone Mac provenance fixture PASS.
- [x] 2026-10-05 — Current integrated Mac affected9/9, full13/13 PASS, including polling and provenance.
- [x] 2026-10-06 — Final Mac13/13 and Linux12/12, native/extracted artifacts, production seam checks and identical Ubuntu24 smoke PASS. Provenance input-drift rejection was resolved by rebuilding against frozen final inputs.
- [x] 2026-10-07 — User-reported Windows verification, successful Actions and v1.0.1 release accepted; fetched tag/source confirmed read-only. Moved plan to closed. New polling Windows verification is not covered by this release.

## Surprises & Discoveries

- POST_BUILD execution alone does not prove linking; build admission plus PRE_LINK invalidation protects final receipt generation.
- Conversely a genuine same-input relink may produce different binary bytes. Reproducibility is not required; the manifest must accurately bind those new bytes and unchanged inputs.
- Server CI and Windows desktop-console behavior are distinct evidence.
- A changing README invalidates provenance too; all final artifacts must be built after inputs stop changing.

## Decision Log

- Preserve MinGW support, separate prefixes, static GNU runtimes/notices and MSVC release artifacts.
- Invalid/missing receipts or replaced binaries require real rebuild recovery. Keep direct finalizer/no-link/tampering/partial-build tests strict.
- CP-38 permits only binary fields to differ on repeated MSVC builds; all other fields must match. Every validation also matches current binary hashes and input fingerprints to successful-link receipts and build-start state. Explicit no-link receipt byte/mtime checks remain strict.
- Retain existing CI experimental coverage without adding old-device acceptance work. Defer live OBS tests and publication separately.
- Teammate executes existing tests; root fixes demonstrated defects. Never mark native Windows PASS from Mac fixtures.
- Closure accepts the user's released-port verification report, separately labeled from local execution. Incoming stale pending/queued-polling instructions are not restored. In the successor branch, the user explicitly retains MinGW CI/release gating while adopting PR/manual/version-tag triggers; the incoming MinGW job deletion is not adopted.

## Defects / Findings Ledger

Each finding appears once; old fixes are grouped to remove duplicate history.

| ID | Priority / status | Evidence / disposition |
|---|---|---|
| **CP-39** | FIXED in released2cbbd92; user-reported release acceptance | Explicit reconfigure after deliberately deleting configure snapshots; assert fail-closed diagnostic and recreation before build. Reconciled into newer polling test without dropping stronger local receipt checks; new regression results tracked under RP-08. |
| **CP-38** | FIXED; released-port acceptance reported | ff6ee7b corrects unchanged-build equality and exercises changed-byte recompilation. New polling branch preserves stronger input/binary receipt validation; its verification is separate. |
| **CP-32** (includes CP-16) | FIXED; released-port acceptance reported | Build-start admission, invalid output removal, PRE_LINK/POST_BUILD receipts. Preserve corrupted-receipt recovery and no-link replacement rejection. |
| CP-35 | FIXED | PTY complete key sequences, observed selection and output draining; supplied hosted Mac setup_terminal PASS. No older-OS rerun requirement. |
| CP-36 | SOURCE COMPLETE; released-port acceptance reported | MinGW preset/prefix/stamp, static runtime/notices; supplied historical MinGW9/9/archive PASS. New polling regression execution is tracked separately. |
| CP-37 | FIXED | Windows TZ/Ctrl-C/TLS fixtures; supplied MSVC/MinGW affected tests passed. |
| CP-33, CP-13, CP-06, CP-28, CP-30 | FIXED | POSIX mandatory mode/owner/ACL/error handling and fsync ordering; current Mac setup/ACL coverage passes. |
| CP-34, CP-31, CP-26, CP-29 | FIXED | Inventory parity, spaced paths, checksum/revision and artifact discovery; retain current provenance/package fixtures. |
| CP-18–25 | FIXED | Native setup child assertions, repeats/signals/PTY EOF, SDK metadata, argv sentinel; current offline and historical Windows tests. |
| CP-14, CP-05, CP-04, CP-08, CP-12 | SOURCE FIXED; released-port acceptance reported | Static Windows linkage, OpenSSL spaces, SDK symlinks and prefix checks. New polling artifacts have separate acceptance. |
| CP-01–03, CP-07, CP-09–11, CP-15, CP-17, CP-27 | SOURCE COMPLETE / FIXED | CI/docs/artifact checker, platform headers, terminal handling/QoS/CRLF, release revision checks, Linux ABI and HTML entities. Covered by corresponding offline gates; publication is separate. |

No open released-port source defect is reported. New polling changes and their native regression acceptance remain in the successor plan.

## Validation Plan and Results

| Gate | Status / evidence |
|---|---|
| Bounded Mac preflight | PASS: macOS26.6.2 ARM64, Clang21/SDK26.5, local CMake4.4.3/Python3.12.3 and SDKcurl. |
| Current Mac suite/artifact | PASS final affected9/9, full13/13 in72.87s, native/extracted archive, checksums and production seams; .deps/verify-polling-final/macos/ with receipt.json. |
| Bounded Linux preflight | PASS: existing network-disabled Ubuntu22 AMD64 container, GCC11.4/local CMake3.31.6/Python3.12.14 and locked dependencies. |
| Current Ubuntu22 suite/artifact | PASS affected9/9 in50.01s; full12/12 in256.59s; native/extracted archive, ABI/checksums and production seams. AMD64 emulation, tests as UID1000; .deps/verify-polling-final/linux/. |
| Identical Ubuntu24 artifact | PASS same archive bytes, network-disabled AMD64 emulation and UID1000; .deps/verify-polling-final/linux/ubuntu24-archive.log. |
| Released Windows / Actions / v1.0.1 | USER-REPORTED PASS; tag resolves to777e147. This is released-port evidence, not execution of the newer polling tree. |
| Windows desktop detail | Not separately supplied; no per-OS/terminal result claimed. Released-port closure accepts the user's overall Windows verification; new polling acceptance is separate. |
| Live OBS | Explicitly deferred outside this plan; loopback tests do not prove live limits/JWT behavior. |
| v1.0.1 release | USER-REPORTED completed; fetched annotated tag confirmed. No publication by this agent. Future releases still require separate authorization. |

### Historical local verification receipts before release reconciliation

Artifacts are schema2/version1.0.0/revision502d2dd2f31fceb8d7b80bb0ccbaf01bf5de553f, dirty=true. They verified the pre-reconciliation polling working tree, not the released v1.0.1 assets. Logs contain exact commands, suite output, seam counts and receipt.json. Mac ran natively on ARM64; Ubuntu22/24 used Docker/Rosetta AMD64 emulation on this Mac. Final v1.0.1-base polling receipts are recorded in the successor plan.

| Target | Archive | SHA256 |
|---|---|---|
| Mac | build/verify-polling-final/verified-package/itu-ders-bot-1.0.0-macos-arm64.tar.gz | be832d0dc69bd9dd68a0d8a7ca2b50efe24d2ed583339def7f480baf29cb3eeb |
| Linux | build/linux-review-20261001/verify-polling-final/verified-package/itu-ders-bot-1.0.0-linux-x64.tar.gz | 7f0a5c8bf74f96bac6e0bb9e80b479bdb149a9a204e375ae168996e26da06a28 |

Those local checks performed no global installations, personal configuration reads, live OBS requests, Git mutations or remote workflows.

Current polling Windows verification instructions are maintained only in the active successor plan.

## Files / Artifacts Changed

Existing cmake provenance helpers and native port implementation are retained. This review changes tests/build_provenance_tests.py for CP-38; README/AGENTS support wording and both plans reflect experimental older macOS and the deferred live phase. Current integration artifacts also contain polling corrections; receipts below must describe the full working tree.

## Handoff Snapshot

- Current objective: none for this closed released-port workstream.
- Last verified state: user-reported Windows/CI/release success at v1.0.1/777e147; historical local receipts above retain their narrower scope.
- Next actions: continue the active polling reconciliation/Windows runbook only; do not reopen old port gates or start live OBS automatically.
- Blockers: none for accepted released port; native polling Windows verification belongs to its active plan.
- Preserve current working-tree changes and read-only Git boundary. Fetched refs were inspected; no pull/merge/checkout was performed.

## Outcomes & Retrospective

The cross-platform port was accepted and released as v1.0.1 according to the user. Released Windows fixes CP-38/39 are reconciled selectively with stronger polling-branch tests. The successor keeps newer polling behavior, experimental older-macOS scope and deferred live testing. Its Windows tests remain a separate acceptance requirement; released-port success must not be reused as proof for code added afterward.

## Revision Notes

- 2026-10-07 — Closed against user-reported Windows/CI/release acceptance and fetched v1.0.1 tag; incorporated CP-39 history, removed obsolete remaining-port gates and transferred current Windows runbook to polling plan.
- 2026-10-06 — Consolidated stale/duplicate sections, incorporated logs_100960878455 and CP-38, recorded final Mac/Linux/archive receipts and current12-test Windows handoff; removed obsolete polling pause, older-OS and live acceptance gates.
- 2026-10-04/05 — Preserved CP-32 real-link correction and previous native evidence.
- 2026-09-30 — Created unified port workstream, superseding macOS-only plans.
