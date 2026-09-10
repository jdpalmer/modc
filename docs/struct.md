# Structs in %C

%C extends structs beyond vanilla C with unified tags, auto-dot field access, Plan 9-style anonymous embeds (with upcast and projection), and package-private fields. Methods on structs are covered in [methods.md](methods.md).

## Unified type namespace

Defining a struct automatically introduces its tag into the type namespace, removing the need for `typedef`:

```c
struct Point {
    int x;
    int y;
};

Point p = {0};           // Clean type usage in user code
struct Point q = {0};    // Allowed for header/C compatibility
```

A tag and another symbol may not share a name in the same scope in user `.mc`
sources. Headers may still use C's tag/ordinary homonyms (for example
`struct if_nameindex` beside `if_nameindex()`); see [interop.md](interop.md).
The same idea applies to `union` and `enum` tags.

## Field Access: Unified Dot Notation

In user translation units (`.mc` files), use the dot operator (`.`) for accessing fields on both values and pointers. The arrow operator (`->`) is restricted to header files ([interop.md](interop.md)).

```c
Point p = {0};
Point *ptr = &p;

p.x = 10;
ptr.x = 20;     // Replaces ptr->x
```

Each `.` operator peels at most **one level** of pointer indirection (or array decay) before performing field lookup. It does not perform arbitrary multi-level auto-dereferencing.

## Anonymous Embeds (Plan 9 Style)

Unnamed struct members contribute their fields directly to the enclosing struct.

```c
struct Transform {
    float x;
    float y;
    float z;
};

struct Entity {
    int id;
    Transform;     /* anonymous embed: x, y, z on Entity */
};

Entity e = {0};
e.x = 1.0f;
```

When a function expects a pointer to an embedded type, you can pass a pointer to the outer struct (provided the embed relationship is unambiguous). The compiler automatically offsets the pointer address:

```c
void draw_transform(Transform *t);

Entity e = {0};
draw_transform(&e);   /* &Entity → &embedded Transform */
```

When unambiguous, an outer struct value can be passed or assigned where the embedded type is expected by value. The compiler extracts the embedded subobject as a copy:

```c
void draw(Transform t);
Entity e = {0};
draw(e);

char[..] s = {0};
U8 u = {0};
s = u;                /* U8 → char[..] */
str_eq(p, "hi");      /* U8 * → char[..] when p is non-null */
```

Several key rules and constraints govern value projections and ranged operations on embedded structs. First, conversions are strictly one-way; while an outer struct can project down to an inner embedded type, reverse conversions from inner types to outer types are explicitly rejected. Additionally, developers should be mindful of performance overhead, as passing large embedded structs by value forces a full copy of the subobject at the call site. Finally, null safety is strictly enforced: attempting to pass a null outer pointer during projection or ranged operations triggers a compile-time error.

### Ranged embed surface

When the unique anonymous embed is a ranged type (`T[..]`), `len(x)` and
`x[lo .. hi]` work on the outer value or pointer like the embedded view
(promoted `.len`; subrange over the view, not a full struct copy). See
[arrays.md](arrays.md) and owning containers in [packages.md](packages.md).

### Ambiguity

Implicit upcasting and projection require a **unique** matching embed type. If a struct contains multiple anonymous embeds of the same type, or uses a named nested field, implicit conversions are disabled:

```c
struct Named {
    int id;
    Transform t;   /* named: no auto upcast from Named* to Transform* */
};
```

Nested unique embeds still upcast through the chain (`Outer` → `Mid` →
`Transform` when each step is a unique anonymous embed).

## Package-Private Pointer Fields

Prefixing a struct or union pointer field with `static` restricts its visibility to the defining package. Importers cannot read or write the field, making it useful for internal system handles (`GtkWidget *`, `HWND`) without exposing raw SDK dependencies.

```c
struct Window {
    static GtkWidget *widget;   /* package-private */
    int id;
};
```

While `static` hides a pointer field from importing packages, the pointer slot itself remains fully preserved within the struct's memory layout. Note that this feature is strictly limited to pointers; applying `static` to non-pointer fields is rejected by the compiler. For more details on field visibility, refer to [packages.md](packages.md).

## Method Interaction & Overloading

Combining anonymous embeds with methods enables mixin-style calls, allowing expressions like `entity.id()` when `Entity` embeds `Widget` and `Widget` implements `(Widget *).id`. Furthermore, pairing anonymous embeds with `overload` facilitates generic dispatch across component types. For a deeper dive into these mechanics, refer to [methods.md](methods.md).
