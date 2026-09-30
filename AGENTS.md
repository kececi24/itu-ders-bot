# AGENTS.md

## Project Goal

This repository is the macOS edition of the İTÜ course registration helper.

The original project was implemented for Windows. This version should reproduce the same user-facing functionality on macOS while using native or portable APIs appropriate for Apple Silicon Macs.

Windows compatibility is not a requirement for this repository.

The Windows and macOS implementations will be distributed as separate versions.

## Supported Platform

Target environment:

```text
macOS
Apple Silicon: arm64
Compiler: Apple Clang
Build system: CMake
```

Intel macOS (`x86_64`) support is not required.

Do not introduce Windows compatibility layers, Windows headers, Win32 APIs, WinHTTP, `winmm`, `.exe` assumptions, or `_WIN32` branches unless explicitly requested.

## Repository Map

Important paths include:

```text
.github/workflows/   CI workflows
data/                Runtime configuration
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
setup/main.cpp       Configuration/setup utility
```

Inspect the repository before assuming this map is exhaustive.

## Porting Principle

Preserve the behavior of the original Windows version, not its implementation details.

The macOS codebase should be clean macOS/portable C++ rather than a Windows codebase wrapped in compatibility conditions.

Remove or replace Windows-specific APIs where encountered.

Prefer:

- standard C++ APIs
- POSIX APIs available on macOS
- Apple APIs where they provide a meaningful advantage
- libcurl for HTTP networking

Do not retain dead Windows code merely for historical compatibility.

## Networking

Replace WinHTTP with libcurl.

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

The macOS version must preserve:

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

Refactoring is appropriate when required to replace Windows-specific infrastructure cleanly.

Prefer removing Windows abstractions entirely rather than introducing cross-platform conditional complexity.

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

Before considering the macOS port complete, verify:

1. CMake configures successfully on Apple Silicon macOS.
2. The project builds successfully with Apple Clang.
3. No required source path depends on Windows headers or libraries.
4. `setup` correctly reads and writes configuration.
5. Password input remains hidden.
6. Authentication reaches JWT acquisition.
7. Cookies and redirects survive the full authentication flow.
8. Clock synchronization retains the intended sampling behavior.
9. `--dry-run`, `--test`, `--local`, and `--logs` behave correctly.
10. Registration payload generation matches the original implementation.
11. Real registration requests are not sent during automated verification unless explicitly authorized.
12. README instructions describe the macOS Apple Silicon build and usage process.

If live OBS verification is unavailable, clearly distinguish static/code-level verification from behavior confirmed against the real service.