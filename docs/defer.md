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

Deferred statements cannot contain `return`, `break`, `continue`, `goto`,
labels, `[[fallthrough]]`, or another `defer`. Cleanup code may call a function
that performs its own ordinary control flow.

## Evaluation Order on return

When executing a `return` statement, including multi-value returns, the compiler
handles the statement in three distinct steps:

1. Evaluates all return expressions into temporary variables.

2. Executes all pending `defer` tasks for exiting blocks in LIFO order.

3. Returns the saved temporary values to the caller.

```c
void write_log(char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    defer fclose(f);
    /* … write … */
    /* f is not returned; fclose runs on every exit */
}
```

Returning a pointer freed by a deferred cleanup is a compile-time error when the
cleanup is recognized as consuming: `free` / `fclose`, or a `%C` parameter marked
`[[releases]]` (including method receivers). Pool blocks schedule `p.free()`
like a defer; views minted via pool ops (`p.copy` / `p.append` / `p.cstr`, …) are
tied to that pool and cannot escape past block end / `p.free()` / `p.reset()`.
The `defer` keyword still only schedules cleanup order; `[[releases]]` / `[[region]]`
are what make the lifetime contract checkable. Owned values from `alloc` /
`[[provides]]` are discharged by a deferred release. The same release markers also
reject straight-line use-after-free and double-free of that local after an immediate
`free(p)` / `fclose(f)` / `[[releases]]` call (uses before a deferred cleanup still run
are allowed; the kill is at block exit).

## Practical application patterns

Block-level scoping makes `defer` useful for temporary state changes, lock management, and pool allocation within small scopes.

```c
mtx_lock(&m);
defer mtx_unlock(&m);

pool p {
	char[..] s = {0};
	p.copy(&s, "hi");
}
```

The feature remains simple by design. It does not introduce a generalized effect system, function-level scoping, or ownership type dependencies. Native headers do not parse `defer` keywords; cleanup routines belong strictly inside user `.mc` source files.

A `goto` may remain in its current lexical block or jump to an enclosing block.
Jumps into child or sibling blocks are rejected, as are forward jumps that
would bypass a `defer` registration in a scope that remains active.
