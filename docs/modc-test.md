# Testing

`modc test` is the portable test runner for %C. It discovers `*_test.mc`
files, builds each as an independent executable (same pipeline as `modc run` /
`modc build`), and reports `ok` or `FAIL`. All package mechanics—`import`,
`#pragma modc c_libs`, `-M` search paths, `--target`—apply as in a normal build.

Compiler-tree CI uses three layers:

1. **Portable** — `MODC_NO_SYSTEM_INCLUDES=1 ./modc test test` (immediate
   `test/*_test.mc`; works with `--target=windows` via Wine/CrossOver when
   tools are installed). Argv-sensitive cases live under `test/special/`.
2. **Corpus** — `scripts/check-corpus.sh` → expect-fail / check-ok needles, plus
   remaining emit+host-`cc` drivers (C-ABI interop, `.qbe.expect`, `.env`).
   Expect-fail files use `F:` fixed diagnostic substrings (exact `error:` text)
   so a wrong message cannot silently match a vague needle. Distinct `bad_*.mc`
   files that share a diagnostic still stay separate — they cover different
   constructs, not duplicate tests.
3. **Special** — `make check-special` → vendor, CLI, and argv-sensitive cases.

`modc test --corpus` still runs `make check` (all three layers). Prefer
`modc test` without `--corpus` for ordinary package work.

## Writing tests

Test files must be named using the `*_test.mc` convention (for example,
`add_test.mc`) and placed directly alongside the package code they exercise.
Directory build commands (`modc build dir`) and package imports automatically
exclude `*_test.mc` files to prevent test entry points from being linked into
production applications.

```c
import "mypkg";

#include <assert.h>

int main() {
    assert(add(2, 3) == 5);
    assert_eq(add(1, 1), 2);
    return 0;
}
```

Every test file is compiled as an independent program. A process exit code of
`0` indicates success, whereas non-zero exit codes (such as those triggered by
`assert` aborts) signal failure.

### Assertion macros

Hosted environments provide two assertion macros via
[assert.h](../src/host/include/assert.h). `assert(expr)` aborts and prints
`file:line: assert failed: <expr>` when `expr` is false. `assert_eq(a, b)`
aborts unless `a == b`, with the failure text showing `a == b`. Defining
`NDEBUG` disables both macros, turning them into no-ops. Tests should rely on
standard process exit codes and C-style stream output rather than a custom test
framework.

## Command usage

```sh
modc test [options] [dir|file_test.mc] [-- program-args...]
```

With no path, the runner scans the current directory (`.`) for immediate
`*_test.mc` files (non-recursive). A directory argument scans that directory the
same way. A path ending in `*_test.mc` runs that single file. Matched files are
sorted alphabetically before execution. Discovering zero tests in a valid
directory prints a “no tests” message and exits `0`. A missing or invalid path
is an error.

```sh
./modc test -M test test/testdriver
./modc test --target=windows test/add_test.mc   # CrossOver/Wine when available
./modc test --corpus                             # make check (compiler tree)
```

Supported build flags include `-D`, `-I`, `-M`, `-F`, `--no-system-includes`,
`--target`, `-v`, and `-h`. Arguments placed after `--` are passed directly to
each test binary at runtime (similar to `modc run`) rather than to the compiler
or linker.

### Output and exit status

Results are reported to standard output one line per file, with fields separated
by tabs:

```text
ok	test/testdriver/add_test.mc
FAIL	pkg/broken_test.mc	(exit 1)
```

The driver exits `0` if all tests pass or if no test files were discovered. If
any test fails, the driver exits `1`.
