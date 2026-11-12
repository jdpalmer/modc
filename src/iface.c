/*
 * Package interface writer: public API as real %C source (no bodies).
 *
 * Generated .pkg.mc files are parseable by the normal frontend.
 */
#include "ast.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_os.h"

typedef struct {
	char* buf;
	size_t n;
	size_t cap;
} Out;

typedef struct {
	Type* type;
	int stub; /* foreign or incomplete: forward decl only */
} TypeEnt;

typedef struct {
	TypeEnt* ents;
	int len;
	int cap;
} TypeTab;

typedef struct {
	Symbol** syms;
	int len;
	int cap;
} SymTab;

typedef struct {
	Type** types;
	int len;
	int cap;
} TypeList;

/* ---- output buffer ---- */

static void
o_grow(Out* o, size_t need) {
	if (o->n + need < o->cap)
		return;
	while (o->n + need >= o->cap)
		o->cap = o->cap ? o->cap * 2 : 256;
	o->buf = xrealloc(o->buf, o->cap);
}

static void
o_putc(Out* o, char ch) {
	o_grow(o, 1);
	o->buf[o->n++] = ch;
}

static void
o_puts(Out* o, const char* s) {
	if (s == NULL)
		return;
	while (*s)
		o_putc(o, *s++);
}

static void
o_printf(Out* o, const char* fmt, ...) {
	char tmp[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n < (int)sizeof(tmp)) {
		o_puts(o, tmp);
		return;
	}
	{
		char* big = xmalloc((size_t)n + 1);
		va_list ap2;
		va_start(ap2, fmt);
		vsnprintf(big, (size_t)n + 1, fmt, ap2);
		va_end(ap2);
		o_puts(o, big);
		free(big);
	}
}

static void
o_nl(Out* o) {
	o_putc(o, '\n');
}

/* ---- path / membership ---- */

static void
path_abs(const char* in, char* out, size_t out_len) {
	char abs[HOST_PATH_MAX];

	if (in == NULL || in[0] == 0) {
		out[0] = 0;
		return;
	}
	snprintf(out, out_len, "%s", in);
	if (host_abspath(out, abs, sizeof(abs)) == 0)
		snprintf(out, out_len, "%s", abs);
}

static int
path_eq(const char* a, const char* b) {
	char aa[HOST_PATH_MAX], bb[HOST_PATH_MAX];

	path_abs(a, aa, sizeof(aa));
	path_abs(b, bb, sizeof(bb));
	return aa[0] && bb[0] && strcmp(aa, bb) == 0;
}

static int
sym_in_pkg(Symbol* s, const char* pkg_dir) {
	char root[HOST_PATH_MAX];

	if (s == NULL || s->home == NULL || s->home[0] == 0 || pkg_dir == NULL)
		return 0;
	pkg_file_root(s->home, root, sizeof(root));
	return path_eq(root, pkg_dir);
}

static void
pkg_ref_id(const char* dir, char* out, size_t out_len) {
	const char* base;
	size_t i, j;

	base = host_path_basename(dir);
	if (base == NULL || base[0] == 0)
		base = "root";
	for (i = 0, j = 0; base[i] && j + 1 < out_len; i++) {
		char ch = base[i];
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
			out[j++] = ch;
		else
			out[j++] = '_';
	}
	out[j] = 0;
}

static int
pkg_ref_matches(const char* root_tok, const char* pkg) {
	char id[128], abs[HOST_PATH_MAX];

	if (root_tok == NULL || root_tok[0] == 0 || strcmp(root_tok, "-") == 0)
		return 1;
	if (pkg == NULL || pkg[0] == 0)
		return 0;
	if (root_tok[0] == '@') {
		pkg_ref_id(pkg, id, sizeof(id));
		return strcmp(root_tok + 1, id) == 0;
	}
	path_abs(root_tok, abs, sizeof(abs));
	return path_eq(abs[0] ? abs : root_tok, pkg);
}

static int
type_foreign(Compiler* c, Type* t, const char* pkg_dir) {
	Symbol* s;

	if (t == NULL)
		return 0;
	if (t->pkg_root && t->pkg_root[0] && pkg_dir &&
	    !pkg_ref_matches(t->pkg_root, pkg_dir))
		return 1;
	if (t->tag == NULL || t->tag[0] == 0)
		return 0;
	s = symbol_lookup_tag(c, t->tag);
	if (s == NULL)
		return 0;
	if (s->header)
		return 1;
	if (s->home && s->home[0] && !sym_in_pkg(s, pkg_dir))
		return 1;
	return 0;
}

static int
export_kind_ok(int kind) {
	return kind == SkFunc || kind == SkVar || kind == SkTypedef || kind == SkTag ||
	       kind == SkEnumCon;
}

/* Public API symbols belonging to pkg_dir. */
static int
sym_exportable(Symbol* s, const char* pkg_dir) {
	if (s == NULL || s->block != 0 || s->header || s->storage == StStatic || s->dead)
		return 0;
	if (!export_kind_ok(s->kind))
		return 0;
	return sym_in_pkg(s, pkg_dir);
}

static int
is_builtin_pool(Compiler* c, Type* t) {
	return t != NULL && c != NULL && c->types.type_pool != NULL && t == c->types.type_pool;
}

static int
is_synth_tag(const char* tag) {
	if (tag == NULL || tag[0] == 0)
		return 0;
	return strncmp(tag, "__Ranged", 8) == 0 || strncmp(tag, "__Tuple", 7) == 0;
}

static int
is_named_aggr(Type* t) {
	if (t == NULL)
		return 0;
	if (t->kind != TyStruct && t->kind != TyUnion && t->kind != TyEnum)
		return 0;
	if (t->is_ranged || t->is_tuple)
		return 0;
	if (t->tag == NULL || t->tag[0] == 0 || is_synth_tag(t->tag))
		return 0;
	return 1;
}

/* ---- tables ---- */

static int
tytab_find(TypeTab* tab, Type* t) {
	int i;

	for (i = 0; i < tab->len; i++)
		if (tab->ents[i].type == t)
			return i;
	return -1;
}

static void
tytab_add(TypeTab* tab, Type* t, int stub) {
	int i;

	if (t == NULL)
		return;
	i = tytab_find(tab, t);
	if (i >= 0) {
		if (!stub)
			tab->ents[i].stub = 0;
		return;
	}
	if (tab->len >= tab->cap) {
		tab->cap = tab->cap ? tab->cap * 2 : 64;
		tab->ents = xrealloc(tab->ents, (size_t)tab->cap * sizeof(TypeEnt));
	}
	tab->ents[tab->len].type = t;
	tab->ents[tab->len].stub = stub;
	tab->len++;
}

static void
symtab_add(SymTab* tab, Symbol* s) {
	int i;

	if (s == NULL)
		return;
	for (i = 0; i < tab->len; i++)
		if (tab->syms[i] == s)
			return;
	if (tab->len >= tab->cap) {
		tab->cap = tab->cap ? tab->cap * 2 : 64;
		tab->syms = xrealloc(tab->syms, (size_t)tab->cap * sizeof(Symbol*));
	}
	tab->syms[tab->len++] = s;
}

static void
tylist_add(TypeList* list, Type* t) {
	int i;

	if (t == NULL)
		return;
	for (i = 0; i < list->len; i++)
		if (list->types[i] == t)
			return;
	if (list->len >= list->cap) {
		list->cap = list->cap ? list->cap * 2 : 32;
		list->types = xrealloc(list->types, (size_t)list->cap * sizeof(Type*));
	}
	list->types[list->len++] = t;
}

static int
tylist_has(TypeList* list, Type* t) {
	int i;

	for (i = 0; i < list->len; i++)
		if (list->types[i] == t)
			return 1;
	return 0;
}

static int
sym_export_cmp(const void* a, const void* b) {
	const Symbol* sa = *(const Symbol* const*)a;
	const Symbol* sb = *(const Symbol* const*)b;
	int c;

	if (sa->kind != sb->kind)
		return sa->kind - sb->kind;
	c = strcmp(sa->name ? sa->name : "", sb->name ? sb->name : "");
	if (c)
		return c;
	c = strcmp(sa->linkname ? sa->linkname : "", sb->linkname ? sb->linkname : "");
	if (c)
		return c;
	c = strcmp(sa->recv_tag ? sa->recv_tag : "", sb->recv_tag ? sb->recv_tag : "");
	if (c)
		return c;
	if (sa->int_val < sb->int_val)
		return -1;
	if (sa->int_val > sb->int_val)
		return 1;
	return 0;
}

static int
type_tag_cmp(const void* a, const void* b) {
	const TypeEnt* ea = a;
	const TypeEnt* eb = b;
	Type *ta = ea->type, *tb = eb->type;
	int c;

	if (ta->kind != tb->kind)
		return ta->kind - tb->kind;
	c = strcmp(ta->tag ? ta->tag : "", tb->tag ? tb->tag : "");
	if (c)
		return c;
	return (ea->stub != eb->stub) ? (ea->stub - eb->stub) : 0;
}

static void collect_type(Compiler* c, TypeTab* tab, SymTab* syms, Type* t,
			 const char* pkg_dir);

static void
collect_type(Compiler* c, TypeTab* tab, SymTab* syms, Type* t, const char* pkg_dir) {
	Field* f;
	Symbol* s;
	int i, stub, idx, was_stub;

	if (t == NULL)
		return;
	stub = 0;
	if ((t->kind == TyStruct || t->kind == TyUnion || t->kind == TyEnum) &&
	    !t->is_ranged && !t->is_tuple && type_foreign(c, t, pkg_dir))
		stub = 1;
	if ((t->kind == TyStruct || t->kind == TyUnion || t->kind == TyEnum) &&
	    !t->is_ranged && !t->is_tuple && !t->complete)
		stub = 1;
	idx = tytab_find(tab, t);
	if (idx >= 0) {
		was_stub = tab->ents[idx].stub;
		if (!stub)
			tab->ents[idx].stub = 0;
		if (stub || !was_stub)
			return;
	} else {
		tytab_add(tab, t, stub);
		if (stub)
			return;
	}

	switch (t->kind) {
	case TyPtr:
	case TyArray:
		collect_type(c, tab, syms, t->base, pkg_dir);
		break;
	case TyFunc:
		collect_type(c, tab, syms, t->base, pkg_dir);
		for (i = 0; i < t->params_len; i++)
			collect_type(c, tab, syms, t->params[i], pkg_dir);
		break;
	case TyStruct:
	case TyUnion:
		if (t->is_ranged)
			collect_type(c, tab, syms, t->base, pkg_dir);
		for (f = t->fields; f; f = f->next)
			collect_type(c, tab, syms, f->type, pkg_dir);
		break;
	case TyEnum:
		for (s = c->syms.symbols; s; s = s->next) {
			if (s->kind != SkEnumCon || s->type != t)
				continue;
			if (sym_exportable(s, pkg_dir))
				symtab_add(syms, s);
		}
		break;
	default:
		break;
	}
}

static void
collect_sym_types(Compiler* c, TypeTab* tab, SymTab* syms, Symbol* s, const char* pkg_dir) {
	collect_type(c, tab, syms, s->type, pkg_dir);
	if (s->is_method && s->recv_type)
		collect_type(c, tab, syms, s->recv_type, pkg_dir);
}

static int
mkdir_parents(const char* path) {
	char buf[HOST_PATH_MAX];
	size_t n, i;

	if (path == NULL || path[0] == 0)
		return 1;
	snprintf(buf, sizeof(buf), "%s", path);
	n = strlen(buf);
	for (i = 1; i < n; i++) {
		if (!host_path_is_sep((unsigned char)buf[i]))
			continue;
		buf[i] = 0;
		if (buf[0] && host_mkdir(buf) != 0 && !host_is_dir(buf))
			return 1;
		buf[i] = '/';
	}
	return 0;
}

/* ---- type / declaration printing ---- */

static const char*
prim_cname(Type* t) {
	if (t == NULL)
		return NULL;
	switch (t->kind) {
	case TyVoid:
		return "void";
	case TyBool:
		return "bool";
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
	default:
		return NULL;
	}
}

static void print_type(Compiler* c, Out* o, Type* t);

/* Prefer a typedef name for anonymous aggregates / fn-ptrs (not primitives). */
static const char*
typedef_name_for(Compiler* c, Type* t) {
	Symbol* s;

	if (c == NULL || t == NULL || prim_cname(t) != NULL)
		return NULL;
	if (is_named_aggr(t) && t->tag && t->tag[0] && !is_synth_tag(t->tag))
		return NULL;
	for (s = c->syms.symbols; s; s = s->next) {
		if (s->kind != SkTypedef || s->type != t)
			continue;
		if (s->name == NULL || s->name[0] == 0 || s->dead)
			continue;
		return s->name;
	}
	return NULL;
}

static void
print_type_qual_prefix(Out* o, Type* t) {
	if (t == NULL)
		return;
	if (t->is_poly)
		o_puts(o, "const? ");
	else if (t->is_readonly)
		o_puts(o, "const ");
}

static void
print_fnptr(Compiler* c, Out* o, Type* ft, const char* name) {
	int i;

	print_type(c, o, ft->base);
	o_puts(o, " (*");
	if (name && name[0])
		o_puts(o, name);
	o_puts(o, ")(");
	for (i = 0; i < ft->params_len; i++) {
		if (i)
			o_puts(o, ", ");
		print_type(c, o, ft->params[i]);
	}
	if (ft->is_varargs)
		o_puts(o, ft->params_len ? ", ..." : "...");
	o_putc(o, ')');
}

static void
print_type(Compiler* c, Out* o, Type* t) {
	Field* f;
	const char* pn;
	const char* td;
	int i, first;

	if (t == NULL) {
		o_puts(o, "void");
		return;
	}
	if (is_builtin_pool(c, t)) {
		o_puts(o, "Pool");
		return;
	}
	td = typedef_name_for(c, t);
	if (td) {
		print_type_qual_prefix(o, t);
		o_puts(o, td);
		return;
	}
	if (t->is_ranged) {
		print_type_qual_prefix(o, t);
		print_type(c, o, t->base);
		o_puts(o, "[..]");
		return;
	}
	if (t->is_tuple) {
		o_putc(o, '(');
		first = 1;
		for (f = t->fields; f; f = f->next) {
			if (!first)
				o_puts(o, ", ");
			first = 0;
			print_type(c, o, f->type);
		}
		o_putc(o, ')');
		return;
	}
	pn = prim_cname(t);
	if (pn) {
		o_puts(o, pn);
		return;
	}
	switch (t->kind) {
	case TyPtr:
		print_type_qual_prefix(o, t);
		if (t->base && t->base->kind == TyFunc) {
			print_fnptr(c, o, t->base, NULL);
			break;
		}
		print_type(c, o, t->base);
		o_putc(o, '*');
		break;
	case TyArray:
		print_type(c, o, t->base);
		if (t->len >= 0)
			o_printf(o, "[%" PRId64 "]", (int64_t)t->len);
		else
			o_puts(o, "[]");
		break;
	case TyFunc:
		/* Bare function type (rare): print as pointer-to-function shape. */
		print_fnptr(c, o, t, NULL);
		break;
	case TyStruct:
	case TyUnion:
	case TyEnum:
		if (t->tag && t->tag[0] && !is_synth_tag(t->tag))
			o_puts(o, t->tag);
		else if (t->kind == TyStruct)
			o_puts(o, "struct");
		else if (t->kind == TyUnion)
			o_puts(o, "union");
		else
			o_puts(o, "enum");
		break;
	default:
		o_puts(o, "void");
		break;
	}
}

static void
print_param_attrs(Out* o, Type* ft, int i) {
	int any = 0;

	if (ft == NULL || i < 0 || i >= ft->params_len)
		return;
	if (ft->param_retains && ft->param_retains[i]) {
		o_puts(o, "[[retains]]");
		any = 1;
	}
	if (ft->param_releases && ft->param_releases[i]) {
		if (any)
			o_puts(o, " ");
		o_puts(o, "[[releases]]");
		any = 1;
	}
	if (ft->param_region && ft->param_region[i]) {
		if (any)
			o_puts(o, " ");
		o_puts(o, "[[region]]");
		any = 1;
	}
	if (ft->param_borrows && ft->param_borrows[i]) {
		if (any)
			o_puts(o, " ");
		o_puts(o, "[[borrows]]");
		any = 1;
	}
	if (any)
		o_putc(o, ' ');
}

static void
print_param(Compiler* c, Out* o, Type* ft, int i, const char* fallback) {
	Type* pt;
	const char* name;
	char gen[32];

	if (ft == NULL || i < 0 || i >= ft->params_len)
		return;
	print_param_attrs(o, ft, i);
	pt = ft->params[i];
	name = (ft->param_names && ft->param_names[i] && ft->param_names[i][0])
		       ? ft->param_names[i]
		       : NULL;
	if (name == NULL) {
		if (fallback && fallback[0])
			name = fallback;
		else {
			snprintf(gen, sizeof(gen), "p%d", i);
			name = gen;
		}
	}
	/* Array params are stored as pointers with param_array / param_fixed_len. */
	if (ft->param_array && ft->param_array[i] && pt && is_ptr(pt)) {
		print_type(c, o, pt->base);
		o_putc(o, ' ');
		o_puts(o, name);
		if (ft->param_fixed_len && ft->param_fixed_len[i] >= 0)
			o_printf(o, "[%" PRId64 "]", (int64_t)ft->param_fixed_len[i]);
		else
			o_puts(o, "[]");
		return;
	}
	print_type(c, o, pt);
	o_putc(o, ' ');
	o_puts(o, name);
}

static const char*
aggr_kw(Type* t) {
	if (t == NULL)
		return "struct";
	if (t->kind == TyUnion)
		return "union";
	if (t->kind == TyEnum)
		return "enum";
	return "struct";
}

static void
print_field(Compiler* c, Out* o, Field* f) {
	if (f == NULL)
		return;
	o_putc(o, '\t');
	if (f->pkg_private)
		o_puts(o, "static ");
	/* Anonymous embed: "Widget;" */
	if ((f->name == NULL || f->name[0] == 0) && f->type && is_named_aggr(f->type)) {
		print_type(c, o, f->type);
		o_puts(o, ";\n");
		return;
	}
	if (f->type && is_array(f->type)) {
		print_type(c, o, f->type->base);
		o_putc(o, ' ');
		o_puts(o, f->name ? f->name : "_");
		if (f->type->len >= 0)
			o_printf(o, "[%" PRId64 "]", (int64_t)f->type->len);
		else
			o_puts(o, "[]");
		o_puts(o, ";\n");
		return;
	}
	print_type(c, o, f->type);
	if (f->name && f->name[0]) {
		o_putc(o, ' ');
		o_puts(o, f->name);
	}
	o_puts(o, ";\n");
}

static int
enumcon_cmp(const void* a, const void* b) {
	const Symbol* sa = *(const Symbol* const*)a;
	const Symbol* sb = *(const Symbol* const*)b;
	int c;

	if (sa->int_val < sb->int_val)
		return -1;
	if (sa->int_val > sb->int_val)
		return 1;
	c = strcmp(sa->name ? sa->name : "", sb->name ? sb->name : "");
	return c;
}

static void
collect_enumcons(Compiler* c, Type* et, SymTab* out) {
	Symbol* s;

	for (s = c->syms.symbols; s; s = s->next) {
		if (s->kind != SkEnumCon || s->type != et)
			continue;
		if (s->header || s->dead || s->storage == StStatic)
			continue;
		symtab_add(out, s);
	}
}

static void
emit_enum_body(Compiler* c, Out* o, Type* t) {
	SymTab cons = {0};
	int i;

	collect_enumcons(c, t, &cons);
	if (cons.len > 1)
		qsort(cons.syms, (size_t)cons.len, sizeof(Symbol*), enumcon_cmp);
	for (i = 0; i < cons.len; i++) {
		o_putc(o, '\t');
		o_puts(o, cons.syms[i]->name);
		o_printf(o, " = %" PRId64, (int64_t)cons.syms[i]->int_val);
		if (i + 1 < cons.len)
			o_putc(o, ',');
		o_nl(o);
	}
	free(cons.syms);
}

static void
emit_aggr_forward(Out* o, Type* t) {
	if (!is_named_aggr(t))
		return;
	o_puts(o, aggr_kw(t));
	o_putc(o, ' ');
	o_puts(o, t->tag);
	o_puts(o, ";\n");
}

static void emit_aggr_full(Compiler* c, Out* o, Type* t, const char* pkg_dir, TypeList* done,
			   TypeList* stack);

/* Emit full defs for by-value named aggregates referenced from t's fields. */
static void
emit_field_aggr_deps(Compiler* c, Out* o, Type* t, const char* pkg_dir, TypeList* done,
		     TypeList* stack) {
	Field* f;

	if (t == NULL || (t->kind != TyStruct && t->kind != TyUnion))
		return;
	for (f = t->fields; f; f = f->next) {
		Type* ft = f->type;
		if (ft == NULL)
			continue;
		if (is_named_aggr(ft) && !is_ptr(ft) && !is_array(ft))
			emit_aggr_full(c, o, ft, pkg_dir, done, stack);
		else if (is_array(ft) && ft->base && is_named_aggr(ft->base))
			emit_aggr_full(c, o, ft->base, pkg_dir, done, stack);
	}
}

static void
emit_aggr_full(Compiler* c, Out* o, Type* t, const char* pkg_dir, TypeList* done,
	       TypeList* stack) {
	Field* f;

	if (!is_named_aggr(t) || is_builtin_pool(c, t))
		return;
	if (tylist_has(done, t) || !t->complete || type_foreign(c, t, pkg_dir))
		return;
	if (tylist_has(stack, t))
		return; /* cycle: leave incomplete; forward was emitted */
	tylist_add(stack, t);
	emit_field_aggr_deps(c, o, t, pkg_dir, done, stack);
	o_puts(o, aggr_kw(t));
	o_putc(o, ' ');
	o_puts(o, t->tag);
	o_puts(o, " {\n");
	if (t->kind == TyEnum)
		emit_enum_body(c, o, t);
	else {
		for (f = t->fields; f; f = f->next)
			print_field(c, o, f);
	}
	o_puts(o, "};\n\n");
	tylist_add(done, t);
	if (stack->len > 0 && stack->types[stack->len - 1] == t)
		stack->len--;
}

static void
emit_typedef(Compiler* c, Out* o, Symbol* s) {
	Type* t;

	if (s == NULL || s->name == NULL)
		return;
	t = s->type;
	if (is_builtin_pool(c, t) || (s->name && strcmp(s->name, "Pool") == 0 && c->types.type_pool))
		return;
	/* Tag already provides the type name in %C. */
	if (t && is_named_aggr(t) && t->tag && strcmp(t->tag, s->name) == 0)
		return;
	o_puts(o, "typedef ");
	if (t && (t->kind == TyStruct || t->kind == TyUnion) && !t->is_ranged && !t->is_tuple &&
	    (t->tag == NULL || t->tag[0] == 0 || is_synth_tag(t->tag))) {
		Field* f;

		o_puts(o, t->kind == TyUnion ? "union" : "struct");
		o_puts(o, " {\n");
		for (f = t->fields; f; f = f->next)
			print_field(c, o, f);
		o_puts(o, "} ");
		o_puts(o, s->name);
		o_puts(o, ";\n\n");
		return;
	}
	/* typedef void (*Name)(args); — name sits inside the declarator. */
	if (t && is_ptr(t) && t->base && t->base->kind == TyFunc) {
		print_fnptr(c, o, t->base, s->name);
		o_puts(o, ";\n\n");
		return;
	}
	print_type(c, o, t);
	o_putc(o, ' ');
	o_puts(o, s->name);
	o_puts(o, ";\n\n");
}

static void
emit_var(Compiler* c, Out* o, Symbol* s) {
	if (s == NULL || s->name == NULL)
		return;
	if (s->storage == StExtern)
		o_puts(o, "extern ");
	if (s->type && is_array(s->type)) {
		print_type(c, o, s->type->base);
		o_putc(o, ' ');
		o_puts(o, s->name);
		if (s->type->len >= 0)
			o_printf(o, "[%" PRId64 "]", (int64_t)s->type->len);
		else
			o_puts(o, "[]");
		o_puts(o, ";\n");
		return;
	}
	print_type(c, o, s->type);
	o_putc(o, ' ');
	o_puts(o, s->name);
	o_puts(o, ";\n");
}

static void
emit_func(Compiler* c, Out* o, Symbol* s) {
	Type* ft;
	int i, start;

	if (s == NULL || s->name == NULL)
		return;
	ft = s->type;
	if (ft == NULL || ft->kind != TyFunc)
		return;
	if (s->is_overload)
		o_puts(o, "overload ");
	if (s->nodiscard)
		o_puts(o, "[[nodiscard]] ");
	if (s->provides || ft->provides)
		o_puts(o, "[[provides]] ");
	print_type(c, o, ft->base);
	o_putc(o, ' ');
	if (s->is_method) {
		Type* recv = s->recv_type;
		const char* rname = "r";

		if (ft->param_names && ft->param_names[0] && ft->param_names[0][0])
			rname = ft->param_names[0];
		o_putc(o, '(');
		if (ft->params_len > 0)
			print_param_attrs(o, ft, 0);
		if (recv)
			print_type(c, o, recv);
		else if (ft->params_len > 0)
			print_type(c, o, ft->params[0]);
		else
			o_puts(o, "void*");
		o_putc(o, ' ');
		o_puts(o, rname);
		o_puts(o, ").");
		o_puts(o, s->name);
		o_putc(o, '(');
		start = 1;
		for (i = start; i < ft->params_len; i++) {
			if (i > start)
				o_puts(o, ", ");
			print_param(c, o, ft, i, NULL);
		}
		if (ft->is_varargs)
			o_puts(o, ft->params_len > start ? ", ..." : "...");
		o_puts(o, ");\n");
		return;
	}
	o_puts(o, s->name);
	o_putc(o, '(');
	for (i = 0; i < ft->params_len; i++) {
		if (i)
			o_puts(o, ", ");
		print_param(c, o, ft, i, NULL);
	}
	if (ft->is_varargs)
		o_puts(o, ft->params_len ? ", ..." : "...");
	o_puts(o, ");\n");
}

static void
emit_anon_enumcon(Out* o, Symbol* s) {
	if (s == NULL || s->name == NULL)
		return;
	o_puts(o, "enum {\n\t");
	o_puts(o, s->name);
	o_printf(o, " = %" PRId64 "\n};\n\n", (int64_t)s->int_val);
}

static int
tagged_enum_emitted(TypeList* done, Type* t) {
	return t && is_named_aggr(t) && t->kind == TyEnum && tylist_has(done, t);
}

/* ---- public API ---- */

int
iface_write_pkg(Compiler* c, const char* pkg_dir, const char* path) {
	TypeTab types = {0};
	SymTab syms = {0};
	TypeList done = {0};
	TypeList stack = {0};
	TypeEnt* sorted = NULL;
	Out out = {0};
	Symbol* s;
	char* existing;
	size_t existing_n;
	int i, err, nfwd;

	if (c == NULL || pkg_dir == NULL || path == NULL)
		return 1;

	for (s = c->syms.symbols; s; s = s->next) {
		if (!sym_exportable(s, pkg_dir))
			continue;
		/* Never emit the compiler builtin Pool. */
		if (s->name && strcmp(s->name, "Pool") == 0 && c->types.type_pool &&
		    (s->kind == SkTypedef || s->kind == SkTag) &&
		    (s->type == NULL || s->type == c->types.type_pool))
			continue;
		symtab_add(&syms, s);
	}
	if (syms.len > 1)
		qsort(syms.syms, (size_t)syms.len, sizeof(Symbol*), sym_export_cmp);
	for (i = 0; i < syms.len; i++)
		collect_sym_types(c, &types, &syms, syms.syms[i], pkg_dir);

	/* Stable order for aggregate decls. */
	if (types.len > 0) {
		sorted = xmalloc((size_t)types.len * sizeof(TypeEnt));
		memcpy(sorted, types.ents, (size_t)types.len * sizeof(TypeEnt));
		qsort(sorted, (size_t)types.len, sizeof(TypeEnt), type_tag_cmp);
	}

	/* Forward decls for foreign / incomplete named aggregates. */
	nfwd = 0;
	for (i = 0; i < types.len; i++) {
		Type* t = sorted[i].type;
		if (!is_named_aggr(t) || is_builtin_pool(c, t))
			continue;
		if (!(sorted[i].stub || !t->complete))
			continue;
		emit_aggr_forward(&out, t);
		nfwd++;
	}
	if (nfwd)
		o_nl(&out);

	/* Full layouts for complete local aggregates (topo via emit_aggr_full). */
	for (i = 0; i < types.len; i++) {
		Type* t = sorted[i].type;
		if (!is_named_aggr(t) || is_builtin_pool(c, t))
			continue;
		if (sorted[i].stub || !t->complete)
			continue;
		stack.len = 0;
		emit_aggr_full(c, &out, t, pkg_dir, &done, &stack);
	}

	/* Typedefs (non-redundant). */
	for (i = 0; i < syms.len; i++) {
		if (syms.syms[i]->kind != SkTypedef)
			continue;
		emit_typedef(c, &out, syms.syms[i]);
	}

	/* Anonymous enum constants (not part of a named enum we emitted). */
	for (i = 0; i < syms.len; i++) {
		Symbol* es = syms.syms[i];
		if (es->kind != SkEnumCon)
			continue;
		if (tagged_enum_emitted(&done, es->type))
			continue;
		if (es->type && is_named_aggr(es->type) && es->type->kind == TyEnum)
			continue; /* named enum not emitted (foreign); skip members */
		emit_anon_enumcon(&out, es);
	}

	/* Globals. */
	for (i = 0; i < syms.len; i++) {
		if (syms.syms[i]->kind != SkVar)
			continue;
		emit_var(c, &out, syms.syms[i]);
	}

	/* Functions / methods. */
	for (i = 0; i < syms.len; i++) {
		if (syms.syms[i]->kind != SkFunc)
			continue;
		emit_func(c, &out, syms.syms[i]);
	}

	if (mkdir_parents(path) != 0) {
		err = 1;
		goto out;
	}

	/* Write-if-changed: leave mtime alone when bytes match. */
	existing = read_file(path, &existing_n);
	if (existing != NULL && existing_n == out.n &&
	    (out.n == 0 || memcmp(existing, out.buf, out.n) == 0)) {
		free(existing);
		err = 0;
		goto out;
	}
	free(existing);

	err = host_write_atomic(path, out.buf ? out.buf : "", out.n) != 0;

out:
	free(out.buf);
	free(types.ents);
	free(syms.syms);
	free(sorted);
	free(done.types);
	free(stack.types);
	return err;
}

/*
 * Load a generated .pkg.mc into Compiler (symbols/types only; no emit).
 * Uses the normal parser with opt.iface_import so prototypes are allowed.
 */
int
iface_load(Compiler* c, const char* path, const char* pkg_dir) {
	char* text;
	int save_ii;
	int nfuncs_before;
	int i;

	(void)pkg_dir;
	if (c == NULL || path == NULL || path[0] == 0)
		return 1;
	if (!host_is_file(path))
		return 1;
	text = read_file(path, NULL);
	if (text == NULL)
		return 1;
	save_ii = c->opt.iface_import;
	c->opt.iface_import = 1;
	nfuncs_before = c->unit.funcs_len;
	/* Fresh token stream per iface (lex_file appends otherwise). */
	c->lex.tokens = NULL;
	c->lex.tokens_len = 0;
	c->lex.tokens_cap = 0;
	c->lex.pos = 0;
	pp_clear_macros(c);
	pp_clear_once(c);
	pp_init(c);
	free(c->paths.infile);
	c->paths.infile = xstrdup(path);
	lex_file(c, c->paths.infile, text, 1);
	free(text);
	if (c->diag.error_count) {
		c->opt.iface_import = save_ii;
		return 1;
	}
	pp_run(c);
	if (c->diag.error_count) {
		c->opt.iface_import = save_ii;
		return 1;
	}
	c->lex.pos = 0;
	prescan_unit_type_names(c);
	c->lex.pos = 0;
	prescan_unit_type_bodies(c);
	type_layout_pending(c);
	/* Skip prescan_unit_funcs: prototypes would be registered twice (prescan +
	 * parse) and overloads error as duplicates. Iface files are ordered. */
	c->lex.pos = 0;
	parse_unit(c);
	c->opt.iface_import = save_ii;
	/* Prototypes should not add funcs; drop any that slipped in. */
	if (c->unit.funcs_len > nfuncs_before) {
		for (i = nfuncs_before; i < c->unit.funcs_len; i++)
			c->unit.funcs[i] = NULL;
		c->unit.funcs_len = nfuncs_before;
	}
	return c->diag.error_count ? 1 : 0;
}
