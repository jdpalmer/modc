# Testing

`modc test` is the portable test runner for %C. It discovers `*_test.mc`
files, builds each as an independent executable (same pipeline as `modc run` /
`modc build`), and reports `ok` or `FAIL`. All package mechanics—`import`,
`#pragma modc c_libs`, `-M` search paths, `--target`—apply as in a normal build.

Compiler-tree CI uses three layers:

1. **Portable package tests** — `./modc test test` (immediate `test/*_test.mc`;
   works with `--target=windows` via Wine/CrossOver when tools are installed).
   Argv-sensitive cases live under `test/special/`.
2. **Selftest (corpus)** — `./modc selftest` runs three in-process lanes (no
   shell or Make):
   - **`*_fail.mc`** — must fail check; `// fail: F:...` and optional
     `// fail: flags: ...`
   - **`*_ok.mc`** — must typecheck; optional `// ok: flags: ...`
   - **`*_run.mc`** — emit/link/run leftovers (C drivers, `.qbe.expect`,
     `.emitflags`, ABI `*_host.c`)
3. **Special** — `make check-special` → thin host checks (vendor, cache,
   format, doc, includes).

`make check` is `./modc test test`, argv-sensitive specials, testdriver, then
`./modc selftest`.

## Writing tests

### `*_test.mc` (package / runnable)

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

### `*_fail.mc` (expect-fail)

Discovered by `modc selftest`. Leading comment directives declare the expected
diagnostic:

```c
// fail: flags: -M $ROOT
// fail: F:cannot use null pointer to Buf

int bad() {
    return (int)len((Buf *)0);
}
```

`$ROOT` expands to the working directory (or `MODC_SELFTEST_ROOT`).

### `*_ok.mc` (must typecheck)

Discovered by `modc selftest`. Optional flags:

```c
// ok: flags: -Ftest/fwk_root

#include <Dummy/Dummy.h>
```

### `*_run.mc` (emit / link / run)

Leftover interop and IR-assert cases for selftest: C `*_main.c` / `*_host.c`
drivers, `.qbe.expect`, `.emitflags`. Prefer `*_test.mc` when the case has a
plain `%C` `main`.

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
modc selftest [options]
```

With no path, `modc test` scans the current directory (`.`) for immediate
`*_test.mc` files (non-recursive). A directory argument scans that directory the
same way. A path ending in `*_test.mc` runs that single file. Matched files are
sorted alphabetically before execution. Discovering zero tests in a valid
directory prints a “no tests” message and exits `0`. A missing or invalid path
is an error.

```sh
./modc test -M test test/testdriver
./modc test --target=windows test/add_test.mc   # CrossOver/Wine when available
./modc selftest                                 # *_fail / *_ok / *_run
make check                                      # test + selftest
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
