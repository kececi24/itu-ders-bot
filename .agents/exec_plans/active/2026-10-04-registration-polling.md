# Complete bounded registration polling and local-clock timing

Status: ACTIVE — selective release reconciliation and Mac/Linux verification complete; newer polling Windows acceptance pending
Created: 2026-10-04
Last updated: 2026-10-07 07:53 Europe/Istanbul
Owner: primary agent for source/integration/plans; teammate for Windows execution and result recording
Primary scope: configuration, classification, scheduler/governor, HTTP/authentication, clock diagnostics, offline verification
Predecessor: ../closed/cross-platform-port.md, accepted for released v1.0.1; newer polling Windows acceptance remains separate

## Purpose / Big Picture

Provide optional add-only registration polling with bounded randomized intervals, explicit client budgets and conservative stop/replay rules. Use local wall time for absolute targets and monotonic time for waiting. Keep default single-attempt registration, ordered ECRN/SCRN payloads and existing authentication compatibility.

The user activated this plan on 2026-10-05 and requested completion/review. Live OBS rate-limit/JWT testing is explicitly deferred until both workstreams finish and separate authorization is given. Older experimental macOS runtime acceptance is outside this plan. Do not reactivate superseded start gates.

## Scope

### In scope
- Finish authorized polling, local-clock diagnostics, bounded token renewal, setup/config compatibility and safe logging.
- Correct discovered defects first; verify current Mac/Linux sources and artifacts, then hand implemented Windows tests to the teammate.
- Preserve server characterization and its uncertainty without treating synthetic fixtures as live evidence.

### Out of scope
- Polling drops, automatic reconciliation/replay after ambiguous POSTs, bypassing controls or rotating identifiers.
- Live OBS calls, threshold probing, NTP packets/clock setting, global installs, personal configuration reads, Git mutation, remote workflows or publication.
- A guarantee of undocumented OBS limits, token lifetime or zero defects on an unexecuted native target.

## Repository Orientation

- include/polling_config.hpp: validated opt-in configuration, RFC3339 window, interval and budget contracts.
- src/registration_result.hpp: transport/application classification and ordered residual CRNs.
- src/polling.*: scheduling, sampling, token renewal, durable shared request governor, structured events.
- src/main.cpp: mode/flag integration, real dry-run preparation, cancellation and final dispatch guards.
- src/http.* and token.*: persistent curl sessions, transfer accounting, redirect cooldown protection, expiry hints and authenticated refresh.
- include/platform.hpp and platform adapters: read-only clock evidence, cancellation, private atomic storage and exclusive lock.
- setup/main.cpp preserves polling settings; packaging/example_config.json stays empty/sanitized.
- CTest includes polling_contracts, platform_polling and polling_execution alongside existing platform/transport/package fixtures.

## Invariants and Acceptance Criteria

1. Polling is opt-in, add-only, one registration in flight, with explicit budgets and finite [start,end) window. Never send after end or resubmit confirmed completed CRNs.
2. Preserve OBS registration/JWT endpoints, Bearer authentication, Origin/Referer, field names, order and 30-second registration timeout. Single-attempt mode sends once; registration redirects are disabled to prevent implicit POST replay.
3. Separate business rejection, infrastructure classification, replay safety, normal samples, backoff and budget delays.
4. Stop on unknown outcomes, malformed/incomplete aggregates, rate-limit/block evidence, permanent rejection, failed authentication, unreliable timing, interruption or exhaustion. Unknown POST outcomes require manual OBS checking.
5. TLS verification and auth cookies/redirects remain intact. Test-only loopback/CA seams stay out of production binaries.
6. No credentials, JWTs, cookies, raw responses or personal CRNs in structured logs/state/packages. Runtime state/logs/dependencies remain project-local and ignored.
7. Distinguish source completion, offline tests, available native execution and live compatibility. Windows PASS requires the teammate's actual run.

## Baseline / Starting Evidence

Characterization on 2026-10-04 used source/history, user reports, public documentation and one read-only local clock query, without OBS requests. Current source identity is HEAD 502d2dd plus the retained uncommitted review corrections. This supersedes stale clean-e5108e8/unimplemented claims.

### Server characterization retained from research

| Question | Evidence and confidence | Consequence |
|---|---|---|
| Registration request limit? | Official [registration notice](https://www.sis.itu.edu.tr/EN/student/course-schedules/202710/ungraduate-announcements.php) specifies a three-second wait; premature requests restart it. | 3000ms documented floor, not a safe sustainable rate. |
| Approximately 100/hour? | UNVERIFIED. [İTÜ's explanation](https://haberler.itu.edu.tr/haberdetay/2025/09/25/itu-kepler-obs-ogrencilerimize-daha-iyi-ders-kaydi-deneyimi-sunmak-icin-gelisimini-surduruyor) describes unspecified volume/window and a one-hour block. | Require explicit limits; no built-in 5–6-second or 100/hour operating preset. |
| Account, JWT/session, IP, endpoint or rolling scope? | UNKNOWN; “same source” is not a defined identity. | Client budget cannot account for other clients or promise server compliance. |
| Block response/status? | UNVERIFIED. [Community observations](https://github.com/AtaTrkgl/itu-ders-secici/issues/14) conflict on timeout/VAL21/counts. | Do not label VAL21 or every timeout as a verified rate limit. |
| Retry-After or rate headers? | No verified OBS sample. | Parse standard forms without claiming OBS returns them. |
| Unsuccessful CRNs counted? | Volume rule unknown; published short cooldown applies to each registration request. | Count all attempts, including rejection/failure. |
| Separate authentication/JWT limit? | UNKNOWN. | Share conservative client budget; bound refresh/login independently. |
| JWT lifetime or repeated-request validity? | UNKNOWN. | exp is an optional scheduling hint, not a verified lifetime or signature check. |

User observations: HTTP200 capacity rejection; timeout after one of three courses was registered; VAL03 means already registered. Source maps successResult to success, VAL06 to capacity and VAL02 to registration-time restriction. Display mappings are not complete wire contracts. Drop-result schema remains undocumented. No additional redacted rate/JWT captures were available. Live characterization is deferred, not a remaining implementation gate.

Local evidence: a previous Darwin ntp_gettime query returned TIME_OK, maxerror138884us and esterror500000us. This establishes an available reporting path, not actual UTC accuracy. Windows enabled/running W32Time is likewise not synchronization proof.

## Milestones

1. [x] Characterization and explicit client-limit policy.
2. [x] Configuration/setup preservation and typed result classification.
3. [x] Scheduler, distribution, persistent governor, replay/stop rules and cancellation.
4. [x] Local-clock defaults, diagnostics, bounded renewal and secret-free events.
5. [x] Final Mac/Linux suite/artifact acceptance and implemented Windows execution handoff.
6. [ ] Teammate Windows execution, result recording and plan closeout.

## Progress

- [x] 2026-10-05 — Source integration completed; local/loopback preflight passed on Mac and existing network-disabled Ubuntu container.
- [x] 2026-10-05 — Fixed stale JWT after admission waits, dispatch after slow persistence/logging, dropped auth cooldowns, shortened-history loss, dry-run preparation and callback lifetime.
- [x] 2026-10-05 — Added polling-only auth redirect Retry-After stop, durable v2 unresolved-transfer accounting, fresh clock health after budget waits and 33 execution scenarios. Focused Mac transport/auth2/2 and polling2/2 passed.
- [x] 2026-10-05 — Main prevents redirected POST replay, retains numeric transport diagnostics, guards dispatch after output and rechecks local target after small backward correction.
- [x] 2026-10-05 — Bounded independent source review found no additional concrete regression. Mac affected9/9 and full13/13 passed.
- [x] 2026-10-06 — RP-07 corrected post-dispatch persistence/logging outcome reporting;35 execution cases and final Mac affected9/9 PASS. Independent review found no further concrete regression in this correction.
- [x] 2026-10-06 — Final-source Mac13/13 and Linux12/12, both native/extracted archives, production seam checks and identical Ubuntu24 archive smoke PASS. Earlier input-drift refusal was resolved by real rebuilding, preserving provenance checks.
- [x] 2026-10-07 — Selective v1.0.1 reconciliation source complete; focused provenance, static workflow invariants, Mac affected2/2 and full13/13 (100.53s), new1.0.1 archive/seams PASS. All runtime/platform/dependency/helper input hashes match the previously verified polling tree; only intended integration files changed.
- [x] 2026-10-07 — Combined Linux affected2/2 (62.01s), full12/12 (273.77s), native/package/archive/seams and identical Ubuntu24 smoke PASS. Independent reconciliation review found no lost polling behavior/docs or unintended CI weakening; final plan wording corrected. No Git mutation or live OBS traffic.
- [ ] Teammate MSVC/MinGW, ZIP/seam and desktop acceptance remain NOT RUN for this corrected tree.

## Surprises & Discoveries

- Randomizing above the short cooldown cannot establish compliance with the unpublished volume threshold.
- Infrastructure failure and safe replay are independent: a timeout or 5xx can follow an applied registration.
- JWT validity can change during admission waits or filesystem/logging delays; preparation alone is insufficient.
- Redirect headers disappear when curl follows a chain; positive cooldown must stop polling auth before the next request.
- Admission timestamps expire slow/later requests too early. Completed chains are charged at completion; unresolved reservations survive crashes.
- Network-time enablement, kernel state and measured accuracy are different evidence.

## Decision Log

### Selective v1.0.1 reconciliation, 2026-10-06

The user fetched origin/main777e147 (peeled v1.0.1 tag) and explicitly requests assessed file edits, not pull/merge or wholesale replacement. Common ancestor is2df7c3b. Eight potentially affected local files are backed up below .deps/reconcile-v1.0.1/before/; source-identity.json records refs. Preserve all newer polling code and stronger receipt validation. Mac/Linux bounded tool/compile preflight PASS for this reconciliation; no downloads, live requests or Git mutations.

Incoming CP-39's explicit reconfiguration after deliberately deleted configure snapshots and useful changed-byte relink coverage are incorporated without replacing our stronger receipt assertions. Focused Mac provenance regression completed successfully in .deps/reconcile-v1.0.1/provenance-focused.log. Version1.0.1 and platform usage guidance are reconciled while preserving local-clock/polling semantics. The user chose to retain MinGW CI/release gating and adopt PR/manual/version-tag triggers; the incoming MinGW-job deletion is rejected. Teammate's successful released-port Windows/CI results do not establish Windows PASS for this newer polling tree. Released-port closure and current-feature acceptance remain distinct.

2026-10-07 resume preflight: Mac tools/SDK available. Initial Docker socket absence stopped Linux-dependent work; user started Desktop, the existing network-disabled container was restarted, and Linux native-tool compile preflight passed. The approval-review usage-limit interruption left one plan edit unapplied; it was explicitly retried after authorization resumed. No global runtime was substituted.

| Incoming file/change | Reconciliation decision |
|---|---|
| tests/build_provenance_tests.py / ff6ee7b,2cbbd92 | Adopt explicit snapshot recovery and changed-byte source-touch recompilation. Retain stronger local receipt/fingerprint validation, MSVC-only repeated-build exception and strict no-link/replacement controls; no production helper changes. |
| CMakeLists.txt | Adopt VERSION1.0.1; preserve polling source and all three new test targets. |
| .github/workflows/release.yml | Adopt PR/manual/v*tag triggers; keep MinGW job and release dependency per explicit user choice. Static comparison confirms all jobs/permissions/publishing logic unchanged. |
| README.md | Adapt platform setup/main/archive examples and version/release guidance; preserve local default, --server-time/--check-clock, polling safety and experimental older-macOS scope. Explain released v1.0.1 does not contain newer polling source. |
| AGENTS.md | Keep MinGW policy; update only released-port closure/current-feature acceptance pointers. |
| Incoming current/port plans | Reject stale polling-unapproved/server-time-default and pre-release-pending states. Close port against user-reported release acceptance; keep current polling Windows runbook here. |

Tracked changes were applied as assessed file edits. No pull, merge, checkout, index/branch update or publication occurred. Ignored backups preserve the pre-reconciliation contents. The source remains based on HEAD502d2dd; incorporating released files does not rewrite Git ancestry.

### Configuration and normal intervals

Missing/disabled polling means single-attempt mode. Enabled polling requires strict RFC3339 start/end with explicit offset, unique ordered nonempty add CRNs, empty SCRN and zero early lead. Preserve legacy time aliases outside polling.

| Setting | Contract/default |
|---|---|
| enabled | false |
| start/end | Required, start < end; dispatch window [start,end) |
| distribution | beta, alternatively uniform |
| min_interval_ms | 3000 minimum |
| max_interval_ms / expected_interval_ms | Required explicit finite values |
| beta_concentration | 6, finite positive |
| request_budget.count / window_seconds | Required positive client rolling budget |
| max_attempts | Required positive registration cap |
| backoff_base_ms / backoff_max_ms | 30000 / 300000, base <= maximum |

Beta uses p=(mean-min)/(max-min), alpha=concentration*p, beta=concentration*(1-p), with mean strictly between bounds. Uniform requires midpoint mean. Independently seeded samples have bounded integer-millisecond rounding; no repeating sequence. Bounded sampling failure stops safely. Truncated normal adds no demonstrated benefit here.

Delay starts after response completion. Show nominal mean and 3600000/expected_interval_ms approximate attempts/hour; transfer/budget/backoff time lowers actual throughput. Explicit configured values are not an OBS-approved rate.

### Admission, response and persistence

One governor covers auth and registration. Reserve the maximum authentication redirect chain, then release only determinably unused counts. Registration reserves one with redirects off. Persist before dispatch and recheck cancellation/window/clock/JWT after waiting, writing and logging.

State version2 stores completion timestamps, unresolved reservations, retained-history horizon and cooldown. Unresolved recovery charges the full reservation from restart; v1 migration waits one full retained horizon because old timestamps cannot identify in-flight requests. Shrinking a configured window retains history; expansion waits conservatively when older history may be absent. Private atomic writes and exclusive project lock prevent concurrent/restart reset. State has no credentials or registration-progress resumption.

| Outcome | Action |
|---|---|
| Complete successResult / user-confirmed VAL03 | Remove completed entries; stop when all satisfied. |
| Valid VAL06 / VAL02 aggregate | Normal randomized interval for residual CRNs; reset infrastructure streak. |
| Action-required rejection | Stop for user correction. |
| HTTP429 / confirmed blocking | Stop; persist valid cooldown, no automatic recovery probing. |
| Proven pre-dispatch DNS/connect failure | Bounded exponential backoff. |
| Timeout/disconnect, incomplete body, 5xx after uncertain POST | Stop unknown outcome; no replay. |
| Unknown/duplicate/missing/unexpected result, redirect or ambiguous overload | Stop conservatively. |

VAL14/VAL16/ERRLoad/VAL21 lack verified replay semantics. A response must account for the submitted set; HTTP200 alone is not success. Retry-After seconds/date is never clamped to backoff maximum. Polling auth stops before following a redirect carrying an active cooldown. Other auth redirects retain existing behavior.

### Clock, authentication and logs

Local time is default; --local remains compatible. --server-time retains seven-sample lowest-RTT Date sampling only in single-attempt mode. --check-clock needs no config, credentials or network. Positively unsynchronized evidence stops; unknown evidence warns. Legacy sampling does not override a negative health report.

Darwin reads ntp_gettime; Linux reads adjtimex and an optional trustworthy systemd marker; Windows reads W32Time registry/service status but leaves synchronization unknown without proof. No service changes, privilege escalation, localized command parsing or new clock estimator. Wall time determines absolute targets, steady time controls waits; >1s discrepancy stops, small NTP corrections are tolerated with final window/target checks.

Reuse JWT/session. Known exp must retain 35s headroom at dispatch. At most two refreshes and one full reauthentication per run; full login only after recognized authentication loss, never a generic403, block or ambiguous POST. Submission-enabled --test is rejected with polling; --test --dry-run bypasses waits but prepares through real HttpSession and sends zero registration POSTs.

--logs emits JSON Lines on stderr, human progress on stdout. Include wall/monotonic timestamps, attempt/counts, status/curl/category/replay evidence, recognized codes, chosen intervals/backoff, budget/cooldown, clock report, refresh and stop reason. Exclude raw data/secrets/CRN lists. Ignored logs/ and .itu-runtime/ never enter archives.

## Defects / Findings Ledger

| ID | Priority / status | Correction or disposition |
|---|---|---|
| RP-08 | P1; FIXED, Mac/Linux PASS | CP-39 recovery and real changed-byte recompilation combined with stronger local receipt assertions; focused and full regressions passed. Version/docs/workflow reconciled without stale polling/clock changes; full Mac/Linux artifacts and Ubuntu24 smoke passed. New polling Windows acceptance remains separate. |
| RP-07 | P1; FIXED, Mac/Linux PASS | Track unresolved registration after dispatch so post-transfer persistence/log failures preserve unknown-outcome advice; proven pre-dispatch failures remain distinct. Stop logging cannot erase the result. Added two scenario groups,35 total. |
| RP-01 | Activation CLEARED | Explicit user authorization superseded predecessor sequencing; keep newer polling Windows acceptance separate from the closed released port. |
| RP-02 | Evidence limitation; DEFERRED | Unknown server volume/auth contracts above; no live work in this task. |
| RP-03 | Feature prerequisite; FIXED | Strict aggregate classification, registration redirects off, local timing default. |
| RP-04 | P1 dispatch/JWT; FIXED | Recheck token/window/clock/cancellation after waits, persistence, logging and final preparation; deterministic regressions. |
| RP-05 | P1 budgets/cooldowns; FIXED | Auth headers including interrupted redirects persist; v2 completion/crash accounting and conservative horizon changes. |
| RP-06 | P1 orchestration; FIXED | Monotonic waits, scoped callbacks, real dry-run prepare, JSON-safe failures, transport diagnostics, no implicit single POST replay. |

No known unfixed source defect remains from the bounded review. Native Windows verification can still reveal platform-specific failures.

## Validation Plan and Results

| Gate | Status / evidence |
|---|---|
| Bounded preflight | PASS: local CMake4.4.3, Clang21/SDK26.5, Python3.12.3, SDK curl; Ubuntu22 GCC11.4/local CMake3.31.6/Python3.12.14, locked dependencies and network-disabled Docker. |
| Contracts/execution | PASS on Mac/Linux: config/setup round trips, strict classifier,35 deterministic execution cases, restart/slow persistence/redirect budget/auth headroom/clock/cancellation/logging and post-dispatch local failures. |
| Real loopback HTTP/auth | PASS on Mac/Linux: redirects/cookies/expiry,21 auth scenarios, final/truncated/redirect Retry-After, no replay, retained observer reset. |
| Application orchestration | PASS on Mac/Linux:32 flag combinations, eight future schedules, clock/cancellation/200ms correction, real dry-run prepare, redirected POST refusal, numeric diagnostics. |
| Reconciled Mac | PASS focused provenance plus affected2/2 and full13/13 (100.53s), native/extracted1.0.1 archive, checksums and production seams. Logs .deps/reconcile-v1.0.1/macos/ with receipt.json; focused regression .deps/reconcile-v1.0.1/provenance-focused.log. |
| Reconciled Linux | PASS configure/build, affected2/2 (62.01s), full12/12 (273.77s), native/extracted1.0.1 archive, ABI/checksums and production seams; tests as UID1000 in network-disabled Ubuntu22 AMD64 emulation. Identical archive PASS on Ubuntu24 AMD64 emulation. Build build/linux-review-20261001/reconcile-v1.0.1; logs .deps/reconcile-v1.0.1/linux/. |
| Pre-reconciliation archives and seams | PASS on both targets; identical Linux archive PASS on Ubuntu24 AMD64 emulation. Historical1.0.0 paths/SHA256 remain in ../closed/cross-platform-port.md, distinct from new1.0.1-base artifacts. |
| Windows | NOT RUN for corrected polling tree; teammate uses the runbook below, expected12/12 CTest on each compiler. Released-port Windows/CI success is recorded separately in ../closed/cross-platform-port.md. |
| Live OBS | NOT RUN; explicitly deferred outside acceptance, no compatibility/limit/lifetime claims. |

### Current reconciled artifact receipts

Both artifacts use schema2/version1.0.1/revision502d2dd2f31fceb8d7b80bb0ccbaf01bf5de553f with dirty=true. They verify this combined working tree, not the clean released tag777e147. Runtime/platform/dependency/helper source hashes match the prior verified polling implementation. Exact command logs, receipts and seam counts are under .deps/reconcile-v1.0.1/{macos,linux}/; focused provenance and pre-edit file backups are in the parent directory. Ubuntu24 used the same Linux archive bytes; no rebuild or network.

| Target | Archive | SHA256 |
|---|---|---|
| Mac | build/reconcile-v1.0.1/verified-package/itu-ders-bot-1.0.1-macos-arm64.tar.gz | 97abab775c6ea159c3b976cfc057c2c963116106a3eb1eddc41ae5c035c58090 |
| Linux | build/linux-review-20261001/reconcile-v1.0.1/verified-package/itu-ders-bot-1.0.1-linux-x64.tar.gz | c45267d18885c0433b2152320adbda73078c172f9b0551876c957605add41149 |

### Current polling Windows verification

All automated tests are implemented: transport, authentication, core_and_clock, application_flags, setup_terminal, setup_files, polling_contracts, platform_polling, polling_execution (35 scenarios), artifact_integrity, manifest_paths and build_provenance.

1. Transfer the complete corrected tree, including untracked tests/polling_execution_tests.cpp and all modified source/tests. Record source identity/dirty state and tool versions. Use existing native tools and project-local dependencies.
2. Preflight in x64 VS2022 developer PowerShell: python scripts/bootstrap.py --target windows-x64 --preflight-only. In native MinGW PowerShell/cmd: python scripts/bootstrap.py --target windows-x64 --toolchain mingw --preflight-only. If tool preflight fails, stop and record it; no global installs. Bootstrap locked dependencies locally if needed.
3. Run each command separately, stopping on failure. This validates the combined polling tree; released v1.0.1 results cannot substitute for this run. Repeat for windows-mingw-x64 in its native compiler environment. Expected affected9/9 then full12/12.

~~~powershell
$preset = "windows-x64"
cmake --preset $preset
cmake --build --preset $preset
ctest --preset $preset -R 'transport|authentication|core_and_clock|application_flags|polling_contracts|platform_polling|polling_execution|manifest_paths|build_provenance' --output-on-failure --parallel 3
ctest --preset $preset --output-on-failure --parallel 3
python tests/native_artifacts.py "build/$preset/bin" --target windows-x64
python scripts/package.py "build/$preset/bin" --target windows-x64 --output "dist/$preset"
$version = (Get-Content "build/$preset/bin/build-manifest.json" -Raw | ConvertFrom-Json).version
$archive = "dist/$preset/itu-ders-bot-$version-windows-x64.zip"
python tests/native_artifacts.py $archive --target windows-x64
Get-FileHash $archive -Algorithm SHA256
~~~

4. Check production/test seams. Generated production itu_core rules must exclude ITU_ENABLE_TEST_SEAMS and http_socket_windows.cpp; itu_core_test includes them. main/setup must link production libraries. MSVC: dumpbin /symbols build/windows-x64/Release/itu_core.lib; positive control itu_core_test.lib. MinGW: nm -C on libitu_core.a/main.exe/setup.exe, positive control libitu_core_test.a. Production must contain no TestOptions/itu_open_loopback symbols; missing/inconclusive tooling is BLOCKED.
5. On Windows10/11 desktops, run setup with synthetic credentials and explicit --env-path/--config-path below ignored .deps. Check Unicode, hidden password, menu/repeats, save/reopen, Ctrl-C/cancellation and terminal restoration. --check-clock is read-only; record evidence/unknowns without claiming actual accuracy. Do not run production main/dry-run against OBS.
6. Save logs under ignored .deps/verify-windows-<date>/<preset>/, record ZIP SHA256/source/toolchain/test counts and desktop/seam results in this active plan. On failure preserve diagnostics and prioritize the concrete fix; do not weaken assertions. Released-port gates are closed; this run checks new polling code and the combined CP-38/39 regressions. On success remove completed current_plan tasks and close this plan per repository convention.

If provenance fails again, capture new manifest field/hash diagnostics plus MSBuild Link/PreLinkEvent/PostBuildEvent output. Strict receipt corruption, direct finalizer, no-link byte/mtime stability, replaced output, preserved-mtime/configuration and failed/partial-build tests must remain enabled.

## Files / Artifacts Changed

Configuration/classifier, shared scheduler/main/HTTP/token code, platform primitives, setup preservation, CMake tests and README implement this feature. Review corrections are in src/polling.*, main.cpp, http.*, token.cpp, platform_posix.cpp and contract/execution/application/transport/auth fixtures. tests/polling_execution_tests.cpp must travel with the complete working tree. Preserve teammate port/provenance work and existing .agents permission changes.

## Handoff Snapshot

- Current objective: teammate Windows tests/results for the newer polling tree; selective v1.0.1 reconciliation complete.
- Last verified state: reconciled Mac13/13, Linux12/12, both1.0.1 artifacts/seams and identical Ubuntu24 archive PASS;35 polling and21 authentication scenarios covered offline; stronger receipt checks and incoming snapshot recovery preserved.
- Next actions: follow this plan's current MSVC/MinGW/desktop runbook; record results and close this plan after acceptance. Do not start live OBS automatically.
- Blockers: Windows native execution unavailable locally and assigned to teammate.
- Risks: live contracts remain unknown by explicit disposition; no guarantee of bug-free native Windows code.
- Preserve HEAD502d2dd plus all working-tree changes, ignored local tools/logs and supplied logs_100960878455; Git read-only.

## Outcomes & Retrospective

Source corrections, selective released-port integration, regression implementation and current Mac/Linux acceptance are complete. Only teammate Windows execution of the newer polling tree and resulting plan updates remain. The released v1.0.1 port is separately closed against the user's acceptance report. Keep this plan active until its own Windows acceptance or explicit disposition; Mac/Linux PASS does not prove Windows or live-service behavior. Live OBS rate-limit/JWT work remains separately deferred.

## Revision Notes

- 2026-10-07 — Selectively incorporated released Windows fixture recovery/version/platform docs, retained stronger local tests and polling semantics, kept MinGW CI per user choice, and adopted PR/manual/tag triggers. Closed released-port plan separately and moved current Windows instructions here; current combined validation recorded above.
- 2026-10-06 — Consolidated stale/unimplemented claims, duplicate activation/history and obsolete counts. Recorded RP-07 and final Mac/Linux/archive results; removed experimental older-OS gates and deferred live OBS. Only Windows execution/plan closeout remains.
- 2026-10-05 — Activated and implemented feature; review expanded execution coverage from19 to33 cases.
- 2026-10-04 — Characterized evidence and recorded explicit-budget, add-only, conservative-stop decisions.
