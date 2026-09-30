/*
 * Package export data: serialize / load a package public API.
 *
 * Lets cache-hit packages skip re-lex/parse/typecheck by restoring
 * types and file-scope symbols from a versioned text artifact.
 */
#include "ast.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_os.h"

enum { ExportVer = 2 };

typedef struct {
	Type* type;
	int stub; /* emit incomplete aggregate (opaque pointee) */
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

static int
type_foreign(Compiler* c, Type* t, const char* pkg_dir) {
	Symbol* s;

	if (t == NULL)
		return 0;
	if (t->pkg_root && t->pkg_root[0] && pkg_dir && !path_eq(t->pkg_root, pkg_dir))
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

/* ---- tables ---- */

static int
tytab_find(TypeTab* tab, Type* t) {
	int i;

	for (i = 0; i < tab->len; i++)
		if (tab->ents[i].type == t)
			return i;
	return -1;
}

static int
tytab_add(TypeTab* tab, Type* t, int stub) {
	int i;

	if (t == NULL)
		return 0;
	i = tytab_find(tab, t);
	if (i >= 0) {
		if (!stub)
			tab->ents[i].stub = 0;
		return i + 1;
	}
	if (tab->len >= tab->cap) {
		tab->cap = tab->cap ? tab->cap * 2 : 64;
		tab->ents = xrealloc(tab->ents, (size_t)tab->cap * sizeof(TypeEnt));
	}
	tab->ents[tab->len].type = t;
	tab->ents[tab->len].stub = stub;
	tab->len++;
	return tab->len;
}

static int
tytab_id(TypeTab* tab, Type* t) {
	int i = tytab_find(tab, t);

	return i < 0 ? 0 : i + 1;
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

static int
export_kind_ok(int kind) {
	return kind == SkFunc || kind == SkVar || kind == SkTypedef || kind == SkTag ||
	       kind == SkEnumCon;
}

static int
sym_exportable(Symbol* s, const char* pkg_dir) {
	if (s == NULL || s->block != 0 || s->header || s->storage == StStatic || s->dead)
		return 0;
	if (!export_kind_ok(s->kind))
		return 0;
	return sym_in_pkg(s, pkg_dir);
}

/* Collect type t into the export tables. Foreign aggregates are always stubbed. */
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
	/* Stub foreign aggregates (even by-value). Owner export has the layout;
	 * load interns by tag. */
	if ((t->kind == TyStruct || t->kind == TyUnion || t->kind == TyEnum) &&
	    type_foreign(c, t, pkg_dir))
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

/* ---- write helpers ---- */

static const struct {
	const char* name;
	int kind;
} PrimTab[] = {
	{"void", TyVoid},     {"bool", TyBool},   {"char", TyChar},
	{"uchar", TyUChar},   {"short", TyShort}, {"ushort", TyUShort},
	{"int", TyInt},       {"uint", TyUInt},   {"long", TyLong},
	{"ulong", TyULong},   {"llong", TyLLong}, {"ullong", TyULLong},
	{"float", TyFloat},   {"double", TyDouble},
};

static const struct {
	const char* name;
	int val;
} SkTab[] = {
	{"func", SkFunc}, {"var", SkVar}, {"typedef", SkTypedef},
	{"tag", SkTag},   {"enumcon", SkEnumCon},
};

static const struct {
	const char* name;
	int val;
} StTab[] = {
	{"extern", StExtern}, {"static", StStatic}, {"local", StLocal},
	{"param", StParam},   {"typedef", StTypedef},
};

static Type*
prim_by_kind(Compiler* c, int kind) {
	switch (kind) {
	case TyVoid:
		return c->types.type_void;
	case TyBool:
		return c->types.type_bool;
	case TyChar:
		return c->types.type_char;
	case TyUChar:
		return c->types.type_uchar;
	case TyShort:
		return c->types.type_short;
	case TyUShort:
		return c->types.type_ushort;
	case TyInt:
		return c->types.type_int;
	case TyUInt:
		return c->types.type_uint;
	case TyLong:
		return c->types.type_long;
	case TyULong:
		return c->types.type_ulong;
	case TyLLong:
		return c->types.type_llong;
	case TyULLong:
		return c->types.type_ullong;
	case TyFloat:
		return c->types.type_float;
	case TyDouble:
		return c->types.type_double;
	default:
		return NULL;
	}
}

static const char*
prim_name(Type* t) {
	size_t i;

	if (t == NULL)
		return NULL;
	for (i = 0; i < sizeof(PrimTab) / sizeof(PrimTab[0]); i++)
		if (PrimTab[i].kind == t->kind)
			return PrimTab[i].name;
	return NULL;
}

static Type*
prim_match(Compiler* c, const char* name) {
	size_t i;

	if (name == NULL)
		return NULL;
	for (i = 0; i < sizeof(PrimTab) / sizeof(PrimTab[0]); i++)
		if (strcmp(name, PrimTab[i].name) == 0)
			return prim_by_kind(c, PrimTab[i].kind);
	return NULL;
}

static const char*
sk_name(int kind) {
	size_t i;

	for (i = 0; i < sizeof(SkTab) / sizeof(SkTab[0]); i++)
		if (SkTab[i].val == kind)
			return SkTab[i].name;
	return "none";
}

static int
parse_sk(const char* s) {
	size_t i;

	if (s == NULL)
		return SkNone;
	for (i = 0; i < sizeof(SkTab) / sizeof(SkTab[0]); i++)
		if (strcmp(s, SkTab[i].name) == 0)
			return SkTab[i].val;
	return SkNone;
}

static const char*
st_name(int st) {
	size_t i;

	for (i = 0; i < sizeof(StTab) / sizeof(StTab[0]); i++)
		if (StTab[i].val == st)
			return StTab[i].name;
	return "none";
}

static int
parse_st(const char* s) {
	size_t i;

	if (s == NULL)
		return StNone;
	for (i = 0; i < sizeof(StTab) / sizeof(StTab[0]); i++)
		if (strcmp(s, StTab[i].name) == 0)
			return StTab[i].val;
	return StNone;
}

static void
write_tok(FILE* f, const char* s) {
	if (s == NULL || s[0] == 0)
		fputs("-", f);
	else
		fputs(s, f);
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

static void
write_type(FILE* f, TypeTab* tab, TypeEnt* ent, int id) {
	Type* t = ent->type;
	Field* fld;
	const char* pn;
	int i, nf;

	fprintf(f, "T %d ", id);
	pn = prim_name(t);
	if (pn) {
		fprintf(f, "%s\n", pn);
		return;
	}
	switch (t->kind) {
	case TyPtr:
		fprintf(f, "ptr %d %d %d\n", tytab_id(tab, t->base), t->is_readonly, t->is_poly);
		break;
	case TyArray:
		fprintf(f, "array %d %" PRId64 " %d\n", tytab_id(tab, t->base), (int64_t)t->len,
			t->is_readonly);
		break;
	case TyFunc:
		fprintf(f, "func %d %d %d", tytab_id(tab, t->base), t->params_len, t->is_varargs);
		for (i = 0; i < t->params_len; i++)
			fprintf(f, " %d", tytab_id(tab, t->params[i]));
		for (i = 0; i < t->params_len; i++)
			fprintf(f, " %d",
				t->param_array ? (unsigned char)t->param_array[i] : 0);
		for (i = 0; i < t->params_len; i++)
			fprintf(f, " %" PRId64,
				t->param_fixed_len ? t->param_fixed_len[i] : (int64_t)-1);
		fputc('\n', f);
		break;
	case TyStruct:
	case TyUnion:
	case TyEnum:
		if (t->kind == TyStruct)
			fputs("struct ", f);
		else if (t->kind == TyUnion)
			fputs("union ", f);
		else
			fputs("enum ", f);
		write_tok(f, t->tag);
		if (ent->stub) {
			/* Keep size/align so importers can lay out by-value embeds;
			 * fields stay omitted (complete=0). */
			fprintf(f, " %d %d 0 %d ", t->size, t->align > 0 ? t->align : 1,
				t->pkg_private);
			write_tok(f, t->pkg_root);
			fprintf(f, " %d %d %d %d 0\n", t->is_ranged, t->is_tuple, t->is_readonly,
				t->is_poly);
			break;
		}
		nf = 0;
		for (fld = t->fields; fld; fld = fld->next)
			nf++;
		fprintf(f, " %d %d %d %d ", t->size, t->align, t->complete, t->pkg_private);
		write_tok(f, t->pkg_root);
		fprintf(f, " %d %d %d %d %d", t->is_ranged, t->is_tuple, t->is_readonly, t->is_poly,
			nf);
		if (t->is_ranged)
			fprintf(f, " %d", tytab_id(tab, t->base));
		for (fld = t->fields; fld; fld = fld->next) {
			fputc(' ', f);
			write_tok(f, fld->name);
			fprintf(f, " %d %d %d", tytab_id(tab, fld->type), fld->offset,
				fld->pkg_private);
		}
		fputc('\n', f);
		break;
	default:
		fprintf(f, "void\n");
		break;
	}
}

static void
write_sym(FILE* f, TypeTab* tab, Symbol* s) {
	fprintf(f, "S %s ", sk_name(s->kind));
	write_tok(f, s->name);
	fprintf(f, " %s %d %d %d ", st_name(s->storage), tytab_id(tab, s->type), s->is_overload,
		s->is_method);
	write_tok(f, s->linkname);
	fputc(' ', f);
	write_tok(f, s->recv_tag);
	fprintf(f, " %" PRId64 " 1\n", (int64_t)s->int_val);
}

int
export_write_pkg(Compiler* c, const char* pkg_dir, const char* path) {
	TypeTab types = {0};
	SymTab syms = {0};
	Symbol* s;
	FILE* f;
	int i;

	if (c == NULL || pkg_dir == NULL || path == NULL)
		return 1;
	for (s = c->syms.symbols; s; s = s->next) {
		if (!sym_exportable(s, pkg_dir))
			continue;
		symtab_add(&syms, s);
	}
	for (i = 0; i < syms.len; i++)
		collect_sym_types(c, &types, &syms, syms.syms[i], pkg_dir);

	if (mkdir_parents(path) != 0)
		return 1;
	f = fopen(path, "wb");
	if (f == NULL)
		return 1;
	fprintf(f, "modc-export %d\n", ExportVer);
	fprintf(f, "pkg ");
	write_tok(f, pkg_dir);
	fputc('\n', f);
	for (i = 0; i < types.len; i++)
		write_type(f, &types, &types.ents[i], i + 1);
	for (i = 0; i < syms.len; i++)
		write_sym(f, &types, syms.syms[i]);
	fclose(f);
	free(types.ents);
	free(syms.syms);
	return 0;
}

/* ---- load ---- */

static char*
read_tok(char** pp) {
	char *p, *start;

	p = *pp;
	while (*p && isspace((unsigned char)*p))
		p++;
	if (*p == 0) {
		*pp = p;
		return NULL;
	}
	start = p;
	while (*p && !isspace((unsigned char)*p))
		p++;
	if (*p) {
		*p = 0;
		p++;
	}
	*pp = p;
	return start;
}

static int
tok_is_dash(const char* s) {
	return s == NULL || strcmp(s, "-") == 0;
}

typedef struct {
	char* kind;
	char* rest; /* remaining payload after kind token; owned line slice */
	Type* type;
	int id;
	int reused; /* map[id] already points at a pre-existing Compiler type */
} LoadTy;

typedef struct {
	int kind;
	char* name;
	int storage;
	int type_id;
	int is_overload;
	int is_method;
	char* linkname;
	char* recv_tag;
	int64_t int_val;
} LoadSym;

static Type*
map_ty(Type** map, int max_id, int id) {
	if (id <= 0 || id > max_id || map == NULL)
		return NULL;
	return map[id];
}

static int
is_synth_tag(const char* tag) {
	if (tag == NULL || tag[0] == 0)
		return 0;
	return strncmp(tag, "__Ranged", 8) == 0 || strncmp(tag, "__Tuple", 7) == 0;
}

static int
is_aggr_kind_name(const char* kind) {
	return kind && (strcmp(kind, "struct") == 0 || strcmp(kind, "union") == 0 ||
			strcmp(kind, "enum") == 0);
}

/* Non-destructive first-token copy (export_load intern must not clobber rest). */
static int
peek_tok(const char* p, char* buf, size_t buf_len) {
	size_t n;

	if (p == NULL || buf == NULL || buf_len == 0)
		return 0;
	while (*p && isspace((unsigned char)*p))
		p++;
	if (*p == 0)
		return 0;
	n = 0;
	while (p[n] && !isspace((unsigned char)p[n]))
		n++;
	if (n >= buf_len)
		n = buf_len - 1;
	memcpy(buf, p, n);
	buf[n] = 0;
	return 1;
}

static void
free_fields(Type* t) {
	Field *f, *n;

	if (t == NULL)
		return;
	for (f = t->fields; f; f = n) {
		n = f->next;
		free(f->name);
		free(f);
	}
	t->fields = NULL;
}

static void
fill_ranged_inplace(Compiler* c, Type* t, Type* elem, int readonly, int poly) {
	Field *ptr, *len, *cap;
	static int next;

	if (elem == NULL)
		elem = c->types.type_void;
	readonly = readonly ? 1 : 0;
	poly = poly ? 1 : 0;
	free_fields(t);
	free(t->tag);
	t->kind = TyStruct;
	t->is_ranged = 1;
	t->is_tuple = 0;
	t->is_readonly = readonly;
	t->is_poly = poly;
	t->base = elem;
	t->tag = xmalloc(32);
	snprintf(t->tag, 32, "__Ranged%d", next++);
	ptr = xmalloc(sizeof(*ptr));
	ptr->name = xstrdup("ptr");
	ptr->type = type_ptr(c, elem);
	if (readonly || poly)
		ptr->type->is_readonly = 1;
	len = xmalloc(sizeof(*len));
	len->name = xstrdup("len");
	len->type = c->types.type_ullong;
	cap = xmalloc(sizeof(*cap));
	cap->name = xstrdup("cap");
	cap->type = c->types.type_ullong;
	ptr->next = len;
	len->next = cap;
	t->fields = ptr;
	type_layout(c, t);
}

static void
fill_tuple_inplace(Compiler* c, Type* t, Type** elts, int n) {
	Field *f, **tail;
	char fname[16];
	int i;
	static int nextid;

	free_fields(t);
	free(t->tag);
	t->kind = TyStruct;
	t->is_ranged = 0;
	t->is_tuple = 1;
	t->is_readonly = 0;
	t->is_poly = 0;
	t->base = NULL;
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
}

static int
fill_type_payload(Compiler* c, Type** map, int max_id, LoadTy* lt) {
	Type* t = lt->type;
	char* p = lt->rest;
	char* tok;
	Field *f, **fp;
	int i, nf, id, ro, poly, va, nparams, complete, pkg_priv, ranged, tuple;
	int64_t len, flen;
	Type* base;
	Type** elts;

	if (strcmp(lt->kind, "ptr") == 0) {
		tok = read_tok(&p);
		id = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		ro = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		poly = tok ? atoi(tok) : 0;
		t->kind = TyPtr;
		t->base = map_ty(map, max_id, id);
		t->is_readonly = ro;
		t->is_poly = poly;
		t->size = 8;
		t->align = 8;
		t->complete = 1;
		t->laid_out = 1;
		return 0;
	}
	if (strcmp(lt->kind, "array") == 0) {
		tok = read_tok(&p);
		id = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		len = tok ? (int64_t)strtoll(tok, NULL, 10) : -1;
		tok = read_tok(&p);
		ro = tok ? atoi(tok) : 0;
		t->kind = TyArray;
		t->base = map_ty(map, max_id, id);
		t->len = len;
		t->is_readonly = ro;
		if (len >= 0 && t->base && t->base->size > 0) {
			t->size = (int)(len * t->base->size);
			t->align = t->base->align > 0 ? t->base->align : 1;
			t->complete = 1;
			t->laid_out = 1;
		}
		return 0;
	}
	if (strcmp(lt->kind, "func") == 0) {
		tok = read_tok(&p);
		id = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		nparams = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		va = tok ? atoi(tok) : 0;
		t->kind = TyFunc;
		t->base = map_ty(map, max_id, id);
		t->params_len = nparams;
		t->is_varargs = va;
		t->size = 8;
		t->align = 8;
		t->complete = 1;
		t->laid_out = 1;
		if (nparams > 0) {
			t->params = xmalloc((size_t)nparams * sizeof(Type*));
			t->param_names = xmalloc((size_t)nparams * sizeof(char*));
			t->param_array = xmalloc((size_t)nparams);
			t->param_fixed_len = xmalloc((size_t)nparams * sizeof(int64_t));
			for (i = 0; i < nparams; i++) {
				tok = read_tok(&p);
				t->params[i] = map_ty(map, max_id, tok ? atoi(tok) : 0);
				t->param_names[i] = NULL;
			}
			for (i = 0; i < nparams; i++) {
				tok = read_tok(&p);
				t->param_array[i] = (char)(tok ? atoi(tok) : 0);
			}
			for (i = 0; i < nparams; i++) {
				tok = read_tok(&p);
				flen = tok ? (int64_t)strtoll(tok, NULL, 10) : -1;
				t->param_fixed_len[i] = flen;
			}
		}
		return 0;
	}
	if (strcmp(lt->kind, "struct") == 0 || strcmp(lt->kind, "union") == 0 ||
	    strcmp(lt->kind, "enum") == 0) {
		/* Already have a complete canonical type for this tag — keep it. */
		if (lt->reused && t && t->complete)
			return 0;

		if (strcmp(lt->kind, "struct") == 0)
			t->kind = TyStruct;
		else if (strcmp(lt->kind, "union") == 0)
			t->kind = TyUnion;
		else
			t->kind = TyEnum;
		tok = read_tok(&p);
		if (!lt->reused) {
			free(t->tag);
			t->tag = tok_is_dash(tok) ? NULL : xstrdup(tok);
		}
		tok = read_tok(&p);
		t->size = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		t->align = tok ? atoi(tok) : 1;
		tok = read_tok(&p);
		complete = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		pkg_priv = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		if (!lt->reused || t->pkg_root == NULL) {
			free(t->pkg_root);
			t->pkg_root = tok_is_dash(tok) ? NULL : xstrdup(tok);
		}
		tok = read_tok(&p);
		ranged = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		tuple = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		ro = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		poly = tok ? atoi(tok) : 0;
		tok = read_tok(&p);
		nf = tok ? atoi(tok) : 0;

		/* Fill in place; type_eq is structural for ranged/tuple shapes. */
		if (ranged) {
			tok = read_tok(&p);
			base = map_ty(map, max_id, tok ? atoi(tok) : 0);
			fill_ranged_inplace(c, t, base, ro, poly);
			return 0;
		}
		if (tuple) {
			elts = nf > 0 ? xmalloc((size_t)nf * sizeof(Type*)) : NULL;
			for (i = 0; i < nf; i++) {
				tok = read_tok(&p);
				(void)tok;
				tok = read_tok(&p);
				elts[i] = map_ty(map, max_id, tok ? atoi(tok) : 0);
				tok = read_tok(&p);
				(void)tok;
				tok = read_tok(&p);
				(void)tok;
			}
			fill_tuple_inplace(c, t, elts, nf);
			free(elts);
			return 0;
		}

		t->pkg_private = pkg_priv;
		t->is_ranged = 0;
		t->is_tuple = 0;
		t->is_readonly = ro;
		t->is_poly = poly;
		t->complete = complete;
		t->laid_out = complete || t->kind == TyEnum;
		if (lt->reused)
			free_fields(t);
		fp = &t->fields;
		for (i = 0; i < nf; i++) {
			f = xmalloc(sizeof(*f));
			tok = read_tok(&p);
			f->name = tok_is_dash(tok) ? NULL : xstrdup(tok);
			tok = read_tok(&p);
			f->type = map_ty(map, max_id, tok ? atoi(tok) : 0);
			tok = read_tok(&p);
			f->offset = tok ? atoi(tok) : 0;
			tok = read_tok(&p);
			f->pkg_private = tok ? atoi(tok) : 0;
			*fp = f;
			fp = &f->next;
		}
		return 0;
	}
	return 1;
}

static Span
export_span(const char* home) {
	Span sp;

	memset(&sp, 0, sizeof(sp));
	sp.file = home;
	sp.line = 1;
	sp.col = 1;
	sp.endcol = 1;
	return sp;
}

static void
ensure_tag(Compiler* c, Type* t, const char* home) {
	Symbol* s;
	Span sp;

	if (t == NULL || t->tag == NULL || t->tag[0] == 0)
		return;
	if (t->kind != TyStruct && t->kind != TyUnion && t->kind != TyEnum)
		return;
	if (is_synth_tag(t->tag))
		return;
	if (symbol_lookup_tag(c, t->tag))
		return;
	sp = export_span(home);
	s = symbol_define(c, t->tag, SkTag, t, t->pkg_private ? StStatic : StNone, sp);
	if (s) {
		s->defined = 1;
		s->node = NULL;
		s->header = 0;
		free(s->home);
		s->home = xstrdup(home);
	}
}

static void
finish_sym(Symbol* s, const char* home, LoadSym* ls) {
	if (s == NULL)
		return;
	s->defined = 1;
	s->node = NULL;
	s->header = 0;
	free(s->home);
	s->home = xstrdup(home);
	s->storage = ls->storage;
	s->int_val = ls->int_val;
	if (!tok_is_dash(ls->linkname)) {
		free(s->linkname);
		s->linkname = xstrdup(ls->linkname);
	}
	if (ls->is_method && !tok_is_dash(ls->recv_tag)) {
		s->is_method = 1;
		free(s->recv_tag);
		s->recv_tag = xstrdup(ls->recv_tag);
	}
	s->is_overload = ls->is_overload;
}

static int
load_one_sym(Compiler* c, Type** map, int max_id, const char* home, LoadSym* ls, int pass) {
	Type* t;
	Symbol* s;
	Span sp;
	int is_tag_td;

	t = map_ty(map, max_id, ls->type_id);
	sp = export_span(home);
	is_tag_td = (ls->kind == SkTag || ls->kind == SkTypedef);

	if (pass == 1) {
		if (!is_tag_td)
			return 0;
		if (ls->kind == SkTag) {
			ensure_tag(c, t, home);
			return 0;
		}
		s = symbol_define(c, ls->name, SkTypedef, t, StTypedef, sp);
		finish_sym(s, home, ls);
		return 0;
	}

	/* pass 2: funcs, methods, vars, enumcons */
	if (is_tag_td)
		return 0;
	if (ls->kind == SkFunc && ls->is_method) {
		Type* recv;
		Symbol* tag;
		char* old_in;

		tag = tok_is_dash(ls->recv_tag) ? NULL : symbol_lookup_tag(c, ls->recv_tag);
		if (tag && tag->type)
			recv = type_ptr(c, tag->type);
		else
			recv = t && is_func(t) && t->params_len > 0 ? t->params[0] : NULL;
		if (recv == NULL || tok_is_dash(ls->recv_tag))
			return 1;
		if (t && is_func(t) && t->params_len > 0)
			t->params[0] = recv;
		old_in = c->paths.infile;
		c->paths.infile = (char*)home;
		s = symbol_define_method(c, ls->name, recv, ls->recv_tag, t, ls->storage, sp);
		c->paths.infile = old_in;
		finish_sym(s, home, ls);
		if (s)
			s->recv_type = recv;
		return 0;
	}
	if (ls->kind == SkFunc) {
		s = symbol_define_func(c, ls->name, t, ls->storage, sp, ls->is_overload);
		finish_sym(s, home, ls);
		return 0;
	}
	if (ls->kind == SkVar || ls->kind == SkEnumCon) {
		s = symbol_define(c, ls->name, ls->kind, t, ls->storage, sp);
		finish_sym(s, home, ls);
		return 0;
	}
	return 0;
}

int
export_load(Compiler* c, const char* path) {
	char *text, *line, *next, *p, *tok;
	Type **map = NULL;
	LoadTy* ltypes = NULL;
	LoadSym* lsyms = NULL;
	int max_id = 0, map_cap = 0, ntypes = 0, nsyms = 0, cap_t = 0, cap_s = 0, ver, i, id;
	char pkg[HOST_PATH_MAX], home[HOST_PATH_MAX];
	size_t n;
	Type* prim;

	if (c == NULL || path == NULL)
		return 1;
	text = read_file(path, &n);
	if (text == NULL)
		return 1;
	pkg[0] = 0;
	line = text;
	while (line && *line) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		p = line;
		while (*p && isspace((unsigned char)*p))
			p++;
		if (*p == 0) {
			line = next;
			continue;
		}
		if (strncmp(p, "modc-export", 11) == 0) {
			p += 11;
			tok = read_tok(&p);
			ver = tok ? atoi(tok) : 0;
			if (ver != ExportVer) {
				free(text);
				return 1;
			}
		} else if (strncmp(p, "pkg ", 4) == 0) {
			p += 4;
			tok = read_tok(&p);
			if (tok_is_dash(tok))
				pkg[0] = 0;
			else
				snprintf(pkg, sizeof(pkg), "%s", tok);
		} else if (p[0] == 'T' && isspace((unsigned char)p[1])) {
			p += 1;
			tok = read_tok(&p);
			id = tok ? atoi(tok) : 0;
			tok = read_tok(&p);
			if (id <= 0 || tok == NULL) {
				free(text);
				free(map);
				free(ltypes);
				free(lsyms);
				return 1;
			}
			if (id >= map_cap) {
				int old = map_cap;
				map_cap = id + 16;
				map = xrealloc(map, (size_t)map_cap * sizeof(Type*));
				memset(map + old, 0, (size_t)(map_cap - old) * sizeof(Type*));
			}
			if (id > max_id)
				max_id = id;
			if (ntypes >= cap_t) {
				cap_t = cap_t ? cap_t * 2 : 64;
				ltypes = xrealloc(ltypes, (size_t)cap_t * sizeof(LoadTy));
			}
			prim = prim_match(c, tok);
			if (prim) {
				map[id] = prim;
				ltypes[ntypes].kind = NULL;
				ltypes[ntypes].rest = NULL;
				ltypes[ntypes].type = prim;
				ltypes[ntypes].id = id;
				ltypes[ntypes].reused = 1;
			} else {
				map[id] = type_new(c, TyVoid);
				ltypes[ntypes].kind = tok;
				ltypes[ntypes].rest = p;
				ltypes[ntypes].type = map[id];
				ltypes[ntypes].id = id;
				ltypes[ntypes].reused = 0;
			}
			ntypes++;
		} else if (p[0] == 'S' && isspace((unsigned char)p[1])) {
			LoadSym ls;
			char *k, *name, *st;

			memset(&ls, 0, sizeof(ls));
			p += 1;
			k = read_tok(&p);
			name = read_tok(&p);
			st = read_tok(&p);
			tok = read_tok(&p);
			ls.type_id = tok ? atoi(tok) : 0;
			tok = read_tok(&p);
			ls.is_overload = tok ? atoi(tok) : 0;
			tok = read_tok(&p);
			ls.is_method = tok ? atoi(tok) : 0;
			ls.linkname = read_tok(&p);
			ls.recv_tag = read_tok(&p);
			tok = read_tok(&p);
			ls.int_val = tok ? (int64_t)strtoll(tok, NULL, 10) : 0;
			ls.kind = parse_sk(k ? k : "");
			ls.name = name;
			ls.storage = parse_st(st ? st : "");
			if (ls.kind == SkNone || tok_is_dash(ls.name)) {
				free(text);
				free(map);
				free(ltypes);
				free(lsyms);
				return 1;
			}
			if (nsyms >= cap_s) {
				cap_s = cap_s ? cap_s * 2 : 64;
				lsyms = xrealloc(lsyms, (size_t)cap_s * sizeof(LoadSym));
			}
			lsyms[nsyms++] = ls;
		}
		line = next;
	}

	/* Intern named aggregates by tag before filling payloads. */
	for (i = 0; i < ntypes; i++) {
		char tagbuf[256];
		Symbol* s;

		if (ltypes[i].kind == NULL || !is_aggr_kind_name(ltypes[i].kind))
			continue;
		if (!peek_tok(ltypes[i].rest, tagbuf, sizeof(tagbuf)))
			continue;
		if (tok_is_dash(tagbuf) || is_synth_tag(tagbuf))
			continue;
		s = symbol_lookup_tag(c, tagbuf);
		if (s == NULL || s->type == NULL)
			continue;
		if (s->type->kind != TyStruct && s->type->kind != TyUnion &&
		    s->type->kind != TyEnum)
			continue;
		map[ltypes[i].id] = s->type;
		ltypes[i].type = s->type;
		ltypes[i].reused = 1;
	}

	for (i = 0; i < ntypes; i++) {
		if (ltypes[i].kind == NULL)
			continue;
		if (fill_type_payload(c, map, max_id, &ltypes[i]) != 0) {
			free(text);
			free(map);
			free(ltypes);
			free(lsyms);
			return 1;
		}
	}

	if (pkg[0])
		snprintf(home, sizeof(home), "%s/mod.mc", pkg);
	else
		snprintf(home, sizeof(home), "mod.mc");

	for (i = 0; i < ntypes; i++)
		ensure_tag(c, ltypes[i].type, home);
	for (i = 0; i < nsyms; i++)
		load_one_sym(c, map, max_id, home, &lsyms[i], 1);
	for (i = 0; i < nsyms; i++)
		load_one_sym(c, map, max_id, home, &lsyms[i], 2);

	free(text);
	free(map);
	free(ltypes);
	free(lsyms);
	return 0;
}
