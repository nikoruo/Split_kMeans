---
description: "C11 coding rules for Split_kMeans: module layout, memory ownership, input validation, flat arrays, determinism."
applyTo: "**/*.{c,h}"
---
# C rules (Split_kMeans)

- Target C11. Compile warning-free with `-Wall -Wextra -Wpedantic`.
- Module pattern: `name.h` holds public types and declarations; `name.c` holds definitions. Mark helpers `static`.
- Header guards on every header. No non-inline function definitions in headers; `static inline` is allowed for small hot helpers.
- Ownership: every allocation has a matching `x_alloc` / `x_free` pair. The owner frees; document it in the header.
- Avoid `DataPoint`-style per-point heap allocations. Use flat arrays (`double` buffer of N*D) and index arithmetic.
- No allocation inside iteration loops. Allocate scratch memory once per run.
- Validate external input at the boundary with a checked error path (`die()` with file and line). Use `assert` for internal invariants. Checks inside hot loops are asserts too, so release builds (NDEBUG) do not pay for them.
- Parse numbers with `strtod` and check `endptr`. Reject NaN and Inf.
- Use `%.17g` for values that must round-trip. Use `size_t` for indices and counts.
- Never use `rand`, `srand`, `time(NULL)` seeding, or `RANDOMIZE`. Pass `Rng*` explicitly.
- No I/O or `printf` inside per-point or per-dimension loops. Inside timed regions, only log messages at level 2 or 3.
- Log-level checks sit at run, swap, split or iteration granularity, never inside per-point loops.
- Keep doc comments to one or two lines. Describe ownership, units and preconditions only.
- Do not delete commented-out code or unused functions on your own. List them and wait for the user's decision.
- An unused `static` function fails `make WERROR=1` (gcc `-Wunused-function`). Functions kept for later use must not be `static`; declare them in their module header.
