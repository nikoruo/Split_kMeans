---
name: verify
description: "Verify Split_kMeans builds and tests pass: gcc/clang warnings-as-errors, make check in release and debug, ASan and UBSan, MinGW and MSBuild where available. Reports only; does not edit files."
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
5. `make debug check` (asserts on; release builds define `NDEBUG`, so only this run checks them)
6. `make asan check`
7. `make ubsan check`

Windows. `<root>` is a check output folder outside OneDrive, written with forward slashes and without spaces (for example the 8.3 short form of `%TEMP%` plus `/skm`); inside OneDrive the default `build/check` hits sharing violations. `<VS>` is `vswhere -latest -property installationPath` (`${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe`).
1. `mingw32-make clean` (until step 3.0 this also deletes the Visual Studio output in `build/msvc`, so check 5 builds it afterwards)
2. `mingw32-make WERROR=1`
3. `mingw32-make check CHECK_ROOT=<root>`
4. `mingw32-make debug check CHECK_ROOT=<root>` (asserts on)
5. Build `Split_kMeans.sln` with MSBuild (`vswhere -latest -find MSBuild\**\Bin\MSBuild.exe`), `/p:Configuration=Release /p:Platform=x64`; `/W4` must give no warnings. If the Makefile has `CHECK_BIN` (step 3.0), also run `mingw32-make check CHECK_BIN=build/msvc/x64/Release/Split_kMeans.exe CHECK_ROOT=<root>`: both Windows builds must match the same golden files.
6. ASan and UBSan with the clang bundled with Visual Studio (MinGW gcc has no sanitizers). Write an empty `m.lib` (the 8 bytes `!<arch>` and a newline) into a temp folder outside the repo; it stands in for the Makefile's `-lm`. Then, in one cmd.exe process: `call "<VS>\VC\Auxiliary\Build\vcvars64.bat"`, put `<VS>\VC\Tools\Llvm\x64\bin` and `<VS>\VC\Tools\Llvm\x64\lib\clang\<version>\lib\windows` (the ASan runtime DLL) first on PATH, then run `mingw32-make asan check CC=clang LDFLAGS=-L<temp folder> CHECK_ROOT=<root>` and the same with `ubsan`. Windows ASan has no leak checker; LeakSanitizer runs only in the Linux CI.
7. clang warnings on every file in `src/` and `tests/`: the clang from check 6 (`<VS>\VC\Tools\Llvm\x64\bin\clang.exe`) with `--target=x86_64-w64-mingw32 -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -Isrc`.

Both: if `cppcheck` is on PATH, run `cppcheck --std=c11 --enable=warning,performance,portability -q` on the source folder and list its findings.

Report as a table: Check, Result (pass, fail, skipped), first relevant error line with file link. End with a one-line verdict and the list of checks that were skipped and why.
