# Split_kMeans refactor plan (v2.0.0)

Goal: correct, reproducible, fast and maintainable split k-means experiments for the master's thesis.

Rules: refactors keep golden outputs bit-identical. Algorithm changes are one commit each, with `make golden-update`, a changelog entry and before/after statistics.

Build target for the professor: `make && make check` in the repo root (Linux). Visual Studio project kept in `msvc/`. Supported platforms: Linux and Windows (MinGW-w64). macOS is not supported.

Default settings (professor's requirement): the exe runs with no arguments (batch mode) and with `-k K data.txt [gt.txt]`. Every new option defaults to today's behavior; `make check` smoke-tests both invocations.

## Phase 0: Agent setup
- [x] 0.1 `.github/copilot-instructions.md`
- [x] 0.2 `.github/instructions/c.instructions.md`
- [x] 0.3 `.github/instructions/makefile.instructions.md`
- [x] 0.4 `.github/agents/c-reviewer.agent.md`
- [x] 0.5 `.github/agents/clustering-auditor.agent.md`
- [x] 0.6 `.github/prompts/verify.prompt.md`
- [x] 0.7 `.github/prompts/plan-step.prompt.md`
- [x] 0.8 `docs/refactor-plan.md`
- [x] 0.9 `.gitignore`: `university/` ignored except the `*.ps1` conversion scripts (copyrighted PDFs and renders)
- [x] 0.10 Clustering auditor: local sources ranked, quote-and-page rule, variant map, verified facts
- [x] 0.11 `.github/agents/literature.agent.md`: answers from the local literature with quotes; edits only `university/thesis-notes.txt`
- [x] 0.12 `.github/prompts/experiment.prompt.md`: release build, log level 1, fixed seed, pilot run first, SSE/CI summary (usable after 1.6 and 1.6b)
- [x] 0.13 c-reviewer: checks for log-level tests in per-point loops, output in timed code, randomness outside `Rng*`
- [x] 0.14 plan-step: Phase 6 steps run the clustering auditor first and cite the paper in the changelog
- [x] 0.15 `.github/copilot-instructions.md`: literature paths and ranking, SSE and CI only, empty-cluster rule
- [x] 0.16 `university/thesis-notes.txt` (local only): verified quotes, variant map, local repartition material, draft issues, methodology checklist

## Phase 1: Foundation
- [x] 1.1 Repo hygiene: `.gitignore` entries for the root `/data/`, `/gt/`, `/centroids/`, `/outputs/`, `/build/` (`Backups/` is already covered by `Backup*/`), `.gitattributes`, `.editorconfig`, `.clang-format`
- [x] 1.2 Move files: `src/main.c`, `src/platform.h`, `src/locale_utils.h`; move VS project to `msvc/` with the debugger working directory at the repo root; move the untracked `data/`, `gt/`, `centroids/` and `Backups/` folders from `Split_kMeans/` to the repo root (you approved) (depends on none)
- [x] 1.3 Root `Makefile`, GNU Make 4.3+; release builds define `NDEBUG` so asserts are off, debug and sanitizer builds keep them (depends on 1.2)
- [x] 1.4 VS project with wildcard sources, C11, `/W4`, `/utf-8` (parallel with 1.3)
- [x] 1.5 `src/rng.[ch]`: PCG32 + SplitMix64, unbiased bounded draws; remove `RANDOMIZE` (fixes the MinGW `rand_s` error); replace `d_type` with `stat()` in `list_files` and guard the `_Analysis_assume_` redefinition, so the code builds on MinGW from here on
- [x] 1.6 CLI `--seed` (default: time-based, printed), `--algo` (default `skm-local`; add `skm-global-basic` for `splitClusterGlobal`); per-run `<Algo>_runs.csv`; guard uninitialized `csvFile`; main()'s hard-coded parameters become the option defaults; remove the 12 commented-out algorithm toggles in `main()` (you approved)
- [x] 1.6b Log levels: `LOGGING` becomes the runtime option `--log-level 1|2|3` (default 1); uncomment the logging blocks at the levels in the Log levels table; fix the three blocks that do not compile or crash (`SSE(...)` in `runRandomSplit`, undefined `repeat`/`result1` in `runBisectingKMeans`, CI without ground truth in `runKMeans`); level checks only per run, swap, split or iteration, never per point (depends on 1.6)
- [x] 1.7 `tests/gen_data.c`: integer-only synthetic scenarios and edge cases (parallel with 1.6); `make testdata` writes 5 main scenarios and 7 edge cases to `build/testdata/{main,edge}`
- [x] 1.8 `tests/compare_runs.c`, `make check`, `make golden-update`; smoke tests for both default invocations; recipes work in cmd.exe and sh; edge case `e01_k1` crashes today (heap corruption, K=1), so it joins the golden set only after 2.3 (depends on 1.6, 1.7). Done: `-o <dir>` landed here (3.5 still owns the strict CLI); 93 golden files (bisecting is left out of the edge runs, see 2.4); `CHECK_ROOT` moves the check output out of OneDrive, where the per-swap tracking files hit sharing violations (3.2 buffers them)
- [x] 1.9 CI: `WERROR=1`, gcc and clang, `make check`; fix the remaining warnings (`set_numeric_locale_finnish` non-static, unused loop counter in `runBisectingKMeans`, gcc `-Wformat-truncation` on the output-path `snprintf` calls in `writeCentroidsToFile` and `writeResultsToFile`); keep the default-settings smoke tests; delete `ci-fixtures/` and point the workflow at generated data (you approved). Done: `set_numeric_locale_finnish` moved to `src/locale_utils.c`; the truncation checks also cover `writeDataPointPartitionsToFile`; `handleMemoryError` takes `void*` (gcc 16 `-Wmaybe-uninitialized` at -O0); the old CLI smoke test with `-k 1` is gone (K=1 crashes until 2.3)

## Phase 2: Crashes, undefined behavior, validation (golden files identical)
- [ ] 2.1 Boundary validation always on (N, D, K <= N, GT dimensions, parameters >= 1); per-point checks become `assert`s; fix the two commented checks that do not compile before re-enabling them
- [ ] 2.2 Optional ground truth: CI and success rate reported as NA without it
- [ ] 2.3 K edge cases: K=1 no split (`build/testdata/edge/e01_k1` crashes today); arrays sized max(K,2); SKM-Global K=2 runs global k-means
- [ ] 2.4 Split selection: only clusters with >= 2 points; clear error otherwise; k-means++ fallback; then add `bisecting` to the edge check runs (`EDGE_ALGOS` in the Makefile; today it loops forever on `e02_k_equals_n` when it picks a one-point cluster) and add its 6 edge golden files
- [ ] 2.5 RKM: correct `bestCI`; relabel points with best centroids before snapshot
- [ ] 2.6 Parser: check `strtod` endptr, reject NaN/Inf, detect long lines; remove `getNumDimensions`
- [ ] 2.7 Paths: checked path-join; `list_files` in `platform.c`; `*.txt` only; same sort on all OSes (the `d_type` fix lands in 1.5); delete the unused `STAT`, `MKDIR_OK`, `PRAGMA_MSVC` macros
- [ ] 2.8 ASan and UBSan clean; sanitizer job in CI

## Phase 3: Measurement, outputs, CLI (SSE/CI unchanged)
- [ ] 3.1 Monotonic wall-clock stopwatch with pause/resume
- [ ] 3.2 Tracking paused and buffered; level 1 prints nothing inside timed code; tracking and log context carries the ground truth (replaces the removed `groundTruth` parameters)
- [ ] 3.3 Separate `--track-progress` and `--snapshots`; keep today's defaults (batch mode: tracking on, CLI: off); add `--no-track` (works in batch mode too); RS keeps one CSV row per swap and its commented alternatives stay as comments; add `--track-inner` (default off): per-iteration SSE and CI of the k-means runs inside the split algorithms
- [ ] 3.4 `%.17g` output; `run_config.txt`; `--decimal-comma`
- [ ] 3.5 Strict CLI: `--max-swaps`, `--repeats`, `--bisect-tries`, `--init`, `-o`, `--batch`, `--version`, each defaulting to today's value; output folder created after validation; RKM and RS keep 1 run by default
- [ ] 3.6 Batch pairing stays by sorted position (ground-truth names differ, for example `birch1.txt` with `b1-gt.txt`); print the data, ground-truth and K file names on the level-1 Dataset line; warn when the ground-truth centroid count differs from K
- [ ] 3.7 Subcommands: `ci`, `sse`, `gt-from-partition`
- [ ] 3.8 Remove the items you marked delete in the Unused code review (`deepCopyDataPoints`, the `groundTruth` parameters, `ClusteringResult.centroidIndex` and `.partition`); `enum Algorithm`; fix docs

## Phase 4: Split into modules (bit-identical)
- [ ] 4.1 Modules: util, platform, rng, dataset, metrics, kmeans, split, baselines, algorithms, tracking, experiment, cli, main
- [ ] 4.2 `tests/test_unit.c`
- [ ] 4.3 Fill module map in `.github/copilot-instructions.md`

## Phase 5: Performance (bit-identical)
- [ ] 5.1 Flat `Dataset` and `Centroids` (capacity K); remove `DataPoint` structs; port every function marked keep in the Unused code review
- [ ] 5.2 Inline squared distance, same summation order
- [ ] 5.3 Scratch memory allocated once per run
- [ ] 5.4 Index views over clusters; shared local 2-means; O(N) membership
- [ ] 5.5 One-pass per-cluster SSE
- [ ] 5.6 Remove redundant work that cannot change results (keep RNG call order)
- [ ] 5.7 `make bench`; record before/after; profile; confirm the log-level checks cost nothing measurable at level 1 (if they do, add a compile-time `LOG_MAX_LEVEL`)

## Phase 6: Algorithm fixes (one commit each; `splitClusterGlobal` stays unchanged as the basic comparison)
References (see the clustering auditor for paths): Fränti, Kaukoranta & Nevalainen 1997 §2.4 (local repartitioning), Fränti 2018 Random Swap p.5 to 7, CBModules `src/modules/split.c`, course slides (`4. RandomSwap` slide 11, `17, Clustering-Part2b` slides 17 to 37); Rantinoja 2023 as a secondary source. `Remapping` recomputes the centroid of every cluster it changes (6.1), `SplitPartition` reuses the precalculated tentative centroids (6.2), `SplitCluster` refuses clusters with at most one unique vector (6.6).
- [ ] 6.1 SKM-Local: both passes stay; they read one fixed set of centroids; afterwards recompute centroids once for the split cluster, the new cluster and every cluster that lost or gained points (no second pass); recompute SSE and the tentative SSE drop for those clusters only; an emptied cluster keeps its centroid with SSE 0 and drop 0 (level-3 warning); level-3 counters for touched clusters and moved points per pass (expect about 4 to 6 touched clusters per split in 2-D, Fränti 2018 p.15)
- [ ] 6.1b `--repartition both|inward` (default `both`): `inward` skips Pass 1, as in 1997 §2.4 and CBModules `Remapping`; golden case for `inward`; compare both with the experiment prompt (100 runs: SSE, CI, success rate, time, touched clusters per split) (depends on 6.1, 3.5)
- [ ] 6.2 SSE-split: apply the evaluated tentative split, not a re-split
- [ ] 6.3 Bisecting: keep the selection (largest-SSE cluster, best try by SSE; same as Rantinoja §3.2); split only the chosen cluster instead of the global `partitionStep`; use the tries for the first split too; keep and document the final k-means
- [ ] 6.4 Random Swap per Fränti 2018 p.5 to 7: the first comparison is against the initial solution's SSE (random representatives + optimal partition); update the affected centroids after the local repartition (step 4; today commented out); then 2 k-means iterations
- [ ] 6.5 Split algorithms start from K=1 with the dataset mean (1997 §2; low impact: the first split overwrites this centroid, so only the random stream and the first snapshot change)
- [ ] 6.6 Distinct-coordinate seeds for local 2-means
- [ ] 6.7 100-run comparison (SSE, CI, relative CI, success rate, time) and changelog

## Phase 7: Docs, CI, re-validation
- [ ] 7.1 Rewrite `README.md`, including a table of the default settings
- [ ] 7.2 CI matrix: Linux gcc/clang, sanitizers, MSVC, MinGW (MSYS2)
- [ ] 7.3 Re-validate on the local SIPU sets in `data/` (not committed) against thesis v1.0.0 tables
- [ ] 7.4 Tag `v2.0.0`

## Unused code review (your decision)
Nothing here is deleted until you write delete in the Decision column. Blank means undecided. Line numbers refer to src/main.c (formerly Split_kMeans/Clustering_with_.c) unless noted.

| Item | Location | Status | Recommendation | Decision |
|---|---|---|---|---|
| generateKMeansPlusPlusCentroids | line 1435 | toggle-only, commented alternative in three wrappers | keep (you confirmed) | keep |
| splitClusterGlobalV2 (runs for splitType 1) | line 2350 | runs only with splitType 1, not in the default run; the thesis variant 1 | keep (you confirmed) | keep |
| splitClusterGlobal (basic global) | line 2454 | no call site; its doc says it gives worse results; the comparison for the thesis | keep (you confirmed); runnable as `skm-global-basic` from 1.6 | keep |
| calculateEuclideanDistance | line 494 | no call site | keep (you confirmed) | keep |
| calculateMSE | line 1552 | no call site | keep (you confirmed) | keep |
| deepCopyDataPoints | line 1003 | no call site | delete (you confirmed); removed in 3.8 | delete |
| freeStringList | line 345 | no call site; main() frees the same lists by hand | keep (you confirmed); use it in main() | keep |
| generateGroundTruthCentroids | line 3519 | no call site; README documents it | keep (you confirmed) | keep |
| runKMeansAlgorithm, runRepeatedKMeansAlgorithm, runRandomSwapAlgorithm, runRandomSplitAlgorithm, runBisectingKMeansAlgorithm | lines 3078, 3141, 3249, 3316, 3454 | toggle-only, enabled by uncommenting calls in main() | keep; selectable with `--algo` since 1.6 | keep |
| randomSwap, localRepartitionForRS, runRandomSplit, runBisectingKMeans, runKMeansWithTracking, deepCopyCentroids, tentativeSplitterForBisecting, allocateClusteringResult, freeClusteringResult | lines 2101, 1942, 2740, 2954, 2047, 1029, 2663, 421, 304 | helpers of the toggle-only wrappers | keep with their wrappers | keep |
| runDebuggery, debugCalculateCI, debugCalculateSSE | lines 3769, 3703, 3739 | toggle-only debug helpers | keep (you confirmed) | keep |
| set_numeric_locale_finnish | locale_utils.h line 58 | toggle-only; static, so it fails WERROR when unused | keep (you confirmed), non-static with a header declaration; never call setlocale globally; decimal comma goes into the CSV output only (step 3.4) | keep |
| LOGGING | line 143 | unused static const; gcc -Wall warns; 30 commented logging lines use it | keep (you confirmed); now the runtime `logLevel` set by `--log-level` (default 1); blocks uncommented in 1.6b per the Log levels table | keep |
| STAT, MKDIR_OK, PRAGMA_MSVC | platform.h | unused macros | delete (your call delegated); removed in 2.7 | delete |
| ClusteringResult.centroidIndex | line 431 | written, read only in a broken comment (line 3006) | delete (you agreed); removed in 3.8 | delete |
| ClusteringResult.partition | lines 432 to 437 | allocated with N entries per bisecting try, never read | delete (you agreed); removed in 3.8; `ClusteringResult` then holds the SSE and two centroids of one bisecting try | delete |
| groundTruth parameter of runKMeans (line 1992), splitClusterIntraCluster (line 2239), splitClusterGlobalV2 (line 2350) | runKMeans never reads it (line 1994); the split helpers only pass it on; the wrappers still use groundTruth for CI | dead parameter in these three functions | remove in 3.8 (you confirmed); k-means results unchanged; the tracking and log context from 3.2 carries the ground truth, so level-3 logs and `--track-inner` keep per-iteration CI | remove |
| scaling parameter | used in writeResultsToFile (line 1106) and printStatistics (line 1230); the (void) cast at line 1228 is stale | keep (you confirmed; the professor's scale is undecided) | keep |
| 88 commented-out statements: 12 toggles, 30 logging/debug, 40 validation, 6 other | Clustering_with_.c | commented out, not deleted | validation: restore in step 2.1; logging: uncomment in 1.6b; algorithm toggles in main(): removed in 1.6; k-means++ toggles: replaced by `--init` in 3.5; runDebuggery toggle: subcommands in 3.7; other: keep | decided |
| RS tracking variants: log every swap or only improvements (lines 2180 to 2183 and 2208 to 2211) | randomSwap | commented alternatives; CSV tracking, not console logging | keep today's behavior (one CSV row per swap); the alternatives stay as comments | keep |
| Two commented checks that do not compile when uncommented: findNearestCentroid (line 1602, uses targetPoints) and splitClusterGlobal (line 2466, return; in a double function) | Clustering_with_.c | broken | fix in step 2.1 before re-enabling | fix |
| ci-fixtures/ | repo root | used by linux-build.yml | delete in 1.9 when CI switches to generated data (you approved) | delete |
| main() variables maxSwaps, maxRepeats, bisectingIterations, maxLoops | lines 4036 to 4039 | unused while the calls are commented; gcc warns | became the `Options` defaults in 1.6 (maxLoops = 1 run for RKM and RS unless `--runs` is given); command-line flags follow in 3.5 | keep |
| loop counter i in runBisectingKMeans | line 2986 | set, never used; gcc warns | remove in 1.9 (needed for WERROR=1; you approved) | remove |

## Log levels (you confirmed: default 1)
Level 1 is basic: what runs and the final results. Level 2 is progress for your own runs. Level 3 is debugging. Errors, warnings, help text and subcommand output (`ci`, `sse`, `gt-from-partition`, debug helpers) always print. Timings for the thesis come from level-1 runs; `run_config.txt` records the level (3.4).

| Message | Location (current line) | Level |
|---|---|---|
| Mode banner, dataset name, N, D, K, runs, algorithm name | main (3909 to 3912, 3944, 4029, 4030), wrappers (3088, 3160, 3260, 3327, 3397, 3465) | 1 |
| Final statistics | printStatistics (1230 to 1233) | 1 |
| "Starting process" | main (4028) | 2 |
| "Round i" per run | wrappers (3092, 3164, 3264, 3331, 3401, 3469) | 2 |
| "Read N centroids from file" | readCentroids (802) | 2 |
| RS improvement: round, best CI, best SSE | randomSwap (2167 to 2171) | 2 |
| "Swap i" for every swap | randomSwap (2131) | 3 |
| "Repeat j" for every repeat | runRepeatedKMeansAlgorithm (3178) | 3 |
| Per-iteration CI and SSE, "Best SSE so far" | runKMeans (2006 to 2014), runKMeansWithTracking (2066 to 2074); CI skipped without ground truth and for the local 2-means | 3 |
| Per-split K, CI, SSE (all split variants, not only Local) | runRandomSplit (2763; fix `SSE` to `calculateSSE`), runSseSplit (2911) | 3 |
| Bisecting try j and SSE | runBisectingKMeans (3006; fix undefined `repeat`, `result1` and the "(RKM)" label) | 3 |
| Centroid dump after reading | readCentroids (804 to 813) | 3 |
| Empty-cluster warning | centroidStep (1700) | 3 |
| CI internals: set sizes, first attributes, orphan counts | calculateCentroidIndex (1761 to 1772; was level 2; guard empty sets) | 3 |

## Open decisions
- Resolved: thesis variant 1 is splitClusterGlobalV2 (what splitType 1 runs). splitClusterGlobal stays as the basic comparison.
- Known: the current code does not build with MinGW gcc 16. platform.h has an implicit rand_s and a struct dirent d_type error. Step 1.5 fixes both.
- Windows dev toolchain: WinLibs gcc 16.2, mingw32-make 4.4.1, cppcheck; no clang (clang runs in Linux CI only); Visual Studio 2026 MSBuild. mingw32-make runs recipes in cmd.exe.
- Resolved: the default run executes SKM-Local only (`--algo skm-local`); the other algorithms run only when selected.
- Resolved: defaults stay (RKM 1000 repeats, RS 5000 swaps, RKM and RS 1 run, others 100). The thesis states that Fränti & Sieranoja 2018 used 100 RKM repeats.
- Resolved: empty clusters keep their previous centroid, as in CBModules k-means (`OptimalRepresentatives`). CBModules GLA fills empty clusters instead (`FillEmptyPartitions`); the articles are silent.
- Resolved: measures are SSE and CI only (success rate and relative CI derive from CI); no nMSE, ACC or other indices.
- Resolved: SKM-Global runs global k-means to convergence after each split (Fränti's request), not the two GLA iterations of the 1997 SGLA.
- Resolved: SKM-Local keeps both passes by default; `--repartition` (6.1b) adds the inward-only variant.
- Resolved: the repo stays in OneDrive.

No open decisions.
