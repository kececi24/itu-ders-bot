# AGENTS.md

## Project Goal

This repository is the unified Windows, macOS, and Linux edition of the İTÜ course registration helper. Preserve the existing registration behavior through a shared C++ core and native platform adapters. One version/tag produces one release with three native archives.

## Supported Platforms

Target environment:

```text
Windows 10/11 x64: Visual Studio 2022 MSVC + Windows SDK
macOS 14+ ARM64: Apple Clang + macOS SDK
Ubuntu 22.04/24.04 x64: GCC 11+, make, Perl
Build system: CMake 3.25+, C++17
Bootstrap/tests/packaging: Python 3.12+
```

Intel Macs, other CPU targets, musl, and 32-bit platforms are outside scope. Other compatible glibc Linux distributions are best effort. Keep Win32/POSIX/Apple APIs in platform adapters and native entry boundaries; shared code consumes portable interfaces. CMake selects the adapters.

## Repository Map

Important paths include:

```text
.github/workflows/   CI workflows
data/                Runtime configuration
packaging/           Sanitized release template
cmake/               Dependency lock and explicit dependency resolution
scripts/             Project-local bootstrap and allowlisted packaging
tests/               Offline fixtures and native artifact checks
include/             Shared headers and terminal utilities
setup/               Interactive setup utility
src/                 Registration application
CMakeLists.txt       Build configuration
README.md            Build and usage documentation
```

Key implementation areas:

```text
src/main.cpp         Main registration flow and HTTP/session orchestration
src/token.cpp        OBS authentication and JWT acquisition
src/clock.cpp        Server clock sampling and target-time waiting
include/console.hpp  Interactive terminal behavior
include/platform.hpp Native platform interface
src/platform_*.cpp   Terminal, Unicode, files, and timing adapters
src/http.cpp         Persistent libcurl transport
setup/main.cpp       Configuration/setup utility
```

Inspect the repository before assuming this map is exhaustive.

## Porting Principle

Preserve the behavior of the original Windows version, not its implementation details.

Use portable C++ for shared behavior and native APIs for platform operations. Do not duplicate authentication, payload, clock, or scheduling logic by OS.

Prefer:

- standard C++ APIs
- POSIX APIs available on macOS/Linux in the POSIX adapter
- Win32 APIs in the Windows adapter
- scoped Apple/Windows scheduling preferences where appropriate
- libcurl for HTTP networking

Do not retain dead platform code merely for historical compatibility.

## Networking

Use libcurl on all three platforms: Apple SDK/system curl on macOS, project-local static curl with Schannel on Windows, and project-local static curl with OpenSSL on Linux. Keep TLS peer/hostname verification enabled. Test-only CA and loopback socket seams must not enter production binaries.

The HTTP layer must preserve the behavioral requirements of the original application:

- persistent HTTP session
- connection reuse where applicable
- cookies
- redirects
- TLS
- custom request headers
- GET, HEAD, and POST requests
- HTTP status handling
- response body access
- response header access
- authentication state across requests

Authentication flows may depend on cookies and redirects spanning multiple requests. Do not implement HTTP calls as unrelated stateless requests.

## Functional Invariants

All supported platforms must preserve:

- OBS authentication flow
- ASP.NET form token extraction
- identity selection when required
- JWT acquisition
- CRN add operations
- SCRN/drop operations
- `.env` credential loading
- `data/config.json` compatibility
- server clock synchronization
- multiple clock samples
- lowest-RTT sample selection
- target-time execution
- `--dry-run`
- `--test`
- `--local`
- `--logs`
- interactive setup utility
- hidden password input

Do not change OBS endpoints, request fields, request ordering, authentication sequencing, or registration payload semantics without a demonstrated compatibility reason.

## Scope Discipline

This task is a platform port, not a redesign.

Do not perform unrelated:

- feature additions
- endpoint changes
- payload changes
- large-scale refactors
- formatting sweeps
- UI redesigns

Refactoring is appropriate when required to isolate platform infrastructure cleanly. Keep platform conditionals out of shared application flows when an adapter can express the operation.

## Dependencies and Distribution

Reuse existing native compilers/SDKs; if one is missing, stop that platform's dependent work and report it. Never install compilers, SDKs, or dependencies globally. Keep downloaded tools, archives, sources, builds, caches, and installations below ignored project `.deps/`. Bootstrap exact source URL/version/SHA-256 lock entries with TLS verification; do not bypass TLS, replace failed hashes, or silently fall back to global libraries.

Preserve the deleted source `data/example_config.json`. Package `packaging/example_config.json` into archive `data/example_config.json`; it must contain empty courses and zero lead, with no account data. Packages allow only native main/setup executables, README, that example, required licenses, build manifest, and checksums. Never derive package inputs from personal configuration.

One release contains versioned `windows-x64.zip`, `macos-arm64.tar.gz`, and `linux-x64.tar.gz` archives with matching version/revision. Validate architecture, expected system linkage, extracted startup, payload/outer checksums, and sanitized contents before publication. Defining release CI does not authorize invoking it or publishing.

## Security

Never commit:

- passwords
- JWTs
- session cookies
- authenticated response dumps
- `.env` secrets
- personal course-registration data

Treat captured authentication traffic as sensitive.

## Verification

Before considering the cross-platform port complete, verify on each available native target and record missing execution gates explicitly:

1. The matching CMake preset configures successfully.
2. The project builds successfully with the supported native compiler.
3. Native headers/libraries remain confined to selected adapters; dependencies resolve from the SDK or project-local prefix.
4. `setup` correctly reads and writes configuration.
5. Password input remains hidden.
6. Authentication reaches JWT acquisition.
7. Cookies and redirects survive the full authentication flow.
8. Clock synchronization retains the intended sampling behavior.
9. `--dry-run`, `--test`, `--local`, and `--logs` behave correctly.
10. Registration payload generation matches the original implementation.
11. Real registration requests are not sent during automated verification unless explicitly authorized.
12. README describes each supported target's build, setup, usage, and archive process.
13. Affected tests pass before the complete offline CTest suite and archive verification.
14. Linux archives satisfy the Ubuntu 22.04 glibc baseline; identical archives smoke on Ubuntu 24.04 and macOS 15 as defined in CI. Windows Server CI is distinct from Windows 10/11 desktop acceptance.

If live OBS verification is unavailable, clearly distinguish static/code-level verification from behavior confirmed against the real service.

Automated verification uses synthetic credentials and loopback endpoints only. Preserve the 30-second registration timeout, unknown-outcome advice, and exactly one submission attempt; never add automatic retries. Do not inspect personal `.env` or `data/config.json` during port validation. Live OBS calls and real registration need explicit account-owner authorization.

## Planning and Git

Follow `.agents/PLANS.md`. Continue `.agents/exec_plans/active/cross-platform-port.md` and keep `.agents/current_plan.md` synchronized after meaningful evidence. Mark source completion separately from executed build/test results; never record PASS for an unrun command. Keep macOS 14/15 runtime and desktop/live gates pending until evidence exists. Do not commit, push, tag, publish, or invoke remote workflows without explicit authorization.
