# Memory and lifetimes in %C

%C preserves C’s memory layout, binary ABI, and host linker. There is no garbage collector, no hidden runtime, and no complex borrow checker. 

Memory safety in user `.mc` code is achieved through **four lightweight, mutually reinforcing contracts** enforced by the compiler, paired with an explicit, predictable boundary for foreign C headers.

---

## Allocation Tiers at a Glance

%C organizes memory into three transparent tiers. Each has clear ownership, lifetime boundaries, and escape rules:

| Tier | Syntax | Backing & Lifetime | How It Is Freed | Escape Rules |
| :--- | :--- | :--- | :--- | :--- |
| **Stack** | `T x = {0};`<br>`T arr[N];` | Current lexical frame (scalars, aggregates, fixed arrays) | Automatic at block exit | Cannot escape function (`&local` / stack `T[..]` return or stash diagnosed). |
| **Pool** | `pool p { … }` | Lexical bump slab | Automatic at `pool` block exit (or `p.free()` / `p.reset()`) | Minted views cannot escape the `pool` block. |
| **Heap** | `alloc(T)` / `alloc(T, n)`<br>`cstr(s)` / `malloc` | Global heap (`malloc`) | Explicit `free(p)`, `release(&s)`, `[[releases]]`, or `defer` of those | Tracked by owned-obligation analysis (`[[provides]]` factories + builtins). |

Heap `T[..]` headers grow with the builtins `reserve` / `push` / `append` and are freed with `release(&s)` — those mutate or free an existing header; they are not a separate allocation tier.

---

## The Four Pillars

```
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 1. Lexical Pools (pool p { })      ──  Fast scratch; scope-bound       │
 │ 2. Linear Obligations (provides)   ──  Leak & use-after-free checking  │
 │ 3. Slice Integrity (T[..], cstr)   ──  Bounds checks; explicit C sinks │
 │ 4. Provenance & Escapes (retains)  ──  Stack & defer escape prevention │
 └────────────────────────────────────────────────────────────────────────┘
```

---

### 1. Lexical Bump Pools — `pool p { … }`

A `pool` block provides cheap bump allocation for temporary data. All memory allocated through `p` is reclaimed when control leaves the enclosing block, acting like an automatic, guaranteed `defer p.free()`.

```c
int run_task(const char[..] name) {
    pool p {
        // Allocate a slice in the pool:
        char[..] buf = p.alloc(char, 256);
        
        // Format or manipulate text:
        p.append(&buf, "task: ");
        p.append(&buf, name);
        
        // Pass to a C API requiring a NUL-terminated string:
        FILE* f = fopen(p.cstr(buf), "w");
        if (f != NULL) {
            defer fclose(f);
            fputs("running\n", f);
        }
    } // Memory for `buf` and `p.cstr` is automatically freed here!
    return 0;
}
```

#### Pool Operations
Within a `pool p { … }` block:
- **`p.alloc(T)`** / **`p.alloc(T, n)`**: Mint a `T*` or `T[..]` inside the pool.
- **`p.copy(&out, s)`** / **`p.append(&out, s)`** / **`p.push(&out, item)`**: Slice mutation in the pool.
- **`p.join(&out, sep, parts, n)`** / **`p.replace(&out, s, old, new)`**: String manipulation in the pool.
- **`p.cstr(s)`**: Produces a guaranteed NUL-terminated `char*` within the pool.
- **`p.reset()`**: Recycles the pool's slab from offset 0 while keeping capacity.

#### Invalidation and Escape Rules
Views minted from `p` carry compile-time region wiring (automatic for builtin pool ops — you do not annotate `pool` call sites with `[[region]]`):
1. **Block Escape**: A pool view cannot leave the `pool` block via `return`, assignment to outer variables, or passing to `[[retains]]` parameters.
2. **Reset Invalidation**: Calling `p.reset()` or `p.free()` invalidates all existing views minted from `p`. Any subsequent read or use is diagnosed as a compile-time error.

```c
char[..] bad_escape() {
    pool p {
        char[..] s = {0};
        p.copy(&s, "hello");
        return s; // Error: view minted from pool 'p' cannot escape block
    }
}
```

#### Custom arenas with `[[region]]`
Builtin `pool` wiring is automatic. For a hand-rolled bump/arena API, mark the
region parameter so returned pointers/`T[..]` die with that object’s
`[[releases]]` cleanup (same escape rules as pool views):

```c
int* ([[region]] Arena* a).bump();
void ([[releases]] Arena* a).reset();
```

See [arrays.md](arrays.md) (pool section), [defer.md](defer.md), and `test/pool_pkg.mc` / `test/region_escape_*.mc`.

---

### 2. Linear Obligations — `[[provides]]` / `[[releases]]`

For long-lived heap memory, %C enforces **linear obligation tracking** on local variables without requiring a garbage collector:

- **`[[provides]]`**: Placed on factory functions (`file_open`, `str_dup`, custom constructors). Builtin factories (`alloc`, `malloc` / `calloc` / `realloc` / `strdup`, heap `cstr(s)`) are recognized the same way even without an attribute on the C declaration. The returned value carries an **owned obligation**.
- **`[[releases]]`**: Placed on `%C` cleanup APIs (`str_free`, `f.close()`, custom destructors). Builtin releases (`free`, `fclose`, `release` on heap `T[..]`) consume ownership without needing the attribute on libc.

An owned variable must be discharged along every control-flow path by:
1. Releasing it via a builtin release or `[[releases]]` call (or scheduling that via `defer`).
2. Stashing it into a parameter marked `[[retains]]`.
3. Returning it from a function marked `[[provides]]`.

```c
struct Node {
    int id;
};

[[provides]] Node* node_create(int id) {
    Node* n = alloc(Node); // alloc is [[provides]]
    n.id = id;
    return n;              // Discharged: returned from [[provides]] function
}

void process() {
    Node* n = node_create(42);
    defer free(n);         // Discharged: scheduled for release on block exit

    // ... use n ...
}
```

#### Compile-Time Leak & Double-Free Diagnostics
Dropping or overwriting an owned value before releasing it is rejected at compile time:

```c
void leak_example() {
    int* z = alloc(int);
    // Error: owned value 'z' never released or retained
}

void overwrite_example() {
    int* z = alloc(int);
    z = alloc(int); // Error: owned value 'z' overwritten without release
    free(z);
}

void uaf_example() {
    char[..] s = alloc(char, 16); // alloc(T, n) → T[..]; alloc(T) → T*
    release(&s);
    s[0] = 'x';    // Error: use of released value 's'
    release(&s);   // Error: use of released value 's' (double-free)
}
```

#### Copy-Alias Tracking
When a pointer or `T[..]` is copied between locals (`q = p`), stored into a
top-level field (`b.p = z`), or merged through a ternary (`q = c ? a : b`),
the compiler links them in an alias group so a release of one poisons the
others:
```c
void alias_example() {
    int* p = alloc(int);
    int* q = p;
    free(p);
    *q = 10;        // Error: use of released value 'q' (aliased to 'p')
}

typedef struct {
    char* p;
} Box;

void field_alias_example() {
    Box b = {0};
    char* z = malloc(16);
    b.p = z;
    free(z);
    *b.p = 'x';     // Error: use of released value (field aliased to 'z')
}
```

---

### 3. Slice Integrity & The C Boundary — `T[..]`, `ptr()`, `cstr()`

%C replaces dangerous raw C pointer arithmetic with first-class ranged slices (`T[..]`), while maintaining compatibility with foreign C APIs:

```c
// Opaque slice header: { T* ptr, size_t len, size_t cap }
int[..] s = alloc(int, 10);
defer release(&s);
```

#### Default Bounds Checks
- **Indexing and Subranges**: `s[i]` and `s[lo..hi]` are **bounds-checked by default**. Out-of-bounds accesses trap with `__modc_bounds_fail`. `--release` disables checks for verified, performance-critical inner loops.
- **Raw Pointers Cannot Be Indexed**: In `.mc` user code, expressions like `p[i]` and `*(p + i)` on a raw pointer `T*` are compile-time errors. You must provide an explicit length contract first via `ranged(p, n)`.

#### Banned Implicit Pointer Decay
In user code, **`T[..]` does not implicitly decay to `T*` or `void*`**. The programmer must make their intent explicit:

1. **`ptr(s)` — Counted Buffer Pointer**:
   Extracts the base pointer for APIs that take a pointer accompanied by an explicit count or size:
   ```c
   write(fd, ptr(s), len(s));
   memcpy(ptr(dst), ptr(src), len(src));
   ```
2. **`cstr(s)` / `p.cstr(s)` — NUL-Terminated C String**:
   Produces a guaranteed NUL-terminated `char*` for foreign C APIs that expect a C string (`fopen`, `sqlite3_open`, `puts`):
   - **Inside a pool (`p.cstr(s)`)**: Allocated in the active pool slab. No separate heap allocation; reclaimed with the pool at block exit.
   - **On the heap (`cstr(s)` on a dynamic `char[..]`)**: Allocates a heap buffer with a trailing `\0`. Treated as `[[provides]]` (caller must `free` or `defer free`).
   - **String literals and `char[N]` (`cstr("literal")`, `cstr(buf)`)**: Evaluate to the existing backing address with **no** allocation (and no owned obligation). Fixed buffers are assumed already NUL-terminated when you pass them this way.

```c
void open_file(const char[..] path) {
    // fopen(path, "r");        <-- Compile error: cannot pass char[..] to const char*
    // fopen(ptr(path), "r");   <-- Unsafe! Subslice may lack trailing '\0'

    pool p {
        FILE* f = fopen(p.cstr(path), "r"); // Safe: pool guarantees trailing '\0'
        if (f != NULL) {
            defer fclose(f);
            // ...
        }
    }
}
```

#### Safe Standard Library Intrinsics
When calling hosted `<string.h>` functions with `char[..]`:
- **`strlen(s)`**: Lowers to $O(1)$ `len(s)` with no NUL scan.
- **`strcmp(a, b)`**: Compares up to $\min(\text{len}(a), \text{len}(b))$ and checks length differences safely.
- **`printf("%s", slice)`**: Automatically rewritten to `%.*s` with `(int)len(slice)` and `ptr(slice)`.

Details: [arrays.md](arrays.md) and [interop.md](interop.md).

---

### 4. Lifetime & Provenance — `[[retains]]` and Stack Escapes

%C prevents dangling stack pointers and use-after-scope bugs via intraprocedural provenance tracking:

```c
int* bad_stack_return() {
    int x = 42;
    return &x; // Error: address of local 'x' escapes this function
}

int[..] bad_stack_slice() {
    int buf[4] = {1, 2, 3, 4};
    return buf[0..2]; // Error: view of local array 'buf' escapes this function
}
```

#### Passing `&local` vs. Retaining Callees
Passing the address of a local variable to an ordinary callee is safe and idiomatic:
```c
void fill_point(Point* p);

void caller() {
    Point pt = {0};
    fill_point(&pt); // OK: fill_point borrows pt for the duration of the call
}
```

However, if a callee stores that pointer into an escaping destination (a global variable, an out-parameter, or an aggregate field), the parameter must be marked **`[[retains]]`**:

```c
struct Registry {
    Point* p;
};

void register_point(Registry* r, [[retains]] Point* p) {
    r.p = p; // Retains p into r
}

void caller(Registry* r) {
    Point pt = {0};
    register_point(r, &pt); // Error: passing stack address to [[retains]] parameter
}
```

- **Inference & Export**: Within `%C` packages, the compiler automatically infers `[[retains]]` by analyzing function bodies and records the attribute in package exports.
- **Write-Back Through Borrows**: Modifying an object through a borrowed pointer (e.g. `c.p = c.p + 1` or `a[i] = a[j]`) is recognized as a local mutation and does not infer `[[retains]]`.

#### `defer` Invalidation
When `defer free(p)` / `defer release(&s)` or a `[[releases]]` call is scheduled, that pointer or view (and any variable aliased to it) is marked dead at block exit:
```c
char* bad_defer() {
    char* p = alloc(char);
    defer free(p);
    return p; // Error: 'p' is invalidated by a defer in this block and cannot escape
}
```

Opaque `defer { … }` bodies that do not contain a recognized release (`free` /
`fclose` / `[[releases]]` / pool consume) are **not** fully checked for
ownership discharge — prefer a single releasing call after `defer`, or a
named cleanup API marked `[[releases]]`.

See [quickstart.md](quickstart.md) and [defer.md](defer.md).

---

## The Foreign C Boundary (Asymmetric Design)

%C uses an **asymmetric verification model**: user `.mc` code is strictly validated at compile time, while included C headers are trusted for host ABI interoperability:

| Language Surface | How %C Verifies It |
| :--- | :--- |
| **`.mc` locals, params, and returns** | Full escape, provenance, dead-init, and linear obligation tracking. |
| **`pool p { … }` allocations** | Lifetime strictly bounded to `{ … }`; cannot escape (automatic region wiring). |
| **Custom `[[region]]` APIs** | Same escape rules as pools when you mark the region parameter. |
| **`T[..]` / `T[N]` indexing** | Checked by default at runtime (`__modc_bounds_fail`). |
| **Method receivers `(T* r)`** | Checked for NULL on entry by default (`__modc_null_fail`). |
| **Host C headers / libc** | No retain/release/`[[provides]]` analysis; treated as borrow-for-call unless wrapped in `%C`. |
| **Raw `T*` via `ptr()` or casts** | Unchecked; bounds must be managed by the developer. |
| **Function pointers** | Calls through function pointers are not statically analyzed for retain/release. |
| **Opaque `defer { … }`** | Weak unless the body contains a recognized release. |

### Handling Retaining C APIs
Most C library functions only borrow pointers for the call (`fopen`, `strlen`, `sqlite3_open`). However, some C APIs stash pointers into global or heap structures (`putenv`, GUI callbacks, thread workers).

- **For Borrowing APIs**: Use `pool p { c_api(p.cstr(s)); }` (or a short-lived heap `cstr` you `free` after the call returns).
- **For Retaining APIs**: Host C does **not** discharge an owned obligation. Wrap the stash in a `%C` API marked `[[retains]]` (or keep the buffer for the process lifetime and accept that the checker still wants an explicit discharge path):
  ```c
  void env_set([[retains]] char* z) {
      putenv(z); // libc; not analyzed — the [[retains]] wrapper discharges ownership
  }

  void install_env(const char[..] env_str) {
      char* z = cstr(env_str); // Heap allocation ([[provides]])
      env_set(z);              // Retained; do not free while still in the environment
  }
  ```

---

## What This Is and Is Not

- **Not Rust**: There are no explicit lifetime annotations (`'a`), no borrow-checker graph fighting, and no monomorphization overhead.
- **Not Cyclone C**: There are no region polymorphism type variables (``r`) and no global garbage collector for the heap.
- **Not a New Runtime**: There is no garbage collector, no green threading runtime, and no custom allocator imposed on the host platform.
- **Vanilla C Power**: It produces standard QBE text and links with the system C compiler and libc, delivering the safety of a modern language with the transparency, speed, and simplicity of C.
