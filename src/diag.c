/*
 * Diagnostics and small AST helpers.
 *
 * Compilation pipeline: lex → pp → parse → type check → emit → QBE
 * (cross-cutting: used by every stage; not a pipeline step.)
 *
 * error_at / error_tok print caret spans; after MaxErr errors we stop.
 * node / node1 / node2 / node_add build the tree used by parse and type.
 * xmalloc always zero-fills.
 */
#include "ast.h"

// Allocate n bytes zero-filled; n==0 is bumped to 1 so we never hand back a zero-size block.
void* xmalloc(size_t n) {
	void* p;

	if (n == 0)
		n = 1;
	p = calloc(1, n);
	if (p == NULL)
		die("out of memory");
	return p;
}

// Allocate n bytes uninitialized (faster for large Tok buffers).
void*
xmalloc_raw(size_t n) {
	void* p;

	if (n == 0)
		n = 1;
	p = malloc(n);
	if (p == NULL)
		die("out of memory");
	return p;
}

// Resize an existing block; OOM is fatal because the compiler has no recovery path.
void* xrealloc(void* p, size_t n) {
	void* q;

	q = realloc(p, n);
	if (q == NULL)
		die("out of memory");
	return q;
}

// Heap-copy a C string; NULL in yields NULL out.
char* xstrdup(const char* s) {
	size_t n;
	char* p;

	if (s == NULL)
		return NULL;
	n = strlen(s);
	p = xmalloc(n + 1);
	memcpy(p, s, n + 1);
	return p;
}

// FNV-1a 32-bit hash of a NUL-terminated string (0 if s is NULL).
unsigned
str_hash(const char* s) {
	unsigned h = 2166136261u;

	if (s == NULL)
		return 0;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 16777619u;
	}
	return h;
}

// FNV-1a 32-bit hash of exactly n bytes (for interning slices without a trailing NUL).
static unsigned
str_hash_n(const char* s, size_t n) {
	unsigned h = 2166136261u;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= (unsigned char)s[i];
		h *= 16777619u;
	}
	return h;
}

struct Intern {
	char* s;
	struct Intern* hash_next;
};

// Double the intern hash table when load grows; keeps string identity lookups O(1).
static void
intern_grow(Compiler* c) {
	struct Intern **old, *p, *n;
	int j, oldn, cap;
	unsigned i;

	cap = c->intern_tab_cap ? c->intern_tab_cap * 2 : 1024;
	old = c->intern_tab;
	oldn = c->intern_tab_cap;
	c->intern_tab = xmalloc((size_t)cap * sizeof(struct Intern*));
	c->intern_tab_cap = cap;
	if (old) {
		for (j = 0; j < oldn; j++) {
			for (p = old[j]; p; p = n) {
				n = p->hash_next;
				i = str_hash(p->s) & (unsigned)(cap - 1);
				p->hash_next = c->intern_tab[i];
				c->intern_tab[i] = p;
			}
		}
		free(old);
	}
}

// Intern a length-bounded slice (does not require s[n] == 0).
char*
str_intern_n(Compiler* c, const char* s, size_t n) {
	struct Intern* e;
	unsigned i, h;

	if (s == NULL)
		return NULL;
	if (c->intern_tab_cap == 0 || c->interns_len * 2 >= c->intern_tab_cap)
		intern_grow(c);
	h = str_hash_n(s, n);
	i = h & (unsigned)(c->intern_tab_cap - 1);
	for (e = c->intern_tab[i]; e; e = e->hash_next)
		if (strlen(e->s) == n && memcmp(e->s, s, n) == 0)
			return e->s;
	e = xmalloc(sizeof(*e));
	e->s = xmalloc(n + 1);
	memcpy(e->s, s, n);
	e->s[n] = 0;
	e->hash_next = c->intern_tab[i];
	c->intern_tab[i] = e;
	c->interns_len++;
	return e->s;
}

// Intern a string for the Compiler lifetime; equal spellings share one pointer.
char*
str_intern(Compiler* c, const char* s) {
	if (s == NULL)
		return NULL;
	return str_intern_n(c, s, strlen(s));
}

// Copy at most n bytes from s and NUL-terminate; does not require s to be longer than n.
char* xstrndup(const char* s, size_t n) {
	char* p;

	p = xmalloc(n + 1);
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

// Internal fatal error (OOM, impossible state): print and exit; not counted in Compiler.error_count.
void die(const char* fmt, ...) {
	va_list ap;

	fprintf(stderr, "modc: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

// User-facing error at a source span: message, caret underline, and fatal after MaxErr.
void error_at(Compiler* c, Span sp, const char* fmt, ...) {
	va_list ap;
	int i, col, len;
	const char *text, *p, *line;
	int lineno;

	c->error_count++;
	if (c->quiet_pp)
		return;
	if (sp.file)
		fprintf(stderr, "%s:%d:%d: error: ", sp.file, sp.line, sp.col > 0 ? sp.col : 1);
	else
		fprintf(stderr, "error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	text = NULL;
	for (i = 0; i < c->src_files_len; i++) {
		if (c->src_files[i] && sp.file && strcmp(c->src_files[i], sp.file) == 0) {
			text = c->src_text[i];
			break;
		}
	}
	if (text && sp.line > 0) {
		p = text;
		lineno = 1;
		while (lineno < sp.line && *p) {
			if (*p == '\n')
				lineno++;
			p++;
		}
		line = p;
		while (*p && *p != '\n')
			p++;
		len = (int)(p - line);
		fprintf(stderr, "  %.*s\n  ", len, line);
		col = sp.col > 0 ? sp.col : 1;
		for (i = 1; i < col && i <= len; i++)
			fputc(line[i - 1] == '\t' ? '\t' : ' ', stderr);
		fputc('^', stderr);
		fputc('\n', stderr);
	}
	if (c->error_count >= MaxErr)
		c->fatal = 1;
}

// error_at with the span taken from a token (or a dummy location when t is NULL).
void error_tok(Compiler* c, Tok* t, const char* fmt, ...) {
	va_list ap;
	char buf[256];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (t)
		error_at(c, t->span, "%s", buf);
	else
		error_at(c, (Span){c->infile, 1, 1, 1}, "%s", buf);
}

// Allocate a fresh AST node with kind and source span; children are added via node_add.
Node* node(int kind, Span sp) {
	Node* n;

	n = xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->kind = kind;
	n->span = sp;
	return n;
}

// node with one child pointer stored in ->a.
Node* node1(int kind, Span sp, Node* a) {
	Node* n;

	n = node(kind, sp);
	n->a = a;
	return n;
}

// node with two children in ->a and ->b.
Node* node2(int kind, Span sp, Node* a, Node* b) {
	Node* n;

	n = node(kind, sp);
	n->a = a;
	n->b = b;
	return n;
}

// Append a child to n->children; grows the children array in steps of eight.
void node_add(Node* n, Node* k) {
	if (k == NULL)
		return;
	if (n->children_len >= 0 && n->children_len % 8 == 0)
		n->children = xrealloc(n->children, (n->children_len + 8) * sizeof(Node*));
	n->children[n->children_len++] = k;
}
