# Methods and overloading

Short rule page for agents: `modc doc methods` (canonical text in
[`src/langdoc.c`](../src/langdoc.c)).

%C introduces the `overload` keyword for type-generic free functions and receiver syntax to attach methods to typed handles. Both features integrate with Plan 9 style anonymous embeds. This design provides clean object-oriented syntax without introducing a heavy object system, vtables, or hidden `this` pointers.

## Function Overloading

Function overloading requires an explicit `overload` keyword. Resolution follows Clang style rules based on parameter types and arity, never on return type alone. The compiler prefers exact matches, followed by standard C type promotions, while ambiguous calls trigger compile-time errors.

```c
overload int max(int a, int b) { return a > b ? a : b; }
overload float max(float a, float b) { return a > b ? a : b; }

int a = max(1, 2);
float f = max(1.0f, 2.0f);
```

This mechanism keeps C semantics rather than C++ complexity. There are no templates, default arguments, operator overloading, or Argument-Dependent Lookup (ADL). Only functions marked with `overload` are name-mangled. Unmarked free functions keep their source names as linker symbols, ensuring stable C entry points and standard library functions like `sin` remain unmangled. The `overload` keyword also defines custom iterator hooks for range-for loops.

## Methods and Receiver Syntax

Method definitions use explicit receiver parameters rather than an implicit `this` pointer, while call sites use standard dot notation (`w.show()`).

```c
void (Window *w).show() {
    ui_backend().window_show(w);
}

void (Window *w).set_title(char *title) {
    w.title = title;   /* auto-dot through pointer */
}

Window *w = window_open(320, 160, "Counter");
w.show();
w.set_title("Tasks");
```

Method declarations follow the form `ret (receiver).name(params)`. Receivers may be:

- a pointer to a named struct or union: `(Window *w).show()`
- a ranged header by value: `(const char[..] s).eq(const char[..] other)`
- a pointer to a ranged header: `(char[..]* s).…` for mutators that need to rebind the header

**Not allowed:** by-value receivers on named structs/unions (`(Window w).show`). Object handles stay pointers; `T[..]` by value is the exception because the header is a small value type. See [stability.md](stability.md).

### Extension methods

Any package that can see the receiver type may define methods on it (including outside a named `T`'s origin package). Callers must `import` the defining package to use those methods — methods are not ambient on `T` worldwide.

Duplicate `(receiver shape, name, parameter types)` is an error (redefinition at define time within a unit; linker name collision across objects). Do not rely on “best effort” or silent preference between two packages’ identical signatures.

Mangling: named-struct methods use **T’s origin package** in the linker prefix; `T[..]` / `T[..]*` methods use the **defining** package and a structural receiver encoding (below).

Multiple methods may share the same receiver shape and name when their parameter lists differ; call-site resolution matches free-function overloads (arity and types, never return type alone). Methods cannot be nested inside functions or marked with the `overload` keyword (receiver methods are always overloadable by signature). Method names exist in a per-receiver-shape namespace, so calling `a.free()` never collides with libc `free(p)`. If a method name collides with a field (including synthetic `ptr` / `len` / `cap` on `T[..]`), the compiler rejects the method at define time.

When calling `expr.name()` on a named aggregate, passing a pointer `T *` passes the pointer directly, while passing a mutable lvalue `T` automatically passes its address `&expr`. By-value `T[..]` methods take the header as a value (literals and temporaries are fine). Calling a method on a null `T *` / `T[..]*` is a caller error (see below). Projecting or upcasting through a null outer pointer for an anonymous embed is a compile-time error (see [struct.md](struct.md)). When an `Outer` struct anonymously embeds an `Inner` struct, calling `outer.method()` resolves to `(Inner *).method` with automatic pointer address adjustments.

### Receivers are live, non-null handles

A method receiver `T *r` is an ordinary pointer parameter with two extra
rules in the method body (compile errors):

1. **No null tests** on the receiver — `r == NULL`, `r != NULL`, `!r`, bare
   `if (r)`, and other forms that ask whether `r` is null. There is **no
   opt-out** for `free` / `close`.
2. **No rebind** — `r = …` (and `++r` / `--r`). Mutating through `r`
   (`r.field = …`) is fine. (Same idea as `T * const r`.)

Callers must not invoke a method on a null `T *`. That is a caller bug, same
as passing null into a C API that does not document `free(3)`-style no-ops.
By default the compiler inserts a null trap at method entry
(`__modc_null_fail`); `--release` omits it. Idempotent teardown is expressed by
an **already-closed object** (for example `f.native == NULL` after `close`),
not by a null receiver pointer.

Everyday mutators (`init`, `show`, `append`, …) and teardown (`free`,
`close`) all assume `r` is live and use `r.field` directly. Do not write
defensive `if (r == NULL) return;` at the start of a method.

```c
void (File *f).close() {
	/* if (f == NULL) return;  — error */
	if (f.native == NULL) {
		return;   /* already closed / never opened — ok */
	}
	/* … */
	f.native = NULL;
}
```

## Linker Symbol Mangling

Named-struct methods use `package_type_method__params`, where **package** is the receiver type’s origin package. Package paths convert slashes to underscores (`ui/draw` → `ui_draw`), struct tags convert to lowercase (`Window` → `window`), and method names remain unchanged. The `__params` suffix encodes post-receiver parameter types (same encoding as free `overload` mangling); a zero-arity method uses `__v`.

`T[..]` / `T[..]*` methods use the **defining** package and a structural receiver encoding (`R` / `RK` / `RY`, with a leading `p` for pointer receivers), for example `str_RKc_eq__RKc` for `(const char[..] a).eq(const char[..] b)` in package `str`.

| **Source Method Declaration**        | **Generated Linker Symbol**   |
| ------------------------------------ | ----------------------------- |
| `void (Window *w).show()` (pkg `ui`) | `ui_window_show__v`           |
| `void (Window *w).set_title(char *)` | `ui_window_set_title__pc`     |
| `void (Window *w).set_title(int)`    | `ui_window_set_title__i`      |
| `bool (const char[..] a).eq(…)` (`str`) | `str_RKc_eq__RKc`          |

As a design convention, use methods for operations on existing object handles and plain free functions for constructors or package initialization entry points (`window_open()`, `ui_init()`, `str_dup()`).

Event callbacks remain plain C functions like `void on_click(Button *self)`. Advanced features such as method values, ADL, and operator overloading are not supported.
