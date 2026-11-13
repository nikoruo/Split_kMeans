---
name: commit-message
description: "Write a commit message and description for the current Split_kMeans changes, in the project's format (summary line, grouped body, known issues). Does not commit."
disable-model-invocation: true
argument-hint: "Optional: scope, for example 'step 2.1' or 'staged only'"
---
Write a commit message for the current changes. Do not stage, commit or push anything.

Procedure:
1. Collect the changes: `git status --short` and `git diff --stat` (or `--staged` when the user asks for staged only), then read the diffs that matter. Check `docs/refactor-plan.md` for the plan steps the changes complete.
2. Base every statement on the diff and on verify results from this conversation. Do not take behavior claims from code comments.
3. Warn about files that look accidental: anything under `build/`, `outputs/`, `data/`, `gt/`, `centroids/`, `university/`, stray `*.o` or `*.exe`.

Format:
- Summary line: at most 72 characters, imperative or "Phase N: ..." / "Step N.N: ...", no trailing period.
- Blank line, then one sentence on the purpose with the plan step ids in parentheses.
- Body grouped under short plain headings (for example Layout and build, Reproducibility, CLI and logging, Tests and CI, Warnings, Fixes). One `-` bullet per change, wrapped at 72 characters, naming files, functions and options in plain text.
- State effects on results explicitly: "golden files unchanged" for refactors; for algorithm changes, say which outputs change and why, that `make golden-update` was run, and cite the paper section from the clustering auditor.
- End with `Known issues (planned): ...`, listing open problems with their plan step ids. Leave it out when there are none.
- No emojis, no Markdown formatting inside the message.

Output: the message in one fenced `text` block, then a one-line note on anything to check before committing.

Example (Phase 1):
```text
Phase 1: build system, seeded RNG, CLI and golden-file regression tests

Restructure the project and add the foundation for the v2.0.0 refactor
(docs/refactor-plan.md, steps 1.1 to 1.9).

Layout and build
- Move sources to src/ (Clustering_with_.c -> src/main.c), the Visual
  Studio project to msvc/, datasets to the repo root (untracked).
- Root Makefile (GNU Make 4.3+): release, debug, asan and ubsan builds
  in build/<config>/; WERROR=1, NATIVE=1, THREADS=1; cmd.exe and sh.
- VS project: wildcard sources, C11, /W4, /utf-8.
- Add .gitattributes, .editorconfig, .clang-format.

Reproducibility
- src/rng.[ch]: PCG32 seeded via SplitMix64, unbiased bounded draws.
  Removes rand(), rand_s and RANDOMIZE; every algorithm takes an Rng*.
  Results differ from v1.0.0 because the random stream changed.
- --seed (default time-based, printed); run i uses a seed derived from
  the master seed and i, written to <Algo>_runs.csv (run;seed;sse;ci;ms).

CLI and logging
- --algo <list> (default skm-local), -r/--runs, -o <dir>,
  --log-level 1|2|3 (default 1). New algorithm skm-global-basic.
- Defaults unchanged: no arguments = batch mode, -k K data.txt [gt.txt].
- Fix logging blocks that did not compile or crashed.

Tests and CI
- tests/gen_data.c: integer synthetic datasets (5 scenarios, 6 edge
  cases; K=1 pending until step 2.3).
- tests/compare_runs.c + tests/golden/ (93 files): make check compares
  every runs.csv (without timing) bit-exactly and smoke-tests both
  default invocations; make golden-update for algorithm changes only.
- CI: gcc and clang, make WERROR=1 and make check; remove ci-fixtures/.

Warnings
- Warning-free with -Werror on gcc 16 (all -O levels) and MSVC /W4:
  set_numeric_locale_finnish moved to src/locale_utils.c, unused
  bisecting loop counter removed, output-path snprintf results checked,
  handleMemoryError takes void*.

Known issues (planned): K=1 crash (2.3), bisecting hangs on K=N (2.4),
OneDrive sharing violations in tracking output (3.2, use CHECK_ROOT).
```
