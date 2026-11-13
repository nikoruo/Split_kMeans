# Split_kMeans: project guidelines

## Context
- Master's thesis project (supervisor: Prof. Pasi Fränti, University of Eastern Finland).
- Fränti's reference code lives in the sibling folder `../CBModules-master_Win` (for example `src/modules/split.c`). Use it to check algorithm steps only; do not copy its coding style.
- Literature (local only, ignored by git except the `*.ps1` conversion scripts): Fränti's articles as text in `university/articles/out/text/` (page renders in `out/pages/`), course slides in `university/course/out/md/`. Fränti's articles, slides and CBModules are primary sources; Rantinoja's 2023 thesis is secondary, used only where they are silent. `Gradu_Ruohonen.txt` is the student's own draft, not a source. Use the literature agent for questions about them.
- Thesis notes for the student live in `university/thesis-notes.txt` (local only).
- Datasets (untracked): `Split_kMeans/data`, `gt`, `centroids` until step 1.2 moves them to the repo root.
- Compares k-means variants and split-based clustering (SKM Intra, Global, Local) against Repeated K-means, Random Swap, Random Split and Bisecting K-means.
- Results must be reproducible and statistically comparable. Thesis is in progress: algorithm fixes are the default behavior.
- Current code is v1.0.0 in `Split_kMeans/Clustering_with_.c`. The refactor plan lives in `docs/refactor-plan.md`.
- Phase 0 (agent setup) is done. Target layout (`src/`, `msvc/`, root `Makefile`) lands in Phase 1; until then sources and Makefile stay in `Split_kMeans/`.

## Build and test
- Primary build: `make` in the repo root (GNU Make 4.3+, gcc or clang; on Windows, `mingw32-make` from MinGW-w64).
- Supported platforms: Linux and Windows (MinGW-w64). macOS is not supported.
- Tests: `make check`. Sanitizers: `make asan check`, `make ubsan check`.
- Warning-free requirement: `make WERROR=1` must pass on gcc, clang and MinGW-w64. MSVC must build with `/W4`.
- Visual Studio project lives in `msvc/` and must keep building.
- Golden files are regenerated only with `make golden-update`, and only in algorithm-change commits.
- Windows dev laptop: WinLibs GCC 16, `mingw32-make` 4.4 and cppcheck (winget `BrechtSanders.WinLibs.POSIX.UCRT`); no clang, no sanitizers; Visual Studio 2026 MSBuild. Recipes run in cmd.exe. If `gcc` is missing in a terminal, reload PATH from the registry.

## Default settings (professor's requirement)
- The exe must run with defaults: no arguments = batch mode over `data/`, `gt/`, `centroids/`; `-k K data.txt [gt.txt]` = single dataset.
- Every new option gets a default equal to today's behavior (SKM-Local, 100 runs, RS 5000 swaps, RKM 1000 repeats, bisecting 5 tries, tracking on in batch mode).
- `make check` smoke-tests both default invocations.

## Code rules
- C11 only, no third-party dependencies, no variable-length arrays.
- All randomness goes through an explicit `Rng*` (see `src/rng.h`). No `rand()`, `rand_s()` or `RANDOMIZE` outside `rng.c`.
- No file or console I/O inside timed code at the default log level (1). Levels 2 and 3 may print inside timed code; their timings are not reported.
- Log levels (`--log-level`, default 1): 1 basic, 2 progress, 3 debug; see the Log levels table in `docs/refactor-plan.md`. Errors and warnings always print to stderr.
- Refactors are bit-identical: golden outputs must not change. Algorithm changes get their own commit, updated golden files, and a changelog entry.
- Use `%.17g` for doubles that must round-trip.
- Quality measures are SSE and CI only; success rate and relative CI derive from CI (professor's requirement). Do not add MSE, nMSE, ACC, ARI, NMI or other indices.
- Empty clusters keep their previous centroid (matches CBModules k-means).
- Platform `#ifdef`s only in `src/platform.c` / `src/platform.h`.

## Module map
To be filled in step 4.3 of `docs/refactor-plan.md`.

## Working agreement
- Make one plan step at a time, verify it, then stop.
- Never commit or push without being asked.
- Never delete unused functions, macros, files or commented-out code without the user's confirmation. List them instead (see the Unused code review section in docs/refactor-plan.md).
- Functions marked keep in that table are ported, not dropped, when their surroundings are refactored.
- Do not create markdown documentation unless requested.
