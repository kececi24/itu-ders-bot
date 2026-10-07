# macOS Apple Silicon port: testing and acceptance

## Goal, status, and scope

Created at the user's request after implementation on 2026-09-27. Establish evidence that the macOS port preserves the original course-registration behavior and works on supported Apple Silicon Macs. This is the active verification workstream, succeeding [the implementation plan](../closed/macos-port.md).

Status: local/offline checks previously passed; the user now reports successful live dry-run and successful real request submission. macOS 14/15 runtime acceptance is explicitly deferred by the user for now. A later live timeout with partial registration is under investigation; manual terminal confirmation and evidence closeout also remain. User-performed submission is evidence, not authorization for the agent to send additional registration requests.

In scope: focused regression review, native/offline tests, terminal smoke, archive verification, supported-OS evidence, and authorized live authentication using dry-run only. Fix demonstrated port regressions and rerun affected checks; avoid unrelated refactoring, new features, endpoint changes, or speculative response-schema changes.

## Constraints and execution gate

- Target macOS 14+, ARM64 only, Apple Clang, C++17, CMake 3.20+, active Apple SDK/system libcurl. No Windows compatibility layer or Intel support.
- Downloaded dependencies must remain project-local and gitignored. Do not install globally; warn before any unavoidable global installation. Current dependencies already exist.
- Git remains read-only. Do not commit, push, tag, change branches, publish releases, or trigger a workflow as a substitute for missing authorization.
- Before resuming substantial execution, inspect current instructions, manifests, this plan, and `.agents/current_plan.md`. Reuse valid preflight evidence; check only changed or previously failed prerequisites. On failure, record the exact blocker and stop the dependent work for user action. Do not repeatedly retry/install/elevate to resolve dependency failures.
- For live verification, credentials must be configured locally by the account owner. Never request passwords in chat or persist tokens, cookies, session URLs, personal CRNs, or authenticated response dumps in plans/logs/artifacts.
- Every live invocation must include `--dry-run`. Never run production `main` or `main --test` without it during this workstream. Automated registration-capable tests must use loopback-only fixture binaries. Real registration is outside this plan and needs separate explicit authorization.
- Keep primary ownership of planning/integration; delegate bounded reviews or test investigations when useful under AGENTS.md. Agents must return concise evidence, not raw sensitive output.

## Architecture to verify

`src/http.*` owns persistent libcurl handles, cookie/connection state, HTTPS/TLS policy, redirects, and prepare/perform separation. `src/token.*` owns an independent authentication session and the fixed OBS sequence. `src/main.cpp` shares the other session between clock sampling and registration, preserves credentials/configuration/flags, and prepares a single submission. `src/clock.*` handles seven samples, minimum RTT, local-time conversion, monotonic waiting, and scoped QoS. `include/console.hpp` and `setup/main.cpp` implement terminal input and atomic local configuration writes.

Test-only HTTP/CA seams are compiled into `itu_core_test` under `ITU_ENABLE_TEST_SEAMS`; production binaries must retain fixed endpoints and HTTPS enforcement. Scheduling/clock seams allow deterministic tests without changing the Mac's clock or contacting OBS.

## Existing evidence (baseline, not a fresh verification)

| Area | Recorded result | Evidence / limit |
| --- | --- | --- |
| Native build | Release configure/build passed on macOS 26.6.2 ARM64, Apple Clang 21 | Local CMake 4.4.3; SDK/system curl; does not prove macOS 14/15 execution |
| Offline regression | All six CTest suites passed in 27.33 seconds | `build/Testing/Temporary/LastTest.log`; generated log may be overwritten |
| Authentication fixtures | 14 scenarios passed | Synthetic redirects/cookies/form actions/identity/JWT/errors; not live OBS |
| Flags/scheduling | 16 flag combinations and eight future-target cases passed | Loopback submission checks and injected scheduling; account fallback included |
| Clock/terminal/files | Passed | Sampling, DST, lead/aliases, wall-clock jumps; PTY secrecy/restoration; atomic files |
| Native artifacts | Passed | ARM64, macOS 14 deployment target, system-only linkage, linker signatures, modes, safe startup |
| Archive | Creation, smoke, payload checksums, archive checksum passed | `dist/itu-ders-bot-macos-arm64.tar.gz` and adjacent `.sha256` |
| Live account | User-reported successful dry-run and real request submission | Exact flags/status/result codes not supplied; do not infer successful course enrollment from successful sending |
| Live clock sampling | Two user-supplied seven-sample logs selected lowest RTT | Selected offsets -76 ms at 130 ms RTT and -589 ms at 127 ms RTT; this verifies sampling operation, not subsecond clock accuracy |
| Supported OS matrix | Deferred by user | Workflow defined; actual macOS 14/15 execution and cross-version archive evidence remain absent |

Existing tools: `cmake-4.4.3-macos-universal/CMake.app/Contents/bin/{cmake,ctest}`, Apple toolchain/SDK, system OpenSSL, Python 3.12+ (prior CTest selected installed Python 3.14). Nothing was installed during implementation.

## Execution sequence

### 1. Confirm baseline and preflight

- Inspect read-only Git diff/status and test definitions to identify changes since the recorded passing build. Do not assume an existing binary/archive contains the latest changes.
- Confirm the local CMake path remains valid, native host/toolchain/SDK are available, and the Python stdlib fixture modules/system OpenSSL used by tests are usable if their environment changed.
- Check presence of local credentials/config without displaying contents. Do not create or overwrite personal configuration automatically.
- Record date, OS, architecture, compiler/CMake/Python versions, selected SDK, and applicable source state. Use existing commit ID plus working-tree status/diff description without creating commits.

### 2. Focused offline and native regression

Reuse unchanged successful checks. For a changed source/test/toolchain baseline, run:

```sh
CMAKE="$PWD/cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake"
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build --parallel 3
"${CMAKE%/cmake}/ctest" --test-dir build --output-on-failure --parallel 3
python3 tests/native_artifacts.py build/bin
```

Verify the following contracts using existing tests and direct source review; add tests only for demonstrated gaps:

- `transport`: GET/HEAD/POST option reset; intermediate cookies; final response headers/body/effective URL; connection reuse/session isolation; native redirect methods; cross-host Authorization isolation; chunked bodies; transport/status distinction; timeout; trusted/untrusted/hostname TLS paths; production cleartext rejection; preparation has no network effect.
- `authentication`: exact form fields/order and fixed endpoint sequence; landing Referer; query-preserving action resolution/UTF-8 encoding; first identity selection; cookies/redirects; missing fields/login failure/invalid JWT/status/transport failures; no sensitive diagnostics.
- `core_and_clock`: string/order-preserving add/drop payloads and missing SCRN; result mappings; actual credential precedence and `.env` parsing; seven attempts/intervals/lowest-RTT ties/invalid dates/fallback; DST/lead/milliseconds/past targets; real wait-loop behavior under injected forward/backward wall-clock jumps.
- `application_flags`: no submissions for every dry-run combination; `--test` suppresses scheduled waits; `--local` only suppresses sampling; 90-second resync/60-second token/final wait sequencing; account fallback; fatal failures nonzero; no application retry.
- `setup_files` and `setup_terminal`: config read/write shape and validation; literal credentials/duplicates/unrelated lines; 0600 mode and failed-write preservation; arrow wrap/Enter; hidden canonical password editing; EOF/errors/SIGINT/SIGTERM restoration; explicit nonTTY failure.
- Verify QoS activates only near final waiting/submission, failure remains nonfatal, and previous QoS restores before result parsing/exit prompt. Distinguish source-reviewed behavior from runtime-observed behavior.
- Inspect owned source/build/workflow paths for required Windows dependencies and review test/production separation. Do not rewrite vendored JSON solely because it contains unrelated platform branches.

Keep failure diagnostics bounded and synthetic. Fix the cause, rerun affected tests, and repeat the combined suite only after material integration changes.

### 3. Real terminal and timing smoke

Use a normal local terminal and synthetic credentials/config at disposable paths with `setup --env-path ... --config-path ...`; do not overwrite real files. Confirm rendering, arrows, password invisibility/editing, terminal restoration after interruption, and resulting file permissions. Record behavior only, not entered passwords.

Use offline clock fixtures for near-future timing and wall-clock changes. Do not change the system clock or invoke production registration to test timing. User-initiated real setup for live acceptance is separate from synthetic smoke.

### 4. Archive and supported-platform acceptance

After any binary/README/config-example change, regenerate the allowlisted archive:

```sh
python3 scripts/package.py
python3 tests/native_artifacts.py dist/itu-ders-bot-macos-arm64.tar.gz
(cd dist && shasum -a 256 -c itu-ders-bot-macos-arm64.tar.gz.sha256)
```

Confirm only `main`, `setup`, `data/example_config.json`, `README.md`, and `SHA256SUMS` are packaged; no personal files, downloaded tools, fixtures, or credentials. Preserve executable modes/linker signatures. Check archive instructions against extracted paths.

Obtain actual macOS 14 and 15 ARM64 test results through already authorized CI or available supported Macs. Build the archive on 14 and run that exact archive on 15; record run URLs/artifact SHA-256 and results. A deployment-target load command on macOS 26 is insufficient. If the only route requires a Git mutation/push or workflow action not authorized in-session, leave this gate pending and state the needed user action. Do not publish a release for verification.

### 5. Live OBS dry-run acceptance

Prerequisites: user-configured authorized local credentials and configuration, usable network access, prior offline/native checks passing. User enters secrets with `./build/bin/setup` in their own terminal. Review the command flags before each invocation; do not display config contents or enable raw curl tracing.

Run once per gate, sequentially:

```sh
./build/bin/main --test --dry-run --local
./build/bin/main --test --dry-run
```

First gate: successful authentication/JWT acquisition and payload/request preparation, exit zero, explicit no-registration message. Record observed identity selection only if it occurs; do not claim both identity paths were live-tested with one account.

Second gate: seven live clock attempts and successful lowest-RTT selection, authentication/preparation, exit zero, no-registration message. A zero-offset fallback keeps the application usable but does not satisfy successful live clock-sampling acceptance.

Record only date, flags, exit status, stage success/failure, sample counts/RTT/offset summary, and whether identity selection occurred. Infer live cookie/redirect continuity only to the extent successful authentication demonstrates it; exact sequence behavior remains supported by fixtures/source review, not unrecorded packet captures.

On auth/network/service failure, stop repeated live attempts, preserve a sanitized blocker, and distinguish connectivity, account/identity, changed service HTML, or port regression when evidence permits. No automatic retry campaigns, real course submission, or secret-bearing dumps.

## Acceptance checklist and remaining work

- [x] Implementation baseline: native Release build and six offline suites recorded passing.
- [x] Baseline native artifact/archive smoke and checksums recorded passing.
- [ ] Reconcile source/environment changes and evidence freshness at verification start.
- [ ] Complete actual normal-terminal synthetic setup/timing smoke; distinguish from existing PTY automation.
- Deferred by user: obtain macOS 14 ARM64 build/test and macOS 15 ARM64 test results; not a current acceptance blocker.
- Deferred by user: smoke-test the exact macOS 14 archive on macOS 15; no compatibility claim inferred.
- [x] User reports successful live dry-run, supporting live authentication and preparation on the tested account. Exact invocation and independent agent observation unavailable.
- [x] User-supplied logs demonstrate successful live sampling; user separately reports working dry-run. Exact combined command/exit status not captured; do not claim a specifically observed combined run.
- [ ] Finalize evidence, residual limitations, and acceptance outcome.

Current remaining work: manual normal-terminal setup confirmation and final evidence reconciliation. The earlier missing-account blocker is superseded by the user's successful live tests. macOS 14/15 evidence is deferred explicitly; do not represent it as passing. No repeated real submissions are needed for verification.

## Evidence and closeout

Keep concise results and material decisions in this plan, immediate next tasks in `current_plan.md`, and nonsensitive generated logs under ignored `build/`. Date every new observation and link available CI runs. Preserve relevant failure/fix context without accumulating raw logs. No new test execution occurred when this plan was created.

Close only after the acceptance checklist is satisfied or the user explicitly changes/abandons its scope. Record the outcome and any waived gates, remove the entry from `current_plan.md`, and move this plan into `exec_plans/closed/`. The predecessor's archive means its implementation scope is finished and remaining acceptance was transferred, not that live compatibility has already passed.

## User acceptance update

The user reports that both `--dry-run` and real tests work, describing the latter as the request being sent successfully. Accept this as user-observed live authentication/preparation and submission evidence, without inventing command flags, HTTP/result codes, successful enrollment, exact timing accuracy, or identity-path coverage. No agent-side live requests were made to reconfirm it.

The user explicitly permits ignoring macOS 14/15 compatibility for now. Defer both the OS matrix and same-archive cross-version gate; retain them as future distribution checks. Do not change the build deployment target or claim supported-OS runtime verification.

Two earlier user-supplied clock logs show functioning seven-sample/minimum-RTT selection but a 513 ms difference between selected offsets. Local scheduling is the user's preferred direction; no source/default-flag change was requested or made. HTTP Date sampling is not accepted as proof of 100–200 ms accuracy.

For closeout, confirm normal-terminal setup behavior (menu, hidden password, restore after Ctrl+C, config persistence) if the user has not already observed it. PTY/files tests already cover these paths automatically. Reuse passing regression evidence unless code/toolchain changes justify new runs. A successful send does not by itself prove OBS accepted individual add/drop operations; checking an existing result or course list is sufficient if enrollment confirmation is desired, with no extra submission required.

## Timeout investigation (2026-09-28)

The user reports a macOS transfer timeout after one of three courses registered. A Windows friend also saw WinHttpReceiveResponse failure; numeric code is unavailable. Prior live-success acceptance is qualified: OBS can apply changes without a complete response reaching the client. Actual server cause is unknown.

Preflight passed on the same native Mac (AppleClang21, local CMake4.4.3, SDK curl headers/library, Python HTTP/TLS modules, loopback socket). Read-only comparison confirms the original Windows and current macOS implementations both send one ECRN/SCRN batch and parse per-course outcomes only after receiving a response. Do not blame batching or failed CRNs without service evidence. Windows failure function alone is not an error classification.

Diagnostic correction implemented: typed HttpTransportError with safe numeric curl code/elapsed time/observed status/received body bytes; registration-specific unknown-outcome guidance. Synthetic offline tests cover mixed accepted/rejected results, request applied then no response, and partial/incomplete response. Keep 30-second timeout, endpoint, payload, and no-retry behavior unchanged. No live calls.

User-deleted data/example_config.json observed in Git status; preserve it. Do not regenerate the release archive without its required example input. Existing archive predates these diagnostic edits. First planning-write approval review timed out; this is its single permitted retry, not a dependency failure.

### Timeout investigation outcome (2026-09-28)

- Source comparison confirms batching and per-course parsing were preserved from Windows. No code path turns a normal returned rejection code into a transport timeout. Windows receive failure cannot be classified without GetLastError; user cannot provide it.
- Added `HttpTransportError` in `src/http.*` with curl code, total elapsed milliseconds, last observed HTTP status (0 absent), and buffered response body byte count. No response content, session URL, headers, secrets, or server-derived error buffer are logged. Numeric metadata is collected even when curl fails.
- `src/main.cpp` catches this type only around registration perform, states that the outcome is unknown/OBS may have applied some or all changes, advises checking OBS before retry, and rethrows for nonzero exit. Firing banner starts a new line to avoid leftover countdown text.
- Extended transport fixtures: applied synthetic POST then stalled before headers; status200 and partial body then stall; completed mixed-result JSON. Short test timeouts, exactly one application attempt, no secret disclosure. Application fixtures cover displayed success+VAL06, complete empty200 parsing failure, and incomplete response after POST with unknown-outcome warning/no retry.
- Local native build passed; transport fixture passed1.79s in worker; application_flags passed27.21s. Parent reran transport/authentication against integrated build and native artifact smoke. `git diff --check` passed. Test evidence lives in generated `build/Testing/Temporary/LastTest.log` (later runs overwrite prior suite logs).
- Kept single batch, endpoint/form/payload semantics, connection/session policy, default30-second timeout, and no automatic retry. No live OBS calls or personal config reads. README now documents ambiguity and sanitized diagnostics.
- These tests reproduce possible failure mechanisms, not OBS's actual internal behavior. No proof failed CRNs cause silence, a shared client defect, load, connection reset, or gateway issue. A longer timeout or per-CRN retries were not introduced without evidence.
- Rebuilt `build/bin/main` includes new diagnostics. Distribution archive remains the older artifact because `data/example_config.json` was deleted by the user; do not restore it silently or claim archive currency.

## Superseded by unified platform work (2026-09-30)

The user requested and approved one shared codebase and release for Windows x64, macOS ARM64, and Linux x64. Remaining verification transfers to `../active/cross-platform-port.md`; this plan is closed as superseded, not as proof that every original gate passed. macOS 14/15 runtime checks remain deferred. The user supplied a completed HTTP200 with two capacity rejections on 2026-09-29, confirming normal per-CRN rejection parsing against OBS; it does not identify the earlier timeout cause. Existing live reports are preserved; no new live request is authorized or required.
