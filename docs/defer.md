# Scope-Based Cleanup with defer

The `defer` keyword provides block-scoped resource management. Unlike Go, where deferred statements run when the surrounding function returns, %C executes deferred tasks at the end of the enclosing block. You can register a single statement or a block of code, and the compiler executes these tasks in Last-In, First-Out (LIFO) order whenever control leaves that scope through normal execution, `return`, `break`, `continue`, or `goto`.

## Basic Usage and Syntax

The `defer` statement attaches directly to single statements or compound blocks.

```c
defer stmt;
defer { /* several statements */ }
```

Placing a `defer` immediately after resource allocation replaces error handling ladders made with `goto err` labels.

```c
char *p = malloc(500);
defer free(p);
/* … use p … */
/* free runs on every exit path from this block */
```

## Evaluation Order on return

When executing a `return` statement, including multi-value returns, the compiler
handles the statement in three distinct steps:

1. Evaluates all return expressions into temporary variables.

2. Executes all pending `defer` tasks for exiting blocks in LIFO order.

3. Returns the saved temporary values to the caller.

```c
(FILE *, bool) open_log(char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return (NULL, false);
    }
    defer fclose(f);
    /* … write … */
    return (f, true);  /* fclose still runs; caller must not use f after */
}
```

Returning a pointer to memory freed by a deferred statement remains a lifetime bug. The `defer` keyword controls execution order, not object lifetimes.

## Practical application patterns

Block-level scoping makes `defer` useful for temporary state changes, lock management, and arena allocation within small scopes.

```c
mtx_lock(&m);
defer mtx_unlock(&m);

Arena a = {0};
a.init();
defer a.free();
```

The feature remains simple by design. It does not introduce a generalized effect system, function-level scoping, or ownership type dependencies. Native headers do not parse `defer` keywords; cleanup routines belong strictly inside user `.mc` source files.
