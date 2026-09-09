# Testing

`modc test` is the test runner for %C project packages. It discovers `*_test.mc`
files, builds each file as an independent executable defining its own `main`
function, and reports test status as `ok` or `FAIL`. All standard package
mechanics—such as `import`, `#pragma modc c_libs`, and `-M` search paths—apply
during compilation exactly as they do in `modc build`.

For compiler toolchain development, `modc test --corpus` serves as a wrapper
around `make check`. Standard project testing should use `modc test` without
flags.

## Writing tests

Test files must be named using the `*_test.mc` convention (for example,
`add_test.mc`) and placed directly alongside the package code they exercise.
Directory build commands (`modc build dir`) and package imports automatically
exclude `*_test.mc` files to prevent test entry points from being linked into
production applications.

```c
import "mypkg";

#include <assert.h>

int main(void) {
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
./modc test uikit                    # e.g. layout_test.mc
./modc test --corpus                 # make check (compiler tree)
```

Supported build flags include `-D`, `-I`, `-M`, `-F`, `--no-system-includes`,
`-v`, and `-h`. Arguments placed after `--` are passed directly to each test
binary at runtime (similar to `modc run`) rather than to the compiler or linker.

### Output and exit status

Results are reported to standard output one line per file, with fields separated
by tabs:

```text
ok	test/testdriver/add_test.mc
FAIL	pkg/broken_test.mc	(exit 1)
```

The driver exits `0` if all tests pass or if no test files were discovered. If
any test fails, the driver exits `1`.
