/*
 * Parser: Tok stream → AST + symbol definitions.
 *
 * Compilation pipeline: lex → pp → [parse] → type check → emit → QBE
 *
 * Recursive-descent with Pratt binary ops. Declarations and statements are
 * typed as they are built (type_expr), so the AST leaving parse_unit is ready
 * for type_check_unit and emit. Entry: parse_unit. Token cursor: peek / take.
 */
#include "ast.h"

static int switch_depth;
/* Header `const` before a declarator: apply is_readonly to the first pointer level. */
static int pending_pointee_const;

// Current token; never advances. Past EOF, returns the final TEof.
Tok* peek(Compiler* c) {
	if (c->pos >= c->tokens_len)
		return &c->tokens[c->tokens_len - 1];
	return &c->tokens[c->pos];
}

// Lookahead n tokens ahead of pos (0 == peek).
Tok* peekn(Compiler* c, int n) {
	int i;

	i = c->pos + n;
	if (i >= c->tokens_len)
		return &c->tokens[c->tokens_len - 1];
	return &c->tokens[i];
}

// Consume and return the current token (no-op at EOF).
Tok* take(Compiler* c) {
	Tok* t;

	t = peek(c);
	if (t->kind != TEof && c->pos < c->tokens_len)
		c->pos++;
	return t;
}

// True when the current token is the given punctuator.
int at(Compiler* c, int punct) {
	Tok* t = peek(c);
	return t->kind == TPunct && t->punct == punct;
}

// True when the current token is the given keyword.
int atkw(Compiler* c, int kw) {
	Tok* t = peek(c);
	return t->kind == TKw && t->kw == kw;
}

// Consume the punctuator if present; returns whether it matched.
int eat(Compiler* c, int punct) {
	if (at(c, punct)) {
		take(c);
		return 1;
	}
	return 0;
}

// Consume the keyword if present; returns whether it matched.
int eatkw(Compiler* c, int kw) {
	if (atkw(c, kw)) {
		take(c);
		return 1;
	}
	return 0;
}

// Require a punctuator or diagnose with a human-readable name.
static void
expect(Compiler* c, int punct, const char* what) {
	if (!eat(c, punct))
		error_tok(c, peek(c), "expected %s", what);
}

// Advance past tokens until ';' or matching '}' at the current brace depth (error recovery).
static void
skip_to_balance(Compiler* c) {
	int depth = 0;

	while (peek(c)->kind != TEof) {
		if (at(c, PLbrace)) {
			depth++;
			take(c);
		} else if (at(c, PRbrace)) {
			if (depth == 0)
				return;
			depth--;
			take(c);
		} else if (at(c, PSemi) && depth == 0) {
			take(c);
			return;
		} else
			take(c);
		if (c->fatal)
			return;
	}
}

// Skip a braced block after consuming its opening '{'.
static void
skip_braced(Compiler* c) {
	int depth;

	if (!eat(c, PLbrace))
		return;
	depth = 1;
	while (peek(c)->kind != TEof && depth > 0) {
		if (at(c, PLbrace))
			depth++;
		else if (at(c, PRbrace))
			depth--;
		take(c);
		if (c->fatal)
			return;
	}
}

/*
 * Skip one top-level declaration. Stops at ';' when depth is 0, or right after
 * a braced group returns to depth 0 (function body / aggregate init). For inits
 * the trailing ';' is still consumed when present; for function definitions
 * there may be none — do not keep scanning into the next declaration.
 */
static void
skip_toplevel_semi(Compiler* c) {
	int depth, saw_brace;

	depth = 0;
	saw_brace = 0;
	while (peek(c)->kind != TEof) {
		if (at(c, PLbrace)) {
			depth++;
			saw_brace = 1;
			take(c);
		} else if (at(c, PRbrace) && depth > 0) {
			depth--;
			take(c);
			if (depth == 0 && saw_brace) {
				if (at(c, PSemi))
					take(c);
				return;
			}
		} else if (at(c, PSemi) && depth == 0) {
			take(c);
			return;
		} else
			take(c);
		if (c->fatal)
			return;
	}
}

// Diagnose function prototypes in user .mc files (allowed only in headers).
static void reject_user_prototype(Compiler* c, Span sp) {
	if (user_source(c, sp))
		error_at(c, sp,
			 "function prototypes are not allowed in %%C user code (allowed in headers)");
}

// Find a prescan-registered function symbol matching name, type, and overload flag.
static Symbol*
find_prescan_func(Compiler* c, const char* name, Type* ty, int isoverload) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next) {
		if (s->hidden || s->dead || s->kind != SK_FUNC || s->block != 0 || s->is_method)
			continue;
		if (strcmp(s->name, name) != 0)
			continue;
		if (isoverload != s->is_overload)
			continue;
		if (!type_eq(s->type, ty))
			continue;
		return s;
	}
	return NULL;
}

// True when a token can start a typename in a cast or declaration.
int is_typename_tok(Compiler* c, Tok* t) {
	Symbol* s;

	if (t->kind == TKw) {
		switch (t->kw) {
		case K_void:
		case K_char:
		case K_short:
		case K_int:
		case K_long:
		case K_float:
		case K_double:
		case K_signed:
		case K_unsigned:
		case K_struct:
		case K_union:
		case K_enum:
		case K_bool:
		case K_const:
		case K_volatile:
		case K_restrict:
			return 1;
		default:
			return 0;
		}
	}
	if (t->kind == TIdent) {
		s = symbol_lookup(c, t->s);
		return s && (s->kind == SK_TYPEDEF || s->kind == SK_TAG);
	}
	return 0;
}

// True when the current token begins a typename.
static int
is_typename(Compiler* c) {
	return is_typename_tok(c, peek(c));
}

// True when the current token is a storage-class or overload specifier.
static int
is_storage(Compiler* c) {
	return atkw(c, K_typedef) || atkw(c, K_extern) || atkw(c, K_static) || atkw(c, K_auto) || atkw(c, K_register) || atkw(c, K_inline) || atkw(c, K_overload);
}

static Node* parse_expr(Compiler* c);
static Node* parse_comma_expr(Compiler* c);
static Node* parse_assign(Compiler* c);
static Node* parse_cond(Compiler* c);
static Node* parse_cast(Compiler* c);
static Node* parse_unary(Compiler* c);
static Node* parse_postfix(Compiler* c, Node* n);
static Node* parse_primary(Compiler* c);
static Node* parse_stmt(Compiler* c);
static Node* parse_compound(Compiler* c, int scoped);
static Node* parse_braced_body(Compiler* c, const char* what);
static Type* parse_typename(Compiler* c);
static Type* parse_declspec(Compiler* c, int* storage, int* saw_type);
static Type* parse_declarator(Compiler* c, Type* base, char** name, int abstract);
static int eat_vendor_attr(Compiler* c);
static int eat_pragma_op(Compiler* c);
static Initializer* parse_init(Compiler* c);
static void parse_decl_or_def(Compiler* c, int in_func);
static void parse_static_assert(Compiler* c);
static int peek_range_for(Compiler* c);
static Node* parse_range_for(Compiler* c, Span sp);
static int peek_tuple_type(Compiler* c);
static int peek_method_receiver(Compiler* c);
static int peek_method_decl(Compiler* c);
static Type* parse_method_declarator(Compiler* c, Type* ret, char** name, char** recv_name, Type** recv_ty, char** recv_tag);
static Type* parse_tuple_type(Compiler* c);
static Node* parse_tuple_lit(Compiler* c, Type* tuple, Span sp);
static int peek_destruct_decl(Compiler* c);
static int peek_auto_local(Compiler* c);
static int peek_for_init_decl(Compiler* c);
static Node* parse_for_init_decl(Compiler* c);
static Node* parse_destruct_decl(Compiler* c, Span sp, int allauto);
static void finish_array_from_init(Compiler* c, Type** pt, Initializer* in);
static Node* mknames(Symbol* s, Span sp);

// Parse static_assert / _Static_assert and evaluate the condition at compile time.
static void
parse_static_assert(Compiler* c) {
	Span sp;
	Node* cond;
	int64_t v;
	char* msg;

	sp = peek(c)->span;
	take(c); /* static_assert / _Static_assert */
	expect(c, PLparen, "'('");
	cond = type_expr(c, parse_cond(c));
	msg = NULL;
	if (eat(c, PComma)) {
		if (peek(c)->kind != TString) {
			error_tok(c, peek(c), "expected string literal in static_assert");
			skip_to_balance(c);
			return;
		}
		msg = take(c)->s;
	}
	expect(c, PRparen, "')'");
	expect(c, PSemi, "';'");
	if (!eval_const(c, cond, &v))
		error_at(c, sp, "static_assert expression is not a constant");
	else if (v == 0) {
		if (msg && msg[0])
			error_at(c, sp, "static_assert failed: \"%s\"", msg);
		else
			error_at(c, sp, "static_assert failed");
	}
}

// Placeholder type for nested declarator parsing passes.
static Type*
dummy_type(Compiler* c) {
	return type_new(c, TY_INT);
}

/* ---- types / declarators ---- */

// Parse a struct or union definition and lay out its fields.
static Type*
parse_struct(Compiler* c, int kind) {
	char* tag;
	Type* t;
	Field *f, **tail;
	Type *ft, *base;
	char* fname;
	int storage, saw;
	Span sp;

	tag = NULL;
	sp = peek(c)->span;
	while (eat_vendor_attr(c))
		;
	if (peek(c)->kind == TIdent) {
		tag = take(c)->s;
	}
	while (eat_vendor_attr(c))
		;
	t = type_struct(c, kind, tag, sp);
	if (!eat(c, PLbrace))
		return t;
	if (t->complete && t->fields) {
		error_at(c, sp, "redefinition of %s", tag ? tag : "struct");
	}
	tail = &t->fields;
	while (!at(c, PRbrace) && peek(c)->kind != TEof) {
		if (at(c, PSemi)) {
			take(c);
			continue;
		}
		storage = ST_NONE;
		saw = 0;
		base = parse_declspec(c, &storage, &saw);
		if (!saw)
			base = c->type_int;
		fname = NULL;
		if (at(c, PColon))
			ft = base;
		else
			ft = parse_declarator(c, base, &fname, 1);
		/* Bit-fields: headers only. Width is accepted; layout still uses base size. */
		if (eat(c, PColon)) {
			Node* w;

			if (user_source(c, peek(c)->span))
				error_tok(c, peek(c), "bit-fields are for headers only");
			w = parse_cond(c);
			(void)w;
		}
		for (;;) {
			f = xmalloc(sizeof(*f));
			f->name = fname ? xstrdup(fname) : NULL;
			f->type = ft;
			f->pkg_private = 0;
			if (storage == ST_STATIC) {
				if (!is_ptr(ft))
					error_at(c, sp,
						 "static struct fields must be pointers (package-private handles)");
				else
					f->pkg_private = 1;
			}
			*tail = f;
			tail = &f->next;
			if (!eat(c, PComma))
				break;
			if (user_source(c, peek(c)->span)) {
				error_tok(c, peek(c), "%%C requires one variable declaration per line");
				while (!at(c, PSemi) && !at(c, PRbrace) && peek(c)->kind != TEof)
					take(c);
				break;
			}
			fname = NULL;
			ft = parse_declarator(c, base, &fname, 1);
			if (eat(c, PColon)) {
				Node* w;

				w = parse_cond(c);
				(void)w;
			}
		}
		expect(c, PSemi, "';'");
		if (c->fatal)
			break;
	}
	expect(c, PRbrace, "'}'");
	t->complete = 1;
	if (tag && user_source(c, sp) && c->infile) {
		char root[1024];

		pkg_file_root(c->infile, root, sizeof(root));
		t->pkg_root = xstrdup(root);
	}
	type_layout(c, t);
	if (kind == TY_UNION && user_source(c, sp)) {
		Field* uf;
		int saw_ptr, saw_other;

		saw_ptr = 0;
		saw_other = 0;
		for (uf = t->fields; uf; uf = uf->next) {
			if (uf->type == NULL)
				continue;
			if (is_ptr(uf->type))
				saw_ptr = 1;
			else
				saw_other = 1;
		}
		if (saw_ptr && saw_other)
			error_at(c, sp,
				 "union must not mix pointers with non-pointer members in %%C user code (allowed in headers)");
	}
	return t;
}

// Parse an enum tag and its enumerator list.
static Type*
parse_enum(Compiler* c) {
	char* tag;
	char* enm;
	Type* et;
	int64_t val;
	Symbol* s;
	Node* n;
	Span sp;

	sp = peek(c)->span;
	tag = NULL;
	et = c->type_int;
	if (peek(c)->kind == TIdent)
		tag = take(c)->s;
	if (tag)
		et = type_struct(c, TY_ENUM, tag, sp);
	if (!eat(c, PLbrace))
		return et;
	val = 0;
	while (!at(c, PRbrace) && peek(c)->kind != TEof) {
		if (peek(c)->kind != TIdent) {
			error_tok(c, peek(c), "expected enumerator");
			break;
		}
		enm = take(c)->s;
		if (eat(c, PEq)) {
			n = parse_cond(c);
			n = type_expr(c, n);
			if (!eval_const(c, n, &val))
				error_tok(c, peek(c), "enumerator is not a constant");
		}
		s = symbol_define(c, enm, SK_ENUMCON, et, ST_NONE, peek(c)->span);
		s->int_val = val;
		val++;
		if (!eat(c, PComma))
			break;
	}
	expect(c, PRbrace, "'}'");
	if (tag) {
		et->complete = 1;
		et->size = 4;
		et->align = 4;
		et->laid_out = 1;
		return et;
	}
	return c->type_int;
}

// Consume a C-only keyword in user code and diagnose (headers may use it).
static int
eat_c_reject_kw(Compiler* c, int kw, const char* name) {
	Tok* t;

	if (!atkw(c, kw))
		return 0;
	t = peek(c);
	take(c);
	if (user_source(c, t->span))
		error_at(c, t->span, "%s is not used in %%C (allowed in headers)", name);
	return 1;
}

// Skip a balanced (...) group (used for __declspec(...) / __attribute__((...))).
static void
skip_paren_group(Compiler* c) {
	int depth;

	if (!eat(c, PLparen))
		return;
	depth = 1;
	while (depth > 0 && peek(c)->kind != TEof) {
		if (at(c, PLparen)) {
			take(c);
			depth++;
		} else if (at(c, PRparen)) {
			take(c);
			depth--;
		} else
			take(c);
	}
}

// True for MSVC/GCC vendor keywords (__declspec, __stdcall, …) skipped in decls.
static int
is_vendor_attr_ident(const char* s) {
	static const char* names[] = {
	    "__attribute__",
	    "__declspec",
	    "__pragma",
	    "_Pragma",
	    "__stdcall",
	    "__cdecl",
	    "__fastcall",
	    "__thiscall",
	    "__vectorcall",
	    "__unaligned",
	    "__forceinline",
	    "__inline",
	    "__w64",
	    "__ptr32",
	    "__ptr64",
	    "__sptr",
	    "__uptr",
	    "__clrcall",
	    "__noop",
	    "near",
	    "far",
	    "huge",
	    "_near",
	    "_far",
	    "_huge",
	    "__near",
	    "__far",
	    "__huge",
	    NULL,
	};
	int i;

	if (s == NULL)
		return 0;
	for (i = 0; names[i]; i++)
		if (strcmp(s, names[i]) == 0)
			return 1;
	return 0;
}

/*
 * Swallow MSVC/GNU calling-convention and attribute keywords in headers.
 * In user .mc sources they are rejected (headers only).
 */
static int
eat_vendor_attr(Compiler* c) {
	Tok* t;
	const char* name;

	t = peek(c);
	if (t->kind != TIdent || !is_vendor_attr_ident(t->s))
		return 0;
	name = t->s;
	if (user_source(c, t->span))
		error_at(c, t->span, "%s is for headers only", name);
	take(c);
	if (strcmp(name, "__attribute__") == 0 || strcmp(name, "__declspec") == 0 ||
	    strcmp(name, "__pragma") == 0 || strcmp(name, "_Pragma") == 0 ||
	    strcmp(name, "__noop") == 0)
		skip_paren_group(c);
	return 1;
}

// Parse declaration specifiers: storage, qualifiers, and base type.
static Type*
parse_declspec(Compiler* c, int* storage, int* saw_type) {
	int nlong, nshort, nsigned, nunsigned, nint, nchar, nvoid, nfloat, ndouble, nbool;
	Type* t;
	Symbol* s;
	Span signed_sp, long_sp;

	pending_pointee_const = 0;
	*storage = ST_NONE;
	*saw_type = 0;
	nlong = nshort = nsigned = nunsigned = 0;
	nint = nchar = nvoid = nfloat = ndouble = nbool = 0;
	signed_sp.file = NULL;
	long_sp.file = NULL;
	t = NULL;
	for (;;) {
		if (eat_vendor_attr(c))
			continue;
		if (eatkw(c, K_typedef)) {
			*storage = ST_TYPEDEF;
			continue;
		}
		if (eatkw(c, K_extern)) {
			if (*storage == ST_NONE)
				*storage = ST_EXTERN;
			continue;
		}
		if (eatkw(c, K_static)) {
			if (*storage == ST_NONE)
				*storage = ST_STATIC;
			continue;
		}
		if (eatkw(c, K_auto))
			error_tok(c, peek(c), "auto is only for initialized locals (auto x = expr)");
		if (atkw(c, K_const)) {
			Tok* ct;

			ct = peek(c);
			take(c);
			if (user_source(c, ct->span))
				error_at(c, ct->span, "const is not used in %%C (allowed in headers)");
			else
				pending_pointee_const = 1;
			continue;
		}
		if (eat_c_reject_kw(c, K_register, "register") || eat_c_reject_kw(c, K_inline, "inline") || eat_c_reject_kw(c, K_volatile, "volatile") || eat_c_reject_kw(c, K_restrict, "restrict"))
			continue;
		if (eatkw(c, K_void)) {
			nvoid++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_char)) {
			nchar++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_short)) {
			nshort++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_int)) {
			nint++;
			*saw_type = 1;
			continue;
		}
		if (atkw(c, K_long)) {
			if (long_sp.file == NULL)
				long_sp = peek(c)->span;
			take(c);
			nlong++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_float)) {
			nfloat++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_double)) {
			ndouble++;
			*saw_type = 1;
			continue;
		}
		if (atkw(c, K_signed)) {
			if (signed_sp.file == NULL)
				signed_sp = peek(c)->span;
			take(c);
			nsigned++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_unsigned)) {
			nunsigned++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_bool)) {
			nbool++;
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_struct)) {
			t = parse_struct(c, TY_STRUCT);
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_union)) {
			t = parse_struct(c, TY_UNION);
			*saw_type = 1;
			continue;
		}
		if (eatkw(c, K_enum)) {
			t = parse_enum(c);
			*saw_type = 1;
			continue;
		}
		if (peek(c)->kind == TIdent && peek(c)->s && strcmp(peek(c)->s, "Slice") == 0) {
			Span sp;

			sp = peek(c)->span;
			take(c);
			error_at(c, sp, "'Slice' is not a type");
			*saw_type = 1;
			continue;
		}
		/* MSVC __intN types (Windows SDK / CRT headers). */
		if (peek(c)->kind == TIdent && peek(c)->s && peek(c)->s[0] == '_') {
			const char* in = peek(c)->s;
			Type* it = NULL;

			if (strcmp(in, "__int64") == 0 || strcmp(in, "_int64") == 0)
				it = c->type_llong;
			else if (strcmp(in, "__int32") == 0 || strcmp(in, "_int32") == 0)
				it = c->type_int;
			else if (strcmp(in, "__int16") == 0 || strcmp(in, "_int16") == 0)
				it = c->type_short;
			else if (strcmp(in, "__int8") == 0 || strcmp(in, "_int8") == 0)
				it = c->type_char;
			if (it) {
				take(c);
				t = it;
				*saw_type = 1;
				continue;
			}
		}
		if (peek(c)->kind == TIdent && t == NULL && nvoid + nchar + nint + nlong + nshort + nfloat + ndouble + nbool + nsigned + nunsigned == 0) {
			s = symbol_lookup(c, peek(c)->s);
			if (s && (s->kind == SK_TYPEDEF || s->kind == SK_TAG)) {
				t = s->type;
				*saw_type = 1;
				take(c);
				continue;
			}
			if (s && s->kind != SK_TAG && s->kind != SK_TYPEDEF) {
				error_tok(c, peek(c), "%s is not a type", peek(c)->s);
				take(c);
				continue;
			}
		}
		break;
	}
	if (t)
		return t;
	if (nvoid)
		return c->type_void;
	if (nbool)
		return c->type_bool;
	if (nfloat)
		return c->type_float;
	if (ndouble)
		return c->type_double;
	if (nchar) {
		if (nsigned && user_source(c, signed_sp.file ? signed_sp : peek(c)->span))
			error_at(c, signed_sp.file ? signed_sp : peek(c)->span,
				 "%%C char is unsigned; signed char is not allowed");
		/* char and unsigned char are the same type */
		return c->type_char;
	}
	if (nshort)
		return nunsigned ? c->type_ushort : c->type_short;
	if (nlong >= 2) {
		/* long long — fixed 64-bit; headers only (stdint uses this for int64_t). */
		if (user_source(c, long_sp.file ? long_sp : peek(c)->span))
			error_at(c, long_sp.file ? long_sp : peek(c)->span,
				 "long long is not used in %%C; use int64_t");
		return nunsigned ? c->type_ullong : c->type_llong;
	}
	if (nlong == 1) {
		/* Host ABI long — headers only (Win32 LLP64 vs Unix LP64). */
		if (user_source(c, long_sp.file ? long_sp : peek(c)->span))
			error_at(c, long_sp.file ? long_sp : peek(c)->span,
				 "long is for headers; use int64_t");
		return nunsigned ? c->type_ulong : c->type_long;
	}
	if (nunsigned)
		return c->type_uint;
	if (nint || nsigned || *saw_type)
		return c->type_int;
	return c->type_int;
}

// True when '(' begins a nested declarator like (*fp)(int), not a param list.
static int
looks_nested(Compiler* c) {
	Tok* n;

	if (!at(c, PLparen))
		return 0;
	n = peekn(c, 1);
	if (n->kind == TPunct && n->punct == PRparen)
		return 0;
	if (n->kind == TPunct && (n->punct == PStar || n->punct == PLparen))
		return 1;
	if (n->kind == TIdent && !is_typename_tok(c, n))
		return 1;
	return 0;
}

static Type* parse_suffix(Compiler* c, Type* base);
static Type* parse_pointers(Compiler* c, Type* base);

// Parse a function parameter list and build a function type.
static Type*
parse_param_list(Compiler* c, Type* ret) {
	Type *params[64], *ty, *base;
	char *names[64], *nm;
	char was_arr[64];
	int64_t fixed_len[64];
	int n, va, storage, saw, i;

	n = 0;
	va = 0;
	if (atkw(c, K_void) && peekn(c, 1)->kind == TPunct && peekn(c, 1)->punct == PRparen) {
		take(c);
		return type_func(c, ret, NULL, 0, 0);
	}
	if (at(c, PRparen))
		return type_func(c, ret, NULL, 0, 0);
	while (!at(c, PRparen) && peek(c)->kind != TEof) {
		Span psp;

		if (at(c, PEllipsis)) {
			take(c);
			va = 1;
			break;
		}
		storage = ST_NONE;
		saw = 0;
		psp = peek(c)->span;
		base = parse_declspec(c, &storage, &saw);
		if (!saw) {
			if (user_source(c, psp))
				error_at(c, psp,
					 "parameter must have a type; %%C does not allow untyped parameters");
			base = c->type_int;
		}
		nm = NULL;
		ty = parse_declarator(c, base, &nm, 1);
		was_arr[n] = 0;
		fixed_len[n] = -1;
		if (is_ranged(ty)) {
			/* ranged array parameter: pass by value */
		} else if (is_array(ty)) {
			was_arr[n] = 1;
			if (user_source(c, psp) && ty->len >= 0)
				fixed_len[n] = ty->len;
			ty = type_ptr(c, ty->base);
			if (pending_pointee_const) {
				ty->is_readonly = 1;
				pending_pointee_const = 0;
			}
		} else if (is_func(ty))
			ty = type_ptr(c, ty);
		if (n < 64) {
			params[n] = ty;
			names[n] = nm;
			n++;
		}
		if (eat(c, PComma))
			continue;
		break;
	}
	ty = type_func(c, ret, params, n, va);
	for (i = 0; i < n; i++) {
		if (ty->param_names)
			ty->param_names[i] = names[i] ? xstrdup(names[i]) : NULL;
		if (ty->param_array)
			ty->param_array[i] = was_arr[i];
		if (ty->param_fixed_len)
			ty->param_fixed_len[i] = fixed_len[i];
	}
	return ty;
}

// Parse declarator suffixes: [..], [N], ranged [..], and (params).
static Type*
parse_suffix(Compiler* c, Type* base) {
	Node* n;
	int64_t len;

	if (eat(c, PLbrack)) {
		if (eat(c, PDotDot)) {
			expect(c, PRbrack, "']'");
			base = type_ranged(c, base);
			base = parse_pointers(c, base);
			return parse_suffix(c, base);
		}
		if (eat(c, PRbrack)) {
			base = parse_suffix(c, base);
			return type_array(c, base, -1);
		}
		n = parse_expr(c);
		n = type_expr(c, n);
		expect(c, PRbrack, "']'");
		if (!eval_const(c, n, &len) || len <= 0) {
			/* Headers: C_ASSERT / SAL often need sizeof of structs we only
			 * partially layout; keep parsing with a dummy length. */
			if (user_source(c, peek(c)->span))
				error_tok(c, peek(c), "array size must be a positive constant");
			len = 1;
		}
		base = parse_suffix(c, base);
		return type_array(c, base, len);
	}
	if (eat(c, PLparen)) {
		base = parse_param_list(c, base);
		expect(c, PRparen, "')'");
		base = parse_suffix(c, base);
		return base;
	}
	return base;
}

// Parse leading * declarators and optional pointee qualifiers.
static Type*
parse_pointers(Compiler* c, Type* base) {
	for (;;) {
		while (eat_vendor_attr(c))
			;
		if (!eat(c, PStar))
			break;
		while (eat_vendor_attr(c) || eat_c_reject_kw(c, K_const, "const") ||
		       eat_c_reject_kw(c, K_volatile, "volatile") ||
		       eat_c_reject_kw(c, K_restrict, "restrict"))
			;
		base = type_ptr(c, base);
		if (pending_pointee_const) {
			base->is_readonly = 1;
			pending_pointee_const = 0;
		}
	}
	return base;
}

// Parse a declarator after declspecs: *, name, nested (…), then [] / ().
static Type*
parse_declarator(Compiler* c, Type* base, char** name, int abstract) {
	Type* ty;
	int save, end;

	*name = NULL;
	base = parse_pointers(c, base);
	while (eat_vendor_attr(c))
		;
	if (looks_nested(c)) {
		save = c->pos;
		take(c); /* skip '(' */
		{
			char* dummy = NULL;
			int save_pc = pending_pointee_const;

			parse_declarator(c, dummy_type(c), &dummy, 1);
			pending_pointee_const = save_pc;
		}
		expect(c, PRparen, "')'");
		ty = parse_suffix(c, base);
		end = c->pos;
		c->pos = save;
		take(c);
		ty = parse_declarator(c, ty, name, abstract);
		expect(c, PRparen, "')'");
		c->pos = end;
		return ty;
	}
	if (peek(c)->kind != TIdent && at(c, PLbrack)) {
		ty = parse_suffix(c, base);
		if (peek(c)->kind == TIdent)
			*name = take(c)->s;
		else if (!abstract)
			error_tok(c, peek(c), "expected identifier");
		while (eat_vendor_attr(c))
			;
		return parse_suffix(c, ty);
	}
	if (peek(c)->kind == TIdent) {
		*name = take(c)->s;
		while (eat_vendor_attr(c))
			;
	} else if (!abstract)
		error_tok(c, peek(c), "expected identifier");
	return parse_suffix(c, base);
}

// Parse a typename for casts and sizeof (abstract declarator).
static Type*
parse_typename(Compiler* c) {
	int storage, saw;
	Type* base;
	char* name;

	storage = ST_NONE;
	saw = 0;
	base = parse_declspec(c, &storage, &saw);
	name = NULL;
	return parse_declarator(c, base, &name, 1);
}

// Probe whether '(' begins a tuple type (multi-type parenthesized list).
static int
peek_tuple_type(Compiler* c) {
	int save, r, n, save_pc;
	int storage, saw;
	Type *base, *ty;
	char* dummy;

	if (!at(c, PLparen))
		return 0;
	save = c->pos;
	save_pc = pending_pointee_const;
	take(c);
	n = 0;
	r = 0;
	while (!at(c, PRparen) && peek(c)->kind != TEof) {
		storage = ST_NONE;
		saw = 0;
		if (!is_typename(c) && !atkw(c, K_void)) {
			c->pos = save;
			pending_pointee_const = save_pc;
			return 0;
		}
		base = parse_declspec(c, &storage, &saw);
		if (!saw) {
			c->pos = save;
			pending_pointee_const = save_pc;
			return 0;
		}
		dummy = NULL;
		ty = parse_declarator(c, base, &dummy, 1);
		(void)ty;
		if (peek(c)->kind == TIdent) {
			c->pos = save;
			pending_pointee_const = save_pc;
			return 0;
		}
		n++;
		if (eat(c, PComma))
			continue;
		break;
	}
	if (!at(c, PRparen) || n == 0) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	take(c);
	r = peek(c)->kind == TIdent || at(c, PLparen);
	c->pos = save;
	pending_pointee_const = save_pc;
	return r;
}

// Parse receiver type T or T* without consuming the receiver identifier.
static Type*
parse_receiver_type(Compiler* c) {
	int storage, saw;
	Type* ty;

	storage = ST_NONE;
	saw = 0;
	ty = parse_declspec(c, &storage, &saw);
	if (!saw)
		error_tok(c, peek(c), "expected receiver type");
	return parse_pointers(c, ty);
}

// True when '(' begins (T *recv).method, not a tuple return type.
static int
peek_method_receiver(Compiler* c) {
	int save, save_pc;
	Type* ty;

	if (!at(c, PLparen))
		return 0;
	if (peek_tuple_type(c))
		return 0;
	save = c->pos;
	save_pc = pending_pointee_const;
	take(c);
	if (!is_typename(c) && !atkw(c, K_struct) && !atkw(c, K_union) && !atkw(c, K_enum)) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	ty = parse_receiver_type(c);
	if (!is_ptr(ty)) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	if (peek(c)->kind != TIdent) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	take(c);
	if (!at(c, PRparen)) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	take(c);
	if (!at(c, PDot)) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	take(c);
	if (peek(c)->kind != TIdent) {
		c->pos = save;
		pending_pointee_const = save_pc;
		return 0;
	}
	c->pos = save;
	pending_pointee_const = save_pc;
	return 1;
}

// True when the next tokens are optional *s then (T *recv).method.
static int
peek_method_decl(Compiler* c) {
	int save, save_pc, r;

	save = c->pos;
	save_pc = pending_pointee_const;
	while (at(c, PStar))
		take(c);
	r = peek_method_receiver(c);
	c->pos = save;
	pending_pointee_const = save_pc;
	return r;
}

// Parse (T *recv).method(params) after the return type has been read.
static Type*
parse_method_declarator(Compiler* c, Type* ret, char** name, char** recv_name, Type** recv_ty, char** recv_tag) {
	Type *ft, *fn, **params;
	char **param_names, *rname;
	const char* tag;
	int i, np;

	*recv_tag = NULL;
	expect(c, PLparen, "'('");
	ft = parse_receiver_type(c);
	if (!is_ptr(ft) || ft->base == NULL || !is_aggr(ft->base))
		error_tok(c, peek(c), "method receiver must be a struct or union pointer");
	tag = ft->base->tag;
	if (tag == NULL)
		error_tok(c, peek(c), "method receiver type needs a name");
	*recv_ty = ft;
	*recv_tag = xstrdup(tag);
	if (peek(c)->kind != TIdent)
		error_tok(c, peek(c), "expected receiver name");
	rname = take(c)->s;
	*recv_name = rname;
	expect(c, PRparen, "')'");
	expect(c, PDot, "'.'");
	if (peek(c)->kind != TIdent)
		error_tok(c, peek(c), "expected method name");
	*name = take(c)->s;
	expect(c, PLparen, "'('");
	ft = parse_param_list(c, ret);
	expect(c, PRparen, "')'");
	np = ft->params_len + 1;
	params = xmalloc((size_t)np * sizeof(Type*));
	param_names = xmalloc((size_t)np * sizeof(char*));
	params[0] = *recv_ty;
	param_names[0] = xstrdup(rname);
	for (i = 0; i < ft->params_len; i++) {
		params[i + 1] = ft->params[i];
		param_names[i + 1] = ft->param_names && ft->param_names[i] ? xstrdup(ft->param_names[i]) : NULL;
	}
	fn = type_func(c, ret, params, np, ft->is_varargs);
	for (i = 0; i < np; i++)
		fn->param_names[i] = param_names[i];
	free(params);
	free(param_names);
	return fn;
}

// Parse a tuple type from (T1, T2, …).
static Type*
parse_tuple_type(Compiler* c) {
	Type *elts[16], *base, *ty;
	int n, storage, saw;
	char* dummy;

	expect(c, PLparen, "'('");
	n = 0;
	while (!at(c, PRparen) && peek(c)->kind != TEof) {
		if (n >= 16) {
			error_tok(c, peek(c), "too many tuple elements");
			break;
		}
		storage = ST_NONE;
		saw = 0;
		base = parse_declspec(c, &storage, &saw);
		if (!saw)
			base = c->type_int;
		dummy = NULL;
		ty = parse_declarator(c, base, &dummy, 1);
		elts[n++] = ty;
		if (eat(c, PComma))
			continue;
		break;
	}
	expect(c, PRparen, "')'");
	return type_tuple(c, elts, n);
}

// Parse a tuple literal (a, b, …) matching a known tuple type.
static Node*
parse_tuple_lit(Compiler* c, Type* tuple, Span sp) {
	Node* n;
	Field* f;
	int i;

	n = node(NTupleLit, sp);
	n->type = tuple;
	expect(c, PLparen, "'('");
	for (f = tuple ? tuple->fields : NULL, i = 0; f; f = f->next, i++) {
		if (i > 0)
			expect(c, PComma, "','");
		node_add(n, type_expr(c, parse_assign(c)));
	}
	expect(c, PRparen, "')'");
	return type_expr(c, n);
}

typedef struct {
	char* name;
	Type* type;
} TupleBind;

// Parse (a, b) or (int x, char* y) binding lists for destructuring.
static int
parse_tuple_bindings(Compiler* c, TupleBind* out, int max, int allauto) {
	int n, storage, saw;
	Type* base;
	char* nm;

	expect(c, PLparen, "'('");
	n = 0;
	while (!at(c, PRparen) && peek(c)->kind != TEof) {
		if (n >= max) {
			error_tok(c, peek(c), "too many destructuring bindings");
			break;
		}
		if (allauto) {
			if (peek(c)->kind != TIdent) {
				error_tok(c, peek(c), "expected identifier");
				break;
			}
			out[n].name = take(c)->s;
			out[n].type = NULL;
		} else {
			storage = ST_NONE;
			saw = 0;
			base = parse_declspec(c, &storage, &saw);
			if (!saw)
				base = c->type_int;
			base = parse_pointers(c, base);
			if (peek(c)->kind != TIdent) {
				error_tok(c, peek(c), "expected binding name");
				break;
			}
			nm = take(c)->s;
			out[n].name = nm;
			out[n].type = base;
		}
		n++;
		if (eat(c, PComma))
			continue;
		break;
	}
	expect(c, PRparen, "')'");
	return n;
}

// Probe whether the next tokens are a tuple destructuring declaration.
static int
peek_destruct_decl(Compiler* c) {
	int save, r, n, storage, saw;
	Type* base;

	save = c->pos;
	if (atkw(c, K_auto)) {
		take(c);
		if (!at(c, PLparen)) {
			c->pos = save;
			return 0;
		}
		take(c);
		n = 0;
		while (!at(c, PRparen) && peek(c)->kind != TEof) {
			if (peek(c)->kind != TIdent) {
				c->pos = save;
				return 0;
			}
			take(c);
			n++;
			if (eat(c, PComma))
				continue;
			break;
		}
		if (!at(c, PRparen) || n == 0) {
			c->pos = save;
			return 0;
		}
		take(c);
		r = at(c, PEq);
		c->pos = save;
		return r;
	}
	if (!at(c, PLparen)) {
		c->pos = save;
		return 0;
	}
	take(c);
	n = 0;
	while (!at(c, PRparen) && peek(c)->kind != TEof) {
		if (!is_typename(c) && !atkw(c, K_void)) {
			c->pos = save;
			return 0;
		}
		storage = ST_NONE;
		saw = 0;
		base = parse_declspec(c, &storage, &saw);
		if (!saw) {
			c->pos = save;
			return 0;
		}
		base = parse_pointers(c, base);
		if (peek(c)->kind != TIdent) {
			c->pos = save;
			return 0;
		}
		take(c);
		n++;
		if (eat(c, PComma))
			continue;
		break;
	}
	if (!at(c, PRparen) || n == 0) {
		c->pos = save;
		return 0;
	}
	take(c);
	r = at(c, PEq);
	c->pos = save;
	return r;
}

// Probe for `auto name = …` local declaration syntax.
static int
peek_auto_local(Compiler* c) {
	int save, r;

	save = c->pos;
	if (!atkw(c, K_auto)) {
		c->pos = save;
		return 0;
	}
	take(c);
	if (peek(c)->kind != TIdent || at(c, PLparen)) {
		c->pos = save;
		return 0;
	}
	take(c);
	r = at(c, PEq);
	c->pos = save;
	return r;
}

// Parse (…) = rhs destructuring into locals via a temporary tuple.
static Node*
parse_destruct_decl(Compiler* c, Span sp, int allauto) {
	TupleBind binds[16];
	Node *rhs, *outer, *d, *dot;
	Symbol *tmp, *vsym;
	Type *tuplety, *vt;
	Field* f;
	int i, n, nf;
	static int ntmp;
	char tname[32];
	Initializer* in;

	if (allauto)
		eatkw(c, K_auto);
	n = parse_tuple_bindings(c, binds, 16, allauto);
	expect(c, PEq, "'='");
	rhs = type_expr(c, parse_assign(c));
	expect(c, PSemi, "';'");
	tuplety = rhs ? rhs->type : NULL;
	if (tuplety == NULL || !is_tuple(tuplety))
		error_at(c, sp, "destructuring requires a multi-return value");
	nf = 0;
	for (f = tuplety->fields; f; f = f->next)
		nf++;
	if (n != nf)
		error_at(c, sp, "destructuring arity mismatch");

	outer = node(NBlock, sp);
	outer->int_val = c->block;
	snprintf(tname, sizeof(tname), "__td%d", ntmp++);
	tmp = symbol_define(c, tname, SK_VAR, tuplety, ST_LOCAL, sp);
	d = node(NDecl, sp);
	d->symbol = tmp;
	d->type = tuplety;
	in = xmalloc(sizeof(*in));
	memset(in, 0, sizeof(*in));
	in->expr = rhs;
	d->init = in;
	node_add(outer, d);

	for (i = 0, f = tuplety->fields; f && i < n; i++, f = f->next) {
		vt = allauto ? f->type : binds[i].type;
		if (!allauto && vt && f->type && !conv_implicit_ok(c, vt, f->type, dot))
			error_at(c, sp, "destructuring type mismatch for %s", binds[i].name);
		vsym = symbol_define(c, binds[i].name, SK_VAR, vt ? vt : f->type, ST_LOCAL, sp);
		dot = node1(NDot, sp, mknames(tmp, sp));
		dot->s = xstrdup(f->name);
		dot = type_expr(c, dot);
		d = node(NDecl, sp);
		d->symbol = vsym;
		d->type = vsym->type;
		in = xmalloc(sizeof(*in));
		memset(in, 0, sizeof(*in));
		in->expr = dot;
		d->init = in;
		node_add(outer, d);
	}
	return outer;
}

// True when '(' begins (typename) for a cast, not a parenthesized expression.
static int
is_cast_lparen(Compiler* c) {
	int save, r;
	Tok* n;

	if (!at(c, PLparen))
		return 0;
	n = peekn(c, 1);
	if (!is_typename_tok(c, n))
		return 0;
	save = c->pos;
	take(c);
	/* typename then ) */
	{
		int storage, saw;
		char* nm = NULL;
		Type* b;
		storage = ST_NONE;
		saw = 0;
		b = parse_declspec(c, &storage, &saw);
		parse_declarator(c, b, &nm, 1);
		r = at(c, PRparen);
	}
	c->pos = save;
	return r;
}

// Build an untyped binary-op AST node.
static Node*
mkbin(int op, Span sp, Node* a, Node* b) {
	Node* n;

	n = node2(NBin, sp, a, b);
	n->op = op;
	return n;
}

// Lowercase one ASCII letter for case-insensitive suffix comparison.
static int
lit_lower(int ch) {
	if (ch >= 'A' && ch <= 'Z')
		return ch - 'A' + 'a';
	return ch;
}

// Case-insensitive exact match of a numeric literal suffix spelling.
static int
suf_is(const char* suf, const char* want) {
	int i;

	for (i = 0; want[i]; i++) {
		if (lit_lower((unsigned char)suf[i]) != lit_lower((unsigned char)want[i]))
			return 0;
	}
	return suf[i] == 0;
}

// Classify a numeric literal spelling: value, suffix, and ModC type.
static Type*
type_number_lit(Compiler* c, Span sp, const char* s, int64_t* out) {
	int i, isfloat, ishex, isbin;
	const char* suf;
	char num[128];
	unsigned long long uv;
	char* end;

	*out = 0;
	if (s == NULL || s[0] == 0)
		return c->type_int;

	i = 0;
	isfloat = 0;
	ishex = 0;
	isbin = 0;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		ishex = 1;
		i = 2;
		while (isxdigit((unsigned char)s[i]))
			i++;
	} else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B') && (s[2] == '0' || s[2] == '1')) {
		isbin = 1;
		i = 2;
		while (s[i] == '0' || s[i] == '1')
			i++;
	} else {
		while (isdigit((unsigned char)s[i]))
			i++;
		if (s[i] == '.') {
			isfloat = 1;
			i++;
			while (isdigit((unsigned char)s[i]))
				i++;
		}
		if (s[i] == 'e' || s[i] == 'E') {
			isfloat = 1;
			i++;
			if (s[i] == '+' || s[i] == '-')
				i++;
			while (isdigit((unsigned char)s[i]))
				i++;
		}
	}
	suf = s + i;
	if ((size_t)i >= sizeof(num))
		i = (int)sizeof(num) - 1;
	memcpy(num, s, (size_t)i);
	num[i] = 0;

	if (suf_is(suf, "ull") || suf_is(suf, "llu") || suf_is(suf, "ll")) {
		error_at(c, sp, "%%C does not support ll suffix; use l or ul (int64_t)");
		return c->type_llong;
	}
	if (suf_is(suf, "f")) {
		if (ishex || isbin) {
			error_at(c, sp, "invalid suffix on integer literal");
			return c->type_int;
		}
		return c->type_float;
	}
	if (isfloat) {
		if (suf[0] == 0)
			return c->type_double;
		if (suf_is(suf, "l")) {
			error_at(c, sp, "%%C does not support long double");
			return c->type_double;
		}
		error_at(c, sp, "invalid suffix on floating literal");
		return c->type_double;
	}
	/* integer value */
	if (isbin) {
		uv = 0;
		for (i = 2; s[i] == '0' || s[i] == '1'; i++)
			uv = (uv << 1) | (unsigned)(s[i] - '0');
		*out = (int64_t)uv;
	} else if (ishex) {
		uv = strtoull(num, &end, 16);
		*out = (int64_t)uv;
	} else {
		uv = strtoull(num, &end, 10);
		*out = (int64_t)uv;
	}

	if (suf[0] == 0) {
		if (*out >= -2147483647LL - 1 && *out <= 2147483647LL)
			return c->type_int;
		return c->type_llong; /* fixed 64-bit, not host long */
	}
	if (suf_is(suf, "ul") || suf_is(suf, "lu"))
		return c->type_ullong;
	if (suf_is(suf, "us"))
		return c->type_ushort;
	if (suf_is(suf, "u"))
		return c->type_uint;
	if (suf_is(suf, "l"))
		return c->type_llong;
	if (suf_is(suf, "s"))
		return c->type_short;
	error_at(c, sp, "invalid suffix on integer literal");
	return c->type_int;
}

/* ---- expressions ---- */

// Literals, identifiers, (expr), sizeof, and tuple literals.
static Node*
parse_primary(Compiler* c) {
	Tok* t;
	Node* n;
	Symbol* s;

	t = peek(c);
	if (t->kind == TNumber) {
		take(c);
		n = node(NLit, t->span);
		n->s = t->s;
		n->type = type_number_lit(c, t->span, t->s, &n->int_val);
		return n;
	}
	if (t->kind == TCharLit) {
		take(c);
		n = node(NLit, t->span);
		n->int_val = t->int_val;
		n->type = c->type_int;
		n->is_char_lit = 1;
		return n;
	}
	if (t->kind == TString) {
		char* acc;
		size_t len;
		acc = xstrdup(t->s);
		len = strlen(acc);
		take(c);
		while (peek(c)->kind == TString) {
			size_t n2 = strlen(peek(c)->s);
			acc = xrealloc(acc, len + n2 + 1);
			memcpy(acc + len, peek(c)->s, n2 + 1);
			len += n2;
			take(c);
		}
		n = node(NStr, t->span);
		n->s = acc;
		n->int_val = intern_str(c, acc);
		n->type = type_array(c, c->type_char, (int64_t)strlen(acc) + 1);
		n->is_immutable = 1;
		return n;
	}
	if (atkw(c, K_true) || atkw(c, K_false)) {
		int v = atkw(c, K_true);
		t = take(c);
		n = node(NLit, t->span);
		n->int_val = v;
		n->type = c->type_bool;
		return n;
	}
	if (t->kind == TIdent) {
		/* MSVC intrinsic used in SAL macros; treat as (void)0 in headers. */
		if (t->s && strcmp(t->s, "__noop") == 0) {
			take(c);
			if (at(c, PLparen))
				skip_paren_group(c);
			n = node(NLit, t->span);
			n->int_val = 0;
			n->type = c->type_int;
			return n;
		}
		take(c);
		n = node(NName, t->span);
		n->s = t->s;
		if (t->s && (strcmp(t->s, "ranged") == 0 || strcmp(t->s, "len") == 0) && at(c, PLparen))
			return n;
		s = symbol_lookup(c, t->s);
		n->symbol = s;
		if (s == NULL) {
			/*
			 * Headers (esp. Windows SDK inlines) often call CRT helpers whose
			 * declarations live in <string.h>/<wchar.h> not yet included —
			 * C89-style: invent an extern function placeholder.
			 */
			if (!user_source(c, t->span) && t->s) {
				Type* ft;

				ft = type_func(c, c->type_int, NULL, 0, 1);
				s = symbol_define(c, t->s, SK_FUNC, ft, ST_EXTERN, t->span);
				n->symbol = s;
				n->type = ft;
			} else
				error_tok(c, t, "undeclared identifier %s", t->s);
		} else {
			n->type = s->type;
			n->int_val = s->int_val;
			if (s->kind == SK_ENUMCON) {
				n->kind = NLit;
				n->int_val = s->int_val;
				n->type = s->type ? s->type : c->type_int;
			} else if (s->kind == SK_VAR || s->kind == SK_FUNC)
				n->is_lvalue = s->kind == SK_VAR;
		}
		return n;
	}
	if (eat(c, PLparen)) {
		if (is_typename_tok(c, peek(c))) {
			Type* ty;
			ty = parse_typename(c);
			expect(c, PRparen, "')'");
			n = node1(NCast, t->span, parse_cast(c));
			n->type = ty;
			return type_expr(c, n);
		}
		n = parse_expr(c);
		expect(c, PRparen, "')'");
		if (n)
			n->paren = 1;
		return n;
	}
	error_tok(c, t, "expected expression");
	take(c);
	n = node(NLit, t->span);
	n->type = c->type_int;
	return n;
}

// Postfix [], (), ., ->, ++/-- on a primary expression.
static Node*
parse_postfix(Compiler* c, Node* n) {
	Span sp;
	Node *idx, *call, *m;
	Tok* t;

	for (;;) {
		sp = peek(c)->span;
		if (eat(c, PLbrack)) {
			Node *lo, *hi, *sr;

			lo = NULL;
			hi = NULL;
			idx = NULL;
			if (eat(c, PDotDot)) {
				if (!at(c, PRbrack))
					hi = parse_expr(c);
			} else {
				idx = parse_expr(c);
				if (eat(c, PDotDot)) {
					lo = idx;
					idx = NULL;
					if (!at(c, PRbrack))
						hi = parse_expr(c);
				}
			}
			expect(c, PRbrack, "']'");
			n = type_expr(c, n);
			if (idx) {
				idx = type_expr(c, idx);
				n = node2(NIndex, sp, n, idx);
			} else {
				if (lo)
					lo = type_expr(c, lo);
				if (hi)
					hi = type_expr(c, hi);
				sr = node(NSubrange, sp);
				sr->a = n;
				sr->b = lo;
				sr->c = hi;
				n = sr;
			}
			n = type_expr(c, n);
			continue;
		}
		if (eat(c, PLparen)) {
			call = node1(NCall, sp, n);
			if (!at(c, PRparen)) {
				for (;;) {
					node_add(call, type_expr(c, parse_assign(c)));
					if (!eat(c, PComma))
						break;
				}
			}
			expect(c, PRparen, "')'");
			n = type_expr(c, call);
			continue;
		}
		if (at(c, PDot) || at(c, PArrow)) {
			int arrow = at(c, PArrow);

			take(c);
			if (peek(c)->kind != TIdent) {
				error_tok(c, peek(c), "expected field name");
				continue;
			}
			t = take(c);
			if (!arrow && at(c, PLparen)) {
				Node* call;

				call = node1(NCall, sp, node1(NMethod, sp, n));
				call->a->s = t->s;
				take(c);
				if (!at(c, PRparen)) {
					for (;;) {
						node_add(call, type_expr(c, parse_assign(c)));
						if (!eat(c, PComma))
							break;
					}
				}
				expect(c, PRparen, "')'");
				n = type_expr(c, call);
				continue;
			}
			m = node1(arrow ? NArrow : NDot, sp, n);
			m->s = t->s;
			n = type_expr(c, m);
			continue;
		}
		if (at(c, PPlusPlus) || at(c, PMinusMinus)) {
			int op = peek(c)->punct;
			take(c);
			n = node1(NPost, sp, type_expr(c, n));
			n->op = op;
			n = type_expr(c, n);
			continue;
		}
		break;
	}
	return n;
}

// Unary + - * & ! ~ ++/-- and cast/(expr) prefix forms.
static Node*
parse_unary(Compiler* c) {
	Tok* t;
	Node* n;
	Span sp;
	int op;

	sp = peek(c)->span;
	if (eatkw(c, K_sizeof)) {
		if (at(c, PLparen) && is_typename_tok(c, peekn(c, 1))) {
			take(c);
			n = node(NSizeofT, sp);
			n->type = parse_typename(c);
			expect(c, PRparen, "')'");
			return n;
		}
		n = node1(NSizeof, sp, parse_unary(c));
		n = type_expr(c, n);
		return n;
	}
	if (at(c, PPlusPlus) || at(c, PMinusMinus) || at(c, PAmp) || at(c, PStar) || at(c, PPlus) || at(c, PMinus) || at(c, PTilde) || at(c, PBang)) {
		op = peek(c)->punct;
		t = take(c);
		n = node1(NUn, t->span, parse_cast(c));
		n->op = op;
		if (op == PAmp)
			n->kind = NAddr;
		else if (op == PStar)
			n->kind = NDeref;
		return type_expr(c, n);
	}
	return parse_postfix(c, parse_primary(c));
}

// Cast expressions and delegate to unary when no leading (typename).
static Node*
parse_cast(Compiler* c) {
	Tok* t;
	Node* n;
	Type* ty;

	if (is_cast_lparen(c)) {
		t = take(c);
		ty = parse_typename(c);
		expect(c, PRparen, "')'");
		n = node1(NCast, t->span, parse_cast(c));
		n->type = ty;
		return type_expr(c, n);
	}
	return parse_unary(c);
}

// Map a punctuator to Pratt precedence (and right-associativity flag).
static int
is_binop(Tok* t, int* prec, int* rassoc) {
	*rassoc = 0;
	if (t->kind != TPunct)
		return 0;
	switch (t->punct) {
	case PStar:
	case PSlash:
	case PPercent:
		*prec = 13;
		return 1;
	case PPlus:
	case PMinus:
		*prec = 12;
		return 1;
	case PShl:
	case PShr:
		*prec = 11;
		return 1;
	case PLt:
	case PGt:
	case PLe:
	case PGe:
		*prec = 10;
		return 1;
	case PEqEq:
	case PBangEq:
		*prec = 9;
		return 1;
	case PAmp:
		*prec = 8;
		return 1;
	case PCaret:
		*prec = 7;
		return 1;
	case PPipe:
		*prec = 6;
		return 1;
	case PAmpAmp:
		*prec = 5;
		return 1;
	case PPipePipe:
		*prec = 4;
		return 1;
	default:
		return 0;
	}
}

// Binary operators via precedence climbing (Pratt parsing).
static Node*
parse_bin(Compiler* c, int minprec) {
	Node *left, *right;
	int prec, r, op;
	Span sp;

	left = parse_cast(c);
	for (;;) {
		if (!is_binop(peek(c), &prec, &r))
			break;
		if (prec < minprec)
			break;
		op = peek(c)->punct;
		sp = peek(c)->span;
		take(c);
		right = parse_bin(c, prec + 1);
		left = type_expr(c, mkbin(op, sp, type_expr(c, left), type_expr(c, right)));
	}
	return left;
}

// Ternary ?: on top of binary expressions. Nested ?: is rejected in user TUs.
static int
expr_has_cond(Node* n)
{
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NCond)
		return 1;
	if (expr_has_cond(n->a) || expr_has_cond(n->b) || expr_has_cond(n->c))
		return 1;
	for (i = 0; i < n->children_len; i++) {
		if (expr_has_cond(n->children[i]))
			return 1;
	}
	return 0;
}

// Parse ?: ternary; %C rejects nested ternaries in user source (use if/else).
static Node*
parse_cond(Compiler* c) {
	Node *n, *t, *e;
	Span sp;
	Node* a;

	n = parse_bin(c, 0);
	if (!at(c, PQuestion))
		return n;
	sp = peek(c)->span;
	take(c);
	a = n;
	t = parse_expr(c);
	expect(c, PColon, "':'");
	e = parse_assign(c);
	if (user_source(c, sp) && (expr_has_cond(a) || expr_has_cond(t) || expr_has_cond(e)))
		error_at(c, sp, "%%C does not allow nested ternary operators; use if/else");
	n = node(NCond, sp);
	n->a = type_expr(c, a);
	n->b = type_expr(c, t);
	n->c = type_expr(c, e);
	return type_expr(c, n);
}

// Assignment and compound-assignment expressions (right-associative).
static Node*
parse_assign(Compiler* c) {
	Node *left, *right;
	int op;
	Span sp;

	left = parse_cond(c);
	if (at(c, PEq) || at(c, PPlusEq) || at(c, PMinusEq) || at(c, PStarEq) || at(c, PSlashEq) || at(c, PPercentEq) || at(c, PAmpEq) || at(c, PPipeEq) || at(c, PCaretEq) || at(c, PShlEq) || at(c, PShrEq)) {
		op = peek(c)->punct;
		sp = peek(c)->span;
		take(c);
		right = parse_assign(c);
		left = node2(NAssign, sp, type_expr(c, left), type_expr(c, right));
		left->op = op;
		left = type_expr(c, left);
	}
	return left;
}

// Top-level expression entry (comma operator rejected in user .mc; headers OK).
static Node*
parse_expr(Compiler* c) {
	Node* n;
	int saw_comma;

	n = parse_assign(c);
	saw_comma = 0;
	while (eat(c, PComma)) {
		if (!saw_comma && user_source(c, peek(c)->span))
			error_tok(c, peek(c), "%%C does not support the comma operator");
		saw_comma = 1;
		n = parse_assign(c);
	}
	return n;
}

// Comma-separated expressions (used where C allows comma, e.g. for-init).
static Node*
parse_comma_expr(Compiler* c) {
	Node* n;
	Span sp;

	n = parse_assign(c);
	while (eat(c, PComma)) {
		sp = peek(c)->span;
		n = node2(NComma, sp, n, parse_assign(c));
		n = type_expr(c, n);
	}
	return n;
}

static Initializer* parse_init(Compiler* c);
static Initializer* parse_init_elem(Compiler* c);

// Parse one initializer element, optionally with a designator.
static Initializer*
parse_init_elem(Compiler* c) {
	Initializer* in;
	char* fields[32];
	int fields_len, save, des;
	int64_t idx;
	Node* ix;

	des = IDNone;
	fields_len = 0;
	idx = 0;
	if (at(c, PDot)) {
		des = IDFieldDot;
		take(c);
		for (;;) {
			if (peek(c)->kind != TIdent) {
				error_tok(c, peek(c), "expected field name after '.'");
				break;
			}
			if (fields_len >= 32) {
				error_tok(c, peek(c), "too many nested field designators");
				break;
			}
			fields[fields_len++] = take(c)->s;
			if (!eat(c, PDot))
				break;
		}
		expect(c, PEq, "'='");
	} else if (at(c, PLbrack)) {
		take(c);
		ix = parse_cond(c);
		ix = type_expr(c, ix);
		if (!eval_const(c, ix, &idx))
			error_tok(c, peek(c), "array designator index is not a constant");
		if (idx < 0)
			error_tok(c, peek(c), "negative array designator index");
		expect(c, PRbrack, "']'");
		if (at(c, PColon)) {
			error_tok(c, peek(c), "%%C uses C99 designated initializers ([n] =); not Plan 9 [n]:");
			take(c);
		} else
			expect(c, PEq, "'='");
		des = IDIndexEq;
	} else if (peek(c)->kind == TIdent && !at(c, PLbrace)) {
		save = c->pos;
		take(c);
		while (eat(c, PDot)) {
			if (peek(c)->kind != TIdent) {
				c->pos = save;
				break;
			}
			take(c);
		}
		if (at(c, PColon)) {
			error_tok(c, peek(c), "%%C uses C99 designated initializers (.field =); not Plan 9 field:");
			take(c);
			des = IDNone;
			fields_len = 0;
			/* continue parsing the init value to recover */
		} else
			c->pos = save;
	}
	in = parse_init(c);
	in->designator = des;
	if (des == IDFieldDot) {
		int i;

		in->fields = xmalloc(fields_len * sizeof(char*));
		in->fields_len = fields_len;
		for (i = 0; i < fields_len; i++)
			in->fields[i] = xstrdup(fields[i]);
	} else if (des == IDIndexEq)
		in->index = idx;
	return in;
}

// Parse a scalar or braced initializer list.
static Initializer*
parse_init(Compiler* c) {
	Initializer *in, *it;
	int cap;

	in = xmalloc(sizeof(*in));
	if (eat(c, PLbrace)) {
		in->is_list = 1;
		cap = 0;
		if (!at(c, PRbrace)) {
			for (;;) {
				if (in->items_len >= cap) {
					cap = cap ? cap * 2 : 4;
					in->items = xrealloc(in->items, cap * sizeof(Initializer));
				}
				it = parse_init_elem(c);
				in->items[in->items_len++] = *it;
				if (!eat(c, PComma))
					break;
				if (at(c, PRbrace))
					break;
			}
		}
		expect(c, PRbrace, "'}'");
		return in;
	}
	in->expr = type_expr(c, parse_assign(c));
	return in;
}

// Record a file-scope global declaration node for later emission.
static void add_global(Compiler* c, Node* n) {
	if (c->globals_len >= c->globals_cap) {
		c->globals_cap = c->globals_cap ? c->globals_cap * 2 : 16;
		c->globals = xrealloc(c->globals, c->globals_cap * sizeof(Node*));
	}
	c->globals[c->globals_len++] = n;
}

// Record a function definition node for later emission.
static void add_func(Compiler* c, Node* n) {
	if (c->funcs_len >= c->funcs_cap) {
		c->funcs_cap = c->funcs_cap ? c->funcs_cap * 2 : 16;
		c->funcs = xrealloc(c->funcs, c->funcs_cap * sizeof(Node*));
	}
	c->funcs[c->funcs_len++] = n;
}

// Parse a compound statement and optionally push/pop a symbol scope.
static Node*
parse_compound(Compiler* c, int scoped) {
	Node *blk, *s;
	Span sp;

	sp = peek(c)->span;
	expect(c, PLbrace, "'{'");
	if (!scoped)
		symbol_push_block(c);
	blk = node(NBlock, sp);
	blk->int_val = c->block;
	while (!at(c, PRbrace) && peek(c)->kind != TEof && !c->fatal) {
		s = parse_stmt(c);
		if (s)
			node_add(blk, s);
	}
	expect(c, PRbrace, "'}'");
	if (!scoped)
		symbol_pop_block(c);
	return blk;
}

static void parse_local_decl(Compiler* c, Node* blk);

// Build an NName node for a resolved symbol.
static Node*
mknames(Symbol* s, Span sp) {
	Node* n;

	n = node(NName, sp);
	n->s = s->name;
	n->symbol = s;
	n->type = s->type;
	n->is_lvalue = 1;
	return n;
}

// Build a typed integer literal AST node.
static Node*
mklitnode(Span sp, int64_t v, Type* t) {
	Node* n;

	n = node(NLit, sp);
	n->int_val = v;
	n->type = t;
	return n;
}

// Wrap an expression as a single-element initializer.
static Initializer*
mkexpr_init(Node* expr) {
	Initializer* in;

	in = xmalloc(sizeof(*in));
	memset(in, 0, sizeof(*in));
	in->expr = expr;
	return in;
}

// Build a binary-op node and run type_expr on it.
static Node*
typed_bin(Compiler* c, int op, Span sp, Node* a, Node* b) {
	Node* n;

	n = node2(NBin, sp, a, b);
	n->op = op;
	return type_expr(c, n);
}

// Build an assignment node and run type_expr on it.
static Node*
typed_assign(Compiler* c, Span sp, Node* lhs, Node* rhs) {
	Node* n;

	n = node2(NAssign, sp, lhs, rhs);
	n->op = PEq;
	return type_expr(c, n);
}

// Build an index/subscript node and run type_expr on it.
static Node*
typed_index(Compiler* c, Span sp, Node* base, Node* idx) {
	Node* n;

	n = node2(NIndex, sp, base, idx);
	return type_expr(c, n);
}

// Build a field-access node and run type_expr on it.
static Node*
typed_dot(Compiler* c, Span sp, Node* base, const char* field) {
	Node* n;

	n = node1(NDot, sp, base);
	n->s = xstrdup(field);
	return type_expr(c, n);
}

typedef struct {
	Type* elem;
	Node* count;
	Node* base;
	Symbol* tmpsym;
	int needtmp;
	int use_hooks;
	int use_at;
} RangeInfo;

// Build a call to a known function symbol with typed arguments.
static Node*
mkcall_resolved(Compiler* c, Span sp, Symbol* fn, Node** args, int args_len) {
	Node *nm, *call;
	int i;

	nm = node(NName, sp);
	nm->s = fn->name;
	nm->symbol = fn;
	nm->type = fn->type;
	call = node1(NCall, sp, nm);
	call->int_val = 1; /* already resolved */
	for (i = 0; i < args_len; i++)
		node_add(call, args[i]);
	return type_expr(c, call);
}

// Build an address-of node and run type_expr on it.
static Node*
typed_addr(Compiler* c, Span sp, Node* e) {
	Node* n;

	n = node1(NAddr, sp, e);
	return type_expr(c, n);
}

// Analyze a range-for iterable and fill RangeInfo for desugaring.
static int
range_for_info(Compiler* c, Span sp, Node* range, RangeInfo* ri) {
	Type *t, *elem;
	Symbol* cs;

	memset(ri, 0, sizeof(*ri));
	if (range == NULL || range->type == NULL) {
		error_at(c, sp, "expression is not iterable in range-for");
		return 0;
	}
	t = range->type;
	if (is_array(t)) {
		if (t->len < 0) {
			error_at(c, sp, "cannot range-for over an array of unknown bound");
			return 0;
		}
		if (!range->is_lvalue) {
			error_at(c, sp, "range-for over array requires an array lvalue");
			return 0;
		}
		ri->elem = t->base;
		ri->count = mklitnode(sp, t->len, c->type_ullong);
		ri->base = range;
		return 1;
	}
	if (is_ranged(t)) {
		ri->elem = t->base;
		if (range->is_lvalue)
			ri->base = range;
		else {
			ri->needtmp = 1;
			ri->tmpsym = NULL;
		}
		return 1;
	}
	if (is_ptr(t)) {
		if (range->kind == NName && range->symbol && range->symbol->array_param && range->symbol->param_fixed_len >= 0) {
			ri->elem = t->base;
			ri->count = mklitnode(sp, range->symbol->param_fixed_len, c->type_ullong);
			ri->base = range;
			return 1;
		}
		error_at(c, sp, "raw pointer is not iterable in range-for; use ranged(ptr, len) or a fixed array");
		return 0;
	}
	cs = symbol_resolve_range_count(c, t, &elem, sp);
	if (cs == NULL) {
		error_at(c, sp, "expression is not iterable in range-for");
		return 0;
	}
	ri->elem = elem;
	ri->use_hooks = 1;
	ri->use_at = symbol_resolve_range_at(c, t, elem, sp) != NULL;
	if (range->is_lvalue)
		ri->base = range;
	else {
		ri->needtmp = 1;
		ri->base = NULL;
	}
	return 1;
}

// Probe for `for (auto x : range)` or `for (T x : range)` syntax.
static int
peek_range_for(Compiler* c) {
	int save, r;
	int storage, saw;
	Type *base, *ty;
	char* name;

	save = c->pos;
	if (atkw(c, K_auto)) {
		take(c);
		if (at(c, PStar))
			take(c);
		if (peek(c)->kind != TIdent) {
			c->pos = save;
			return 0;
		}
		take(c);
		r = at(c, PColon);
		c->pos = save;
		return r;
	}
	if (!is_typename(c)) {
		c->pos = save;
		return 0;
	}
	storage = ST_NONE;
	saw = 0;
	base = parse_declspec(c, &storage, &saw);
	if (!saw) {
		c->pos = save;
		return 0;
	}
	name = NULL;
	ty = parse_declarator(c, base, &name, 0);
	(void)ty;
	if (name == NULL) {
		c->pos = save;
		return 0;
	}
	r = at(c, PColon);
	c->pos = save;
	return r;
}

// Desugar range-for into a conventional for loop over index/hooks.
static Node*
parse_range_for(Compiler* c, Span sp) {
	int isauto, byref;
	Type* vtype;
	char* vname;
	Node *range, *body, *outer, *f, *bodyblk, *xdecl, *elem;
	Node *args[2], *countcall, *atcall;
	Symbol *isym, *vsym, *rsym, *psym, *nsym, *cs, *ats;
	RangeInfo ri;
	static int ngen;
	char iname[32], rname[32], pname[32], nname[32];
	Initializer* xin;

	isauto = 0;
	byref = 0;
	vtype = NULL;
	vname = NULL;
	if (eatkw(c, K_auto)) {
		isauto = 1;
		if (eat(c, PStar))
			byref = 1;
		if (peek(c)->kind != TIdent)
			error_tok(c, peek(c), "expected range-for loop variable");
		else
			vname = take(c)->s;
	} else {
		int storage, saw;

		storage = ST_NONE;
		saw = 0;
		vtype = parse_declspec(c, &storage, &saw);
		if (!saw)
			vtype = c->type_int;
		vname = NULL;
		vtype = parse_declarator(c, vtype, &vname, 0);
		if (vname == NULL)
			error_tok(c, peek(c), "expected range-for loop variable");
		if (vtype && is_ptr(vtype))
			byref = 1;
	}
	expect(c, PColon, "':'");
	range = type_expr(c, parse_expr(c));
	expect(c, PRparen, "')'");
	if (vname == NULL)
		return node(NSkip, sp);
	if (!range_for_info(c, sp, range, &ri))
		return node(NSkip, sp);
	if (byref && ri.use_hooks && ri.use_at)
		error_at(c, sp,
			 "range-for pointer binding requires contiguous elements; use by-value for range_at-only types");
	if (isauto)
		vtype = byref ? type_ptr(c, ri.elem) : ri.elem;
	else if (byref) {
		if (vtype == NULL || !is_ptr(vtype) || ri.elem == NULL || !type_eq(vtype->base, ri.elem))
			error_at(c, sp, "range-for pointer variable type mismatch with element type");
	} else if (vtype && ri.elem && !conv_implicit_ok(c, vtype, ri.elem, NULL))
		error_at(c, sp, "range-for variable type mismatch with element type");

	snprintf(iname, sizeof(iname), "__ri%d", ngen++);
	snprintf(rname, sizeof(rname), "__rr%d", ngen++);
	snprintf(pname, sizeof(pname), "__rp%d", ngen++);
	snprintf(nname, sizeof(nname), "__rn%d", ngen++);

	symbol_push_block(c);
	outer = node(NBlock, sp);
	outer->int_val = c->block;

	if (ri.needtmp) {
		rsym = symbol_define(c, rname, SK_VAR, range->type, ST_LOCAL, sp);
		xdecl = node(NDecl, sp);
		xdecl->symbol = rsym;
		xdecl->type = range->type;
		xdecl->init = mkexpr_init(range);
		node_add(outer, xdecl);
		ri.base = mknames(rsym, sp);
	} else
		rsym = NULL;
	(void)rsym;

	if (ri.use_hooks) {
		Type* ptrty;

		ptrty = type_ptr(c, ri.elem);
		psym = symbol_define(c, pname, SK_VAR, ptrty, ST_LOCAL, sp);
		xdecl = node(NDecl, sp);
		xdecl->symbol = psym;
		xdecl->type = ptrty;
		node_add(outer, xdecl);

		cs = symbol_resolve_range_count(c, range->type, NULL, sp);
		args[0] = ri.base;
		args[1] = typed_addr(c, sp, mknames(psym, sp));
		countcall = mkcall_resolved(c, sp, cs, args, 2);

		nsym = symbol_define(c, nname, SK_VAR, countcall->type ? countcall->type : c->type_ullong,
				  ST_LOCAL, sp);
		xdecl = node(NDecl, sp);
		xdecl->symbol = nsym;
		xdecl->type = nsym->type;
		xdecl->init = mkexpr_init(countcall);
		node_add(outer, xdecl);
		ri.count = mknames(nsym, sp);

		ats = ri.use_at ? symbol_resolve_range_at(c, range->type, ri.elem, sp) : NULL;
	} else {
		psym = NULL;
		ats = NULL;
		nsym = NULL;
		if (is_ranged(range->type))
			ri.count = typed_dot(c, sp, ri.base, "len");
		else if (is_array(range->type))
			ri.count = mklitnode(sp, range->type->len, c->type_ullong);
	}

	isym = symbol_define(c, iname, SK_VAR, c->type_ullong, ST_LOCAL, sp);
	vsym = symbol_define(c, vname, SK_VAR, vtype ? vtype : ri.elem, ST_LOCAL, sp);
	body = parse_braced_body(c, "for");

	bodyblk = node(NBlock, sp);
	bodyblk->int_val = c->block;
	if (ri.use_hooks && ats) {
		args[0] = ri.base;
		args[1] = mknames(isym, sp);
		atcall = mkcall_resolved(c, sp, ats, args, 2);
		elem = atcall;
	} else if (ri.use_hooks) {
		elem = typed_index(c, sp, mknames(psym, sp), mknames(isym, sp));
	} else
		elem = typed_index(c, sp, ri.base, mknames(isym, sp));
	if (byref)
		elem = typed_addr(c, sp, elem);
	xdecl = node(NDecl, sp);
	xdecl->symbol = vsym;
	xdecl->type = vsym->type;
	xin = mkexpr_init(elem);
	xdecl->init = xin;
	node_add(bodyblk, xdecl);
	if (body)
		node_add(bodyblk, body);

	f = node(NFor, sp);
	f->a = typed_assign(c, sp, mknames(isym, sp), mklitnode(sp, 0, c->type_ullong));
	f->b = typed_bin(c, PLt, sp, mknames(isym, sp), ri.count);
	{
		Node* inc;

		inc = node1(NPost, sp, mknames(isym, sp));
		inc->op = PPlusPlus;
		f->c = type_expr(c, inc);
	}
	node_add(f, bodyblk);

	node_add(outer, f);
	symbol_pop_block(c);
	return outer;
}

static Node* parse_stmt(Compiler* c);

// True when a statement cannot fall through (return, break, goto, etc.).
static int
stmt_terminates(Node* n) {
	int i;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NBreak:
	case NContinue:
	case NReturn:
	case NGoto:
	case NFallthrough:
		return 1;
	case NBlock:
		for (i = n->children_len - 1; i >= 0; i--) {
			if (n->children[i]->kind == NDefer)
				continue;
			return stmt_terminates(n->children[i]);
		}
		return 0;
	case NIf:
		return stmt_terminates(n->b) && n->c != NULL && stmt_terminates(n->c);
	case NLabel:
		return stmt_terminates(n->a);
	default:
		return 0;
	}
}

// Track switch case arms for implicit-fallthrough diagnosis.
static void
walk_switch_fallthrough(Compiler* c, Node* n, int* has_code, int* terminated) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NCase || n->kind == NDefault) {
		if (*has_code && !*terminated && user_source(c, n->span))
			error_at(c, n->span,
				 "implicit fallthrough; insert 'fallthrough;' or 'break'");
		*has_code = 0;
		*terminated = 0;
		return;
	}
	if (n->kind == NSwitch) {
		/* nested switch checked when parsed */
		*has_code = 1;
		*terminated = 0;
		return;
	}
	if (n->kind == NBlock) {
		for (i = 0; i < n->children_len; i++)
			walk_switch_fallthrough(c, n->children[i], has_code, terminated);
		return;
	}
	if (n->kind == NDefer || n->kind == NSkip)
		return;
	*has_code = 1;
	*terminated = stmt_terminates(n);
}

// Check a switch body for missing fallthrough/break between cases.
static void
check_switch_fallthrough(Compiler* c, Node* body) {
	int has_code, terminated;

	if (body == NULL)
		return;
	has_code = 0;
	terminated = 0;
	walk_switch_fallthrough(c, body, &has_code, &terminated);
}

// Reject bare assignment in if/while conditions unless parenthesized.
static void
check_cond_assign(Compiler* c, Node* n) {
	if (n == NULL || !user_source(c, n->span))
		return;
	if (n->kind == NAssign && !n->paren)
		error_at(c, n->span,
			 "assignment in condition; use '==' or extra parentheses '((…))'");
}

// Parse a statement body, requiring braces in user code.
static Node*
parse_braced_body(Compiler* c, const char* what) {
	Span sp;

	sp = peek(c)->span;
	if (!at(c, PLbrace)) {
		if (user_source(c, sp))
			error_at(c, sp, "%%C requires braces around %s body", what);
		return parse_stmt(c);
	}
	return parse_compound(c, 0);
}

// Probe whether for-init is a declaration rather than an expression.
static int
peek_for_init_decl(Compiler* c) {
	int save, r;
	Tok* t;
	Symbol* s;

	save = c->pos;
	r = 0;
	if (peek_auto_local(c)) {
		r = 1;
		goto out;
	}
	if (atkw(c, K_typedef) || atkw(c, K_static_assert) || peek_destruct_decl(c))
		goto out;
	if (is_typename(c)) {
		r = 1;
		goto out;
	}
	if (is_storage(c)) {
		while (is_storage(c))
			take(c);
		r = is_typename(c) || atkw(c, K_void);
		goto out;
	}
	t = peek(c);
	if (t->kind == TIdent) {
		s = symbol_lookup(c, t->s);
		if (s && s->kind == SK_TYPEDEF)
			r = 1;
	}
out:
	c->pos = save;
	return r;
}

// True if in carries an expression or brace list (not an empty Initializer).
static int
init_present(Initializer* in) {
	return in != NULL && (in->expr != NULL || in->items_len > 0 || in->is_list);
}

// Error on uninitialized locals in user code (storage ST_LOCAL only).
static void
require_local_init(Compiler* c, Span sp, const char* name, int storage, Initializer* in) {
	if (storage != ST_LOCAL)
		return;
	if (!user_source(c, sp))
		return;
	if (init_present(in))
		return;
	error_at(c, sp, "uninitialized local '%s'; initialize at declaration",
		 name != NULL ? name : "");
}

// Parse a declaration in for-loop init position.
static Node*
parse_for_init_decl(Compiler* c) {
	int storage, saw, isoverload;
	Type *base, *ty;
	char* name;
	Symbol* s;
	Node* d;
	Span sp;
	Initializer* in;

	sp = peek(c)->span;
	if (peek_auto_local(c)) {
		eatkw(c, K_auto);
		if (peek(c)->kind != TIdent) {
			error_tok(c, peek(c), "expected identifier");
			return NULL;
		}
		name = take(c)->s;
		expect(c, PEq, "'='");
		d = node(NDecl, sp);
		d->init = parse_init(c);
		in = d->init;
		if (in == NULL || in->is_list || in->expr == NULL || in->expr->type == NULL) {
			error_at(c, sp, "auto requires an expression initializer with a known type");
			return d;
		}
		ty = decay(c, in->expr->type);
		s = symbol_define(c, name, SK_VAR, ty, ST_LOCAL, sp);
		d->symbol = s;
		d->type = ty;
		return d;
	}
	isoverload = 0;
	if (eatkw(c, K_overload))
		isoverload = 1;
	storage = ST_NONE;
	saw = 0;
	base = parse_declspec(c, &storage, &saw);
	if (!saw) {
		error_at(c, sp, "expected declaration in for loop");
		return NULL;
	}
	name = NULL;
	ty = parse_declarator(c, base, &name, 0);
	if (eat(c, PComma)) {
		error_tok(c, peek(c), "%%C requires one variable declaration per line");
		skip_to_balance(c);
		return NULL;
	}
	if (name == NULL) {
		error_at(c, sp, "expected declarator in for loop");
		return NULL;
	}
	if (!is_func(ty) && ty && (is_aggr(ty) || ty->kind == TY_ENUM) && !ty->complete)
		error_at(c, sp, "incomplete type %s", type_name(ty));
	if (storage == ST_TYPEDEF) {
		error_at(c, sp, "typedef not allowed in for loop");
		return NULL;
	}
	if (is_func(ty)) {
		reject_user_prototype(c, sp);
		error_at(c, sp, "function declaration not allowed in for loop");
		(void)isoverload;
		return NULL;
	}
	if (storage == ST_STATIC)
		storage = ST_STATIC;
	else
		storage = ST_LOCAL;
	s = symbol_define(c, name, SK_VAR, ty, storage, sp);
	d = node(NDecl, sp);
	d->symbol = s;
	d->type = ty;
	if (eat(c, PEq)) {
		d->init = parse_init(c);
		finish_array_from_init(c, &s->type, d->init);
		d->type = s->type;
		if (d->init && d->init->expr && s->type) {
			d->init->expr = apply_implicit_conversions(c, s->type, d->init->expr);
			check_implicit_conv(c, sp, s->type, d->init->expr);
		}
	}
	require_local_init(c, sp, name, storage, d->init);
	if (storage == ST_STATIC) {
		s->int_val = ++c->static_seq;
		add_global(c, d);
	}
	return d;
}

/* ---- statements ---- */

// Parse one statement: control flow, decls, or expression-stmt.
static Node*
parse_stmt(Compiler* c) {
	Tok* t;
	Node *n, *a, *b, *d;
	Span sp;
	Symbol* s;

	sp = peek(c)->span;
	if (at(c, PLbrace))
		return parse_compound(c, 0);
	if (atkw(c, K_static_assert)) {
		parse_static_assert(c);
		return node(NSkip, sp);
	}
	if (peek_destruct_decl(c))
		return parse_destruct_decl(c, sp, atkw(c, K_auto));
	if (is_storage(c) || is_typename(c)) {
		n = node(NBlock, sp);
		parse_local_decl(c, n);
		return n;
	}
	if (eatkw(c, K_if)) {
		expect(c, PLparen, "'('");
		a = type_expr(c, parse_expr(c));
		expect(c, PRparen, "')'");
		check_cond_assign(c, a);
		b = parse_braced_body(c, "if");
		n = node(NIf, sp);
		n->a = a;
		n->b = b;
		if (eatkw(c, K_else)) {
			if (atkw(c, K_if))
				n->c = parse_stmt(c); /* else if */
			else
				n->c = parse_braced_body(c, "else");
		}
		return n;
	}
	if (eatkw(c, K_while)) {
		expect(c, PLparen, "'('");
		a = type_expr(c, parse_expr(c));
		expect(c, PRparen, "')'");
		check_cond_assign(c, a);
		n = node(NWhile, sp);
		n->a = a;
		n->b = parse_braced_body(c, "while");
		return n;
	}
	if (eatkw(c, K_do)) {
		n = node(NDo, sp);
		n->a = parse_braced_body(c, "do");
		if (!eatkw(c, K_while))
			error_tok(c, peek(c), "expected 'while'");
		expect(c, PLparen, "'('");
		n->b = type_expr(c, parse_expr(c));
		expect(c, PRparen, "')'");
		check_cond_assign(c, n->b);
		expect(c, PSemi, "';'");
		return n;
	}
	if (eatkw(c, K_for)) {
		int for_scope;

		expect(c, PLparen, "'('");
		if (peek_range_for(c))
			return parse_range_for(c, sp);
		n = node(NFor, sp);
		for_scope = 0;
		if (!at(c, PSemi)) {
			if (peek_for_init_decl(c)) {
				symbol_push_block(c);
				for_scope = 1;
				n->a = parse_for_init_decl(c);
			} else
				n->a = type_expr(c, parse_comma_expr(c));
		}
		expect(c, PSemi, "';'");
		if (!at(c, PSemi))
			n->b = type_expr(c, parse_expr(c));
		expect(c, PSemi, "';'");
		check_cond_assign(c, n->b);
		if (!at(c, PRparen))
			n->c = type_expr(c, parse_comma_expr(c));
		expect(c, PRparen, "')'");
		node_add(n, parse_braced_body(c, "for"));
		if (for_scope)
			symbol_pop_block(c);
		return n;
	}
	if (eatkw(c, K_switch)) {
		expect(c, PLparen, "'('");
		n = node(NSwitch, sp);
		n->a = type_expr(c, parse_expr(c));
		expect(c, PRparen, "')'");
		switch_depth++;
		n->b = parse_stmt(c);
		switch_depth--;
		check_switch_fallthrough(c, n->b);
		return n;
	}
	if (eatkw(c, K_case)) {
		int64_t hi;

		n = node(NCase, sp);
		n->a = type_expr(c, parse_expr(c));
		if (!eval_const(c, n->a, &n->int_val))
			error_at(c, sp, "case label is not a constant");
		{
			int isrange = 0;

			if (eat(c, PDotDot))
				isrange = 1;
			else if (at(c, PEllipsis)) {
				error_tok(c, peek(c),
					  "case ranges use '..' ('...' is only for varargs)");
				(void)eat(c, PEllipsis);
				isrange = 1;
			}
			if (isrange) {
				n->b = type_expr(c, parse_expr(c));
				if (!eval_const(c, n->b, &hi))
					error_at(c, sp, "case range end is not a constant");
				if (hi < n->int_val)
					error_at(c, sp, "empty case range");
				/* stash high as NLit in b for emit */
				n->b = node(NLit, sp);
				n->b->int_val = hi;
				n->b->type = c->type_int;
			}
		}
		expect(c, PColon, "':'");
		return n;
	}
	if (eatkw(c, K_default)) {
		expect(c, PColon, "':'");
		return node(NDefault, sp);
	}
	if (eatkw(c, K_break)) {
		expect(c, PSemi, "';'");
		return node(NBreak, sp);
	}
	if (eatkw(c, K_continue)) {
		expect(c, PSemi, "';'");
		return node(NContinue, sp);
	}
	if (eatkw(c, K_fallthrough)) {
		if (switch_depth == 0)
			error_at(c, sp, "fallthrough outside of switch");
		expect(c, PSemi, "';'");
		return node(NFallthrough, sp);
	}
	if (eatkw(c, K_defer)) {
		n = node(NDefer, sp);
		n->a = parse_stmt(c);
		return n;
	}
	if (eatkw(c, K_return)) {
		n = node(NReturn, sp);
		if (!at(c, PSemi)) {
			Type* rt;

			rt = c->current_fn && c->current_fn->type ? c->current_fn->type->base : NULL;
			if (at(c, PLparen) && rt && is_tuple(rt))
				n->a = parse_tuple_lit(c, rt, sp);
			else
				n->a = type_expr(c, parse_expr(c));
			if (n->a && rt && !is_tuple(rt)) {
				n->a = apply_implicit_conversions(c, rt, n->a);
				check_implicit_conv(c, sp, rt, n->a);
			}
		}
		expect(c, PSemi, "';'");
		return n;
	}
	if (eatkw(c, K_goto)) {
		if (peek(c)->kind != TIdent)
			error_tok(c, peek(c), "expected label");
		t = take(c);
		s = symbol_define(c, t->s, SK_LABEL, NULL, ST_NONE, t->span);
		n = node(NGoto, sp);
		n->s = t->s;
		n->symbol = s;
		expect(c, PSemi, "';'");
		return n;
	}
	if (peek(c)->kind == TIdent && peekn(c, 1)->kind == TPunct && peekn(c, 1)->punct == PColon) {
		t = take(c);
		take(c); /* : */
		s = symbol_define(c, t->s, SK_LABEL, NULL, ST_NONE, t->span);
		s->defined = 1;
		n = node(NLabel, sp);
		n->s = t->s;
		n->symbol = s;
		n->a = parse_stmt(c);
		return n;
	}
	if (eat(c, PSemi))
		return node(NSkip, sp);
	n = type_expr(c, parse_expr(c));
	expect(c, PSemi, "';'");
	(void)d;
	return n;
}

// Infer array bound from initializer when declared as T a[].
static void
finish_array_from_init(Compiler* c, Type** pt, Initializer* in) {
	Type* t;
	int64_t len, i, idx;
	int pos;

	t = *pt;
	if (t == NULL || t->kind != TY_ARRAY || t->len >= 0)
		return;
	len = 0;
	if (in && in->is_list) {
		pos = 0;
		for (i = 0; i < in->items_len; i++) {
			if (in->items[i].designator == IDIndexEq) {
				idx = in->items[i].index + 1;
				if (idx > len)
					len = idx;
			} else {
				pos++;
				if (pos > len)
					len = pos;
			}
		}
		*pt = type_array(c, t->base, len);
	} else if (in && in->expr && in->expr->kind == NStr)
		*pt = type_array(c, t->base, in->expr->type ? in->expr->type->len : 1);
	(void)c;
}

// Parse `auto name = expr;` and define the local with inferred type.
static void
parse_auto_local(Compiler* c, Node* blk, Span sp) {
	char* name;
	Type* ty;
	Symbol* s;
	Node* d;
	Initializer* in;

	eatkw(c, K_auto);
	if (peek(c)->kind != TIdent) {
		error_tok(c, peek(c), "expected identifier");
		return;
	}
	name = take(c)->s;
	expect(c, PEq, "'='");
	d = node(NDecl, sp);
	d->init = parse_init(c);
	in = d->init;
	if (in == NULL || in->is_list || in->expr == NULL || in->expr->type == NULL) {
		error_at(c, sp, "auto requires an expression initializer with a known type");
		expect(c, PSemi, "';'");
		return;
	}
	ty = decay(c, in->expr->type);
	s = symbol_define(c, name, SK_VAR, ty, ST_LOCAL, sp);
	d->symbol = s;
	d->type = ty;
	expect(c, PSemi, "';'");
	node_add(blk, d);
}

// Parse one local variable declaration and append it to a block.
static void
parse_local_decl(Compiler* c, Node* blk) {
	int storage, saw, isoverload;
	Type *base, *ty;
	char* name;
	Symbol* s;
	Node* d;
	Span sp;

	sp = peek(c)->span;
	if (atkw(c, K_static_assert)) {
		parse_static_assert(c);
		return;
	}
	if (peek_destruct_decl(c)) {
		node_add(blk, parse_destruct_decl(c, sp, atkw(c, K_auto)));
		return;
	}
	if (peek_auto_local(c)) {
		parse_auto_local(c, blk, sp);
		return;
	}
	isoverload = 0;
	if (eatkw(c, K_overload))
		isoverload = 1;
	storage = ST_NONE;
	saw = 0;
	base = parse_declspec(c, &storage, &saw);
	if (at(c, PSemi)) {
		take(c);
		return;
	}
	name = NULL;
	ty = parse_declarator(c, base, &name, 0);
	for (;;) {
		if (name == NULL) {
			expect(c, PSemi, "';'");
			return;
		}
		if (!is_func(ty) && ty && (is_aggr(ty) || ty->kind == TY_ENUM) && !ty->complete)
			error_at(c, sp, "incomplete type %s", type_name(ty));
		if (storage == ST_TYPEDEF) {
			symbol_define(c, name, SK_TYPEDEF, ty, ST_TYPEDEF, sp);
		} else if (is_func(ty)) {
			reject_user_prototype(c, sp);
			s = symbol_define_func(c, name, ty, storage == ST_NONE ? ST_EXTERN : storage, sp, isoverload);
			(void)s;
		} else {
			int st = storage == ST_STATIC ? ST_STATIC : ST_LOCAL;

			s = symbol_define(c, name, SK_VAR, ty, st, sp);
			d = node(NDecl, sp);
			d->symbol = s;
			d->type = ty;
			if (eat(c, PEq)) {
				d->init = parse_init(c);
				finish_array_from_init(c, &s->type, d->init);
				d->type = s->type;
				if (d->init && d->init->expr && s->type) {
					d->init->expr = apply_implicit_conversions(c, s->type, d->init->expr);
					check_implicit_conv(c, sp, s->type, d->init->expr);
				}
			}
			require_local_init(c, sp, name, st, d->init);
			if (st == ST_STATIC) {
				s->int_val = ++c->static_seq;
				add_global(c, d);
			}
			node_add(blk, d);
		}
		if (!eat(c, PComma)) {
			expect(c, PSemi, "';'");
			return;
		}
		if (user_source(c, peek(c)->span)) {
			error_tok(c, peek(c), "%%C requires one variable declaration per line");
			skip_to_balance(c);
			return;
		}
		name = NULL;
		ty = parse_declarator(c, base, &name, 0);
	}
}

// Skip MSVC/Clang pragma operators used as free-standing statements in headers.
static int
eat_pragma_op(Compiler* c) {
	Tok* t;

	t = peek(c);
	if (t->kind != TIdent || t->s == NULL)
		return 0;
	if (strcmp(t->s, "__pragma") != 0 && strcmp(t->s, "_Pragma") != 0)
		return 0;
	if (user_source(c, t->span))
		error_at(c, t->span, "%s is for headers only", t->s);
	take(c);
	skip_paren_group(c);
	return 1;
}

/* ---- declarations / unit ---- */

// Parse a top-level or in-function declaration or function definition.
static void
parse_decl_or_def(Compiler* c, int in_func) {
	int storage, saw, i, isoverload, ismethod;
	Type *base, *ty, *recv_ty;
	char *name, *recv_name, *recv_tag;
	char* doc;
	Symbol *s, *ps;
	Node *fn, *body, *d;
	Span sp;

	while (eat_pragma_op(c))
		;
	doc = peek(c)->doc;
	peek(c)->doc = NULL;
	sp = peek(c)->span;
	isoverload = 0;
	ismethod = 0;
	recv_tag = NULL;
	recv_name = NULL;
	recv_ty = NULL;
	if (atkw(c, K_static_assert)) {
		free(doc);
		parse_static_assert(c);
		return;
	}
	if (eatkw(c, K_overload))
		isoverload = 1;
	if (at(c, PSemi)) {
		free(doc);
		take(c);
		return;
	}
	storage = ST_NONE;
	saw = 0;
	if (at(c, PLparen) && peek_tuple_type(c)) {
		base = parse_tuple_type(c);
		saw = 1;
	} else
		base = parse_declspec(c, &storage, &saw);
	if (at(c, PSemi)) {
		free(doc);
		take(c);
		return;
	}
	name = NULL;
	if (peek_method_decl(c)) {
		ismethod = 1;
		if (in_func || c->block != 0) {
			free(doc);
			error_at(c, sp, "methods must have file scope");
			skip_to_balance(c);
			return;
		}
		if (isoverload) {
			free(doc);
			error_at(c, sp, "methods cannot be overload");
			skip_to_balance(c);
			return;
		}
		base = parse_pointers(c, base);
		ty = parse_method_declarator(c, base, &name, &recv_name, &recv_ty, &recv_tag);
	} else
		ty = parse_declarator(c, base, &name, 1);
	if (name == NULL) {
		free(doc);
		if (!eat(c, PSemi)) {
			error_tok(c, peek(c), "expected declaration");
			skip_to_balance(c);
		}
		return;
	}
	if (storage == ST_TYPEDEF) {
		for (;;) {
			s = symbol_define(c, name, SK_TYPEDEF, ty, ST_TYPEDEF, sp);
			if (!in_func && c->block == 0 && storage != ST_STATIC && doc) {
				s->doc = doc;
				doc = NULL;
			}
			if (!eat(c, PComma)) {
				free(doc);
				expect(c, PSemi, "';'");
				return;
			}
			if (user_source(c, peek(c)->span)) {
				free(doc);
				error_tok(c, peek(c), "%%C requires one variable declaration per line");
				skip_to_balance(c);
				return;
			}
			name = NULL;
			ty = parse_declarator(c, base, &name, 1);
			if (name == NULL) {
				free(doc);
				expect(c, PSemi, "';'");
				return;
			}
		}
	}
	if (is_func(ty) && at(c, PLbrace)) {
		if (eat(c, PComma)) {
			free(doc);
			error_tok(c, peek(c), "%%C requires one variable declaration per line");
			skip_to_balance(c);
			return;
		}
		if (ismethod) {
			s = symbol_find_method(c, recv_tag, name);
			if (s && s->type && !type_eq(s->type, ty))
				error_at(c, sp, "method %s signature mismatch", name);
			if (s == NULL)
				s = symbol_define_method(c, name, recv_ty, recv_tag, ty, storage, sp);
			else {
				if (s->defined)
					error_at(c, sp, "redefinition of method %s", name);
				/* Keep prescanned Type* so earlier call sites share readonly inference. */
				if (s->type)
					ty = s->type;
				else
					s->type = ty;
			}
		} else {
			s = find_prescan_func(c, name, ty, isoverload);
			if (s == NULL)
				s = symbol_define_func(c, name, ty, storage, sp, isoverload);
			else {
				if (s->defined)
					error_at(c, sp, "redefinition of %s", name);
				if (s->type)
					ty = s->type;
				else
					s->type = ty;
			}
		}
		s->defined = 1;
		if (!in_func && c->block == 0 && storage != ST_STATIC)
			s->doc = doc;
		else
			free(doc);
		if (storage == ST_STATIC && s->linkname == NULL && !ismethod) {
			char buf[160];

			snprintf(buf, sizeof(buf), "__f%d_%s", ++c->static_seq, name);
			s->linkname = xstrdup(buf);
		}
		c->current_fn = s;
		symbol_push_block(c);
		for (i = 0; i < ty->params_len; i++) {
			if (ty->param_names && ty->param_names[i]) {
				ps = symbol_define(c, ty->param_names[i], SK_VAR, ty->params[i], ST_PARAM, sp);
				if (ty->param_array && ty->param_array[i])
					ps->array_param = 1;
				if (ty->param_fixed_len)
					ps->param_fixed_len = ty->param_fixed_len[i];
				else
					ps->param_fixed_len = -1;
			}
		}
		body = parse_compound(c, 1);
		symbol_pop_block(c);
		fn = node(NFunc, sp);
		fn->symbol = s;
		fn->type = ty;
		fn->a = body;
		s->node = fn;
		add_func(c, fn);
		c->current_fn = NULL;
		(void)in_func;
		return;
	}
	for (;;) {
		if (is_func(ty)) {
			reject_user_prototype(c, sp);
			if (ismethod)
				s = symbol_define_method(c, name, recv_ty, recv_tag, ty,
						     storage == ST_STATIC ? ST_STATIC : ST_EXTERN, sp);
			else
				s = symbol_define_func(c, name, ty, storage == ST_STATIC ? ST_STATIC : ST_EXTERN, sp,
						    isoverload);
			if (!in_func && c->block == 0 && storage != ST_STATIC && doc) {
				s->doc = doc;
				doc = NULL;
			}
			(void)s;
		} else {
			s = symbol_define(c, name, SK_VAR, ty, storage, sp);
			if (!in_func && c->block == 0 && storage != ST_STATIC && doc) {
				s->doc = doc;
				doc = NULL;
			}
			d = node(NDecl, sp);
			d->symbol = s;
			d->type = ty;
			if (eat(c, PEq)) {
				d->init = parse_init(c);
				finish_array_from_init(c, &s->type, d->init);
				d->type = s->type;
				s->type = d->type;
				s->defined = 1;
				if (d->init && d->init->expr && ty) {
					d->init->expr = apply_implicit_conversions(c, ty, d->init->expr);
					check_implicit_conv(c, sp, ty, d->init->expr);
				}
			}
			if (c->block == 0)
				add_global(c, d);
		}
		if (!eat(c, PComma)) {
			free(doc);
			expect(c, PSemi, "';'");
			return;
		}
		if (user_source(c, peek(c)->span) || ismethod) {
			free(doc);
			error_tok(c, peek(c), "%%C requires one variable declaration per line");
			skip_to_balance(c);
			return;
		}
		name = NULL;
		ty = parse_declarator(c, base, &name, 1);
		if (name == NULL) {
			free(doc);
			expect(c, PSemi, "';'");
			return;
		}
	}
}

static void parse_local_decl(Compiler* c, Node* blk);
static void parse_decl_or_def(Compiler* c, int in_func);
static void prescan_toplevel(Compiler* c);

/* ---- prescan ---- */

// Register incomplete tags / typedef stubs from user tokens so later files can
// name types before their defining file is type-prescanned.
void
prescan_unit_type_names(Compiler* c) {
	int i, depth, d, n;
	Tok *t, *n1;
	char* last;
	Span sp;
	Symbol* s;
	Type *ty, *tagged;

	n = c->tokens_len;
	depth = 0;
	for (i = 0; i < n; i++) {
		t = &c->tokens[i];
		if (t->kind == TPunct) {
			if (t->punct == PLbrace)
				depth++;
			else if (t->punct == PRbrace)
				depth--;
			continue;
		}
		if (depth != 0)
			continue;
		if (!user_source(c, t->span))
			continue;
		if (t->kind == TKw && (t->kw == K_struct || t->kw == K_union || t->kw == K_enum)) {
			n1 = (i + 1 < n) ? &c->tokens[i + 1] : NULL;
			if (n1 && n1->kind == TIdent && n1->s)
				(void)type_struct(c, t->kw == K_union ? TY_UNION : (t->kw == K_enum ? TY_ENUM : TY_STRUCT),
						  n1->s, n1->span);
			continue;
		}
		if (t->kind != TKw || t->kw != K_typedef)
			continue;
		sp = t->span;
		last = NULL;
		tagged = NULL;
		d = 0;
		for (i++; i < n; i++) {
			n1 = &c->tokens[i];
			if (n1->kind == TPunct) {
				if (n1->punct == PLbrace || n1->punct == PLparen || n1->punct == PLbrack)
					d++;
				else if (n1->punct == PRbrace || n1->punct == PRparen || n1->punct == PRbrack)
					d--;
				else if (n1->punct == PSemi && d == 0)
					break;
			}
			if (d == 0 && n1->kind == TKw &&
			    (n1->kw == K_struct || n1->kw == K_union || n1->kw == K_enum)) {
				Tok* n2 = (i + 1 < n) ? &c->tokens[i + 1] : NULL;
				if (n2 && n2->kind == TIdent && n2->s) {
					int k = n1->kw == K_union ? TY_UNION : (n1->kw == K_enum ? TY_ENUM : TY_STRUCT);
					tagged = type_struct(c, k, n2->s, n2->span);
				}
			}
			if (d == 0 && n1->kind == TIdent && n1->s)
				last = n1->s;
		}
		if (last == NULL)
			continue;
		s = symbol_lookup(c, last);
		if (s && (s->kind == SK_TYPEDEF || s->kind == SK_TAG))
			continue;
		ty = tagged ? tagged : type_struct(c, TY_STRUCT, last, sp);
		(void)symbol_define(c, last, SK_TYPEDEF, ty, ST_TYPEDEF, sp);
	}
}

// Parse typedefs and tag definitions (bodies); stubs should already exist.
void
prescan_unit_type_bodies(Compiler* c) {
	c->pos = 0;
	while (peek(c)->kind != TEof && !c->fatal) {
		if (at(c, PSemi)) {
			take(c);
			continue;
		}
		while (eat_pragma_op(c))
			;
		if (atkw(c, K_import)) {
			take(c);
			if (peek(c)->kind == TString)
				take(c);
			expect(c, PSemi, "';'");
			continue;
		}
		if (atkw(c, K_static_assert)) {
			parse_static_assert(c);
			continue;
		}
		if (atkw(c, K_typedef) || atkw(c, K_struct) || atkw(c, K_union) || atkw(c, K_enum)) {
			parse_decl_or_def(c, 0);
			if (c->error_count && peek(c)->kind != TEof)
				skip_to_balance(c);
			continue;
		}
		skip_toplevel_semi(c);
	}
}

// One-file type prescan: stubs then bodies.
void
prescan_unit_types(Compiler* c) {
	prescan_unit_type_names(c);
	prescan_unit_type_bodies(c);
}

// Second pass: file-scope function / method signatures (bodies skipped).
void
prescan_unit_funcs(Compiler* c) {
	c->pos = 0;
	while (peek(c)->kind != TEof && !c->fatal) {
		if (at(c, PSemi)) {
			take(c);
			continue;
		}
		if (atkw(c, K_import)) {
			take(c);
			if (peek(c)->kind != TString)
				error_tok(c, peek(c), "expected string literal after import");
			else
				take(c);
			expect(c, PSemi, "';'");
			continue;
		}
		prescan_toplevel(c);
		if (c->error_count && peek(c)->kind != TEof)
			skip_to_balance(c);
	}
}

// Prescan: types, then file-scope function signatures (skip bodies).
void
prescan_unit(Compiler* c) {
	prescan_unit_types(c);
	prescan_unit_funcs(c);
}

// True when a typedef declaration's name is already in the symbol table.
static int
typedef_decl_names_known(Compiler* c) {
	int pos0, depth;
	char* last;
	Tok* t;
	Symbol* s;

	pos0 = c->pos;
	depth = 0;
	last = NULL;
	take(c); /* typedef */
	while (peek(c)->kind != TEof) {
		t = peek(c);
		if (t->kind == TPunct) {
			if (t->punct == PLparen || t->punct == PLbrack || t->punct == PLbrace)
				depth++;
			else if (t->punct == PRparen || t->punct == PRbrack || t->punct == PRbrace)
				depth--;
			else if (t->punct == PSemi && depth == 0) {
				take(c);
				break;
			}
		}
		if (depth == 0 && t->kind == TIdent && t->s)
			last = t->s;
		take(c);
		if (c->fatal)
			break;
	}
	if (last == NULL) {
		c->pos = pos0;
		return 0;
	}
	s = symbol_lookup(c, last);
	if (s && (s->kind == SK_TYPEDEF || (s->kind == SK_TAG && s->type && s->type->complete)))
		return 1;
	c->pos = pos0;
	return 0;
}

// Skip a complete tag/typedef declaration if already parsed in prescan.
static int
skip_parsed_type_decl(Compiler* c) {
	Tok* t;
	Symbol* s;
	int pos0;

	if (atkw(c, K_struct) || atkw(c, K_union) || atkw(c, K_enum)) {
		pos0 = c->pos;
		take(c); /* struct / union / enum */
		/* SDK: struct __declspec(deprecated(...)) Tag { ... }; */
		while (eat_vendor_attr(c))
			;
		t = peek(c);
		if (t->kind == TIdent && t->s) {
			s = symbol_lookup_tag(c, t->s);
			if (s && s->type && s->type->complete) {
				c->pos = pos0;
				skip_toplevel_semi(c);
				return 1;
			}
		}
		c->pos = pos0;
	}
	if (atkw(c, K_typedef) && typedef_decl_names_known(c))
		return 1;
	return 0;
}

// Register file-scope function signatures without parsing bodies.
// Leave Tok.doc alone — the main parse attaches docs to symbols.
static void
prescan_toplevel(Compiler* c) {
	int pos0, storage, saw, isoverload, ismethod;
	Type *base, *ty, *recv_ty;
	char *name, *recv_name, *recv_tag;
	Symbol* s;
	Span sp;

	pos0 = c->pos;
	sp = peek(c)->span;
	isoverload = 0;
	ismethod = 0;
	recv_tag = NULL;
	recv_name = NULL;
	recv_ty = NULL;
	if (atkw(c, K_static_assert)) {
		parse_static_assert(c);
		return;
	}
	if (eatkw(c, K_overload))
		isoverload = 1;
	if (at(c, PSemi)) {
		take(c);
		return;
	}
	/*
	 * Leading __pragma / __declspec must not hide a following typedef/tag
	 * from the skip path — otherwise parse_declspec probes the enum/struct
	 * body, defines enumerators, then rewind+skip leaves them allocated and
	 * the main parse redefines them (Windows excpt.h / _CRT_BEGIN_C_HEADER).
	 */
	while (eat_pragma_op(c))
		;
	while (eat_vendor_attr(c))
		;
	if (atkw(c, K_typedef) || atkw(c, K_struct) || atkw(c, K_union) || atkw(c, K_enum)) {
		skip_toplevel_semi(c);
		return;
	}
	storage = ST_NONE;
	saw = 0;
	if (at(c, PLparen) && peek_tuple_type(c)) {
		base = parse_tuple_type(c);
		saw = 1;
	} else
		base = parse_declspec(c, &storage, &saw);
	if (at(c, PSemi)) {
		take(c);
		return;
	}
	name = NULL;
	if (peek_method_decl(c)) {
		ismethod = 1;
		base = parse_pointers(c, base);
		ty = parse_method_declarator(c, base, &name, &recv_name, &recv_ty, &recv_tag);
	} else
		ty = parse_declarator(c, base, &name, 1);
	if (name == NULL || !is_func(ty)) {
		c->pos = pos0;
		skip_toplevel_semi(c);
		return;
	}
	if (eat(c, PComma)) {
		c->pos = pos0;
		skip_toplevel_semi(c);
		return;
	}
	if (storage == ST_TYPEDEF) {
		c->pos = pos0;
		skip_toplevel_semi(c);
		return;
	}
	if (at(c, PLbrace)) {
		if (ismethod)
			s = symbol_define_method(c, name, recv_ty, recv_tag, ty, storage, sp);
		else
			s = symbol_define_func(c, name, ty, storage, sp, isoverload);
		skip_braced(c);
		(void)s;
		return;
	}
	reject_user_prototype(c, sp);
	if (ismethod)
		s = symbol_define_method(c, name, recv_ty, recv_tag, ty, storage == ST_STATIC ? ST_STATIC : ST_EXTERN, sp);
	else
		s = symbol_define_func(c, name, ty, storage == ST_STATIC ? ST_STATIC : ST_EXTERN, sp, isoverload);
	expect(c, PSemi, "';'");
	(void)s;
}

// Top-level: imports then decl/def until EOF (main parse entry).
void parse_unit(Compiler* c) {
	c->pos = 0;
	while (peek(c)->kind != TEof && !c->fatal) {
		if (at(c, PSemi)) {
			take(c);
			continue;
		}
		while (eat_pragma_op(c))
			;
		if (atkw(c, K_import)) {
			take(c);
			if (peek(c)->kind != TString)
				error_tok(c, peek(c), "expected string literal after import");
			else
				take(c);
			expect(c, PSemi, "';'");
			continue;
		}
		if (skip_parsed_type_decl(c))
			continue;
		parse_decl_or_def(c, 0);
		if (c->error_count && peek(c)->kind != TEof) {
			/* recover at next likely declaration */
			if (!is_typename(c) && !is_storage(c) && peek(c)->kind != TEof)
				skip_to_balance(c);
		}
	}
}