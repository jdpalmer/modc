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
	case KwChar:
	case KwShort:
	case KwInt:
	case KwLong:
	case KwFloat:
	case KwDouble:
	case KwVoid:
	case KwBool:
	case KwStruct:
	case KwUnion:
	case KwEnum:
	case KwSigned:
	case KwUnsigned:
	case KwConst:
	case KwVolatile:
	case KwRestrict:
	case KwTypedef:
	case KwAuto:
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

	if (i < 0 || i >= n || tokens[i].kind != TkPunct || tokens[i].punct != PnLparen)
		return 0;
	t = next_code(tokens, n, i);
	if (t == NULL)
		return 0;
	if (t->kind == TkKw && (t->kw == KwStruct || t->kw == KwUnion || t->kw == KwEnum)) {
		t = next_code(tokens, n, (int)(t - tokens));
		if (t == NULL || t->kind != TkIdent)
			return 0;
	} else if (t->kind != TkIdent)
		return 0;
	star = next_code(tokens, n, (int)(t - tokens));
	if (star == NULL || star->kind != TkPunct || star->punct != PnStar)
		return 0;
	while (star && star->kind == TkPunct && star->punct == PnStar)
		star = next_code(tokens, n, (int)(star - tokens));
	name = star;
	if (name == NULL || name->kind != TkIdent)
		return 0;
	rp = next_code(tokens, n, (int)(name - tokens));
	if (rp == NULL || rp->kind != TkPunct || rp->punct != PnRparen)
		return 0;
	dot = next_code(tokens, n, (int)(rp - tokens));
	if (dot == NULL || dot->kind != TkPunct || dot->punct != PnDot)
		return 0;
	meth = next_code(tokens, n, (int)(dot - tokens));
	return meth != NULL && meth->kind == TkIdent;
}

// Control keywords that need a space before their following '('.
static int
kw_space_before_paren(int kw) {
	switch (kw) {
	case KwIf:
	case KwFor:
	case KwWhile:
	case KwSwitch:
	case KwSizeof:
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
	if (t->kind == TkIdent || t->kind == TkNumber || t->kind == TkString || t->kind == TkCharLit)
		return 1;
	if (t->kind == TkKw && (t->kw == KwTrue || t->kw == KwFalse))
		return 1;
	if (t->kind == TkPunct) {
		switch (t->punct) {
		case PnRparen:
		case PnRbrack:
		case PnPlusPlus:
		case PnMinusMinus:
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
	if (t->kind == TkIdent || t->kind == TkNumber || t->kind == TkString || t->kind == TkCharLit || t->kind == TkKw)
		return 1;
	if (t->kind == TkPunct) {
		switch (t->punct) {
		case PnLparen:
		case PnLbrack:
		case PnPlus:
		case PnMinus:
		case PnStar:
		case PnAmp:
		case PnBang:
		case PnTilde:
		case PnPlusPlus:
		case PnMinusMinus:
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
	if (before->kind == TkKw)
		return before->kw == KwReturn || before->kw == KwCase;
	if (before->kind != TkPunct)
		return 0;
	switch (before->punct) {
	case PnLparen:
	case PnLbrack:
	case PnLbrace:
	case PnEq:
	case PnPlusEq:
	case PnMinusEq:
	case PnStarEq:
	case PnSlashEq:
	case PnPercentEq:
	case PnAmpEq:
	case PnPipeEq:
	case PnCaretEq:
	case PnShlEq:
	case PnShrEq:
	case PnComma:
	case PnSemi:
	case PnColon:
	case PnQuestion:
	case PnEqEq:
	case PnBangEq:
	case PnLt:
	case PnGt:
	case PnLe:
	case PnGe:
	case PnShl:
	case PnShr:
	case PnPlus:
	case PnMinus:
	case PnStar:
	case PnSlash:
	case PnPercent:
	case PnAmp:
	case PnPipe:
	case PnCaret:
	case PnAmpAmp:
	case PnPipePipe:
	case PnBang:
	case PnTilde:
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
		if (tokens[j].kind == TkNewline || tokens[j].kind == TkComment)
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
		if (tokens[j].kind == TkEof)
			return NULL;
		if (tokens[j].kind == TkNewline || tokens[j].kind == TkComment)
			continue;
		return &tokens[j];
	}
	return NULL;
}

// True for infix operators that take spaces on both sides.
static int
is_binary_punct(int p) {
	switch (p) {
	case PnPlus:
	case PnMinus:
	case PnStar:
	case PnSlash:
	case PnPercent:
	case PnAmp:
	case PnPipe:
	case PnCaret:
	case PnShl:
	case PnShr:
	case PnEqEq:
	case PnBangEq:
	case PnLt:
	case PnGt:
	case PnLe:
	case PnGe:
	case PnAmpAmp:
	case PnPipePipe:
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
	if (cur->kind == TkIdent || cur->kind == TkNumber || cur->kind == TkCharLit || cur->kind == TkString)
		return 1;
	if (cur->kind == TkPunct) {
		switch (cur->punct) {
		case PnLparen:
		case PnStar:
		case PnAmp:
		case PnBang:
		case PnTilde:
		case PnPlusPlus:
		case PnMinusMinus:
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
	if (prev == NULL || prev->kind != TkKw)
		return 0;
	switch (prev->kw) {
	case KwReturn:
	case KwCase:
	case KwAuto:
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
	if (prev->kind == TkKw) {
		if (kw_space_before_paren(prev->kw) || kw_space_after(prev))
			return 1;
		return 0;
	}
	if (prev->kind == TkPunct) {
		if (prev->punct == PnComma || prev->punct == PnEq)
			return 1;
		if (prev->punct == PnPlus || prev->punct == PnMinus) {
			left = prev_code(tokens, (int)(prev - tokens));
			if (left && is_atom(left))
				return 1;
			return 0;
		}
		if (prev->punct == PnStar) {
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

		if (t->kind == TkNewline || t->kind == TkComment)
			continue;
		if (t->kind != TkPunct)
			continue;
		if (t->punct == PnRparen) {
			if (depth == 0)
				depth = 1;
			else
				depth++;
		} else if (t->punct == PnLparen) {
			depth--;
			if (depth == 0)
				return !comma;
		} else if (t->punct == PnComma && depth == 1)
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
		if (t->kind == TkNewline || t->kind == TkComment)
			continue;
		if (t->kind == TkIdent)
			return 1;
		if (t->kind == TkKw && (t->kw == KwTypedef || is_type_kw(t->kw)))
			continue;
		if (t->kind == TkPunct && (t->punct == PnStar || t->punct == PnRparen || t->punct == PnComma))
			continue;
		return 0;
	}
	return 0;
}

// Pointer declarator * (T* name): attach * to the type, not unary/binary *.
static int
pointer_decl_star(Tok* tokens, int n, int i) {
	Tok *prev, *next, *before;

	if (tokens[i].kind != TkPunct || tokens[i].punct != PnStar)
		return 0;
	prev = prev_code(tokens, i);
	next = next_code(tokens, n, i);
	if (prev == NULL)
		return 0;
	if (prev->kind == TkKw && is_type_kw(prev->kw))
		return 1;
	if (prev->kind == TkPunct && prev->punct == PnStar)
		return 1;
	if (prev->kind == TkPunct && prev->punct == PnRparen && paren_expr_is_cast(tokens, (int)(prev - tokens)))
		return 1;
	if (prev->kind == TkIdent && next && next->kind == TkIdent) {
		int j;

		for (j = i - 1; j >= 0; j--) {
			Tok* t = &tokens[j];

			if (t->kind == TkNewline || t->kind == TkComment)
				continue;
			if (t->kind == TkPunct) {
				if (t->punct == PnLparen) {
					if (method_recv_lparen(tokens, n, j) || func_param_lparen(tokens, j))
						return 1;
					return 0;
				}
				if (t->punct == PnSemi || t->punct == PnLbrace || t->punct == PnComma)
					return 1;
				if (is_binary_punct(t->punct) || t->punct == PnEq || t->punct == PnLbrack || t->punct == PnRbrack || t->punct == PnQuestion || t->punct == PnColon)
					return 0;
				continue;
			}
			if (t->kind == TkKw) {
				switch (t->kw) {
				case KwReturn:
				case KwIf:
				case KwFor:
				case KwWhile:
				case KwSwitch:
				case KwCase:
				case KwElse:
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
	if (prev->kind == TkIdent && next && next->kind == TkPunct && next->punct == PnLparen) {
		before = prev_code(tokens, (int)(prev - tokens));
		if (before && before->kind == TkPunct && before->punct == PnStar)
			return 1;
		if (before && ((before->kind == TkKw && is_type_kw(before->kw)) || before->kind == TkIdent))
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
	cp = cur->kind == TkPunct ? cur->punct : -1;
	pp = prev->kind == TkPunct ? prev->punct : -1;

	if (pp == PnHash)
		return 0; /* #include */
	if (cp == PnLt)
		return 1; /* include <...> */
	if (pp == PnLt)
		return 0; /* <stdio.h> glued */
	if (cp == PnGt || pp == PnDot || cp == PnDot || pp == PnSlash || cp == PnSlash)
		return 0;
	if (cp == PnLparen && (prev->kind == TkIdent || prev->kind == TkKw) && !cur->ws)
		return 0;
	if (cp == PnComma || cp == PnRparen)
		return 0;
	if (prev->kind == TkString || cur->kind == TkString)
		return 1;
	if (prev->kind == TkKw || prev->kind == TkIdent)
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

	if (cur->kind == TkComment)
		return 1;

	cp = cur->kind == TkPunct ? cur->punct : -1;
	pp = prev->kind == TkPunct ? prev->punct : -1;

	if (cp == PnComma || cp == PnSemi || cp == PnRparen || cp == PnRbrack)
		return 0;
	if (cp == PnDot || cp == PnArrow)
		return 0;
	if (cp == PnDotDot) {
		if (prev->kind == TkPunct && prev->punct == PnLbrack)
			return 0;
		return 1;
	}
	if (cp == PnPlusPlus || cp == PnMinusMinus)
		return 0;
	if (cp == PnLbrack || cp == PnColon)
		return 0;

	if (cp == PnLparen)
		return space_before_lparen(tokens, n, i);

	if (cp == PnLbrace)
		return 1;

	if (cp == PnStar) {
		if (pp == PnEq || pp == PnPlusEq || pp == PnMinusEq || pp == PnStarEq || pp == PnSlashEq || pp == PnPercentEq || pp == PnAmpEq || pp == PnPipeEq || pp == PnCaretEq || pp == PnShlEq || pp == PnShrEq)
			return 1;
		if (pointer_decl_star(tokens, n, i))
			return 0;
		if (binary_star(prev, next))
			return 1;
		if (prev->kind == TkIdent || pp == PnStar)
			return 1;
		return 0;
	}

	if (pp == PnDotDot) {
		if (cur->kind == TkPunct && cur->punct == PnRbrack)
			return 0;
		return 1;
	}
	if (pp == PnRparen && operand_after_cast(cur) && paren_expr_is_cast(tokens, (int)(prev - tokens)))
		return 0;
	if (pp == PnLparen || pp == PnLbrack || pp == PnDot || pp == PnArrow)
		return 0;
	if (pp == PnBang || pp == PnTilde || pp == PnPlusPlus || pp == PnMinusMinus)
		return 0;

	if (pp == PnStar) {
		if (pointer_decl_star(tokens, n, (int)(prev - tokens)) && cur->kind == TkIdent)
			return 1;
		left = prev_code(tokens, (int)(prev - tokens));
		if (!binary_star(left, cur)) {
			if (cur->kind == TkIdent || cur->kind == TkNumber || (cur->kind == TkPunct && (cur->punct == PnStar || cur->punct == PnLparen || cur->punct == PnAmp)))
				return 0;
		}
	}
	if (pp == PnAmp) {
		left = prev_code(tokens, (int)(prev - tokens));
		if (!is_atom(left)) {
			if (cur->kind == TkIdent || cur->kind == TkKw || (cur->kind == TkPunct && cur->punct == PnLparen))
				return 0;
		}
	}
	if (pp == PnPlus || pp == PnMinus) {
		left = prev_code(tokens, (int)(prev - tokens));
		if (unary_context(left) && !is_atom(left)) {
			if (cur->kind == TkIdent || cur->kind == TkNumber || (cur->kind == TkPunct && cur->punct == PnLparen))
				return 0;
		}
	}

	if (pp == PnRbrace && cur->kind == TkKw && cur->kw == KwElse)
		return 1;

	return 1;
}

// Write one token's spelling into the output buffer.
static void
emit_tok(Out* o, Tok* t) {
	if (t->kind == TkIdent || t->kind == TkKw || t->kind == TkNumber)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TkString) {
		o_putc(o, '"');
		o_puts(o, t->s ? t->s : "");
		o_putc(o, '"');
	} else if (t->kind == TkCharLit) {
		o_putc(o, '\'');
		o_puts(o, t->s ? t->s : "");
		o_putc(o, '\'');
	} else if (t->kind == TkHeader)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TkComment)
		o_puts(o, t->s ? t->s : "");
	else if (t->kind == TkPunct)
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
		if (cur->kind == TkEof)
			break;

		if (cur->kind == TkNewline) {
			nln = 0;
			while (i < n && tokens[i].kind == TkNewline) {
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
		if (cur->kind == TkPunct && cur->punct == PnHash && o.paren == 0 && (o.bol || cur->bol)) {
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

		if (cur->kind == TkComment) {
			if (o.bol && !o.in_dir)
				o_indent(&o);
			else if (o.col > 0)
				o_putc(&o, ' ');
			emit_tok(&o, cur);
			if (cur->s && cur->s[0] == '/' && cur->s[1] == '/')
				o_nl(&o);
			else if (next && next->kind != TkEof)
				o_nl(&o);
			continue;
		}

		if (cur->kind == TkPunct && cur->punct == PnRbrace && o.depth > 0)
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

		if (cur->kind == TkPunct) {
			if (cur->punct == PnLparen)
				o.paren++;
			else if (cur->punct == PnRparen && o.paren > 0)
				o.paren--;
			else if (cur->punct == PnLbrack)
				o.brack++;
			else if (cur->punct == PnRbrack && o.brack > 0)
				o.brack--;
			else if (cur->punct == PnLbrace)
				o.depth++;
		}

		if (o.in_dir)
			continue;

		if (cur->kind == TkPunct) {
			if (cur->punct == PnLbrace)
				o_nl(&o);
			else if (cur->punct == PnSemi && o.paren == 0 && o.brack == 0)
				o_nl(&o);
			else if (cur->punct == PnRbrace) {
				if (next && next->kind == TkKw && next->kw == KwElse) {
					/* } else on same line */
				} else if (next && next->kind == TkPunct && (next->punct == PnSemi || next->punct == PnComma)) {
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
