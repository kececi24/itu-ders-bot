# Current Plan

Last updated: 2026-10-05 21:54 Europe/Istanbul
Primary ExecPlan: `.agents/exec_plans/active/cross-platform-port.md`
Queued successor: `.agents/exec_plans/active/2026-10-04-registration-polling.md`
State: BLOCKED — corrected native MSVC and external acceptance pending

## Current Objective

Continue in `C:/Users/ASUS/Desktop/itu ders bot`. CP-39 missing configure-snapshot recovery is now explicitly configured and locally verified; require native MSVC confirmation and remaining port acceptance. Polling remains queued and unauthorized.

## Last Verified State

- SOURCE COMPLETE: CP-39 changes only `tests/build_provenance_tests.py`. After deliberately deleting either configure snapshot, the test requires the exact missing-provenance rejection and absent manifest, reruns configure with `check=True`, checks the snapshot exists and configure alone did not create a manifest, then builds/validates both executables. Spaced paths and all existing integrity controls retained; production provenance/runtime helpers unchanged.
- PASS: only `ctest --preset windows-mingw-x64 -R '^build_provenance$' -V`,1/1 in107.86s on existing MinGW GCC15.2/CMake4.2.3/native Python3.14.4. Both missing-snapshot recovery cases and real Git hidden-untracked rejection reached PASS. Log `.deps/verify-snapshot-recovery-20261005/focused.log`. No new full-suite or archive run.
- Latest observed MSVC CI retry at ff6ee7b: production build and8/9 PASS, provenance FAIL at old line591 after snapshot deletion; [job111925462017](https://github.com/kececi24/itu-ders-bot/actions/runs/37355657214/job/111925462017),139.90s. Earlier paths-with-spaces builds and CP-38 same-input relink case succeeded. Linux/macOS14/MinGW jobs succeeded; archive-smoke/release skipped. These runs precede CP-39.
- Current source HEAD14c1ec4 (user committed workflow/README/plans); only CP-39 test and synchronized plans are uncommitted. Branch pushes disabled, PR/manual/tag triggers retained. No commit/push/publication, remote invocation during this fix, global installation or live OBS access.

## Next Actions

1. Make the corrected source available on a native MSVC runner through separately authorized Git actions; run `ctest --preset windows-x64 -R '^build_provenance$' --output-on-failure`. Re-running the old ff6ee7b job cannot test this uncommitted correction.
2. After affected MSVC PASS, complete remaining full-suite/archive/seam/desktop and external port gates with explicit evidence.
3. Finalize the port only when all gates have evidence/dispositions; stop before polling, which needs separate authorization.

## Blockers / Risks

- Native MSVC unavailable locally (missing cl); corrected-source MSVC execution NOT RUN. MinGW PASS does not establish native MSVC acceptance.
- Previous connector Windows retry was rejected with API403 `Resource not accessible by integration`. A later observed CI attempt also failed on the unchanged snapshot recovery case; this fix has not reached that runner.
