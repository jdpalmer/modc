# `modc doc`

`modc doc` prints package documentation for %C sources. Ordinary comments
immediately above an exported declaration become the doc string. The command
type-checks the package and prints the public API.

There is no `//@` or `///` requirement and no `@param` tag language.

## Writing docs

Place `//` or `/* */` comments directly above a non-`static` file-scope
declaration (function, typedef, or package-visible variable). Blank lines or
other tokens between the comment and the declaration drop the attachment.

```c
// add returns the sum of a and b.
int add(int a, int b) {
	return a + b;
}

// not exported: static (package-private) APIs never appear in modc doc
static int hide() {
	return 0;
}
```

Multi-line `//` runs that the lexer attaches as one pending doc block are fine.
Prefer short prose that describes what the API does and any preconditions (see
[quickstart.md](quickstart.md)).

Exported means file scope, not `static` / local / param, and not hidden or
dead. That is the same surface importers see via `import`. Only functions,
typedefs, and package-visible variables are listed; bare struct tags and
header-only symbols are not.

## Command

```sh
modc doc [options] [target]
```

The command runs a check-only compile of the chosen package (same `-D` / `-I` /
`-M` rules as other driver commands), then prints matching exports. With no
target it uses the current directory (`.`). A path or `file.mc` documents that
package root or file. A bare name `pkg` resolves the package via `-M` /
`MODC_PATH` / the stdlib root and lists all exports. A bare `Name` prefers a
local symbol in `.`, otherwise it is treated as a package name. `pkg.Name`
resolves `pkg` and prints only symbol `Name`. At most one target is allowed.

Examples from the test corpus:

```sh
./modc doc -M test docpkg           # package API
./modc doc -M test docpkg.add       # one symbol
```

Common options: `-D`, `-I`, `-M`, `-F`, `--no-system-includes`, `-v`, `-h`.

## Output

Each export is a signature line, then its doc text (if any):

```text
add(int a, int b) -> int
add returns the sum of a and b.
```

Functions print as `name(type pname, …) -> ret` (varargs as `...`). Typedefs
print as `typedef T Name`. Other exports print as `T name`. Symbols without a
doc comment still print the signature.

## What it does not do

`modc doc` does not read or write `.modc-cache`; it always re-parses and
type-checks (cached docs via export data may come later). It emits plain text
on stdout, not HTML or markdown. It does not document `static` helpers, locals,
or header-only symbols, and it does not require special comment markers beyond
adjacency to the declaration.

## Related

- Packages and `import`: [packages.md](packages.md)
- Quickstart (`modc doc` tour): [quickstart.md](quickstart.md)
