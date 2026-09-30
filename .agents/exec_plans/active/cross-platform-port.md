# Unified Windows, macOS, and Linux port

## Goal and current status

Created 2026-09-30 from the comprehensive plan approved by the user. Implement one GitHub codebase, one version/tag, and one release containing native Windows x64, macOS ARM64, and Linux x64 archives. Setup may differ by OS. Status: implementation started; native macOS preflight passed. No cross-platform execution or new release is yet verified.

## Support and constraints

- Windows 10/11 x64 with MSVC2022; macOS ARM64/AppleClang with deployment target14+; Ubuntu22.04/24.04 x64 GCC11+. Other compatible glibc distributions are best effort. IntelMac, other CPU targets, musl and32bit are outside scope.
- Preserve endpoints, OBS authentication order, cookies/redirects/session reuse, JWT, one ordered ECRN/SCRN batch, config/env precedence and schema, seven lowest-RTT clock samples, target scheduling and four existing flags. Preserve30second timeout, unknown-outcome guidance and no automatic retry.
- All downloads, tools, sources, installations and caches stay below ignored project `.deps/` (existing local CMake may be reused). No global installs. Missing compiler/SDK stops dependent work for user action; bootstrap must not install them.
- Git is read-only. No commit/push/branch/tag/release publication or workflow invocation is authorized. Offline loopback fixtures only; no live OBS calls or real registration. Do not inspect `.env` or personal config.
- User-deleted `data/example_config.json` stays deleted. Add a new sanitized packaging template with empty courses and zero lead; never derive it from personal configuration.
- macOS14/15 runtime checks remain user-deferred. Existing user-reported macOS dry-run/submission and HTTP200 capacity-rejection evidence is retained, not invented for other platforms.

## Architecture and decisions

1. Shared registration/authentication/clock/HTTP core. OS implementations selected by CMake live in platform adapters; shared UI consumes semantic menu keys and UTF8 lines. Native APIs/OS headers stay in adapters and entry boundaries.
2. POSIX terminal behavior is retained. Windows uses native Unicode console input, cooked line/password editing, hidden input before prompt, cooperative CtrlC/Break cancellation and restoration. Scoped output/codepage modes restore. Redirected interactive setup fails explicitly.
3. Windows argv/environment are decoded UTF16 toUTF8; paths use filesystem UTF8 conversion. Environment preserves unset versus empty semantics. All three systems use the same JSON/env data.
4. Private atomic file write: same-directory0600/fsync/rename on POSIX; owner-restricted CREATE_NEW/DACL, checked WriteFile/FlushFileBuffers and same-volume replacement retaining temporary ACL on Windows. Failures preserve original and remove temporary; unsupported permission enforcement fails.
5. Shared monotonic deadline computation; scoped MacQoS, Windows best-effort thread priority/timer resolution, Linux normal priority. ARM yield/x64pause are adapter operations; restore after transfer. Local-time DST semantics retained.
6. libcurl on every OS, HTTPS/TLS verification retained. Mac SDK/systemcurl; Windows project-local static curl/Schannel/staticMSVC runtime; Linux static curl/OpenSSL with dynamicglibc and system UbuntuCA bundle. Pin zlib/nghttp2 too for compressedHTTP2 responses. API floor curl7.85, bundled supported curl8.x.
7. CMake3.25+/C++17, compiler-specific flags, platform presets, explicit dependency prefix, consistent bin/staging targets. BUILD_TESTING=OFF has no Python requirement. Bootstrap verified source archives from a committed exact URL/version/SHA256 lock; never Git package checkouts/global caches.
8. Three archives share version/revision: windows-x64.zip, macos-arm64.tar.gz, linux-x64.tar.gz. Allowlist main/setup, README, sanitized example, licenses, build manifest and payload checksums; outer archive hashes. Run from extracted root. No installer/updater/signing service in this iteration.
9. Native CI builds/tests all three. Ubuntu22 builds and Ubuntu24 smokes identical archive. Tag publishing aggregates all verified assets into one release only after all jobs pass. CI WindowsServer evidence is distinguished from Windows10/11 desktop acceptance.

## Work sequence and ownership

Primary owns integration, src/main/http/clock, repository policy/docs, planning and final evidence. Bounded workers: terminal/platform adapters and setup; build/bootstrap/package/CI; portable tests/artifact checks. Workers are not alone and cannot revert others' edits.

- [x] Read planning/instructions/manifests and bounded native preflight.
- [ ] Add platform interfaces/adapters and keep native macOS regression baseline.
- [ ] Implement Windows terminal/Unicode/files/timing and portable loopback sockets.
- [ ] Implement Linux POSIX reuse, dependency recipe and native build rules.
- [ ] Port existing fixtures; Windows console/DACL coverage; deterministic test-only PEM fixtures.
- [ ] Pin and implement project-local dependency bootstrap and presets.
- [ ] Unify allowlisted packages/checksums/manifests/licenses and one-release CI.
- [ ] Run affected then integrated local native checks, archive clean-directory smoke and inspect dependency locality.
- [ ] Update README/AGENTS/support evidence and record native CI/manual gates still pending.

## Preflight evidence (2026-09-30)

Native host DarwinARM64; localCMake4.4.3 at `cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake`; AppleClang21; GNUmake3.81; SDK `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk`; Python3.12.3 with stdlib HTTP/TLS/PTY/archive modules; loopback bind and SDKcurl compile/link/run passed. Scratch evidence in ignored `build/preflight/`. PLANS.md absent. No credentials/service connection needed. Windows/Linux native runners are not available on this Mac; native execution is a CI/manual gate, not claimed as passing.

## Validation and acceptance

Retain all six suites covering transport TLS/cookies/redirects/reuse/loopback restriction, synthetic14case authentication, payload order and clock/DST/walljumps,16flag/eightfuture orchestration, secure setup files, terminal secrecy/restoration. Port platform assumptions with actual semantics; use explicit UTF8 subprocess decoding and native Windows console/ACL checks. Test applied-then-stalled and partial responses with exactly one attempt and unknown-outcome advice. Test-only CA/socket options cannot enter production binaries.

Verify PE/MachO/ELF architecture, system-only expected linkage, no developer curl/VCruntime dependency, Mac min14/linker signatures, LinuxUbuntu22glibc baseline, safe archives/checksums/manifests and missingconfig/nonTTY startup before network. PEM fixtures remain local tests, never truststore-installed or packaged, and have checked SAN/expiry.

Done means implementation and locally available verification are complete, three native jobs are defined, generated archives are reviewable, dependencies are confined to the project, one-release aggregation is ready, and residual native/manual/live verification is accurately recorded. No successful-native-release claim until all native runners execute. Live dry-run on new platforms requires account-owner action; real registration is unnecessary for acceptance.

## Discoveries, decisions and evidence log

- 2026-09-30: Existing macOS port edits are uncommitted and preserved. Prior verification plan closed as superseded, with incomplete/deferred gates transferred here. Latest HTTP200 normal rejection is user evidence only; previous timeout cause remains unknown.
- 2026-09-30: Native dependencies usable, no installs. Parallel implementation ownership assigned. Explicit cross-platform request supersedes old macOS-only repository policy; update it with this support matrix.

## Blockers / next evidence

None for native macOS implementation. Windows/Linux native execution, desktop-console manual acceptance and new-platform live dry-runs remain external gates. No automatic retries or global dependency remedies.
