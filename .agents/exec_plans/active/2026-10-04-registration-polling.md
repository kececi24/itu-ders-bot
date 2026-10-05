# Add bounded registration polling and reliable local-clock timing

Status: BLOCKED
Execution state: QUEUED; implementation has not started
Created: 2026-10-04
Last updated: 2026-10-05 09:27 Europe/Istanbul
Owner: primary agent after completion of the cross-platform port
Primary scope: shared registration classification/scheduling/authentication, native clock diagnostics, configuration/setup, offline tests and usage documentation
Predecessor: `cross-platform-port.md` in this directory; follow its recorded closed location after completion

## Purpose / Big Picture

Add optional, bounded, add-only course-registration polling and make reliable local wall-clock time the normal timing reference. Keep normal randomized polling separate from infrastructure backoff and request-budget enforcement. Preserve single-attempt registration and manual/drop workflows.

**Execution dependency:** the user explicitly requires the cross-platform-port workstream to finish completely before this plan executes. Saving this plan does not authorize starting its implementation, changing timing defaults, or relaxing the current single-submission invariant. Activate only after the predecessor is marked COMPLETED with its acceptance/outcome record and moved out of active planning. Unresolved port failures must not be hidden by starting this feature.

**Current authorization boundary:** on 2026-10-05 the user required stopping after cross-platform-port and waiting for explicit authorization before executing this plan. Both predecessor completion and separate user authorization are required. This successor remains queued; do not start polling implementation as part of the port or automatically after it closes.

## Scope

### In scope

- Preserve the approved characterization, uncertainties and user decisions below; recheck source drift and time-sensitive official guidance when this queued work begins.
- Optional ECRN/add polling within an absolute start/end window; per-CRN completion tracking; independent bounded interval samples; separate error backoff and shared client request budget.
- Local-clock defaults in both modes, best-effort native synchronization diagnostics, monotonic waiting, bounded JWT renewal and secret-free structured logs.
- Existing JSON configuration/setup, backward compatibility, offline fixtures, supported native builds and artifact verification.

### Out of scope

- Polling SCRN/drop operations, probing an exact production rate threshold, automatic enrollment reconciliation after ambiguous POSTs, or automatic replay of unknown outcomes.
- Bypassing controls, rotating accounts/IPs/sessions/tokens to evade a block, imitating human behavior to defeat detection, CAPTCHA work, or uncontrolled load testing.
- Automatic clock setting, NTP/service reconfiguration, elevation, global installations, or new complex OBS HTTP-Date estimation.
- Live OBS/authentication/registration calls without separate account-owner authorization; Git mutation, remote workflow invocation and publication without explicit authorization.
- Reading personal `.env`, `data/config.json`, raw authenticated captures, passwords, JWTs or session cookies during implementation verification.

## Repository Orientation

- `src/main.cpp` builds ordered ECRN/SCRN batches and performs one registration POST; its current result display is not a complete-success predicate.
- `src/response.hpp` maps result codes to display messages. It does not define verified retry/replay semantics.
- `src/http.*` provides persistent libcurl sessions, final headers/status/body and typed transport diagnostics. Automatic redirects currently apply to registration as well as authentication.
- `src/token.*` maintains a separate authentication session and obtains a compact JWT through the existing login/context/JWT sequence. There is no current lifetime or refresh model.
- `src/clock.*` already separates wall and steady clocks for final waiting, but normal startup/resync performs HTTP-Date sampling and the earlier authentication wait uses wall-clock sleeping.
- `include/platform.hpp` and native adapters own platform operations. Put clock diagnostics/cancellation/locking there rather than adding OS branches to the shared flow.
- `setup/main.cpp` currently reconstructs configuration from time/courses; adding sections requires preservation during later edits. `packaging/example_config.json` remains sanitized with empty courses and zero lead.
- Offline transport/authentication/application/core fixtures use synthetic credentials and loopback only. The predecessor covers native build, dependencies, provenance, package and release mechanics, including MSVC and MinGW Windows paths.

## Invariants and Acceptance Criteria

1. Complete the predecessor first. The first implementation step must perform a bounded preflight of the actual native tools/services needed; do not assume old tool/service evidence is current.
2. Keep endpoints, bearer authentication, Origin/Referer, payload field names and CRN ordering compatible. Registration remains `POST https://obs.itu.edu.tr/api/ders-kayit/v21`, Origin `https://obs.itu.edu.tr`, Referer `https://obs.itu.edu.tr/ogrenci/DersKayitIslemleri/DersKayit`; JWT endpoint remains `/ogrenci/auth/jwt`.
3. Polling is disabled by default. It must require valid explicit operating intervals and budgets, refuse SCRN, and never infer that 100 requests/hour is an OBS contract.
4. Exactly one registration is in flight. Confirmed completed CRNs leave the pending set permanently for that run. Stop after all requested adds are satisfied.
5. Business rejection uses normal randomized scheduling, not exponential backoff. Transient classification alone never authorizes replay of a possibly applied POST.
6. Preserve the 30-second total registration timeout and unknown-outcome/manual-check guidance. Do not send new submissions at or after the window end.
7. Keep TLS peer/hostname verification, authentication cookies/redirects and production exclusion of test seams. Never respond to blocking by rotating identifiers.
8. No secrets or raw authenticated content in diagnostics, fixtures, plans, manifests or packages. Logs and local budget state stay ignored/private; no dependencies are installed globally.
9. Record source completion separately from executed native/fixture/live results. Tests must prove admission/stop behavior, not merely mirror implementation.

## Baseline / Starting Evidence

Characterization on 2026-10-04 used tracked source/history at `44ce24c`, user observations, primary public documentation and a read-only local kernel-clock query. No OBS request, NTP packet, system-clock change or application test campaign was performed. At persistence the checkout is clean `e5108e89a85639c70e0452b0bfbc5c3d20380ee4`, incorporating the teammate's MinGW extension; the feature remains unimplemented. Rebase the analysis on the completed port before coding.

### Server characterization

| Unknown | Evidence / confidence | Implementation consequence |
|---|---|---|
| Does a request-rate limit exist? | **Officially documented.** Current registration notice specifies a 3-second wait after each registration request; premature requests restart it. [Current İTÜ notice](https://www.sis.itu.edu.tr/EN/student/course-schedules/202710/ungraduate-announcements.php). | Treat 3 seconds as a documented registration floor, not a sustainable polling rate. |
| Is the volume limit 100 requests/hour? | **UNVERIFIED.** İTÜ describes an unspecified count/window followed by a one-hour block. Block duration does not identify the counting window. [İTÜ explanation, 2025-09-25](https://haberler.itu.edu.tr/haberdetay/2025/09/25/itu-kepler-obs-ogrencilerimize-daha-iyi-ders-kaydi-deneyimi-sunmak-icin-gelisimini-surduruyor). | No built-in 5–6-second or 100/hour operating preset; require explicit client limits. |
| Account, JWT/session, IP, endpoint, rolling window or other scope? | **UNKNOWN.** Official wording says the same source without defining its identity. | Client budgeting cannot promise compliance with a hidden shared/server-wide limit or account for other clients. |
| What is the rate-limit response? | **UNVERIFIED.** Firsthand community comments mention timeouts and VAL21, with conflicting counts/durations. [Issue and observations](https://github.com/AtaTrkgl/itu-ders-secici/issues/14). | Do not label VAL21 or every timeout as confirmed rate limiting. Stop conservatively on ambiguous responses. |
| Retry-After / rate-related headers? | No verified OBS sample available. | Support standard parsing without claiming OBS sends these fields. |
| Do unsuccessful CRNs count? | Not established for the volume threshold; official cooldown wording applies to each registration request. | Count every client attempt regardless of business result or transfer failure. |
| Separate authentication/JWT limits? | **UNKNOWN.** No official contract or sanitized observation found in this bounded research. | Include authentication in a conservative client budget and bound refresh/login calls independently. |
| JWT lifetime / repeated-request effects on validity? | **UNKNOWN.** Current code checks compact-token syntax only. | Never invent a lifetime or reauthenticate every polling cycle. |

The user confirmed there are no additional already-redacted rate-limit/JWT-expiry captures. Exact threshold discovery would require additional external evidence or separately authorized live activity; this plan does not pursue it. Any later characterization must be low-rate, bounded, stop immediately on blocking/overload/abnormal responses, and never rotate identifiers. Disruptive threshold discovery is not authorized.

### Existing application evidence

- Historical user reports include HTTP 200 with capacity rejection and a timeout after one of three courses registered. The latter establishes an unknown partial outcome, not the cause of the transport failure. See the closed macOS verification plan's timeout investigation.
- The user confirmed VAL03 means the relevant CRN is already registered. Their success/time-rejection messages are user-observed display text; they are not raw wire captures proving every code, header or status mapping.
- Source maps VAL06 to capacity, VAL02 to registration-time restriction, and successResult to success. Preserve these mappings with their source/user evidence labels; do not describe the entire message dictionary as independently live-verified.
- Existing result parsing accepts structurally valid but empty/incomplete/all-rejected lists and returns zero. Existing fixtures exercise display/orchestration, not complete fulfillment of all requested operations.
- Drop-result schema is explicitly undocumented. The user chose add-only polling and manual/existing single-attempt drops.
- Automatic redirects are enabled for all requests; a polling POST redirect can repeat a request before the scheduler grants another attempt. Authentication redirect behavior must remain separate.

### Local clock evidence

A single unprivileged Darwin `ntp_gettime` query returned `time_state=TIME_OK`, `maxerror=138884 us`, `esterror=500000 us`. This proves an available read-only reporting path on the current Mac; it does not establish actual UTC offset, subsecond accuracy, or macOS 14/15 execution. Network-time enablement, provider state and kernel synchronization evidence are distinct.

Primary references: [Apple automatic network time](https://support.apple.com/guide/mac-help/set-the-date-and-time-automatically-mchlp2996/14.0/mac/14.0), [Apple public timex interface](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/timex.h), [Windows W32Time diagnostics](https://learn.microsoft.com/en-us/windows-server/networking/windows-time-service/windows-time-service-tools-and-settings), [Ubuntu synchronization guidance](https://documentation.ubuntu.com/server/how-to/networking/timedatectl-and-timesyncd/index.html), [chrony monitoring](https://chrony-project.org/doc/4.3/chronyc.html).

## Milestones

1. **Dependency/authorization gate:** predecessor fully completed/closed and user explicitly authorizes polling; then refresh source/evidence and preflight. Exit: recorded authorization and activation checkpoint, with remaining OBS uncertainties still labeled.
2. **Contracts/configuration:** implement typed classification, pending-set completion, validation and setup round trips. Exit: offline result/configuration cases pass without enabling polling by default.
3. **Scheduling/admission:** implement bounded sampling, separate backoff, request budgets, locking, cancellation and stop rules. Exit: deterministic no-overlap/no-replay/no-out-of-window tests pass.
4. **Timing/authentication:** introduce local-clock default/diagnostics and bounded token renewal. Exit: clock and authentication scenarios pass with no routine OBS Date sampling.
5. **Observability/acceptance:** document behavior, emit safe structured events, run affected/full native suites and artifact checks. Exit: acceptance evidence recorded per supported target; live/server unknowns remain honest.

## Progress

- [x] 2026-10-04 — Read-only characterization and user decisions captured — PASS for the stated research scope, not live OBS validation.
- [x] 2026-10-04 — Accepted chat plan persisted as a separate queued ExecPlan; primary work remains the cross-platform port.
- [ ] Implement any feature milestone — BLOCKED on complete predecessor closeout and separate explicit authorization; no implementation has started.

## Surprises & Discoveries

- The three-second cooldown and the unspecified volume block are separate controls. Randomizing intervals or staying above three seconds cannot establish compliance with the unpublished volume limit.
- A temporary infrastructure failure can still have an unsafe-to-replay outcome. Classification needs replay safety as an independent dimension.
- The current exit status and display dictionary cannot serve as the polling success predicate; matching and completeness of per-CRN results must be checked.
- A network-time enabled flag is not proof of synchronization or accuracy. A container's missing time service is unavailable evidence, not proof the host clock is wrong.

## Decision Log

### User decisions, 2026-10-04

- Keep polling disabled by default and require explicit conservative limits while the volume threshold is unknown; do not block all development on discovering a hidden threshold.
- Stop after ambiguous registration outcomes for manual OBS checking; no automatic reconciliation feature in this version.
- Poll only adds. Refuse nonempty SCRN in polling rather than silently dropping configured operations.
- Terminate the run on explicit rate-limit/block responses; do not probe for recovery or automatically resume after Retry-After.
- Use local wall time in both modes; retain --local compatibility. When synchronization evidence is unavailable, warn and continue. Positively unsynchronized evidence stops scheduled submission.
- Execute this workstream only after cross-platform-port is completely finished and the user separately authorizes registration-polling.
- Complete the now-authorized predecessor before starting this feature.

### Configuration and distribution contract

Extend the existing configuration with a `polling` section; missing/disabled polling keeps single-attempt registration. Validate before authentication and preserve new settings when setup updates courses/time.

| Setting | Contract / initial default |
|---|---|
| `enabled` | false |
| `start`, `end` | Required when enabled; absolute RFC 3339 timestamps with explicit UTC offset; start < end |
| `distribution` | beta; also support uniform |
| `min_interval_ms` | 3000; reject values below the documented registration floor |
| `max_interval_ms`, `expected_interval_ms` | Explicit finite user values required; no automatic operating-rate preset |
| `beta_concentration` | 6; finite and positive |
| `request_budget.count`, `request_budget.window_seconds` | Explicit positive user values; rolling client safety guard, not an OBS limit claim |
| `max_attempts` | Explicit positive total registration-attempt limit |
| `backoff_base_ms`, `backoff_max_ms` | 30000 and 300000; positive base <= maximum |

Reject empty/duplicate add CRNs, nonempty SCRN, invalid or unrepresentable dates/durations, nonzero early-send lead in polling and inconsistent distribution parameters. Keep legacy single-attempt local calendar fields/aliases compatible.

For beta, set `p=(expected-minimum)/(maximum-minimum)`, `alpha=concentration*p`, `beta=concentration*(1-p)` and require `minimum < expected < maximum`. Uniform requires expected to equal the midpoint. Use independent samples from a process-seeded generator, with an injected deterministic source only for tests; do not replay predefined sequences. Truncated normal is not needed initially because clipping/truncation complicates preserving the requested mean without a demonstrated benefit.

Normal delay runs from response completion to the next dispatch. Display bounds, nominal mean and approximate `3600000/expected_interval_ms` requests/hour; response time and safety delays reduce the actual rate. Reassess the floor if authoritative guidance changes before activation. No field is an assertion of an accepted sustained OBS rate. Randomness distributes attempts, not evades detection.

### Admission, budget and response contracts

Use one shared client governor across registration and authentication. Count successful/rejected/failed requests, initial authentication, refresh and redirects. Registration reserves one request with redirects disabled. For each authentication transfer, reserve its maximum redirect chain (one plus configured redirect cap), then retain actual observed usage and release the unused reservation only when determinable; retain the conservative reservation on uncertain failure. Stop before dispatch if admission is impossible. Do not call the registration cooldown a verified login/JWT requirement.

Persist budget timestamps and valid server cooldown deadlines privately in ignored project/runtime storage, with an exclusive instance lock and atomic writes. A restart must not silently reset the guard. Persisted state contains no tokens, credentials or personal course lists; it is not automatic registration-progress resumption. Other browsers, installations or shared-IP traffic remain outside this guard's knowledge.

Normal samples, capped exponential infrastructure backoff and budget waits are separate. A valid application response resets the infrastructure streak. Retry-After is parsed as delay-seconds or HTTP-date and is never shortened to fit the backoff maximum. Safety waits can exceed the normal interval maximum; no catch-up bursts. [HTTP retry/delay semantics](https://www.rfc-editor.org/rfc/rfc9110.html#section-10.2.3).

Typed outcomes must separate transport metadata, HTTP category, per-operation application results/completeness and replay safety:

| Result | Action |
|---|---|
| Recognized success / user-confirmed VAL03 for the submitted CRN | Mark satisfied and remove from future payloads; preserve remaining order. |
| VAL06 capacity / VAL02 time rejection | Keep pending, reset infrastructure backoff, use normal randomized delay. |
| Confirmed permanent or action-required rejection | Stop and explain the required action. |
| Explicit rate limit/block, including HTTP 429 | Terminate; report and retain valid cooldown information. |
| Proven failure before registration dispatch | Bounded exponential backoff; if dispatch safety cannot be established, stop instead. |
| Timeout/disconnect/incomplete response or registration HTTP 5xx with uncertain application outcome | Classify as appropriate, then stop for manual verification; no POST replay. |
| Unknown code, ambiguous overload/in-progress result, unexpected redirect or malformed aggregate | Stop without resubmitting. |

Treat VAL14, VAL16, ERRLoad and VAL21 conservatively until replay semantics are characterized. Zero received bytes or a curl code alone is not proof nothing was applied. Every response must account for exactly the submitted CRNs without missing/duplicate/conflicting/unexpected entries. Record unambiguous completed entries but stop the aggregate if coverage is invalid. HTTP 200 or process exit zero never proves all requested adds succeeded.

### Clock, authentication and user interface contracts

- Introduce a native `ClockHealth` report with independently optional enablement/synchronization/provider/age/error evidence and unavailable/permission-denied reasons. macOS uses read-only ntp_gettime plus accessible configuration; Windows uses bounded W32Time queries; Linux uses kernel and available systemd/chrony reports. Use no automatic elevation, NTP probes, installs or service changes. Unknown/localized/unparseable data cannot become an invented success.
- Local wall time becomes the default in both modes. Retain --local; expose old coarse HTTP-Date sampling only as explicit --server-time for single-attempt mode and reject it with polling. Add --check-clock with no credentials or OBS requests. Remove normal initial/resync HEAD sampling; retain legacy sampling behavior only on explicit selection.
- Resolve absolute start/end with wall time and use steady_clock for waits, interval/backoff timers and elapsed logging. Recheck the wall window before dispatch. Handle wall/steady discontinuity or resume conservatively: recheck clock/window, stop when timing is unreliable, and never compress intervals or burst overdue attempts.
- The submission window is [start,end). An already-started transfer may finish within the existing 30-second timeout. Cancellation while in flight reports unknown outcome. Timing preferences remain scoped, not active throughout idle/backoff periods.
- Introduce an auth-token result with bearer value and optional expiry metadata. Decode exp only as a scheduling hint, never as client-side signature validation and never for logging; absent expiry remains unknown. Reuse session/JWT between attempts.
- Before known expiry, try the existing JWT endpoint with the authenticated session. Require enough remaining lifetime for the transfer timeout plus a documented small margin. Bound refresh and full reauthentication independently: initial defaults two JWT refresh attempts and one full reauthentication per run. Stop on unusable/repeated near-expiry replacements rather than creating an authentication loop.
- Full reauthentication requires recognizable authentication failure; do not treat generic403 as expiry, reauthenticate after unknown POST outcomes, or refresh to evade limits. Existing login/cookie/identity behavior must remain intact; uncharacterized expiry responses stop conservatively.
- Stop on all adds satisfied, window end, attempt/budget exhaustion, permanent rejection, unresolved authentication, rate limiting, unknown outcome, unreliable timing or user interruption.
- Reject submission-enabled --test with polling. Polling --dry-run validates time/configuration/clock/authentication/request preparation and exits with zero registration POSTs. --test --dry-run may bypass schedule waits but must not create a polling burst.

### Observability

Polling --logs emits JSON Lines on stderr; ordinary human console output stays on stdout. Include wall timestamp, monotonic elapsed time, attempt/operation indices, HTTP/curl status, category/replay safety, recognized application codes, pending/completed counts, sampled delay, effective deadline, backoff/budget state, parsed Retry-After, clock evidence, JWT refresh events and start/stop reason. Do not log passwords, JWT contents/claims, cookies, raw bodies, session URLs, arbitrary unknown server text or personal CRN lists. Document an ignored logs directory; packaging excludes runtime logs/state.

## Defects / Findings Ledger

| ID | Type / component | Severity | Status | Evidence | Disposition |
|---|---|---|---|---|---|
| RP-01 | Predecessor and authorization | Execution gate | BLOCKED | User requires full port completion followed by explicit polling authorization; native Windows acceptance remains pending there. | Keep this plan queued after port closeout until separately authorized. |
| RP-02 | Server rate/auth contract | Evidence limitation | UNVERIFIED | Characterization table; no additional sanitized captures supplied. | Explicit client limits, conservative stops and bounded auth; no unsupported server claims. |
| RP-03 | Current parser/redirect/clock behavior | Feature prerequisite | NOT IMPLEMENTED | Repository orientation and application evidence. | Address within milestones after activation; not a reason to expand current port work. |

## Validation Plan and Results

Only read-only characterization/plan persistence is complete. No feature build, test or live-service PASS is claimed.

| Gate | Method / scenarios | Status |
|---|---|---|
| Research/evidence boundary | Source/history, cited primary guidance, user confirmations, one read-only kernel query | PASS for stated scope; live behavior remains PARTIAL/UNVERIFIED above |
| Configuration/setup | Legacy inputs, disabled mode, strict interval/window/budget validation, SCRN refusal and preservation on setup round trips | NOT RUN |
| Classification/completion | Partial success -> residual-only payload; VAL03; all rejected; missing/duplicate/conflicting/unknown entries; permanent and ambiguous results | NOT RUN |
| Scheduler/governor | Injected wall/steady/random/transport; bounds/mean/independent samples, rolling budget, authentication reservations, restart/lock/state failure, no overlap or catch-up | NOT RUN |
| Error/replay policy | Business response without backoff; proven pre-dispatch backoff; unknown POST stop; 429 stop; Retry-After forms, invalid values and cooldown beyond window | NOT RUN |
| Clock/window/cancellation | Enabled-but-unsynchronized, absent/denied/malformed providers; clock steps, suspend/resume, boundary milliseconds, no late dispatch, interrupted transfer | NOT RUN |
| Authentication | Reuse, known/missing exp, refresh, unusable/repeated replacement, full-login bound, no every-cycle auth or renewal on blocking | NOT RUN |
| Flags/logging | Default/--local no HEADs, explicit legacy option, dry-run zero POSTs, dangerous --test combination refusal, structured events without synthetic secrets | NOT RUN |
| Available native verification | Matching preset preflight/build; affected then full suites; production test-seam exclusion, native/extracted archives; identical Linux archive on Ubuntu24 | BLOCKED on activation |
| Windows / supported OS gates | Teammate MSVC and MinGW execution, Windows10/11 desktop evidence and required macOS runtime gates | BLOCKED on activation; predecessor results do not test this feature |
| Live characterization/compatibility | Only separately authorized, bounded observations with sanitized metadata and immediate stop conditions | NOT RUN / not authorized by this plan |

## Files / Artifacts Changed

- This file: persisted characterization, approved feature contracts, dependency gate and future verification.
- `.agents/current_plan.md`: lists this as queued secondary work; cross-platform port remains primary.
- No application/configuration/build/test implementation or personal data changed while saving the plan.

## Handoff Snapshot

- Current objective: retain a decision-complete queued feature plan while finishing the cross-platform port.
- Last verified state: characterization above and explicit user choices; no polling implementation.
- Next actions: (1) finish/close the authorized predecessor; (2) stop and await explicit polling authorization; (3) only then refresh source/guidance/preflight and activate this workstream.
- Blockers: predecessor incomplete and polling not authorized; published volume threshold/auth contracts remain unknown and are handled by the accepted explicit-limit/stop policies.
- Preserve existing single-attempt behavior and all port fixes until activation. Any future AGENTS timing/retry-policy update must scope the exception to the new opt-in mode.

## Outcomes & Retrospective

Plan saved, not executed. Completion will require implemented contracts and recorded verification, not merely resolving the predecessor or creating this file.

## Revision Notes

- 2026-10-05 09:27 Europe/Istanbul — Added explicit post-port authorization gate at the user's request; no feature implementation performed.
- 2026-10-04 20:50 Europe/Istanbul — Reaffirmed queued status and recorded the user's explicit prohibition on further predecessor execution until authorization; implementation remains unstarted.
- 2026-10-04 14:38 Europe/Istanbul — Persisted the accepted chat plan with characterization/evidence limits and user-selected defaults; queued execution strictly after complete cross-platform-port closeout.
