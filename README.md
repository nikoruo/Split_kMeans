# Split_kMeans

> **Status:** version 2.0.1 replaces v1.0.0 (October 2025); it is v2.0.0 with hardened Windows builds. It was developed on `feature/refactor` and is released by a reviewed merge into `main`; the tags `v2.0.0` and `v2.0.1` mark the tested release commits. The thesis experiments continue on `main`. See the [refactor plan](docs/refactor-plan.md) and [changelog](CHANGELOG.md).

Split_kMeans is a C11 program for reproducible clustering experiments. It compares k-means and split-based variants using sum of squared errors (SSE), Centroid Index (CI), and elapsed time. The project is licensed under [AGPL-3.0](LICENSE).

## Algorithms and measures

| `--algo` name | Algorithm |
|---|---|
| `km` | K-means from random or k-means++ initial centroids. |
| `rkm` | Repeated k-means; keep the repeat with the lowest finite SSE. |
| `rs` | Random Swap; accept a swap only if SSE improves, after local repartition and up to two k-means iterations. |
| `skm-random` | Random Split: repeatedly split a randomly selected eligible cluster, then refine globally. |
| `skm-intra` | Select a cluster by its tentative SSE decrease and apply its evaluated local split; refine globally at the end. |
| `skm-global` | Apply the selected evaluated split and run global k-means after subsequent splits. |
| `skm-global-basic` | For subsequent splits, select by tentative SSE decrease, then use fresh random split seeds and global k-means. |
| `skm-local` | Apply the selected local split and repartition near the two children; refine globally at the end. |
| `bisecting` | Split the eligible cluster selected by SSE or size, keep the best local try, then refine globally at the end. |

KM, RKM and RS default to coordinate-distinct random data points as initial centroids. Split algorithms and bisecting start from the dataset mean. Their split seeds are distinct by coordinate; clusters without two distinct points cannot be split. For K=2, all four SSE-selected split variants skip trial splits because no further split needs them. This changes the final RNG state, but no later K=2 step draws randomness and each run is seeded independently. Empty clusters keep their previous centroid.

SSE is the sum of squared distances to the **assigned** centroids. CI is the larger of the two orphan-centroid counts obtained by matching each centroid set to its nearest centroids in the other set. Success rate is the fraction of runs with CI = 0 (shown as a percentage in the console and report); relative CI is mean CI divided by K. Without ground truth, CI and success rate are `NA`.

Each k-means call returns the SSE and solution of its **last** iteration, including a non-improving final iteration. Floating-point rounding can make that SSE higher than an earlier one, and labels need not be nearest to the newly updated centroids. For a finite selected solution, reported SSE, labels, centroids, CI and final snapshots describe the same returned state. RKM selects the best repeat, and bisecting selects the best try.

RKM accepts finite SSEs including `DBL_MAX`. It skips repeats with non-finite SSE and warns once per affected run after the stopwatch stops; skipped repeats reduce the effective repeat count. If no repeat is finite, the call exits with an error without writing a result, progress rows or snapshots for that run; earlier completed runs remain.
Older run folders may contain the former `DBL_MAX` fallback, which cannot be distinguished from a valid `DBL_MAX` winner by score alone; regenerate or separately verify those results.

## Build and test

Supported platforms are Linux (gcc or clang, GNU Make 4.3+) and Windows (MinGW-w64 or Visual Studio 2026). macOS is not supported. The C program has no third-party library dependencies.

```sh
# Linux, from the repository root
make WERROR=1
make WERROR=1 check
```

```powershell
# Windows, from the repository root
mingw32-make WERROR=1
mingw32-make WERROR=1 check CHECK_ROOT=C:\Temp\skm-check
```

The Makefile builds `build/release/split_kmeans` on Linux and `build\release\split_kmeans.exe` on Windows. For Visual Studio, open `Split_kMeans.sln` and build **Release | x64**; the executable is `build\msvc\x64\Release\Split_kMeans.exe`. On Windows, the Makefile's release build and the Visual Studio project embed a version resource (`src/version.rc`; the Makefile compiles it with `windres` from MinGW-w64 binutils), and the Makefile's Windows release executables are stripped, to avoid antivirus false positives (plan step 8.5).

`check` regenerates synthetic data in `build/testdata`, runs unit and golden tests, checks tracking variants and report output, and smoke-tests the default batch and single-dataset invocations. Most check output goes under `CHECK_ROOT`, whose `check` subfolder is **deleted first**; the default-invocation smoke tests also write under `build/testdata/main/outputs`. Use a disposable `CHECK_ROOT` path without spaces, outside OneDrive on Windows. To test the Visual Studio executable with the same golden suite:

```powershell
mingw32-make WERROR=1 check CHECK_BIN=build\msvc\x64\Release\Split_kMeans.exe CHECK_ROOT=C:\Temp\skm-msvc-check
```

That `CHECK_BIN` check uses the Makefile's MinGW-built unit runner. Windows CI also compiles the unit suite with MSVC `/W4 /WX` and runs it against the Visual Studio executable.

Other useful targets are `debug check` (assertions enabled), `asan check`, `ubsan check`, `testdata`, `bench`, `help` and `clean`. Sanitizers require a supporting compiler; the [Windows verification recipe](.github/skills/verify/SKILL.md) uses Visual Studio's clang because MinGW gcc has no sanitizers. Windows ASan has no leak checker. `golden-update` regenerates golden files and is for algorithm-change commits only. `clean` removes Makefile builds, `build/testdata` and `build/check`, not the Visual Studio output; preserve any wanted files before using it.

`make bench` runs fixed jobs on local real datasets; on Windows it compares the MinGW build with Visual Studio by default. `BENCH_REF` selects another reference binary and `BENCH_ROOT` selects an output root. Thesis timings use the Linux gcc release build (WSL2 Ubuntu on the reference desktop) at log level 1 on an idle, plugged-in machine; do not time debug or sanitizer builds.

## Input files

Dataset and centroid files are plain text: one point or centroid per line, with the same number of space- or tab-separated finite numeric coordinates on every nonempty line. Blank lines are ignored. Use a decimal point, not a decimal comma; lines must not exceed 16,383 characters. K must be positive and no larger than the number of distinct data points. Ground-truth centroids must have the same dimension as the data; if their count differs from K, the program warns and compares the sets as given.

For default batch mode, place `.txt` files in these folders under the working directory:

```text
data/s1.txt          # data points
gt/s1-cb.txt         # ground-truth centroids
centroids/s1.txt     # one positive integer: K
```

The three directories must contain the same number of text files. Files are paired **by sorted position**, not by matching names: sorting is case-insensitive in the program's default C locale with a bytewise tie-break. Directory-enumeration failures are errors; verify the printed pairings and input hashes before experiments. Dataset names (data-file base names without the extension) may contain commas but not `;`, `"`, CR or LF; selected invalid names are rejected before any run. `--datasets` uses commas as separators, so a name containing a comma cannot be selected individually with that option. Non-text files and subdirectories are ignored. The local `data/`, `gt/` and `centroids/` datasets are not included in the repository.

Single-dataset mode takes `-k K data.txt [gt.txt]`; the ground-truth file is optional. The program writes `NA` for CI-derived values when it is absent.

## Options and defaults

Options apply to batch and single-dataset mode unless noted. Each option can be supplied at most once.

| Option | Default | Meaning |
|---|---|---|
| `--algo <list>` | `skm-local` | Comma-separated names from the algorithm table. |
| `-r, --runs <N>` | 100; RKM and RS: 1 | Last run number per dataset, not a count when `--first-run` is used. |
| `--first-run <i>` | 1 | First run number; with RKM or RS, values above 1 require `--runs`. |
| `--seed <N>` | Time-based | Non-negative master seed, printed at startup. Set it explicitly for reproducibility. |
| `-k <K>` | Required in single mode | Cluster count for the supplied data file. Batch mode reads K files. |
| `--max-iterations <N>` | Until SSE stops strictly improving | Positive per-call k-means cap for diagnostics, not a wall-clock timeout. Capped runs may not converge. RS retains its separate two-iteration limit. |
| `--max-swaps <N>` | 5000 | Maximum Random Swap trials per run; if K equals the distinct-point count, there are no swaps. |
| `--repeats <N>` | 1000 | RKM repeat attempts per run. |
| `--bisect-tries <N>` | 5 | Local tries at every bisecting split, including the first. |
| `--bisect-select sse\|size` | `sse` | Select the eligible cluster by SSE or assigned row count; ties use the lowest index. |
| `--init random\|kmeans++` | `random` | Initial centroids for KM, RKM and RS. |
| `--repartition both\|inward\|inward-nonchild` | `both` | SKM-Local remapping; other algorithms are unaffected. |
| `--batch` | Automatic without a data file | Select batch mode explicitly; it cannot be combined with `-k` or a data file. |
| `--datasets <list>` | All | Batch-only comma-separated data file names without `.txt`; pairing remains positional. |
| `--track-progress` | On in batch, off in single mode | Record progress rows for every run. |
| `--snapshots <spec>` | Batch: `1`; single: `none` | Select iteration snapshots; see [Tracking and snapshots](#tracking-and-snapshots). |
| `--track-inner` | Off | Record k-means iterations inside split algorithms and bisecting. |
| `--no-track` | Off | Disable progress rows, selected snapshots and inner rows; cannot be combined with the three tracking options above. |
| `--decimal-comma` | Off | Use decimal commas in Excel-facing output, not in input or `_runs.csv`. |
| `--log-level <1-3>` | 1 | Basic results, per-run progress, or debug output. Use level 1 for timings. |
| `-o <dir>` | `outputs/<timestamp>` | Output root; missing parent directories are created, and existing components must be directories. |
| `-h, --help`; `--version` | — | Print usage, or version, commit and compiler. |

SKM-Local's default `both` first sends child points to their nearest centroid among all clusters, then checks the other points against the children. It adapts Random Swap's rejection-and-attraction rule to the two split children; the RS sources do not specify this split procedure. `inward`, the source-based comparison from Fränti, Kaukoranta & Nevalainen 1997 §2.4 and CBModules, checks each point against its current centroid and both children. `inward-nonchild` skips child points. Moves use fixed centroids, then touched means are recalculated. If points outside the split cluster were nearest among the unchanged centroids, `both` gives nearest-centroid assignments for the fixed centroids; the mean updates can invalidate that property.

All random choices use seeded PCG32. Each algorithm run derives its own seed from the master seed and run index. To reproduce run 51, use the same master seed with `--first-run 51 --runs 51`; for runs 51 through 100 use `--first-run 51 --runs 100`.

## Running examples

With the required batch folders present, running the executable with **no arguments** starts the default batch:

```sh
./build/release/split_kmeans
# Single dataset, with or without ground truth:
./build/release/split_kmeans -k 15 data/s1.txt gt/s1-cb.txt
./build/release/split_kmeans -k 15 data/s1.txt
```

```powershell
.\build\release\split_kmeans.exe
```

For a bounded example using generated data, choose a new output folder outside OneDrive. The following runs SKM-Local and RS once, records progress and split snapshots, and creates a report:

```sh
# Linux
make testdata
./build/release/split_kmeans --seed 42 --runs 1 --algo skm-local,rs --max-swaps 50 \
  --track-progress --track-inner --snapshots 1:all:split \
  -o /tmp/skm-example -k 4 build/testdata/main/data/s01_blobs2d.txt \
  build/testdata/main/gt/s01_blobs2d.txt
./build/release/split_kmeans report -o /tmp/skm-example-report /tmp/skm-example
export SKM_RUN=/tmp/skm-example
```

```powershell
# Windows PowerShell
mingw32-make testdata
$run = Join-Path $env:TEMP ("skm-example-" + (Get-Date -Format yyyyMMdd-HHmmss))
.\build\release\split_kmeans.exe --seed 42 --runs 1 --algo 'skm-local,rs' --max-swaps 50 `
  --track-progress --track-inner --snapshots '1:all:split' `
  -o $run -k 4 build\testdata\main\data\s01_blobs2d.txt `
  build\testdata\main\gt\s01_blobs2d.txt
.\build\release\split_kmeans.exe report -o "${run}-report" $run
$env:SKM_RUN = $run
```

The report command accepts single-dataset output folders as well as batch output folders. For a batch subset, omit `-k` and the input files and use, for example, `--datasets s1,s4 --algo skm-local --seed 42 -r 10`. Batch mode still requires all three input directories to have matching file counts. The default timestamp uses local time; clock or conversion errors terminate the call.

Each output root has a `run_config.txt` with build, machine, seed and parameter information plus a record of each call. Recorded arguments can contain local paths, so review them before sharing. Reusing `-o` requires a matching configuration header and, for each existing dataset, matching K, N, D and SHA-256 fingerprints of the raw data and ground-truth bytes. Identical bytes at different paths match; LF and CRLF files have different fingerprints, even when they parse identically. No ground-truth file is recorded as `none`, distinct from an empty file. Existing results without the required metadata or fingerprints cannot be appended to; use a new folder. Use distinct run numbers. Different repartition or bisecting selection modes cannot share a run folder.

During experiment loading, the data file and any ground-truth file are each read once in binary mode into a snapshot, hashed and parsed from that same snapshot before timing begins. This temporarily needs additional memory for the raw file. K is recorded as a value, not as a hash of the K file. SHA-256 identifies the recorded bytes; it does not authenticate results or detect changes to the parser, so the build configuration still matters. Paths in `dataset.txt` escape backslashes, single quotes and control characters.

Input-file read, result-file write, flush or close, and directory-enumeration failures exit nonzero. A failed call can leave incomplete output files; do not treat them as finished results.

## Tracking and snapshots

`--snapshots` accepts `none` or `RUNS[:COUNTS][:split]`. Runs are 1-based; counts are the progress counts and may include 0. Each selection is `all` or a comma-separated list and ranges such as `1,3-5`. Counts default to `all`; `split` additionally requests the state immediately after a two-way split where available. Examples:

```text
--snapshots 1
--snapshots 1:all:split
--snapshots 2-3:5-10
```

Batch mode defaults to progress rows for every run and snapshots for run 1; single-dataset mode defaults to neither. Snapshot selection is separate from progress rows. KM records iterations; RKM records the best finite solution after each repeat and snapshots improvements, with `NA` for SSE and CI until its first finite repeat; RS records its initial solution and improvements; split and bisecting algorithms record split steps and their final refinement where applicable.

Tracking work and snapshot I/O pause the monotonic stopwatch. Use `--track-progress --snapshots none --log-level 1` for timed runs that will be reported. In particular, RS **must** have progress rows for `report`; `--no-track` is unsuitable for those runs. Levels 2 and 3 may print inside timed code, so do not use their timings for the thesis.

The output root contains `run_config.txt` and one subfolder per dataset. Algorithm filename prefixes are `KM`, `RKM`, `RS`, `SKM_Random`, `SKM_Intra`, `SKM_Global`, `SKM_Global_Basic`, `SKM_Local` and `Bisecting_KM`.

| File in a dataset folder | Contents |
|---|---|
| `dataset.txt` | Escaped input paths, raw-byte SHA-256 fingerprints (or `none` for absent ground truth), K, number of points and dimensions. |
| `<Algo>_runs.csv` | `run;seed;sse;ci;ms`, one row per run. `seed` is the derived run seed; reproduce via the master seed and run number. |
| `<Algo>_log.csv` | `run;count;sse;ci;ms`, buffered progress rows written after each run. |
| `<Algo>_inner.csv` | With `--track-inner`: `run;count;stage;iteration;k;sse;ci;ms`. Stages include `split`, `try`, `global` and `final`. |
| `<Algo>_iteration_stats.txt` | `Run;Iteration;Phase;NumCentroids;SSE;CI;SplitCluster;MS`, one row per selected snapshot. |
| `<Algo>_run<RRR>_iter<IIII>_<phase>_centroids.txt` | Snapshot centroids, one whitespace-separated row per centroid. |
| `<Algo>_run<RRR>_iter<IIII>_<phase>_partitions.txt` | Snapshot labels, one **0-based** label per input point. |
| `<dataset>.csv` | Per-call algorithm summary; `Success Rate` is a fraction from 0 to 1. Mean SSE uses an overflow-safe calculation when its sum is non-finite. |

Snapshot phases include `start`, `split`, `step` and `final` where applicable. Run and count padding are minimum widths; names with more than three run digits or four count digits do not necessarily sort numerically. In an SKM-Global-Basic `split` snapshot, new centroids are placed before global reassignment, so labels still describe the previous partition; bisecting snapshots already contain the selected trial labels. When ground truth is present, the first perfect and first failed example solutions can also be saved as `<Algo>_centroids_perfect.txt`, `<Algo>_partitions_perfect.txt` and corresponding `failed` files. These example files are independent of `--no-track`; without ground truth they are not saved.

For SSE-selected splits, a selected trial's inner rows retain their evaluation timestamps and are appended when that trial is applied. Their timestamps can therefore precede rows for earlier applied splits. Bisecting records each try.

CSV files use semicolons. `_runs.csv` writes SSE and milliseconds with `%.17g` and a decimal point. Excel-facing progress, inner, summary and iteration-stat files use fixed decimals and follow `--decimal-comma`. Snapshot centroids are written with **six decimal places**, so they are intended for figures, not exact floating-point comparisons. Snapshot labels stay 0-based.

## Scoring existing results

```text
split_kmeans ci <centroids.txt> <ground-truth.txt>
split_kmeans sse <data.txt> <centroids.txt>
split_kmeans gt-from-partition <data.txt> <partition.txt> <out.txt>
```

`ci` prints the Centroid Index. `sse` assigns each point to its nearest supplied centroid and prints SSE with `%.17g`; this can differ from a run's assigned-partition SSE, especially when using rounded snapshots. A non-finite SSE is an error. Both commands require nonempty, dimension-compatible files.

`gt-from-partition` accepts exactly one integer label per data point, separated by spaces, tabs or line breaks, in the range 1 through N. It writes one centroid per label through the largest label, with six decimal places; a missing label produces a zero centroid and a warning on stderr using a 0-based cluster index. Errors also go to stderr; informational messages remain on stdout. Snapshot partitions are 0-based and must be converted to 1-based labels before using this command.

## Reports

```text
split_kmeans report [-o dir] [--names file] [--curve-run i] [--decimal-comma] [--no-time] <run-folder>...
```

`report` merges finished folders into `summary.csv`, `table_ci.csv`, `table_success.csv`, `table_sse.csv`, `table_time.csv` and `curves_<dataset>.csv`. `report_info.txt` records whether input identities were recorded. The default output is `<first run-folder>/report`; use a new `-o` folder to keep reports separate.

- Configuration headers must match, and a run number may occur only once per dataset and algorithm. Dataset folders are matched by name; matching datasets must have the same K, N, D and data and ground-truth fingerprints. Every dataset in a multi-folder report must have fingerprints, even if it appears in only one folder.
- A single historical run folder with valid `dataset.txt` files but no fingerprints can still be reported: a warning goes to stderr and `report_info.txt` says `input identity: not recorded`. Missing or malformed metadata is an error. This exception does not authenticate historical results or permit appending to them.
- For each algorithm, either every run has `_log.csv` rows or none does. RS requires them. Use the same master seed and non-overlapping `--first-run`/`--runs` ranges to split a job.
- `summary.csv` reports mean SSE, sample SSE deviation, mean CI, relative CI, success percentage, mean time and sample time deviation. Deviations are `NA` for one run.
- RS tables and curves end at each run's **last SSE improvement**, determined afterwards from its last progress row; if there was no improvement, that is the count-0 initial row. `full_time_ms_mean` retains the time for the full run, including all performed swaps. This is a retrospective reporting rule, not an online stopping rule.
- RKM and RS curves use `--curve-run` (default 1) when progress rows exist. RKM curve points before the first finite repeat have `NA` SSE and CI. Other curves average the runs at each count, retaining a finished run's last state. Without rows, an algorithm has only its end point. Curve SSE is scaled by the power of ten shown in its header, chosen from the smallest positive SSE in that dataset's means and curves.
- `--names <file>` lists `<dataset-folder> [display name]` per line; listed datasets come first in that order. Dataset folder and display names may contain commas but not `;`, `"`, CR or LF; invalid names are rejected before reports are written. Line endings delimit entries. `--decimal-comma` formats the report for a decimal-comma spreadsheet. `--no-time` omits time columns and `table_time.csv`.

The standard `summary.csv` header is:

```text
dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct;time_ms_mean;time_ms_sd;full_time_ms_mean
```

## Cluster figures

[`visuals/generalizedplotting.m`](visuals/generalizedplotting.m) draws **2-D** data from snapshot prefixes or `{centroidsFile, partitionsFile}` pairs. It accepts a single snapshot or a cell array of snapshots; panels share axis limits. `Titles`, `GroundTruth`, `Highlight`, `Zoom`, `Layout`, `Output` and `Visible` control the figure. Highlighting compares consecutive snapshots and marks moved points. `Output` writes both a 300-dpi PNG and a PDF.

After the generated-data example above, start MATLAB or Octave in the repository root with `SKM_RUN` set to the chosen `-o` folder:

```matlab
addpath('visuals');
runDir = getenv('SKM_RUN');
data = fullfile('build', 'testdata', 'main', 'data', 's01_blobs2d.txt');
split = fullfile(runDir, 's01_blobs2d', 'SKM_Local_run001_iter0001_split');
step = fullfile(runDir, 's01_blobs2d', 'SKM_Local_run001_iter0001_step');
fig = generalizedplotting(data, {split, step}, 'Titles', {'Split', 'Repartition'}, ...
    'Highlight', true, 'Visible', 'off', 'Output', fullfile(runDir, 'example'));
close(fig);
```

For interactive file selection, call `generalizedplotting()` without arguments. The fuller [`demo_figures.m`](visuals/demo_figures.m) workflow requires local S4 data and generated test data; its header lists the required clustering calls.

The scripts have been tested with GNU Octave 11.3. Invisible Octave figures use the gnuplot toolkit for export. MATLAB R2026b is the intended target, but the MATLAB demo has **not yet been verified** and remains due before the thesis figures.

## Project layout

- `src/`: C sources and headers; `msvc/`: Visual Studio project.
- `tests/`: synthetic-data generator, unit tests and golden outputs.
- `visuals/`: MATLAB/Octave figure scripts.
- `results/`: tracked milestone and experiment summaries; full run folders stay local outside OneDrive.
- `docs/refactor-plan.md`: decisions, milestones and release plan.
