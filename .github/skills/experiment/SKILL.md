---
name: experiment
description: "Run a thesis experiment with Split_kMeans (for example the --repartition comparison of step 6.1b, the 6.7 comparison or the 7.3 re-validation) and summarize SSE, CI, relative CI, success rate and time. Does not edit source files."
disable-model-invocation: true
argument-hint: "Experiment: algorithms, options, datasets, runs, seed"
---
Run the experiment the user describes. Do not edit source files and do not commit anything.

Preconditions: the root `Makefile` exists (step 1.3), `--seed`, `--algo` and `--log-level` exist (steps 1.6 and 1.6b), and the per-run `<Algo>_runs.csv` is written (step 1.6). If one is missing, report which step is needed and stop.

Procedure:
1. Build the release configuration (`make` on Linux, `mingw32-make` on Windows). Never use a debug or sanitizer build for timings.
2. Use `--log-level 1` for every timed run. Use a fixed `--seed`; take the user's seed or pick one and report it.
3. Datasets come from the root `data/`, `gt/` and `centroids/` folders (paired by sorted position) unless the user names files.
4. Pilot first: run each configuration once on each dataset (`--runs 1`) and report the pilot times. Ask before the full run if any configuration takes long. Repeated k-means and Random Swap on large sets (birch, worms) can run for hours.
5. Full run: the requested number of runs (default 100). Outputs go under `outputs/`, which git ignores. On Windows inside OneDrive, pass `-o <dir outside OneDrive>` instead: batch mode tracks progress and writes a file per swap, which OneDrive can lock.
6. Summarize from the `<Algo>_runs.csv` files, one row per dataset and configuration: mean and standard deviation of SSE, mean CI, relative CI (CI/K), success rate (% runs with CI = 0), mean and standard deviation of time in ms. Report only these measures.
7. Record the exact commands, the seed, the commit hash (`git rev-parse --short HEAD`) and `run_config.txt` locations.

Output: the summary table, the commands, and anything that failed or was skipped.
