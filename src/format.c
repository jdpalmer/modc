/*
 * printf/scanf format checking and printf-family lowering.
 *
 * Literal formats only. Bare %s with char[..] rewrites to %.*s + len + ptr.
 * Fixed char[N] decays to a pointer (NUL C string). Non-bare %s with char[..]
 * is an error. printf("…\n") → puts; printf("%c", x) → putchar.
 */
#include "ast.h"

enum {
	FmtPrint = 0,
	FmtScan = 1
};

typedef struct {
	const char* name;
	int fmt_idx; /* 0-based index of format among call args */
	int kind;    /* FmtPrint or FmtScan */
} FmtEnt;

static const FmtEnt fmt_table[] = {
    {"printf", 0, FmtPrint},
    {"scanf", 0, FmtScan},
    {"fprintf", 1, FmtPrint},
    {"fscanf", 1, FmtScan},
    {"sprintf", 1, FmtPrint},
    {"sscanf", 1, FmtScan},
    {"snprintf", 2, FmtPrint},
    {NULL, 0, 0},
};

static const FmtEnt*
fmt_lookup(const char* name) {
	int i;

	if (name == NULL)
		return NULL;
	for (i = 0; fmt_table[i].name; i++)
		if (strcmp(fmt_table[i].name, name) == 0)
			return &fmt_table[i];
	return NULL;
}

static Node*
mk_name(Span sp, const char* name) {
	Node* n;

	n = node(NdName, sp);
	n->s = (char*)name;
	return n;
}

static int
is_char_elem(Type* t) {
	return t && (t->kind == TyChar || t->kind == TyUChar);
}

static int
is_char_view(Type* t) {
	if (t == NULL)
		return 0;
	if (is_ranged(t) && is_char_elem(t->base))
		return 1;
	if (is_array(t) && t->len >= 0 && is_char_elem(t->base))
		return 1;
	return 0;
}

static int
is_ranged_char(Type* t) {
	return t && is_ranged(t) && is_char_elem(t->base);
}

/* True when the conversion text is exactly "%s" (no flags/width/precision/length). */
static int
is_bare_s(const char* s, int start, int end, int star_w, int star_p, int len_mod,
	  char conv) {
	return conv == 's' && !star_w && !star_p && len_mod == 0 && end - start == 2 &&
	       s[start] == '%' && s[start + 1] == 's';
}


static int
is_char_ptr_ty(Compiler* c, Type* t) {
	Type* d;

	d = decay(c, t);
	return d && is_ptr(d) && is_char_elem(d->base);
}

static int
is_any_ptr(Compiler* c, Type* t) {
	Type* d;

	d = decay(c, t);
	return d && is_ptr(d);
}

static int
is_promoted_int(Compiler* c, Type* t) {
	Type* d;

	(void)c;
	d = t;
	if (d == NULL)
		return 0;
	if (is_array(d) || is_ranged(d))
		return 0;
	return is_int(d) || d->kind == TyBool || d->kind == TyEnum;
}

static int
is_doubleish(Type* t) {
	return t && (t->kind == TyDouble || t->kind == TyFloat);
}

/* Append decoded bytes (already unescaped) into the string pool. */
static int
intern_decoded(Compiler* c, const char* bytes, int nbytes) {
	int off, n;

	n = nbytes + 1;
	off = c->strpool_len;
	if (c->strpool_len + n > c->strpool_cap) {
		c->strpool_cap = c->strpool_cap ? c->strpool_cap * 2 : 256;
		while (c->strpool_cap < c->strpool_len + n)
			c->strpool_cap *= 2;
		c->strpool = xrealloc(c->strpool, c->strpool_cap);
	}
	memcpy(c->strpool + c->strpool_len, bytes, (size_t)nbytes);
	c->strpool[c->strpool_len + nbytes] = 0;
	c->strpool_len += n;
	return off;
}

static Node*
make_str_lit(Compiler* c, Span sp, const char* bytes, int nbytes) {
	Node* n;
	int pool_len;

	n = node(NdStr, sp);
	n->s = xmalloc((size_t)nbytes + 1);
	memcpy(n->s, bytes, (size_t)nbytes);
	n->s[nbytes] = 0;
	n->int_val = intern_decoded(c, bytes, nbytes);
	pool_len = nbytes + 1;
	n->type = type_array(c, c->type_char, (int64_t)pool_len);
	n->type->is_readonly = 1;
	return n;
}

static Node*
synth_len(Compiler* c, Span sp, Node* arg) {
	Node* call;

	call = node1(NdCall, sp, mk_name(sp, "len"));
	node_add(call, arg);
	call = type_expr(c, call);
	/* %.*s wants int */
	{
		Node* cast;

		cast = node1(NdCast, sp, call);
		cast->type = c->type_int;
		cast->is_synth = 1;
		return cast;
	}
}

static Node*
synth_ptr(Compiler* c, Span sp, Node* arg) {
	Node* call;

	call = node1(NdCall, sp, mk_name(sp, "ptr"));
	node_add(call, arg);
	return type_expr(c, call);
}

static const char*
fmt_text(Compiler* c, Node* fmt) {
	if (fmt == NULL || fmt->kind != NdStr)
		return "";
	if (c->strpool && fmt->int_val >= 0 && fmt->int_val < c->strpool_len)
		return (const char*)(c->strpool + fmt->int_val);
	return fmt->s ? fmt->s : "";
}

static int
fmt_has_conversions(const char* s) {
	int i;

	if (s == NULL)
		return 0;
	for (i = 0; s[i]; i++) {
		if (s[i] == '%' && s[i + 1]) {
			if (s[i + 1] == '%') {
				i++;
				continue;
			}
			return 1;
		}
	}
	return 0;
}

static int
try_lower_puts(Compiler* c, Node* call, Node* fmt) {
	Symbol* puts_sym;
	const char* s;
	size_t n;
	Node* lit;

	s = fmt_text(c, fmt);
	n = strlen(s);
	if (n == 0 || s[n - 1] != '\n')
		return 0;
	if (fmt_has_conversions(s))
		return 0;
	puts_sym = symbol_lookup(c, "puts");
	if (puts_sym == NULL || puts_sym->type == NULL)
		return 0;
	/* Strip trailing newline for puts. */
	lit = make_str_lit(c, fmt->span, s, (int)(n - 1));
	call->a = mk_name(call->span, "puts");
	call->a->symbol = puts_sym;
	call->a->type = puts_sym->type;
	call->children_len = 0;
	call->children = NULL;
	node_add(call, lit);
	if (puts_sym->type->params_len > 0)
		call->children[0] =
		    apply_implicit_conversions(c, puts_sym->type->params[0], lit);
	return 1;
}

static int
try_lower_putchar(Compiler* c, Node* call, Node* fmt, Node** args, int args_len,
		  int fmt_idx) {
	Symbol* pc;
	Node* ch;
	const char* s;

	s = fmt_text(c, fmt);
	if (strcmp(s, "%c") != 0)
		return 0;
	if (args_len != fmt_idx + 2)
		return 0;
	ch = args[fmt_idx + 1];
	if (ch == NULL || ch->type == NULL || !is_promoted_int(c, ch->type))
		return 0;
	pc = symbol_lookup(c, "putchar");
	if (pc == NULL || pc->type == NULL || pc->type->params_len < 1)
		return 0;
	call->a = mk_name(call->span, "putchar");
	call->a->symbol = pc;
	call->a->type = pc->type;
	call->children_len = 0;
	call->children = NULL;
	node_add(call, ch);
	call->children[0] = apply_implicit_conversions(c, pc->type->params[0], ch);
	return 1;
}

static void
expect_type(Compiler* c, Span sp, Node* arg, const char* what, int ok) {
	if (!ok)
		error_at(c, arg && arg->span.file ? arg->span : sp,
			 "format specifies %s but the argument has type %s", what,
			 type_name(arg ? arg->type : NULL));
}

/*
 * Parse one conversion starting at *pi (on '%'). Advances *pi past the
 * specifier. Returns 0 on error. Fills out_* describing consumed args.
 */
static int
parse_one(Compiler* c, Span sp, const char* s, int* pi, int scan, int* star_w,
	  int* star_p, int* is_s_view_ok, char* conv_out, int* len_mod) {
	int i;
	int assign_suppress = 0;

	i = *pi;
	if (s[i] != '%')
		return 0;
	i++;
	*star_w = 0;
	*star_p = 0;
	*is_s_view_ok = 0;
	*len_mod = 0;
	*conv_out = 0;
	if (s[i] == '%') {
		i++;
		*pi = i;
		*conv_out = '%';
		return 1;
	}
	if (scan && s[i] == '*') {
		assign_suppress = 1;
		i++;
	}
	/* flags (printf) */
	if (!scan) {
		while (s[i] == '-' || s[i] == '+' || s[i] == ' ' || s[i] == '#' || s[i] == '0')
			i++;
	}
	/* width */
	if (s[i] == '*') {
		*star_w = 1;
		i++;
	} else {
		while (s[i] >= '0' && s[i] <= '9')
			i++;
	}
	/* precision (printf) */
	if (!scan && s[i] == '.') {
		i++;
		if (s[i] == '*') {
			*star_p = 1;
			i++;
		} else {
			while (s[i] >= '0' && s[i] <= '9')
				i++;
		}
	}
	/* length */
	if (s[i] == 'h' && s[i + 1] == 'h') {
		*len_mod = 'H';
		i += 2;
	} else if (s[i] == 'l' && s[i + 1] == 'l') {
		*len_mod = 'Q';
		i += 2;
	} else if (s[i] == 'h' || s[i] == 'l' || s[i] == 'L' || s[i] == 'z' ||
		   s[i] == 't' || s[i] == 'j') {
		*len_mod = s[i];
		i++;
	}
	if (s[i] == 0) {
		error_at(c, sp, "incomplete format specifier");
		return 0;
	}
	*conv_out = s[i];
	if (*conv_out == 's' || *conv_out == 'S')
		*is_s_view_ok = 1;
	if (scan && *conv_out == '[') {
		i++;
		if (s[i] == '^')
			i++;
		if (s[i] == ']')
			i++;
		while (s[i] && s[i] != ']')
			i++;
		if (s[i] == ']')
			i++;
		*conv_out = 's';
		*is_s_view_ok = 1;
		*pi = i;
		if (assign_suppress) {
			*star_w = 0;
			*star_p = 0;
			*is_s_view_ok = 0;
			*conv_out = '*'; /* suppressed */
		}
		return 1;
	}
	i++;
	*pi = i;
	if (assign_suppress) {
		*star_w = 0;
		*star_p = 0;
		*is_s_view_ok = 0;
		*conv_out = '*';
	}
	return 1;
}

static int
match_print_arg(Compiler* c, Span sp, Node* arg, char conv, int len_mod) {
	Type* t;

	if (arg == NULL || arg->type == NULL) {
		error_at(c, sp, "missing argument for format conversion");
		return 0;
	}
	t = arg->type;
	switch (conv) {
	case 'd':
	case 'i':
	case 'u':
	case 'o':
	case 'x':
	case 'X':
		expect_type(c, sp, arg, "an integer", is_promoted_int(c, t));
		return is_promoted_int(c, t);
	case 'c':
		expect_type(c, sp, arg, "an integer (char)", is_promoted_int(c, t));
		return is_promoted_int(c, t);
	case 'f':
	case 'F':
	case 'e':
	case 'E':
	case 'g':
	case 'G':
	case 'a':
	case 'A':
		expect_type(c, sp, arg, "a floating-point value",
			    is_doubleish(t) || (len_mod == 'L' && t && t->kind == TyDouble));
		return is_doubleish(t);
	case 's':
		if (is_char_view(t) || is_char_ptr_ty(c, t))
			return 1;
		expect_type(c, sp, arg, "a string (char * or char[..])", 0);
		return 0;
	case 'p':
		expect_type(c, sp, arg, "a pointer", is_any_ptr(c, t));
		return is_any_ptr(c, t);
	case 'n':
		expect_type(c, sp, arg, "a pointer to an integer", is_any_ptr(c, t));
		return is_any_ptr(c, t);
	default:
		error_at(c, sp, "unsupported format conversion '%%%c'", conv);
		return 0;
	}
}

static int
match_scan_arg(Compiler* c, Span sp, Node* arg, char conv) {
	Type* t;
	Type* d;

	if (arg == NULL || arg->type == NULL) {
		error_at(c, sp, "missing argument for format conversion");
		return 0;
	}
	t = arg->type;
	d = decay(c, t);
	if (conv == '*')
		return 1;
	if (!is_ptr(d) && !is_char_view(t)) {
		error_at(c, arg->span, "scanf conversion expects a pointer");
		return 0;
	}
	if (conv == 's') {
		if (is_char_view(t)) {
			error_at(c, arg->span,
				 "scanf '%%s' does not take char[..]; pass a mutable char * with an explicit field width");
			return 0;
		}
		if (is_char_ptr_ty(c, t)) {
			if (d->is_readonly) {
				error_at(c, arg->span,
					 "scanf '%%s' requires a mutable char *");
				return 0;
			}
			return 1;
		}
		error_at(c, arg->span, "scanf '%%s' expects char *");
		return 0;
	}
	return 1;
}

static void
check_and_rewrite(Compiler* c, Node* call, Type* ft, const FmtEnt* ent, Node* fmt) {
	const char* s;
	int i, ai, nargs;
	int star_w, star_p, view_ok, len_mod;
	char conv;
	Node* new_args[MaxParams];
	int nnew;
	char newfmt[1024];
	int nf;
	int changed;
	Span sp;

	s = fmt_text(c, fmt);
	sp = call->span;
	nargs = call->children_len;
	ai = ent->fmt_idx + 1;
	nnew = 0;
	nf = 0;
	changed = 0;

	/* Preserve fixed args before format, then format slot filled later. */
	for (i = 0; i < ent->fmt_idx && i < nargs && nnew < MaxParams; i++)
		new_args[nnew++] = call->children[i];
	/* placeholder for format */
	if (nnew >= MaxParams) {
		error_at(c, sp, "too many format arguments");
		return;
	}
	new_args[nnew++] = fmt;

	i = 0;
	while (s[i]) {
		if (s[i] != '%') {
			if (nf < (int)sizeof(newfmt) - 1)
				newfmt[nf++] = s[i];
			i++;
			continue;
		}
		{
			int start = i;

			if (!parse_one(c, sp, s, &i, ent->kind == FmtScan, &star_w, &star_p,
				       &view_ok, &conv, &len_mod))
				return;
			if (conv == '%') {
				if (nf + 2 < (int)sizeof(newfmt)) {
					newfmt[nf++] = '%';
					newfmt[nf++] = '%';
				}
				continue;
			}
			if (conv == '*') {
				/* scanf assignment suppression — copy lexeme */
				while (start < i && nf < (int)sizeof(newfmt) - 1)
					newfmt[nf++] = s[start++];
				continue;
			}
			if (ent->kind == FmtPrint) {
				if (star_w) {
					if (ai >= nargs) {
						error_at(c, sp, "too few arguments for format");
						return;
					}
					expect_type(c, sp, call->children[ai], "an int (width *)",
						    is_promoted_int(c, call->children[ai]->type));
					if (nnew >= MaxParams) {
						error_at(c, sp, "too many format arguments");
						return;
					}
					new_args[nnew++] = call->children[ai++];
				}
				if (star_p) {
					if (ai >= nargs) {
						error_at(c, sp, "too few arguments for format");
						return;
					}
					expect_type(c, sp, call->children[ai], "an int (precision *)",
						    is_promoted_int(c, call->children[ai]->type));
					if (nnew >= MaxParams) {
						error_at(c, sp, "too many format arguments");
						return;
					}
					new_args[nnew++] = call->children[ai++];
				}
				if (ai >= nargs) {
					error_at(c, sp, "too few arguments for format");
					return;
				}
				if (conv == 's' && is_ranged_char(call->children[ai]->type)) {
					Node *len_n, *ptr_n;

					if (!is_bare_s(s, start, i, star_w, star_p, len_mod,
						       conv)) {
						error_at(c, call->children[ai]->span,
							 "char[..] with %%s requires a bare \"%%s\" "
							 "(no flags, width, or precision); "
							 "it is rewritten to %%.*s");
						return;
					}
					/* Discard any * width/precision already queued — bare
					 * %s has none; still reject if parse claimed stars. */
					if (star_w || star_p) {
						error_at(c, call->children[ai]->span,
							 "char[..] with %%s requires a bare \"%%s\"");
						return;
					}
					/* Rewrite this conversion to %.*s */
					if (nf + 4 >= (int)sizeof(newfmt)) {
						error_at(c, sp, "format string is too long");
						return;
					}
					newfmt[nf++] = '%';
					newfmt[nf++] = '.';
					newfmt[nf++] = '*';
					newfmt[nf++] = 's';
					if (nnew + 2 > MaxParams) {
						error_at(c, sp, "too many format arguments");
						return;
					}
					len_n = synth_len(c, call->children[ai]->span,
							  call->children[ai]);
					ptr_n = synth_ptr(c, call->children[ai]->span,
							  call->children[ai]);
					new_args[nnew++] = len_n;
					new_args[nnew++] = ptr_n;
					ai++;
					changed = 1;
				} else if (conv == 's' && is_array(call->children[ai]->type) &&
					   is_char_elem(call->children[ai]->type->base)) {
					/* Fixed char[N]: decay to pointer; keep conversion
					 * (NUL-terminated C string). Do not %.*s with N. */
					Node* p;

					while (start < i && nf < (int)sizeof(newfmt) - 1)
						newfmt[nf++] = s[start++];
					p = synth_ptr(c, call->children[ai]->span,
						      call->children[ai]);
					if (nnew >= MaxParams) {
						error_at(c, sp, "too many format arguments");
						return;
					}
					new_args[nnew++] = p;
					ai++;
					changed = 1;
				} else {
					while (start < i && nf < (int)sizeof(newfmt) - 1)
						newfmt[nf++] = s[start++];
					if (!match_print_arg(c, sp, call->children[ai], conv,
							     len_mod))
						/* keep going to find more errors */;
					if (nnew >= MaxParams) {
						error_at(c, sp, "too many format arguments");
						return;
					}
					new_args[nnew++] = call->children[ai++];
				}
			} else {
				/* scanf */
				while (start < i && nf < (int)sizeof(newfmt) - 1)
					newfmt[nf++] = s[start++];
				if (ai >= nargs) {
					error_at(c, sp, "too few arguments for format");
					return;
				}
				match_scan_arg(c, sp, call->children[ai], conv);
				if (nnew >= MaxParams) {
					error_at(c, sp, "too many format arguments");
					return;
				}
				new_args[nnew++] = call->children[ai++];
			}
		}
	}
	if (ai < nargs) {
		error_at(c, call->children[ai] ? call->children[ai]->span : sp,
			 "too many arguments for format");
		return;
	}
	if (changed) {
		Node* new_fmt_node;
		Type* fmt_ty;
		int j;

		newfmt[nf] = 0;
		new_fmt_node = make_str_lit(c, fmt->span, newfmt, nf);
		fmt_ty = NULL;
		if (ft && is_func(ft) && ent->fmt_idx < ft->params_len)
			fmt_ty = ft->params[ent->fmt_idx];
		new_args[ent->fmt_idx] =
		    fmt_ty ? apply_implicit_conversions(c, fmt_ty, new_fmt_node)
			   : new_fmt_node;
		call->children = xmalloc((size_t)nnew * sizeof(Node*));
		call->children_len = nnew;
		for (j = 0; j < nnew; j++)
			call->children[j] = new_args[j];
	}
}

void check_format_call(Compiler* c, Node* call, Type* ft) {
	const FmtEnt* ent;
	const char* name;
	Node* fmt;

	(void)ft;
	if (c == NULL || call == NULL || call->a == NULL)
		return;
	if (!user_source(c, call->span))
		return;
	if (call->a->kind != NdName || call->a->s == NULL)
		return;
	name = call->a->s;
	ent = fmt_lookup(name);
	if (ent == NULL)
		return;
	if (ent->fmt_idx >= call->children_len || call->children[ent->fmt_idx] == NULL)
		return;
	fmt = call->children[ent->fmt_idx];
	if (fmt->kind != NdStr)
		return;

	if (ent->kind == FmtPrint && strcmp(name, "printf") == 0) {
		if (call->children_len == 1 && try_lower_puts(c, call, fmt))
			return;
		if (try_lower_putchar(c, call, fmt, call->children, call->children_len,
				      ent->fmt_idx))
			return;
	}
	check_and_rewrite(c, call, ft, ent, fmt);
}
