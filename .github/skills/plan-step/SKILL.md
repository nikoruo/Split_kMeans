---
name: plan-step
description: "Implement exactly one step from docs/refactor-plan.md, or run one milestone (M0 to M4), verify it, tick its checkbox, and stop. Usage: give the step id such as 3.1 or M1."
disable-model-invocation: true
argument-hint: "Step id, for example 3.1 or M1"
---
Implement exactly one step from `docs/refactor-plan.md`: the step id given by the user.

Procedure:
1. Read `docs/refactor-plan.md` and find the step. If a step it depends on (a "depends on" note, or the earlier milestone it is compared with) is not ticked, stop and report which ones are missing.
2. Read `.github/copilot-instructions.md` and the matching instruction files before editing.
3. Milestones (M0, M1, M2, M3a, M3b, M4) change no code. Run the milestone with the experiment skill in milestone mode; it also copies the summaries to `results/<id>/`. M1, M2 and M3a must match the main group of `results/M0` exactly in SSE and CI, M3b `results/M3b-ref`. A mismatch fails the milestone: report it, do not fix code in this step. Then go to procedure point 6.
4. Code steps: make the smallest change that completes the step. Do not start other steps. Delete code only if its row in the Unused code review says delete in the Decision column; list any new unused findings there instead. Keep the default settings working (no arguments, and `-k K data.txt`).
   - Phases 3, 4, 5 and 7 and steps 6b.1 and 6b.2 are bit-identical: the golden files must not change, and `make golden-update` never runs.
   - Phase 6 and step 6b.3 (algorithm changes), in this order: (a) run the clustering-auditor subagent on the algorithm the step changes; (b) run M4 on the unchanged code (only the algorithms the step changes) as the before run; (c) make the change; (d) `make check` now fails where results change: list every changed golden line and explain it from the step; a change the step does not explain is a bug to fix first (`compare_runs` prints at most 5 changed lines per file; after (e), `git diff tests/golden` lists all of them, and `git checkout -- tests/golden` undoes the update); (e) `make golden-update`, after which `make check` passes; (f) changelog entry that cites the paper section the auditor reported (the update log in `src/main.c` until step 4.1d, then `CHANGELOG.md`); (g) M4 after the change; report before and after SSE, CI, success rate and time.
5. Run the checks from the verify skill (`.github/skills/verify/SKILL.md`). Fix failures caused by your change. In Phases 4, 5, 6b and 7, also run the c-reviewer subagent on the changed code; fix the critical and high findings caused by the change and list the others.
6. Tick the step's checkbox in `docs/refactor-plan.md` and add a short "Done:" note: what changed, the verify results, and whether the golden files are unchanged (or which changed and why).
7. For step 6.7 onward (including Phase 6b), commit the completed and verified step with a descriptive message naming the step; preserve any separate commits the plan requires. Stage only that step's changes; if unrelated changes cannot be separated, ask the user before committing. For earlier steps, commit only when asked. Never push unless explicitly asked. Stop and report the commit, files changed, verify results and anything deferred.
