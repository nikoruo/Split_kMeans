---
description: "Makefile conventions for Split_kMeans: GNU Make 4.3+, user CFLAGS handling, per-configuration build folders, Windows (cmd.exe) recipes."
applyTo: "**/Makefile"
---
# Makefile rules (Split_kMeans)

- Target GNU Make 4.3 or newer. Ubuntu 24.04 LTS ships 4.3. macOS is not supported.
- Use 4.4-only functions (`$(let ...)`, `$(intcmp ...)`) only after the professor confirms make 4.4 on his machine. Check the GNU Make NEWS file for the version that introduced any feature.
- Keep target names stable: `all run check debug asan ubsan bench clean help golden-update`.
- User flags are appended with `+=` (for example `CFLAGS += $(USER_CFLAGS)`). Never set `CFLAGS ?=`, because an environment `CFLAGS` would silently drop `-O3`, `-std=c11` and the warnings.
- Each configuration builds into its own folder under `build/<config>/`. Sanitizer builds never reuse release objects.
- Do not chain `clean` into configuration targets. Use a separate object directory instead.
- Detect Windows with `ifeq ($(OS),Windows_NT)`, not `WINDIR`.
- Executable suffix: `.exe` on Windows only, via a variable.
- Header dependencies via `-MMD -MP`, included with `-include`.
- `WERROR=1` adds `-Werror`. CI uses it.
- On Windows, `mingw32-make` runs recipes in cmd.exe (no `sh.exe` on PATH). Define `MKDIR`, `RM` and path separators once per OS under `ifeq ($(OS),Windows_NT)`. Recipes are plain command invocations: no shell loops, `test`, `[ ]` or globs. Put test logic in C programs under `tests/`.
