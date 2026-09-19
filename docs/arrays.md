# Arrays in %C

%C provides three distinct bracket forms for arrays. While all three share a unified mental model, they serve different operational roles at compile time and across C language boundaries.

| Array Kind       | Syntax  | Internal Representation & Semantics                                                                  |
| ---------------- | ------- | ---------------------------------------------------------------------------------------------------- |
| **Fixed array**  | `T[N]`  | Fixed compile-time size N. Allocates stack storage or establishes an exact-size parameter contract.  |
| **Open array**   | `T[]`   | C-compatible parameter syntax. Decays directly to T * with no length metadata preserved in the type. |
| **Ranged array** | `T[..]` | Opaque `{ T *ptr; size_t len; size_t cap }` header (interned per element type T). Like `T *`: address plus bounds; ownership is convention. |

## Fixed Arrays `T[N]`

Use a fixed array when storage allocation is owned locally and the element count is known at compile time:

```c
int a[4] = {0};
char buf[64] = {0};
```

Fixed arrays implicitly convert to `T[..]` where a ranged type is expected. In user translation units, declaring a parameter as `void f(int a[N])` enforces an exact `T[N]` matching requirement at call sites. Within `f`, calling `len(a)` correctly returns the compile-time length `N`.

## Open Arrays `T[]`

Open arrays represent C-compatible parameters that intentionally omit size metadata from the type signature:

```c
void legacy(int a[]);   /* same as int * at the ABI */
```

Open arrays should be reserved for external C headers and foreign function boundaries where length is not enforced by the parameter contract. In user source code, evaluating `sizeof` on an open array parameter is a compile-time error because the parameter behaves as a raw pointer.

## Ranged Arrays `T[..]`

A ranged array is an opaque `{ T *ptr; size_t len; size_t cap }` header
(interned per element type). User code does not name those fields; use
`len(s)`, `cap(s)`, and `ptr(s)`. Rebind the header by assignment (`s = …`);
element writes through `s[i]` are fine. The language does not allocate or
`free` through the header — same as `T *`.

**Ownership is convention, like `char *`.** The type is not “view-only” and
not an owning container. A `char[..]` (or `T[..]`) field or local may be:

- the only handle to a `malloc` / `realloc` block (`str_free(&s)` or
  `free(ptr(s))` when done),
- a window into stack storage, an arena region, or a caller buffer,
- a subslice or string literal (must not free).

Do not invent a parallel `{ char *p; size_t len; size_t cap }` struct to “own”
what `char[..]` already is. Prefer storing `char[..]` in structs when you
need length-aware text; keep `char *` for foreign NUL C boundaries, then
convert once with `ranged` / `str_from_cstr`. Heap growth:
`str_dup` / `str_reserve` / `str_append` / `str_set` / `str_free` in `str`.
Region lifetimes: `import "arena"` (`defer a.free()`).

```c
int[..] s = {0};
char[..] line = {0};

void upload(Vertex[..] verts);

struct Token {
	char[..] text;   /* fine: header in a struct; who frees is separate */
};
```

`len(s)` is the initialized window. Indexing (`s[i]`), `len(s)`, and range-for
stop there. `cap(s)` is how many elements are addressable from `ptr(s)`
(`len <= cap`). Spare room is for mutators that grow into the same block; it is
not inherited by a subslice.

Closed windows (string literals, `T[N]` → `T[..]`, and `s[lo..hi]`) set
`len == cap`. Writable scratch over a block is `ranged(p, 0, n)` — length
zero, capacity `n`. To grow length into spare capacity, assign a new header
(`s = ranged(ptr(s), new_len, cap(s))`), do not write fields.

### Constructing Ranged Arrays

```c
int a[4] = {0};
int[..] s = {0};
const char[..] line = {0};

s = a;                  /* implicit at ranged sites; len == cap == 4 */
s = ranged(a, 2);       /* pointer + count; len == cap == 2 */
s = ranged(a, 0, 4);    /* empty scratch over a; len 0, cap 4 */
s = a[1..3];            /* subrange; end exclusive; len == cap */
s = a[2..];             /* open end through len(a) */
line = "James";         /* requires const char[..]; NUL excluded from len and cap */
```

Assigning a string literal to `const char[..]` sets length and capacity to the byte
length of the text **excluding** the trailing NUL terminator. However, the
backing read-only memory retains the NUL byte, preserving compatibility with
standard C APIs. Mutable `char[..]` cannot bind a literal without a cast; use
`char buf[] = "..."` to copy into a mutable array. See [const.md](const.md).

### Implicit conversions

Implicit conversions apply predictably based on whether the target expects a ranged type or a raw C pointer.

At ranged-typed parameters and assignments:

- `T[N]` → `T[..]`
- `"text"` → `const char[..]` (NUL excluded; needs `const` / `const?` sink)

At pointer-typed parameters and assignments (C boundary):

- `T[..]` → `T *` or `void *` (same as `ptr(s)`)

There is no implicit conversion from `T *` to `T[..]`. You must supply a length with
`ranged(p, n)`.

### C Interoperability and Pointer Decay

At call sites expecting `T *`, a ranged array automatically decays to its underlying pointer:

```c
#include <string.h>

char[..] name = {0};

name = "James";
strlen(name);           /* same as strlen(ptr(name)) */
```

This automatic decay saves syntax overhead but does not make standard C functions length-aware. C library expectations remain unchanged; string functions continue scanning until encountering a NUL byte.

Because pointer decay passes `ptr(s)` without length boundaries, slicing string buffers requires caution when invoking C string utilities:

```c
char buf[] = "hello world";
char[..] word = {0};

word = buf[0..5];       /* len == 5; indices 0..4 are "hello" */
strlen(word);           /* scans until NUL → 11, not 5 */
```

To perform bounded reads safely, use `len(word)` within %C code, or pass explicit length bounds to compliant C functions (such as `strnlen(ptr(word), len(word))`). Similarly, when wrapping non-NUL-terminated buffers (such as output from `read()`), `strlen(chunk)` will read out of bounds because no NUL terminator exists at `len(chunk)`.

## Range-for

Fixed arrays and ranged arrays support ranged iteration without an explicit index. Bind by value with `auto x`, or bind a pointer into the range with `auto *p`:

```c
int a[4] = {1, 2, 3, 4};
int sum = 0;

for (auto x : a) {
    sum = sum + x;
}

for (auto *p : a) {
    *p = *p * 2;
}
```

The same forms work over `T[..]`.

Custom types can participate by providing `overload` hooks. `range_count` supplies a length (and optionally a base pointer via an out-parameter). `range_at` supplies element access when iteration is not over a contiguous buffer:

```c
overload size_t range_count(MyBuf b, int** out_ptr) {
    *out_ptr = b.data;
    return (size_t)b.n;
}

overload size_t range_count(Triple t, int** out_ptr) {
    (void)t;
    *out_ptr = 0;
    return 3;
}

overload int range_at(Triple t, size_t i) {
    if (i == 0) {
        return t.a;
    }
    if (i == 1) {
        return t.b;
    }
    return t.c;
}
```

See `test/range_for.mc` and `test/range_hooks.mc`. Overload rules are in [methods.md](methods.md).

## Const and strings

Prefer `const char[..]` for read-only text (like `const char *`, with length).
Stores through `const` are errors; rebind of the header is allowed. String
literals require a `const` / `const?` sink (or a mutable array copy via
`char buf[] = "..."`):

```c
const char[..] name = "James";
name[0] = 'x';         /* Error: not a modifiable lvalue */

char *p = "James";     /* Error: literal is const */
const char *q = "James";

char buf[8] = {0};
char[..] s = {0};
s = buf;
s[0] = 'x';             /* ok: mutates buf[0] */
```

Passthrough APIs use `const?` so a mutable argument stays mutable at the
result. See [const.md](const.md).

For string literals, prefer `const char[..]` rather than wrapping with `str_from_cstr`. Pass `"..."` where a `const` / `const?` `char[..]` parameter is expected. Use `str_eq(a, "x")` and `str_starts_with(s, "pre")`; reserve `str_from_cstr` / `*_cstr` for foreign NUL-terminated `const char *` values. See [packages.md](packages.md) and `str/mod.mc`.

## Quick Reference

| **Operational Need**                         | **Recommended Type / Construct**                      |
| -------------------------------------------- | ----------------------------------------------------- |
| **Stack Allocation**                         | Fixed array `T[N]`                                    |
| **Unbounded C API Boundary**                 | Open array `T[]` or raw pointer `T *`                 |
| **Pointer + length (+ cap)**                 | Opaque `T[..]` in locals/structs; `ranged(p, n)`, `ranged(p, len, cap)`, `len()` / `cap()` / `ptr()` |
| **C String Integration (`strlen`/`printf`)** | `const char[..]` / `char[..]` (decays to `ptr(s)`; verify NUL termination) |
| **Literal Text Management**                  | `const char[..] = "..."` or `char buf[] = "..."`; `const?` for passthrough |
| **Subrange Slicing**                         | Syntax forms `s[lo..hi]`, `s[lo..]`, or `s[..hi]`     |
| **Who frees the bytes**                      | Convention (like `char *`): `str_free` / arena / stack — not the type |



## Standard Library Packages

Importing `str` (`import "str";`) provides length-aware helpers on `char[..]`:
`cstr_write(buf, s)`, `str_split_once`, trim/chomp, compare, parse via
`str_to_long`, search via `str_find` / `str_ifind`, and heap owners via
`str_dup` / `str_append` / `str_free` (see `str/mod.mc`). Prefer `char[..]`
and non-`*_cstr` APIs for literals (`str_eq(s, "ok")`); use `str_from_cstr` /
`*_cstr` only for foreign `char *`.

Importing `arena` (`import "arena";`) is bump allocation into a region.
Methods `a.copy`, `a.join`, `a.replace`, `a.append` / `a.append_byte`
(assign the returned `char[..]`), and `a.z` for NUL-terminated copies.
Returned headers stay valid until `a.free()` / `a.reset()`. See
`arena/mod.mc`.

`path`, `fs`, and `os` sit beside `str` and `arena`: slash paths, `File` handles, and process helpers, with `(T, bool)` results rather than POSIX or Win32 types. See [os.md](os.md).
