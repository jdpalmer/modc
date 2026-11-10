# Memory and lifetimes in %C

Memory management in %C is built around three **kinds of allocation** (stack,
pool, and heap) plus **compile-time contracts** that check ownership, bounds,
and escapes on top of those. There is no garbage collector, no hidden runtime,
and no borrow checker.

## Rules of thumb

- **Stack** for local, fixed-size data tied to the current scope.
- **Pool** for short-lived scratch where per-object `free` would cost more than
  it is worth (tight loops, multi-step work).
- **Heap** for data that must outlive the creating scope, or grow dynamically.

| | Syntax | Backing | Freed | Escape |
| :--- | :--- | :--- | :--- | :--- |
| **Stack** | `T x = {0};`<br>`T arr[N];` | Current frame | Block exit | Must not outlive the frame ([Escapes](#escapes)) |
| **Pool** | `pool p { … }` | Lexical bump slab | Block end / `p.free()` / `p.reset()` | Views stay inside the block ([Escapes](#escapes)) |
| **Heap** | `alloc(T)` / `alloc(T, n)`<br>`cstr(s)` / `malloc` | `malloc` | `free` / `release` / `[[releases]]` / `defer` | [Ownership attributes](#ownership-attributes-provides--releases--retains) |

Grow a heap `T[..]` with `reserve` / `push` / `append`, and free it with
`release(&s)`.

---

## Stack allocations

Stack allocations live in the current call frame. Use them for fixed-size
scalars, arrays, and local aggregates that only need to last until the block
ends. They are cheap, need no explicit free, and are reclaimed automatically
when the frame exits.

```c
void process_buffer() {
	uint8_t buffer[256] = { 0 };
	Point p = { 10, 20 };

	do_work(buffer, sizeof(buffer), &p);
} // buffer and p reclaimed here
```

Frame-rooted addresses and `T[..]` views must not outlive the frame. The rules
and examples live under [Escapes](#escapes).

---

## Pool allocations

Pool allocations live in a lexical bump slab (`pool p { … }`). Use them for
short-lived scratch where many small allocations should die together. They are
cheap, need no per-object free, and are reclaimed when the block ends (or
earlier via `p.reset()` / `p.free()`).

```c
void format_request(const char[..] input) {
	pool p {
		char[..] buf = { 0 };
		p.copy(&buf, "req: ");
		p.append(&buf, input);
		puts(p.cstr(buf)); /* NUL view dies with the pool */
	}
}
```

Inside a `pool p { … }` block you can:

- **`p.alloc(T)`** / **`p.alloc(T, n)`**: allocate a `T*` or `T[..]` in the pool
- **`p.copy` / `p.append` / `p.push`**: grow and fill slice headers in the pool
- **`p.join` / `p.replace`**: build strings in the pool
- **`p.cstr(s)`**: get a NUL-terminated `char*` in the slab (no separate heap alloc)
- **`p.reset()`**: rewind the offset, keep capacity, and invalidate views from
  the pool
- **`p.free()`**: end the pool early (same invalidation as leaving the block)

### Reusing a pool in a loop

When work repeats (for example each frame of a loop), keep one pool and call
`reset` each iteration. That reuses the slab’s capacity instead of nesting a
new pool every time:

```c
void event_loop(int nframes) {
	pool frame_pool {
		for (int i = 0; i < nframes; i++) {
			char[..] msg = { 0 };
			frame_pool.copy(&msg, "frame");
			/* … use msg … */
			frame_pool.reset();
		}
	}
}
```

Views from the pool die with the block (or with `reset` / `p.free()`). Escape
rules and the copy-out pattern are under [Escapes](#escapes).

---

## Heap allocations

Heap allocations live on the global `malloc` heap. Use them for data that must
outlive the creating scope, or that needs to grow. Each owned block must be
freed, retained, or returned; pair allocation with `defer` so every exit path
releases.

- **`alloc(T)`** allocates one object and returns a `T*`
- **`alloc(T, n)`** allocates `n` elements and returns a `T[..]`
- **`cstr(s)`** on a dynamic `char[..]` allocates a heap NUL string marked
  `[[provides]]` (free it with `free` or `defer free`); string literals and
  `char[N]` buffers do not allocate
- **`malloc` / `calloc` / `realloc` / `strdup`** are tracked the same way as
  other owned heap values

```c
Widget* w = alloc(Widget);
uint8_t[..] buffer = alloc(uint8_t, 1024);
defer free(w);
defer release(&buffer);
```

`defer` covers early returns as well as the normal exit:

```c
bool process_file(const char[..] path) {
	uint8_t[..] data = alloc(uint8_t, 4096);
	if (ptr(data) == NULL) {
		return false;
	}
	defer release(&data);

	if (!read_bytes(path, data)) {
		return false;
	}
	return parse_contents(data);
}
```

### Growing a heap slice

Once you have a heap `T[..]`, grow it with `reserve`, `push`, and `append`.
Those resize the existing header in place. Free the slice with `release(&s)`.
Do not call `free` on the header itself:

```c
void build_dataset() {
	int[..] numbers = alloc(int, 0);
	defer release(&numbers);

	for (int i = 0; i < 10; i++) {
		push(&numbers, i * 10);
	}
	print_stats(numbers);
}
```

How ownership moves across calls is under
[Ownership attributes](#ownership-attributes-provides--releases--retains).

---

## Compile-time contracts

Stack, pool, and heap choose **where** a value lives. On top of that, the
checker tracks ownership across calls, rejects escapes of short-lived storage,
and keeps `T[..]` lengths intact until you take a raw pointer.

### Ownership attributes (`[[provides]]` / `[[releases]]` / `[[retains]]`)

Three attributes say how a value’s obligation moves at a call:

| Attribute | Marks | Means |
| :--- | :--- | :--- |
| **`[[provides]]`** | Function result | Caller receives ownership and must release or hand it off |
| **`[[releases]]`** | Parameter or method receiver | Callee takes ownership and ends the value’s lifetime |
| **`[[retains]]`** | Parameter | Callee keeps the value past the call; the caller’s obligation ends without freeing at that call |

Ordinary parameters (no attribute) only **borrow** for the duration of the
call. That is enough for `fopen`, `strlen`, and most C APIs. Use
`[[retains]]` only when the callee stores the pointer or slice somewhere that
outlives the call (a field, global, registry, or similar).

**`[[provides]]`** belongs on factories (`file_open`, `str_dup`, and similar).
Built-ins such as `alloc`, `malloc` / `calloc` / `realloc` / `strdup`, and
heap `cstr(s)` provide ownership even if the C declaration has no attribute.

**`[[releases]]`** belongs on cleanups (`str_free`, `f.close()`, and similar).
Built-ins such as `free`, `fclose`, and `release` consume ownership the same
way, without needing an attribute on the libc declaration.

On every path, an owned local must do one of these:

1. Pass it to a `[[releases]]` cleanup (or `free` / `release` / `fclose`),
   directly or via `defer`
2. Pass it to a `[[retains]]` parameter
3. Return it from a `[[provides]]` function

```c
struct Widget {
	int id;
};

struct Registry {
	Widget* w;
};

[[provides]] Widget* create_widget(int id) {
	Widget* w = alloc(Widget);
	w.id = id;
	return w;
}

void register_widget(Registry* r, [[retains]] Widget* w) {
	r.w = w;
}

void ([[releases]] Widget* w).destroy() {
	free(w);
}

void pipeline(Registry* r) {
	Widget* w = create_widget(101); /* provides: caller owns */
	register_widget(r, w);          /* retains: registry owns */
	r.w.destroy();                  /* releases: lifetime ends */
}
```

C headers are not analyzed. If a foreign API keeps a pointer (`putenv`,
registration callbacks), wrap it in a `%C` function with `[[retains]]` so the
checker can see the hand-off. See [The foreign C boundary](#the-foreign-c-boundary).

#### Leak, overwrite, and use-after-free

The checker rejects common ownership mistakes. Leaving an owned local without
releasing or handing it off is a leak. Overwriting an owned name without
releasing the old value first loses that block. Using or freeing a value after
it has already been released is a use-after-free or double-free:

```c
void leak_example() {
	int* z = alloc(int);
	/* Error: owned value 'z' never released or retained */
}

void overwrite_example() {
	int* z = alloc(int);
	z = alloc(int); /* Error: owned value 'z' overwritten without release */
	free(z);
}

void uaf_example() {
	char[..] s = alloc(char, 16); /* alloc(T, n) → T[..]; alloc(T) → T* */
	release(&s);
	s[0] = 'x';  /* Error: use of released value 's' */
	release(&s); /* Error: double-free */
}
```

#### Aliases

Copying a name (`q = p`), storing into a top-level field (`b.p = z`), or
assigning through a ternary (`q = c ? a : b`) joins those names into an alias
group. Releasing one name makes the others unusable as well. For a ternary,
both arms join the group, so releasing either arm (or `q`) poisons all of them:

```c
void alias_example() {
	int* p = alloc(int);
	int* q = p;
	free(p);
	*q = 10; /* Error: 'q' aliased to released 'p' */
}

typedef struct {
	char* p;
} Box;

void field_alias_example() {
	Box b = { 0 };
	char* z = malloc(16);
	b.p = z;
	free(z);
	*b.p = 'x'; /* Error: field aliased to released 'z' */
}
```

### Escapes

Values whose storage will die must not outlive that storage: no returning
them, no storing them in globals or statics, and no writing them through
other pointers that outlive the block.

**Stack.** Returning a frame-rooted address or `T[..]` view is rejected:

```c
int* bad_get_count() {
	int count = 42;
	return &count; // Error: address of local 'count' escapes this function
}

int[..] bad_get_slice() {
	int arr[4] = { 1, 2, 3, 4 };
	return arr[0..4]; // Error: view of local array 'arr' escapes this function
}
```

Passing `&local` to an ordinary (borrow) parameter is fine. Passing it to a
`[[retains]]` parameter is not, because that callee may keep the pointer past
the call:

```c
struct PointRegistry {
	Point* p;
};

void register_point(PointRegistry* r, [[retains]] Point* p) {
	r.p = p;
}

void bad_register(PointRegistry* r) {
	Point pt = { 0 };
	register_point(r, &pt); /* Error: stack address to [[retains]] parameter */
}
```

**Pool.** Views allocated from a pool must not leave the block. Copy out to
the heap when the result must survive:

```c
import "str";

[[provides]] (bool, char[..]) process_name(const char[..] raw_input) {
	pool work_pool {
		char[..] temp = { 0 };
		work_pool.copy(&temp, raw_input);

		/* return (true, temp);  // Error: pool view escapes */
		return str_dup(temp); /* heap copy; caller releases with str_free */
	}
}
```

After `p.reset()` or `p.free()`, using a view that came from that pool is also
an error.

**Defer.** A `defer free(p)`, `defer release(&s)`, or deferred `[[releases]]`
call marks the value and its aliases dead at block exit:

```c
char* bad_defer() {
	char* p = alloc(char);
	defer free(p);
	return p; /* Error: invalidated by defer */
}
```

An opaque `defer { … }` block that does not contain a recognized release
(`free`, `fclose`, a `[[releases]]` call, or ending a pool) is only weakly
checked. Prefer a single releasing call, or wrap the cleanup in a
`[[releases]]` API.

When a `%C` package function stores a parameter somewhere that outlives the
call, the compiler infers `[[retains]]` on that parameter and exports it.
In-place write-back through the same borrow (`c.p = c.p + 1`, `a[i] = a[j]`)
does not count as retaining. How `[[retains]]` fits the ownership model is
under
[Ownership attributes](#ownership-attributes-provides--releases--retains).

See [quickstart.md](quickstart.md) and [defer.md](defer.md).

### Slices and the C edge (`T[..]`, `ptr()`, `cstr()`)

A `T[..]` is a pointer plus a length. Indexing and slicing use that length.
When you need a bare C pointer, ask for one explicitly with `ptr` or `cstr`.

```c
int[..] s = alloc(int, 10);
defer release(&s);
```

- **Raw pointers:** Bare `p[i]` and `*(p + i)` are errors in `.mc` files.
  Attach a length with `ranged(p, n)` (see [arrays.md](arrays.md)), or keep
  the data in a `T[N]` or `T[..]`.
- **Calling C:** Use `ptr(s)` when the API wants a pointer and a separate
  length. Use `cstr(s)` or `p.cstr(s)` when it wants a NUL-terminated string.
- **`p.cstr(s)`** returns a `char*` in the pool slab; it dies with the pool.
- **`cstr(s)`** on a dynamic `char[..]` allocates a heap NUL string marked
  `[[provides]]`. Free it with `free`. String literals and `char[N]` buffers
  do not allocate; they are assumed to already be NUL-terminated.

```c
write(fd, ptr(s), len(s));
memcpy(ptr(dst), ptr(src), len(src) * sizeof(dst[0]));

void open_file(const char[..] path) {
	/* fopen(path, "r");      // Error: char[..] is not const char* */
	/* fopen(ptr(path), "r"); // Wrong tool: fopen wants NUL, not pointer+length */

	pool p {
		FILE* f = fopen(p.cstr(path), "r");
		if (f != NULL) {
			defer fclose(f);
		}
	}
}
```

You still write ordinary calls such as `strlen(s)` or `printf("%s", slice)`;
the compiler rewrites them to use the length: `strlen` becomes an O(1) `len`
call, `strcmp` compares by length, and `printf("%s", slice)` becomes `%.*s`
with `(int)len(slice)` and `ptr(slice)`.

See [arrays.md](arrays.md) and [interop.md](interop.md).

### Runtime checks (bounds and null receivers)

Compile-time contracts catch ownership and escape mistakes. Two runtime traps
catch indexing and method-call bugs unless you pass `--release`:

- **Bounds:** `s[i]` and `s[lo..hi]` on `T[..]` / `T[N]` check against the
  length and trap on overflow (`__modc_bounds_fail`).
- **Null receivers:** A method call on `(T* r)` traps if `r` is null at entry
  (`__modc_null_fail`). Callers must pass a live receiver. Methods must not
  null-check `r` at the start of the body; express “already closed” with an
  object field instead.

```c
void (File* f).close() {
	/* if (f == NULL) return;  // Error: null test on method receiver */
	if (f.native == NULL) {
		return; /* already closed / never opened */
	}
	/* … */
}
```

See [arrays.md](arrays.md) and [methods.md](methods.md).

---

## The foreign C boundary

Safety checking stops at the C ABI:

- **`.mc` code** is checked for escapes and ownership.
- **Host C / libc** declarations are trusted; there is no retain, release, or
  `[[provides]]` analysis of foreign bodies.
- **Raw `T*`** after `ptr()` or casts leaves the checked world.
- **Calls through function pointers** do not track retain or release.

Most C APIs only borrow for the duration of the call (`fopen`, `strlen`). For
those, pass a short-lived C string from a pool (`p.cstr(s)`), or allocate a
heap `cstr` and `free` it after the call returns.

APIs that keep the pointer need a `%C` wrapper with `[[retains]]` (see
[Ownership attributes](#ownership-attributes-provides--releases--retains)):

```c
void env_set([[retains]] char* z) {
	putenv(z); /* libc is unchecked; [[retains]] discharges ownership here */
}

void install_env(const char[..] env_str) {
	char* z = cstr(env_str);
	env_set(z); /* retained; do not free while still in the environment */
}
```
