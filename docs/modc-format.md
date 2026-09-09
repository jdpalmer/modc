# Source code formatting

`modc format` rewrites `.mc` source files in place to enforce a single deterministic code style without configuration knobs. The formatter operates strictly at the lexer level, retaining comments while re-emitting tokens according to fixed K&R rules. Because it does not run the preprocessor or parser, it never expands macros, resolves types, or reformats included headers.

For language conventions beyond automated formatting rules, see [quickstart.md](quickstart.md).

## Command

```sh
modc format file.mc...
```

The command accepts one or more `.mc` file paths and modifies them directly in place. It intentionally lacks an output flag (`-o`) or built-in diff mode; projects requiring validation in CI should rely on external utilities like `diff` or Makefile scripts.

```sh
./modc format hello.mc util/mod.mc
make format          # rewrite tree .mc (skips format golden inputs)
make format-check    # fail if any formattable .mc differs from formatter output
```

Lexing failures leave target files completely unmodified and cause the command to exit with a non-zero status code.

## Formatting rules

Layout follows fixed K&R conventions. Indentation uses hard tabs driven by brace nesting depth. Opening braces stay on the same line as the construct that introduces them (`if (cond) {`, `int main() {`), and control keywords take a single space before the parenthesis (`if (`, `for (`, `while (`, `switch (`, `sizeof (`).

Pointer declarators attach the asterisk to the type: write `int* p` and `char[..]* s`, never `int *p`.

The printer inserts line breaks after `;`, `{`, and `}` when those tokens sit outside parentheses or brackets. Author-chosen line breaks are mostly ignored, but source newlines still matter as blank-line hints and to end `#` directive lines. Consecutive empty lines at top level collapse to at most one blank line. Both `//` and `/* … */` comments are preserved in place. Preprocessor directives are not reflowed: each `#include` or `#define` line ends at its original newline.

Project-level formatting files, column alignment options, and `clang-format` compatibility layers are deliberately unsupported. Compiler source files (`src/*.c`) use a separate `.clang-format` file and are not processed by this tool.
