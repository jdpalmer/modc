# Methods and overloading

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

Method declarations follow the form `ret (T *receiver).name(params)`. Receivers must be pointers, and both the method definition and target type `T` must reside in the same package. Methods cannot be nested inside functions, combined with `overload`, or overloaded on arity. Method names exist in a per-type namespace, so calling `a.free()` never collides with libc `free(p)`. If a method name collides with a struct field, the field wins and the compiler emits a diagnostic.

When calling `expr.name()`, passing a pointer `T *` passes the pointer directly, while passing a mutable lvalue `T` automatically passes its address `&expr`. Calling a method on a null `T *` is a caller error (see below). Projecting or upcasting through a null outer pointer for an anonymous embed is a compile-time error (see [struct.md](struct.md)). When an `Outer` struct anonymously embeds an `Inner` struct, calling `outer.method()` resolves to `(Inner *).method` with automatic pointer address adjustments.

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
Idempotent teardown is expressed by an **already-closed object** (for example
`f.native == NULL` after `close`), not by a null receiver pointer.

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

Methods generate linker symbols using the `package_type_method` pattern. Package paths convert slashes to underscores (`ui/draw` becomes `ui_draw`), struct tags convert to lowercase (`Window` becomes `window`), and method names remain unchanged. The table below assumes the defining package is `ui`:

| **Source Method Declaration**        | **Generated Linker Symbol** |
| ------------------------------------ | --------------------------- |
| `void (Window *w).show()`            | `ui_window_show`            |
| `void (Window *w).set_title(char *)` | `ui_window_set_title`       |

As a design convention, use methods for operations on existing object handles and plain free functions for constructors or package initialization entry points (`window_open()`, `ui_init()`).

Event callbacks remain plain C functions like `void on_click(Button *self)`. Advanced features such as method values, ADL, cross-package extension methods, and operator overloading are not supported.
