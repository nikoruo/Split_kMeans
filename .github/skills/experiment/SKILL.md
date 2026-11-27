---
name: experiment
description: "Run a thesis experiment with Split_kMeans (a milestone run M0 to M4, the --repartition comparison of step 6.1b, the 6.7 comparison, the 9.4 re-validation or the final Phase 9 runs) and summarize SSE, CI, relative CI, success rate and time. Does not edit source files."
disable-model-invocation: true
argument-hint: "Milestone id (M0 to M4), or: algorithms, options, datasets, runs, seed"
---
Run the experiment the user describes. Do not edit source files and do not commit anything.

Preconditions: the root `Makefile` exists (step 1.3), `--seed`, `--algo` and `--log-level` exist (steps 1.6 and 1.6b), and the per-run `<Algo>_runs.csv` is written (step 1.6). If one is missing, report which step is needed and stop.

Rules for every mode:
- Build a gcc release configuration; never use a debug or sanitizer build for timings. Thesis (Phase 9) timings use the Linux gcc release build in WSL2 Ubuntu on the desktop (plan D20): `make` in a clone in the WSL file system, with `data/`, `gt/` and `centroids/` copied there and checked with `sha256sum -c tests/realdata-sha256.txt`, run folders in the WSL file system and summaries copied to `results/`. The MinGW (`mingw32-make`) and Visual Studio Release builds give identical SSE and CI and are fine for untimed work; the Linux build took 0.88 times the MinGW time on the bench jobs (equal on 64-D data), and Visual Studio 1.185 times (steps 8.3 and D20).
- `--log-level 1` for every timed run. A fixed `--seed`: take the user's seed or pick one and report it.
- Until step 3.3, timed runs use CLI mode (`-k K data.txt gt.txt`), one dataset per call, because batch mode writes tracking rows inside the timed code on every run. From 3.10, every timed run (CLI or batch mode) adds `--track-progress --snapshots none`: the rows cost no measured time (the stopwatch pauses; A/B test in plan step 3.10), `report` needs them for RS (its results end at the last SSE improvement of each run, plan "Thesis results"), and no snapshot files are written around the timed runs. Never use `--no-track` for runs that go through `report`. Figure snapshots come from a separate call with the same seed.
- `-r N` also applies to RKM and RS; without it they run once. Run them in a separate call without `-r` unless the user asks for more runs.
- Start each experiment/configuration in a new folder outside OneDrive, given with `-o`. Compatible calls may append to that folder for different datasets, algorithms or non-overlapping run ranges; never repeat a run for the same dataset and algorithm. Run folders stay local.
- Review `run_config.txt` before sharing or tracking it: recorded arguments can contain local paths. Preserve the seed, configuration, commands and build provenance when replacing sensitive paths with documented placeholders.
- Timings: only on the reference machine (plan D3: your personal desktop from step 8.3), nothing else running, plugged in, F-Secure scanning off; for timed Windows runs shut down WSL first. If a time looks off, repeat in alternating rounds and compare per-run minimums; background load (for example right after a build) can slow runs 2 to 5 times.
- Datasets come from the root `data/`, `gt/` and `centroids/` folders (paired by sorted position; K from the `centroids/` file) unless the user names files. On a new machine or a fresh copy, check them first with `sha256sum -c tests/realdata-sha256.txt` from the repo root.

Milestone mode (M0 to M4; plan section "Milestones"):
- Seed 20261119 for every milestone. M1 to M3b are exact comparisons: keep the seed, datasets, algorithms and run counts identical within each reference comparison.
- Main group (every milestone). Datasets, K and ground truth: s1 (15, `gt/s1-cb.txt`), s4 (15, `gt/s4-cb.txt`), a3 (50, `gt/a3-ga-cb.txt`), unbalance (8, `gt/unbalance-gt.txt`), dim064 (16, `gt/dim064.txt`), worms_2d (35, `gt/worms_2d-gt.txt`). Calls per dataset: `--algo km,skm-intra,skm-global,skm-local,bisecting -r 100`, then `--algo rkm,rs` (1 run each). worms_2d only `--algo km,skm-intra,skm-local,bisecting -r 10`. About 3 minutes.
- Hard group (M0 and M4 only; the sets where the split variants did not reach CI = 0 in v1). Datasets, K and ground truth: birch1 (100, `gt/b1-gt.txt`), birch2 (100, `gt/b2-gt.txt`), birch3 (100, `gt/b3-gt.txt`), worms_64d (25, `gt/worms_64d-gt.txt`). Calls per dataset: `--algo skm-intra,skm-local -r 100`, then `--algo skm-global -r 10`. About 52 minutes, 40 of them SKM-Global. Compare mean CI here; with 10 runs a success rate moves in 10 % steps.
- M4 (Phase 6): main and hard group, only the algorithms the step changes, with the same datasets, seed and run counts; one run before the change and one after, kept as `results/M4/<step>-before/` and `results/M4/<step>-after/` (for example `results/M4/6.1-before/`).
- M3b: run the reference set of plan step 7.0 once before step 7.1 and keep it as `results/M3b-ref`: the main group with all nine algorithms (worms_2d still without SKM-Global, RKM and RS) and explicit `--repartition inward --bisect-select sse`, plus SKM-Local with `--repartition both` and bisecting with `--bisect-select size` in their own folders. M3b after 7.3 repeats the same set and must match it exactly.
- Keep the summaries in `results/<id>/` (tracked in git, plan D4): every `<dataset>/<Algo>_runs.csv`; `run_config.txt` once it exists (step 3.4), before that a `commands.txt` with the exact commands, seed, commit (`git rev-parse --short HEAD`), build and date; the `report` tables once `report` exists (step 3.9). Tracking logs and snapshots stay in the run folder.
- M1, M2 and M3a: compare the main group with `results/M0` using `compare_runs` (build it with `mingw32-make build/release/compare_runs.exe`; usage `compare_runs results/M0 <run folder> s1/KM_runs.csv ...`; it ignores the time column). M3b: against `results/M3b-ref`. Any difference in SSE or CI fails the milestone.
- M0 also: compare mean CI and success rate with the v1 seminar tables (`university/excels/out/csv/seminaari_SRandCI_KM/01_Taul1.csv`, slides 53 and 54). The same distribution is expected, not the same numbers: the random stream changed in step 1.5, v1 RKM had 3 runs and RS 1 run, and v1 worms_2d and worms_64d ran on coordinates truncated to integers.

Before/after mode (an older commit against the current code): `git worktree add <folder outside OneDrive> <commit>`, build there with `mingw32-make`, run that build with absolute paths to the repo's `data/` and `gt/` (they are not in git), then `git worktree remove <folder>`.

Final-run mode (Phase 9, from tag v2.0.1, Linux build in WSL2 per D20): the design frozen in plan step 9.1 (datasets, algorithms, run counts D1, seed). Split the runs into jobs with `--datasets` and `--first-run` (step 3.5) using the sections in plan step 9.2, run the jobs one at a time, merge them with `report` (step 3.9) and keep the summaries in `results/final`.

Other experiments (6.1b, 6.7 or the user's own):
1. Pilot first: run each configuration once on each dataset (`-r 1`) and report the pilot times. Ask before the full run if any configuration takes long: RKM and RS on birch and worms run for hours, SKM-Global on worms too.
2. Full run with the requested number of runs; if the user names none, the development budget (plan D1): 100 runs, 10 on slow sets, RKM and RS 1 run.

Summary (every mode): from `report` once it exists (step 3.9); before that from the `<Algo>_runs.csv` files, one row per dataset and configuration: mean and standard deviation of SSE, mean CI, relative CI (CI/K), success rate (% runs with CI = 0), mean and standard deviation of time in ms. Report only these measures. RS times from `_runs.csv` alone are for all swaps; `report` gives RS at the last SSE improvement of each run (plan "Thesis results"; SSE and CI are the same either way). Record the exact commands, the seed, the commit hash and the `run_config.txt` or `commands.txt` locations.

Output: the summary table, the commands, and anything that failed or was skipped.
