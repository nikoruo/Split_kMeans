---
name: verify
description: "Verify Split_kMeans builds and tests pass: gcc/clang warnings-as-errors, make check in release and debug, ASan and UBSan, MinGW and MSBuild where available. Reports only; does not edit files."
disable-model-invocation: true
---
Verify the current state of the Split_kMeans repository. Do not edit any files.

If there is no `Makefile` in the repo root yet (before step 1.3), report that the checks are not available and stop.

Run the checks for the current OS in the repo root, in order. If a check fails, record it and continue with the next one. Stop an affected operation on a security-software alert or block; do not bypass it. Ask before installing tools or deleting datasets or results.

Do not run `make clean` automatically. Choose a new disposable `<root>` without spaces for each check: `check` deletes `<root>/check`. It also regenerates known files in `build/testdata`, and its default-invocation smoke tests write under `build/testdata/main/outputs`; preserve any existing data or results before running it.

Linux:
1. If clang is available, `make -B CC=clang WERROR=1 check CHECK_ROOT=<root>/clang`.
2. `make -B CC=gcc WERROR=1 check CHECK_ROOT=<root>/gcc`.
3. `make -B CC=gcc WERROR=1 debug check CHECK_ROOT=<root>/debug` (asserts on).
4. `make -B CC=gcc asan check CHECK_ROOT=<root>/asan`.
5. `make -B CC=gcc ubsan check CHECK_ROOT=<root>/ubsan`.

Windows: use a new `<root>` outside OneDrive and without spaces. Find `<VS>` with `${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe`, requiring `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`.
1. `mingw32-make -B WERROR=1 check CHECK_ROOT=<root>\release`.
2. `mingw32-make -B debug WERROR=1 check CHECK_ROOT=<root>\debug` (asserts on).
3. Build `Split_kMeans.sln` with MSBuild from `<VS>`, `/p:Configuration=Release /p:Platform=x64`; `/W4` must give no warnings. Run `mingw32-make WERROR=1 check CHECK_BIN=build\msvc\x64\Release\Split_kMeans.exe CHECK_ROOT=<root>\msvc`.
4. Run the `Build and run MSVC unit tests` step in `.github/workflows/windows-build.yml`, setting `CHECK_ROOT` to `<root>\msvc` and `RUNNER_TEMP` to a new disposable folder in that process. `CHECK_BIN` alone uses the MinGW-built unit runner.
5. ASan and UBSan with the clang bundled with Visual Studio (MinGW gcc has no sanitizers). Write an empty `m.lib` (the 8 bytes `!<arch>` and a newline) into a temp folder without spaces outside the repo; it stands in for the Makefile's `-lm`. In one cmd.exe process, call `<VS>\VC\Auxiliary\Build\vcvars64.bat`, put `<VS>\VC\Tools\Llvm\x64\bin` and `<VS>\VC\Tools\Llvm\x64\lib\clang\<version>\lib\windows` on PATH, then run `mingw32-make -B asan check CC=clang LDFLAGS=-L<temp folder> CHECK_ROOT=<root>\asan` and the same with `ubsan` and its own check root. Windows ASan has no leak checker; LeakSanitizer runs only in the Linux CI.
6. Check clang warnings on every file in `src/` and `tests/`: the clang from check 5 with `--target=x86_64-w64-mingw32 -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -Isrc`.

Both: if `cppcheck` is on PATH, run `cppcheck --std=c11 --enable=warning,performance,portability -q` on the source folder and list its findings.

Report as a table: Check, Result (pass, fail, skipped), first relevant error line with file link. End with a one-line verdict and the list of checks that were skipped and why.

## Learnings

- gcc and clang share `build/release`; changing `CC` alone does not force every existing object to rebuild. Use `-B` before comparing compilers so a check does not mix objects from different builds.
