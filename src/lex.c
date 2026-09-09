/*
 * Lexer: source text → Tok[].
 *
 * Compilation pipeline: [lex] → pp → parse → type check → emit → QBE
 *
 * Handles \\newline splicing, digraphs, keywords (including iso646 alts),
 * string/char escapes, and #include header-name form. Does not expand macros —
 * that is pp.c. Entry: lex_file.
 */
#include "ast.h"

static const char* kwname[] = {
    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "defer",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "fallthrough",
    "float",
    "for",
    "goto",
    "if",
    "import",
    "inline",
    "int",
    "long",
    "overload",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    "_Bool",
    "true",
    "false",
};

// Map an identifier spelling to a keyword id, or -1 if it is not reserved.
int keyword(const char* s) {
	static const char* keys[128];
	static int vals[128];
	static int ready;
	unsigned h, i;

	if (s == NULL || s[0] == 0)
		return -1;
	if (!ready) {
		int k;
		memset(keys, 0, sizeof(keys));
		for (k = 0; k < K_nkw; k++) {
			h = str_hash(kwname[k]) & 127u;
			while (keys[h])
				h = (h + 1) & 127u;
			keys[h] = kwname[k];
			vals[h] = k;
		}
		/* Aliases not in kwname[]. */
		{
			const char* alias[] = {"bool", "_Static_assert"};
			int akw[] = {K_bool, K_static_assert};
			for (k = 0; k < 2; k++) {
				h = str_hash(alias[k]) & 127u;
				while (keys[h])
					h = (h + 1) & 127u;
				keys[h] = alias[k];
				vals[h] = akw[k];
			}
		}
		ready = 1;
	}
	h = str_hash(s) & 127u;
	for (i = 0; i < 128; i++) {
		if (keys[h] == NULL)
			return -1;
		if (strcmp(keys[h], s) == 0)
			return vals[h];
		h = (h + 1) & 127u;
	}
	return -1;
}

static const char* alt[] = {
    "and", "and_eq", "bitand", "bitor", "compl",
    "not", "not_eq", "or", "or_eq", "xor", "xor_eq",
    NULL};

// Read a file into a null-terminated buffer; returns NULL on open failure.
char* read_file(const char* path, size_t* outlen) {
	FILE* f;
	char* buf;
	size_t n, cap;
	long sz;

	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	sz = -1;
	if (fseek(f, 0, SEEK_END) == 0) {
		sz = ftell(f);
		if (fseek(f, 0, SEEK_SET) != 0)
			sz = -1;
	}
	if (sz >= 0) {
		cap = (size_t)sz + 1;
		buf = xmalloc(cap);
		n = fread(buf, 1, (size_t)sz, f);
		buf[n] = 0;
		fclose(f);
		if (outlen)
			*outlen = n;
		return buf;
	}
	cap = 4096;
	n = 0;
	buf = xmalloc(cap);
	for (;;) {
		size_t r;
		if (n + 1024 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		r = fread(buf + n, 1, cap - n - 1, f);
		if (r == 0)
			break;
		n += r;
	}
	fclose(f);
	buf[n] = 0;
	if (outlen)
		*outlen = n;
	return buf;
}

// Remove backslash-newline splices so the lexer sees logical source lines.
static char*
splice_lines(const char* in) {
	size_t n, i, j;
	char* out;
	int need;

	n = strlen(in);
	need = 0;
	for (i = 0; i < n; i++) {
		if (in[i] == '\\' && (in[i + 1] == '\n' || (in[i + 1] == '\r' && in[i + 2] == '\n'))) {
			need = 1;
			break;
		}
	}
	if (!need) {
		out = xmalloc(n + 2);
		memcpy(out, in, n + 1);
		return out;
	}
	out = xmalloc(n + 2);
	j = 0;
	for (i = 0; in[i]; i++) {
		if (in[i] == '\\' && in[i + 1] == '\n') {
			i++;
			continue;
		}
		if (in[i] == '\\' && in[i + 1] == '\r' && in[i + 2] == '\n') {
			i += 2;
			continue;
		}
		out[j++] = in[i];
	}
	out[j] = 0;
	return out;
}

// Append one token to c->tokens, attaching any pending doc comment to it.
static void
addtok(Compiler* c, Tok t) {
	if (t.kind != TNewline && t.kind != TEof && c->pending_doc) {
		t.doc = c->pending_doc;
		c->pending_doc = NULL;
	}
	if (c->tokens_len >= c->tokens_cap) {
		c->tokens_cap = c->tokens_cap ? c->tokens_cap * 2 : 256;
		c->tokens = xrealloc(c->tokens, c->tokens_cap * sizeof(Tok));
	}
	c->tokens[c->tokens_len++] = t;
}

// Discard accumulated leading doc text before the next declaration.
static void
doc_clear(Compiler* c) {
	free(c->pending_doc);
	c->pending_doc = NULL;
}

// Append one trimmed // or /* */ line to the pending doc string for the next decl.
static void
doc_append_line(Compiler* c, const char* text, int n) {
	int olen, i;

	while (n > 0 && (text[0] == ' ' || text[0] == '\t')) {
		text++;
		n--;
	}
	while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r'))
		n--;
	if (n < 0)
		n = 0;
	olen = c->pending_doc ? (int)strlen(c->pending_doc) : 0;
	c->pending_doc = xrealloc(c->pending_doc, (size_t)olen + (size_t)n + 2);
	if (olen) {
		c->pending_doc[olen] = '\n';
		olen++;
	}
	for (i = 0; i < n; i++)
		c->pending_doc[olen + i] = text[i];
	c->pending_doc[olen + n] = 0;
}

// True for the first character of an identifier (letters, _, $, UTF-8 lead).
// '$' is an MSVC extension required by Windows SDK SAL macros.
static int
isident1(int ch) {
	return isalpha(ch) || ch == '_' || ch == '$' || (unsigned char)ch >= 0x80;
}

// True for identifier continuation characters (includes digits and '$').
static int
isident(int ch) {
	return isalnum(ch) || ch == '_' || ch == '$' || (unsigned char)ch >= 0x80;
}

// Lex one source file into c->tokens (keywords, literals, punct, comments, newlines).
void lex_file(Compiler* c, const char* path, const char* raw, int bol_start) {
	char* src;
	int i, line, col, startcol, ch, kw, bol, hadws, ban_alt;
	Tok t;
	Span sp;

	src = splice_lines(raw);
	if (c->src_files_len % 8 == 0) {
		c->src_files = xrealloc(c->src_files, (c->src_files_len + 8) * sizeof(char*));
		c->src_text = xrealloc(c->src_text, (c->src_files_len + 8) * sizeof(char*));
	}
	/* Stable copy: callers may free their path; spans keep this pointer. */
	c->src_files[c->src_files_len] = xstrdup(path);
	c->src_text[c->src_files_len] = src;
	path = c->src_files[c->src_files_len];
	c->src_files_len++;

	/* iso646 ban only for user TUs — decide once per file, not per ident. */
	ban_alt = 0;
	if (c->unit_files_len == 0)
		ban_alt = 1;
	else {
		int u;
		for (u = 0; u < c->unit_files_len; u++) {
			if (c->unit_files[u] && strcmp(path, c->unit_files[u]) == 0) {
				ban_alt = 1;
				break;
			}
		}
	}

	i = 0;
	line = 1;
	col = 1;
	bol = bol_start;
	doc_clear(c);
	{
		int after_doc = 0;

		while (src[i]) {
			int at_bol;

			if (c->fatal)
				break;
			hadws = 0;
			at_bol = bol;
			while (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\f' || src[i] == '\v') {
				hadws = 1;
				if (src[i] == '\t')
					col = (col + 7) / 8 * 8 + 1;
				else
					col++;
				i++;
			}
			if (src[i] == '/' && src[i + 1] == '/') {
				int start, end;

				hadws = 1;
				start = i + 2;
				end = start;
				while (src[end] && src[end] != '\n')
					end++;
				if (c->keep_comments) {
					memset(&t, 0, sizeof(t));
					t.kind = TComment;
					t.s = xstrndup(src + i, end - i);
					t.span.file = path;
					t.span.line = line;
					t.span.col = col;
					t.span.endcol = col + (end - i);
					t.bol = at_bol;
					t.ws = hadws;
					addtok(c, t);
				} else if (at_bol) {
					doc_append_line(c, src + start, end - start);
					after_doc = 1;
				}
				if (at_bol && c->keep_comments)
					after_doc = 1;
				col += end - i;
				i = end;
				continue;
			}
			if (src[i] == '/' && src[i + 1] == '*') {
				int start, end, cstart, ccol;

				hadws = 1;
				cstart = i;
				ccol = col;
				i += 2;
				col += 2;
				start = i;
				while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) {
					if (src[i] == '\n') {
						if (!c->keep_comments && at_bol && i >= start)
							doc_append_line(c, src + start, i - start);
						start = i + 1;
						line++;
						col = 1;
						bol = 1;
						if (at_bol)
							after_doc = 1;
					} else
						col++;
					i++;
				}
				end = i;
				if (!c->keep_comments && at_bol) {
					doc_append_line(c, src + start, end - start);
					after_doc = 1;
				}
				if (src[i]) {
					i += 2;
					col += 2;
				}
				if (c->keep_comments) {
					memset(&t, 0, sizeof(t));
					t.kind = TComment;
					t.s = xstrndup(src + cstart, i - cstart);
					t.span.file = path;
					t.span.line = line; /* end line; ok for fmt */
					t.span.col = ccol;
					t.span.endcol = col;
					t.bol = at_bol;
					t.ws = hadws;
					addtok(c, t);
					if (at_bol)
						after_doc = 1;
				}
				continue;
			}
			if (src[i] == 0)
				break;
			if (src[i] == '\n') {
				if (at_bol && !after_doc)
					doc_clear(c);
				after_doc = 0;
				memset(&t, 0, sizeof(t));
				t.kind = TNewline;
				t.span.file = path;
				t.span.line = line;
				t.span.col = col;
				t.bol = bol;
				t.ws = hadws;
				addtok(c, t);
				i++;
				line++;
				col = 1;
				bol = 1;
				continue;
			}
			if (src[i] == '?' && src[i + 1] == '?') {
				sp.file = path;
				sp.line = line;
				sp.col = col;
				error_at(c, sp, "%%C does not support trigraphs");
				i += 2;
				col += 2;
				bol = 0;
				continue;
			}
			/*
			 * Outside strings, MSVC/Clang keep odd `\t` sequences in SDK
			 * macros (SAL). Treat known escapes as whitespace; bare `\` alone
			 * is skipped so headers can lex.
			 */
			if (src[i] == '\\' && src[i + 1] != 0 && src[i + 1] != '\n' && src[i + 1] != '\r') {
				int esc = (unsigned char)src[i + 1];
				i++;
				col++;
				if (strchr("tnrabeefv\\'\"?", esc)) {
					i++;
					col++;
				}
				hadws = 1;
				continue;
			}
			/* digraphs */
			if ((src[i] == '<' && src[i + 1] == '%') ||
			    (src[i] == '%' && src[i + 1] == '>') ||
			    (src[i] == '<' && src[i + 1] == ':') ||
			    (src[i] == ':' && src[i + 1] == '>') ||
			    (src[i] == '%' && src[i + 1] == ':')) {
				sp.file = path;
				sp.line = line;
				sp.col = col;
				error_at(c, sp, "%%C does not support digraphs");
				i += 2;
				col += 2;
				bol = 0;
				continue;
			}

			startcol = col;
			memset(&t, 0, sizeof(t));
			t.span.file = path;
			t.span.line = line;
			t.span.col = startcol;
			t.bol = bol;
			t.ws = hadws;
			bol = 0;
			ch = (unsigned char)src[i];

			if (isident1(ch)) {
				int j = i;
				while (isident((unsigned char)src[j]))
					j++;
				t.s = str_intern_n(c, src + i, (size_t)(j - i));
				col += j - i;
				i = j;
				kw = keyword(t.s);
				if (kw >= 0) {
					t.kind = TKw;
					t.kw = kw;
				} else {
					int a;
					/* iso646 alts banned in user .mc; headers may spell them. */
					if (ban_alt) {
						for (a = 0; alt[a]; a++) {
							if (strcmp(t.s, alt[a]) == 0) {
								error_at(c, t.span,
									 "%%C does not support alternative token \"%s\"",
									 t.s);
								break;
							}
						}
					}
					t.kind = TIdent;
				}
				t.span.endcol = col;
				addtok(c, t);
				continue;
			}
			/* MSVC apiset .def sugar: `X @##ordinal` in macro bodies. */
			if (ch == '@') {
				t.kind = TIdent;
				t.s = str_intern(c, "@");
				i++;
				col++;
				t.span.endcol = col;
				addtok(c, t);
				continue;
			}

			if (ch == '.' && isdigit((unsigned char)src[i + 1]))
				goto number;
			if (isdigit(ch)) {
			number: {
				int j = i;
				int hexfloat = 0, octal = 0, binary = 0, isfloat = 0;
				unsigned long long uv = 0;

				if (src[j] == '0' && (src[j + 1] == 'x' || src[j + 1] == 'X')) {
					j += 2;
					while (isxdigit((unsigned char)src[j]))
						j++;
					if (src[j] == '.' || src[j] == 'p' || src[j] == 'P')
						hexfloat = 1;
					if (hexfloat) {
						if (src[j] == '.') {
							j++;
							while (isxdigit((unsigned char)src[j]))
								j++;
						}
						if (src[j] == 'p' || src[j] == 'P') {
							j++;
							if (src[j] == '+' || src[j] == '-')
								j++;
							while (isdigit((unsigned char)src[j]))
								j++;
						}
					}
				} else if (src[j] == '0' && (src[j + 1] == 'b' || src[j + 1] == 'B') && (src[j + 2] == '0' || src[j + 2] == '1')) {
					binary = 1;
					j += 2;
					while (src[j] == '0' || src[j] == '1') {
						uv = (uv << 1) | (unsigned)(src[j] - '0');
						j++;
					}
				} else {
					if (src[j] == '0' && isdigit((unsigned char)src[j + 1]))
						octal = 1;
					while (isdigit((unsigned char)src[j]))
						j++;
					/* `3..` is int 3 + `..`, not float `3.` */
					if (src[j] == '.' && src[j + 1] != '.') {
						octal = 0;
						isfloat = 1;
						j++;
						while (isdigit((unsigned char)src[j]))
							j++;
					}
					if (src[j] == 'e' || src[j] == 'E') {
						octal = 0;
						isfloat = 1;
						j++;
						if (src[j] == '+' || src[j] == '-')
							j++;
						while (isdigit((unsigned char)src[j]))
							j++;
					}
					(void)isfloat;
				}
				while (isalpha((unsigned char)src[j]))
					j++;
				t.kind = TNumber;
				t.s = xstrndup(src + i, j - i);
				if (binary)
					t.int_val = (int64_t)uv;
				else
					t.int_val = strtoll(t.s, NULL, 0);
				col += j - i;
				i = j;
				t.span.endcol = col;
				if (user_source(c, t.span)) {
					if (hexfloat)
						error_at(c, t.span, "%%C does not support hex floats");
					else if (octal)
						error_at(c, t.span, "%%C does not support octal literals");
				}
				addtok(c, t);
				continue;
			}
			}

			if (ch == '\'' || ch == '"') {
				int quote = ch;
				int j = i + 1;
				col++;
				i++;
				while (src[j] && src[j] != quote) {
					if (src[j] == '\\' && src[j + 1]) {
						j += 2;
						col += 2;
					} else {
						if (src[j] == '\n')
							break;
						j++;
						col++;
					}
				}
				t.s = xstrndup(src + i, j - i);
				if (src[j] == quote) {
					j++;
					col++;
				} else
					error_at(c, t.span, "unterminated %s", quote == '"' ? "string" : "character constant");
				t.kind = quote == '"' ? TString : TCharLit;
				if (t.kind == TCharLit) {
					if (t.s[0] == '\\') {
						switch (t.s[1]) {
						case 'n':
							t.int_val = '\n';
							break;
						case 't':
							t.int_val = '\t';
							break;
						case 'r':
							t.int_val = '\r';
							break;
						case '0':
							t.int_val = 0;
							break;
						case '\\':
							t.int_val = '\\';
							break;
						case '\'':
							t.int_val = '\'';
							break;
						default:
							t.int_val = (unsigned char)t.s[1];
							break;
						}
					} else
						t.int_val = (unsigned char)t.s[0];
				}
				i = j;
				t.span.endcol = col;
				addtok(c, t);
				continue;
			}

			t.kind = TPunct;
			if (src[i] == '.' && src[i + 1] == '.' && src[i + 2] == '.') {
				t.punct = PEllipsis;
				i += 3;
				col += 3;
			} else if (src[i] == '.' && src[i + 1] == '.') {
				t.punct = PDotDot;
				i += 2;
				col += 2;
			} else if (src[i] == '<' && src[i + 1] == '<' && src[i + 2] == '=') {
				t.punct = PShlEq;
				i += 3;
				col += 3;
			} else if (src[i] == '>' && src[i + 1] == '>' && src[i + 2] == '=') {
				t.punct = PShrEq;
				i += 3;
				col += 3;
			} else if (src[i] == '#' && src[i + 1] == '#') {
				t.punct = PHashHash;
				i += 2;
				col += 2;
			} else if (src[i] == '+' && src[i + 1] == '+') {
				t.punct = PPlusPlus;
				i += 2;
				col += 2;
			} else if (src[i] == '-' && src[i + 1] == '-') {
				t.punct = PMinusMinus;
				i += 2;
				col += 2;
			} else if (src[i] == '-' && src[i + 1] == '>') {
				t.punct = PArrow;
				i += 2;
				col += 2;
			} else if (src[i] == '<' && src[i + 1] == '<') {
				t.punct = PShl;
				i += 2;
				col += 2;
			} else if (src[i] == '>' && src[i + 1] == '>') {
				t.punct = PShr;
				i += 2;
				col += 2;
			} else if (src[i] == '&' && src[i + 1] == '&') {
				t.punct = PAmpAmp;
				i += 2;
				col += 2;
			} else if (src[i] == '|' && src[i + 1] == '|') {
				t.punct = PPipePipe;
				i += 2;
				col += 2;
			} else if (src[i] == '=' && src[i + 1] == '=') {
				t.punct = PEqEq;
				i += 2;
				col += 2;
			} else if (src[i] == '!' && src[i + 1] == '=') {
				t.punct = PBangEq;
				i += 2;
				col += 2;
			} else if (src[i] == '<' && src[i + 1] == '=') {
				t.punct = PLe;
				i += 2;
				col += 2;
			} else if (src[i] == '>' && src[i + 1] == '=') {
				t.punct = PGe;
				i += 2;
				col += 2;
			} else if (src[i] == '+' && src[i + 1] == '=') {
				t.punct = PPlusEq;
				i += 2;
				col += 2;
			} else if (src[i] == '-' && src[i + 1] == '=') {
				t.punct = PMinusEq;
				i += 2;
				col += 2;
			} else if (src[i] == '*' && src[i + 1] == '=') {
				t.punct = PStarEq;
				i += 2;
				col += 2;
			} else if (src[i] == '/' && src[i + 1] == '=') {
				t.punct = PSlashEq;
				i += 2;
				col += 2;
			} else if (src[i] == '%' && src[i + 1] == '=') {
				t.punct = PPercentEq;
				i += 2;
				col += 2;
			} else if (src[i] == '&' && src[i + 1] == '=') {
				t.punct = PAmpEq;
				i += 2;
				col += 2;
			} else if (src[i] == '|' && src[i + 1] == '=') {
				t.punct = PPipeEq;
				i += 2;
				col += 2;
			} else if (src[i] == '^' && src[i + 1] == '=') {
				t.punct = PCaretEq;
				i += 2;
				col += 2;
			} else {
				switch (src[i]) {
				case '+':
					t.punct = PPlus;
					break;
				case '-':
					t.punct = PMinus;
					break;
				case '*':
					t.punct = PStar;
					break;
				case '/':
					t.punct = PSlash;
					break;
				case '%':
					t.punct = PPercent;
					break;
				case '&':
					t.punct = PAmp;
					break;
				case '|':
					t.punct = PPipe;
					break;
				case '^':
					t.punct = PCaret;
					break;
				case '~':
					t.punct = PTilde;
					break;
				case '!':
					t.punct = PBang;
					break;
				case '=':
					t.punct = PEq;
					break;
				case '<':
					t.punct = PLt;
					break;
				case '>':
					t.punct = PGt;
					break;
				case '?':
					t.punct = PQuestion;
					break;
				case ':':
					t.punct = PColon;
					break;
				case ',':
					t.punct = PComma;
					break;
				case ';':
					t.punct = PSemi;
					break;
				case '(':
					t.punct = PLparen;
					break;
				case ')':
					t.punct = PRparen;
					break;
				case '[':
					t.punct = PLbrack;
					break;
				case ']':
					t.punct = PRbrack;
					break;
				case '{':
					t.punct = PLbrace;
					break;
				case '}':
					t.punct = PRbrace;
					break;
				case '.':
					t.punct = PDot;
					break;
				case '#':
					t.punct = PHash;
					break;
				default:
					error_at(c, t.span, "unexpected character '%c'", src[i]);
					i++;
					col++;
					continue;
				}
				i++;
				col++;
			}
			t.span.endcol = col;
			addtok(c, t);
		}
	} /* after_doc */
	memset(&t, 0, sizeof(t));
	t.kind = TEof;
	t.span.file = path;
	t.span.line = line;
	t.span.col = col;
	t.bol = 1;
	addtok(c, t);
}

// Spell a punctuator as source text (pp stringize/paste, format).
const char*
punct_spell(int p) {
	switch (p) {
	case PPlus: return "+";
	case PMinus: return "-";
	case PStar: return "*";
	case PSlash: return "/";
	case PPercent: return "%";
	case PAmp: return "&";
	case PPipe: return "|";
	case PCaret: return "^";
	case PTilde: return "~";
	case PBang: return "!";
	case PEq: return "=";
	case PPlusEq: return "+=";
	case PMinusEq: return "-=";
	case PStarEq: return "*=";
	case PSlashEq: return "/=";
	case PPercentEq: return "%=";
	case PAmpEq: return "&=";
	case PPipeEq: return "|=";
	case PCaretEq: return "^=";
	case PShlEq: return "<<=";
	case PShrEq: return ">>=";
	case PEqEq: return "==";
	case PBangEq: return "!=";
	case PLt: return "<";
	case PGt: return ">";
	case PLe: return "<=";
	case PGe: return ">=";
	case PShl: return "<<";
	case PShr: return ">>";
	case PAmpAmp: return "&&";
	case PPipePipe: return "||";
	case PPlusPlus: return "++";
	case PMinusMinus: return "--";
	case PQuestion: return "?";
	case PColon: return ":";
	case PComma: return ",";
	case PSemi: return ";";
	case PLparen: return "(";
	case PRparen: return ")";
	case PLbrack: return "[";
	case PRbrack: return "]";
	case PLbrace: return "{";
	case PRbrace: return "}";
	case PDot: return ".";
	case PDotDot: return "..";
	case PArrow: return "->";
	case PEllipsis: return "...";
	case PHash: return "#";
	case PHashHash: return "##";
	default: return "?";
	}
}
