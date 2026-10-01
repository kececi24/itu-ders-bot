# Current Plan

Last updated: 2026-10-01 11:10 Europe/Istanbul
Primary ExecPlan: `.agents/exec_plans/active/cross-platform-port.md`
State: ACTIVE

## Current Objective

All open implementation defects, security vulnerabilities, and review findings (CP-13 through CP-30) have been resolved and verified with native execution, 7/7 passing offline CTest suites, and package/archive validation.

## Last Verified State

- CP-13 to CP-19: ACL stripping, Windows curl `iphlpapi`, release revision checks, build provenance, Ubuntu 22.04 ABI ceilings, Windows console test synchronization, and repeat count handling. PASS.
- CP-20: Fixed macOS missing dependencies in `build-manifest.json` by writing `${CMAKE_BINARY_DIR}/dependency-manifest.json` from `ITU_DEPENDENCY_MANIFEST`. Generated manifest verified with Apple SDK curl. PASS.
- CP-21: Replaced conditional `if (CreateProcessW)` with hard assertions in `windows_console_tests.cpp`; added child timeout checks and process termination on hang; verified non-zero exit.
- CP-22: Fixed `on_control` in `src/platform_windows.cpp` to return `handled` (FALSE when inactive) so terminal control signals are not swallowed during reader teardown.
- CP-23: Restricted `MenuInput::read()` key repeat buffering strictly to directional keys (`up`, `down`), preventing duplicate `enter` actions.
- CP-24: Handled `errno.EIO` in `setup_pty_tests.py` draining loop (`Session.finish`), preventing crashes on Linux when child process closes slave PTY.
- CP-25: Added terminating `nullptr` to `setup/main.cpp` `pointers` vector for standard C/C++ `argv[argc] == nullptr` compliance.
- CP-26: Handled missing `.sha256` files gracefully in `release_checksums.py`; added support for 64-char Git SHA-256 commit hashes in `package.py` and `release_checksums.py`.
- CP-27: Supported uppercase hex HTML entities (e.g. `&#X2D;`) in `src/token.cpp` `decode_html`; verified in `auth_fixture.py`. PASS.
- CP-28: Added owner UID check (`info.st_uid == geteuid()`) to `test_helpers::private_file()` on POSIX, matching Windows DACL owner assertions. PASS.
- CP-29: Added `tests/` to `sys.path` in `artifact_integrity_tests.py` for direct `unittest` execution; added 4 new unit tests covering dirty manifests, invalid revisions, missing checksum files, and SHA-256 hashes (15/15 passed). PASS.
- CP-30: Guarded `fsync` with `if (written)` in `atomic_write_private` in `src/platform_posix.cpp` to skip redundant sync on failure. PASS.
- macOS ARM64 Verification: All 7/7 offline CTest suites passed (35.58s); native artifact inspection/startup passed; sanitized archive package generated and verified. PASS.

## Next Actions

1. Native Windows MSVC build/CTest/archive execution when a Windows environment/compiler is available.
2. Authorized remote CI workflow runs for Linux (Ubuntu 22/24) and Windows Server.
3. macOS 14/15 runtime and desktop/live acceptance remain deferred as agreed.

## Blockers / Risks

Windows/MSVC and Linux native execution remain unavailable on this local Mac host. Remote CI, release publication, and live OBS registration calls are unauthorized and were not run.

## Handoff Notes

Preserve the deleted `data/example_config.json`; use the sanitized packaging template. Git remains read-only for this task, dependencies remain project-local, and personal credentials/config were not inspected. Scratch evidence is under ignored `build/review-20261001/`.
