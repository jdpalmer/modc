/*
 * Types, conversions, constant folding, and type_expr.
 *
 * Compilation pipeline: lex → pp → parse → [type check] → emit → QBE
 * (type_expr also runs during parse; type_check_unit lives in check.c.)
 *
 * Owns the Compiler type pool (type_list). Symbols are in symbol.c;
 * post-parse analyses are in check.c.
 */
#include "ast.h"
#include <ctype.h>

// Allocate a primitive Type with fixed size/align/signedness.
static Type*
mkprim(Compiler* c, int kind, int size, int align, int is_unsigned) {
	Type* t;

	t = type_new(c, kind);
	t->size = size;
	t->align = align;
	t->is_unsigned = is_unsigned;
	return t;
}

// Wire up the standard primitive types on Compiler (void, integers, floats, bool, void*).
void type_init(Compiler* c) {
	c->type_void = mkprim(c, TyVoid, 0, 1, 0);
	c->type_char = mkprim(c, TyChar, 1, 1, 1); /* %C: char is unsigned 8-bit */
	c->type_uchar = mkprim(c, TyUChar, 1, 1, 1);
	c->type_short = mkprim(c, TyShort, 2, 2, 0);
	c->type_ushort = mkprim(c, TyUShort, 2, 2, 1);
	c->type_int = mkprim(c, TyInt, 4, 4, 0);
	c->type_uint = mkprim(c, TyUInt, 4, 4, 1);
	/* Host ABI long (LP64: 8; LLP64/Windows: 4). Headers only in user dialect. */
	c->type_long = mkprim(c, TyLong, (int)sizeof(long), (int)sizeof(long), 0);
	c->type_ulong = mkprim(c, TyULong, (int)sizeof(unsigned long),
			    (int)sizeof(unsigned long), 1);
	/* Fixed 64-bit for int64_t and dialect literal suffixes l/ul. */
	c->type_llong = mkprim(c, TyLLong, 8, 8, 0);
	c->type_ullong = mkprim(c, TyULLong, 8, 8, 1);
	c->type_float = mkprim(c, TyFloat, 4, 4, 0);
	c->type_double = mkprim(c, TyDouble, 8, 8, 0);
	c->type_bool = mkprim(c, TyBool, 1, 1, 0);
	c->type_void_ptr = type_ptr(c, c->type_void);
}

// Allocate a Type on the Compiler pool; nodes are never freed so Type* stays stable.
Type* type_new(Compiler* c, int kind) {
	Type* t;

	t = xmalloc(sizeof(*t));
	t->kind = kind;
	t->len = -1;
	t->emit_id = 0;
	t->next = c->type_list;
	c->type_list = t;
	return t;
}

// Fresh pointer type with host pointer size; equality is structural via type_eq.
Type* type_ptr(Compiler* c, Type* base) {
	Type* t;

	t = type_new(c, TyPtr);
	t->base = base;
	t->size = 8;
	t->align = 8;
	t->complete = 1;
	t->laid_out = 1;
	return t;
}

// Fixed-length array; size is known when len and the element layout are complete.
Type* type_array(Compiler* c, Type* base, int64_t len) {
	Type* t;

	t = type_new(c, TyArray);
	t->base = base;
	t->len = len;
	if (len >= 0 && base && base->size > 0) {
		t->size = (int)(len * base->size);
		t->align = base->align > 0 ? base->align : 1;
		t->complete = 1;
		t->laid_out = 1;
	}
	return t;
}

// Function type with return type, parameter list, and optional varargs.
Type* type_func(Compiler* c, Type* ret, Type** params, int n, int va) {
	Type* t;
	int i;

	t = type_new(c, TyFunc);
	t->base = ret;
	t->params_len = n;
	t->is_varargs = va;
	t->size = 8;
	t->align = 8;
	t->complete = 1;
	t->laid_out = 1;
	if (n > 0) {
		t->params = xmalloc(n * sizeof(Type*));
		t->param_names = xmalloc(n * sizeof(char*));
		t->param_array = xmalloc(n);
		t->param_fixed_len = xmalloc(n * sizeof(int64_t));
		for (i = 0; i < n; i++) {
			t->params[i] = params[i];
			t->param_names[i] = NULL;
			t->param_array[i] = 0;
			t->param_fixed_len[i] = -1;
		}
	}
	return t;
}

// Named or anonymous struct/union/enum; registers a non-empty tag in the symbol table.
// storage StStatic marks the tag (and type) package-private.
Type* type_struct(Compiler* c, int kind, char* tag, Span sp, int storage) {
	Type* t;
	Symbol* s;

	if (tag && tag[0]) {
		s = symbol_lookup_tag(c, tag);
		if (s && s->type && (s->type->kind == TyStruct || s->type->kind == TyUnion || s->type->kind == TyEnum)) {
			if (storage == StStatic) {
				s->storage = StStatic;
				s->type->pkg_private = 1;
			}
			return s->type;
		}
	}
	t = type_new(c, kind);
	t->tag = tag ? xstrdup(tag) : NULL;
	t->align = 1;
	if (storage == StStatic)
		t->pkg_private = 1;
	if (tag && tag[0])
		symbol_define(c, tag, SkTag, t, storage == StStatic ? StStatic : StNone, sp);
	return t;
}

// Interned {ptr,len,cap} struct for T[..]; one ranged Type per element type.
// len is the initialized window; cap is addressable elements from ptr (len <= cap).
// Views (literals, subranges, T[N]→T[..]) set len == cap. Spare room is not inherited.
Type* type_ranged(Compiler* c, Type* elem) {
	Type* t;
	Field *ptr, *len, *cap;
	static int next;

	if (elem == NULL)
		elem = c->type_void;
	for (t = c->type_list; t; t = t->next)
		if (t->is_ranged && type_eq(t->base, elem))
			return t;
	t = type_new(c, TyStruct);
	t->is_ranged = 1;
	t->base = elem;
	t->tag = xmalloc(32);
	snprintf(t->tag, 32, "__Ranged%d", next++);
	ptr = xmalloc(sizeof(*ptr));
	ptr->name = xstrdup("ptr");
	ptr->type = type_ptr(c, elem);
	len = xmalloc(sizeof(*len));
	len->name = xstrdup("len");
	len->type = c->type_ullong; /* size_t */
	cap = xmalloc(sizeof(*cap));
	cap->name = xstrdup("cap");
	cap->type = c->type_ullong; /* size_t */
	ptr->next = len;
	len->next = cap;
	t->fields = ptr;
	type_layout(c, t);
	return t;
}

// True when t is a tuple with exactly these element types in order.
static int
tuple_matches(Type* t, Type** elts, int n) {
	Field* f;
	int i;

	if (t == NULL || !t->is_tuple)
		return 0;
	for (f = t->fields, i = 0; i < n; i++, f = f ? f->next : NULL) {
		if (f == NULL || !type_eq(f->type, elts[i]))
			return 0;
	}
	return f == NULL;
}

// Interned multi-return struct so tuple types share one node for diagnostics.
Type* type_tuple(Compiler* c, Type** elts, int n) {
	Type* t;
	Field *f, **tail;
	char fname[16];
	int i;
	static int nextid;

	if (n <= 0)
		return type_struct(c, TyStruct, NULL, (Span){0}, StNone);
	for (t = c->type_list; t; t = t->next) {
		if (tuple_matches(t, elts, n))
			return t;
	}
	t = type_new(c, TyStruct);
	t->is_tuple = 1;
	t->tag = xmalloc(32);
	snprintf(t->tag, 32, "__Tuple%d", nextid++);
	tail = &t->fields;
	for (i = 0; i < n; i++) {
		f = xmalloc(sizeof(*f));
		snprintf(fname, sizeof(fname), "f%d", i);
		f->name = xstrdup(fname);
		f->type = elts[i];
		*tail = f;
		tail = &f->next;
	}
	type_layout(c, t);
	return t;
}

// Round n up to the next multiple of alignment a.
static int
align_up(int n, int a) {
	if (a <= 1)
		return n;
	return (n + a - 1) / a * a;
}

// True when member m can participate in aggregate layout.
static int
member_layout_ready(Type* m) {
	if (m == NULL)
		return 0;
	while (m->kind == TyArray) {
		if (m->len < 0 || m->base == NULL)
			return 0;
		m = m->base;
	}
	if (m->kind == TyStruct || m->kind == TyUnion || m->kind == TyEnum)
		return m->complete;
	return 1;
}

// True when t's layout can be computed (no incomplete aggregate members).
static int
type_layout_ready(Type* t) {
	Field* f;

	if (t == NULL)
		return 0;
	if (t->kind == TyArray)
		return t->len >= 0 && member_layout_ready(t->base);
	if (t->kind != TyStruct && t->kind != TyUnion)
		return 1;
	/* Forward tags have no fields yet; ranged / bodies-in-progress have fields. */
	if (!t->complete && t->fields == NULL)
		return 0;
	for (f = t->fields; f; f = f->next) {
		if (!member_layout_ready(f->type))
			return 0;
	}
	return 1;
}

// Compute struct/union/array size, alignment, and field offsets once.
// Incomplete field types defer layout (cross-file embeds); call type_layout_pending later.
void type_layout(Compiler* c, Type* t) {
	Field* f;
	int off, al, maxal, sz;

	if (t == NULL || t->laid_out)
		return;
	if (t->kind == TyArray) {
		if (!type_layout_ready(t))
			return;
		type_layout(c, t->base);
		if (t->base == NULL || !t->base->laid_out)
			return;
		t->size = (int)(t->len * type_size(c, t->base));
		t->align = type_align(c, t->base);
		t->complete = 1;
		t->laid_out = 1;
		return;
	}
	if (t->kind != TyStruct && t->kind != TyUnion)
		return;
	if (!type_layout_ready(t))
		return;
	off = 0;
	maxal = 1;
	for (f = t->fields; f; f = f->next) {
		type_layout(c, f->type);
		if (f->type && !f->type->laid_out &&
		    (f->type->kind == TyStruct || f->type->kind == TyUnion || f->type->kind == TyArray))
			return;
		al = type_align(c, f->type);
		sz = type_size(c, f->type);
		if (al > maxal)
			maxal = al;
		if (t->kind == TyUnion) {
			f->offset = 0;
			if (sz > off)
				off = sz;
		} else {
			off = align_up(off, al);
			f->offset = off;
			off += sz;
		}
	}
	if (maxal < 1)
		maxal = 1;
	t->align = maxal;
	t->size = align_up(off, maxal);
	if (t->size == 0)
		t->size = 1;
	t->complete = 1;
	t->laid_out = 1;
	(void)c;
}

// After all package type bodies are known, finish deferred aggregate layouts.
void type_layout_pending(Compiler* c) {
	Type* t;
	int progress, guard;

	for (guard = 0; guard < 64; guard++) {
		progress = 0;
		for (t = c->type_list; t; t = t->next) {
			if (t->laid_out)
				continue;
			if (t->kind != TyStruct && t->kind != TyUnion && t->kind != TyArray)
				continue;
			type_layout(c, t);
			if (t->laid_out)
				progress = 1;
		}
		if (!progress)
			break;
	}
}

// Byte size of t, running layout first when the type is still incomplete.
int type_size(Compiler* c, Type* t) {
	if (t == NULL)
		return 0;
	if (!t->laid_out)
		type_layout(c, t);
	if (t->kind == TyArray && t->size == 0 && t->len >= 0 && t->base)
		return (int)(t->len * type_size(c, t->base));
	return t->size;
}

// Alignment of t, running layout first when needed.
int type_align(Compiler* c, Type* t) {
	if (t == NULL)
		return 1;
	if (!t->laid_out)
		type_layout(c, t);
	return t->align > 0 ? t->align : 1;
}

// Integer kinds including bool and enum.
int is_int(Type* t) {
	if (t == NULL)
		return 0;
	switch (t->kind) {
	case TyChar:
	case TyUChar:
	case TyShort:
	case TyUShort:
	case TyInt:
	case TyUInt:
	case TyLong:
	case TyULong:
	case TyLLong:
	case TyULLong:
	case TyBool:
	case TyEnum:
		return 1;
	default:
		return 0;
	}
}

// True if t is an integer or floating-point type.
int is_arith(Type* t) {
	return is_int(t) || (t && (t->kind == TyFloat || t->kind == TyDouble));
}

// Scalar in the C sense: arithmetic or pointer.
int is_scalar(Type* t) {
	return is_arith(t) || is_ptr(t);
}

// True if t is a pointer type.
int is_ptr(Type* t) {
	return t && t->kind == TyPtr;
}

// True if t is a function type.
int is_func(Type* t) {
	return t && t->kind == TyFunc;
}

// True if t is an array type.
int is_array(Type* t) {
	return t && t->kind == TyArray;
}

// True if t is a struct or union.
int is_aggr(Type* t) {
	return t && (t->kind == TyStruct || t->kind == TyUnion);
}

// Interned {ptr,len} ranged-array struct.
int is_ranged(Type* t) {
	return t && t->is_ranged;
}

// Interned multi-return tuple struct.
int is_tuple(Type* t) {
	return t && t->is_tuple;
}

// Signed integer kinds; excludes char and all unsigned variants.
int is_signed_int(Type* t) {
	if (t == NULL || t->is_unsigned)
		return 0;
	switch (t->kind) {
	case TyShort:
	case TyInt:
	case TyLong:
	case TyLLong:
	case TyBool:
	case TyEnum:
		return 1;
	default:
		return 0;
	}
}

// Array→pointer and function→pointer decay; other types are unchanged.
Type* decay(Compiler* c, Type* t) {
	if (t == NULL)
		return t;
	if (t->kind == TyArray)
		return type_ptr(c, t->base);
	if (t->kind == TyFunc)
		return type_ptr(c, t);
	return t;
}

// Integer promotions and array/func decay used before usual_arith and comparisons.
Type* promote(Compiler* c, Type* t) {
	if (t == NULL)
		return c->type_int;
	if (t->kind == TyFloat || t->kind == TyDouble)
		return t;
	if (t->kind == TyPtr || t->kind == TyArray || t->kind == TyFunc)
		return decay(c, t);
	if (t->kind == TyLLong || t->kind == TyULLong)
		return t;
	if (t->kind == TyLong || t->kind == TyULong)
		return t;
	if (t->kind == TyUInt && t->size >= 4)
		return t;
	return c->type_int;
}

// Common type for binary arithmetic after integer promotions on both operands.
Type* usual_arith(Compiler* c, Type* a, Type* b) {
	a = promote(c, a);
	b = promote(c, b);
	if (a->kind == TyDouble || b->kind == TyDouble)
		return c->type_double;
	if (a->kind == TyFloat || b->kind == TyFloat)
		return c->type_float;
	if (a->kind == TyULLong || b->kind == TyULLong)
		return c->type_ullong;
	if (a->kind == TyLLong || b->kind == TyLLong)
		return c->type_llong;
	if (a->kind == TyULong || b->kind == TyULong)
		return c->type_ulong;
	if (a->kind == TyLong || b->kind == TyLong)
		return c->type_long;
	if (a->kind == TyUInt || b->kind == TyUInt)
		return c->type_uint;
	return c->type_int;
}

// Structural type equality; aggregates compare by identity, char/uchar equated.
int type_eq(Type* a, Type* b) {
	int i;

	if (a == b)
		return 1;
	if (a == NULL || b == NULL)
		return 0;
	/* char and unsigned char are the same type (%C char is unsigned) */
	if ((a->kind == TyChar || a->kind == TyUChar) && (b->kind == TyChar || b->kind == TyUChar))
		return 1;
	if (a->kind != b->kind)
		return 0;
	switch (a->kind) {
	case TyPtr:
		return type_eq(a->base, b->base);
	case TyArray:
		return type_eq(a->base, b->base) && (a->len < 0 || b->len < 0 || a->len == b->len);
	case TyFunc:
		if (!type_eq(a->base, b->base) || a->params_len != b->params_len || a->is_varargs != b->is_varargs)
			return 0;
		for (i = 0; i < a->params_len; i++)
			if (!type_eq(a->params[i], b->params[i]))
				return 0;
		return 1;
	case TyStruct:
	case TyUnion:
	case TyEnum:
		return a == b;
	default:
		return 1;
	}
}

// Loose assignment compatibility (ints, ptrs, arith, same aggregate identity).
int type_compat(Type* a, Type* b) {
	if (type_eq(a, b))
		return 1;
	if (a == NULL || b == NULL)
		return 0;
	if (is_int(a) && is_int(b))
		return 1;
	if (is_ptr(a) && is_ptr(b))
		return 1;
	if (is_ptr(a) && is_int(b))
		return 1;
	if (is_int(a) && is_ptr(b))
		return 1;
	if (is_arith(a) && is_arith(b))
		return 1;
	if (is_aggr(a) && is_aggr(b) && a == b)
		return 1;
	return 0;
}

// True when sp is in user code (unit files or main input), not header-only.
int user_source(Compiler* c, Span sp) {
	int i;

	if (c == NULL || sp.file == NULL)
		return 1;
	if (c->unit_files_len > 0) {
		for (i = 0; i < c->unit_files_len; i++)
			if (c->unit_files[i] && strcmp(sp.file, c->unit_files[i]) == 0)
				return 1;
		return 0;
	}
	if (c->infile == NULL)
		return 1;
	return strcmp(sp.file, c->infile) == 0;
}

Node* type_expr(Compiler* c, Node* n);

// Walk anonymous aggregate embeds; count inner matches and record the sole offset.
static void
count_anon_embed(Type* outer, Type* want, int base, int* count, int* off) {
	Field* f;

	if (outer == NULL || want == NULL || !is_aggr(outer))
		return;
	for (f = outer->fields; f; f = f->next) {
		if (f->name && f->name[0] != 0)
			continue;
		if (!is_aggr(f->type))
			continue;
		if (type_eq(f->type, want)) {
			(*count)++;
			*off = base + f->offset;
		}
		count_anon_embed(f->type, want, base + f->offset, count, off);
	}
}

// Locate a uniquely embedded inner type; 0 none, 1 ok, 2 ambiguous.
int anon_embed_offset(Type* outer, Type* inner, int* off) {
	int count, o;

	count = 0;
	o = 0;
	count_anon_embed(outer, inner, 0, &count, &o);
	if (count == 1) {
		if (off)
			*off = o;
		return 1;
	}
	if (count > 1)
		return 2;
	return 0;
}

// Walk anonymous embeds looking for ranged-array fields.
static void
count_anon_embed_ranged(Type* outer, Type** found, int base, int* count, int* off) {
	Field* f;

	if (outer == NULL || !is_aggr(outer))
		return;
	for (f = outer->fields; f; f = f->next) {
		if (f->name && f->name[0] != 0)
			continue;
		if (!is_aggr(f->type))
			continue;
		if (is_ranged(f->type)) {
			(*count)++;
			if (found)
				*found = f->type;
			if (off)
				*off = base + f->offset;
		}
		count_anon_embed_ranged(f->type, found, base + f->offset, count, off);
	}
}

// Find the sole anonymous ranged embed; same 0/1/2 tri-state as anon_embed_offset.
int anon_embed_unique_ranged(Type* outer, Type** ranged, int* off) {
	int count, o;
	Type* r;

	count = 0;
	o = 0;
	r = NULL;
	count_anon_embed_ranged(outer, &r, 0, &count, &o);
	if (count == 1) {
		if (ranged)
			*ranged = r;
		if (off)
			*off = o;
		return 1;
	}
	if (count > 1)
		return 2;
	return 0;
}

int
is_null_expr(Node* n);

// Reject null pointer literals projected into a ranged slice type.
static void
check_embed_ranged_null(Compiler* c, Span sp, Node* x) {
	Type* t;

	if (!user_source(c, sp) || x == NULL || x->type == NULL)
		return;
	t = decay(c, x->type);
	if (is_ptr(t) && is_null_expr(x))
		error_at(c, sp, "cannot use null pointer to %s",
			 type_name(t->base));
}

// True if t is a pointer to void.
int
is_void_ptr(Type* t) {
	return is_ptr(t) && t->base && t->base->kind == TyVoid;
}

// Error in user code when arithmetic is done on void*.
static void
reject_void_ptr_arith(Compiler* c, Span sp, Type* t) {
	if (t && is_void_ptr(t) && user_source(c, sp))
		error_at(c, sp,
			 "`void *` arithmetic is not allowed in %%C user code (allowed in headers); cast to a typed pointer first");
}

// True if n is a null pointer constant (0 or cast of 0).
int
is_null_expr(Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NdLit && n->int_val == 0)
		return 1;
	if (n->kind == NdCast && n->type && is_ptr(n->type) && n->a && n->a->kind == NdLit && n->a->int_val == 0)
		return 1;
	return 0;
}

// True if integer literal v fits in type t without truncation.
static int
int_lit_fits_type(Type* t, int64_t v) {
	int64_t min, max;
	uint64_t umax;
	int bits;

	if (t == NULL || !is_int(t) || t->kind == TyEnum)
		return 0;
	if (t->size <= 0)
		return 0;
	bits = t->size * 8;
	if (t->is_unsigned) {
		if (v < 0)
			return 0;
		if (bits >= 64)
			return 1;
		umax = bits == 64 ? ~0ULL : ((1ULL << bits) - 1);
		return (uint64_t)v <= umax;
	}
	if (bits >= 64)
		return 1;
	min = -(1LL << (bits - 1));
	max = (1LL << (bits - 1)) - 1;
	return v >= min && v <= max;
}

// Whether an implicit conversion from src to dst is allowed for this expression.
int conv_implicit_ok(Compiler* c, Type* dst, Type* src, Node* expr) {
	Type *from, *to;

	if (dst == NULL || src == NULL)
		return 1;
	if (type_eq(dst, src))
		return 1;
	to = dst;
	from = src;
	if (is_array(to) && is_array(from) && type_eq(to->base, from->base)) {
		if (to->len < 0 || from->len < 0 || to->len == from->len)
			return 1;
		return 0;
	}
	from = decay(c, src);
	if (is_array(src) && is_ptr(to) && type_eq(src->base, to->base))
		return 1;
	if (is_null_expr(expr) && is_ptr(to))
		return 1;
	if (is_ptr(to) && is_ptr(from)) {
		if (type_eq(to->base, from->base))
			return 1;
		if (is_void_ptr(to) || is_void_ptr(from))
			return 1;
		if (is_aggr(from->base) && is_aggr(to->base) && anon_embed_offset(from->base, to->base, NULL) == 1)
			return 1;
		return 0;
	}
	if (is_aggr(to) && is_aggr(from)) {
		if (type_eq(to, from))
			return 1;
		if (anon_embed_offset(from, to, NULL) == 1)
			return 1;
		return 0;
	}
	if (is_aggr(to) && is_ptr(from) && is_aggr(from->base) && anon_embed_offset(from->base, to, NULL) == 1) {
		if (is_null_expr(expr))
			return 0;
		return 1;
	}
	if (is_ranged(to) && is_ranged(from) && type_eq(to, from))
		return 1;
	if (is_ranged(to) && is_array(src) && src->len >= 0 && to->base && type_eq(to->base, src->base))
		return 1;
	if (is_ranged(to) && expr && expr->kind == NdStr && to->base && (to->base->kind == TyChar || to->base->kind == TyUChar))
		return 1;
	if (is_ptr(to) && is_ranged(from) && from->base &&
	    (type_eq(to->base, from->base) || to->base->kind == TyVoid))
		return 1;
	if (is_arith(to) && is_arith(from)) {
		if (to->kind == TyDouble)
			return 1;
		if (to->kind == TyFloat && from->kind != TyDouble)
			return 1;
		if (is_int(to) && is_int(from)) {
			/* Tagged enums are distinct: widen to int-like; not back, not cross-enum */
			if (to->kind == TyEnum && from->kind == TyEnum)
				return to == from;
			if (to->kind == TyEnum)
				return 0;
			if (from->kind == TyEnum)
				return from->size <= to->size;
			if (expr && expr->kind == NdLit && !expr->is_char_lit && int_lit_fits_type(to, expr->int_val))
				return 1;
			if ((to->kind == TyChar || to->kind == TyUChar) && expr && expr->kind == NdLit && expr->is_char_lit && (uint64_t)(unsigned char)expr->int_val == (uint64_t)expr->int_val)
				return 1;
			return from->size <= to->size;
		}
		if (is_int(to) && (from->kind == TyFloat || from->kind == TyDouble))
			return 0;
		return 0;
	}
	if (is_int(from) && is_ptr(to) && is_null_expr(expr))
		return 1;
	return 0;
}

// Deref a pointer and project through anonymous embed to reach dst aggregate.
Node* maybe_embed_deref_project(Compiler* c, Type* dst, Node* src) {
	Type* from;
	Node* load;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	from = decay(c, src->type);
	if (!is_ptr(from) || !is_aggr(dst) || !is_aggr(from->base))
		return src;
	if (anon_embed_offset(from->base, dst, NULL) != 1)
		return src;
	if (is_null_expr(src)) {
		if (user_source(c, src->span))
			error_at(c, src->span,
				 "cannot project through null pointer to %s",
				 type_name(from->base));
		return src;
	}
	load = node1(NdDeref, src->span, src);
	load->type = from->base;
	load->is_lvalue = 1;
	load = type_expr(c, load);
	return maybe_embed_project(c, dst, load);
}

// Project src aggregate to outer dst when inner is uniquely anonymously embedded.
Node* maybe_embed_project(Compiler* c, Type* dst, Node* src) {
	Type* from;
	Node *addr, *up, *deref;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	from = src->type;
	if (!is_aggr(dst) || !is_aggr(from) || type_eq(dst, from))
		return src;
	if (anon_embed_offset(from, dst, NULL) != 1)
		return src;
	addr = node1(NdAddr, src->span, src);
	addr->type = type_ptr(c, from);
	up = maybe_embed_upcast(c, type_ptr(c, dst), addr);
	deref = node1(NdDeref, src->span, up);
	deref->type = dst;
	deref->is_lvalue = 0;
	return type_expr(c, deref);
}

// Adjust pointer by anonymous-embed offset when upcasting to a base aggregate.
Node* maybe_embed_upcast(Compiler* c, Type* dst, Node* src) {
	Type *from, *to;
	int off, r;
	Node *n, *cp, *add, *lit;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	to = dst;
	from = decay(c, src->type);
	if (!is_ptr(to) || !is_ptr(from))
		return src;
	if (type_eq(to->base, from->base))
		return src;
	if (!is_aggr(from->base) || !is_aggr(to->base))
		return src;
	r = anon_embed_offset(from->base, to->base, &off);
	if (r != 1)
		return src;
	if (off == 0) {
		n = node1(NdCast, src->span, src);
		n->type = to;
		return n;
	}
	lit = node(NdLit, src->span);
	lit->int_val = off;
	lit->type = c->type_llong;
	cp = node1(NdCast, src->span, src);
	cp->type = type_ptr(c, c->type_char);
	add = node2(NdBin, src->span, cp, lit);
	add->op = PnPlus;
	add->type = cp->type;
	n = node1(NdCast, src->span, add);
	n->type = to;
	return n;
}

// AST name node for a compiler-inserted builtin (e.g. ranged()).
static Node*
mk_builtin_name(Compiler* c, Span sp, const char* name) {
	Node* fn;

	fn = node(NdName, sp);
	fn->s = (char*)name;
	return fn;
}

// Wrap array or string literal in ranged() when the target is a ranged type.
Node* maybe_ranged_conv(Compiler* c, Type* dst, Node* src) {
	Node* call;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	if (!is_ranged(dst))
		return src;
	if (is_ranged(src->type) && type_eq(dst, src->type))
		return src;
	if (is_array(src->type) && src->type->len >= 0 && dst->base && type_eq(dst->base, src->type->base)) {
		call = node1(NdCall, src->span, mk_builtin_name(c, src->span, "ranged"));
		node_add(call, src);
		call->type = type_ranged(c, src->type->base);
		return call;
	}
	if (src->kind == NdStr && dst->base && (dst->base->kind == TyChar || dst->base->kind == TyUChar)) {
		call = node1(NdCall, src->span, mk_builtin_name(c, src->span, "ranged"));
		node_add(call, src);
		call->type = type_ranged(c, dst->base);
		return call;
	}
	return src;
}

// Take .ptr when a ranged value is used where T* or void* is expected.
Node* maybe_ranged_decay(Compiler* c, Type* dst, Node* src) {
	Node* d;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	if (!is_ptr(dst))
		return src;
	if (!is_ranged(src->type) || !src->type->base)
		return src;
	/* Element pointer or void* (memcpy/memcmp). */
	if (!type_eq(dst->base, src->type->base) && dst->base->kind != TyVoid)
		return src;
	d = node(NdDot, src->span);
	d->a = src;
	d->s = "ptr";
	d->type = type_ptr(c, src->type->base);
	if (dst->base->kind == TyVoid)
		d->type = dst;
	d->int_val = 0;
	d->is_lvalue = 0;
	d->is_synth = 1;
	return d;
}

// Run embed and ranged rewrite passes before assignment or argument checking.
Node* apply_implicit_conversions(Compiler* c, Type* dst, Node* src) {
	if (dst == NULL || src == NULL)
		return src;
	src = maybe_embed_deref_project(c, dst, src);
	src = maybe_embed_project(c, dst, src);
	src = maybe_embed_upcast(c, dst, src);
	src = maybe_ranged_conv(c, dst, src);
	src = maybe_ranged_decay(c, dst, src);
	return src;
}

// Enforce fixed array length at call sites that take T[n] parameters.
static void
check_fixed_array_arg(Compiler* c, Span sp, Type* param, Node* arg, int64_t fixed_n) {
	Type* at;

	if (fixed_n < 0 || param == NULL || arg == NULL)
		return;
	at = arg->type;
	if (at && is_array(at) && at->len >= 0) {
		if (at->len != fixed_n)
			error_at(c, sp,
				 "cannot pass fixed array of length %" PRId64 " to parameter expecting %" PRId64,
				 (int64_t)at->len, (int64_t)fixed_n);
		if (type_eq(at->base, param->base))
			return;
	}
	if (at && is_ptr(at) && type_eq(at->base, param->base))
		error_at(c, sp,
			 "cannot pass pointer where fixed array of length %" PRId64 " is required",
			 (int64_t)fixed_n);
}

// Diagnose implicit conversions that conv_implicit_ok would reject.
void check_implicit_conv(Compiler* c, Span sp, Type* dst, Node* src) {
	Type* from;
	int emb;

	if (!user_source(c, sp) || dst == NULL || src == NULL)
		return;
	if (is_array(dst))
		from = src->type;
	else
		from = src->type ? decay(c, src->type) : NULL;
	if (from && is_ptr(dst) && is_ptr(from) && is_aggr(from->base) && is_aggr(dst->base)) {
		emb = anon_embed_offset(from->base, dst->base, NULL);
		if (emb == 2) {
			error_at(c, sp, "ambiguous anonymous embed conversion to %s *",
				 type_name(dst->base));
			return;
		}
	}
	if (from && is_aggr(from) && is_aggr(dst) && !type_eq(from, dst)) {
		emb = anon_embed_offset(from, dst, NULL);
		if (emb == 2) {
			error_at(c, sp, "ambiguous anonymous embed conversion to %s",
				 type_name(dst));
			return;
		}
	}
	if (from && is_ptr(from) && is_aggr(from->base) && is_aggr(dst)) {
		emb = anon_embed_offset(from->base, dst, NULL);
		if (emb == 2) {
			error_at(c, sp, "ambiguous anonymous embed conversion to %s",
				 type_name(dst));
			return;
		}
		if (emb == 1 && is_null_expr(src)) {
			error_at(c, sp, "cannot project through null pointer to %s",
				 type_name(from->base));
			return;
		}
	}
	if (from && conv_implicit_ok(c, dst, from, src))
		return;
	error_at(c, sp, "implicit conversion from %s to %s requires a cast",
		 type_name(from), type_name(dst));
}

// Error when a compile-time shift count is out of range for the lhs width.
void check_shift_count(Compiler* c, Span sp, Type* lhs, Node* count) {
	int64_t v;
	int bits;
	Type* pt;

	if (count == NULL || lhs == NULL || !is_int(lhs))
		return;
	pt = promote(c, lhs);
	bits = type_size(c, pt) * 8;
	if (bits <= 0)
		return;
	if (!eval_const(c, count, &v))
		return;
	if (v < 0 || v >= (int64_t)bits)
		error_at(c, sp, "shift count %" PRId64 " is out of range for %d-bit type",
			 (int64_t)v, bits);
}

// True when signed constant v is non-negative and fits in unsigned type uty.
static int
signed_const_fits_unsigned(Compiler* c, Type* uty, int64_t v) {
	int bits;
	uint64_t max;

	if (v < 0 || uty == NULL || !is_int(uty) || is_signed_int(uty))
		return 0;
	bits = type_size(c, uty) * 8;
	if (bits <= 0)
		return 0;
	if (bits >= 64)
		return 1; /* any non-negative int64_t fits in uint64_t */
	max = (1ULL << bits) - 1;
	return (uint64_t)v <= max;
}

// Error on signed vs unsigned comparisons in user code, except when the signed
// side is a non-negative constant that fits in the unsigned operand's type.
void check_sign_compare(Compiler* c, Span sp, Node* a, Node* b) {
	Type *pa, *pb;
	int sa, sb;
	int64_t v;

	if (!user_source(c, sp))
		return;
	if (a == NULL || b == NULL || a->type == NULL || b->type == NULL)
		return;
	if (!is_int(a->type) || !is_int(b->type))
		return;
	pa = promote(c, a->type);
	pb = promote(c, b->type);
	if (!is_int(pa) || !is_int(pb))
		return;
	sa = is_signed_int(pa);
	sb = is_signed_int(pb);
	if (sa == sb)
		return;
	if (sa && !sb && eval_const(c, a, &v) && signed_const_fits_unsigned(c, pb, v))
		return;
	if (!sa && sb && eval_const(c, b, &v) && signed_const_fits_unsigned(c, pa, v))
		return;
	error_at(c, sp, "comparison between signed and unsigned integers");
}

// Peel comma expressions; true if the value is marked immutable.
int
expr_is_immutable(Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NdComma)
		return expr_is_immutable(n->b);
	return n->is_immutable;
}

// Apply implicit conversions and arity checks for a direct function call.
void check_call_args(Compiler* c, Span sp, Type* fn, Node** args, int args_len) {
	int i, need;

	if (fn == NULL || !is_func(fn))
		return;
	if (fn->is_varargs) {
		if (args_len < fn->params_len) {
			error_at(c, sp, "too few arguments to function call");
			return;
		}
		need = fn->params_len;
	} else {
		if (args_len < fn->params_len) {
			error_at(c, sp, "too few arguments to function call");
			return;
		}
		if (args_len > fn->params_len) {
			error_at(c, sp, "too many arguments to function call");
			return;
		}
		need = fn->params_len;
	}
	for (i = 0; i < need; i++) {
		if (args[i] && fn->params[i]) {
			args[i] = apply_implicit_conversions(c, fn->params[i], args[i]);
			if (fn->param_array && fn->param_array[i])
				check_fixed_array_arg(c, sp, fn->params[i], args[i],
						      fn->param_fixed_len ? fn->param_fixed_len[i] : -1);
			check_implicit_conv(c, sp, fn->params[i], args[i]);
		}
	}
	/* Auto-const IMMUTABLE→mutable checks run in type_check_unit after
	 * READONLY inference (v2), not here during parse. */
	(void)need;
}

// Short diagnostic string for a type (not a full declaration printer).
const char*
type_name(Type* t) {
	if (t == NULL)
		return "<null>";
	switch (t->kind) {
	case TyVoid:
		return "void";
	case TyChar:
		return "char";
	case TyUChar:
		return "unsigned char";
	case TyShort:
		return "short";
	case TyUShort:
		return "unsigned short";
	case TyInt:
		return "int";
	case TyUInt:
		return "unsigned int";
	case TyLong:
		return "long";
	case TyULong:
		return "unsigned long";
	case TyLLong:
		return "long long";
	case TyULLong:
		return "unsigned long long";
	case TyFloat:
		return "float";
	case TyDouble:
		return "double";
	case TyBool:
		return "bool";
	case TyPtr:
		return "pointer";
	case TyArray:
		return "array";
	case TyFunc:
		return "function";
	case TyStruct:
		return t->is_ranged ? "ranged array" : (t->tag ? t->tag : "struct");
	case TyUnion:
		return t->tag ? t->tag : "union";
	case TyEnum:
		return t->tag ? t->tag : "enum";
	default:
		return "?";
	}
}

// Map ModC type to QBE value class (w/l/s/d/@) for temps and calls.
char qbe_class(Type* t) {
	if (t == NULL)
		return 'w';
	if (t->kind == TyPtr || t->kind == TyArray || t->kind == TyFunc)
		return 'l';
	if (is_aggr(t))
		return '@';
	switch (t->kind) {
	case TyLong:
	case TyULong:
		return t->size == 8 ? 'l' : 'w';
	case TyLLong:
	case TyULLong:
		return 'l';
	case TyFloat:
		return 's';
	case TyDouble:
		return 'd';
	default:
		return 'w';
	}
}

// Parse one hex digit for string-literal escape decoding.
static int
hexval(int ch) {
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return 0;
}

// Decode C escapes and append a NUL-terminated string to the compile-time pool.
int intern_str(Compiler* c, const char* raw, int* out_len) {
	unsigned char buf[4096];
	int n, i, off, dig;
	unsigned v;

	n = 0;
	if (raw == NULL)
		raw = "";
	for (i = 0; raw[i] && n < (int)sizeof(buf) - 1;) {
		if (raw[i] == '\\' && raw[i + 1]) {
			i++;
			switch (raw[i]) {
			case 'n':
				buf[n++] = '\n';
				i++;
				break;
			case 't':
				buf[n++] = '\t';
				i++;
				break;
			case 'r':
				buf[n++] = '\r';
				i++;
				break;
			case '\\':
				buf[n++] = '\\';
				i++;
				break;
			case '"':
				buf[n++] = '"';
				i++;
				break;
			case '\'':
				buf[n++] = '\'';
				i++;
				break;
			case 'x':
				i++;
				v = 0;
				while (isxdigit((unsigned char)raw[i])) {
					v = v * 16 + hexval(raw[i]);
					i++;
				}
				buf[n++] = (unsigned char)v;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				v = (unsigned)(raw[i] - '0');
				i++;
				for (dig = 0; dig < 2 && raw[i] >= '0' && raw[i] <= '7'; dig++) {
					v = v * 8 + (unsigned)(raw[i] - '0');
					i++;
				}
				buf[n++] = (unsigned char)v;
				break;
			default:
				buf[n++] = (unsigned char)raw[i++];
				break;
			}
		} else
			buf[n++] = (unsigned char)raw[i++];
	}
	buf[n++] = 0;
	if (out_len)
		*out_len = n;
	off = c->strpool_len;
	if (c->strpool_len + n > c->strpool_cap) {
		c->strpool_cap = c->strpool_cap ? c->strpool_cap * 2 : 256;
		while (c->strpool_cap < c->strpool_len + n)
			c->strpool_cap *= 2;
		c->strpool = xrealloc(c->strpool, c->strpool_cap);
	}
	memcpy(c->strpool + c->strpool_len, buf, n);
	c->strpool_len += n;
	return off;
}

// Recursively evaluate a constant expression tree into an int64.
static int
eval_rec(Compiler* c, Node* n, int64_t* out) {
	int64_t a, b, d;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NdLit:
		*out = n->int_val;
		return 1;
	case NdSizeof:
	case NdSizeofT:
		if (n->type)
			*out = type_size(c, n->kind == NdSizeof && n->a && n->a->type ? n->a->type : n->type);
		else if (n->a && n->a->type)
			*out = type_size(c, n->a->type);
		else
			return 0;
		return 1;
	case NdCast:
		if (!eval_rec(c, n->a, out))
			return 0;
		return 1;
	case NdUn:
		if (!eval_rec(c, n->a, &a))
			return 0;
		switch (n->op) {
		case PnPlus:
			*out = a;
			return 1;
		case PnMinus:
			*out = -a;
			return 1;
		case PnTilde:
			*out = ~a;
			return 1;
		case PnBang:
			*out = !a;
			return 1;
		default:
			return 0;
		}
	case NdBin:
		if (!eval_rec(c, n->a, &a) || !eval_rec(c, n->b, &b))
			return 0;
		switch (n->op) {
		case PnPlus:
			*out = a + b;
			return 1;
		case PnMinus:
			*out = a - b;
			return 1;
		case PnStar:
			*out = a * b;
			return 1;
		case PnSlash:
			*out = b ? a / b : 0;
			return 1;
		case PnPercent:
			*out = b ? a % b : 0;
			return 1;
		case PnAmp:
			*out = a & b;
			return 1;
		case PnPipe:
			*out = a | b;
			return 1;
		case PnCaret:
			*out = a ^ b;
			return 1;
		case PnShl:
			*out = a << b;
			return 1;
		case PnShr:
			*out = a >> b;
			return 1;
		case PnEqEq:
			*out = a == b;
			return 1;
		case PnBangEq:
			*out = a != b;
			return 1;
		case PnLt:
			*out = a < b;
			return 1;
		case PnGt:
			*out = a > b;
			return 1;
		case PnLe:
			*out = a <= b;
			return 1;
		case PnGe:
			*out = a >= b;
			return 1;
		case PnAmpAmp:
			*out = a && b;
			return 1;
		case PnPipePipe:
			*out = a || b;
			return 1;
		default:
			return 0;
		}
	case NdCond:
		if (!eval_rec(c, n->a, &a))
			return 0;
		return eval_rec(c, a ? n->b : n->c, out);
	case NdAddr:
		/* offsetof: &((T*)0)->member */
		if (n->a && (n->a->kind == NdArrow || n->a->kind == NdDot)) {
			*out = n->a->int_val;
			return 1;
		}
		return 0;
	case NdName:
		if (n->symbol && n->symbol->kind == SkEnumCon) {
			*out = n->symbol->int_val;
			return 1;
		}
		return 0;
	case NdComma:
		return eval_rec(c, n->b, out);
	default:
		(void)d;
		return 0;
	}
	return 0;
}

// Evaluate a constant expression into *out; returns 0 on failure.
int eval_const(Compiler* c, Node* n, int64_t* out) {
	return eval_rec(c, n, out);
}


// Look up a named field, including through anonymous aggregate embeds.
Field*
find_field(Type* t, const char* name, int* off) {
	Field *f, *g;
	int inner;

	if (t == NULL || name == NULL)
		return NULL;
	for (f = t->fields; f; f = f->next) {
		if (f->name && strcmp(f->name, name) == 0) {
			if (off)
				*off = f->offset;
			return f;
		}
		if ((f->name == NULL || f->name[0] == 0) && is_aggr(f->type)) {
			g = find_field(f->type, name, &inner);
			if (g) {
				if (off)
					*off = f->offset + inner;
				return g;
			}
		}
	}
	return NULL;
}

// Struct/union, or one peel of T* / T[N] (same as C `->`).
Type*
field_lhs(Type* t) {
	if (t == NULL)
		return NULL;
	if (is_aggr(t))
		return t;
	if (is_array(t))
		t = t->base;
	if (t && is_ptr(t))
		t = t->base;
	return t;
}

// Element type of an array or pointer; other types unchanged.
static Type*
ptr_base(Type* t) {
	if (t == NULL)
		return t;
	if (t->kind == TyArray || t->kind == TyPtr)
		return t->base;
	return t;
}

Node* type_expr(Compiler* c, Node* n);
void mark_symbol_used(Node* n);

// Type-check a call: overload resolution, builtins, method calls, and argument checking.
static Node* type_expr_call(Compiler* c, Node* n);

// Rewrite a method call into an ordinary call with receiver as first arg.
static Node*
lower_method_call(Compiler* c, Node* n) {
	Node *recv, *nm, *call;
	Symbol* sym;
	int i;

	if (n == NULL || n->a == NULL || n->a->kind != NdMethod)
		return n;
	recv = type_expr(c, n->a->a);
	if (recv == NULL || recv->type == NULL)
		return n;
	if (!is_ptr(recv->type)) {
		if (!recv->is_lvalue)
			error_at(c, n->span, "method call requires an lvalue receiver");
		recv = node1(NdAddr, n->span, recv);
		recv = type_expr(c, recv);
	}
	sym = symbol_resolve_method_call(c, recv->type, n->a->s, n->span);
	if (sym == NULL)
		return n;
	if (sym->recv_type)
		recv = maybe_embed_upcast(c, sym->recv_type, recv);
	for (i = 0; i < n->children_len; i++)
		if (n->children[i])
			n->children[i] = type_expr(c, n->children[i]);
	nm = node(NdName, n->span);
	nm->s = sym->name;
	nm->symbol = sym;
	nm->type = sym->type;
	mark_symbol_used(nm);
	call = node1(NdCall, n->span, nm);
	call->int_val = n->int_val;
	node_add(call, recv);
	for (i = 0; i < n->children_len; i++)
		node_add(call, n->children[i]);
	return type_expr_call(c, call);
}

// Type-check a call: overload resolution, builtins, methods, and arguments.
static Node*
type_expr_call(Compiler* c, Node* n) {
	Type* ft;
	char* bn = NULL;
	Symbol* osym;
	int i;

	if (n->a && n->a->kind == NdMethod)
		return lower_method_call(c, n);
	n->a = type_expr(c, n->a);
	if (n->a && n->a->kind == NdName && n->a->s && symbol_has_overload(c, n->a->s) && !n->int_val) {
		for (i = 0; i < n->children_len; i++)
			if (n->children[i])
				n->children[i] = type_expr(c, n->children[i]);
		osym = symbol_resolve_overload(c, n->a->s, n->children, n->children_len, n->span);
		if (osym) {
			n->a->symbol = osym;
			n->a->type = osym->type;
		}
		n->int_val = 1;
	}
	ft = n->a ? n->a->type : NULL;
	if (ft && is_ptr(ft) && is_func(ft->base))
		ft = ft->base;
	if (ft && is_func(ft) == 0 && n->a && n->a->kind == NdName && n->a->symbol)
		ft = n->a->symbol->type;
	if (n->a && n->a->kind == NdName && n->a->s)
		bn = n->a->s;
	if (bn && strcmp(bn, "__builtin_va_start") == 0) {
		n->type = c->type_void;
		return n;
	}
	if (bn && strcmp(bn, "__builtin_va_end") == 0) {
		n->type = c->type_void;
		return n;
	}
	if (bn && strcmp(bn, "__builtin_va_copy") == 0) {
		n->type = c->type_void;
		return n;
	}
	if (bn && strcmp(bn, "__builtin_va_arg") == 0) {
		if (n->children_len >= 2 && n->children[1] && n->children[1]->type && is_ptr(n->children[1]->type))
			n->type = n->children[1]->type->base;
		else
			n->type = c->type_int;
		return n;
	}
	/* User funcs/methods named len/cap/ranged win over the builtins. */
	if (bn && strcmp(bn, "ranged") == 0 && !(n->a && n->a->symbol)) {
		Node *x, *y, *z;
		Type *et, *pt;
		int64_t lv, cv;

		if (n->children_len == 1) {
			x = n->children[0];
			if (x && is_array(x->type) && x->type->len >= 0 && x->type->base) {
				n->type = type_ranged(c, x->type->base);
				return n;
			}
			if (x && x->kind == NdStr && x->type && x->type->base && (x->type->base->kind == TyChar || x->type->base->kind == TyUChar)) {
				n->type = type_ranged(c, x->type->base);
				return n;
			}
			error_at(c, n->span,
				 "ranged() with one argument requires a fixed array or string literal");
			n->type = type_ranged(c, c->type_int);
			return n;
		}
		if (n->children_len == 2 || n->children_len == 3) {
			x = n->children[0];
			y = n->children[1];
			z = n->children_len == 3 ? n->children[2] : NULL;
			pt = x ? decay(c, x->type) : NULL;
			if (pt && is_ptr(pt) && pt->base) {
				et = pt->base;
				if (y && y->type && !is_int(y->type))
					error_at(c, n->span, "ranged() length must be an integer");
				if (z && z->type && !is_int(z->type))
					error_at(c, n->span, "ranged() capacity must be an integer");
				if (z && eval_const(c, y, &lv) && eval_const(c, z, &cv) && lv > cv)
					error_at(c, n->span, "ranged() length exceeds capacity");
				n->type = type_ranged(c, et);
				return n;
			}
			error_at(c, n->span, "ranged() with two or three arguments requires a pointer");
			n->type = type_ranged(c, c->type_int);
			return n;
		}
		error_at(c, n->span, "ranged() takes one, two, or three arguments");
		n->type = type_ranged(c, c->type_int);
		return n;
	}
	if (bn && strcmp(bn, "len") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Type* lt;

		if (n->children_len != 1) {
			error_at(c, n->span, "len() takes one argument");
			n->type = c->type_ullong;
			return n;
		}
		if (n->children[0])
			n->children[0] = type_expr(c, n->children[0]);
		x = n->children[0];
		lt = x ? x->type : NULL;
		if (is_ranged(lt)) {
			n->type = c->type_ullong;
			return n;
		}
		if (is_array(lt) && lt->len >= 0) {
			n->type = c->type_ullong;
			return n;
		}
		if (x && x->kind == NdName && x->symbol && x->symbol->array_param && x->symbol->param_fixed_len >= 0) {
			n->type = c->type_ullong;
			return n;
		}
		{
			Type *ag, *rg;
			int er;

			ag = field_lhs(lt);
			er = ag ? anon_embed_unique_ranged(ag, &rg, NULL) : 0;
			if (er == 2) {
				error_at(c, n->span,
					 "ambiguous anonymous embed for len()");
				n->type = c->type_ullong;
				return n;
			}
			if (er == 1) {
				check_embed_ranged_null(c, n->span, x);
				n->type = c->type_ullong;
				return n;
			}
		}
		error_at(c, n->span, "len() requires a fixed or ranged array");
		n->type = c->type_ullong;
		return n;
	}
	if (bn && strcmp(bn, "cap") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Type* lt;

		if (n->children_len != 1) {
			error_at(c, n->span, "cap() takes one argument");
			n->type = c->type_ullong;
			return n;
		}
		if (n->children[0])
			n->children[0] = type_expr(c, n->children[0]);
		x = n->children[0];
		lt = x ? x->type : NULL;
		if (is_ranged(lt)) {
			n->type = c->type_ullong;
			return n;
		}
		if (is_array(lt) && lt->len >= 0) {
			n->type = c->type_ullong;
			return n;
		}
		if (x && x->kind == NdName && x->symbol && x->symbol->array_param && x->symbol->param_fixed_len >= 0) {
			n->type = c->type_ullong;
			return n;
		}
		{
			Type *ag, *rg;
			int er;

			ag = field_lhs(lt);
			er = ag ? anon_embed_unique_ranged(ag, &rg, NULL) : 0;
			if (er == 2) {
				error_at(c, n->span,
					 "ambiguous anonymous embed for cap()");
				n->type = c->type_ullong;
				return n;
			}
			if (er == 1) {
				check_embed_ranged_null(c, n->span, x);
				n->type = c->type_ullong;
				return n;
			}
		}
		error_at(c, n->span, "cap() requires a fixed or ranged array");
		n->type = c->type_ullong;
		return n;
	}
	if (bn && strcmp(bn, "ptr") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Type* lt;

		if (n->children_len != 1) {
			error_at(c, n->span, "ptr() takes one argument");
			n->type = c->type_void_ptr;
			return n;
		}
		if (n->children[0])
			n->children[0] = type_expr(c, n->children[0]);
		x = n->children[0];
		lt = x ? x->type : NULL;
		if (is_ranged(lt) && lt->base) {
			n->type = type_ptr(c, lt->base);
			return n;
		}
		if (is_array(lt) && lt->base) {
			n->type = type_ptr(c, lt->base);
			return n;
		}
		{
			Type *ag, *rg;
			int er;
			int off;

			ag = field_lhs(lt);
			er = ag ? anon_embed_unique_ranged(ag, &rg, &off) : 0;
			if (er == 2) {
				error_at(c, n->span,
					 "ambiguous anonymous embed for ptr()");
				n->type = c->type_void_ptr;
				return n;
			}
			if (er == 1 && rg && rg->base) {
				check_embed_ranged_null(c, n->span, x);
				n->type = type_ptr(c, rg->base);
				return n;
			}
		}
		error_at(c, n->span, "ptr() requires a fixed or ranged array");
		n->type = c->type_void_ptr;
		return n;
	}
	if (ft && is_func(ft)) {
		if (n->type == NULL) {
			for (i = 0; i < n->children_len; i++)
				if (n->children[i] && n->children[i]->type == NULL)
					n->children[i] = type_expr(c, n->children[i]);
			check_call_args(c, n->span, ft, n->children, n->children_len);
		}
		n->type = ft->base;
	} else if (ft && is_ptr(ft) && is_func(ft->base)) {
		if (n->type == NULL) {
			for (i = 0; i < n->children_len; i++)
				if (n->children[i] && n->children[i]->type == NULL)
					n->children[i] = type_expr(c, n->children[i]);
			check_call_args(c, n->span, ft->base, n->children, n->children_len);
		}
		n->type = ft->base->base;
	} else
		n->type = c->type_int;
	return n;
}

// Type-check binary operators: usual arithmetic, pointers, comparisons, shifts.
static Node*
type_expr_bin(Compiler* c, Node* n) {
	Type *lt, *rt;

	n->a = type_expr(c, n->a);
	n->b = type_expr(c, n->b);
	lt = n->a ? decay(c, n->a->type) : NULL;
	rt = n->b ? decay(c, n->b->type) : NULL;
	if (n->op == PnPlus) {
		if (is_ptr(lt) && is_int(rt)) {
			if (n->type == NULL)
				reject_void_ptr_arith(c, n->span, lt);
			n->type = lt;
		} else if (is_int(lt) && is_ptr(rt)) {
			if (n->type == NULL)
				reject_void_ptr_arith(c, n->span, rt);
			n->type = rt;
		} else
			n->type = usual_arith(c, lt, rt);
	} else if (n->op == PnMinus) {
		if (is_ptr(lt) && is_ptr(rt)) {
			if (n->type == NULL) {
				reject_void_ptr_arith(c, n->span, lt);
				reject_void_ptr_arith(c, n->span, rt);
			}
			n->type = c->type_llong; /* ptrdiff_t: signed pointer-width */
		} else if (is_ptr(lt) && is_int(rt)) {
			if (n->type == NULL)
				reject_void_ptr_arith(c, n->span, lt);
			n->type = lt;
		} else
			n->type = usual_arith(c, lt, rt);
	} else if (n->op == PnEqEq || n->op == PnBangEq || n->op == PnAmpAmp || n->op == PnPipePipe)
		n->type = c->type_bool;
	else if (n->op == PnLt || n->op == PnGt || n->op == PnLe || n->op == PnGe) {
		if (n->type == NULL)
			check_sign_compare(c, n->span, n->a, n->b);
		n->type = c->type_bool;
	} else if (n->op == PnShl || n->op == PnShr) {
		if (n->type == NULL)
			check_shift_count(c, n->span, lt, n->b);
		n->type = promote(c, lt);
	} else
		n->type = usual_arith(c, lt, rt);
	return n;
}

// Type-check compound and simple assignment with conversions and side checks.
static Node*
type_expr_assign(Compiler* c, Node* n) {
	int shift_chk, void_arith_chk;

	shift_chk = (n->op == PnShlEq || n->op == PnShrEq) && n->type == NULL;
	void_arith_chk = (n->op == PnPlusEq || n->op == PnMinusEq) && n->type == NULL;
	n->a = type_expr(c, n->a);
	n->b = type_expr(c, n->b);
	n->type = n->a ? n->a->type : c->type_int;
	n->is_lvalue = 0;
	if (n->a && n->a->type) {
		n->b = apply_implicit_conversions(c, n->a->type, n->b);
		check_implicit_conv(c, n->span, n->a->type, n->b);
	}
	if (void_arith_chk && n->a)
		reject_void_ptr_arith(c, n->span, decay(c, n->a->type));
	if (shift_chk)
		check_shift_count(c, n->span, n->a ? n->a->type : NULL, n->b);
	return n;
}

// Reject access to package-private fields from another package root.
static void
check_pkg_private_field(Compiler* c, Type* aggr, Field* f, Span sp) {
	char cur[1024];

	if (f == NULL || !f->pkg_private || aggr == NULL || aggr->pkg_root == NULL)
		return;
	if (c->infile == NULL)
		return;
	pkg_file_root(c->infile, cur, sizeof(cur));
	if (strcmp(cur, aggr->pkg_root) == 0)
		return;
	error_at(c, sp, "field '%s' is static (package-private)", f->name ? f->name : "");
}

// Type-check . / -> field access and attach field type and offset.
// T[..] headers are opaque: use len()/cap()/ptr(), not .len/.cap/.ptr.
static Node*
type_expr_field(Compiler* c, Node* n) {
	Type* lt;
	Field* f;
	int off;

	n->a = type_expr(c, n->a);
	if (n->kind == NdArrow && user_source(c, n->span))
		error_at(c, n->span, "%%C uses '.' for field access; '->' is for headers");
	lt = field_lhs(n->a ? n->a->type : NULL);
	if (!n->is_synth && is_ranged(lt) && n->s &&
	    (strcmp(n->s, "ptr") == 0 || strcmp(n->s, "len") == 0 || strcmp(n->s, "cap") == 0) &&
	    user_source(c, n->span)) {
		error_at(c, n->span,
			 "T[..] is opaque; use len(), cap(), or ptr() (not .%s)",
			 n->s);
		n->type = c->type_void_ptr;
		return n;
	}
	f = find_field(lt, n->s, &off);
	if (f == NULL)
		error_at(c, n->span, "no field named %s", n->s ? n->s : "");
	else {
		check_pkg_private_field(c, lt, f, n->span);
		n->type = f->type;
		n->int_val = off;
		n->is_lvalue = 1;
	}
	return n;
}

// Main expression type checker: recurse by kind and set result types.
Node* type_expr(Compiler* c, Node* n) {
	Type *lt, *rt;
	int i, nk;
	Field* f;

	if (n == NULL)
		return n;
	if (n->type && n->kind != NdCast && n->kind != NdSizeof && n->kind != NdSizeofT && n->kind != NdBin && n->kind != NdUn && n->kind != NdPost && n->kind != NdCall && n->kind != NdIndex && n->kind != NdAddr && n->kind != NdDeref && n->kind != NdAssign && n->kind != NdCond && n->kind != NdComma && n->kind != NdDot && n->kind != NdArrow && n->kind != NdTupleLit)
		return n;

	switch (n->kind) {
	case NdLit:
		return n;
	case NdStr:
		n->is_immutable = 1;
		return n;
	case NdName:
		if (n->symbol)
			n->type = n->symbol->type;
		return n;
	case NdSizeofT:
		n->int_val = type_size(c, n->type);
		n->type = c->type_ullong;
		n->kind = NdLit;
		return n;
	case NdSizeof:
		n->a = type_expr(c, n->a);
		if (n->a && n->a->kind == NdName && n->a->symbol && n->a->symbol->array_param && user_source(c, n->span) && n->a->symbol->param_fixed_len < 0)
			error_at(c, n->span,
				 "sizeof on array parameter '%s' is the size of a pointer",
				 n->a->symbol->name);
		mark_symbol_used(n->a);
		n->int_val = n->a && n->a->type ? type_size(c, n->a->type) : 0;
		n->type = c->type_ullong;
		n->kind = NdLit;
		n->a = NULL;
		return n;
	case NdCast:
		n->a = type_expr(c, n->a);
		if (n->type && is_ranged(n->type))
			n->a = apply_implicit_conversions(c, n->type, n->a);
		if (n->type && n->type->kind == TyVoid)
			return n;
		/* Auto-const escape: cast to non-readonly pointer strips IMMUTABLE. */
		if (n->a && expr_is_immutable(n->a)) {
			if (n->type && is_ptr(n->type) && n->type->is_readonly)
				n->is_immutable = 1;
			else if (n->type && is_ptr(n->type))
				n->is_immutable = 0;
			else
				n->is_immutable = 1;
		}
		return n;
	case NdAddr:
		n->a = type_expr(c, n->a);
		if (n->a && n->a->type)
			n->type = type_ptr(c, n->a->type);
		n->is_lvalue = 0;
		return n;
	case NdDeref:
		n->a = type_expr(c, n->a);
		lt = n->a ? decay(c, n->a->type) : NULL;
		if (lt && is_ptr(lt)) {
			n->type = lt->base;
			n->is_lvalue = 1;
		} else
			error_at(c, n->span, "indirection requires a pointer");
		return n;
	case NdUn:
		n->a = type_expr(c, n->a);
		if (n->op == PnBang) {
			n->type = c->type_bool;
			return n;
		}
		if (n->op == PnPlusPlus || n->op == PnMinusMinus) {
			if (n->type == NULL && n->a)
				reject_void_ptr_arith(c, n->span, decay(c, n->a->type));
			n->type = n->a ? n->a->type : c->type_int;
			n->is_lvalue = 0;
			return n;
		}
		n->type = n->a ? promote(c, n->a->type) : c->type_int;
		return n;
	case NdPost:
		n->a = type_expr(c, n->a);
		if (n->type == NULL && n->a && (n->op == PnPlusPlus || n->op == PnMinusMinus))
			reject_void_ptr_arith(c, n->span, decay(c, n->a->type));
		n->type = n->a ? n->a->type : c->type_int;
		return n;
	case NdIndex:
		n->a = type_expr(c, n->a);
		n->b = type_expr(c, n->b);
		lt = n->a ? n->a->type : NULL;
		if (is_ranged(lt) && lt->base) {
			n->type = lt->base;
			n->is_lvalue = 1;
			return n;
		}
		if (lt && (is_array(lt) || is_ptr(lt))) {
			if (n->type == NULL)
				reject_void_ptr_arith(c, n->span, decay(c, lt));
			n->type = ptr_base(lt);
		} else if (n->b && n->b->type && (is_array(n->b->type) || is_ptr(n->b->type))) {
			if (n->type == NULL)
				reject_void_ptr_arith(c, n->span, decay(c, n->b->type));
			n->type = ptr_base(n->b->type);
		} else
			error_at(c, n->span, "subscripted value is not an array or pointer");
		n->is_lvalue = 1;
		return n;
	case NdSubrange: {
		Type* elem;
		int64_t lo, hi;

		n->a = type_expr(c, n->a);
		if (n->b)
			n->b = type_expr(c, n->b);
		if (n->c)
			n->c = type_expr(c, n->c);
		lt = n->a ? n->a->type : NULL;
		elem = NULL;
		if (!is_ranged(lt) && !(lt && is_array(lt) && lt->base)) {
			Type *ag, *rg;
			int er;

			ag = field_lhs(lt);
			er = ag ? anon_embed_unique_ranged(ag, &rg, NULL) : 0;
			if (er == 2) {
				error_at(c, n->span,
					 "ambiguous anonymous embed subrange");
				n->type = type_ranged(c, c->type_int);
				return n;
			}
			if (er == 1 && rg) {
				check_embed_ranged_null(c, n->span, n->a);
				n->a = apply_implicit_conversions(c, rg, n->a);
				lt = n->a ? n->a->type : NULL;
			}
		}
		if (is_ranged(lt) && lt->base)
			elem = lt->base;
		else if (is_array(lt) && lt->base)
			elem = lt->base;
		else {
			error_at(c, n->span, "subrange requires a fixed or ranged array");
			n->type = type_ranged(c, c->type_int);
			return n;
		}
		if (n->b && n->b->type && !is_int(n->b->type))
			error_at(c, n->b->span, "subrange bound must be an integer");
		if (n->c && n->c->type && !is_int(n->c->type))
			error_at(c, n->c->span, "subrange bound must be an integer");
		if (n->b && n->c && eval_const(c, n->b, &lo) && eval_const(c, n->c, &hi) && lo > hi)
			error_at(c, n->span, "subrange start greater than end");
		n->type = type_ranged(c, elem);
		n->is_lvalue = 0;
		return n;
	}
	case NdDot:
	case NdArrow:
		return type_expr_field(c, n);
	case NdMethod:
		n->a = type_expr(c, n->a);
		return n;
	case NdTupleLit:
		for (i = 0; i < n->children_len; i++)
			n->children[i] = type_expr(c, n->children[i]);
		nk = 0;
		for (f = n->type ? n->type->fields : NULL; f; f = f->next)
			nk++;
		if (n->children_len != nk)
			error_at(c, n->span, "tuple arity mismatch");
		for (f = n->type ? n->type->fields : NULL, i = 0; f && i < n->children_len; f = f->next, i++) {
			if (n->children[i] && n->children[i]->type) {
				n->children[i] = apply_implicit_conversions(c, f->type, n->children[i]);
				check_implicit_conv(c, n->span, f->type, n->children[i]);
			}
		}
		return n;
	case NdCall:
		return type_expr_call(c, n);
	case NdBin:
		return type_expr_bin(c, n);
	case NdAssign:
		return type_expr_assign(c, n);
	case NdCond:
		n->a = type_expr(c, n->a);
		n->b = type_expr(c, n->b);
		n->c = type_expr(c, n->c);
		lt = n->b ? decay(c, n->b->type) : NULL;
		rt = n->c ? decay(c, n->c->type) : NULL;
		if (lt && rt && is_arith(lt) && is_arith(rt))
			n->type = usual_arith(c, lt, rt);
		else if (lt)
			n->type = lt;
		else
			n->type = rt;
		return n;
	case NdComma:
		n->a = type_expr(c, n->a);
		n->b = type_expr(c, n->b);
		n->type = n->b ? n->b->type : c->type_int;
		return n;
	default:
		return n;
	}
}


static void mark_symbol_used_init(Initializer* in);

// Mark every NdName symbol in the subtree as used.
void
mark_symbol_used(Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdName && n->symbol)
		n->symbol->used = 1;
	mark_symbol_used(n->a);
	mark_symbol_used(n->b);
	mark_symbol_used(n->c);
	for (i = 0; i < n->children_len; i++)
		mark_symbol_used(n->children[i]);
	if (n->init)
		mark_symbol_used_init(n->init);
}

// Mark symbols referenced from an initializer tree as used.
static void
mark_symbol_used_init(Initializer* in) {
	int i;

	if (in == NULL)
		return;
	mark_symbol_used(in->expr);
	for (i = 0; i < in->items_len; i++)
		mark_symbol_used_init(&in->items[i]);
}
