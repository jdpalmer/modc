# %C

**%C** (pronounced "mod-C") is a **dialect of C** and an associated compiler that uses the C ecosystem (includes, libraries, linker, etc.) you already have with a tweaked syntax that doesn't completely change what you know about C. No runtime, no borrow checker, no overhead, no new language + FFI. A beautifully simple language that fits in your head (or the context window of our agentic friends).

## Features

* **Vanilla C** without the uncommon and crufty parts (K&R function definitions, digraphs, VLAs, optional annexes, nested ternaries, comma operators, etc.)

* **No prototypes or parallel headers** in pure %C code; `import` exposes package APIs, functions can appear in any order, and mutual recursion just works.

* A **unified type name space**: `struct Point {…}; Point p;`

* clang-style C **function overloading** and plan9-style C **struct inheritance/composition**

* Go-style **receiver syntax** — `(T *r).name(…)`

* An **auto-dot** dereference system (no more arrows!)

* An **auto-const** system that automatically tracks const-ness across function calls without `const` annotations

* An **auto-inline** system that automatically inlines functions without `inline` annotations

* An **`auto`** type specifier that provides type inference and multi-return tuple decoding

* A **`defer`** keyword that supports block-scoped LIFO cleanup

* A **`for`** construct providing ranged iteration — `for (auto x : expr)` by
  value, or `for (auto *p : expr)` to bind a pointer into the range

* **Switch/case ranges** such as `case 0 .. 10` and `case 'a' .. 'z'`

* **Switch/case fallthrough** is explicit

* **Switch/case enums** must be exhaustive

* **Multiple return values** for more consistent data/control flow

* **Common C footguns are rejected in user code** including pointer-primitive unions, `void *` arithmetic, implicit narrowing, unrelated pointer conversions, unsequenced `i = i++`, assignments in conditionals, and other classic C traps

* **Defined behavior** for things that are often undefined in standard C (including `char`'s definition as an unsigned value and widths for numeric types)

* **Ranged arrays** that provide typed pointer/length views (subrange + ranged iteration)

* **UTF8** by default (stdlib wchar support remains intact)

* Strong tooling for source [packages](docs/packages.md), [documentation](docs/modc-doc.md), [source formatting](docs/modc-format.md), [tests](docs/modc-test.md), and more

* A small readable compiler!

## Anti-Features

* No memory management system

* No concurrency system; host C plus `defer` instead

* No object system; lightweight struct composition plus `overload` and receiver syntax instead

* No templates; No _Generic; but some generic programming possible with `overload`

* Not a drop in replacement for C; uses a more forgiving dialect of C for header compatibility and a stricter dialect in user code

## Hello, world

At its core %C is just C.

```c
#include <stdio.h>

int main() {
    printf("hello, world\n");
    return 0;
}
```

```sh
modc build && ./hello
```

But %C also supports some fancy dialect extras:

```c
#include <stdio.h>
#include <stdlib.h>

struct Point {
    double x;
    double y;
};

struct Particle {
    Point; // Plan 9 anonymous embed
    double mass;
};

// Comments before functions are documentation.
double dist2(Point * p) {
    return p.x * p.x + p.y * p.y; // auto-dot through pointers
}

int main() {
    Particle * particles = malloc(100 * sizeof(Particle));
    defer free(particles);
    double system_mass = 0;
    auto cloud = ranged(particles, 100);
    for (auto *p: cloud) {
        p.x =(double) rand() /(double) RAND_MAX;
        p.y =(double) rand() /(double) RAND_MAX;
        p.mass = dist2(p); // Particle upcasts to Point
        system_mass += p.mass;
    }
    printf("Particle system created with total mass %f.\n", system_mass);
    return 0;
}
```

## Documentation

Full index: [docs/README.md](docs/README.md).

| Doc | Topic |
| --- | ----- |
| [Installation](docs/installation.md) | Build and install `modc` |
| [Quickstart](docs/quickstart.md) | Write and build a first program |
| [Interop](docs/interop.md) | User `.mc` versus `#include` headers |
| [Packages](docs/packages.md) | `import`, vendoring, and cache |
