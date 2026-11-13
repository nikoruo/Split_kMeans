---
name: plan-step
description: "Implement exactly one step from docs/refactor-plan.md, verify it, tick its checkbox, and stop. Usage: give the step id such as 1.3."
disable-model-invocation: true
argument-hint: "Step id, for example 1.3"
---
Implement exactly one step from `docs/refactor-plan.md`: the step id given by the user.

Procedure:
1. Read `docs/refactor-plan.md` and find the step. If its dependencies are not ticked, stop and report which ones are missing.
2. Read `.github/copilot-instructions.md` and the matching instruction files before editing. For Phase 6 steps, first run the clustering-auditor subagent on the algorithm the step changes, and cite the paper section it reports in the changelog entry.
3. Make the smallest change that completes the step. Do not start other steps. Delete code only if its row in the Unused code review says delete in the Decision column; list any new unused findings there instead. Keep the default settings working (no arguments, and `-k K data.txt`).
4. Run the checks from the verify skill (`.github/skills/verify/SKILL.md`). Fix failures caused by your change.
5. Tick the step's checkbox in `docs/refactor-plan.md`.
6. Stop. Do not commit. Report: files changed, verify results, anything deferred.
