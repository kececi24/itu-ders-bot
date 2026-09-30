# Current Plan

Last updated: 2026-09-30 21:13 Europe/Istanbul
Primary ExecPlan: `.agents/exec_plans/active/cross-platform-port.md`
State: ACTIVE

## Current Objective

Finish the remaining native Mac/Windows and external acceptance gates for the unified port. Locally available Linux build/test/archive work is complete.

## Last Verified State

- Validation used `cross-platform` base `7780a30` plus continuation fixes; this user-authorized checkpoint includes those fixes and synchronized plans.
- PASS: Ubuntu22 WSL GCC11.4, project-local CMake3.31.6/Python3.12.14, verified pinned static dependencies, integrated configure/build.
- PASS: affected core/setup and corrected application tests, then full offline CTest 7/7 (27.59s); archive contract tests also 8/8 on Windows Python.
- PASS: Linux ELF/linkage/glibc<=2.35/clean startup; sanitized archive outer/payload hashes, exact allowlist/licenses and empty courses/zero lead.
- PASS: production test seams absent; setup refuses this ignored-chmod filesystem, preserves synthetic original and removes temporary files.
- README/AGENTS and three-target CI updated; YAML/matrices/release dependencies/bash syntax PASS (static only).
- Archive: `dist/itu-ders-bot-1.0.0-linux-x64.tar.gz`; command logs: `build/preflight/*.log`. Fixes and details are in the ExecPlan.

## Next Actions

1. On a Mac, use project-local CMake with `macos-arm64` preset; build, affected/full CTest, package and verify the native archive.
2. On Windows, provide existing MSVC2022 x64 developer environment; bootstrap, build/test `windows-x64`, then package/verify. No compiler/SDK installation is authorized.
3. After authorization, execute the defined native/newer-OS CI; retain separate Windows10/11 desktop and account-owner live acceptance gates.

## Blockers / Risks

- Current Windows host cannot execute native macOS; Windows bootstrap is BLOCKED at missing `cl`.
- macOS14/15 runtime, Ubuntu24 archive smoke, native Windows and desktop/live evidence remain NOT RUN/deferred.
- Local archive predates this checkpoint; its manifest records base `7780a30` and its binaries include continuation fixes. Rebuild before a published release.

## Handoff Notes

- Preserve deleted source `data/example_config.json`; sanitized archive template is `packaging/example_config.json`.
- Dependencies/tools/logs/archive remain ignored/project-local; no global installs, TLS bypass, live OBS/registration/retry, workflow invocation or publication occurred.
- Primary checkout shares this branch. User authorized the local continuation checkpoint on `cross-platform` on 2026-09-30; pushes, tags, publication and remote CI remain unauthorized. Do not inspect personal config/credentials.
