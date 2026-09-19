# Const, string literals, and `const?`

%C spells C-style **pointee `const`** in user source and headers. String
literals are **const** (limited C++ rule): they bind to `const char *` /
`const char[..]` / `const?` sinks, not to mutable `char *` or `char[..]`.
Read-only APIs take `const` so call sites stay cast-free; stores through
read-only views are diagnosed.

## Spelling

```c
int len(const char *s);
bool str_eq(const char[..] a, const char[..] b);

const char *p = "hi";
const char[..] v = "ab";
```

- `const T *` — cannot store through the pointer; the pointer variable may be reassigned.
- `const char[..]` — **element/string const** (like `const char[]`), not a frozen header. Rebind (`v = other`) is allowed; `v[i] =` is not.
- Discarding const requires an explicit cast: `char *q = (char *)p`.

Mutable → const is implicit. Const → mutable is not.

## Literals (limited C++)

```c
char *p = "hello";           /* error — needs const or a cast */
char[..] s = "hello";        /* error — needs const or a cast */
const char *q = "hello";     /* OK */
const char[..] v = "hello";  /* OK */
char buf[] = "hello";        /* OK — copies into a mutable array */
q[0] = 'x';                  /* error: not a modifiable lvalue */
```

Writing through a cast to a string literal remains undefined at run time; the
type system only blocks the accidental mutable binding.

## `const?` (passthrough)

For APIs that return a view into an argument (find, trim, path_dir, …), spell
`const?` so constness follows the call site:

```c
const? char[..] str_trim(const? char[..] s);
(bool, const? char[..]) str_find(const? char[..] hay, const char[..] needle);
```

- At the call site, a const argument yields a const result; a mutable argument
  yields a mutable result (no dual overloads).
- Inside the function body, `const?` parameters are treated as **const** (read-only).
- String literals match `const?` sinks.

Use ordinary `const` when the result is always read-only (or newly allocated
mutable storage, which should be plain `char[..]` / `char *`).

## `strchr` and friends (C vs C++ vs %C)

Search APIs that return a pointer **into** an input string are the usual
const-safety stress test.

### C

```c
char *strchr(const char *s, int c);
```

One function, one symbol. The parameter is `const` so you can pass a
read-only string, but the return type is always `char *` — a silent
**const discard**. The pitfall: a `const char *` argument yields a mutable
`char *` into the same memory, so the type system will not stop a write
through the result (undefined if the object really was const / a literal).

### C++

```c
const char *strchr(const char *s, int c);
      char *strchr(      char *s, int c);
```

Two overloads restore const correctness: const in → const out, mutable in →
mutable out. The pitfall: you need a language with overloading (and two
declarations) for what is still one libc symbol; C headers cannot express
this, and mixing C and C++ declarations is easy to get wrong at the boundary.

### %C

```c
const? char *strchr(const? char *s, int c);
```

One prototype, one libc symbol. `const?` means “same constness as the
argument at this call site”: a const (or literal) argument gives
`const char *`; a mutable `char *` gives `char *`. No overload pair, no
silent discard. Host `<string.h>` / `<stdlib.h>` use this for the
passthrough search APIs:

```c
const? char *strchr(const? char *s, int c);
const? char *strrchr(const? char *s, int c);
const? char *strstr(const? char *haystack, const char *needle);
const? char *strpbrk(const? char *s, const char *accept);
const? void *memchr(const? void *s, int c, size_t n);
const? void *bsearch(const void *key, const? void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *));
```

Not rewritten: APIs that allocate (`strdup`), return static storage
(`strerror`, `getenv`), or mutate through the pointer (`strtok`).

Imported plain `const T *` (for example `strlen`) still maps to
`Type.is_readonly` the same as user `const`.

## Escape hatch

```c
char *s = (char *)"hello";
char[..] v = (char[..])"hello";
```

Use sparingly at foreign or intentionally mutable boundaries.
