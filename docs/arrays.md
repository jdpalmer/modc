# Arrays in %C

%C provides three distinct bracket forms for arrays. While all three share a unified mental model, they serve different operational roles at compile time and across C language boundaries.

| Array Kind       | Syntax  | Internal Representation & Semantics                                                                  |
| ---------------- | ------- | ---------------------------------------------------------------------------------------------------- |
| **Fixed array**  | `T[N]`  | Fixed compile-time size N. Allocates stack storage or establishes an exact-size parameter contract.  |
| **Open array**   | `T[]`   | C-compatible parameter syntax. Decays directly to T * with no length metadata preserved in the type. |
| **Ranged array** | `T[..]` | A runtime view struct { T *ptr; size_t len; size_t cap } (interned per element type T). |

## Fixed Arrays `T[N]`

Use a fixed array when storage allocation is owned locally and the element count is known at compile time:

```c
int a[4] = {0};
char buf[64] = {0};
```

Fixed arrays implicitly convert to ranged views where expected. In user translation units, declaring a parameter as `void f(int a[N])` enforces an exact `T[N]` matching requirement at call sites. Within `f`, calling `len(a)` correctly returns the compile-time length `N`.

## Open Arrays `T[]`

Open arrays represent C-compatible parameters that intentionally omit size metadata from the type signature:

```c
void legacy(int a[]);   /* same as int * at the ABI */
```

Open arrays should be reserved for external C headers and foreign function boundaries where length is not enforced by the parameter contract. In user source code, evaluating `sizeof` on an open array parameter is a compile-time error because the parameter behaves as a raw pointer.

## Ranged Arrays `T[..]`

A ranged array is an opaque slice header over storage the caller already owns.
Internally it is `{ T *ptr; size_t len; size_t cap }` (interned per element type),
but user code does not name those fields. Use `len(s)`, `cap(s)`, and `ptr(s)`.
Mutate the header only by assigning a new view (`s = …`); element writes through
`s[i]` are fine. There is no allocator on the header.

```c
int[..] s = {0};
char[..] line = {0};

void upload(Vertex[..] verts);
```

`len(s)` is the initialized window. Indexing (`s[i]`), `len(s)`, and range-for
stop there. `cap(s)` is how many elements are addressable from `ptr(s)`
(`len <= cap`). Spare room is for mutators that grow into the same block; it is
not inherited by a subslice.

Views (string literals, `T[N]` → `T[..]`, and `s[lo..hi]`) set `len == cap`. A
writable scratch over storage you own is `ranged(p, 0, n)` — length zero,
capacity `n`. To grow length into spare capacity, assign a new view
(`s = ranged(ptr(s), new_len, cap(s))`), do not write fields.

### Constructing Ranged Arrays

```c
int a[4] = {0};
int[..] s = {0};
char[..] line = {0};

s = a;                  /* implicit at ranged sites; len == cap == 4 */
s = ranged(a, 2);       /* pointer + count; len == cap == 2 */
s = ranged(a, 0, 4);    /* empty scratch over a; len 0, cap 4 */
s = a[1..3];            /* subrange; end exclusive; len == cap */
s = a[2..];             /* open end through len(a) */
line = "James";         /* char[..] only; NUL excluded from len and cap */
```

Assigning a string literal to `char[..]` sets length and capacity to the byte
length of the text **excluding** the trailing NUL terminator. However, the
backing read-only memory retains the NUL byte, preserving compatibility with
standard C APIs.

### Implicit conversions

Implicit conversions apply predictably based on whether the target expects a ranged view or a raw C pointer.

At ranged-typed parameters and assignments:

- `T[N]` → `T[..]`
- `"text"` → `char[..]` (NUL excluded)

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

Fixed arrays and ranged views support ranged iteration without an explicit index. Bind by value with `auto x`, or bind a pointer into the range with `auto *p`:

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

The same forms work over `T[..]` views.

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

## Auto-Const Mutability Tracking

Auto-const enforces write safety across array views by tracking string literal provenance. Storing through a pointer or ranged view backed by immutable memory triggers a compile-time error:

```c
char *s = "James";
s[0] = 'x';            /* Error: Mutation through immutable pointer */

char[..] name = "James";
name[0] = 'x';         /* Error: Mutation through immutable ranged view */
```

Conversely, ranged views created over mutable memory remain writable:

```c
char buf[8] = {0};
char[..] view = {0};

buf[0] = 'a';
view = buf;
view[0] = 'x';          /* ok: mutates buf[0] */
```

Attempting to pass a literal-backed `char[..]` to a function expecting a mutable `char *` parameter is caught by auto-const analysis regardless of whether `name` or `ptr(name)` is supplied.

## Quick Reference

| **Operational Need**                         | **Recommended Type / Construct**                      |
| -------------------------------------------- | ----------------------------------------------------- |
| **Stack Allocation**                         | Fixed array `T[N]`                                    |
| **Unbounded C API Boundary**                 | Open array `T[]` or raw pointer `T *`                 |
| **Safe Pointer + Length Pair**               | Opaque `T[..]`, `ranged(p, n)`, `ranged(p, len, cap)`, `len()` / `cap()` / `ptr()` |
| **C String Integration (`strlen`/`printf`)** | `char[..]` (decays to `ptr(s)`; verify NUL termination) |
| **Literal Text Management**                  | `char[..] = "..."` or `char *` managed by auto-const  |
| **Subrange Slicing**                         | Syntax forms `s[lo..hi]`, `s[lo..]`, or `s[..hi]`     |



## Standard Library Packages

The standard library includes two essential packages for working with string views and memory. Importing `str` (`import "str";`) provides non-owning utilities for `char[..]` views, including safe buffer writes to fixed `char[N]` targets via `cstr_write(buf, view)`, string splitting with `str_split_once`, trimming, chomping, comparisons, and numeric parsing via `str_to_long`. It also supports substring searching through `str_find` and `str_ifind`, which return `(bool, char[..])` tuples where an empty needle matches at position zero. Full definitions are located in `str/mod.mc`.

Similarly, importing `arena` (`import "arena";`) introduces bump allocation. This package handles memory copying and concatenation using `a.copy`, `a.join`, and `a.replace`, incremental growth via `a.append` / `a.append_byte` (assign the returned view), and NUL-terminated C string allocations with `a.z`. Full definitions are located in `arena/mod.mc`.

`path`, `fs`, and `os` sit beside `str` and `arena`: slash paths, `File` handles, and process helpers, with `(T, bool)` results rather than POSIX or Win32 types. See [os.md](os.md).
