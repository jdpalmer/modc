# Type Inference and Multi-Return Tuples

The `auto` keyword provides local type inference for initialized variables, while multi-return tuples offer a clean alternative to out-pointer APIs. These features deliver concise variable declarations and multi-value returns without introducing C++ template mechanics or complex sum types.

## Type Inference with auto

The `auto` keyword applies strictly to initialized local variables. The variable receives the exact type of its initializing expression, subject to standard C array-to-pointer and function-to-pointer decay rules.

```c
auto n = 3;           /* int */
auto p = malloc(8);   /* void * */
auto q = 1.0;         /* double */
auto s = "hi";        /* const char * (decay); see const.md */
auto cloud = ranged(particles, 100);  /* Particle[..] */
```

To maintain clear ABI contracts and avoid hidden state, `auto` is forbidden on function parameters, struct fields, file-scope variables, and public return types. Uninitialized declarations like `auto x;` are rejected. The traditional C storage-class meaning of `auto` is completely replaced by this type inference mechanism. Using `auto` is best when the initializer type is obvious, while explicit type names remain preferred when documenting formal API boundaries.

## Multi-Return Tuples

Functions can return multiple values as ordered positional tuples of the form `(T1, T2, ...)` without defining custom named structs.

```c
(int, bool) parse(int x) {
    if (x < 0) {
        return (0, false);
    }
    return (x, true);
}

int use() {
    auto (v, ok) = parse(5);
    if (!ok) {
        return -1;
    }
    return v;
}
```

Tuple operations follow a straightforward syntax across declarations, returns, and call sites:

| **Tuple Expression**      | **Syntactic Meaning**                                                |
| ------------------------- | -------------------------------------------------------------------- |
| **`(T1, T2) f(...)`**     | Declares a function returning a tuple.                               |
| **`return (a, b);`**      | Constructs and returns a tuple value.                                |
| **`auto (x, y) = f();`**  | Destructures a returned tuple into inferred local variables.         |
| **`(T1 x, T2 y) = f();`** | Destructures a returned tuple into explicitly typed local variables. |

Under the hood, tuple values lower to unnamed aggregate structs that conform to the target platform's struct-return ABI, matching the runtime performance of out-pointer parameters.

## Enforcement and Declarator Rules

Discarding a tuple return value at a call site is a compile-time error. If ignoring a returned tuple is deliberate, the expression must be explicitly cast to `(void)parse(1);`.

While multi-declarator statements like `int a, b;` are rejected in user source code to enforce single declarations, tuple destructuring statements such as `auto (a, b) = f();` are recognized as a single destructuring statement rather than a declarator list. Standard parameter lists like `int f(int a, int b)` remain unaffected.

### Compiler Inlining Behavior

User source files cannot use the `inline` keyword, which is reserved strictly for imported headers. Instead, the compiler automatically evaluates candidates for inlining at call sites. Every function definition retains an exported linker symbol, ensuring consistent linking.

The optimization heuristic targets small, simple function bodies while skipping functions that contain `defer` statements, `goto` labels, `switch` blocks, variadic arguments, or aggregate return types. Because inlining acts purely as an optimization heuristic rather than a language contract, missing an inline expansion does not alter program semantics.

See also the `auto` and multi-return tour in [quickstart.md](quickstart.md).
