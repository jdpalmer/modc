# Auto-Const Provenance Tracking

Auto-const tracks data provenance so string literals and read-only data remain protected without requiring explicit `const` type qualifiers in user source code.

## How Auto-Const Operates

In C, string literals reside in read-only memory, and writing to them causes a runtime segmentation fault. %C prevents these errors at compile time by attaching an internal `IMMUTABLE` flag to literal expressions (`Node.is_immutable`).

Attempting a direct write through a local variable or ranged view that aliases a string literal triggers a hard compiler error:

```c
char *name = "Flagstaff";
name[0] = 'X';              /* Error: store through immutable pointer */

char[..] view = "James";
view[0] = 'X';
```

Conversely, stack array initializations like `char buf[] = "hi"` create mutable local copies, so mutating `buf` is permitted.

## Tracking Mechanics Across Contexts

Within each %C function body, pointer locals and parameters are tracked using a lightweight state machine with three states: **Plain** (unknown mutability or address-escaped), **IMMUTABLE** (assigned from a literal, a `READONLY`-typed result, or another immutable pointer), and **MUTATED** (passed to a may-mutate parameter slot or used in a write operation).

Assigning from an immutable source sets the target pointer state to `IMMUTABLE`. Attempting a store through an `IMMUTABLE` pointer triggers a compile error and marks the variable as `MUTATED`. Passing an `IMMUTABLE` value to a parameter that may mutate its argument triggers a call-site error.

Taking the address of a pointer (`&p`) disables tracking, returning its state to `Plain`. Assigning an immutable source to a file-scope mutable pointer (`char *g = "..."`) is rejected. Similarly, returning an immutable source from a function declared with a mutable return type triggers a compiler diagnostic.

## C Header Integration

User `.mc` source code cannot spell the `const` keyword. However, when importing C headers, parameter and return types marked `const T *` are preserved internally as `Type.is_readonly`.

Functions like `strlen(const char *s)` accept immutable string literals, while functions like `strcpy(char *dest, ...)` reject them for mutable target slots. Functions returning `const char *` propagate the `IMMUTABLE` state to local variables upon assignment. Foreign parameters without `const` annotations, `void *` pointers, and variadic arguments are treated conservatively as may-mutate slots.

## Inferred READONLY Functions

For functions defined within the current compilation graph, pointer parameters and return types start with an optimistic `READONLY` classification. The compiler removes this classification if the function body performs any taints, such as writing through the pointer, storing it into untracked memory, or returning a non-readonly-safe expression. A fixpoint analysis algorithm propagates these summaries across callers and callees in the same compilation, enabling transitive read-only protection across function boundaries. `void *` parameters are never inferred as `READONLY`.

## Escape Hatches and Safety Limits

An explicit cast serves as an intentional escape hatch from provenance tracking:

```c
char *s = (char *)"hello"; /* Strips IMMUTABLE tag for this assignment */
```

Auto-const reliably catches string literal misuse, enforces read-only safety across single-compile function chains, and prevents invalid file-scope initializations. It does not guarantee safety in heap allocations, runtime global assignments, or untracked stores, such as stashing a pointer inside a struct field:

```c
struct Container {
    char *name;
};

struct Container c = { .name = "Alice" }; /* Auto-const does not track struct field contents */
```

Additionally, `void *` pointers and cross-translation-unit callees without header declarations default to may-mutate status.
