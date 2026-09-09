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

/*
 * Types, symbols, and post-parse checks.
 *
 * Compilation pipeline: lex → pp → parse → [type check] → emit → QBE
 * (type_expr also runs during parse; type_check_unit is the whole-unit pass.)
 *
 * Owns the Compiler type pool (type_list), symbol table, overload resolution, and
 * analyses that need a finished AST (uninit, fall-off, unused, discarded
 * tuple results, …). This file is the type system’s home, not a second parse.
 */
#include "ast.h"
#include <ctype.h>

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
	c->type_void = mkprim(c, TY_VOID, 0, 1, 0);
	c->type_char = mkprim(c, TY_CHAR, 1, 1, 1); /* %C: char is unsigned 8-bit */
	c->type_uchar = mkprim(c, TY_UCHAR, 1, 1, 1);
	c->type_short = mkprim(c, TY_SHORT, 2, 2, 0);
	c->type_ushort = mkprim(c, TY_USHORT, 2, 2, 1);
	c->type_int = mkprim(c, TY_INT, 4, 4, 0);
	c->type_uint = mkprim(c, TY_UINT, 4, 4, 1);
	/* Host ABI long (LP64: 8; LLP64/Windows: 4). Headers only in user dialect. */
	c->type_long = mkprim(c, TY_LONG, (int)sizeof(long), (int)sizeof(long), 0);
	c->type_ulong = mkprim(c, TY_ULONG, (int)sizeof(unsigned long),
			    (int)sizeof(unsigned long), 1);
	/* Fixed 64-bit for int64_t and dialect literal suffixes l/ul. */
	c->type_llong = mkprim(c, TY_LLONG, 8, 8, 0);
	c->type_ullong = mkprim(c, TY_ULLONG, 8, 8, 1);
	c->type_float = mkprim(c, TY_FLOAT, 4, 4, 0);
	c->type_double = mkprim(c, TY_DOUBLE, 8, 8, 0);
	c->type_bool = mkprim(c, TY_BOOL, 1, 1, 0);
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

	t = type_new(c, TY_PTR);
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

	t = type_new(c, TY_ARRAY);
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

Type* type_func(Compiler* c, Type* ret, Type** params, int n, int va) {
	Type* t;
	int i;

	t = type_new(c, TY_FUNC);
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
Type* type_struct(Compiler* c, int kind, char* tag, Span sp) {
	Type* t;
	Symbol* s;

	if (tag && tag[0]) {
		s = symbol_lookup_tag(c, tag);
		if (s && s->type && (s->type->kind == TY_STRUCT || s->type->kind == TY_UNION || s->type->kind == TY_ENUM))
			return s->type;
	}
	t = type_new(c, kind);
	t->tag = tag ? xstrdup(tag) : NULL;
	t->align = 1;
	if (tag && tag[0])
		symbol_define(c, tag, SK_TAG, t, ST_NONE, sp);
	return t;
}

// Interned {ptr,len} struct for T[..]; one ranged Type per element type.
Type* type_ranged(Compiler* c, Type* elem) {
	Type* t;
	Field *ptr, *len;
	static int next;

	if (elem == NULL)
		elem = c->type_void;
	for (t = c->type_list; t; t = t->next)
		if (t->is_ranged && type_eq(t->base, elem))
			return t;
	t = type_new(c, TY_STRUCT);
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
	ptr->next = len;
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
		return type_struct(c, TY_STRUCT, NULL, (Span){0});
	for (t = c->type_list; t; t = t->next) {
		if (tuple_matches(t, elts, n))
			return t;
	}
	t = type_new(c, TY_STRUCT);
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
	while (m->kind == TY_ARRAY) {
		if (m->len < 0 || m->base == NULL)
			return 0;
		m = m->base;
	}
	if (m->kind == TY_STRUCT || m->kind == TY_UNION || m->kind == TY_ENUM)
		return m->complete;
	return 1;
}

// True when t's layout can be computed (no incomplete aggregate members).
static int
type_layout_ready(Type* t) {
	Field* f;

	if (t == NULL)
		return 0;
	if (t->kind == TY_ARRAY)
		return t->len >= 0 && member_layout_ready(t->base);
	if (t->kind != TY_STRUCT && t->kind != TY_UNION)
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
	if (t->kind == TY_ARRAY) {
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
	if (t->kind != TY_STRUCT && t->kind != TY_UNION)
		return;
	if (!type_layout_ready(t))
		return;
	off = 0;
	maxal = 1;
	for (f = t->fields; f; f = f->next) {
		type_layout(c, f->type);
		if (f->type && !f->type->laid_out &&
		    (f->type->kind == TY_STRUCT || f->type->kind == TY_UNION || f->type->kind == TY_ARRAY))
			return;
		al = type_align(c, f->type);
		sz = type_size(c, f->type);
		if (al > maxal)
			maxal = al;
		if (t->kind == TY_UNION) {
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
			if (t->kind != TY_STRUCT && t->kind != TY_UNION && t->kind != TY_ARRAY)
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
	if (t->kind == TY_ARRAY && t->size == 0 && t->len >= 0 && t->base)
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

/* Integer kinds including bool and enum. */
int is_int(Type* t) {
	if (t == NULL)
		return 0;
	switch (t->kind) {
	case TY_CHAR:
	case TY_UCHAR:
	case TY_SHORT:
	case TY_USHORT:
	case TY_INT:
	case TY_UINT:
	case TY_LONG:
	case TY_ULONG:
	case TY_LLONG:
	case TY_ULLONG:
	case TY_BOOL:
	case TY_ENUM:
		return 1;
	default:
		return 0;
	}
}

int is_arith(Type* t) {
	return is_int(t) || (t && (t->kind == TY_FLOAT || t->kind == TY_DOUBLE));
}

/* Scalar in the C sense: arithmetic or pointer. */
int is_scalar(Type* t) {
	return is_arith(t) || is_ptr(t);
}

int is_ptr(Type* t) {
	return t && t->kind == TY_PTR;
}

int is_func(Type* t) {
	return t && t->kind == TY_FUNC;
}

int is_array(Type* t) {
	return t && t->kind == TY_ARRAY;
}

int is_aggr(Type* t) {
	return t && (t->kind == TY_STRUCT || t->kind == TY_UNION);
}

/* Interned {ptr,len} ranged-array struct. */
int is_ranged(Type* t) {
	return t && t->is_ranged;
}

/* Interned multi-return tuple struct. */
int is_tuple(Type* t) {
	return t && t->is_tuple;
}

/* Signed integer kinds; excludes char and all unsigned variants. */
int is_signed_int(Type* t) {
	if (t == NULL || t->is_unsigned)
		return 0;
	switch (t->kind) {
	case TY_SHORT:
	case TY_INT:
	case TY_LONG:
	case TY_LLONG:
	case TY_BOOL:
	case TY_ENUM:
		return 1;
	default:
		return 0;
	}
}

// Array→pointer and function→pointer decay; other types are unchanged.
Type* decay(Compiler* c, Type* t) {
	if (t == NULL)
		return t;
	if (t->kind == TY_ARRAY)
		return type_ptr(c, t->base);
	if (t->kind == TY_FUNC)
		return type_ptr(c, t);
	return t;
}

// Integer promotions and array/func decay used before usual_arith and comparisons.
Type* promote(Compiler* c, Type* t) {
	if (t == NULL)
		return c->type_int;
	if (t->kind == TY_FLOAT || t->kind == TY_DOUBLE)
		return t;
	if (t->kind == TY_PTR || t->kind == TY_ARRAY || t->kind == TY_FUNC)
		return decay(c, t);
	if (t->kind == TY_LLONG || t->kind == TY_ULLONG)
		return t;
	if (t->kind == TY_LONG || t->kind == TY_ULONG)
		return t;
	if (t->kind == TY_UINT && t->size >= 4)
		return t;
	return c->type_int;
}

// Common type for binary arithmetic after integer promotions on both operands.
Type* usual_arith(Compiler* c, Type* a, Type* b) {
	a = promote(c, a);
	b = promote(c, b);
	if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE)
		return c->type_double;
	if (a->kind == TY_FLOAT || b->kind == TY_FLOAT)
		return c->type_float;
	if (a->kind == TY_ULLONG || b->kind == TY_ULLONG)
		return c->type_ullong;
	if (a->kind == TY_LLONG || b->kind == TY_LLONG)
		return c->type_llong;
	if (a->kind == TY_ULONG || b->kind == TY_ULONG)
		return c->type_ulong;
	if (a->kind == TY_LONG || b->kind == TY_LONG)
		return c->type_long;
	if (a->kind == TY_UINT || b->kind == TY_UINT)
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
	if ((a->kind == TY_CHAR || a->kind == TY_UCHAR) && (b->kind == TY_CHAR || b->kind == TY_UCHAR))
		return 1;
	if (a->kind != b->kind)
		return 0;
	switch (a->kind) {
	case TY_PTR:
		return type_eq(a->base, b->base);
	case TY_ARRAY:
		return type_eq(a->base, b->base) && (a->len < 0 || b->len < 0 || a->len == b->len);
	case TY_FUNC:
		if (!type_eq(a->base, b->base) || a->params_len != b->params_len || a->is_varargs != b->is_varargs)
			return 0;
		for (i = 0; i < a->params_len; i++)
			if (!type_eq(a->params[i], b->params[i]))
				return 0;
		return 1;
	case TY_STRUCT:
	case TY_UNION:
	case TY_ENUM:
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

int
is_void_ptr(Type* t) {
	return is_ptr(t) && t->base && t->base->kind == TY_VOID;
}

// Error in user code when arithmetic is done on void*.
static void
reject_void_ptr_arith(Compiler* c, Span sp, Type* t) {
	if (t && is_void_ptr(t) && user_source(c, sp))
		error_at(c, sp,
			 "`void *` arithmetic is not allowed in %%C user code (allowed in headers); cast to a typed pointer first");
}

int
is_null_expr(Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NLit && n->int_val == 0)
		return 1;
	if (n->kind == NCast && n->type && is_ptr(n->type) && n->a && n->a->kind == NLit && n->a->int_val == 0)
		return 1;
	return 0;
}

static int
int_lit_fits_type(Type* t, int64_t v) {
	int64_t min, max;
	uint64_t umax;
	int bits;

	if (t == NULL || !is_int(t) || t->kind == TY_ENUM)
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
	if (is_ranged(to) && expr && expr->kind == NStr && to->base && (to->base->kind == TY_CHAR || to->base->kind == TY_UCHAR))
		return 1;
	if (is_ptr(to) && is_ranged(from) && from->base && type_eq(to->base, from->base))
		return 1;
	if (is_arith(to) && is_arith(from)) {
		if (to->kind == TY_DOUBLE)
			return 1;
		if (to->kind == TY_FLOAT && from->kind != TY_DOUBLE)
			return 1;
		if (is_int(to) && is_int(from)) {
			/* Tagged enums are distinct: widen to int-like; not back, not cross-enum */
			if (to->kind == TY_ENUM && from->kind == TY_ENUM)
				return to == from;
			if (to->kind == TY_ENUM)
				return 0;
			if (from->kind == TY_ENUM)
				return from->size <= to->size;
			if (expr && expr->kind == NLit && !expr->is_char_lit && int_lit_fits_type(to, expr->int_val))
				return 1;
			if ((to->kind == TY_CHAR || to->kind == TY_UCHAR) && expr && expr->kind == NLit && expr->is_char_lit && (uint64_t)(unsigned char)expr->int_val == (uint64_t)expr->int_val)
				return 1;
			return from->size <= to->size;
		}
		if (is_int(to) && (from->kind == TY_FLOAT || from->kind == TY_DOUBLE))
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
	load = node1(NDeref, src->span, src);
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
	addr = node1(NAddr, src->span, src);
	addr->type = type_ptr(c, from);
	up = maybe_embed_upcast(c, type_ptr(c, dst), addr);
	deref = node1(NDeref, src->span, up);
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
		n = node1(NCast, src->span, src);
		n->type = to;
		return n;
	}
	lit = node(NLit, src->span);
	lit->int_val = off;
	lit->type = c->type_llong;
	cp = node1(NCast, src->span, src);
	cp->type = type_ptr(c, c->type_char);
	add = node2(NBin, src->span, cp, lit);
	add->op = PPlus;
	add->type = cp->type;
	n = node1(NCast, src->span, add);
	n->type = to;
	return n;
}

// AST name node for a compiler-inserted builtin (e.g. ranged()).
static Node*
mk_builtin_name(Compiler* c, Span sp, const char* name) {
	Node* fn;

	fn = node(NName, sp);
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
		call = node1(NCall, src->span, mk_builtin_name(c, src->span, "ranged"));
		node_add(call, src);
		call->type = type_ranged(c, src->type->base);
		return call;
	}
	if (src->kind == NStr && dst->base && (dst->base->kind == TY_CHAR || dst->base->kind == TY_UCHAR)) {
		call = node1(NCall, src->span, mk_builtin_name(c, src->span, "ranged"));
		node_add(call, src);
		call->type = type_ranged(c, dst->base);
		return call;
	}
	return src;
}

// Take .ptr when a ranged value is used where T* is expected.
Node* maybe_ranged_decay(Compiler* c, Type* dst, Node* src) {
	Node* d;

	if (c == NULL || dst == NULL || src == NULL || src->type == NULL)
		return src;
	if (!is_ptr(dst))
		return src;
	if (!is_ranged(src->type) || !src->type->base)
		return src;
	if (!type_eq(dst->base, src->type->base))
		return src;
	d = node(NDot, src->span);
	d->a = src;
	d->s = "ptr";
	d->type = dst;
	d->int_val = 0;
	d->is_lvalue = 0;
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
				 "cannot pass fixed array of length %lld to parameter expecting %lld",
				 (long long)at->len, (long long)fixed_n);
		if (type_eq(at->base, param->base))
			return;
	}
	if (at && is_ptr(at) && type_eq(at->base, param->base))
		error_at(c, sp,
			 "cannot pass pointer where fixed array of length %lld is required",
			 (long long)fixed_n);
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
		error_at(c, sp, "shift count %lld is out of range for %d-bit type",
			 (long long)v, bits);
}

// Warn on signed vs unsigned comparisons in user code.
void check_sign_compare(Compiler* c, Span sp, Type* a, Type* b) {
	Type *pa, *pb;
	int sa, sb;

	if (!user_source(c, sp))
		return;
	if (a == NULL || b == NULL || !is_int(a) || !is_int(b))
		return;
	pa = promote(c, a);
	pb = promote(c, b);
	if (!is_int(pa) || !is_int(pb))
		return;
	sa = is_signed_int(pa);
	sb = is_signed_int(pb);
	if (sa == sb)
		return;
	error_at(c, sp, "comparison between signed and unsigned integers");
}

// Peel comma expressions; true if the value is marked immutable.
int
expr_is_immutable(Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NComma)
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
	case TY_VOID:
		return "void";
	case TY_CHAR:
		return "char";
	case TY_UCHAR:
		return "unsigned char";
	case TY_SHORT:
		return "short";
	case TY_USHORT:
		return "unsigned short";
	case TY_INT:
		return "int";
	case TY_UINT:
		return "unsigned int";
	case TY_LONG:
		return "long";
	case TY_ULONG:
		return "unsigned long";
	case TY_LLONG:
		return "long long";
	case TY_ULLONG:
		return "unsigned long long";
	case TY_FLOAT:
		return "float";
	case TY_DOUBLE:
		return "double";
	case TY_BOOL:
		return "bool";
	case TY_PTR:
		return "pointer";
	case TY_ARRAY:
		return "array";
	case TY_FUNC:
		return "function";
	case TY_STRUCT:
		return t->is_ranged ? "ranged array" : (t->tag ? t->tag : "struct");
	case TY_UNION:
		return t->tag ? t->tag : "union";
	case TY_ENUM:
		return t->tag ? t->tag : "enum";
	default:
		return "?";
	}
}

// Map ModC type to QBE value class (w/l/s/d/@) for temps and calls.
char qbe_class(Type* t) {
	if (t == NULL)
		return 'w';
	if (t->kind == TY_PTR || t->kind == TY_ARRAY || t->kind == TY_FUNC)
		return 'l';
	if (is_aggr(t))
		return '@';
	switch (t->kind) {
	case TY_LONG:
	case TY_ULONG:
		return t->size == 8 ? 'l' : 'w';
	case TY_LLONG:
	case TY_ULLONG:
		return 'l';
	case TY_FLOAT:
		return 's';
	case TY_DOUBLE:
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
int intern_str(Compiler* c, const char* raw) {
	unsigned char buf[4096];
	int n, i, off;
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
			case '0':
				buf[n++] = 0;
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
			default:
				buf[n++] = (unsigned char)raw[i++];
				break;
			}
		} else
			buf[n++] = (unsigned char)raw[i++];
	}
	buf[n++] = 0;
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
	case NLit:
		*out = n->int_val;
		return 1;
	case NSizeof:
	case NSizeofT:
		if (n->type)
			*out = type_size(c, n->kind == NSizeof && n->a && n->a->type ? n->a->type : n->type);
		else if (n->a && n->a->type)
			*out = type_size(c, n->a->type);
		else
			return 0;
		return 1;
	case NCast:
		if (!eval_rec(c, n->a, out))
			return 0;
		return 1;
	case NUn:
		if (!eval_rec(c, n->a, &a))
			return 0;
		switch (n->op) {
		case PPlus:
			*out = a;
			return 1;
		case PMinus:
			*out = -a;
			return 1;
		case PTilde:
			*out = ~a;
			return 1;
		case PBang:
			*out = !a;
			return 1;
		default:
			return 0;
		}
	case NBin:
		if (!eval_rec(c, n->a, &a) || !eval_rec(c, n->b, &b))
			return 0;
		switch (n->op) {
		case PPlus:
			*out = a + b;
			return 1;
		case PMinus:
			*out = a - b;
			return 1;
		case PStar:
			*out = a * b;
			return 1;
		case PSlash:
			*out = b ? a / b : 0;
			return 1;
		case PPercent:
			*out = b ? a % b : 0;
			return 1;
		case PAmp:
			*out = a & b;
			return 1;
		case PPipe:
			*out = a | b;
			return 1;
		case PCaret:
			*out = a ^ b;
			return 1;
		case PShl:
			*out = a << b;
			return 1;
		case PShr:
			*out = a >> b;
			return 1;
		case PEqEq:
			*out = a == b;
			return 1;
		case PBangEq:
			*out = a != b;
			return 1;
		case PLt:
			*out = a < b;
			return 1;
		case PGt:
			*out = a > b;
			return 1;
		case PLe:
			*out = a <= b;
			return 1;
		case PGe:
			*out = a >= b;
			return 1;
		case PAmpAmp:
			*out = a && b;
			return 1;
		case PPipePipe:
			*out = a || b;
			return 1;
		default:
			return 0;
		}
	case NCond:
		if (!eval_rec(c, n->a, &a))
			return 0;
		return eval_rec(c, a ? n->b : n->c, out);
	case NAddr:
		/* offsetof: &((T*)0)->member */
		if (n->a && (n->a->kind == NArrow || n->a->kind == NDot)) {
			*out = n->a->int_val;
			return 1;
		}
		return 0;
	case NName:
		if (n->symbol && n->symbol->kind == SK_ENUMCON) {
			*out = n->symbol->int_val;
			return 1;
		}
		return 0;
	case NComma:
		return eval_rec(c, n->b, out);
	default:
		(void)d;
		return 0;
	}
	return 0;
}

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
	if (t->kind == TY_ARRAY || t->kind == TY_PTR)
		return t->base;
	return t;
}

Node* type_expr(Compiler* c, Node* n);
void mark_symbol_used(Node* n);

// Type-check a call: overload resolution, builtins, method calls, and argument checking.
static Node* type_expr_call(Compiler* c, Node* n);

static Node*
lower_method_call(Compiler* c, Node* n) {
	Node *recv, *nm, *call;
	Symbol* sym;
	int i;

	if (n == NULL || n->a == NULL || n->a->kind != NMethod)
		return n;
	recv = type_expr(c, n->a->a);
	if (recv == NULL || recv->type == NULL)
		return n;
	if (!is_ptr(recv->type)) {
		if (!recv->is_lvalue)
			error_at(c, n->span, "method call requires an lvalue receiver");
		recv = node1(NAddr, n->span, recv);
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
	nm = node(NName, n->span);
	nm->s = sym->name;
	nm->symbol = sym;
	nm->type = sym->type;
	mark_symbol_used(nm);
	call = node1(NCall, n->span, nm);
	call->int_val = n->int_val;
	node_add(call, recv);
	for (i = 0; i < n->children_len; i++)
		node_add(call, n->children[i]);
	return type_expr_call(c, call);
}

static Node*
type_expr_call(Compiler* c, Node* n) {
	Type* ft;
	char* bn = NULL;
	Symbol* osym;
	int i;

	if (n->a && n->a->kind == NMethod)
		return lower_method_call(c, n);
	n->a = type_expr(c, n->a);
	if (n->a && n->a->kind == NName && n->a->s && symbol_has_overload(c, n->a->s) && !n->int_val) {
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
	if (ft && is_func(ft) == 0 && n->a && n->a->kind == NName && n->a->symbol)
		ft = n->a->symbol->type;
	if (n->a && n->a->kind == NName && n->a->s)
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
	if (bn && strcmp(bn, "ranged") == 0) {
		Node *x, *y;
		Type *et, *pt;

		if (n->children_len == 1) {
			x = n->children[0];
			if (x && is_array(x->type) && x->type->len >= 0 && x->type->base) {
				n->type = type_ranged(c, x->type->base);
				return n;
			}
			if (x && x->kind == NStr && x->type && x->type->base && (x->type->base->kind == TY_CHAR || x->type->base->kind == TY_UCHAR)) {
				n->type = type_ranged(c, x->type->base);
				return n;
			}
			error_at(c, n->span,
				 "ranged() with one argument requires a fixed array or string literal");
			n->type = type_ranged(c, c->type_int);
			return n;
		}
		if (n->children_len == 2) {
			x = n->children[0];
			y = n->children[1];
			pt = x ? decay(c, x->type) : NULL;
			if (pt && is_ptr(pt) && pt->base) {
				et = pt->base;
				if (y && y->type && !is_int(y->type))
					error_at(c, n->span, "ranged() length must be an integer");
				n->type = type_ranged(c, et);
				return n;
			}
			error_at(c, n->span, "ranged() with two arguments requires a pointer");
			n->type = type_ranged(c, c->type_int);
			return n;
		}
		error_at(c, n->span, "ranged() takes one or two arguments");
		n->type = type_ranged(c, c->type_int);
		return n;
	}
	if (bn && strcmp(bn, "len") == 0) {
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
		if (x && x->kind == NName && x->symbol && x->symbol->array_param && x->symbol->param_fixed_len >= 0) {
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
	if (n->op == PPlus) {
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
	} else if (n->op == PMinus) {
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
	} else if (n->op == PEqEq || n->op == PBangEq || n->op == PAmpAmp || n->op == PPipePipe)
		n->type = c->type_bool;
	else if (n->op == PLt || n->op == PGt || n->op == PLe || n->op == PGe) {
		if (n->type == NULL)
			check_sign_compare(c, n->span, lt, rt);
		n->type = c->type_bool;
	} else if (n->op == PShl || n->op == PShr) {
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

	shift_chk = (n->op == PShlEq || n->op == PShrEq) && n->type == NULL;
	void_arith_chk = (n->op == PPlusEq || n->op == PMinusEq) && n->type == NULL;
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
static Node*
type_expr_field(Compiler* c, Node* n) {
	Type* lt;
	Field* f;
	int off;

	n->a = type_expr(c, n->a);
	if (n->kind == NArrow && user_source(c, n->span))
		error_at(c, n->span, "%%C uses '.' for field access; '->' is for headers");
	lt = field_lhs(n->a ? n->a->type : NULL);
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
	if (n->type && n->kind != NCast && n->kind != NSizeof && n->kind != NSizeofT && n->kind != NBin && n->kind != NUn && n->kind != NPost && n->kind != NCall && n->kind != NIndex && n->kind != NAddr && n->kind != NDeref && n->kind != NAssign && n->kind != NCond && n->kind != NComma && n->kind != NDot && n->kind != NArrow && n->kind != NTupleLit)
		return n;

	switch (n->kind) {
	case NLit:
		return n;
	case NStr:
		n->is_immutable = 1;
		return n;
	case NName:
		if (n->symbol)
			n->type = n->symbol->type;
		return n;
	case NSizeofT:
		n->int_val = type_size(c, n->type);
		n->type = c->type_ullong;
		n->kind = NLit;
		return n;
	case NSizeof:
		n->a = type_expr(c, n->a);
		if (n->a && n->a->kind == NName && n->a->symbol && n->a->symbol->array_param && user_source(c, n->span) && n->a->symbol->param_fixed_len < 0)
			error_at(c, n->span,
				 "sizeof on array parameter '%s' is the size of a pointer",
				 n->a->symbol->name);
		mark_symbol_used(n->a);
		n->int_val = n->a && n->a->type ? type_size(c, n->a->type) : 0;
		n->type = c->type_ullong;
		n->kind = NLit;
		n->a = NULL;
		return n;
	case NCast:
		n->a = type_expr(c, n->a);
		if (n->type && is_ranged(n->type))
			n->a = apply_implicit_conversions(c, n->type, n->a);
		if (n->type && n->type->kind == TY_VOID)
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
	case NAddr:
		n->a = type_expr(c, n->a);
		if (n->a && n->a->type)
			n->type = type_ptr(c, n->a->type);
		n->is_lvalue = 0;
		return n;
	case NDeref:
		n->a = type_expr(c, n->a);
		lt = n->a ? decay(c, n->a->type) : NULL;
		if (lt && is_ptr(lt)) {
			n->type = lt->base;
			n->is_lvalue = 1;
		} else
			error_at(c, n->span, "indirection requires a pointer");
		return n;
	case NUn:
		n->a = type_expr(c, n->a);
		if (n->op == PBang) {
			n->type = c->type_bool;
			return n;
		}
		if (n->op == PPlusPlus || n->op == PMinusMinus) {
			if (n->type == NULL && n->a)
				reject_void_ptr_arith(c, n->span, decay(c, n->a->type));
			n->type = n->a ? n->a->type : c->type_int;
			n->is_lvalue = 0;
			return n;
		}
		n->type = n->a ? promote(c, n->a->type) : c->type_int;
		return n;
	case NPost:
		n->a = type_expr(c, n->a);
		if (n->type == NULL && n->a && (n->op == PPlusPlus || n->op == PMinusMinus))
			reject_void_ptr_arith(c, n->span, decay(c, n->a->type));
		n->type = n->a ? n->a->type : c->type_int;
		return n;
	case NIndex:
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
	case NSubrange: {
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
	case NDot:
	case NArrow:
		return type_expr_field(c, n);
	case NMethod:
		n->a = type_expr(c, n->a);
		return n;
	case NTupleLit:
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
	case NCall:
		return type_expr_call(c, n);
	case NBin:
		return type_expr_bin(c, n);
	case NAssign:
		return type_expr_assign(c, n);
	case NCond:
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
	case NComma:
		n->a = type_expr(c, n->a);
		n->b = type_expr(c, n->b);
		n->type = n->b ? n->b->type : c->type_int;
		return n;
	default:
		return n;
	}
}


static void mark_symbol_used_init(Initializer* in);

void
mark_symbol_used(Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NName && n->symbol)
		n->symbol->used = 1;
	mark_symbol_used(n->a);
	mark_symbol_used(n->b);
	mark_symbol_used(n->c);
	for (i = 0; i < n->children_len; i++)
		mark_symbol_used(n->children[i]);
	if (n->init)
		mark_symbol_used_init(n->init);
}

static void
mark_symbol_used_init(Initializer* in) {
	int i;

	if (in == NULL)
		return;
	mark_symbol_used(in->expr);
	for (i = 0; i < in->items_len; i++)
		mark_symbol_used_init(&in->items[i]);
}
