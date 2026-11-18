# `modc doc`

`modc doc` prints package documentation for %C sources, and short language
topic pages for dialect rules. Ordinary comments immediately above an exported
declaration become the doc string for package APIs. Language topics are baked
into the toolchain for offline use by people and agents.

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
typedefs, and package-visible variables **defined in the target package** are
listed. Header-only symbols, compiler builtins (`__modc_*`), and symbols from
imports / other packages are omitted. Bare struct tags are not listed.

## Command

```sh
modc doc [options] [target]
```

### Language topics

Exact topic names are reserved and win over package resolution:

```sh
modc doc topics          # index of language topics
modc doc lang            # same index
modc doc defer           # one topic (rules + short examples)
modc doc const
modc doc escape
```

Topics today: `auto`, `const`, `defer`, `escape`, `goto`, `import`, `methods`,
`pool`, `provides`, `slice`. Each page lists hard rules, OK / not-OK snippets
when useful, and `See also` cross-links.

Dialect diagnostics may append a footnote:

```text
note: modc doc defer
```

That points at the matching topic. Footnotes are deduplicated per compile.

### Package API

With a package target, the command runs a check-only compile of the chosen
package (same `-D` / `-I` / `-M` rules as other driver commands), then prints
matching exports from that package only. With no target it uses the current
directory (`.`). When `.` (or another directory) has sibling package
subdirectories, output also lists `Local packages:` and how to document one
(`modc doc -M . <pkg>`). A path or `file.mc` documents that package root or
file. A bare name `pkg` resolves the package via `-M` / `MODC_PATH` / the
stdlib root and lists all exports. A bare `Name` prefers a local symbol in
`.`, otherwise it is treated as a package name (unless it is a language topic).
`pkg.Name` resolves `pkg` and prints only symbol `Name`. At most one target is
allowed. Package output ends with `Language topics: modc doc topics`.

Examples from the test corpus:

```sh
./modc doc -M test docpkg           # package API
./modc doc -M test docpkg.add       # one symbol
```

Common options: `-D`, `-I`, `-M`, `-F`, `--no-system-includes`, `-v`, `-h`.

## Output

### Language topics

Plain text: a title line, a `Rules:` list, optional OK / Not OK snippets, and
`See also`.

### Package API

Each export is a signature line, then its doc text (if any):

```text
add(int a, int b) -> int
add returns the sum of a and b.
```

Functions print as `name(type pname, …) -> ret` (varargs as `...`). Typedefs
print as `typedef T Name`. Other exports print as `T name`. Symbols without a
doc comment still print the signature.

## What it does not do

`modc doc` does not read or write `.pkg.*` artifacts; package mode always
re-parses and type-checks (cached docs via export data may come later). It emits
plain text on stdout, not HTML or markdown. Package mode does not document
`static` helpers, locals, header-only symbols, import/transitive APIs, or
`__modc_*` builtins, and it does not require special comment markers beyond
adjacency to the declaration. Language topics are a curated index, not a dump
of the full markdown guides under `docs/`.

## Related

- Packages and `import`: [packages.md](packages.md)
- Quickstart (`modc doc` tour): [quickstart.md](quickstart.md)
