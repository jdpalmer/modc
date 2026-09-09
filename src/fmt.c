/*
 * Token rewriter: Tok[] → formatted .mc source (K&R, tabs, no knobs).
 *
 * Pointer declarators attach * to the type: int* p, char[..]* s (not int *p).
 *
 * Used by `modc format`. Does not run the preprocessor or parser.
 * Source newlines are ignored except as blank-line hints and to end
 * `#` directive lines; the printer inserts breaks after `;` / `{` / `}`
 * (not inside () / []).
 */
#include "ast.h"

typedef struct {
	char* buf;
	size_t n, cap;
	int col;
	int depth; /* { } */
	int paren; /* ( ) */
	int brack; /* [ ] */
	int blank;
	int in_dir;
	int bol; /* next token starts a line */
} Out;

// Grow the output buffer when appending would exceed capacity.
static void
o_grow(Out* o, size_t need) {
	if (o->n + need < o->cap)
		return;
	while (o->n + need >= o->cap)
		o->cap = o->cap ? o->cap * 2 : 256;
	o->buf = xrealloc(o->buf, o->cap);
}

// Append one character and update column / blank-line tracking.
static void
o_putc(Out* o, char ch) {
	o_grow(o, 1);
	o->buf[o->n++] = ch;
	if (ch == '\n') {
		o->col = 0;
		o->blank++;
		o->bol = 1;
	} else {
		o->col++;
		o->blank = 0;
		o->bol = 0;
	}
}

// Append a C string to the output buffer.
static void
o_puts(Out* o, const char* s) {
	while (*s)
		o_putc(o, *s++);
}

// End the current line without duplicating consecutive blank lines.
static void
o_nl(Out* o) {
	if (o->col > 0 || o->n == 0)
		o_putc(o, '\n');
	else if (o->blank == 0)
		o_putc(o, '\n');
}

// Emit tabs for the current brace depth at the start of a line.
static void
o_indent(Out* o) {
	int i, d;

	d = o->depth;
	if (d < 0)
		d = 0;
	for (i = 0; i < d; i++)
		o_putc(o, '\t');
	o->bol = 0;
}

// True for keywords that begin a type specifier sequence.
static int
is_type_kw(int kw) {
	switch (kw) {
	case K_char:
	case K_short:
	case K_int:
	case K_long:
	case K_float:
	case K_double:
	case K_void:
	case K_bool:
	case K_struct:
	case K_union:
	case K_enum:
	case K_signed:
	case K_unsigned:
	case K_const:
	case K_volatile:
	case K_restrict:
	case K_typedef:
	case K_auto:
		return 1;
	default:
		return 0;
	}
}

static Tok* next_code(Tok* tokens, int n, int i);

// True when tokens[i] is '(' of a method declarator (T *recv).name.
static int
method_recv_lparen(Tok* tokens, int n, int i) {
	Tok *t, *star, *name, *rp, *dot, *meth;

	if (i < 0 || i >= n || tokens[i].kind != TPunct || tokens[i].punct != PLparen)
		return 0;
	t = next_code(tokens, n, i);
	if (t == NULL)
		return 0;
	if (t->kind == TKw && (t->kw == K_struct || t->kw == K_union || t->kw == K_enum)) {
		t = next_code(tokens, n, (int)(t - tokens));
		if (t == NULL || t->kind != TIdent)
			return 0;
	} else if (t->kind != TIdent)
		return 0;
	star = next_code(tokens, n, (int)(t - tokens));
	if (star == NULL || star->kind != TPunct || star->punct != PStar)
		return 0;
	while (star && star->kind == TPunct && star->punct == PStar)
		star = next_code(tokens, n, (int)(star - tokens));
	name = star;
	if (name == NULL || name->kind != TIdent)
		return 0;
	rp = next_code(tokens, n, (int)(name - tokens));
	if (rp == NULL || rp->kind != TPunct || rp->punct != PRparen)
		return 0;
	dot = next_code(tokens, n, (int)(rp - tokens));
	if (dot == NULL || dot->kind != TPunct || dot->punct != PDot)
		return 0;
	meth = next_code(tokens, n, (int)(dot - tokens));
	return meth != NULL && meth->kind == TIdent;
}

// Control keywords that need a space before their following '('.
static int
kw_space_before_paren(int kw) {
	switch (kw) {
	case K_if:
	case K_for:
	case K_while:
	case K_switch:
	case K_sizeof:
		return 1;
	default:
		return 0;
	}
}

// True when a token can end a postfix/unary operand (no space before next op).
static int
is_atom(Tok* t) {
	if (t == NULL)
		return 0;
	if (t->kind == TIdent || t->kind == TNumber || t->kind == TString || t->kind == TCharLit)
		return 1;
	if (t->kind == TKw && (t->kw == K_true || t->kw == K_false))
		return 1;
	if (t->kind == TPunct) {
		switch (t->punct) {
		case PRparen:
		case PRbrack:
		case PPlusPlus:
		case PMinusMinus:
			return 1;
		default:
			return 0;
		}
	}
	return 0;
}

// True when a token can start an expression (for * spacing heuristics).
static int
starts_expr(Tok* t) {
	if (t == NULL)
		return 0;
	if (t->kind == TIdent || t->kind == TNumber || t->kind == TString || t->kind == TCharLit || t->kind == TKw)
		return 1;
	if (t->kind == TPunct) {
		switch (t->punct) {
		case PLparen:
		case PLbrack:
		case PPlus:
		case PMinus:
		case PStar:
		case PAmp:
		case PBang:
		case PTilde:
		case PPlusPlus:
		case PMinusMinus:
			return 1;
		default:
			return 0;
		}
	}
	return 0;
}

// Distinguish binary multiplication from declarator * (atom followed by expr).
static int
binary_star(Tok* left, Tok* right) {
	return is_atom(left) && starts_expr(right);
}

// True when + - * & may be unary (operand follows a prefix context).
static int
unary_context(Tok* before) {
	if (before == NULL)
		return 1;
	if (before->kind == TKw)
		return before->kw == K_return || before->kw == K_case;
	if (before->kind != TPunct)
		return 0;
	switch (before->punct) {
	case PLparen:
	case PLbrack:
	case PLbrace:
	case PEq:
	case PPlusEq:
	case PMinusEq:
	case PStarEq:
	case PSlashEq:
	case PPercentEq:
	case PAmpEq:
	case PPipeEq:
	case PCaretEq:
	case PShlEq:
	case PShrEq:
	case PComma:
	case PSemi:
	case PColon:
	case PQuestion:
	case PEqEq:
	case PBangEq:
	case PLt:
	case PGt:
	case PLe:
	case PGe:
	case PShl:
	case PShr:
	case PPlus:
	case PMinus:
	case PStar:
	case PSlash:
	case PPercent:
	case PAmp:
	case PPipe:
	case PCaret:
	case PAmpAmp:
	case PPipePipe:
	case PBang:
	case PTilde:
		return 1;
	default:
		return 0;
	}
}

// Previous non-newline, non-comment token (for spacing decisions).
static Tok*
prev_code(Tok* tokens, int i) {
	int j;

	for (j = i - 1; j >= 0; j--) {
		if (tokens[j].kind == TNewline || tokens[j].kind == TComment)
			continue;
		return &tokens[j];
	}
	return NULL;
}

// Next non-newline, non-comment token (for spacing decisions).
static Tok*
next_code(Tok* tokens, int n, int i) {
	int j;

	for (j = i + 1; j < n; j++) {
		if (tokens[j].kind == TEof)
			return NULL;
		if (tokens[j].kind == TNewline || tokens[j].kind == TComment)
			continue;
		return &tokens[j];
	}
	return NULL;
}

// True for infix operators that take spaces on both sides.
static int
is_binary_punct(int p) {
	switch (p) {
	case PPlus:
	case PMinus:
	case PStar:
	case PSlash:
	case PPercent:
	case PAmp:
	case PPipe:
	case PCaret:
	case PShl:
	case PShr:
	case PEqEq:
	case PBangEq:
	case PLt:
	case PGt:
	case PLe:
	case PGe:
	case PAmpAmp:
	case PPipePipe:
		return 1;
	default:
		return 0;
	}
}

// Token kinds that may follow a closing ')' in a cast expression.
static int
operand_after_cast(Tok* cur) {
	if (cur == NULL)
		return 0;
	if (cur->kind == TIdent || cur->kind == TNumber || cur->kind == TCharLit || cur->kind == TString)
		return 1;
	if (cur->kind == TPunct) {
		switch (cur->punct) {
		case PLparen:
		case PStar:
		case PAmp:
		case PBang:
		case PTilde:
		case PPlusPlus:
		case PMinusMinus:
			return 1;
		default:
			return 0;
		}
	}
	return 0;
}

// Keywords that need a space before their operand (return x, case N:).
static int
kw_space_after(Tok* prev) {
	if (prev == NULL || prev->kind != TKw)
		return 0;
	switch (prev->kw) {
	case K_return:
	case K_case:
	case K_auto:
		return 1;
	default:
		return 0;
	}
}

// Whether to insert a space before '(' (calls vs casts vs declarators).
static int
space_before_lparen(Tok* tokens, int n, int i) {
	Tok *prev, *left;

	prev = prev_code(tokens, i);
	if (prev == NULL)
		return 0;
	if (method_recv_lparen(tokens, n, i))
		return 1;
	if (prev->kind == TKw) {
		if (kw_space_before_paren(prev->kw) || kw_space_after(prev))
			return 1;
		return 0;
	}
	if (prev->kind == TPunct) {
		if (prev->punct == PComma || prev->punct == PEq)
			return 1;
		if (prev->punct == PPlus || prev->punct == PMinus) {
			left = prev_code(tokens, (int)(prev - tokens));
			if (left && is_atom(left))
				return 1;
			return 0;
		}
		if (prev->punct == PStar) {
			left = prev_code(tokens, (int)(prev - tokens));
			if (binary_star(left, next_code(tokens, n, i)))
				return 1;
			return 0;
		}
		if (is_binary_punct(prev->punct))
			return 1;
	}
	return 0;
}

// True when (…) before the current token is a cast, not a parenthesized expr.
static int
paren_expr_is_cast(Tok* tokens, int rparen_i) {
	int depth, i, comma;

	depth = 0;
	comma = 0;
	for (i = rparen_i; i >= 0; i--) {
		Tok* t = &tokens[i];

		if (t->kind == TNewline || t->kind == TComment)
			continue;
		if (t->kind != TPunct)
			continue;
		if (t->punct == PRparen) {
			if (depth == 0)
				depth = 1;
			else
				depth++;
		} else if (t->punct == PLparen) {
			depth--;
			if (depth == 0)
				return !comma;
		} else if (t->punct == PComma && depth == 1)
			comma = 1;
	}
	return 0;
}

// True when ( … ) at i opens a function parameter list (name/kw before '(').
static int
func_param_lparen(Tok* tokens, int i) {
	Tok* t;
	int j;

	for (j = i - 1; j >= 0; j--) {
		t = &tokens[j];
		if (t->kind == TNewline || t->kind == TComment)
			continue;
		if (t->kind == TIdent)
			return 1;
		if (t->kind == TKw && (t->kw == K_typedef || is_type_kw(t->kw)))
			continue;
		if (t->kind == TPunct && (t->punct == PStar || t->punct == PRparen || t->punct == PComma))
			continue;
		return 0;
	}
	return 0;
}

// Pointer declarator * (T* name): attach * to the type, not unary/binary *.
static int
pointer_decl_star(Tok* tokens, int n, int i) {
	Tok *prev, *next, *before;

	if (tokens[i].kind != TPunct || tokens[i].punct != PStar)
		return 0;
	prev = prev_code(tokens, i);
	next = next_code(tokens, n, i);
	if (prev == NULL)
		return 0;
	if (prev->kind == TKw && is_type_kw(prev->kw))
		return 1;
	if (prev->kind == TPunct && prev->punct == PStar)
		return 1;
	if (prev->kind == TPunct && prev->punct == PRparen && paren_expr_is_cast(tokens, (int)(prev - tokens)))
		return 1;
	if (prev->kind == TIdent && next && next->kind == TIdent) {
		int j;

		for (j = i - 1; j >= 0; j--) {
			Tok* t = &tokens[j];

			if (t->kind == TNewline || t->kind == TComment)
				continue;
			if (t->kind == TPunct) {
				if (t->punct == PLparen) {
					if (method_recv_lparen(tokens, n, j) || func_param_lparen(tokens, j))
						return 1;
					return 0;
				}
				if (t->punct == PSemi || t->punct == PLbrace || t->punct == PComma)
					return 1;
				if (is_binary_punct(t->punct) || t->punct == PEq || t->punct == PLbrack || t->punct == PRbrack || t->punct == PQuestion || t->punct == PColon)
					return 0;
				continue;
			}
			if (t->kind == TKw) {
				switch (t->kw) {
				case K_return:
				case K_if:
				case K_for:
				case K_while:
				case K_switch:
				case K_case:
				case K_else:
					return 0;
				default:
					break;
				}
			}
		}
		return 0;
	}
	if (binary_star(prev, next))
		return 0;
	if (prev->kind == TIdent && next && next->kind == TPunct && next->punct == PLparen) {
		before = prev_code(tokens, (int)(prev - tokens));
		if (before && before->kind == TPunct && before->punct == PStar)
			return 1;
		if (before && ((before->kind == TKw && is_type_kw(before->kw)) || before->kind == TIdent))
			return 1;
	}
	return 0;
}

// Spacing rules inside #include and other preprocessor lines.
static int
dir_space(Tok* prev, Tok* cur) {
	int pp, cp;

	if (prev == NULL)
		return 0;
	cp = cur->kind == TPunct ? cur->punct : -1;
	pp = prev->kind == TPunct ? prev->punct : -1;

	if (pp == PHash)
		return 0; /* #include */
	if (cp == PLt)
		return 1; /* include <...> */
	if (pp == PLt)
		return 0; /* <stdio.h> glued */
	if (cp == PGt || pp == PDot || cp == PDot || pp == PSlash || cp == PSlash)
		return 0;
	if (cp == PLparen && (prev->kind == TIdent || prev->kind == TKw) && !cur->ws)
		return 0;
	if (cp == PComma || cp == PRparen)
		return 0;
	if (prev->kind == TString || cur->kind == TString)
		return 1;
	if (prev->kind == TKw || prev->kind == TIdent)
		return 1;
	return 1;
}

// Main heuristic: whether to insert a space before token i.
static int
space_between(Tok* tokens, int n, int i) {
	Tok *prev, *cur, *next, *left;
	int pp, cp;

	cur = &tokens[i];
	prev = prev_code(tokens, i);
	if (prev == NULL)
		return 0;
	next = next_code(tokens, n, i);

	if (cur->kind == TComment)
		return 1;

	cp = cur->kind == TPunct ? cur->punct : -1;
	pp = prev->kind == TPunct ? prev->punct : -1;

	if (cp == PComma || cp == PSemi || cp == PRparen || cp == PRbrack)
		return 0;
	if (cp == PDot || cp == PArrow)
		return 0;
	if (cp == PDotDot) {
		if (prev->kind == TPunct && prev->punct == PLbrack)
			return 0;
		return 1;
	}
	if (cp == PPlusPlus || cp == PMinusMinus)
		return 0;
	if (cp == PLbrack || cp == PColon)
		return 0;

	if (cp == PLparen)
		return space_before_lparen(tokens, n, i);

	if (cp == PLbrace)
		return 1;

	if (cp == PStar) {
		if (pp == PEq || pp == PPlusEq || pp == PMinusEq || pp == PStarEq || pp == PSlashEq || pp == PPercentEq || pp == PAmpEq || pp == PPipeEq || pp == PCaretEq || pp == PShlEq || pp == PShrEq)
			return 1;
		if (pointer_decl_star(tokens, n, i))
			return 0;
		if (binary_star(prev, next))
			return 1;
		if (prev->kind == TIdent || pp == PStar)
			return 1;
		return 0;
	}

	if (pp == PDotDot) {
		if (cur->kind == TPunct && cur->punct == PRbrack)
			return 0;
		return 1;
	}
	if (pp == PRparen && operand_after_cast(cur) && paren_expr_is_cast(tokens, (int)(prev - tokens)))
		return 0;
	if (pp == PLparen || pp == PLbrack || pp == PDot || pp == PArrow)
		return 0;
	if (pp == PBang || pp == PTilde || pp == PPlusPlus || pp == PMinusMinus)
		return 0;

	if (pp == PStar) {
		if (pointer_decl_star(tokens, n, (int)(prev - tokens)) && cur->kind == TIdent)
			return 1;
		left = prev_code(tokens, (int)(prev - tokens));
		if (!binary_star(left, cur)) {
			if (cur->kind == TIdent || cur->kind == TNumber || (cur->kind == TPunct && (cur->punct == PStar || cur->punct == PLparen || cur->punct == PAmp)))
				return 0;
		}
	}
	if (pp == PAmp) {
		left = prev_code(tokens, (int)(prev - tokens));
		if (!is_atom(left)) {
			if (cur->kind == TIdent || cur->kind == TKw || (cur->kind == TPunct && cur->punct == PLparen))
				return 0;
		}
	}
	if (pp == PPlus || pp == PMinus) {
		left = prev_code(tokens, (int)(prev - tokens));
		if (unary_context(left) && !is_atom(left)) {
			if (cur->kind == TIdent || cur->kind == TNumber || (cur->kind == TPunct && cur->punct == PLparen))
				return 0;
		}
	}

	if (pp == PRbrace && cur->kind == TKw && cur->kw == K_else)
		return 1;

	return 1;
}

// Write one token's spelling into the output buffer.
static void
emit_tok(Out* o, Tok* t) {
	if (t->kind == TIdent || t->kind == TKw || t->kind == TNumber)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TString) {
		o_putc(o, '"');
		o_puts(o, t->s ? t->s : "");
		o_putc(o, '"');
	} else if (t->kind == TCharLit) {
		o_putc(o, '\'');
		o_puts(o, t->s ? t->s : "");
		o_putc(o, '\'');
	} else if (t->kind == THeader)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TComment)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TPunct)
		o_puts(o, punct_spell(t->punct));
}

// Reformat lexed tokens into canonical ModC source (K&R, tabs, T* style).
char* fmt_source(Compiler* c) {
	Out o;
	Tok *tokens, *cur, *next;
	int i, n, nln, pending_blank;

	memset(&o, 0, sizeof(o));
	o.bol = 1;
	tokens = c->tokens;
	n = c->tokens_len;
	if (n == 0)
		return xstrdup("");

	pending_blank = 0;
	for (i = 0; i < n; i++) {
		cur = &tokens[i];
		if (cur->kind == TEof)
			break;

		if (cur->kind == TNewline) {
			nln = 0;
			while (i < n && tokens[i].kind == TNewline) {
				nln++;
				i++;
			}
			i--;
			/* Directives end at newline — never glue following code onto #if/#else. */
			if (o.in_dir) {
				o.in_dir = 0;
				o_nl(&o);
			}
			if (nln >= 2 && o.depth == 0 && o.paren == 0)
				pending_blank = 1;
			continue;
		}

		next = next_code(tokens, n, i);

		/* start # directive */
		if (cur->kind == TPunct && cur->punct == PHash && o.paren == 0 && (o.bol || cur->bol)) {
			if (!o.bol && o.col > 0)
				o_nl(&o);
			if (pending_blank && o.n > 0) {
				if (o.blank < 2)
					o_putc(&o, '\n');
				pending_blank = 0;
			}
			o.in_dir = 1;
			o.bol = 0;
			emit_tok(&o, cur);
			continue;
		}

		if (pending_blank && o.depth == 0 && o.paren == 0 && !o.in_dir) {
			if (o.col > 0)
				o_nl(&o);
			if (o.blank < 2)
				o_putc(&o, '\n');
			pending_blank = 0;
			o.bol = 1;
		}

		if (cur->kind == TComment) {
			if (o.bol && !o.in_dir)
				o_indent(&o);
			else if (o.col > 0)
				o_putc(&o, ' ');
			emit_tok(&o, cur);
			if (cur->s && cur->s[0] == '/' && cur->s[1] == '/')
				o_nl(&o);
			else if (next && next->kind != TEof)
				o_nl(&o);
			continue;
		}

		if (cur->kind == TPunct && cur->punct == PRbrace && o.depth > 0)
			o.depth--;

		{
			int line_start;

			line_start = o.bol;
			if (line_start && !o.in_dir)
				o_indent(&o);

			if (!line_start && o.col > 0) {
				int space;

				if (o.in_dir)
					space = dir_space(prev_code(tokens, i), cur);
				else
					space = space_between(tokens, n, i);
				if (space)
					o_putc(&o, ' ');
			}
		}

		emit_tok(&o, cur);

		if (cur->kind == TPunct) {
			if (cur->punct == PLparen)
				o.paren++;
			else if (cur->punct == PRparen && o.paren > 0)
				o.paren--;
			else if (cur->punct == PLbrack)
				o.brack++;
			else if (cur->punct == PRbrack && o.brack > 0)
				o.brack--;
			else if (cur->punct == PLbrace)
				o.depth++;
		}

		if (o.in_dir)
			continue;

		if (cur->kind == TPunct) {
			if (cur->punct == PLbrace)
				o_nl(&o);
			else if (cur->punct == PSemi && o.paren == 0 && o.brack == 0)
				o_nl(&o);
			else if (cur->punct == PRbrace) {
				if (next && next->kind == TKw && next->kw == K_else) {
					/* } else on same line */
				} else if (next && next->kind == TPunct && (next->punct == PSemi || next->punct == PComma)) {
					/* }; or }, */
				} else
					o_nl(&o);
			}
		}
	}

	if (o.n == 0 || o.buf[o.n - 1] != '\n')
		o_putc(&o, '\n');
	o_grow(&o, 1);
	o.buf[o.n] = 0;
	return o.buf;
}
