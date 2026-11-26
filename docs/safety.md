# Safety in %C

%C is still C at the ABI: no GC, no borrow checker, no hidden runtime that
rewrites your program. Safety is two layers on top of that model:

1. **Compile-time contracts** in `.mc` — escapes, ownership attributes,
   initialization, and rejecting unchecked indexing of raw pointers.
2. **Runtime traps** (on by default) — bounds, null method receivers, size
   wrap, integer division faults, signed overflow, and signed `<<`. They abort
   through the [`crash`](os.md#crash) package. Pass `--release` to omit them
   for production.

This page is the inventory and the honest limitations list. Deep rules live in
the linked guides.

## Compile-time

| Check | What it does | Detail |
| ----- | ------------ | ------ |
| Escapes | Stack / pool / `defer`-released / `[[region]]` views must not outlive their scope | [memory.md](memory.md#escapes) |
| Ownership attrs | `[[provides]]` / `[[releases]]` / `[[retains]]` / `[[borrows]]` / `[[region]]` | [memory.md](memory.md#ownership-attributes-provides--releases--retains) |
| No raw `T*` indexing | `p[i]` and `*(p + i)` rejected in `.mc`; use `T[N]`, `T[..]`, or `ranged` | [arrays.md](arrays.md) |
| Dead / placeholder init | Locals must be initialized; overwritten-before-read placeholder inits are errors | [quickstart.md](quickstart.md) |
| Const / literals | String literals are `const char[..]`; no mutable sink without a copy | [const.md](const.md) |
| Live receivers | Methods must not null-check `(T *r)` at entry; callers keep them live | [methods.md](methods.md) |
| Constant arithmetic faults | Constant `/0`, `%0`, `MIN/-1`, signed overflow, and bad signed `<<` are compile errors | [arrays.md](arrays.md#runtime-checks-on-by-default) |

`modc check` type-checks the import graph’s **API surface** only (same idea as
an interface parse). Body-level diagnostics (escapes, releases, dead-init, …)
run on `modc build` / `test`. `check` can exit **0** when `build` would fail on
those body issues. For CI and “build until clean,” use `build` or `test`, not
`check` alone; see [packages.md](packages.md) and [quickstart.md](quickstart.md).

## Runtime traps (default builds)

Inserted by `modc build` / `run` / `test` / `emit` unless `--release`. Failures
call `__modc_*_fail` helpers that share the `crash` death path (cleanups,
one-line stderr, abort). Full bullets: [arrays.md](arrays.md#runtime-checks-on-by-default);
death path: [os.md](os.md#crash).

`--release` is **not** one behavior: some checks are omitted, some wrap, some
soft-fail. Skimming “omit traps” as “same as debug without abort” is wrong.

| Check | Default | `--release` |
| ----- | ------- | ----------- |
| `a[i]` / `s[i]` / slices | `__modc_bounds_fail` (abort) | omitted (unchecked) |
| `copy(dst, src)` length | `__modc_bounds_fail` (abort) | soft-fail: false, no write |
| Pointer method entry | `__modc_null_fail` (abort) | omitted (unchecked) |
| `alloc` / `reserve` / `push` / `append` size wrap | `__modc_size_fail` (abort) | soft-fail: NULL / false |
| Integer `/` `%` zero or signed `MIN/-1` | `__modc_div_fail` (abort) | omitted (unchecked) |
| Signed `+` `-` `*` unary `-` (and compounds) | `__modc_overflow_fail` (abort) | wraps (two’s complement) |
| Signed `<<` (neg LHS or unrepresentable) | `__modc_shift_fail` (abort) | omitted (unchecked) |

Unsigned arithmetic wraps without traps in both modes. Floating-point `/`
follows IEEE and is not trapped. Shift **counts** are still masked to the type
width in both modes.

## Limitations

%C does not make C memory-safe end-to-end. In particular:

- **Foreign C / `#include`** — bodies are not analyzed; hermetic prototypes
  carry ownership attrs where known. Crossing into unchecked C leaves the
  contract world. See [memory.md](memory.md#the-foreign-c-boundary) and
  [interop.md](interop.md).
- **Raw `T*`** — after `ptr()`, casts, or open `T[]` from headers, indexing and
  arithmetic are unchecked (headers and C still allow `p[i]`).
- **Function pointers** — calls through function pointers do not track
  `[[provides]]` / `[[releases]]` / `[[retains]]` / escapes. Ownership attrs on
  the pointee type are informational only at an indirect call; prefer a direct
  call when the checker must see the contract.
- **`--release`** — traps are gone; signed overflow wraps; some size/`copy`
  paths soft-fail instead of aborting. Production builds are closer to ordinary
  C performance and closer to ordinary C footguns.
- **No concurrency model** — no language threads/channels/async; use host C.
  See [stability.md](stability.md).
- **Traps are not a sandbox** — they abort the process; they do not isolate
  untrusted code or recover from faults (`crash` has no `recover`).
- **Const is not runtime** — casting away `const` and writing a literal remains
  undefined at run time ([const.md](const.md)).

## See also

[arrays.md](arrays.md) · [memory.md](memory.md) · [os.md](os.md#crash) ·
[methods.md](methods.md) · [const.md](const.md) · [stability.md](stability.md)
