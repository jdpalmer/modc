# %C documentation

%C (pronounced *mod-C*) is a dialect of C with the same ABI and linker model as
ordinary C. These pages describe how to install the compiler, write programs,
and use the dialect features.

Start with [Installation](installation.md) and the [Quickstart](quickstart.md).

## Start here

| Document | Description |
| -------- | ----------- |
| [Installation](installation.md) | Build and install `modc` on macOS, Linux, and Windows |
| [Quickstart](quickstart.md) | Write and build a first program |

## Language

| Document | Description |
| -------- | ----------- |
| [Interop](interop.md) | User `.mc` sources versus `#include` headers |
| [Structs](struct.md) | Unified tags, auto-dot, and Plan 9 embeds |
| [Methods](methods.md) | `overload` and receiver methods |
| [Defer](defer.md) | Block-scoped cleanup |
| [Auto](auto.md) | Local type inference and multi-return |
| [Arrays](arrays.md) | Fixed, open, and ranged arrays |
| [Auto-const](auto-const.md) | Provenance-based immutability without `const` |

## Packages and tooling

| Document | Description |
| -------- | ----------- |
| [Packages](packages.md) | `import`, vendoring, and the build cache |
| [`modc format`](modc-format.md) | Source formatting |
| [`modc test`](modc-test.md) | Package test runner |
| [`modc doc`](modc-doc.md) | Package API docs |

## Internals

| Document | Description |
| -------- | ----------- |
| [Architecture](architecture.md) | Compiler pipeline and layout of `src/` |
