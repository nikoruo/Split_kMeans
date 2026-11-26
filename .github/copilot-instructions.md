# Split_kMeans: project guidelines

## Context
- Master's thesis project (supervisor: Prof. Pasi Fränti, University of Eastern Finland).
- Fränti's reference code lives in the sibling folder `../CBModules-master_Win` (for example `src/modules/split.c`). Use it to check algorithm steps only; do not copy its coding style.
- Literature (local only, ignored by git except the `*.ps1` conversion scripts): Fränti's articles as text in `university/articles/out/text/` (page renders in `out/pages/`), course slides in `university/course/out/md/`. Fränti's articles, slides and CBModules are primary sources; Rantinoja's 2023 thesis is secondary, used only where they are silent. `Gradu_Ruohonen.txt` is the student's own draft, not a source. Use the literature agent for questions about them.
- Comments in `src/` (doc comments, `@note`, `@errors`, commented-out code) come from an older AI-assisted version and are unverified claims, not a source. Current behavior is what the code does; intended behavior comes from the primary sources or the user. Never change code to match a comment on its own; report the mismatch.
- Thesis notes for the student live in `university/thesis-notes.txt` (local only).
- Datasets (untracked): `data/`, `gt/`, `centroids/` (and `Backups/`) in the repo root.
- Compares k-means variants and split-based clustering (SKM Intra, Global, Local) against Repeated K-means, Random Swap, Random Split and Bisecting K-means.
- Results must be reproducible and statistically comparable. Thesis is in progress: algorithm fixes are the default behavior.
- Code: the v2.0.0 refactor is in progress on `feature/refactor` (v1.0.0 is on `main`). The refactor plan lives in `docs/refactor-plan.md`; see the module map below.
- Layout: sources in `src/`, Visual Studio project in `msvc/` (solution at the root), Makefile at the root, cluster figures in `visuals/` (MATLAB code that must also run unchanged in GNU Octave); Makefile builds go to `build/<config>/` (release, debug, asan, ubsan), Visual Studio builds to `build/msvc/`. Milestone and final result summaries go to `results/<name>/` (tracked in git, plan D4); run folders with tracking logs and snapshots stay local, outside OneDrive.

## Build and test
- Primary build: `make` in the repo root (GNU Make 4.3+, gcc or clang; on Windows, `mingw32-make` from MinGW-w64).
- Supported platforms: Linux and Windows (MinGW-w64 and Visual Studio 2026; both Windows builds are first-class and must give identical SSE and CI). macOS is not supported.
- Tests: `make check`. Sanitizers: `make asan check`, `make ubsan check`.
- Warning-free requirement: `make WERROR=1` must pass on gcc, clang and MinGW-w64. MSVC must build with `/W4`.
- Visual Studio project lives in `msvc/` and must keep building.
- Golden files are regenerated only with `make golden-update`, and only in algorithm-change commits.
- Thesis timings come from the MinGW gcc release build at log level 1 (plan D3; on the desktop in step 8.3 Visual Studio Release took 1.185 times as long, same SSE and CI). Never time debug or sanitizer builds.
- Windows dev machine and timing reference: your personal desktop (AMD Ryzen 7 7800X3D, 16 logical processors, Windows 11) from plan step 8.3; the company laptop's security software blocks newly built executables. WinLibs GCC 16.2.0, `mingw32-make` 4.4.1 and cppcheck 2.22 (winget `BrechtSanders.WinLibs.POSIX.UCRT`); Visual Studio 2026 18.12 (MSVC 19.52, toolset v145) with MSBuild and its bundled clang 22.1 (component "C++ Clang tools for Windows"), which runs ASan and UBSan (see the verify skill; MinGW gcc has no sanitizers, Windows ASan no leak checker); WSL2 Ubuntu 24.04 with `build-essential` (gcc 13.3, GNU Make 4.3) for the real-data Linux gcc comparison (plan D13; run `wsl --shutdown` before timed Windows runs); GNU Octave 11.3 for `visuals/` (winget `GNU.Octave --scope user`; `octave-cli` is not on PATH but in `%LOCALAPPDATA%\Programs\GNU Octave\Octave-11.3.0\mingw64\bin`; run it headless, figures with `'Visible', 'off'`, which use the gnuplot toolkit there because Octave's OpenGL printing hangs on invisible figures). Recipes run in cmd.exe. If `gcc` is missing in a terminal, reload PATH from the registry. Check copied inputs with `sha256sum -c tests/realdata-sha256.txt`.

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
- `main.c`, `cli.[ch]`: entry point, argument parsing, subcommands and dataset dispatch.
- `dataset.[ch]`: flat dataset coordinates and partitions, capacity-sized centroid buffers, ownership and input parsing.
- `metrics.[ch]`: distances, SSE and Centroid Index; nearest-centroid and squared-distance hot helpers are inline.
- `kmeans.[ch]`: initialization, assignment, centroid updates and k-means iterations.
- `split.[ch]`: stable cluster-membership indexes, shared local two-means, split primitives and local repartition; `algorithms.[ch]`: algorithm IDs, names and split-k-means runners.
- `baselines.[ch]`: Random Swap and bisecting k-means.
- `experiment.[ch]`: algorithm run loops, statistics, run configuration and dataset metadata.
- `tracking.[ch]`: paused stopwatch, progress rows, snapshots and run CSV output.
- `report.[ch]`: merge finished runs and produce summary tables and curves.
- `util.[ch]`, `version.h`: shared parsing, formatting, errors, paths and build/version information.
- `platform.[ch]`, `rng.[ch]`, `locale_utils.[ch]`: platform shims and clock, seeded PCG32, and the retained locale helper.

## Working agreement
- Ask for and receive the user's explicit approval before any installation of software, tools, packages, dependencies or extensions, including installations initiated by scripts or package managers.
- Stop the affected operation and report security-software alerts or blocks. Never disable or bypass protections, add exceptions, or restore flagged or quarantined files.
- Ask for the user's explicit approval before using administrator privileges or making persistent changes to PATH or the registry.
- Ask for the user's explicit approval before running newly downloaded scripts or executables.
- Ask for the user's explicit approval before deleting datasets or results, or rewriting Git history. Temporary run outputs created for the current task may be deleted without approval if neither the plan nor the user requires keeping them.
- Never disclose secrets. Ask for the user's explicit approval before uploading files or logs to external services.
- Follow the plan execution workflow below. For other work, commit only when asked; push only when explicitly requested or authorized by a request to work on multiple phases.
- Never delete unused functions, macros, files or commented-out code without the user's confirmation, except temporary run outputs as described above. List them instead (see the Unused code review section in docs/refactor-plan.md).
- Functions marked keep in that table are ported, not dropped, when their surroundings are refactored.
- Do not create markdown documentation unless requested.

## Learnings
- Docker is unavailable in the user's environment. Do not run Docker commands, check its availability, or attempt to start it unless the user explicitly requests it.
- By default, implement and verify one plan step, commit it with a descriptive message, then stop without pushing unless the user asks to push.
- If the user requests a full phase, complete it one verified step at a time and commit each step. Do not push unless the user asks.
- If the user requests multiple phases, complete and commit each verified step. After each phase, including the final one, push its commits, review all changes in that phase, implement and verify applicable review fixes, then commit and push those fixes separately before proceeding or finishing. If there are no fixes, do not create an empty commit or push again. Stop and report if verification, review fixes or a required push is blocked; honor an explicit no-push instruction.
