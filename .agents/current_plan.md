# Current Plan

Last updated: 2026-10-01 15:34 Europe/Istanbul
Primary ExecPlan: `.agents/exec_plans/active/cross-platform-port.md`
State: ACTIVE

## Current Objective

Run the current revision's Linux build, offline tests, and Ubuntu22/24 archive checks in user-authorized Docker containers. Keep CP-31/CP-32/CP-33 open and prioritized for subsequent fixes. Windows execution and desktop testing belong to the user's teammate.

## Last Verified State

- Reviewed clean `e1aea0712fbc95f28b74b75291b64485e5567071`: fresh macOS build, 7/7 suites (28.28s), native inspection and actual extracted package PASS. CP-13 inherited-ACL create/replace and failure-preservation probes PASS; Linux ABI boundary checks PASS with mocked readelf.
- CP-31/CP-32 reproduced in disposable build fixtures; CP-33 includes source-confirmed test skip and fault-injected ACL-read acceptance. No current credential exposure reproduced. Historical Linux results predate these fixes.

## Next Actions

1. Correct CP-31/CP-32 and add path-space, failed-build and partial-build regressions; make CP-33 ACL checks mandatory and reject unexpected errors.
2. Rerun affected and full macOS checks; run the corrected revision on Ubuntu22.04 x64 and smoke that exact archive on Ubuntu24.04.
3. Collect teammate's Windows build/test/archive and desktop evidence separately. Keep Windows source fixes labeled NOT RUN until then.

## Blockers / Risks

The user now authorizes pulling required images and running containers, including Docker's external image storage. Ubuntu AMD64 containers run through Docker Desktop on the ARM64 Mac; record emulation separately from physical x64 execution. Environment preparation/preflight is underway; project dependency downloads/builds remain below ignored `.deps/`. macOS14/15 remains deferred; remote CI/live calls/publication remain unauthorized.

## Handoff Notes

Only planning files changed in this review. Git stays read-only; preserve personal config and deleted `data/example_config.json`. Evidence and the verified local macOS archive are under ignored `build/review2-20261001/`; use the ExecPlan for details and exact scope limits.
