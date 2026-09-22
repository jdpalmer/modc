# %C quickstart

This guide is designed to get you started writing %C code. For the full index of %C documentation, see [README.md](README.md).

## Hello world.

If you already know C, then %C will look very familiar.

For the examples below, create a folder called `helloworld` and save the following program as `hello.mc` within that directory.

```c
#include <stdio.h>

int main() {
    printf("hello, world\n");
    return 0;
}
```

While %C is very similar to C, it is neither a strict superset nor a strict subset.  Thus %C files use their own `.mc` file extension to differentiate them from `.c` files.

## Compiling a program

If `modc` is not on your `PATH` yet, build and install it first; see [installation.md](installation.md).

Building our program is easy:

```sh
cd helloworld
modc build
./helloworld
```

But %C is loaded with a number of useful build tools:

```sh
modc build                    # build current dir; -o defaults to dir name
modc build hello.mc -o hello  # modc → qbe → cc → binary
modc build pkgdir/            # same for a package directory
modc check hello.mc           # parse + type-check only
modc emit hello.mc            # QBE IL to stdout
modc run hello.mc             # build temp, run, delete
modc build hello.mc -o hello -- -lglfw   # extra linker flags after --
```

`modc build` writes a content-addressed cache under `.modc-cache/` (package objects and link inputs keyed by sources, flags, and tool versions). Hits skip re-emit and reassemble. You can use `modc clean` to remove that cache for a project.

## %C for C programmers

While our "Hello, World!" example demonstrates %C's deep compatibility with C, the language truly shines through its subtle extensions: introducing compile-time safety and modern ergonomics without sacrificing simplicity or low-level control.

### defer

Resource management in %C relies on the `defer` statement to guarantee block-scoped cleanup. A `defer` statement schedules a statement to execute when the enclosing block exits. Deferred statements run in last-in, first-out (LIFO) order, ensuring that resources allocated later in a block are released before those initialized earlier.

When a function executes a `return` statement, the return expressions are evaluated first, capturing their values before any deferred code runs.

```c
void work() {
    char *p = malloc(64);
    defer free(p);
    // more code..
}
```

Because deferred statements execute prior to block exit, you must never return a pointer that references memory freed by a `defer`. Doing so returns a dangling pointer to deallocated storage.

### auto and multi-return

The `auto` keyword introduces type inference and destructuring capabilities, simplifying variable declarations without sacrificing static type safety. When initializing a single variable, `auto` infers the type directly from the right-hand initializer expression.

Functions in %C can return multiple values as tuples. Destructuring syntax allows tuple return values to be unpacked directly into individual local variables in a single declaration.

```c
(int, bool) parse(int n) {
    if (n < 0) {
        return (0, false);
    }
    return (n * 2, true);
}

auto n = 3;
auto (v, ok) = parse(5);
```

In this example, `n` is inferred as an integer, while `parse(5)` returns a two-value tuple containing a result and a status flag. The `auto (v, ok)` statement infers the respective type for each field and binds them to `v` and `ok`. This pattern avoids the need for out-parameters or explicit struct definitions when returning multiple values.

### Arrays

%C provides three distinct bracket forms for arrays, unifying stack storage, foreign C interfaces, and length-aware headers. Fixed arrays (`T[N]`) represent fixed-size compile-time storage. Open arrays (`T[]`) maintain compatibility with standard C parameter syntax, decaying directly to pointers without length information in the type signature. Ranged arrays (`T[..]`) are opaque `{ T *ptr; size_t len; size_t cap }` headers — like `T *` with bounds; ownership is convention (no automatic `free`). Use `len(s)`, `cap(s)`, and `ptr(s)`; assign to rebind the header. Closed windows set `len == cap`; writable scratch is `ranged(p, 0, n)`. Store `T[..]` in structs when you need length; prefer `char *` only at foreign NUL boundaries.

```c
int a[4] = {0};
int[..] s = {0};

s = a;               // implicit at ranged sites; len == cap == 4
s = ranged(a, 2);    // explicit count; len == cap == 2
s = ranged(a, 0, 4); // empty scratch over a
s[0] = 1;
s = a[1..3];         // window over &a[1], len 2
s = a[2..];          // through end
len(a);              // element count
cap(s);              // capacity (equals len for a closed window)
strlen(s);           // T[..] → T* at pointer sites (uses ptr(s))
```

### Ranged Iteration

Ranged iteration provides a clean syntax for traversing fixed arrays and `T[..]` values without manually tracking loop indices or array bounds. The range clause iterates over the underlying collection, binding each element either by value using `auto x` or by pointer using `auto *p`.

Binding by value is ideal for read-only passes or accumulation, while binding by pointer provides direct access to mutate elements in place:

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

The same forms work over `T[..]` views. Custom types can participate by providing `overload` hooks; see [arrays.md](arrays.md).

### Overload

The `overload` keyword introduces compile-time function overloading, allowing multiple functions to share the same identifier when distinguished by their parameter types or arity. This mechanism provides function overloading similar to Clang's `overloadable` attribute in C.

When functions are marked with `overload`, the compiler generates unique, name-mangled symbols for the linker to resolve calls correctly based on the argument types at the call site:

```c
overload int max(int a, int b) {
    return a > b ? a : b;
}

overload float max(float a, float b) {
    return a > b ? a : b;
}

int a = max(1, 2);
float f = max(1.0f, 2.0f);
```

Ordinary free functions without the `overload` keyword preserve their exact source name as the exported linker symbol. Consequently, functions intended as stable C entry points or ABI boundaries should not be marked `overload` if an unmangled symbol name is required. See [methods.md](methods.md) for further details on method dispatch and symbol visibility.

### Receiver syntax

Receiver syntax attaches functions directly to typed pointer types, enabling object-oriented call patterns without modifying struct layouts or introducing runtime dispatch overhead. Method definitions declare the receiver before the function name using the syntax `(T *r).name(...)`, while call sites invoke methods using standard dot notation.

Within a method body, the receiver behaves as an explicit named parameter; there is no implicit `this` or `self` keyword. Field access through the pointer receiver uses the standard dot operator (`.`), matching the language's unified member access rules:

```c
struct Window {
    char *title;
};

void (Window *w).show() {
    /* … */
}

void (Window *w).set_title(char *title) {
    w.title = title; /* auto-dot through the pointer */
}

Window *w = /* … */;
w.show();
w.set_title("Tasks");
```

Methods must be defined within the same package as the target type `T` and must always specify an explicit pointer receiver (`T *`). Additionally, a method name cannot conflict with any existing field name on the underlying type `T`. See [methods.md](methods.md) for deeper details on receiver bindings and structural constraints.

### Packages

In %C, code reuse is organized around source packages, eliminating the need to write or maintain separate header files for internal code. Any non-`static` identifier declared at file scope acts as part of the package's public interface, while functions and variables marked `static` remain strictly file-private.

Packages are imported by path or module name using the `import` directive:

```c
// log.mc
void log_info(char *msg) { /* … */ }
static int helper() { return 1; }

// app.mc
import "log";

int
main()
{
    log_info("hi");
    return 0;
}
```

When resolving an `import` statement, the compiler uses a first-match-wins search strategy. It searches for package declarations in the following sequence:

1. The directory containing the importing source file

2. The project root (the nearest parent containing `modc.ini`, or the build
   entry's directory when there is no manifest)

3. Parent directories walking upward through local `vendor/` folders

4. Standard search paths specified via `-M` flags or the `MODC_PATH` environment variable

5. The system library root (such as `lib/modc/pkg` or the in-tree library directory)

An imported path can refer either to a single `.mc` file or to a directory containing multiple `.mc` files (where `mod.mc` is loaded first, if present). When importing vendored dependencies, write `import "math";` for a module located at `vendor/math/` rather than including the `vendor/` prefix in the import path.

For external C standard library linkages or third-party binary dependencies, source files declare their C link directives inline:

```c
#pragma modc c_libs(m)           /* → -lm on build/run */
```

Standard C header inclusion via `#include` remains fully supported for interoperation with system C libraries and legacy headers.

## modc format

The `modc format` command rewrites %C source files in place to enforce a single deterministic code style across a project. It applies opinionated layout rules, such as standard tab indentation, K&R placement for braces, and consistent pointer type alignment (`T* p`), without requiring or supporting configuration options.

Because formatting is strictly constrained to source layout, the tool does not expand macros or alter included header files:

```sh
modc format hello.mc
```

By restricting style options, `modc format` ensures that codebases remain consistent regardless of editor setup or individual author preference. Full mechanics and driver details are documented in [modc-format.md](modc-format.md). Conventions beyond layout are in the quickstart guidelines: [quickstart.md](quickstart.md).

## modc doc

The `modc doc` command extracts and displays a package's public API directly from source code. Any standard `//` or `/* */` comment placed immediately above a non-`static` file-scope declaration is treated as the documentation string for that symbol. The tool avoids special marker syntax like `///` or `//@`, relying instead on clear, natural prose positioned adjacent to declarations.

```c
// add returns the sum of a and b.
int add(int a, int b) {
    return a + b;
}
```

The command accepts package paths or specific symbol references to inspect declarations at various levels of granularity:

```sh
modc doc .              # current package
modc doc pkg            # resolve package pkg
modc doc pkg.add        # one symbol
```

By extracting documentation directly from plain comments, `modc doc` keeps API reference material close to the implementation and easy to maintain. Additional details on output formatting and package lookup rules can be found in [modc-doc.md](modc-doc.md).

## modc test

The `modc test` command provides built-in test discovery and execution for %C packages. It automatically locates files ending in `*_test.mc` adjacent to package sources, compiles each test file as an independent executable with its own `main` entry point, and reports an overall `ok` or `FAIL` status for each suite. Test files are strictly excluded during standard `modc build` invocations, preventing test logic or extra `main` functions from accidentally linking into production binaries.

Test files import their parent package and assert expectations using standard assertion facilities:

```c
#include <assert.h>

int main() {
    assert(add(2, 3) == 5);
    assert_eq(add(1, 1), 2);
    return 0;
}
```

The test runner can be executed against the working directory or targeted to specific package paths:

```sh
modc test               # tests in current directory
modc test pkgdir/       # package directory
```

For compiler development, passing the `--corpus` flag wraps the compiler test suite execution (`make check`), though standard package testing requires only `modc test`. Full command options and execution details are documented in [`modc-test.md`](modc-test.md).

## Language Safety and Consistency Rules

The %C language enforces strict syntactic and semantic safety rules in user source files (`.mc`). Headers included via `#include` remain (mostly) standard C, while user code enforces the following practices:

- **Control Structure Clarity**: Compound statement braces `{}` are mandatory on all `if`, `else`, `for`, `while`, and `do` bodies (`else if` remains valid syntax).

- **Unified Field Access**: The dot operator (`.`) accesses members through both direct values and pointers (`p.x`). The arrow operator (`->`) is reserved for foreign C headers.

- **First-Class Struct Tags**: Struct, union, and enum tags act directly as type names in user code (`Point p = {0};`), eliminating the need for `struct` prefixes.

- **Empty Parameter Lists**: Write `foo()` for a function or method with no parameters. The legacy C spelling `foo(void)` is rejected in user source, while imported C headers retain standard C semantics.

- **Function Arity**: Function declarations and calls support at most 64 parameters or arguments. Larger interfaces are rejected instead of being truncated.

- **Control-Flow Limits**: A function may nest at most 32 loops and switches and 64 lexical blocks. A scope may register at most 64 deferred statements, and a switch may contain at most 128 cases. Exceeding these implementation limits is diagnosed.

- **Distinct Tagged Enums**: Integers and other enum types do not implicitly convert to a tagged enum. Comparisons and conditional-expression arms require the same enum type; arithmetic, bitwise, increment, and compound-assignment operators require an explicit integer cast. Enum values widen to integers for C interoperability, and an explicit cast converts an integer back. Anonymous enum constants remain ordinary integers and are the preferred form for flags and numeric constants.

- **Predictable Integer Types**: The `char` type is explicitly unsigned and 8-bit wide; user code writes `char`, never `signed char` or the redundant `unsigned char` spelling. Standard 64-bit integers use `int64_t`, while `long` is reserved for host ABI compatibility in foreign headers.

- **Single Declarations**: Multiple variable declarations on a single line (such as `int a, b;`) are prohibited, with an exception for tuple destructuring syntax.

- **Modernized Type System**: Variable-length arrays (VLAs), digraphs, trigraphs, comma operators, leading-zero octal literals, and bit-fields are disabled. Explicit `volatile`, `restrict`, and `register` are omitted from user source types. Pointee `const` / `const?` are allowed (see [const.md](const.md)); `inline` remains an auto-inline hint rather than a storage-class keyword.

- **Condition Assignment Safety**: Assignments within conditional expressions are rejected unless explicitly wrapped in parentheses (`if ((x = 0))` is allowed; `if (x = 0)` is a compile-time error).

- **Explicit Switch Execution**: Implicit fallthrough across `switch` cases results in a compile-time error. Cases must terminate with `break` or explicitly declare `fallthrough;`.

- **Defensive Local Initialization**: All local variables must be initialized at their point of declaration (`int x = 0;`). The compiler actively diagnoses uninitialized reads, `goto` jumps that bypass local declarations, control flow falling off non-`void` functions, signed/unsigned comparisons (except non-negative integer literals that fit the unsigned side), and unsequenced modifications (`i = i++`).

- **Strict Type Conversions**: Implicit narrowing conversions and conversions between unrelated pointer types require explicit casts.

- **No Unnecessary Casts**: A cast is a compile-time error when removing it preserves the value and %C would perform the same conversion at an assignment, initializer, return, or fixed-parameter boundary. Same-type arithmetic casts are rejected, as are narrow integer casts of literals that already fit (e.g. `(char)0x80` in comparisons — `char` is unsigned and promotes back to `int`). Casts that control arithmetic width, signedness, enum operations, overload selection, vararg ABI, pointer provenance, or intentional `(void)` discards remain valid.

## Guidelines & Best Practices

When writing or generating %C code, adhere to the following dialect conventions and workflow practices to ensure clean compilation and consistency across the codebase.

### Code Design and Error Patterns

- **Live Method Receivers**: Do not null-check `(T *r)` at the start of a method unless the API documents null/closed as valid (idempotent `close`/`free`, or fallible I/O). Callers keep receivers live; see [methods.md](methods.md).

- **Resource Management**: Prefer `defer` for resource cleanup rather than implementing traditional `goto err` failure ladders.

- **Return Values over Out-Parameters**: Return multi-value tuples such as `(T, bool)` alongside `auto` destructuring instead of passing mutable pointer out-parameters.

- **Manual Lifetime Discipline**: Deferred statements execute prior to block exit, so never return interior pointers referencing storage that a `defer` releases.

### Formatting and Declaration Structure

- **Explicit Initialization**: Every local variable declaration requires an explicit initializer (`T x = expr` or `auto x = expr`). Uninitialized declarations like `T x;` are rejected by the compiler. Local variables should be declared in proximity to their use.

- **Control Flow and Layout**: Write one declaration or statement per line, and always wrap control-flow bodies (`if`, `else`, `for`, `while`) in explicit braces `{}`.

- **Syntax Restrictions**: Maintain strict dialect boundaries. Do not introduce features or syntax borrowed from C++, Rust, or Go, such as templates, the `->` operator in user code, or multiple variable declarators on one line. Use `const` / `const?` where string and pointer contracts need them (see [const.md](const.md)).

### Package Dependencies and Verification

- **Imports and Headers**: Use the `import` directive for first-party %C packages, reserving `#include` directives and build flags for standard C library dependencies.

- **Compiler Diagnostics**: Execute `./modc check file.mc` prior to running `build`, resolving all compiler warnings and diagnostics before finalizing changes.
