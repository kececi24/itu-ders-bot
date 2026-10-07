# PLANS.md — current_plan + ExecPlan Protocol

This repository uses two complementary planning artifacts so work survives long sessions, context compaction, and agent handoff without turning `AGENTS.md` into a project diary.

## 1. Files and Responsibilities

### `.agents/current_plan.md`
A compact **handoff index** describing the repository's current work state. It answers, in one screen:
- What are we trying to achieve now?
- Which ExecPlan is authoritative for that work?
- What has been verified most recently?
- What are the next concrete actions?
- What is blocked or uncertain?

It is not a backlog, transcript, or duplicate of the ExecPlan. Keep it short and overwrite stale state rather than appending history forever.

### `.agents/exec_plans/active/<date>-<slug>.md`
A **living, self-contained execution plan** for complex work. It must contain enough repository context, rationale, milestones, evidence, and current progress that a new stateless agent can continue safely without reading old chat history.

### `.agents/exec_plans/completed/<date>-<slug>.md`
Completed ExecPlans are retained as engineering history and decision context. Do not keep completed plans in `active/`.

## Plan Mode and Persistence

`/plan` / Plan Mode and this repository's ExecPlan files are related but are not the same thing.

- `.agents/PLANS.md` is the **protocol/template** for ExecPlans. Never overwrite it with the current campaign plan.
- While Plan Mode is read-only, the model may research, ask planning questions, and present a decision-complete plan in chat, but that chat plan is **not yet durable repository state**.
- After the user accepts/finishes the planning questions, persist the final plan as a separate file under `.agents/exec_plans/active/<date>-<slug>.md` and update `.agents/current_plan.md`.
- If the host's Plan Mode prevents writes, exit/switch to normal execution mode and perform only the plan-persistence writes first. Do **not** begin implementation in the same step unless the user asked to proceed.
- Do not assume the existence of a chat plan means an ExecPlan file was created. Verify the file exists before handoff or starting a fresh thread.

## 2. When an ExecPlan Is Required
Create or continue an ExecPlan when work is any of the following:
- spans multiple files/packages/modules;
- changes architecture, data contracts, security boundaries, scoring logic, persistence, or deployment behavior;
- includes a repository-wide test/debug/security campaign;
- is likely to survive context compaction or agent handoff;
- contains multiple dependent milestones or non-trivial investigation;
- has material risk if an agent forgets prior decisions or verification evidence.

A small isolated edit with an obvious test does not require a new ExecPlan. If it belongs to an existing active plan, record it there instead of creating a competing plan.

## 3. Lifecycle

1. **Orient** — read `AGENTS.md`, `docs/README.md`, `current_plan.md`, the active ExecPlan, relevant specs, `git status`, and affected code/tests.
2. **Create or adopt** — if no suitable active plan exists, create one from the template below. Set it as the primary plan in `current_plan.md`.
3. **Establish baseline** — record what is known, what is assumed, and which checks were actually run before edits.
4. **Execute milestone-by-milestone** — do not convert the plan into a speculative implementation dump. Update it as facts change.
5. **Checkpoint after evidence** — after meaningful work, update `Progress`, `Surprises & Discoveries`, `Decision Log`, `Validation`, and the handoff snapshot as applicable.
6. **Synchronize `current_plan.md`** — keep current objective, verified state, next steps, and blockers aligned with the ExecPlan.
7. **Complete** — only after acceptance criteria are verified or explicitly marked blocked/accepted. Write `Outcomes & Retrospective`, move the file to `completed/`, and update `current_plan.md`.

## 4. Evidence Rules
- Never write `PASS` for a command that was not run.
- Use explicit states: `PASS`, `FAIL`, `BLOCKED`, `NOT RUN`, `PARTIAL`.
- Record the relevant command, scope, and result.
- Distinguish observation from inference. Example: “test X fails with Y” is evidence; “likely race condition” is a hypothesis until traced.
- If code/config contradicts docs/spec, record the drift and the resolution decision.
- If the worktree contains unrelated modifications, note them and preserve them.
- A plan never overrides repository security invariants.

## 5. Required ExecPlan Structure

Every active ExecPlan must contain these sections and keep the living sections current.

```markdown
# <Action-oriented milestone title>

Status: ACTIVE | BLOCKED | COMPLETED
Created: YYYY-MM-DD
Last updated: YYYY-MM-DD HH:MM <timezone>
Owner: <agent/human or "unassigned">
Primary scope: <paths/modules>

## Purpose / Big Picture
What outcome should exist when this plan is complete, and why it matters.

## Scope
### In scope
- ...

### Out of scope
- ...

## Repository Orientation
Key packages, entry points, docs, contracts, runtime dependencies, and data flow needed by a new agent.

## Invariants and Acceptance Criteria
Non-negotiable architecture/security requirements plus observable completion criteria.

## Baseline / Starting Evidence
Known repository state, git/worktree state, commands already run, failures, metrics, and important unknowns.

## Milestones
1. <milestone and exit criterion>
2. ...

## Progress
- [ ] YYYY-MM-DD HH:MM — <concrete item> — NOT RUN
- [x] YYYY-MM-DD HH:MM — <concrete item> — PASS — <evidence>
- [ ] YYYY-MM-DD HH:MM — <concrete item> — BLOCKED — <reason>

## Surprises & Discoveries
Facts learned during execution that materially affect design, scope, risk, or verification.

## Decision Log
- YYYY-MM-DD — Decision: ...
  - Why: ...
  - Alternatives rejected: ...
  - Consequences: ...

## Defects / Findings Ledger
| ID | Type | Component | Severity | Status | Evidence / Reproducer | Fix / Disposition |
|---|---|---|---|---|---|---|

## Validation Plan and Results
| Gate | Command / Method | Status | Result / Evidence |
|---|---|---|---|

## Files / Artifacts Changed
- `path`: why it changed

## Handoff Snapshot
- Current objective:
- Last verified state:
- Next 1–3 actions:
- Blockers:
- Open hypotheses / risks:
- Uncommitted or unrelated worktree changes to preserve:

## Outcomes & Retrospective
Filled when completed: what changed, what was proven, what remains, and what should be done next.

## Revision Notes
- YYYY-MM-DD HH:MM — <what changed in this plan and why>
```

## 6. `current_plan.md` Required Shape

Keep this file deliberately smaller than an ExecPlan:

```markdown
# Current Plan

Last updated: YYYY-MM-DD HH:MM <timezone>
Primary ExecPlan: `.agents/exec_plans/active/<file>.md` | `NONE`
State: ACTIVE | BLOCKED | IDLE

## Current Objective
<1 short paragraph>

## Last Verified State
- <fact + evidence/status>

## Next Actions
1. ...
2. ...
3. ...

## Blockers / Risks
- ...

## Handoff Notes
- <only details a fresh agent needs before opening the ExecPlan>
```

## 7. Multiple Plans
Prefer one primary active ExecPlan for a coherent campaign. Parallel plans are allowed only when scopes are genuinely independent. `current_plan.md` must identify the primary plan and list any secondary active plans explicitly; do not let two plans silently edit the same security boundary.

## 8. Completion Standard
An ExecPlan is not complete because code was written. Completion requires:
- acceptance criteria evaluated;
- relevant tests/security checks recorded;
- failures either fixed, explicitly accepted, or documented as blocked;
- docs/contracts updated when intended behavior changed;
- `Handoff Snapshot` and `Outcomes & Retrospective` finalized;
- the plan moved from `active/` to `completed/`.