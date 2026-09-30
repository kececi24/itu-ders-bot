# Current Plan

Last updated: 2026-09-30 21:40 Europe/Istanbul
Primary ExecPlan: `.agents/exec_plans/active/cross-platform-port.md`
State: ACTIVE

## Current Objective

Finish the remaining native Windows build and external acceptance gates for the unified port. Locally available Linux and macOS build/test/archive work is complete.

## Last Verified State

- Validation on macOS ARM64 (Apple Clang 21, macOS 26.6 SDK, project-local CMake 4.4.3, Python 3.12.3):
  - PASS: integrated configure and build with `macos-arm64` preset using Apple SDK libcurl 8.7.1.
  - PASS: all 7/7 offline CTest suites passed in 37.86s (transport, authentication, core_and_clock, application_flags, setup_terminal, setup_files, artifact_integrity).
  - PASS: native Mach-O binary inspection, ARM64 architecture, minos 14.0, codesign verification, clean-directory missing-config/non-TTY startup smoke.
  - PASS: sanitized package `dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz` and outer checksum `dist/itu-ders-bot-1.0.0-macos-arm64.tar.gz.sha256`; extracted archive passes integrity and startup smoke.
  - PASS: production `BUILD_TESTING=OFF` build verifies test seams and loopback socket symbols are absent.
- Validation on Ubuntu 22.04 WSL (GCC 11.4, project-local CMake 3.31.6/Python 3.12.14, static dependencies):
  - PASS: integrated configure/build, all 7/7 offline CTest suites (27.59s), native ELF inspection, sanitized archive `dist/itu-ders-bot-1.0.0-linux-x64.tar.gz`.
  - PASS: production test seams absent; setup refuses un-enforced chmod filesystem and cleans up.
- Fixed during macOS validation:
  - CP-08: Resolved symlinks in `ITU_APPLE_SDK` in `cmake/Dependencies.cmake` so `cmake_path(IS_PREFIX ...)` correctly validates curl include/library paths within SDK.
  - CP-09: Updated `is_terminal()` in `src/platform_posix.cpp` and `src/platform_windows.cpp` to check both STDIN and STDOUT handles so non-interactive redirected output does not hang waiting for interactive Enter.
  - CP-10: Fixed macOS QoS demotion and unspecified elevation in `TimingGuard` (`src/platform_posix.cpp`), preventing downgrade of `USER_INTERACTIVE` threads and correctly elevating unspecified QoS threads.
  - CP-11: Stripped trailing `\r` on newline and EOF in POSIX `read_line()` (`src/platform_posix.cpp`) to prevent CRLF input from corrupting passwords and configuration values.
  - CP-12: Added pre-check for dependency manifest existence in `cmake/Dependencies.cmake` before resolving `file(REAL_PATH)` to eliminate CMake author warnings on unbootstrapped checkouts.

## Next Actions

1. On Windows, provide existing MSVC2022 x64 developer environment; bootstrap, build/test `windows-x64`, then package/verify. No compiler/SDK installation is authorized.
2. After authorization, execute the defined native/newer-OS CI; retain separate Windows 10/11 desktop and account-owner live acceptance gates.

## Blockers / Risks

- Current macOS host cannot execute native Windows MSVC build; Windows bootstrap is BLOCKED at missing `cl` on non-Windows hosts.
- Ubuntu 24 archive smoke, native Windows 10/11 desktop and live service evidence remain NOT RUN/deferred.
- Remote CI workflow invocation and publication remain unauthorized.

## Handoff Notes

- Preserve deleted source `data/example_config.json`; sanitized archive template is `packaging/example_config.json`.
- Dependencies/tools/logs/archive remain ignored/project-local; no global installs, TLS bypass, live OBS/registration/retry, workflow invocation or publication occurred.
- Primary checkout shares this branch. User authorized the local continuation checkpoint on `cross-platform` on 2026-09-30; pushes, tags, publication and remote CI remain unauthorized. Do not inspect personal config/credentials.
