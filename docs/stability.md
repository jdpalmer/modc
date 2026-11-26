# Language stability

%C aims for C-like longevity: a small core, a stable mental model, and no
C++-style feature accretion. The dialect surface below is **frozen**. New work
should harden semantics, diagnostics, stdlib, and tooling — not reopen these
lines.

Short agent summary: treat this page as the “do not invent” list; detail for
each area lives in the linked guides.

## Closed by design

| Area | Freeze |
| ---- | ------ |
| Polymorphism | `overload` + Plan 9 embeds + `T[..]` + ordinary C function pointers. **No** templates, `_Generic`, interfaces, vtables, generics, method values, or capturing closures. |
| Errors | Multi-return `(T, bool)` / similar + `[[nodiscard]]` + `defer`. **No** `?` operator, error interfaces, or exceptions. |
| Concurrency | **No** language threads, channels, or async. Use host C (`#include`, pthreads, `<stdatomic.h>`, …) when needed; no dialect sugar for `_Atomic` / `_Thread_local`. |
| Object model | Named aggregates use **pointer** receivers `(T *r)` (live, non-null). **No** by-value `(T r)` methods on named structs/unions. `T[..]` / `T[..]*` receivers are the exception (headers are values). |
| Ownership attrs | Closed set: `[[provides]]`, `[[releases]]`, `[[retains]]`, `[[borrows]]`, `[[region]]` (plus `[[nodiscard]]` for discard). **No** lifetime parameters or new ownership attributes. Gaps stay at the C boundary. |
| Distribution / ABI | Ship and depend on **`.mc` source packages**. `.pkg.o` / `.pkg.mc` are a local build cache. **Stable binary ABI** is unmangled C entry points (`extern` / unmarked free functions). Method and `overload` mangling may change only under documented one-time rules (always-suffixed params); do not treat `%C`↔`%C` object link as a cross-version contract. |

See [methods.md](methods.md), [memory.md](memory.md), [auto.md](auto.md),
[packages.md](packages.md), and [interop.md](interop.md).

## Extension methods (policy)

Any package that can see a receiver type may define methods on it. Callers must
`import` the defining package to use those methods. Duplicate
`(receiver shape, name, parameter types)` is an error (define-time and/or
linker), not “best effort.” Named-struct methods mangle under **T’s origin
package**; `T[..]` methods mangle under the **defining** package. Details:
[methods.md](methods.md).

## What still moves

Diagnostics, escape/ownership edge cases, stdlib APIs, `modc` CLI/docs/install,
and CalVer releases. Prefer clarifying rules over new syntax.
