# Memory and lifetimes in %C

%C keeps C’s layout, ABI, and linker. There is no GC and no borrow checker.
Safety in `.mc` comes from four checks that reinforce each other, plus an
explicit, unchecked C boundary.

## The four pillars

### 1. Lexical pools — `pool p { … }`

Bump allocation with cleanup at block end (like a deferred `p.free()`).
Views from `p.copy` / `p.append` / `p.cstr` / `p.alloc` are tied to that
region via `[[region]]` wiring: they must not escape past block end,
`p.free()`, or `p.reset()`.

Prefer a pool for short-lived scratch. Prefer heap factories when ownership
must outlive the block.

See [arrays.md](arrays.md) (pool section) and [defer.md](defer.md).

### 2. Linear obligations — `[[provides]]` / `[[releases]]`

Factory results (`alloc`, `malloc`, `file_open`, heap `cstr(s)`, …) are
owning. The caller must release or retain them (`free` / `[[releases]]` /
`[[retains]]` stash / `[[provides]]` return). Dropping or overwriting an
owned local without discharging it is a compile-time error. Straight-line
`free(p)` / `[[releases]]` also diagnoses later use and double-free of that
local (and Name→Name / field-store / ternary aliases).

Mark new factories `[[provides]]` and cleanup APIs `[[releases]]`. C headers
are not annotated; libc-style APIs stay borrow-for-call unless you wrap them.

### 3. Slice integrity and the C edge — `T[..]`, `ptr()`, `cstr()`

- Index only `T[N]` and `T[..]` in `.mc` (bounds traps on by default;
  `--release` omits them). Raw `p[i]` and `*(p+i)` are rejected.
- `T[..]` does not decay to a pointer. Use `ptr(s)` for a counted base, or
  `cstr(s)` / `pool.cstr(s)` for a NUL C string.
- Heap `cstr(s)` allocates and is `[[provides]]` (`free` it). Literals and
  `char[N]` do not allocate. `pool.cstr` is region-minted.

Details: [arrays.md](arrays.md).

### 4. Lifetime and provenance — `[[retains]]` and escapes

Stack addresses and deferred-freed values must not escape via `return`,
globals/statics, or stores through other pointers. Passing `&local` into an
ordinary callee is fine; mark `%C` parameters `[[retains]]` when the callee
may stash the pointer (inference covers many `.mc` bodies; exports record
the result). C headers are not analyzed.

Also: [quickstart.md](quickstart.md) (No Stack Escapes / Defer Invalidation)
and [defer.md](defer.md).

## C-boundary caveats (intentional)

| Surface | Checked? |
| ------- | -------- |
| `.mc` locals, `%C` callees, pools, `T[N]` / `T[..]` indexing | Yes |
| Host C headers / libc | No retain/release/provides analysis |
| Function pointers | No |
| Raw `T*` after `ptr()` or an explicit cast | No bounds; treat as foreign |
| Opaque `defer { … }` without a recognized release | Weak / unchecked |

Interop rules: [interop.md](interop.md).

## What this is not

Not memory-safe C, not Rust, not a hidden runtime. Same machine model as C,
with compile-time contracts on the `%C` side and an explicit hole at FFI.
