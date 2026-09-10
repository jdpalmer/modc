/*
 * Symbol table: define, lookup, overloads, methods, mangling.
 *
 * Compilation pipeline: lex → pp → parse → [type check] → emit → QBE
 * (symbols are defined during parse; resolved again in type_expr.)
 */
#include "ast.h"
#include <ctype.h>

// Rebuild the symbol hash table at capacity cap from the linked symbol list.
static void
symbol_tab_rebuild(Compiler* c, int cap) {
	Symbol* s;
	unsigned i;

	free(c->symbol_tab);
	c->symbol_tab = xmalloc((size_t)cap * sizeof(Symbol*));
	c->symbol_tab_cap = cap;
	for (s = c->symbols; s; s = s->next) {
		i = str_hash(s->name) & (unsigned)(cap - 1);
		s->hash_next = c->symbol_tab[i];
		c->symbol_tab[i] = s;
	}
}

// Insert s into the symbol hash (s already linked on c->symbols).
static void
symbol_tab_add(Compiler* c, Symbol* s) {
	unsigned i;

	c->symbols_len++;
	if (c->symbol_tab_cap == 0 || c->symbols_len * 2 >= c->symbol_tab_cap) {
		symbol_tab_rebuild(c, c->symbol_tab_cap ? c->symbol_tab_cap * 2 : 1024);
		return;
	}
	i = str_hash(s->name) & (unsigned)(c->symbol_tab_cap - 1);
	s->hash_next = c->symbol_tab[i];
	c->symbol_tab[i] = s;
}

// True if s is visible in the current block / including file.
static int
symbol_visible(Compiler* c, Symbol* s) {
	if (s->block != 0) {
		if (s->dead)
			return 0;
		return s->block <= c->block;
	}
	if (s->header) {
		if (c->infile == NULL || s->home == NULL)
			return 0;
		return strcmp(c->infile, s->home) == 0;
	}
	return 1;
}

// Find the visible symbol by name in the current scope chain.
Symbol* symbol_lookup(Compiler* c, const char* name) {
	Symbol* s;
	unsigned i;

	if (name == NULL || c->symbol_tab_cap == 0)
		return NULL;
	i = str_hash(name) & (unsigned)(c->symbol_tab_cap - 1);
	for (s = c->symbol_tab[i]; s; s = s->hash_next) {
		if (s->hidden || s->dead || s->is_method || strcmp(s->name, name) != 0)
			continue;
		if (symbol_visible(c, s))
			return s;
	}
	return NULL;
}

// Find a visible struct/union/enum tag symbol.
Symbol* symbol_lookup_tag(Compiler* c, const char* name) {
	Symbol* s;
	unsigned i;

	if (name == NULL || c->symbol_tab_cap == 0)
		return NULL;
	i = str_hash(name) & (unsigned)(c->symbol_tab_cap - 1);
	for (s = c->symbol_tab[i]; s; s = s->hash_next) {
		if (s->hidden || s->dead || s->kind != SkTag || strcmp(s->name, name) != 0)
			continue;
		if (symbol_visible(c, s))
			return s;
	}
	return NULL;
}

// Record defining file; header symbols are scoped to their home file.
// Exception: headers included from bridge.mc are the package C surface (host
// ABI), so those decls are package-visible and link to the host object.
static void
symbol_set_home(Compiler* c, Symbol* s, Span sp) {
	const char* f;
	const char* home;
	const char* base;

	if (s->block == 0 && !user_source(c, sp)) {
		home = c->infile ? c->infile : "";
		base = strrchr(home, '/');
		base = base ? base + 1 : home;
#ifdef _WIN32
		{
			const char* b2 = strrchr(home, '\\');
			if (b2 && b2 + 1 > base)
				base = b2 + 1;
		}
#endif
		if (strcmp(base, "bridge.mc") == 0) {
			s->header = 0;
			s->home = xstrdup(home);
			return;
		}
		s->header = 1;
		s->home = xstrdup(home);
		return;
	}
	s->header = 0;
	f = sp.file ? sp.file : c->infile;
	s->home = xstrdup(f ? f : "");
}

// Insert or update a symbol, handling tags, labels, shadowing, and redefs.
Symbol* symbol_define(Compiler* c, const char* name, int kind, Type* t, int storage, Span sp) {
	Symbol *old, *s;

	if (kind == SkTag) {
		old = symbol_lookup_tag(c, name);
		if (old && old->block == c->block) {
			if (old->type && t && old->type != t && old->type->complete && t->complete)
				error_at(c, sp, "redefinition of %s", name);
			if (t)
				old->type = t;
			return old;
		}
		old = symbol_lookup(c, name);
		if (old && old->block == c->block && old->kind != SkTag)
			error_at(c, sp, "redefinition of %s", name);
		s = xmalloc(sizeof(*s));
		memset(s, 0, sizeof(*s));
		s->param_fixed_len = -1;
		s->name = xstrdup(name);
		s->kind = kind;
		s->type = t;
		s->storage = storage;
		s->block = c->block;
		s->span = sp;
		s->shadow = old;
		symbol_set_home(c, s, sp);
		s->next = c->symbols;
		c->symbols = s;
		symbol_tab_add(c, s);
		return s;
	}
	if (kind == SkLabel) {
		if (c->symbol_tab_cap) {
			unsigned i = str_hash(name) & (unsigned)(c->symbol_tab_cap - 1);
			for (s = c->symbol_tab[i]; s; s = s->hash_next) {
				if (s->kind == SkLabel && strcmp(s->name, name) == 0) {
					s->block = 0;
					return s;
				}
			}
		}
		s = xmalloc(sizeof(*s));
		memset(s, 0, sizeof(*s));
		s->param_fixed_len = -1;
		s->name = xstrdup(name);
		s->kind = kind;
		s->type = t;
		s->storage = storage;
		s->block = 0;
		s->span = sp;
		s->shadow = symbol_lookup(c, name);
		symbol_set_home(c, s, sp);
		s->next = c->symbols;
		c->symbols = s;
		symbol_tab_add(c, s);
		return s;
	}
	old = symbol_lookup(c, name);
	if (old && old->block == c->block) {
		if (old->kind == SkLabel && kind == SkLabel)
			return old;
		if (kind == SkTypedef && old->kind == SkTypedef && !user_source(c, sp))
			return old; /* headers: allow repeated typedefs (CRT/SDK) */
		if (kind == SkTypedef && old->kind == SkTypedef && old->type && !old->type->complete) {
			old->type = t;
			return old; /* package stub typedef → real type */
		}
		if (kind == SkTypedef && old->kind == SkTypedef && t && old->type == t)
			return old;
		if (kind == SkTypedef && old->kind == SkTag && t && old->type == t)
			return old;
		if (kind == SkFunc && old->kind == SkFunc) {
			old->type = t ? t : old->type;
			if (storage == StNone && old->storage == StExtern)
				old->storage = StNone;
			return old;
		}
		if (kind == SkVar && old->kind == SkVar && old->block == 0) {
			if (storage != StExtern)
				old->storage = storage;
			if (t)
				old->type = t;
			return old;
		}
		error_at(c, sp, "redefinition of %s", name);
		return old;
	}
	s = xmalloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	s->param_fixed_len = -1;
	s->name = xstrdup(name);
	s->kind = kind;
	s->type = t;
	s->storage = storage;
	s->block = c->block;
	s->span = sp;
	s->owner = (kind == SkVar) ? c->current_fn : NULL;
	s->shadow = old;
	symbol_set_home(c, s, sp);
	s->next = c->symbols;
	c->symbols = s;
	symbol_tab_add(c, s);
	if (kind == SkVar && old && old->kind == SkVar && c->current_fn && old->owner == c->current_fn && user_source(c, sp))
		error_at(c, sp, "'%s' shadows a previous declaration", name);
	return s;
}

// Append s to a mangled-name buffer, truncating if needed.
static void
mappend(char* buf, int* pos, int cap, const char* s) {
	int n;

	if (s == NULL || *pos >= cap - 1)
		return;
	n = (int)strlen(s);
	if (*pos + n >= cap)
		n = cap - 1 - *pos;
	memcpy(buf + *pos, s, n);
	*pos += n;
	buf[*pos] = 0;
}

// Encode a type as a short suffix for overload linker names.
static void
mangle_type(Type* t, char* buf, int* pos, int cap) {
	char tmp[32];
	const char* p;

	if (t == NULL) {
		mappend(buf, pos, cap, "z");
		return;
	}
	if (t->is_ranged) {
		snprintf(tmp, sizeof(tmp), "S%d", t->emit_id);
		mappend(buf, pos, cap, tmp);
		return;
	}
	switch (t->kind) {
	case TyVoid:
		mappend(buf, pos, cap, "v");
		break;
	case TyChar:
	case TyUChar:
		mappend(buf, pos, cap, "c");
		break;
	case TyShort:
		mappend(buf, pos, cap, "h");
		break;
	case TyUShort:
		mappend(buf, pos, cap, "H");
		break;
	case TyInt:
		mappend(buf, pos, cap, "i");
		break;
	case TyUInt:
		mappend(buf, pos, cap, "I");
		break;
	case TyLong:
		mappend(buf, pos, cap, "l");
		break;
	case TyULong:
		mappend(buf, pos, cap, "L");
		break;
	case TyLLong:
		mappend(buf, pos, cap, "q");
		break;
	case TyULLong:
		mappend(buf, pos, cap, "Q");
		break;
	case TyFloat:
		mappend(buf, pos, cap, "f");
		break;
	case TyDouble:
		mappend(buf, pos, cap, "d");
		break;
	case TyBool:
		mappend(buf, pos, cap, "b");
		break;
	case TyPtr:
		mappend(buf, pos, cap, "p");
		mangle_type(t->base, buf, pos, cap);
		break;
	case TyArray:
		mangle_type(t->base, buf, pos, cap);
		break;
	case TyStruct:
	case TyUnion:
		mappend(buf, pos, cap, "A");
		if (t->tag) {
			for (p = t->tag; *p; p++) {
				if (isalnum((unsigned char)*p)) {
					tmp[0] = *p;
					tmp[1] = 0;
					mappend(buf, pos, cap, tmp);
				}
			}
		} else {
			snprintf(tmp, sizeof(tmp), "%d", t->emit_id);
			mappend(buf, pos, cap, tmp);
		}
		break;
	case TyEnum:
		mappend(buf, pos, cap, "e");
		if (t->tag) {
			for (p = t->tag; *p; p++) {
				if (isalnum((unsigned char)*p)) {
					tmp[0] = *p;
					tmp[1] = 0;
					mappend(buf, pos, cap, tmp);
				}
			}
		} else {
			snprintf(tmp, sizeof(tmp), "%d", t->emit_id);
			mappend(buf, pos, cap, tmp);
		}
		break;
	case TyFunc:
		mappend(buf, pos, cap, "F");
		break;
	default:
		mappend(buf, pos, cap, "?");
		break;
	}
}

// Build name__mangled_param_types for overload discrimination at link time.
static void
mangle_func(const char* name, Type* fn, char* buf, int cap) {
	int i, pos;

	pos = 0;
	buf[0] = 0;
	mappend(buf, &pos, cap, name);
	mappend(buf, &pos, cap, "__");
	if (fn == NULL || !is_func(fn) || fn->params_len == 0)
		mappend(buf, &pos, cap, "v");
	else {
		for (i = 0; i < fn->params_len; i++) {
			if (i)
				mappend(buf, &pos, cap, "_");
			mangle_type(fn->params[i], buf, &pos, cap);
		}
	}
}

// Find an existing file-scope overload with the same name and signature.
static Symbol*
find_func_overload(Compiler* c, const char* name, Type* t) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next) {
		if (s->hidden)
			continue;
		if (s->kind == SkFunc && s->block == 0 && s->is_overload && strcmp(s->name, name) == 0 && type_eq(s->type, t))
			return s;
	}
	return NULL;
}

// True if linkname is already used by another visible symbol.
static int
linkname_taken(Compiler* c, const char* linkname) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next)
		if (!s->hidden && s->linkname && strcmp(s->linkname, linkname) == 0)
			return 1;
	return 0;
}

// Assign a unique mangled linkname to an overload entry.
static void
set_func_linkname(Compiler* c, Symbol* s, Span sp) {
	char buf[256];

	mangle_func(s->name, s->type, buf, sizeof(buf));
	if (linkname_taken(c, buf))
		error_at(c, sp, "linker name collision for overload %s", s->name);
	s->linkname = xstrdup(buf);
}

// Define or extend a function symbol; overloads get mangled linknames.
Symbol* symbol_define_func(Compiler* c, const char* name, Type* t, int storage, Span sp, int isoverload) {
	Symbol *old, *s;

	if (isoverload && c->block != 0) {
		error_at(c, sp, "overload functions must have file scope");
		isoverload = 0;
	}
	old = symbol_lookup(c, name);
	if (old && old->block == c->block) {
		if (isoverload || old->is_overload) {
			if (isoverload != old->is_overload) {
				error_at(c, sp, "cannot mix overload and non-overload declarations of %s", name);
				return old;
			}
			if (isoverload && type_eq(old->type, t)) {
				old->type = t ? t : old->type;
				if (storage == StNone && old->storage == StExtern)
					old->storage = StNone;
				return old;
			}
			if (isoverload) {
				if (find_func_overload(c, name, t))
					error_at(c, sp, "duplicate overload of %s", name);
				goto create;
			}
		}
		if (old->kind == SkFunc) {
			old->type = t ? t : old->type;
			if (storage == StNone && old->storage == StExtern)
				old->storage = StNone;
			return old;
		}
		error_at(c, sp, "redefinition of %s", name);
		return old;
	}
create:
	s = xmalloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	s->param_fixed_len = -1;
	s->name = xstrdup(name);
	s->kind = SkFunc;
	s->type = t;
	s->storage = storage;
	s->block = c->block;
	s->span = sp;
	s->is_overload = isoverload;
	s->shadow = old;
	symbol_set_home(c, s, sp);
	s->next = c->symbols;
	c->symbols = s;
	symbol_tab_add(c, s);
	if (isoverload)
		set_func_linkname(c, s, sp);
	return s;
}

// Append alphanumeric characters of s as lowercase (for method mangling).
static void
mangle_lower(const char* s, char* buf, int* pos, int cap) {
	const char* p;
	char ch[2];

	for (p = s; p && *p; p++) {
		if (!isalnum((unsigned char)*p))
			continue;
		ch[0] = (char)tolower((unsigned char)*p);
		ch[1] = 0;
		mappend(buf, pos, cap, ch);
	}
}

// Build pkg_typename_method linker symbol for a method.
static void
mangle_method(const char* pkg, const char* tag, const char* method, char* buf, int cap) {
	int pos;

	pos = 0;
	buf[0] = 0;
	if (pkg && pkg[0])
		mappend(buf, &pos, cap, pkg);
	if (tag && tag[0]) {
		if (pos > 0)
			mappend(buf, &pos, cap, "_");
		mangle_lower(tag, buf, &pos, cap);
	}
	if (method && method[0]) {
		if (pos > 0)
			mappend(buf, &pos, cap, "_");
		mappend(buf, &pos, cap, method);
	}
}


// Find a file-scope method on recv_tag with the given name.
Symbol* symbol_find_method(Compiler* c, const char* recv_tag, const char* name) {
	Symbol* s;

	if (recv_tag == NULL || name == NULL)
		return NULL;
	for (s = c->symbols; s; s = s->next) {
		if (s->hidden || s->dead || !s->is_method || s->block != 0)
			continue;
		if (s->recv_tag == NULL || strcmp(s->recv_tag, recv_tag) != 0)
			continue;
		if (strcmp(s->name, name) != 0)
			continue;
		if (symbol_visible(c, s))
			return s;
	}
	return NULL;
}

// Resolve a method call on recv_ty, including anonymous-embed upcast.
Symbol* symbol_resolve_method_call(Compiler* c, Type* recv_ty, const char* method, Span sp) {
	Type* ag;
	Symbol *s, *hit, *ts, *tagsym;

	if (method == NULL)
		return NULL;
	ag = recv_ty;
	if (is_ptr(ag))
		ag = ag->base;
	if (ag == NULL || !is_aggr(ag) || ag->tag == NULL) {
		error_at(c, sp, "method call requires a struct pointer receiver");
		return NULL;
	}
	s = symbol_find_method(c, ag->tag, method);
	if (s)
		return s;
	hit = NULL;
	for (ts = c->symbols; ts; ts = ts->next) {
		if (ts->hidden || ts->dead || !ts->is_method || ts->block != 0)
			continue;
		if (strcmp(ts->name, method) != 0 || ts->recv_tag == NULL)
			continue;
		tagsym = symbol_lookup_tag(c, ts->recv_tag);
		if (tagsym == NULL || tagsym->type == NULL || tagsym->type->kind != ag->kind)
			continue;
		if (anon_embed_offset(ag, tagsym->type, NULL) == 1) {
			if (hit)
				error_at(c, sp, "ambiguous method %s for receiver", method);
			hit = ts;
		}
	}
	if (hit == NULL)
		error_at(c, sp, "no method named %s for receiver type", method);
	return hit;
}

// Define a method; linkname is pkg_typename_method.
Symbol* symbol_define_method(Compiler* c, const char* name, Type* recv, const char* recv_tag, Type* t, int storage, Span sp) {
	Symbol *old, *s;
	char pkg[128], buf[256];
	Type* rt;

	if (c->block != 0) {
		error_at(c, sp, "methods must have file scope");
		return NULL;
	}
	if (recv == NULL || !is_ptr(recv) || recv->base == NULL || !is_aggr(recv->base) || recv_tag == NULL) {
		error_at(c, sp, "method receiver must be a struct or union pointer");
		return NULL;
	}
	rt = recv->base;
	if (rt->pkg_root && c->infile) {
		char root[1024];

		pkg_file_root(c->infile, root, sizeof(root));
		if (strcmp(root, rt->pkg_root) != 0)
			error_at(c, sp, "method must be defined in the same package as %s", recv_tag);
	}
	if (find_field(rt, name, NULL))
		error_at(c, sp, "method name %s conflicts with field on %s", name, recv_tag);
	old = symbol_find_method(c, recv_tag, name);
	if (old && t && old->type && type_eq(old->type, t)) {
		old->type = t;
		if (storage == StNone && old->storage == StExtern)
			old->storage = StNone;
		return old;
	}
	if (old)
		error_at(c, sp, "duplicate method %s on %s", name, recv_tag);
	pkg_mangle_from_file(c->infile ? c->infile : sp.file, pkg, sizeof(pkg));
	if (pkg[0] == 0)
		snprintf(pkg, sizeof(pkg), "main");
	mangle_method(pkg, recv_tag, name, buf, sizeof(buf));
	if (linkname_taken(c, buf))
		error_at(c, sp, "linker name collision for method %s", name);
	s = xmalloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	s->param_fixed_len = -1;
	s->name = xstrdup(name);
	s->kind = SkFunc;
	s->type = t;
	s->storage = storage;
	s->block = 0;
	s->span = sp;
	s->is_method = 1;
	s->recv_type = recv;
	s->recv_tag = xstrdup(recv_tag);
	s->linkname = xstrdup(buf);
	s->shadow = symbol_lookup(c, name);
	symbol_set_home(c, s, sp);
	s->next = c->symbols;
	c->symbols = s;
	symbol_tab_add(c, s);
	return s;
}

// True if name has at least one file-scope overload entry.
int symbol_has_overload(Compiler* c, const char* name) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next)
		if (!s->hidden && !s->dead && s->kind == SkFunc && s->block == 0 && s->is_overload && strcmp(s->name, name) == 0)
			return 1;
	return 0;
}

// Rank how well an argument matches a ranged parameter (exact > array/string).
static int
overload_ranged_score(Type* param, Type* argty, Node* expr) {
	if (param == NULL || argty == NULL)
		return 0;
	if (is_ranged(param) && is_ranged(argty) && type_eq(param, argty))
		return 2;
	if (is_ranged(param) && is_array(argty) && argty->len >= 0 && param->base && type_eq(param->base, argty->base))
		return 1;
	if (is_ranged(param) && expr && expr->kind == NdStr && param->base && (param->base->kind == TyChar || param->base->kind == TyUChar))
		return 1;
	return 0;
}

// Per-argument match quality for overload resolution (0 means no match).
static int
overload_arg_score(Compiler* c, Type* param, Type* argty, Node* expr) {
	Type *p, *a;
	int rs;

	if (param == NULL)
		return 0;
	p = decay(c, param);
	if (is_ptr(p) && is_null_expr(expr))
		return 1;
	if (argty == NULL) {
		rs = overload_ranged_score(param, argty, expr);
		return rs;
	}
	if (type_eq(param, argty))
		return 2;
	rs = overload_ranged_score(param, argty, expr);
	if (rs != 0)
		return rs;
	if (is_aggr(param) && is_aggr(argty) && !type_eq(param, argty) && anon_embed_offset(argty, param, NULL) == 1)
		return 1;
	a = decay(c, argty);
	p = decay(c, param);
	if (type_eq(p, a))
		return 2;
	if (a->kind == TyFloat && p->kind == TyDouble)
		return 1;
	if (is_int(a) && is_int(p)) {
		Type* ip;

		ip = promote(c, a);
		if (type_eq(p, ip))
			return 1;
		if (p->kind == TyLong && (ip->kind == TyInt || ip->kind == TyUInt))
			return 1;
		if (p->kind == TyULong && (ip->kind == TyInt || ip->kind == TyUInt))
			return 1;
		if (p->kind == TyLLong && (ip->kind == TyInt || ip->kind == TyUInt))
			return 1;
		if (p->kind == TyULLong && (ip->kind == TyInt || ip->kind == TyUInt))
			return 1;
		if (p->kind == TyUInt && ip->kind == TyInt)
			return 1;
	}
	if (is_ptr(p) && is_ptr(a) && type_eq(p->base, a->base))
		return 2;
	if (is_ptr(p) && is_ptr(a) && is_aggr(a->base) && is_aggr(p->base) && anon_embed_offset(a->base, p->base, NULL) == 1)
		return 1;
	if (is_ptr(p) && argty->kind == TyArray && type_eq(p->base, argty->base))
		return 1;
	return 0;
}

// Sum of per-arg scores; -1 if arity or any argument fails to match.
static int
overload_score(Compiler* c, Type* fn, Node** args, int args_len) {
	int i, score, need;

	if (fn == NULL || !is_func(fn))
		return -1;
	if (fn->is_varargs) {
		if (args_len < fn->params_len)
			return -1;
		need = fn->params_len;
	} else if (args_len != fn->params_len)
		return -1;
	else
		need = fn->params_len;
	score = 0;
	for (i = 0; i < need; i++) {
		Type* argty;
		int as;

		argty = args[i] ? args[i]->type : NULL;
		as = overload_arg_score(c, fn->params[i], argty, args[i]);
		if (as == 0)
			return -1;
		score += as;
	}
	return score;
}

// Pick the best overload by score; error on ambiguity or no match.
Symbol* symbol_resolve_overload(Compiler* c, const char* name, Node** args, int args_len, Span sp) {
	Symbol *s, *best;
	int score, bestscore;

	best = NULL;
	bestscore = -1;
	for (s = c->symbols; s; s = s->next) {
		if (s->kind != SkFunc || s->block != 0 || !s->is_overload || strcmp(s->name, name) != 0)
			continue;
		score = overload_score(c, s->type, args, args_len);
		if (score < 0)
			continue;
		if (score > bestscore) {
			best = s;
			bestscore = score;
		} else if (score == bestscore && best) {
			error_at(c, sp, "ambiguous call to overloaded %s", name);
			return best;
		}
	}
	if (best == NULL)
		error_at(c, sp, "no matching overload for %s", name);
	return best;
}

// Resolve range_count overload for R; set *elem_out to the element type.
Symbol* symbol_resolve_range_count(Compiler* c, Type* range_ty, Type** elem_out, Span sp) {
	Symbol *s, *best;
	int bestscore;
	Type* elem;

	best = NULL;
	bestscore = -1;
	elem = NULL;
	if (elem_out)
		*elem_out = NULL;
	if (range_ty == NULL)
		return NULL;
	for (s = c->symbols; s; s = s->next) {
		Type *fn, *p0, *p1, *el;
		int sc;

		if (s->kind != SkFunc || s->block != 0 || !s->is_overload || strcmp(s->name, "range_count") != 0)
			continue;
		fn = s->type;
		if (fn == NULL || !is_func(fn) || fn->params_len != 2)
			continue;
		p0 = fn->params[0];
		p1 = fn->params[1];
		if (p1 == NULL || !is_ptr(p1) || p1->base == NULL || !is_ptr(p1->base) || p1->base->base == NULL)
			continue;
		el = p1->base->base;
		sc = overload_arg_score(c, p0, range_ty, NULL);
		if (sc <= 0)
			continue;
		if (!is_int(fn->base))
			continue;
		if (sc > bestscore) {
			best = s;
			bestscore = sc;
			elem = el;
		} else if (sc == bestscore && best) {
			error_at(c, sp, "ambiguous call to overloaded range_count");
			if (elem_out)
				*elem_out = elem;
			return best;
		}
	}
	if (best == NULL)
		return NULL;
	if (elem_out)
		*elem_out = elem;
	return best;
}

// Best range_at(R, index) overload, or NULL if none (no error).
Symbol* symbol_resolve_range_at(Compiler* c, Type* range_ty, Type* elem, Span sp) {
	Symbol *s, *best;
	int bestscore;

	best = NULL;
	bestscore = -1;
	if (range_ty == NULL)
		return NULL;
	for (s = c->symbols; s; s = s->next) {
		Type *fn, *p0, *p1;
		int sc;

		if (s->kind != SkFunc || s->block != 0 || !s->is_overload || strcmp(s->name, "range_at") != 0)
			continue;
		fn = s->type;
		if (fn == NULL || !is_func(fn) || fn->params_len != 2)
			continue;
		p0 = fn->params[0];
		p1 = fn->params[1];
		if (p1 == NULL || !is_int(p1))
			continue;
		sc = overload_arg_score(c, p0, range_ty, NULL);
		if (sc <= 0)
			continue;
		if (elem && fn->base && !type_eq(fn->base, elem) && !conv_implicit_ok(c, elem, fn->base, NULL))
			continue;
		if (sc > bestscore) {
			best = s;
			bestscore = sc;
		} else if (sc == bestscore && best) {
			error_at(c, sp, "ambiguous call to overloaded range_at");
			return best;
		}
	}
	return best;
}

// Enter a nested scope (increment block counter).
void symbol_push_block(Compiler* c) {
	c->block++;
}

// Leave a scope; mark block locals dead but keep them for later analysis.
void symbol_pop_block(Compiler* c) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next) {
		if (s->block == c->block && s->kind != SkFunc)
			s->dead = 1;
	}
	c->block--;
}

// Hide file-scope static symbols when compiling another translation unit.
void symbol_hide_file_statics(Compiler* c) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next) {
		if (s->block != 0)
			continue;
		if (s->storage == StStatic)
			s->hidden = 1;
	}
}

