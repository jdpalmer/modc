<p align="center">
  <img src="logo.svg" alt="%C" width="128" height="128">
</p>

**%C** (pronounced "mod-C") is a **dialect of C** and an associated compiler that uses the C ecosystem (includes, libraries, linker, etc.) you already have with a tweaked syntax that doesn't completely change what you know about C. No runtime, no borrow checker, no overhead, no new language + FFI. A beautifully simple language that fits in your head (or the context window of our agentic friends).

## Features

* **Vanilla C** without the uncommon and crufty parts (K&R function definitions, digraphs, VLAs, optional annexes, nested ternaries, comma operators, etc.)

* **No prototypes or package headers** in pure %C code; `import` exposes package APIs, functions can appear in any order, and mutual recursion just works.

* **Unified type name space** like C++: `struct Point {…}; Point p;`

* **Function overloading** like clang's overloadable extension and **struct inheritance/composition** like Plan 9 C

* **Receiver syntax** like Go — `(T *r).name(…)`

* **Dot accessors** automatically dereference (no more arrows!)

* **`const` / `const?`** for read-only and passthrough APIs without poisoning callers

* **`auto`** type specifier provides type inference and multi-return tuple decoding

* **`defer`** keyword that supports block-scoped LIFO cleanup

* A **`for`** construct providing ranged iteration — `for (auto x : expr)` by
  value, or `for (auto *p : expr)` to bind a pointer into the range

* **Switch/case ranges** such as `case 0 .. 10` and `case 'a' .. 'z'`

* **Switch/case fallthrough** is explicit (`[[fallthrough]];`)

* **Switch/case enums** must be exhaustive

* **Multiple return values** for more consistent data/control flow

* **Common C antipatterns are rejected in user code** including pointer-primitive unions, `void *` arithmetic, implicit narrowing, unrelated pointer conversions, unsequenced `i = i++`, assignments in conditionals, and other classic C traps

* **Defined behavior** for things that are often undefined in standard C (including `char`'s definition as an unsigned value and widths for numeric types)

* **Ranged arrays** that provide typed pointer/length views (subrange + ranged iteration)

* Strong tooling for source [packages](docs/packages.md), [documentation](docs/modc-doc.md), [source formatting](docs/modc-format.md), [tests](docs/modc-test.md), and more

## Anti-Features

* No memory management or concurrency system; host C plus `defer` instead

* No object system; lightweight struct composition plus `overload` plus receiver syntax instead

* No templates or `_Generic`; some generic programming is still possible with `overload`

* Not a drop-in replacement for C: stricter `.mc` dialect, more forgiving headers

## Hello, world

At its core %C is just C with source files that use the `.mc` extension. 

```c
#include <stdio.h>

int main() {
    printf("hello, world\n");
    return 0;
}
```

And if you have [installed](docs/installation.md) `modc`, you can build it in one line:

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
    Point; // struct composition
    double mass;
};

// Comments before functions are `modc doc` documentation
overload double dist2(double x, double y) {
    return (x * x) + (y * y);
}

overload double dist2(Point* p) {
    return dist2(p.x, p.y); // Dot access through pointers
}

int main() {
    Particle* particles = malloc(100 * sizeof(Particle));
    defer free(particles);
    double system_mass = 0;
    auto cloud = ranged(particles, 100);
    for (auto* p: cloud) {
        p.x = rand() / (double)RAND_MAX;
        p.y = rand() / (double)RAND_MAX;
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
