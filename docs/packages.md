# %C packages

%C seamlessly interoperates with ordinary C libraries and headers while offering a source packaging system built around `.mc` file directories and the `import` keyword. Packages support the full %C language dialect—including auto-const, auto-inline, and methods—and mix cleanly with C headers and prebuilt binary libraries.

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

Use `import` to bring a package into scope. Unlike C, no header file (`util.h`) is required: every non-`static` declaration in the package is part of its public API, while `static` declarations remain file-local.

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

Every `.mc` file acts as its own translation unit. Importers see the union of all non-`static` declarations across all files in a package, allowing forward references and mutual recursion to work seamlessly without header prototypes. Only immediate `*.mc` files in a directory belong to that package; subdirectories are treated as distinct packages.

## Methods and linker names

Methods attach to a struct type within the same package, declared as `void (Window *w).show(void)` and called using dot-syntax (`w.show()`).

Linker symbols are mangled as `{package}_{type}_{method}`. The type tag is converted to lowercase, and any slashes (`/`) in the import path become underscores (`_`). For example, in package `ui`, the method `(Window *w).show` resolves to `ui_window_show`. Standard free functions retain their source name unless explicitly marked with `overload`. See [methods.md](methods.md) for complete rules.

## Package-private fields

While file-scope `static` indicates file-local visibility, placing `static` before a pointer field inside a struct or union body designates it as package-private. Inside the defining package, the field retains its full type and can be accessed normally. Outside importers can see the memory layout slot, but cannot read or write to the field directly.

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
main(void)
{
    Window *w;

    w = window_open(640, 480, "hi");
    /* w.widget = 0;  error: field 'widget' is static (package-private) */
    return w == 0 ? 1 : 0;
}
```

Importers can manipulate `Window` and `Window *` directly because the memory layout accounts for the pointer slot. However, they do not gain access to `GtkWidget` types unless they explicitly `#include <gtk/gtk.h>`. Header symbols included in another package are never re-exported. For architectural details on platform-independent app/backend splits, see [../uikit/DESIGN.md](../uikit/DESIGN.md) and reference tests in `test/pkg_encap*`.

## Owning containers

Growable or owning container types can embed a view type anonymously while adding metadata like capacity or reference counts. Combined with promoted fields and value projection, callers can pass containers directly into APIs expecting embedded views by value. See [struct.md](struct.md) for full embedding rules.

```c
struct U8 {
    Arena* arena;
    char[..];            /* .ptr, .len; U8 → char[..] at call sites */
};

void (U8* u).begin(Arena* a);
bool (U8* u).put(char[..] s);
(bool, char[..]) (Arena* a).copy(char[..] s);
```

```c
import "arena";

Arena a;
U8 out;
U8 chunk;

a.init();
defer a.free();
out.begin(&a);
chunk.begin(&a);
chunk.put("part");
out.put(chunk);   /* U8 → char[..] by embed projection */
str_eq(out, "part");
```

Mutator operations should be methods on pointer types (`T *`), whereas readers accept values (`str_eq(u, ...)`) or pointers (`str_eq(p, ...)`). The `{ View; extra; }` layout pattern is standard for custom buffers; see `arena/mod.mc` and `test/arena_pkg.mc` for canonical examples.

For routine string operations, use `char[..]` alongside `str_*` helpers for reading, comparison, slicing, and searching. Construct or transform text using `arena` methods (`a.copy`, `a.join`, `a.replace`, `u.put`). Export data to C APIs using `cstr_write(buf, view)` or `a.z(view)` for NUL-terminated copies. Convert foreign C strings (`char *`) via `str_from_cstr` and `str_eq_cstr`. See [quickstart.md](quickstart.md) and [arrays.md](arrays.md).

## How imports resolve

When resolving `import "a/b";`, the compiler searches package roots in order until it finds a match:

1. **Relative to the importer:** looks for `dirname(importer)/a/b/` or
   `dirname(importer)/a/b.mc`.

2. **Vendored tree walk:** traverses parent directories searching for
   `vendor/a/b/` or `vendor/a/b.mc`. When importing vendored packages, write
   `import "math"` rather than `import "vendor/math"`.

3. **Explicit paths:** directories supplied via `-M dir` flags or the
   `MODC_PATH` environment variable (colon-separated), checked as `dir/a/b` or
   `dir/a/b.mc`.

4. **Standard library:** the installation package root (containing `str`,
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

Each dependency entry must specify a `git` URL (`https://` or `file://`) and exactly one version pin (`tag`, `rev`, or `branch`). Set `subdir = path` if a package resides within a subfolder of a monorepo.

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

`modc vendor` recursively evaluates `modc.ini` manifests across all dependencies, converts `tag` and `branch` references to concrete Git commit hashes, and deduplicates packages by import name. Conflicting pins for the same import name raise an error. The command generates `modc.lock` and clones dependency source trees directly into `vendor/<import-name>/` as standard files without submodules (requires `git` on `PATH`).

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
main(void)
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
architecture, and project-relative paths. Purge old caches with `modc clean` or
`rm -rf .modc-cache`.

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
frameworks, and links; directory roots omit `*_test.mc`. `modc clean` removes
`.modc-cache/` for that project. `modc test` discovers `*_test.mc` files and
builds them with the usual `-M` / `import` rules—see [modc-test.md](modc-test.md).
`modc doc` extracts package API docs from comments above non-`static`
declarations ([modc-doc.md](modc-doc.md)), and `modc format` rewrites `.mc` files in
place ([modc-format.md](modc-format.md)). `modc emit` still emits QBE for one translation
unit; imports must resolve even then.

Extra package roots come from `-M dir` or `MODC_PATH`; override the stdlib root
with `MODC_PKG`. The familiar `-I`, `-D`, and `--no-system-includes` flags
still apply to the C lane, and everything after `--` is linker passthrough.
