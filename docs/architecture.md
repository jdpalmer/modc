# Architecture

This document describes how the modc compiler is put together for developers
interested in hacking on the compiler.  If you aren't hacking on the
compiler, this document won't be of much interest.

modc is a small hosted C99 compiler (about 21k LOC in `src/`) with a classic
pipeline and a shared compilation object. There is no IR middle-end beyond
typed AST to QBE text.  There are no immediate plans to make modc
self-hosting.

## Pipeline

| Stage | Entry | Notes |
|-------|--------|--------|
| Lex | `lex_pp_file` / lexer | Tokens; optional `TComment` for `modc format` |
| PP | same call path | Macros, includes, `#pragma modc …` |
| Parse | `parse_unit` | Recursive descent; builds AST and symbols |
| Type | `type_expr` (during parse) + `type_check_unit` | Conversions during parse; whole-unit passes after |
| Emit | `emit_qbe` / `emit_qbe_pkg` | QBE IL text |
| Link | `modc build` in `cli_build.c` | `qbe` → host `cc`; cache under `.modc-cache/` |

Package builds discover an import graph (`pkg_discover`), typecheck the
stitched units once, then emit per-package objects when caching. See
[packages.md](packages.md) and the build-cache notes in
[quickstart.md](quickstart.md).

## Source map (`src/`)

| File | Role |
|------|------|
| [ast.h](../src/ast.h) | Shared types (`Compiler`, `Node`, `Type`, `Symbol`, …) and module APIs |
| [diag.c](../src/diag.c) | Errors, spans, alloc / intern helpers |
| [lex.c](../src/lex.c) | Lexer |
| [pp.c](../src/pp.c) | Preprocessor |
| [parse.c](../src/parse.c) | Parser → AST |
| [type.c](../src/type.c) | Type pool, conversions, `type_expr` |
| [symbol.c](../src/symbol.c) | Symbol table, overloads, methods, mangling |
| [check.c](../src/check.c) | Post-parse checks (`type_check_unit`) |
| [emit.c](../src/emit.c) | AST → QBE |
| [pkg.c](../src/pkg.c) | `import` discovery, search paths, link pragmas |
| [cache.c](../src/cache.c) | Content-addressed cache helpers |
| [vendor.c](../src/vendor.c) | `modc vendor` (`modc.ini` → `vendor/`) |
| [fmt.c](../src/fmt.c) | `modc format` ([modc-format.md](modc-format.md)) |
| [cli.h](../src/cli.h) | Shared CLI types (`CliOpts`) and prototypes |
| [main.c](../src/main.c) | CLI entry, usage, path/sysinc setup, flag parsing |
| [cli_build.c](../src/cli_build.c) | Compile graph, package cache, foreign objs, link |
| [cli_cmd.c](../src/cli_cmd.c) | Subcommands (`check`, `emit`, `build`, `run`, …) |
| [host_os.c](../src/host_os.c) / [host_os.h](../src/host_os.h) | Paths, dirs, processes (POSIX / Win32) |
| [host/include/](../src/host/include/) | Hosted C stubs for `#include` interop |

Larger frontend files (`parse`, `emit`, `pp`, `check`) stay one phase per file,
with section banners and prefixes (`da_`, `ac_`, `ir_`, and so on). Prefer
splitting only when a stable seam appears.

## Core object: `Compiler`

One `Compiler` holds state for a compile: token buffer, type pool, symbol table,
macros and include caches, collected functions and globals, package link hints
(`c_libs`, `c_sources`, …), and string pool.

User `.mc` and `#include`d headers share the same machinery. Dialect rules are
stricter on user sources. Details are in [interop.md](interop.md).

## CLI vs language

The language frontend runs from lex through emit. Output is QBE text, or
check-only with no emit.

The CLI (`modc build` / `run` / `test`) invokes `qbe` and the host
assembler/linker, compiles `#pragma modc c_sources`, applies frameworks and
`-l`, and reuses `.modc-cache/`.

Install layout and how to build the compiler itself are in
[installation.md](installation.md).

## Tests and packages

The corpus is `test/*.mc` (plus `*_main.c` where needed), run with `make check`
via `scripts/check-corpus.sh` (emit/link/run and expect-fail) and
`scripts/check-special.sh` (format, vendor, cache, install, …). See
[test/README.md](../test/README.md).

Shipped packages include `str/` and `arena/`. Import resolution order is
documented in [packages.md](packages.md).
