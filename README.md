# Split_kMeans

C implementations of k-means and split-based k-means variants for empirical comparison. Built for a master’s thesis to study algorithmic behavior and also to gain hands-on experience with C.

- Who: Niko Ruohonen
- Why: Compare clustering algorithms and initialization strategies on multi-dimensional data; provide repeatable evaluation artifacts (SSE, CI, logs).
- What: Single-binary C program that discovers datasets, runs selected algorithms, and writes results under timestamped output folders.

License: AGPL-3.0-only (see LICENSE)

# TLDR
- Default run: SSE Split (Local Repartition, splitType=2) with random unique centroid initialization.
- All other algorithms are commented out in `main()` in `Split_kMeans/clustering_with_.c`.
- Two modes: **Directory Batch Mode** (no args) and **CLI Mode** (with args).

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
  - RNG uses system sources: rand_s/getrandom/arc4random/random_r fallback.

## Project layout

### Directory Batch Mode structure:
- data/ — input datasets, one point per line (whitespace-separated doubles). All rows must have identical dimensionality.
- gt/ — ground-truth centroids, one centroid per line (same dimensionality as corresponding data file).
- centroids/ — a `.k` file per dataset containing only one positive integer K (UTF‑8 BOM tolerated).
- outputs/<YYYY-MM-DD_HH-MM-SS>/<dataset-base-name>/ — created automatically; contains logs, snapshots, aggregates.
- Split_kMeans/*.c|*.h — source.

Pairing rule
- The program enumerates files in data/, gt/, centroids/; counts must match.
- Files are paired by sorted filename order. Keep base names aligned, e.g.:
  - data/worms_64d.txt
  - gt/worms_64d-gt.txt
  - centroids/worms_64d.k

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
- `Split_kMeans/clustering_with_.c` contains toggles in `main()` to select algorithms and iteration budgets.
- At the moment, only “SSE Split (Local Repartition)” is enabled.

## Build

Windows (Visual Studio 2022)
- Option A: Create a Console Application project and add:
  - `Split_kMeans/clustering_with_.c`
  - `Split_kMeans/platform.h`
  - `Split_kMeans/locale_utils.h`
- Ensure the file is compiled as C (the `.c` extension is sufficient in MSVC).
- Build and run (Debug/Release as desired).

POSIX (Linux/macOS, GCC/Clang)
- with Makefile (see `Split_kMeans/Makefile` for details):

Notes
- Uses C11 on POSIX via Makefile (`-std=c11`).
- No external library dependencies.

## Usage (user manual)

### Directory Batch Mode (no arguments)
Input discovery
- Program looks for:
  - data/*.txt — points
  - gt/*.txt — ground-truth centroids
  - centroids/*.k — K per dataset
- All three folders must contain the same number of files; pairs are formed by sorted filename order.

### CLI Mode (with arguments)
Syntax:
*.exe -k <K> [-r <runs>] [--track-progress] <data.txt> [gt.txt]

Runtime behavior
- On start, a folder is created: outputs/<YYYY-MM-DD_HH-MM-SS>/.
- For each dataset, a subfolder is created: outputs/<timestamp>/<dataset-base-name>/.
- Algorithms are run as configured in `main()`; switch algorithms by uncommenting the relevant calls.

Outputs per dataset (examples)
- Aggregates
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
- Numeric formatting may follow the current C locale.
- The program currently attempts to set LC_NUMERIC to a Finnish locale for friendly Excel import on Finnish systems. If your data uses dot decimals and you want to guarantee dot output, either remove the locale call or write critical results using the "C" locale.

Determinism
- Randomness is sourced from system RNGs; runs are non-deterministic by design. Seed capture is not currently implemented.

## Selecting algorithms and parameters

Open `Split_kMeans/clustering_with_.c`, see `main()`:
- Toggle algorithms by (un)commenting:
  - `runKMeansAlgorithm`
  - `runRepeatedKMeansAlgorithm`
  - `runRandomSwapAlgorithm`
  - `runRandomSplitAlgorithm`
  - `runSseSplitAlgorithm` (splitType: 0, 1, 2)
  - `runBisectingKMeansAlgorithm`
- Adjust parameters near the dataset loop:
  - loops, maxIterations, maxRepeats, maxSwaps, bisectingIterations
  - trackProgress (writes per-iteration logs/snapshots on the first run)
- Current debug settings include:
  - A restricted dataset index range in the for-loop.
  - Finnish numeric locale set at startup.

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