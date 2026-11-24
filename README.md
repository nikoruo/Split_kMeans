# Split_kMeans

> **Status: work in progress.** The v2.0.0 refactor is under way on the `feature/refactor` branch (v1.0.0 is on `main`); see [docs/refactor-plan.md](docs/refactor-plan.md). Supported platforms: Linux (gcc or clang, GNU Make 4.3+) and Windows (MinGW-w64 or Visual Studio 2026). macOS is not supported.

C implementations of k-means and split-based k-means variants for empirical comparison. Built for a master’s thesis to study algorithmic behavior and also to gain hands-on experience with C.

- Who: Niko Ruohonen
- Why: Compare clustering algorithms and initialization strategies on multi-dimensional data; provide repeatable evaluation artifacts (SSE, CI, logs).
- What: Single-binary C program that discovers datasets, runs selected algorithms, and writes results under timestamped output folders.

License: AGPL-3.0-only (see LICENSE)

# TLDR
- Default run: SSE Split (Local Repartition, splitType=2), starting from the dataset mean.
- All other algorithms run only when selected with `--algo` (see below).
- Two modes: **Directory Batch Mode** (no args) and **CLI Mode** (with args).
- Supports Linux (gcc or clang) and Windows (MinGW-w64 or Visual Studio 2026).

## Quick Start
- Clone the repo.
- Build with the Makefile (`make` on Linux, `mingw32-make` on Windows) or with Visual Studio 2026 (`Split_kMeans.sln`).
    - build/msvc/x64/Release/Split_kMeans.exe (Visual Studio), build/release/split_kmeans.exe (MinGW) or build/release/split_kmeans (Linux) is produced.
- Run without arguments for batch mode or with arguments for CLI mode.
- See **Usage** section below for details.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## Features

- Algorithms
  - K-means: standard assignment + centroid update until no SSE improvement.
  - Repeated K-means (RKM): multiple K-means runs; keep the best.
  - Random Swap: try centroid–point swaps using a target distinct from every current centroid, update affected means after local repartition, then run two k-means iterations; accept only when SSE improves over the initial or current solution. If K equals the number of distinct data points, all swaps are no-ops.
  - Random Split k-means: grow K by repeatedly splitting a random cluster (local k-means) then refining.
  - SSE Split k-means (Split_kMeans): grow K by splitting the cluster that maximizes expected SSE drop.
    - 0 = IntraCluster: split cluster by local k-means on that cluster’s points.
    - 1 = Global: local split, global refinement.
    - 2 = LocalRepartition: local split then targeted reassignments near the split.
    - For K=2 these variants and SKM-Global-Basic skip trial splits, since no later split is needed.
  - Bisecting k-means: repeatedly split the eligible cluster with the largest SSE (or most rows), using multiple local tries; keep the best split, then run global k-means. This is bisecting plus final refinement, not the unrefined variant.

- Initialization
  - Random unique seeding (default): pick K distinct data points.
  - K-means++ seeding: `--init kmeans++` (KM, RKM and RS).

- Metrics and logs
  - SSE: Sum of Squared Errors; also MSE helper present.
  - CI: Centroid Index between result and ground truth centroids.
  - Per-iteration CSV logs and time tracking for selected algorithms.
  - Optional iteration snapshots: centroids and partitions per iteration.

- Portability
  - Linux (GNU Make, gcc or clang) and Windows (MinGW-w64 with mingw32-make, or Visual Studio 2026).
  - RNG: seeded PCG32 (`src/rng.c`), identical sequences on every platform for the same seed.
  - Datasets and centroids use flat row-major coordinate buffers; dataset labels are stored separately. Split algorithms use indexed cluster views without copying coordinates.

## Project layout

### Directory Batch Mode structure:
- data/ — input datasets, one point per line (whitespace-separated doubles). All rows must have identical dimensionality.
- gt/ — ground-truth centroids, one centroid per line (same dimensionality as corresponding data file).
- centroids/ — a txt file per dataset containing only one positive integer K.
- outputs/<YYYY-MM-DD_HH-MM-SS>/<dataset-base-name>/ — created automatically; contains logs, snapshots, aggregates.
- src/*.c|*.h — source.
- msvc/ — Visual Studio project (open `Split_kMeans.sln` in the repo root).

Pairing rule
- The program enumerates files in data/, gt/, centroids/; counts must match.
- Files are paired by sorted filename order. Keep base names aligned, e.g.:
  - data/worms_64d.txt
  - gt/worms_64d-gt.txt
  - centroids/worms_64d.txt

Example formats
- data file (N×D):
  0.12 1.34 5.67
  9.01 2.34 8.76
- gt file (K×D, same D):
  0.10 1.30 5.60
  9.00 2.30 8.70
- K file (single integer, no extra content):
  2

### CLI Mode:
- Files can be located anywhere; provide full or relative paths on the command line.
- No specific directory structure required.
- Outputs still written to: outputs/<YYYY-MM-DD_HH-MM-SS>/<dataset-base-name>/

## How it works (high level)

- Discovers datasets under data/, gt/, centroids/.
- For each paired dataset (by sorted order), creates outputs/<timestamp>/<dataset-base-name>/.
- Validates: N > 0, 1 ≤ K ≤ N, data and gt dimensionality match.
- Runs the algorithms selected with `--algo` (default: SKM-Local only); writes results and logs into the dataset’s output folder.

## Build

Windows (Visual Studio 2026)
- Open `Split_kMeans.sln` in the repo root (project in `msvc/`). The debugger runs in the repo root, so batch mode finds `data/`, `gt/` and `centroids/`.
- Build and run (Debug/Release as desired).

Windows (MinGW-w64)
- `mingw32-make` in the repo root builds `build/release/split_kmeans.exe`; the targets are the same as on Linux.

Linux (gcc or clang)
- with the Makefile in the repo root: `make` builds `build/release/split_kmeans`; `make run ARGS="-k 20 data/a1.txt"` runs it from the repo root; `make help` lists the other targets.

Tests
- `make check` runs the unit tests, generates synthetic datasets in `build/testdata`, runs the algorithms with a fixed seed, compares every `<Algorithm>_runs.csv` (without the time column) with `tests/golden`, runs the edge group again with `--no-track`, with every snapshot and `--track-inner`, and at `--log-level 3` against the same golden files (tracking, snapshots and logging must not change results), runs `report --no-time` on the main group and compares its files whole with `tests/golden/report`, and smoke-tests the two default invocations and the `ci` and `sse` subcommands.
- `make golden-update` regenerates `tests/golden`; use it only in commits that change an algorithm.
- The check runs write to `build/check`. Inside a OneDrive folder the tracking files hit sharing violations, so pass an output root outside it, for example `mingw32-make check CHECK_ROOT=C:/Temp` (its `check` subfolder is deleted first; no spaces in the path).
- `CHECK_BIN=<exe>` runs the same checks with another binary, for example the Visual Studio Release build: `mingw32-make check CHECK_BIN=build/msvc/x64/Release/Split_kMeans.exe`. Both Windows builds must match the same golden files.
- `make clean` removes the Makefile builds and the test data; the Visual Studio output in `build/msvc` stays.

Benchmark
- `make bench` runs fixed CLI jobs on the local s1, dim064, worms_2d and birch1 datasets, with seed 20261119, log level 1 and no tracking. RS uses 300 swaps and RKM 25 repeats; the selected algorithms and run counts are in `tests/bench.c`.
- On Windows, build Visual Studio 2026 Release x64 first; the default reference is rejected if it is missing or older than the sources or project. The benchmark alternates the MinGW and Visual Studio binaries for four rounds and fails if any run's seed, SSE or CI differs. On Linux it benchmarks the Makefile build alone. `BENCH_REF=<exe>` selects another release binary (the caller must check its freshness); `BENCH_REF=-` disables comparison.
- Each benchmark creates a new folder under `build/bench`; `BENCH_ROOT=<dir>` selects another root, for example one outside OneDrive. `summary.csv` gives the minimum of four times per run; the raw run files and `run_config.txt` retain all timings and build details. Use an idle, plugged-in machine for comparable timing.

Notes
- Uses C11 (`-std=c11`) and `-ffp-contract=off` (no fused multiply-add, so every compiler rounds the same way); the Visual Studio project uses `/fp:precise` without `/fp:contract`.
- No external library dependencies.

## Usage (user manual)

### Directory Batch Mode (no arguments)
Input discovery
- Program looks for:
  - data/*.txt — points
  - gt/*.txt — ground-truth centroids
  - centroids/*.txt — K per dataset
- All three folders must contain the same number of *.txt files (other files and folders are ignored); pairs are formed by sorted filename order (case-insensitive, the same on every OS).

### CLI Mode (with arguments)
Syntax:
split_kmeans [options] -k <K> <data.txt> [gt.txt]

Options: `--algo`, `--seed`, `-r`, `--first-run`, `--max-iterations`, `--max-swaps`, `--repeats`, `--bisect-tries`, `--bisect-select`, `--init`, `--repartition`, `-o`, `--track-progress`, `--snapshots`, `--track-inner`, `--no-track`, `--decimal-comma`, `--log-level`, `--version`, `-h` (see below and `-h`).

The options also work in batch mode (no data file), for example `split_kmeans --algo skm-local,rs --seed 42`.

Runtime behavior
- On start, a folder is created: outputs/<YYYY-MM-DD_HH-MM-SS>/, and the seed is printed. `-o <dir>` writes to `<dir>` instead (created if missing; existing `<Algorithm>_runs.csv` files are appended to, and its `run_config.txt` must match, see Run configuration).
- For each dataset, a subfolder is created: outputs/<timestamp>/<dataset-base-name>/.
- The algorithms chosen with `--algo` run in a fixed order (default: skm-local only).

Outputs per dataset (examples)
- `dataset.txt`: the data and ground-truth files, K, N and D (read by `report`; a second call with `-o` must use the same values)
- Aggregates
  - <Algorithm>_runs.csv: run;seed;sse;ci;ms, one row per run; the seed reproduces that run; ms is wall-clock time from a monotonic clock, without the tracking
  - <Algorithm>_log.csv: run;count;sse;ci;ms, the tracked states of every run (written after each run). count is the split number for the split algorithms and bisecting (0 is the start; the last row is the final k-means), the swap for RS (the initial solution and each improvement), the repeat for RKM (the best solution so far) and the iteration for KM
  - <Algorithm>_iteration_stats.txt: Run;Iteration;Phase;NumCentroids;SSE;CI;SplitCluster;MS, one row per snapshot
  - <Algorithm>_inner.csv (with `--track-inner`): run;count;stage;iteration;k;sse;ci;ms, every iteration of the k-means runs inside the split algorithms and bisecting; stage is split (the local 2-means of the split), try (a bisecting try), global (global k-means after a split) or final; CI only for global and final
  - <base>.csv: a summary row per algorithm with averages:
    - Algorithm;Average CI;SSE;Relative CI;MS;Success Rate
- Snapshots (first “perfect” and first “failed”, when applicable)
  - kMeans_centroids_perfect.txt, kMeans_partitions_perfect.txt
  - kMeans_centroids_failed.txt, kMeans_partitions_failed.txt
  - RandomSwap_*, RandomSplit_*, Bisecting_*, and for SSE Split: IntraCluster|Global|LocalRepartition
- Iteration snapshots (`--snapshots`; batch mode: run 1)
  - <Algorithm>_run<RRR>_iter<IIII>_<phase>_centroids.txt: one space-separated row per centroid
  - <Algorithm>_run<RRR>_iter<IIII>_<phase>_partitions.txt: one 0-based label per line
  - run is 1-based and iter is the count of `_log.csv`; phase is start, step, final (after the final k-means) or split (after the two-way split, before the follow-up step: the local repartition of SKM-Local or the global k-means of SKM-Global and SKM-Global-Basic; bisecting's split and step snapshots both show the applied local split; SKM-Intra and Random Split have none)

Locale and CSV
- CSV files use semicolons (;).
- The program never changes the locale: data files are read and all files are written with a decimal point.
- `--decimal-comma` writes the Excel-facing files (`<base>.csv`, `_log.csv`, `_inner.csv`, `_iteration_stats.txt`) with a decimal comma for a Finnish Excel; `_runs.csv` and the snapshots keep the point. The numbers are formatted first and the point is then replaced, without `setlocale` (v1 set the Finnish `LC_NUMERIC` at start-up, so `strtod` read `4260.1` as `4260`).
- Number formats: `_runs.csv` SSE and ms with `%.17g` (round-trip); `_log.csv`, `_inner.csv` and `_iteration_stats.txt` SSE with 6 decimals and ms with 3; the summary as before (CI and rates 2 decimals, SSE and ms 0).

Run configuration
- Every output folder gets `run_config.txt`: version, git commit (`-dirty` when the working tree has changes or `src/` holds files git does not track yet), compiler and flags, assertions, timer, machine (OS and logical processors; no host name, because the file is published in `results/`) and CPU, master seed, log level, the algorithm parameters and `--decimal-comma`, then one entry per call with its time, command line (the program by file name, without its folder) and resolved options (mode, algorithms, runs, tracking).
- A call that writes with `-o` into a folder whose `run_config.txt` has a different header (for example another seed, build, machine, log level or iteration limit) stops with an error, so results of different configurations are never mixed. Algorithms, runs, datasets and tracking may differ between calls.
- Without `-o`, every call gets a new folder (`outputs/<timestamp>`, then `_2`, `_3`, ... within the same second).

Determinism
- All randomness comes from one seeded PCG32 generator. `--seed <N>` sets the master seed (default: the clock, printed at start). Run i of every algorithm uses a seed derived from the master seed and i, so the same seed and options reproduce the same results.

## Selecting algorithms and parameters

- `--algo <list>`: comma-separated, any of `km`, `rkm`, `rs`, `skm-random`, `skm-intra`, `skm-global`, `skm-global-basic`, `skm-local`, `bisecting` (default `skm-local`).
- `-r, --runs <N>`: the last run per dataset (default 100). RKM and RS run once unless `--runs` is given.
- `--first-run <i>` (default 1): start at run i; run i keeps the seed derived from the master seed and i, so a long job can resume or be split, for example `-r 50` and `-r 100 --first-run 51`. With `rkm` or `rs` it needs `-r`.
- `--max-iterations <N>`: positive per-call k-means iteration cap for diagnostics. It applies to KM, each RKM repeat, and every local, trial, global and final k-means call in the split algorithms and bisecting. The default is `SIZE_MAX`, so k-means stops when SSE no longer strictly decreases; it may stop earlier than the cap. A capped run may not converge, and this option is not a wall-clock timeout. The returned SSE describes the last iteration's centroids and partition, which need not assign every point to the nearest updated centroid. RKM preserves its best repeat's partition for its example snapshot instead of reassigning it. Random Swap is unaffected and still allows at most two k-means iterations per swap.
- `--max-swaps <N>` (default 5000), `--repeats <N>` (default 1000), `--bisect-tries <N>` (default 5): RS swaps and RKM repeats per run, and bisecting tries at every split, including the first.
- `--bisect-select sse|size` (default `sse`): choose the eligible cluster with the largest SSE or the most assigned rows, including duplicates. Ties choose the lowest cluster index. The mode is recorded in `run_config.txt`, so different modes cannot be mixed in one output folder or report.
- `--init random|kmeans++` (default `random`): coordinate-distinct initial centroids of KM, RKM and RS (the split algorithms and bisecting start from the dataset mean either way). K cannot exceed the number of distinct data points. Split seeds also have distinct coordinates, and duplicate-only clusters are not eligible for splitting.
- `--repartition inward|inward-nonchild|both` selects SKM-Local remapping. The default `both` first sends child points to their nearest centroid among all clusters, then checks the other points against the children. It adapts Random Swap's rejection-and-attraction rule to the two split children; the Random Swap sources do not specify this split procedure. `inward`, the source-based comparison from Fränti, Kaukoranta & Nevalainen 1997 §2.4 and CBModules, compares every point with its current centroid and the two children, retaining the current cluster on ties. `inward-nonchild` skips points already in either child. All modes use fixed centroids for the moves and recompute touched means afterwards. If points outside the split cluster were nearest among the unchanged centroids, `both` gives nearest-centroid assignments for the fixed centroids, but updating means can invalidate them. Other algorithms are unaffected. The mode is recorded in `run_config.txt`, so different modes cannot be mixed in one output folder or report.
- `--batch` selects batch mode explicitly; `--datasets <list>` runs only the listed batch datasets (data file names without `.txt`, for example `--datasets s1,birch1`; an empty list is an error); pairing stays by sorted position.
- `-o <dir>` creates the folder and its missing parents. Every option may be given once; invalid arguments stop the program before any output folder is created. `--version` prints the version, commit and compiler.
- `--track-progress` writes the `_log.csv` rows of every run (default in batch mode). The stopwatch pauses while tracking, so it does not count in the run times (an A/B test on dim064 and unbalance measured no difference). `report` needs these rows for RS, so timed runs use `--track-progress --snapshots none`.
- `--snapshots <spec>`: `none`, or `RUNS[:ITERATIONS][:split]`, where RUNS (1-based) and ITERATIONS (the `_log.csv` counts) are `all` or lists such as `1,3-5`; `split` adds the state after each two-way split. Default: `1` in batch mode (every state of run 1), `none` in CLI mode. Examples: `--snapshots all`, `--snapshots 1:all:split`, `--snapshots 2-3:5-10`.
- `--track-inner` writes `<Algorithm>_inner.csv` (off by default). For SSE-selected splits, a selected trial's rows retain their actual evaluation times and are labeled with the split where it was applied; these times can precede rows for earlier splits. The applied state is the trial's final iteration, including a non-improving one. Bisecting records every local try as `try`, including the first split; iteration numbering restarts for each try.
- `--no-track` turns off the rows and snapshots, also in batch mode (the first perfect and failed example snapshots stay; they are written after the timed run). It cannot be combined with the three options above.
- `--log-level <1-3>`: 1 prints the setup and final statistics (default), 2 adds per-run progress, 3 adds per-swap, per-split and per-iteration debug output. Use level 1 for timing experiments.

## Subcommands: scoring finished results

These score results from any program, for example the centroids that CBModules Split-PCA writes as plain text (they replace the v1 `runDebuggery()` helper):
- `split_kmeans ci <centroids.txt> <ground-truth.txt>` prints the Centroid Index.
- `split_kmeans sse <data.txt> <centroids.txt>` prints the SSE (`%.17g`) with every point at its nearest centroid.
- `split_kmeans gt-from-partition <data.txt> <partition.txt> <out.txt>` writes the mean of every cluster. The partition file holds exactly one label per data point, an integer from 1 to N (whitespace-separated, usually one per line); a missing, extra or non-integer label is an error (exit 1), so a wrong file never gives a silently wrong ground truth.

The files are plain whitespace-separated numbers (the same parser as the data files); the dimensions must match. CBModules' own `.cb` and `.pa` formats (with a `VQ CODEBOOK` header) need exporting to text first.

The C helpers `generateGroundTruthCentroids`, `debugCalculateCI`, `debugCalculateSSE` and `runDebuggery()` (fixed paths under debuggery/, gt/, data/) remain in `src/cli.c`.

## Subcommand: report (tables and curves for Excel)

`split_kmeans report [-o dir] [--names file] [--curve-run i] [--decimal-comma] [--no-time] <batch dir>...` reads finished output folders (`_runs.csv`, `_log.csv`, `dataset.txt` and `run_config.txt`) and writes semicolon-separated tables for the thesis:
- Several folders are merged, for example the parts of a job split with `--first-run`; their `run_config.txt` headers must agree (same seed, build, machine and parameters) and a run number may appear only once per dataset and algorithm. Datasets are matched by folder name.
- `_log.csv` rows must exist for every run of an algorithm or for none (calls with and without tracking are not mixed), and RS needs them (see below); a row of a run that is not in `_runs.csv` is an error.
- `-o` (default `<first dir>/report`) receives:
  - `summary.csv`: dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct;time_ms_mean;time_ms_sd;full_time_ms_mean. Standard deviations are sample deviations (n − 1), NA with one run; relative CI is CI / K (K from `dataset.txt`, NA when missing); success is the percentage of runs with CI = 0.
  - RS results end at the last SSE improvement of each run: the time of its last `_log.csv` row, with the final SSE and CI (Random Swap keeps its best solution, the one found at that swap). If no swap improves, this is the initial count-0 row. `full_time_ms_mean` gives the RS time of all swaps.
  - `table_ci.csv` (2 decimals), `table_success.csv` (1), `table_time.csv` (ms, 2) and `table_sse.csv` (2): algorithms × datasets, as in the seminar slides.
  - `curves_<dataset>.csv`: per algorithm the columns count, ms, SSE and CI; the first row (`end`) holds the summary values, the next rows the curve. RS and RKM show run `--curve-run` (default 1; an error when that run is missing; RS up to its last improvement, RKM every repeat); the other algorithms the mean over all runs per count, where a run that has ended keeps its last state (KM runs stop at different iterations). SSE is in units of 10^e (shown in the header), e chosen from the smallest SSE of the dataset.
- `--names <file>`: one line per dataset, `<folder name> [display name]`; the listed datasets come first in that order and use the display name, the others follow sorted by name.
- `--decimal-comma` writes decimal commas (Finnish Excel); `--no-time` leaves out the time columns and `table_time.csv` (used by `make check`).
- CI needs a ground truth; without one the CI columns read NA.

## Notes and limitations

- **Batch Mode**: Input pairing is strictly by sorted order; keep file bases aligned across data/gt/centroids/.
- **CLI Mode**: Files can be located anywhere; K must be explicitly specified via `-k`.
- K files (batch mode) must contain a single positive integer (no trailing content).
- Snapshots are written for the runs and iterations `--snapshots` selects (batch mode: run 1); the `_log.csv` rows for every run.
- K-means++ seeding is off by default (`--init kmeans++`).
- Ground truth is optional in CLI mode; if omitted, CI calculations use an empty centroid set.

## Acknowledgements

- Windows portability: WinAPI headers via the MSVC toolset.
- RNG portability shims and basic filesystem helpers in `platform.h`.