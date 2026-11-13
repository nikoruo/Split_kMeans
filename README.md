# Split_kMeans

C implementations of k-means and split-based k-means variants for empirical comparison. Built for a master’s thesis to study algorithmic behavior and also to gain hands-on experience with C.

- Who: Niko Ruohonen
- Why: Compare clustering algorithms and initialization strategies on multi-dimensional data; provide repeatable evaluation artifacts (SSE, CI, logs).
- What: Single-binary C program that discovers datasets, runs selected algorithms, and writes results under timestamped output folders.

License: AGPL-3.0-only (see LICENSE)

# TLDR
- Default run: SSE Split (Local Repartition, splitType=2) with random unique centroid initialization.
- All other algorithms run only when selected with `--algo` (see below).
- Two modes: **Directory Batch Mode** (no args) and **CLI Mode** (with args).
- Supports MinGW/Visual Studio on Windows and GCC/Clang on POSIX (Linux/macOS).

## Quick Start
- Clone the repo.
- Build using Visual Studio 2022 (Windows) or Makefile (POSIX).
    - build/msvc/x64/Release/Split_kMeans.exe (Visual Studio), build/release/split_kmeans.exe (MinGW) or build/release/split_kmeans (Linux) is produced.
- Run without arguments for batch mode or with arguments for CLI mode.
- See **Usage** section below for details.

## Changelog

### v1.0.0 - 26-10-2025 by Niko Ruohonen
- Initial release
- Implements K-means, RKM, Random Swap, Random Split, SKM (3 variants), Bisecting K-means
- CLI and Batch modes
- Cross-platform support (Windows MSVC, MinGW, Linux, macOS)

## Features

- Algorithms
  - K-means: standard assignment + centroid update until no SSE improvement.
  - Repeated K-means (RKM): multiple K-means runs; keep the best.
  - Random Swap: try centroid–point swaps with local repartition; keep if MSE/SSE improves.
  - Random Split k-means: grow K by repeatedly splitting a random cluster (local k-means) then refining.
  - SSE Split k-means (Split_kMeans): grow K by splitting the cluster that maximizes expected SSE drop.
    - 0 = IntraCluster: split cluster by local k-means on that cluster’s points.
    - 1 = Global: local split, global refinement.
    - 2 = LocalRepartition: local split then targeted reassignments near the split.
  - Bisecting k-means: repeatedly split the cluster with the largest SSE using multiple local tries; keep best split.

- Initialization
  - Random unique seeding (default): pick K distinct data points.
  - K-means++ seeding (available): switch by replacing `generateRandomCentroids` with `generateKMeansPlusPlusCentroids` in the spots marked by comments.

- Metrics and logs
  - SSE: Sum of Squared Errors; also MSE helper present.
  - CI: Centroid Index between result and ground truth centroids.
  - Per-iteration CSV logs and time tracking for selected algorithms.
  - Optional iteration snapshots: centroids and partitions per iteration.

- Portability
  - Windows (MSVC/Visual Studio 2022) and POSIX (GNU Make).
  - RNG: seeded PCG32 (`src/rng.c`), identical sequences on every platform for the same seed.

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
- Runs the algorithm(s) you enable in `main()`; writes results and logs into the dataset’s output folder.

Current defaults in code
- `src/main.c` contains toggles in `main()` to select algorithms and iteration budgets.
- At the moment, only “SSE Split (Local Repartition)” is enabled.

## Build

Windows (Visual Studio)
- Open `Split_kMeans.sln` in the repo root (project in `msvc/`). The debugger runs in the repo root, so batch mode finds `data/`, `gt/` and `centroids/`.
- Build and run (Debug/Release as desired).

POSIX (Linux, GCC/Clang)
- with the Makefile in the repo root: `make` builds `build/release/split_kmeans`; `make run ARGS="-k 20 data/a1.txt"` runs it from the repo root; `make help` lists the other targets.

Tests
- `make check` generates synthetic datasets in `build/testdata`, runs the algorithms with a fixed seed, compares every `<Algorithm>_runs.csv` (without the time column) with `tests/golden`, and smoke-tests the two default invocations.
- `make golden-update` regenerates `tests/golden`; use it only in commits that change an algorithm.
- The check runs write to `build/check`. Inside a OneDrive folder the tracking files hit sharing violations, so pass an output root outside it, for example `mingw32-make check CHECK_ROOT=C:/Temp` (its `check` subfolder is deleted first; no spaces in the path).

Notes
- Uses C11 on POSIX via Makefile (`-std=c11`).
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
split_kmeans [--algo <list>] [--seed <N>] [-r <runs>] [-o <dir>] [--track-progress] -k <K> <data.txt> [gt.txt]

The options also work in batch mode (no data file), for example `split_kmeans --algo skm-local,rs --seed 42`.

Runtime behavior
- On start, a folder is created: outputs/<YYYY-MM-DD_HH-MM-SS>/, and the seed is printed. `-o <dir>` writes to `<dir>` instead (created if missing; existing `<Algorithm>_runs.csv` files are appended to).
- For each dataset, a subfolder is created: outputs/<timestamp>/<dataset-base-name>/.
- The algorithms chosen with `--algo` run in a fixed order (default: skm-local only).

Outputs per dataset (examples)
- Aggregates
  - <Algorithm>_runs.csv: run;seed;sse;ci;ms, one row per run; the seed reproduces that run
  - <Algorithm>_log.csv: ci;iteration;sse;ms (semicolon-separated)
  - <Algorithm>_iteration_stats.txt: Iteration;NumCentroids;SSE;CI;SplitCluster
  - <base>.csv: a summary row per algorithm with averages:
    - Algorithm;Average CI;SSE;Relative CI;MS;Success Rate
- Snapshots (first “perfect” and first “failed”, when applicable)
  - kMeans_centroids_perfect.txt, kMeans_partitions_perfect.txt
  - kMeans_centroids_failed.txt, kMeans_partitions_failed.txt
  - RandomSwap_*, RandomSplit_*, Bisecting_*, and for SSE Split: IntraCluster|Global|LocalRepartition
- Iteration snapshots (when tracking is enabled)
  - <Algorithm>_centroids_iter_<n>.txt
  - <Algorithm>_partitions_iter_<n>.txt

Locale and CSV
- CSV files use semicolons (;).
- Numeric formatting uses the "C" locale by default (dot decimals).
- Finnish locale support is available via `set_numeric_locale_finnish()` 
  in `locale_utils.h` (currently commented out in main()).

Determinism
- All randomness comes from one seeded PCG32 generator. `--seed <N>` sets the master seed (default: the clock, printed at start). Run i of every algorithm uses a seed derived from the master seed and i, so the same seed and options reproduce the same results.

## Selecting algorithms and parameters

- `--algo <list>`: comma-separated, any of `km`, `rkm`, `rs`, `skm-random`, `skm-intra`, `skm-global`, `skm-global-basic`, `skm-local`, `bisecting` (default `skm-local`).
- `-r, --runs <N>`: runs per dataset (default 100). RKM and RS run once unless `--runs` is given.
- Defaults in `main()`: k-means until SSE stops improving, RS 5000 swaps, RKM 1000 repeats, bisecting 5 tries; options for these follow.
- `--track-progress` writes per-iteration logs and snapshots on the first run; batch mode always tracks.
- `--log-level <1-3>`: 1 prints the setup and final statistics (default), 2 adds per-run progress, 3 adds per-swap, per-split and per-iteration debug output. Use level 1 for timing experiments.

## Ground-truth and debugging helpers

These helpers are available (call them from `main()` when needed):
- `generateGroundTruthCentroids(dataFile, partitionFile, outFile)`: build centroids from data + 1-based partitions (converted to 0-based).
- `debugCalculateCI(debugCentroidsFile, groundTruthFile)`: print CI.
- `debugCalculateSSE(centroidsFile, dataFile)`: print SSE.
- `runDebuggery()`: quick fixed-file CI/SSE test (uses paths under debuggery/, gt/, data/).

## Notes and limitations

- **Batch Mode**: Input pairing is strictly by sorted order; keep file bases aligned across data/gt/centroids/.
- **CLI Mode**: Files can be located anywhere; K must be explicitly specified via `-k`.
- K files (batch mode) must contain a single positive integer (no trailing content).
- Some algorithms log only during the first loop when `trackProgress` is true.
- K-means++ seeding is implemented but not enabled by default.
- Ground truth is optional in CLI mode; if omitted, CI calculations use an empty centroid set.

## Acknowledgements

- Windows portability: SAL annotations and WinAPI headers via MSVC toolset.
- RNG portability shims and basic filesystem helpers in `platform.h`.