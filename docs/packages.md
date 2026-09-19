# %C packages

%C seamlessly interoperates with ordinary C libraries and headers while offering a source packaging system built around `.mc` file directories and the `import` keyword. Packages support the full %C language dialect—including `const`, auto-inline, and methods—and mix cleanly with C headers and prebuilt binary libraries.

Declare third-party dependencies in `modc.ini`, fetch them with `modc vendor`, and bring them into scope using `import`.

This guide covers package mechanics, import resolution, and the interaction between vendoring and linking. For a brief intro to `import`, see the Packages section in [quickstart.md](quickstart.md).

## Your first package

A package is either a single `name.mc` file or a directory containing `.mc` files. Consider a small game project containing a local `util` package:

```text
mygame/
  main.mc
  util/
    mod.mc
```

```c
// util/mod.mc
int clamp(int x, int lo, int hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}
```

Use `import` to bring a package into scope. Unlike C, no header file (`util.h`) is required: every non-`static` declaration in the package is part of its public API, while `static` declarations are **package-private** (visible to all `.mc` files in that package, hidden from importers).

```c
/* main.mc */
import "util";

int main() {
    return clamp(50, 0, 10) == 10 ? 0 : 1;
}
```

```bash
modc build
```

Every `.mc` file acts as its own translation unit for parsing, but the package
shares one symbol space and one cached `pkg.o`. Importers see the union of all
non-`static` declarations across all files in a package. The compiler type-prescans
the whole package (names, then bodies, then layouts) before function/method
signatures, so types, methods, and by-value embeds work across files regardless of
basename order — like a Go package. Only immediate `*.mc` files in a directory
belong to that package; subdirectories are treated as distinct packages.

## Package-private `static`

At file scope, `static` means package-private (not C file-local):

```c
static int helper();           /* callable from sibling .mc in this package */
static struct Node { … };          /* type name hidden from importers */
static enum { Cap = 16 };          /* enumerators package-private */
static int g;                      /* package-private global */
```

Function-local `static` keeps ordinary C rules (static storage duration). There is
no separate file-private spelling.

`static` on a pointer field inside a struct remains package-private field access
(see below). Public functions, variables, typedefs, and types must not mention
package-private types in their signatures or fields.

Single-file packages (`import "foo"` → `foo.mc`) use the `.mc` path as their
package identity when they sit beside other packages in a mixed directory, so
`static` in `foo.mc` is not visible to sibling `bar.mc` files.

## Methods and linker names

Methods attach to a struct type within the same package, declared as `void (Window *w).show()` and called using dot-syntax (`w.show()`).

Linker symbols are mangled as `{package}_{type}_{method}`. The type tag is converted to lowercase, and any slashes (`/`) in the import path become underscores (`_`). For example, in package `ui`, the method `(Window *w).show` resolves to `ui_window_show`. Standard free functions retain their source name unless explicitly marked with `overload`. See [methods.md](methods.md) for complete rules.

## Package-private fields

While file-scope `static` marks package-private APIs and types, placing `static` before a pointer field inside a struct or union body designates that field as package-private. Inside the defining package, the field retains its full type and can be accessed normally. Outside importers can see the memory layout slot, but cannot read or write to the field directly.

Use this mechanism to encapsulate backend native handles (`GtkWidget *`, `HWND`, etc.) without exposing third-party SDK types in your public API. Non-pointer `static` fields are invalid and rejected by the compiler.

```c
/* ui_gtk/mod.mc: backend package; not imported by apps */
#include <gtk/gtk.h>

struct Window {
    static GtkWidget *widget;
    int id;
};

int
window_get_id(Window *w)
{
    w.widget = 0;   /* OK: same package, fully typed */
    return w.id;
}
```

```c
/* main.mc: imports portable facade only */
import "ui";

int
main()
{
    Window *w;

    w = window_open(640, 480, "hi");
    /* w.widget = 0;  error: field 'widget' is static (package-private) */
    return w == 0 ? 1 : 0;
}
```

Importers can manipulate `Window` and `Window *` directly because the memory layout accounts for the pointer slot. However, they do not gain access to `GtkWidget` types unless they explicitly `#include <gtk/gtk.h>`. Header symbols included in another package are never re-exported — except when the includer is a package `bridge.mc`, whose includes form the package C surface (package-visible, linked to host objects). See `test/pkg_encap*`.

## Arenas and `char[..]`

`T[..]` / `char[..]` is a length-aware header like `T *`: ownership is
convention, not a language attribute. Prefer `char[..]` in structs and APIs
when you need bounds; use `char *` at foreign NUL boundaries.

**Heap-owned text:** `str_dup`, `str_free`, `str_reserve`, `str_append` /
`str_append_byte`, `str_set`, and `str_ensure_z` malloc/realloc through a
`char[..]*` (rebind after growth). Only `str_free` headers these created (or
that you treat as malloc owners) — not literals, subslices, or arena results.

```c
char[..] name = {0};

{
    auto (ok, n) = str_dup("jem");
    if (!ok) {
        return;
    }
    name = n;
}
str_append(&name, "!");
str_free(&name);
```

**Arena regions:** the `arena` package owns a bump region; methods return
`char[..]` into that storage (valid until `a.free()` / `a.reset()`). Grow by
assigning (`s = a.append(s, …)`), not by writing header fields. Do not invent a
parallel `{ ptr, len, cap }` type — that is already `char[..]`.

```c
(bool, char[..]) (Arena* a).copy(const char[..] s);
(bool, char[..]) (Arena* a).append(char[..] cur, const char[..] s);
(bool, char[..]) (Arena* a).append_byte(char[..] cur, char c);
(bool, char[..]) (Arena* a).join(const char[..] sep, const char[..]* parts, size_t nparts);
```

```c
import "arena";

Arena a;
char[..] out = {0};
char[..] chunk = {0};

a.init();
defer a.free();
{
    auto (ok, s) = a.append(str_empty(), "part");
    if (!ok) {
        return;
    }
    chunk = s;
}
{
    auto (ok, s) = a.append(out, chunk);
    if (!ok) {
        return;
    }
    out = s;
}
str_eq(out, "part");
```

Mutator operations should be methods on pointer types (`T *`), whereas readers accept values (`str_eq(s, ...)`). See `arena/mod.mc` and `test/arena_pkg.mc` for canonical examples.

For routine string operations, use `char[..]` alongside `str_*` helpers for reading, comparison, slicing, and searching. String literals belong on `const char[..]` (or `const?` passthrough parameters): pass `"..."` into APIs that take `const` / `const?` `char[..]` (for example `str_eq(a, "x")`, `str_starts_with(s, "pre")`). You do not need `str_from_cstr` for literals; that helper and other `*_cstr` entry points are for foreign NUL-terminated `char *` values. Heap-owned fields use `str_dup` / `str_append` / `str_free`. Region builds use `arena` (`a.copy`, `a.append`, `a.join`, `a.replace`). Export to C with `cstr_write(buf, s)`, `str_ensure_z(&s)`, or `a.z(s)`. See [quickstart.md](quickstart.md), [arrays.md](arrays.md), and [const.md](const.md).

## How imports resolve

When resolving `import "a/b";`, the compiler searches package roots in order until it finds a match:

1. **Relative to the importer:** looks for `dirname(importer)/a/b/` or
   `dirname(importer)/a/b.mc`.

2. **Project root:** looks under the nearest ancestor containing `modc.ini`,
   or under the build entry's directory when there is no manifest. This makes
   project packages available without passing `-M .`.

3. **Vendored tree walk:** traverses parent directories searching for
   `vendor/a/b/` or `vendor/a/b.mc`. When importing vendored packages, write
   `import "math"` rather than `import "vendor/math"`.

4. **Explicit paths:** directories supplied via `-M dir` flags or the
   `MODC_PATH` environment variable (`;`-separated on Windows, `:`-separated
   elsewhere), checked as `dir/a/b` or
   `dir/a/b.mc`.

5. **Standard library:** the installation package root (containing `str`,
   `arena`, etc.), checking `MODC_PKG` first, then `$PREFIX/lib/modc/pkg`
   adjacent to the binary, and finally the compiler repository build root.

Path targets can be single `.mc` files or directories (where `mod.mc` loads first if present). Cyclic imports are strictly prohibited. Local files next to the importer intentionally shadow `vendor/` directories to prioritize project code, and local code/`-M` paths shadow stdlib modules. `modc` uses no central package registry; packages are defined via Git URLs in `modc.ini` and downloaded locally into `vendor/`.

## Vendoring dependencies

Applications declare direct dependencies in `modc.ini`, lock the resolved dependency tree in `modc.lock`, and fetch source dependencies into `vendor/` using `modc vendor`.

```text
mygame/
  modc.ini              ← direct deps (you edit)
  modc.lock             ← pinned revisions (generated)
  main.mc
  engine/               ← local packages
    window/
      mod.mc
  vendor/               ← third-party packages (generated or committed)
    math/
      mod.mc
    log.mc
```

```c
/* main.mc */
import "engine/window";   /* local package: path is the identity */
import "math";            /* found under vendor/math */
import "log";             /* found under vendor/log.mc */
```

### Writing `modc.ini`

Applications list top-level direct dependencies only, where section names correspond directly to the string used in `import` statements.

```ini
# mygame/modc.ini

[deps.math]
git = https://github.com/you/modc-math.git
tag = v1.0.0

[deps.log]
git = https://github.com/you/modc-log.git
rev = abc123def4567890
```

Each dependency entry must specify a `git` URL (`https://` or `file://`) and exactly one version pin (`tag`, `rev`, or `branch`). Dependency names are single portable path components. Set `subdir = path` if a package resides within a subfolder of a monorepo; it must be relative and cannot contain `.` or `..` path components.

Library packages publish their own `modc.ini` at their repository root or subdirectory declaring their dependencies. Libraries define a `[package]` section matching their import name and do not commit a `vendor/` folder.

```ini
# modc-log/modc.ini: library manifest

[package]
name = log

[deps.other]
git = https://github.com/you/modc-other.git
tag = v0.3
```

The `[package] name` should match the import name consumers use in
`[deps.log]`.

### Running `modc vendor`

Execute the vendor workflow from your project root:

```bash
git clone https://github.com/you/mygame.git
cd mygame
modc vendor
modc build
```

`modc vendor` recursively evaluates `modc.ini` manifests across all dependencies, converts `tag` and `branch` references to concrete Git commit hashes, and deduplicates packages by import name. Conflicting pins for the same import name raise an error. Dependency trees containing symbolic links are rejected. The complete tree is staged before `vendor/` and `modc.lock` are replaced, so a clone or copy failure preserves the previous vendored state. The command requires `git` on `PATH`.

Use `modc vendor --check` in CI to verify that `vendor/` matches `modc.lock`. Use `modc vendor -C dir` to target another project root, and `-v` to inspect underlying Git operations.

### Lockfiles and workflows

Commit the generated `modc.lock` file to version control to guarantee reproducible builds. Each vendored tree includes a `.modc-vendor-rev` metadata file used during `--check` validation.

```ini
# modc lockfile v1

[pkg.math]
git = https://github.com/you/modc-math.git
rev = abc123def4567890abcdef1234567890abcdef12

[pkg.log]
git = https://github.com/you/modc-log.git
rev = deadbeefdeadbeefdeadbeefdeadbeefdeadbeef
```

Projects can adopt a fetch-on-build workflow (omitting `vendor/` from source control and fetching on clone) or a zero-fetch workflow (committing `vendor/` directly so consumers can build offline). In both cases, the application root owns `vendor/` and resolves global version pins. Transitive dependencies are flattened at the app level, ensuring diamond dependencies (`A → B` and `C → B`) resolve to a single `vendor/b/` instance.

## Mixing with C libraries

Packages declare system library and native linking requirements directly within source code; they do not replace `#include` directives.

```c
/* engine/window/mod.mc */
#pragma modc c_libs(glfw)

#include <GLFW/glfw3.h>

GLFWwindow *
window_open(int w, int h, char *title)
{
    return glfwCreateWindow(w, h, title, 0, 0);
}
```

```c
/* main.mc */
import "engine/window";
#include <GLFW/glfw3.h>    /* needed if this file names GLFWwindow */

int
main()
{
    GLFWwindow *w;

    w = window_open(800, 600, "hi");
    return w == 0 ? 1 : 0;
}
```

`#pragma modc c_libs(a, b)` tells `modc build` / `modc run` to add `-la -lb`.
On Apple hosts, `#pragma modc frameworks(…)` does the same for frameworks.
`#pragma modc c_sources(path, …)` compiles C-family source files (`.c`, `.m`,
`.cpp`, `.cxx`, `.mm`) located relative to the declaring `.mc` file and links
their object files. Only packages in the import graph contribute sources. C++
shims must expose `extern "C"` entry points; override the host compilers with
`MODC_CC` / `MODC_CXX` (defaults: `$CC`/`cc` and `$CXX`/`c++`).

Build artifacts are content-addressed and cached under `.modc-cache/` (next to
`modc.ini` when present, otherwise next to the build entry). Unchanged packages
and `c_sources` objects are reused across builds; keys include target OS,
architecture, project-relative paths, and the contents of transitively included
project headers. Purge old caches with `modc clean` or `rm -rf .modc-cache`.

Header inclusion priority follows [interop.md](interop.md) (`modc` stubs, then
`-I` paths, then system headers). C header symbols remain strictly scoped to the
`.mc` file where included and are never re-exported. If a foreign C type like
`GLFWwindow` appears in a signature consumed by an importer, that importer must
explicitly `#include` the relevant header. Pass-through flags after `--` go
straight to the linker (for example `modc build main.mc -- -lGL`).

## Driver commands that touch packages

Most day-to-day commands already understand packages. `modc vendor` resolves
`modc.ini` into `modc.lock` and `vendor/`. `modc check file.mc` resolves
imports and type-checks the graph. `modc build` (with a file, a directory, or
`.` by default) compiles the root plus its packages, applies `c_libs` /
frameworks, and links; directory roots omit `*_test.mc`. The project root is
inferred automatically, so the usual project command is simply `modc build`;
`-M .` remains accepted but is redundant. `modc clean` removes
`.modc-cache/` for that project. `modc test` discovers `*_test.mc` files and
builds them with the usual project-root / `import` rules—see
[modc-test.md](modc-test.md).
`modc doc` extracts package API docs from comments above non-`static`
declarations ([modc-doc.md](modc-doc.md)), and `modc format` rewrites `.mc` files in
place ([modc-format.md](modc-format.md)). `modc emit` still emits QBE for one translation
unit; imports must resolve even then.

Extra package roots come from `-M dir` or `MODC_PATH`; override the stdlib root
with `MODC_PKG`. The familiar `-I`, `-D`, and `--no-system-includes` flags
still apply to the C lane, and everything after `--` is linker passthrough.
