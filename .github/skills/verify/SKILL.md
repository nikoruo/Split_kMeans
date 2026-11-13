---
name: verify
description: "Verify Split_kMeans builds and tests pass: gcc/clang warnings-as-errors, make check, ASan and UBSan, MinGW and MSBuild where available. Reports only; does not edit files."
disable-model-invocation: true
---
Verify the current state of the Split_kMeans repository. Do not edit any files.

If there is no `Makefile` in the repo root yet (before step 1.3), report that the checks are not available and stop.

Run the checks for the current OS in the repo root, in order. If a check fails, record it and continue with the next one.

Linux:
1. `make clean`
2. `make WERROR=1` (gcc)
3. If `clang` is on PATH: `make CC=clang WERROR=1 clean all`
4. `make check` (includes the default-settings smoke tests)
5. `make asan check`
6. `make ubsan check`

Windows (MinGW gcc has no sanitizers; skip them):
1. `mingw32-make clean`
2. `mingw32-make WERROR=1`
3. `mingw32-make check CHECK_ROOT=<%TEMP% with forward slashes>/skm` (inside OneDrive the default `build/check` hits sharing violations)
4. Find MSBuild with `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe`, then build `Split_kMeans.sln` with `/p:Configuration=Release /p:Platform=x64`.
5. If `clang` is not on PATH, use the one bundled with Visual Studio (`VC\Tools\Llvm\x64\bin\clang.exe`): `--target=x86_64-w64-mingw32 -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -Isrc` on each file in `src/` and `tests/`.

Both: if `cppcheck` is on PATH, run `cppcheck --std=c11 --enable=warning,performance,portability -q` on the source folder and list its findings.

Report as a table: Check, Result (pass, fail, skipped), first relevant error line with file link. End with a one-line verdict and the list of checks that were skipped and why.
