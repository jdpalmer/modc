/*
 * Preprocessor: directives, macros, #include.
 *
 * Compilation pipeline: lex → [pp] → parse → type check → emit → QBE
 *
 * Consumes the lexer Tok stream and rewrites c->tokens in place. Conditional
 * stack is IfOn / IfWait / IfDone. Macro expansion follows Prosser-ish
 * argument expand / # / ## rules. After pp_run, parse sees a flat token list
 * with no directives left.
 */
#include "ast.h"
#include "host_os.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX HOST_PATH_MAX
#endif

enum {
	IfOn = 0, /* taking tokens */
	IfWait,   /* skipping until else/elif/endif; sibling not yet taken */
	IfDone,   /* already took a branch; skip rest */
};

static void emit_tok(Compiler* c, Tok t);
static void process(Compiler* c, Tok* src, int src_files_len, int* skipstack, int* nsp);
static void expand_into(Compiler* c, Tok* src, int src_files_len, int i, int* ni, int hide_self);
static void expand_list_into(Compiler* c, Tok* src, int src_files_len, Tok** out, int* out_len);
static int if_eval(Compiler* c, Tok* src, int src_files_len, int* i);

/* Invocation-site span for __FILE__/__LINE__ inside nested macro expansion. */
static Span pp_dyn_span;
static int pp_expand_depth;
static int pp_prof;
static int pp_ninc_open, pp_ninc_once, pp_ninc_guard, pp_ninc_miss;
static double pp_t_find, pp_t_read, pp_t_lex, pp_t_proc;
static double pp_t_emit, pp_t_expand, pp_t_dir;
static uint64_t pp_n_emit, pp_n_expand, pp_n_dir;

/* Bump arena for macro-expansion Tok[] scratch (reset after each top-level expand). */
typedef struct PpChunk {
	struct PpChunk* next;
	size_t used;
	size_t cap;
} PpChunk;

static PpChunk* pp_arena_head;
static PpChunk* pp_arena_cur;

// Monotonic-ish wall time in seconds for -profile timings.
static double
pp_now(void) {
#ifdef _WIN32
	static double inv;
	LARGE_INTEGER t;

	if (inv == 0) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		inv = 1.0 / (double)f.QuadPart;
	}
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * inv;
#else
	return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

// Keep the first bump chunk; free extras after a top-level expand.
static void
pp_arena_reset(void) {
	PpChunk *ch, *n;

	if (pp_arena_head == NULL)
		return;
	for (ch = pp_arena_head->next; ch; ch = n) {
		n = ch->next;
		free(ch);
	}
	pp_arena_head->next = NULL;
	pp_arena_head->used = 0;
	pp_arena_cur = pp_arena_head;
}

// Free the entire macro-expansion bump arena.
static void
pp_arena_clear(void) {
	PpChunk *ch, *n;

	for (ch = pp_arena_head; ch; ch = n) {
		n = ch->next;
		free(ch);
	}
	pp_arena_head = NULL;
	pp_arena_cur = NULL;
}

// Allocate n bytes from the pp bump arena (grows in large chunks).
static void*
pp_bump(size_t n) {
	size_t need;
	PpChunk* ch;
	char* p;

	if (n == 0)
		n = 1;
	n = (n + 7u) & ~(size_t)7;
	if (pp_arena_cur == NULL || pp_arena_cur->used + n > pp_arena_cur->cap) {
		need = n + 65536;
		if (need < 256 * 1024)
			need = 256 * 1024;
		ch = malloc(sizeof(PpChunk) + need);
		if (ch == NULL)
			die("out of memory");
		ch->next = NULL;
		ch->used = 0;
		ch->cap = need;
		if (pp_arena_cur)
			pp_arena_cur->next = ch;
		else
			pp_arena_head = ch;
		pp_arena_cur = ch;
	}
	p = (char*)(pp_arena_cur + 1) + pp_arena_cur->used;
	pp_arena_cur->used += n;
	return p;
}

// Reallocate bump storage by copying into a larger arena block.
static void*
pp_bump_grow(void* p, size_t old_bytes, size_t new_bytes) {
	void* q;

	q = pp_bump(new_bytes);
	if (p && old_bytes)
		memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
	return q;
}

// True for __FILE__ and __LINE__, which expand at the invocation site.
static int
is_file_or_line(const char* name) {
	return name != NULL && (strcmp(name, "__FILE__") == 0 || strcmp(name, "__LINE__") == 0);
}

// Build a string literal token for expanded __FILE__ (with escapes).
static Tok
make_file_tok(Span sp) {
	Tok t;
	const char* path;
	char* esc;
	int i, j, n;

	memset(&t, 0, sizeof(t));
	t.kind = TkString;
	t.span = sp;
	path = sp.file ? sp.file : "";
	n = 0;
	for (i = 0; path[i]; i++) {
		if (path[i] == '\\' || path[i] == '"')
			n++;
		n++;
	}
	esc = xmalloc(n + 1);
	j = 0;
	for (i = 0; path[i]; i++) {
		if (path[i] == '\\' || path[i] == '"')
			esc[j++] = '\\';
		esc[j++] = path[i];
	}
	esc[j] = 0;
	t.s = esc;
	return t;
}

// Build a number token for expanded __LINE__ from a span.
static Tok
make_line_tok(Span sp) {
	Tok t;
	char buf[32];

	memset(&t, 0, sizeof(t));
	t.kind = TkNumber;
	t.span = sp;
	t.int_val = sp.line;
	snprintf(buf, sizeof(buf), "%d", sp.line);
	t.s = xstrdup(buf);
	return t;
}

// Lazy-init the macro-name bloom filter bitset.
static void
macro_bits_ensure(Compiler* c) {
	if (c->macro_bits_cap != 0)
		return;
	/* 2^20 bits ≈ 128KiB; keeps false-positive rate low with ~20k macros. */
	c->macro_bits_cap = 32768;
	c->macro_bits = xmalloc((size_t)c->macro_bits_cap * sizeof(uint32_t));
}

// Mark hash h present in the macro bloom filter.
static void
macro_bit_set(Compiler* c, unsigned h) {
	unsigned nbits;

	macro_bits_ensure(c);
	nbits = (unsigned)c->macro_bits_cap * 32u;
	h &= nbits - 1u;
	c->macro_bits[h >> 5] |= 1u << (h & 31u);
}

// Record that some macro name starts with name[0] (fast reject).
static void
macro_start_set(Compiler* c, const char* name) {
	unsigned char ch;

	if (name == NULL || name[0] == 0)
		return;
	ch = (unsigned char)name[0];
	c->macro_start[ch >> 5] |= 1u << (ch & 31u);
}

// True if the bloom filter says a macro with hash h might exist.
static int
macro_bit_may(Compiler* c, unsigned h) {
	unsigned nbits;

	if (c->macro_bits_cap == 0)
		return 0;
	nbits = (unsigned)c->macro_bits_cap * 32u;
	h &= nbits - 1u;
	return (c->macro_bits[h >> 5] & (1u << (h & 31u))) != 0;
}

// Rebuild the macro hash from c->macros (capacity must be power of two).
static void
macro_tab_rebuild(Compiler* c, int cap) {
	Macro *m;
	unsigned i, h;

	free(c->macro_tab);
	c->macro_tab = xmalloc((size_t)cap * sizeof(Macro*));
	c->macro_tab_cap = cap;
	macro_bits_ensure(c);
	memset(c->macro_bits, 0, (size_t)c->macro_bits_cap * sizeof(uint32_t));
	memset(c->macro_start, 0, sizeof(c->macro_start));
	for (m = c->macros; m; m = m->next) {
		h = str_hash(m->name);
		macro_bit_set(c, h);
		macro_start_set(c, m->name);
		i = h & (unsigned)(cap - 1);
		m->hash_next = c->macro_tab[i];
		c->macro_tab[i] = m;
	}
}

// Insert m into the macro hash (m already linked on c->macros).
static void
macro_tab_add(Compiler* c, Macro* m) {
	unsigned i, h;

	c->macros_len++;
	h = str_hash(m->name);
	macro_bit_set(c, h);
	macro_start_set(c, m->name);
	if (c->macro_tab_cap == 0 || c->macros_len * 2 >= c->macro_tab_cap) {
		macro_tab_rebuild(c, c->macro_tab_cap ? c->macro_tab_cap * 2 : 256);
		return;
	}
	i = h & (unsigned)(c->macro_tab_cap - 1);
	m->hash_next = c->macro_tab[i];
	c->macro_tab[i] = m;
}

// Remove name from the macro hash only (list unlink is separate).
static void
macro_tab_del(Compiler* c, const char* name) {
	Macro *m, **pp;
	unsigned i;

	if (c->macro_tab_cap == 0 || name == NULL)
		return;
	name = str_intern(c, name);
	i = str_hash(name) & (unsigned)(c->macro_tab_cap - 1);
	pp = &c->macro_tab[i];
	while ((m = *pp) != NULL) {
		if (m->name == name) {
			*pp = m->hash_next;
			c->macros_len--;
			return;
		}
		pp = &m->hash_next;
	}
}

// Remove a macro definition from the list and hash.
static void
undef_macro(Compiler* c, const char* name) {
	Macro *m, **pp;

	if (name == NULL)
		return;
	name = str_intern(c, name);
	macro_tab_del(c, name);
	pp = &c->macros;
	while ((m = *pp) != NULL) {
		if (m->name == name) {
			*pp = m->next;
			return;
		}
		pp = &m->next;
	}
}

// Lookup a macro by name. name must be interned for pointer equality.
static Macro*
find_macro(Compiler* c, const char* name) {
	Macro* m;
	unsigned i, h;

	if (name == NULL || c->macro_tab_cap == 0)
		return NULL;
	/* Cheap reject: no macro's name starts with this character. */
	{
		unsigned char ch = (unsigned char)name[0];
		if ((c->macro_start[ch >> 5] & (1u << (ch & 31u))) == 0)
			return NULL;
	}
	h = str_hash(name);
	if (!macro_bit_may(c, h))
		return NULL;
	i = h & (unsigned)(c->macro_tab_cap - 1);
	for (m = c->macro_tab[i]; m; m = m->hash_next)
		if (m->name == name)
			return m;
	return NULL;
}

// Drop the macro list pointer and hash table (does not free Macro bodies).
void
pp_clear_macros(Compiler* c) {
	free(c->macro_tab);
	c->macro_tab = NULL;
	c->macro_tab_cap = 0;
	c->macros_len = 0;
	c->macros = NULL;
	if (c->macro_bits)
		memset(c->macro_bits, 0, (size_t)c->macro_bits_cap * sizeof(uint32_t));
	memset(c->macro_start, 0, sizeof(c->macro_start));
}

// True when a name is currently #defined (used by #if defined()).
int pp_defined(Compiler* c, const char* name) {
	if (name == NULL)
		return 0;
	return find_macro(c, str_intern(c, name)) != NULL;
}

// Append one token to the preprocessor output stream.
static void
emit_tok(Compiler* c, Tok t) {
	if (c->tokens_len >= c->tokens_cap) {
		c->tokens_cap = c->tokens_cap ? c->tokens_cap * 2 : 256;
		c->tokens = xrealloc(c->tokens, c->tokens_cap * sizeof(Tok));
	}
	c->tokens[c->tokens_len++] = t;
}

// True when t is a '#' punctuator (start of a directive).
static int
tok_is_hash(Tok* t) {
	return t && t->kind == TkPunct && t->punct == PnHash;
}

// True when t is an identifier spelling exactly s.
static int
ident_is(Tok* t, const char* s) {
	return t && t->kind == TkIdent && t->s && strcmp(t->s, s) == 0;
}

// Advance past the rest of a preprocessor line (through newline).
static void
skip_nl(Tok* src, int src_files_len, int* i) {
	while (*i < src_files_len && src[*i].kind != TkNewline && src[*i].kind != TkEof)
		(*i)++;
	if (*i < src_files_len && src[*i].kind == TkNewline)
		(*i)++;
}

// Join a directory and relative path (absolute b is returned as-is).
static char*
join_path(const char* a, const char* b) {
	return host_join_path(a, b);
}

// Copy the parent directory of a file path into out.
static void
dir_of(const char* path, char* out, size_t n) {
	host_dirname(path, out, n);
}

// True when a path is a readable regular file (no fopen).
static int
file_exists(const char* path) {
	return host_is_file(path);
}

/* ---- once / include / guard caches ---- */

struct PpOnce {
	char* path;
	struct PpOnce* hash_next;
};

struct PpInc {
	char* key;  /* "1:name" angled, or "0:dir/name" quoted */
	char* path; /* resolved path; NULL = miss cached */
	struct PpInc* hash_next;
};

struct PpGuard {
	char* path;  /* resolved include path */
	char* macro; /* #ifndef / #define guard name */
	struct PpGuard* hash_next;
};

// Insert path into the #pragma once hash table.
static void
once_tab_add(Compiler* c, const char* path) {
	struct PpOnce* e;
	unsigned i;
	int cap;

	if (path == NULL)
		return;
	if (c->once_tab_cap == 0 || c->once_files_len * 2 >= c->once_tab_cap) {
		struct PpOnce **old, *p, *n;
		int j, oldn;

		cap = c->once_tab_cap ? c->once_tab_cap * 2 : 256;
		old = c->once_tab;
		oldn = c->once_tab_cap;
		c->once_tab = xmalloc((size_t)cap * sizeof(struct PpOnce*));
		c->once_tab_cap = cap;
		if (old) {
			for (j = 0; j < oldn; j++) {
				for (p = old[j]; p; p = n) {
					n = p->hash_next;
					i = str_hash(p->path) & (unsigned)(cap - 1);
					p->hash_next = c->once_tab[i];
					c->once_tab[i] = p;
				}
			}
			free(old);
		}
	}
	i = str_hash(path) & (unsigned)(c->once_tab_cap - 1);
	e = xmalloc(sizeof(*e));
	e->path = (char*)path; /* aliases once_files[] entry */
	e->hash_next = c->once_tab[i];
	c->once_tab[i] = e;
}

// True when a header was already included via #pragma once.
static int
already_once(Compiler* c, const char* resolved) {
	struct PpOnce* e;
	unsigned i;

	if (resolved == NULL || c->once_tab_cap == 0)
		return 0;
	i = str_hash(resolved) & (unsigned)(c->once_tab_cap - 1);
	for (e = c->once_tab[i]; e; e = e->hash_next)
		if (strcmp(e->path, resolved) == 0)
			return 1;
	return 0;
}

// Record a canonical path as included under #pragma once.
static void
mark_once(Compiler* c, const char* resolved) {
	char* p;

	if (resolved == NULL || already_once(c, resolved))
		return;
	if (c->once_files_len % 8 == 0)
		c->once_files = xrealloc(c->once_files, (c->once_files_len + 8) * sizeof(char*));
	p = xstrdup(resolved);
	c->once_files[c->once_files_len++] = p;
	once_tab_add(c, p);
}

// Double the #include path-cache hash table.
static void
include_tab_grow(Compiler* c) {
	struct PpInc **old, *p, *n;
	int j, oldn, cap;
	unsigned i;

	cap = c->include_tab_cap ? c->include_tab_cap * 2 : 512;
	old = c->include_tab;
	oldn = c->include_tab_cap;
	c->include_tab = xmalloc((size_t)cap * sizeof(struct PpInc*));
	c->include_tab_cap = cap;
	if (old) {
		for (j = 0; j < oldn; j++) {
			for (p = old[j]; p; p = n) {
				n = p->hash_next;
				i = str_hash(p->key) & (unsigned)(cap - 1);
				p->hash_next = c->include_tab[i];
				c->include_tab[i] = p;
			}
		}
		free(old);
	}
}

// Look up a cached include key → resolved path (or miss).
static struct PpInc*
inc_lookup(Compiler* c, const char* key) {
	struct PpInc* e;
	unsigned i;

	if (c->include_tab_cap == 0 || key == NULL)
		return NULL;
	i = str_hash(key) & (unsigned)(c->include_tab_cap - 1);
	for (e = c->include_tab[i]; e; e = e->hash_next)
		if (strcmp(e->key, key) == 0)
			return e;
	return NULL;
}

// Cache an include search key and its resolved path (NULL = miss).
static void
inc_insert(Compiler* c, const char* key, const char* path) {
	struct PpInc* e;
	unsigned i;

	if (c->include_tab_cap == 0)
		include_tab_grow(c);
	e = xmalloc(sizeof(*e));
	e->key = xstrdup(key);
	e->path = path ? xstrdup(path) : NULL;
	i = str_hash(key) & (unsigned)(c->include_tab_cap - 1);
	e->hash_next = c->include_tab[i];
	c->include_tab[i] = e;
	/* grow occasionally */
	{
		int bucket = 0;
		for (e = c->include_tab[i]; e; e = e->hash_next)
			bucket++;
		if (bucket > 8 && c->include_tab_cap < 65536)
			include_tab_grow(c);
	}
}

// Double the include-guard (path → macro) hash table.
static void
guard_tab_grow(Compiler* c) {
	struct PpGuard **old, *p, *n;
	int j, oldn, cap;
	unsigned i;

	cap = c->guard_tab_cap ? c->guard_tab_cap * 2 : 512;
	old = c->guard_tab;
	oldn = c->guard_tab_cap;
	c->guard_tab = xmalloc((size_t)cap * sizeof(struct PpGuard*));
	c->guard_tab_cap = cap;
	if (old) {
		for (j = 0; j < oldn; j++) {
			for (p = old[j]; p; p = n) {
				n = p->hash_next;
				i = str_hash(p->path) & (unsigned)(cap - 1);
				p->hash_next = c->guard_tab[i];
				c->guard_tab[i] = p;
			}
		}
		free(old);
	}
}

// Return the #ifndef guard macro for path, or NULL.
static const char*
guard_lookup(Compiler* c, const char* path) {
	struct PpGuard* e;
	unsigned i;

	if (path == NULL || c->guard_tab_cap == 0)
		return NULL;
	i = str_hash(path) & (unsigned)(c->guard_tab_cap - 1);
	for (e = c->guard_tab[i]; e; e = e->hash_next)
		if (strcmp(e->path, path) == 0)
			return e->macro;
	return NULL;
}

// Record that path is wrapped by an include-guard macro.
static void
guard_insert(Compiler* c, const char* path, const char* macro) {
	struct PpGuard* e;
	unsigned i;

	if (path == NULL || macro == NULL || guard_lookup(c, path))
		return;
	if (c->guard_tab_cap == 0)
		guard_tab_grow(c);
	e = xmalloc(sizeof(*e));
	e->path = xstrdup(path);
	e->macro = xstrdup(macro);
	i = str_hash(path) & (unsigned)(c->guard_tab_cap - 1);
	e->hash_next = c->guard_tab[i];
	c->guard_tab[i] = e;
	{
		int bucket = 0;
		for (e = c->guard_tab[i]; e; e = e->hash_next)
			bucket++;
		if (bucket > 8 && c->guard_tab_cap < 65536)
			guard_tab_grow(c);
	}
}

// Canonicalize a path for #pragma once deduplication.
static char*
resolve_once(const char* path) {
	char buf[PATH_MAX];

	if (host_realpath(path, buf, sizeof(buf)) == 0)
		return xstrdup(buf);
	return xstrdup(path);
}

// Detect #ifndef GUARD / #define GUARD at the start of a lexed header.
static const char*
detect_include_guard(Tok* tokens, int tokens_len) {
	int i;
	const char* name;

	i = 0;
	while (i < tokens_len && tokens[i].kind == TkNewline)
		i++;
	/* Skip leading #pragma once / #pragma warning lines. */
	while (i + 1 < tokens_len && tokens[i].kind == TkPunct && tokens[i].punct == PnHash && tokens[i].bol &&
	       tokens[i + 1].kind == TkIdent && strcmp(tokens[i + 1].s, "pragma") == 0) {
		i += 2;
		while (i < tokens_len && tokens[i].kind != TkNewline && tokens[i].kind != TkEof)
			i++;
		while (i < tokens_len && tokens[i].kind == TkNewline)
			i++;
	}
	if (i + 2 >= tokens_len || !(tokens[i].kind == TkPunct && tokens[i].punct == PnHash && tokens[i].bol))
		return NULL;
	i++;
	name = NULL;
	if (tokens[i].kind == TkIdent && strcmp(tokens[i].s, "ifndef") == 0) {
		i++;
		if (i < tokens_len && tokens[i].kind == TkIdent)
			name = tokens[i].s;
	} else if (tokens[i].kind == TkIdent && strcmp(tokens[i].s, "if") == 0) {
		i++;
		if (i < tokens_len && tokens[i].kind == TkPunct && tokens[i].punct == PnBang)
			i++;
		else
			return NULL;
		if (i < tokens_len && tokens[i].kind == TkIdent && strcmp(tokens[i].s, "defined") == 0)
			i++;
		else
			return NULL;
		if (i < tokens_len && tokens[i].kind == TkPunct && tokens[i].punct == PnLparen)
			i++;
		if (i < tokens_len && tokens[i].kind == TkIdent)
			name = tokens[i].s;
	}
	if (name == NULL)
		return NULL;
	/* Next directive should be #define NAME */
	i++;
	while (i < tokens_len && tokens[i].kind == TkNewline)
		i++;
	if (i + 2 >= tokens_len || !(tokens[i].kind == TkPunct && tokens[i].punct == PnHash && tokens[i].bol))
		return NULL;
	if (!(tokens[i + 1].kind == TkIdent && strcmp(tokens[i + 1].s, "define") == 0))
		return NULL;
	if (!(tokens[i + 2].kind == TkIdent && strcmp(tokens[i + 2].s, name) == 0))
		return NULL;
	return name;
}

// Search include paths for "file" or <file> relative to the including TU.
static char*
find_include_raw(Compiler* c, const char* fromfile, const char* name, int angled) {
	char dir[PATH_MAX], *p;
	int i;

	if (!angled) {
		dir_of(fromfile, dir, sizeof(dir));
		p = join_path(dir, name);
		if (file_exists(p))
			return p;
		free(p);
		p = join_path(".", name);
		if (file_exists(p))
			return p;
		free(p);
	}
	if (c->modc_include) {
		p = join_path(c->modc_include, name);
		if (file_exists(p))
			return p;
		free(p);
	}
	for (i = 0; i < c->incpaths_len; i++) {
		p = join_path(c->incpaths[i], name);
		if (file_exists(p))
			return p;
		free(p);
	}
	for (i = 0; i < c->sysincpaths_len; i++) {
		p = join_path(c->sysincpaths[i], name);
		if (file_exists(p))
			return p;
		free(p);
	}
	/* <Framework/Header.h> → Frameworks/Framework.framework/Headers/Header.h */
	if (angled) {
		const char* slash;
		char fw[256], hdr[512], cand[PATH_MAX];
		size_t nfw;

		slash = strchr(name, '/');
		if (slash && slash != name && slash[1]) {
			nfw = (size_t)(slash - name);
			if (nfw < sizeof(fw)) {
				memcpy(fw, name, nfw);
				fw[nfw] = 0;
				snprintf(hdr, sizeof(hdr), "%s", slash + 1);
				for (i = 0; i < c->framework_paths_len; i++) {
					snprintf(cand, sizeof(cand),
						 "%s/%s.framework/Headers/%s",
						 c->framework_paths[i], fw, hdr);
					if (file_exists(cand))
						return xstrdup(cand);
				}
			}
		}
	}
	return NULL;
}

// Cached include lookup; returns owned resolved path or NULL.
static char*
find_include(Compiler* c, const char* fromfile, const char* name, int angled) {
	char key[PATH_MAX + 64], dir[PATH_MAX], *raw, *resolved;
	struct PpInc* e;

	if (name == NULL)
		return NULL;
	if (angled)
		snprintf(key, sizeof(key), "1:%s", name);
	else {
		dir_of(fromfile ? fromfile : "", dir, sizeof(dir));
		snprintf(key, sizeof(key), "0:%s/%s", dir, name);
	}
	e = inc_lookup(c, key);
	if (e)
		return e->path ? xstrdup(e->path) : NULL;
	raw = find_include_raw(c, fromfile, name, angled);
	if (raw == NULL) {
		inc_insert(c, key, NULL);
		return NULL;
	}
	resolved = resolve_once(raw);
	free(raw);
	inc_insert(c, key, resolved);
	return resolved; /* caller owns; cache has its own copy */
}

// Parse the header operand of #include ("…", <…>, or tokens).
static char*
header_name(Tok* src, int src_files_len, int* i, int* angled) {
	Tok* t;
	char buf[512];
	int n;

	*angled = 0;
	if (*i >= src_files_len)
		return NULL;
	t = &src[*i];
	if (t->kind == TkString) {
		(*i)++;
		return xstrdup(t->s);
	}
	if (t->kind == TkPunct && t->punct == PnLt) {
		*angled = 1;
		(*i)++;
		n = 0;
		buf[0] = 0;
		while (*i < src_files_len && !(src[*i].kind == TkPunct && src[*i].punct == PnGt) && src[*i].kind != TkNewline && src[*i].kind != TkEof) {
			t = &src[*i];
			if (t->kind == TkIdent || t->kind == TkNumber || t->kind == TkString) {
				n += snprintf(buf + n, sizeof(buf) - n, "%s", t->s ? t->s : "");
			} else if (t->kind == TkPunct) {
				switch (t->punct) {
				case PnDot:
					buf[n++] = '.';
					buf[n] = 0;
					break;
				case PnSlash:
					buf[n++] = '/';
					buf[n] = 0;
					break;
				case PnMinus:
					buf[n++] = '-';
					buf[n] = 0;
					break;
				default:
					break;
				}
			}
			(*i)++;
			if (n >= (int)sizeof(buf) - 1)
				break;
		}
		if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnGt)
			(*i)++;
		return xstrdup(buf);
	}
	return NULL;
}

// Lex, preprocess, and splice one #include file into the output stream.
static void
do_include(Compiler* c, Tok* at, const char* name, int angled, int skipping) {
	char *path, *text;
	const char* guard;
	Tok* itoks;
	int tokens_len, cap, nsave, capsave;
	Tok* saved;
	double t0, t1;

	if (skipping)
		return;
	if (pp_prof)
		t0 = pp_now();
	path = find_include(c, at->span.file, name, angled);
	if (pp_prof) {
		t1 = pp_now();
		pp_t_find += t1 - t0;
	}
	if (path == NULL) {
		pp_ninc_miss++;
		error_tok(c, at, "cannot find include file %s", name);
		return;
	}
	if (already_once(c, path)) {
		pp_ninc_once++;
		free(path);
		return;
	}
	guard = guard_lookup(c, path);
	if (guard && pp_defined(c, guard)) {
		pp_ninc_guard++;
		free(path);
		return;
	}
	if (pp_prof)
		t0 = pp_now();
	text = read_file(path, NULL);
	if (pp_prof) {
		t1 = pp_now();
		pp_t_read += t1 - t0;
	}
	if (text == NULL) {
		error_tok(c, at, "cannot read include file %s", path);
		free(path);
		return;
	}
	saved = c->tokens;
	nsave = c->tokens_len;
	capsave = c->tokens_cap;
	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = 0;
	if (pp_prof)
		t0 = pp_now();
	lex_file(c, path, text, 1);
	if (pp_prof) {
		t1 = pp_now();
		pp_t_lex += t1 - t0;
	}
	itoks = c->tokens;
	tokens_len = c->tokens_len;
	cap = c->tokens_cap;
	c->tokens = saved;
	c->tokens_len = nsave;
	c->tokens_cap = capsave;
	/* drop trailing TkEof */
	if (tokens_len > 0 && itoks[tokens_len - 1].kind == TkEof)
		tokens_len--;
	guard = detect_include_guard(itoks, tokens_len);
	{
		int skipstack[64], nsp = 0;
		if (pp_prof)
			t0 = pp_now();
		process(c, itoks, tokens_len, skipstack, &nsp);
		if (pp_prof) {
			t1 = pp_now();
			pp_t_proc += t1 - t0;
		}
		(void)cap;
	}
	pp_ninc_open++;
	if (guard && !already_once(c, path))
		guard_insert(c, path, guard);
	free(text);
	free(path);
}

// Parse and register a #define (object-like or function-like macro).
static void
do_define(Compiler* c, Tok* src, int src_files_len, int* i, int skipping) {
	Macro* m;
	Tok* name;
	int n, cap, func, varargs;
	char** params;
	int params_len, j;

	if (*i >= src_files_len || (src[*i].kind != TkIdent && src[*i].kind != TkKw)) {
		if (!skipping)
			error_tok(c, *i < src_files_len ? &src[*i] : NULL, "expected macro name");
		skip_nl(src, src_files_len, i);
		return;
	}
	name = &src[*i];
	(*i)++;
	func = 0;
	varargs = 0;
	params = NULL;
	params_len = 0;
	if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnLparen && !src[*i].ws) {
		func = 1;
		(*i)++;
		if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnRparen)
			(*i)++;
		else {
			for (;;) {
				if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnEllipsis) {
					varargs = 1;
					(*i)++;
					if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnRparen)
						(*i)++;
					break;
				}
				if (*i >= src_files_len || src[*i].kind != TkIdent) {
					if (!skipping)
						error_tok(c, *i < src_files_len ? &src[*i] : name, "bad macro parameter");
					break;
				}
				if (params_len % 4 == 0)
					params = xrealloc(params, (params_len + 4) * sizeof(char*));
				params[params_len++] = xstrdup(src[*i].s);
				(*i)++;
				if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnComma) {
					(*i)++;
					continue;
				}
				if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnRparen) {
					(*i)++;
					break;
				}
				break;
			}
		}
	}
	m = xmalloc(sizeof(*m));
	m->name = str_intern(c, name->s);
	m->func = func;
	m->params = params;
	m->params_len = params_len;
	m->varargs = varargs;
	n = 0;
	cap = 0;
	m->body = NULL;
	while (*i < src_files_len && src[*i].kind != TkNewline && src[*i].kind != TkEof) {
		if (n >= cap) {
			cap = cap ? cap * 2 : 8;
			m->body = xrealloc(m->body, cap * sizeof(Tok));
		}
		m->body[n++] = src[*i];
		(*i)++;
	}
	m->body_len = n;
	if (skipping)
		return;
	/* Constraints on # / ## in the replacement list (ISO). */
	for (j = 0; j < m->body_len; j++) {
		if (m->body[j].kind == TkPunct && m->body[j].punct == PnHashHash) {
			if (j == 0 || j + 1 >= m->body_len)
				error_tok(c, &m->body[j], "'##' cannot appear at either end of a macro body");
		}
		if (m->func && m->body[j].kind == TkPunct && m->body[j].punct == PnHash) {
			int ok = 0, k;
			if (j + 1 < m->body_len && m->body[j + 1].kind == TkIdent && m->body[j + 1].s) {
				if (m->varargs && strcmp(m->body[j + 1].s, "__VA_ARGS__") == 0)
					ok = 1;
				for (k = 0; !ok && k < m->params_len; k++)
					if (strcmp(m->body[j + 1].s, m->params[k]) == 0)
						ok = 1;
			}
			if (!ok)
				error_tok(c, &m->body[j], "'#' is not followed by a macro parameter");
		}
	}
	undef_macro(c, m->name);
	m->next = c->macros;
	c->macros = m;
	macro_tab_add(c, m);
}

/* ---- #if token expression ---- */

typedef struct {
	Compiler* c;
	Tok* t;
	int n, p;
	int err;
} Ifp;

// Peek the next non-newline token in a #if expression parse.
static Tok*
ip_peek(Ifp* p) {
	while (p->p < p->n && p->t[p->p].kind == TkNewline)
		p->p++;
	if (p->p >= p->n) {
		static Tok eof;
		eof.kind = TkEof;
		return &eof;
	}
	return &p->t[p->p];
}

// Consume one token from a #if expression parse.
static Tok*
ip_take(Ifp* p) {
	Tok* t;

	t = ip_peek(p);
	if (t->kind != TkEof)
		p->p++;
	return t;
}

static int64_t iexpr(Ifp* p);
static int64_t ior(Ifp* p);
static int64_t iandand(Ifp* p);
static int64_t ibit(Ifp* p);
static int64_t icmp(Ifp* p);
static int64_t ishift(Ifp* p);
static int64_t iadd(Ifp* p);
static int64_t imul(Ifp* p);
static int64_t iunary(Ifp* p);
static int64_t iprimary(Ifp* p);

// Consume an expected punctuator in #if parsing.
static int
ip_eatp(Ifp* p, int punct) {
	if (ip_peek(p)->kind == TkPunct && ip_peek(p)->punct == punct) {
		ip_take(p);
		return 1;
	}
	return 0;
}

// Top-level #if constant expression entry.
static int64_t
iexpr(Ifp* p) {
	return ior(p);
}

// Parse || in #if expressions (short-circuit not modeled; both sides evaluated).
static int64_t
ior(Ifp* p) {
	int64_t a, b;

	a = iandand(p);
	while (ip_eatp(p, PnPipePipe)) {
		b = iandand(p); /* always parse */
		a = a || b;
	}
	return a;
}

// Parse && in #if expressions.
static int64_t
iandand(Ifp* p) {
	int64_t a, b;

	a = ibit(p);
	while (ip_eatp(p, PnAmpAmp)) {
		b = ibit(p);
		a = a && b;
	}
	return a;
}

// Parse bitwise &, |, ^ in #if expressions.
static int64_t
ibit(Ifp* p) {
	int64_t a, b;
	int op;

	a = icmp(p);
	for (;;) {
		op = ip_peek(p)->kind == TkPunct ? ip_peek(p)->punct : -1;
		if (op != PnAmp && op != PnPipe && op != PnCaret)
			break;
		ip_take(p);
		b = icmp(p);
		if (op == PnAmp)
			a &= b;
		else if (op == PnPipe)
			a |= b;
		else
			a ^= b;
	}
	return a;
}

// Parse relational and equality operators in #if expressions.
static int64_t
icmp(Ifp* p) {
	int64_t a, b;
	int op;

	a = ishift(p);
	for (;;) {
		op = ip_peek(p)->kind == TkPunct ? ip_peek(p)->punct : -1;
		if (op != PnEqEq && op != PnBangEq && op != PnLt && op != PnGt && op != PnLe && op != PnGe)
			break;
		ip_take(p);
		b = ishift(p);
		switch (op) {
		case PnEqEq:
			a = a == b;
			break;
		case PnBangEq:
			a = a != b;
			break;
		case PnLt:
			a = a < b;
			break;
		case PnGt:
			a = a > b;
			break;
		case PnLe:
			a = a <= b;
			break;
		case PnGe:
			a = a >= b;
			break;
		}
	}
	return a;
}

// Parse << and >> in #if expressions.
static int64_t
ishift(Ifp* p) {
	int64_t a, b;
	int op;

	a = iadd(p);
	for (;;) {
		op = ip_peek(p)->kind == TkPunct ? ip_peek(p)->punct : -1;
		if (op != PnShl && op != PnShr)
			break;
		ip_take(p);
		b = iadd(p);
		a = op == PnShl ? a << b : a >> b;
	}
	return a;
}

// Parse + and - in #if expressions.
static int64_t
iadd(Ifp* p) {
	int64_t a, b;
	int op;

	a = imul(p);
	for (;;) {
		op = ip_peek(p)->kind == TkPunct ? ip_peek(p)->punct : -1;
		if (op != PnPlus && op != PnMinus)
			break;
		ip_take(p);
		b = imul(p);
		a = op == PnPlus ? a + b : a - b;
	}
	return a;
}

// Parse *, /, % in #if expressions.
static int64_t
imul(Ifp* p) {
	int64_t a, b;
	int op;

	a = iunary(p);
	for (;;) {
		op = ip_peek(p)->kind == TkPunct ? ip_peek(p)->punct : -1;
		if (op != PnStar && op != PnSlash && op != PnPercent)
			break;
		ip_take(p);
		b = iunary(p);
		if (op == PnStar)
			a *= b;
		else if (b == 0)
			a = 0;
		else if (op == PnSlash)
			a /= b;
		else
			a %= b;
	}
	return a;
}

// Parse unary ! ~ + - in #if expressions.
static int64_t
iunary(Ifp* p) {
	if (ip_eatp(p, PnBang))
		return !iunary(p);
	if (ip_eatp(p, PnTilde))
		return ~iunary(p);
	if (ip_eatp(p, PnPlus))
		return iunary(p);
	if (ip_eatp(p, PnMinus))
		return -iunary(p);
	return iprimary(p);
}

// Parse primaries: numbers, defined(), macros, and parenthesized #if exprs.
static int64_t
iprimary(Ifp* p) {
	Tok* t;
	Macro* m;
	int64_t v;
	char* name;

	t = ip_peek(p);
	if (t->kind == TkPunct && t->punct == PnLparen) {
		ip_take(p);
		v = iexpr(p);
		if (!ip_eatp(p, PnRparen)) {
			p->err = 1;
			error_tok(p->c, t, "syntax in #if");
		}
		return v;
	}
	if (ident_is(t, "defined")) {
		ip_take(p);
		t = ip_peek(p);
		if (t->kind == TkPunct && t->punct == PnLparen) {
			ip_take(p);
			t = ip_take(p);
			name = t->kind == TkIdent ? t->s : NULL;
			if (!ip_eatp(p, PnRparen)) {
				p->err = 1;
				error_tok(p->c, t, "syntax in #if");
			}
		} else if (t->kind == TkIdent) {
			name = t->s;
			ip_take(p);
		} else {
			p->err = 1;
			error_tok(p->c, t, "syntax in #if");
			return 0;
		}
		return name && find_macro(p->c, name) != NULL;
	}
	if (t->kind == TkNumber || t->kind == TkCharLit) {
		v = t->int_val;
		ip_take(p);
		return v;
	}
	if (t->kind == TkKw) {
		/* sizeof and such are not allowed */
		p->err = 1;
		error_tok(p->c, t, "syntax in #if");
		ip_take(p);
		return 0;
	}
	if (t->kind == TkIdent) {
		if (strcmp(t->s, "__LINE__") == 0 && find_macro(p->c, t->s) != NULL) {
			v = t->span.line;
			ip_take(p);
			return v;
		}
		m = find_macro(p->c, t->s);
		ip_take(p);
		if (m == NULL || m->hide)
			return 0;
		if (m->body_len == 1 && (m->body[0].kind == TkNumber || m->body[0].kind == TkCharLit))
			return m->body[0].int_val;
		if (m->body_len == 0)
			return 0;
		/* expand object-like into a tiny eval — identifiers → 0 */
		{
			Ifp sub;
			Tok* body = m->body;
			int nb = m->body_len;
			/* If body is a single ident, recurse once */
			if (nb == 1 && body[0].kind == TkIdent) {
				Macro* m2 = find_macro(p->c, body[0].s);
				if (m2 && m2->body_len == 1 && m2->body[0].kind == TkNumber)
					return m2->body[0].int_val;
				return 0;
			}
			memset(&sub, 0, sizeof(sub));
			sub.c = p->c;
			sub.t = body;
			sub.n = nb;
			m->hide = 1;
			v = iexpr(&sub);
			m->hide = 0;
			if (sub.err)
				p->err = 1;
			return v;
		}
	}
	p->err = 1;
	error_tok(p->c, t, "syntax in #if");
	if (t->kind != TkEof)
		ip_take(p);
	return 0;
}

// Evaluate the constant expression on a #if / #elif line.
// Expand macros first (C rules). Resolve defined() before expansion so the
// operand is not macro-replaced. Remaining identifiers evaluate to 0.
static int
if_eval(Compiler* c, Tok* src, int src_files_len, int* i) {
	Tok *line, *exp;
	int n, cap, start, val, end, j, nexp;
	Ifp p;
	Tok one;

	start = *i;
	n = 0;
	cap = 0;
	line = NULL;
	end = start;
	while (end < src_files_len && src[end].kind != TkNewline && src[end].kind != TkEof) {
		if (n >= cap) {
			int ncap = cap ? cap * 2 : 16;
			line = pp_bump_grow(line, (size_t)cap * sizeof(Tok), (size_t)ncap * sizeof(Tok));
			cap = ncap;
		}
		line[n++] = src[end++];
	}

	/* defined ident / defined(ident) → 0/1 before macro expansion. */
	for (j = 0; j < n; j++) {
		char* name;
		int defined;

		if (!ident_is(&line[j], "defined"))
			continue;
		name = NULL;
		if (j + 1 < n && line[j + 1].kind == TkIdent) {
			name = line[j + 1].s;
			memset(&one, 0, sizeof(one));
			one.kind = TkNumber;
			one.int_val = name && find_macro(c, name) != NULL;
			one.s = one.int_val ? "1" : "0";
			one.span = line[j].span;
			line[j] = one;
			/* blank the identifier so expansion skips it */
			memset(&line[j + 1], 0, sizeof(Tok));
			line[j + 1].kind = TkNewline;
			j++;
			continue;
		}
		if (j + 3 < n && line[j + 1].kind == TkPunct && line[j + 1].punct == PnLparen &&
		    (line[j + 2].kind == TkIdent || line[j + 2].kind == TkKw) &&
		    line[j + 3].kind == TkPunct && line[j + 3].punct == PnRparen) {
			name = line[j + 2].s;
			defined = name && find_macro(c, name) != NULL;
			memset(&one, 0, sizeof(one));
			one.kind = TkNumber;
			one.int_val = defined;
			one.s = defined ? "1" : "0";
			one.span = line[j].span;
			line[j] = one;
			memset(&line[j + 1], 0, sizeof(Tok));
			line[j + 1].kind = TkNewline;
			memset(&line[j + 2], 0, sizeof(Tok));
			line[j + 2].kind = TkNewline;
			memset(&line[j + 3], 0, sizeof(Tok));
			line[j + 3].kind = TkNewline;
			j += 3;
		}
	}

	exp = NULL;
	nexp = 0;
	expand_list_into(c, line, n, &exp, &nexp);

	memset(&p, 0, sizeof(p));
	p.c = c;
	p.t = exp;
	p.n = nexp;
	val = nexp == 0 ? 0 : (int)iexpr(&p);
	if (p.err)
		val = 0;
	*i = end;
	pp_arena_reset();
	return val != 0;
}

// Parse macro call arguments between ( and ), respecting nested parens.
static int
collect_args(Tok* src, int src_files_len, int* i, Tok*** args, int** argn, int* args_len) {
	int depth, na, n, cap, acap;
	Tok* buf;

	/* *i is on '(' */
	if (*i >= src_files_len || src[*i].kind != TkPunct || src[*i].punct != PnLparen)
		return 0;
	(*i)++;
	na = 0;
	acap = 0;
	*args = NULL;
	*argn = NULL;
	if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnRparen) {
		(*i)++;
		*args_len = 0;
		return 1;
	}
	for (;;) {
		n = 0;
		cap = 0;
		buf = NULL;
		depth = 0;
		while (*i < src_files_len) {
			if (src[*i].kind == TkPunct && src[*i].punct == PnLparen)
				depth++;
			else if (src[*i].kind == TkPunct && src[*i].punct == PnRparen) {
				if (depth == 0)
					break;
				depth--;
			} else if (src[*i].kind == TkPunct && src[*i].punct == PnComma && depth == 0)
				break;
			if (n >= cap) {
				int ncap = cap ? cap * 2 : 8;
				buf = pp_bump_grow(buf, (size_t)cap * sizeof(Tok), (size_t)ncap * sizeof(Tok));
				cap = ncap;
			}
			buf[n++] = src[(*i)++];
		}
		if (na >= acap) {
			int nacap = acap ? acap * 2 : 4;
			*args = pp_bump_grow(*args, (size_t)acap * sizeof(Tok*), (size_t)nacap * sizeof(Tok*));
			*argn = pp_bump_grow(*argn, (size_t)acap * sizeof(int), (size_t)nacap * sizeof(int));
			acap = nacap;
		}
		(*args)[na] = buf;
		(*argn)[na] = n;
		na++;
		if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnComma) {
			(*i)++;
			continue;
		}
		if (*i < src_files_len && src[*i].kind == TkPunct && src[*i].punct == PnRparen) {
			(*i)++;
			break;
		}
		break;
	}
	*args_len = na;
	return 1;
}

enum {
	MpNone = -3,
	MpVa = -1
};

static void expand_list_into(Compiler* c, Tok* src, int src_files_len, Tok** out, int* out_len);
static void expand_into_buf(Compiler* c, Tok* src, int src_files_len, int i, int* ni, Tok** out, int* out_len);

// Append one token to a growable macro-expansion buffer (bump arena).
static void
pp_append(Tok** dst, int* n, int* cap, Tok t) {
	if (*n >= *cap) {
		int ncap = *cap ? *cap * 2 : 8;
		*dst = pp_bump_grow(*dst, (size_t)*cap * sizeof(Tok), (size_t)ncap * sizeof(Tok));
		*cap = ncap;
	}
	(*dst)[(*n)++] = t;
}

// Append a token sequence, skipping newlines and EOF markers.
static void
pp_append_n(Tok** dst, int* n, int* cap, Tok* src, int src_files_len) {
	int i, add;

	add = 0;
	for (i = 0; i < src_files_len; i++)
		if (src[i].kind != TkNewline && src[i].kind != TkEof)
			add++;
	if (add == 0)
		return;
	if (*n + add > *cap) {
		int ncap = *cap ? *cap : 8;
		while (ncap < *n + add)
			ncap *= 2;
		*dst = pp_bump_grow(*dst, (size_t)*cap * sizeof(Tok), (size_t)ncap * sizeof(Tok));
		*cap = ncap;
	}
	for (i = 0; i < src_files_len; i++)
		if (src[i].kind != TkNewline && src[i].kind != TkEof)
			(*dst)[(*n)++] = src[i];
}

// Append one token's spelling to a buffer (for # and ## processing).
static void
spell_tok(Tok* t, char** buf, int* len, int* cap) {
	const char* s;
	int n, i;

	if (t == NULL)
		return;
	if (t->kind == TkIdent || t->kind == TkNumber || t->kind == TkKw) {
		s = t->s ? t->s : "";
		n = (int)strlen(s);
	} else if (t->kind == TkString) {
		/* Include quotes; escape interior \ and " for stringize. */
		n = 2;
		s = t->s ? t->s : "";
		for (i = 0; s[i]; i++) {
			if (s[i] == '\\' || s[i] == '"')
				n++;
			n++;
		}
		if (*len + n + 1 > *cap) {
			*cap = *len + n + 16;
			*buf = xrealloc(*buf, *cap);
		}
		(*buf)[(*len)++] = '"';
		for (i = 0; s[i]; i++) {
			if (s[i] == '\\' || s[i] == '"')
				(*buf)[(*len)++] = '\\';
			(*buf)[(*len)++] = s[i];
		}
		(*buf)[(*len)++] = '"';
		(*buf)[*len] = 0;
		return;
	} else if (t->kind == TkCharLit) {
		n = 2;
		s = t->s ? t->s : "";
		for (i = 0; s[i]; i++) {
			if (s[i] == '\\' || s[i] == '\'')
				n++;
			n++;
		}
		if (*len + n + 1 > *cap) {
			*cap = *len + n + 16;
			*buf = xrealloc(*buf, *cap);
		}
		(*buf)[(*len)++] = '\'';
		for (i = 0; s[i]; i++) {
			if (s[i] == '\\' || s[i] == '\'')
				(*buf)[(*len)++] = '\\';
			(*buf)[(*len)++] = s[i];
		}
		(*buf)[(*len)++] = '\'';
		(*buf)[*len] = 0;
		return;
	} else if (t->kind == TkPunct) {
		s = punct_spell(t->punct);
		n = (int)strlen(s);
	} else
		return;
	if (*len + n + 1 > *cap) {
		*cap = *len + n + 16;
		*buf = xrealloc(*buf, *cap);
	}
	memcpy(*buf + *len, s, n + 1);
	*len += n;
}

// Turn a macro argument token list into one string literal (# stringize).
static Tok
stringize_arg(Tok* src, int src_files_len, Span span) {
	Tok t;
	char* buf;
	int len, cap, i, needsp;

	buf = NULL;
	len = 0;
	cap = 0;
	needsp = 0;
	for (i = 0; i < src_files_len; i++) {
		if (src[i].kind == TkNewline || src[i].kind == TkEof)
			continue;
		if (needsp && src[i].ws) {
			if (len + 2 > cap) {
				cap = len + 16;
				buf = xrealloc(buf, cap);
			}
			buf[len++] = ' ';
			buf[len] = 0;
		}
		spell_tok(&src[i], &buf, &len, &cap);
		needsp = 1;
	}
	memset(&t, 0, sizeof(t));
	t.kind = TkString;
	t.s = buf ? buf : xstrdup("");
	t.span = span;
	return t;
}

// Longest-match punctuator spelling at the start of s (for ## re-lex).
static int
match_punct_spell(const char* s, int* np) {
	int best, bestlen, p, n;
	const char* sp;

	best = -1;
	bestlen = 0;
	for (p = 0; p < PnCount; p++) {
		sp = punct_spell(p);
		n = (int)strlen(sp);
		if (n > bestlen && strncmp(s, sp, n) == 0) {
			best = p;
			bestlen = n;
		}
	}
	*np = bestlen;
	return best;
}

// Re-lex a glued spelling into a single preprocessing token after ##.
static int
tok_from_spell(Compiler* c, const char* s, Span span, Tok* out) {
	int kw, p, np, i;
	char* copy;

	memset(out, 0, sizeof(*out));
	out->span = span;
	if (s == NULL || s[0] == 0)
		return 0;
	if (isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '$' || s[0] == '@') {
		for (i = 0; s[i] && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$' || s[i] == '@');
		     i++)
			;
		if (s[i] != 0) {
			error_tok(c, out, "invalid token paste \"%s\"", s);
			return 0;
		}
		copy = str_intern(c, s);
		kw = keyword(copy);
		if (kw >= 0) {
			out->kind = TkKw;
			out->kw = kw;
			out->s = copy;
		} else {
			out->kind = TkIdent;
			out->s = copy;
		}
		return 1;
	}
	if (isdigit((unsigned char)s[0])) {
		for (i = 0; s[i]; i++)
			if (!(isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_'))
				break;
		if (s[i] != 0) {
			error_tok(c, out, "invalid token paste \"%s\"", s);
			return 0;
		}
		out->kind = TkNumber;
		out->s = xstrdup(s);
		out->int_val = strtoll(s, NULL, 0);
		return 1;
	}
	p = match_punct_spell(s, &np);
	if (p >= 0 && s[np] == 0) {
		out->kind = TkPunct;
		out->punct = p;
		return 1;
	}
	error_tok(c, out, "invalid token paste \"%s\"", s);
	return 0;
}

// Token-paste the last left token with the first right token (##).
static void
glue_seq(Compiler* c, Tok** left, int* nleft, int* capleft, Tok* right, int nright, Span span) {
	char* buf;
	int len, cap, i;
	Tok glued;

	if (nright <= 0)
		return;
	if (*nleft <= 0) {
		pp_append_n(left, nleft, capleft, right, nright);
		return;
	}
	buf = NULL;
	len = 0;
	cap = 0;
	spell_tok(&(*left)[*nleft - 1], &buf, &len, &cap);
	spell_tok(&right[0], &buf, &len, &cap);
	(*nleft)--;
	if (tok_from_spell(c, buf, span, &glued))
		pp_append(left, nleft, capleft, glued);
	free(buf);
	for (i = 1; i < nright; i++)
		pp_append(left, nleft, capleft, right[i]);
}

// Map a body token to a formal parameter index, __VA_ARGS__, or none.
static int
param_idx(Macro* m, Tok* t) {
	int k;

	if (!m->func || t == NULL || t->kind != TkIdent || t->s == NULL)
		return MpNone;
	if (m->varargs && strcmp(t->s, "__VA_ARGS__") == 0)
		return MpVa;
	for (k = 0; k < m->params_len; k++)
		if (strcmp(t->s, m->params[k]) == 0)
			return k;
	return MpNone;
}

// Fetch raw (unexpanded) argument tokens for # or ## substitution.
static void
raw_arg(Macro* m, Tok** args, int* argn, int args_len, int pi, Tok** out, int* out_len, int* owned) {
	int i, j, n, cap;
	Tok comma;

	*owned = 0;
	*out = NULL;
	*out_len = 0;
	if (pi == MpVa) {
		n = 0;
		cap = 0;
		for (i = m->params_len; i < args_len; i++) {
			if (i > m->params_len) {
				memset(&comma, 0, sizeof(comma));
				comma.kind = TkPunct;
				comma.punct = PnComma;
				pp_append(out, &n, &cap, comma);
			}
			for (j = 0; j < argn[i]; j++)
				pp_append(out, &n, &cap, args[i][j]);
		}
		*out_len = n;
		*owned = 1;
		return;
	}
	if (pi >= 0 && pi < args_len) {
		*out = args[pi];
		*out_len = argn[pi];
	}
}

// True when t is the token-paste operator ##.
static int
is_hashhash(Tok* t) {
	return t && t->kind == TkPunct && t->punct == PnHashHash;
}

// True when t is the stringize operator #.
static int
is_hash(Tok* t) {
	return t && t->kind == TkPunct && t->punct == PnHash;
}

// Expand a macro body: substitute args, apply # and ##, pre-expand where required.
static void
subst_body(Compiler* c, Macro* m, Tok** args, int* argn, int args_len, Tok** out, int* out_len) {
	int i, pi, n, cap, nraw, raw_owned;
	int nleft, capleft;
	Tok *dst, *raw, *left;
	Tok** earg;
	int* eargn;
	int va_exp_n, va_owned;
	Tok* va_exp;

	/* Pre-expand ordinary args (Prosser: expand before insert unless # / ##). */
	earg = NULL;
	eargn = NULL;
	va_exp = NULL;
	va_exp_n = 0;
	va_owned = 0;
	if (m->func && m->params_len > 0) {
		earg = pp_bump((size_t)m->params_len * sizeof(Tok*));
		eargn = pp_bump((size_t)m->params_len * sizeof(int));
		for (i = 0; i < m->params_len; i++) {
			earg[i] = NULL;
			eargn[i] = 0;
			if (i < args_len)
				expand_list_into(c, args[i], argn[i], &earg[i], &eargn[i]);
		}
	}
	if (m->func && m->varargs) {
		raw_arg(m, args, argn, args_len, MpVa, &raw, &nraw, &raw_owned);
		expand_list_into(c, raw, nraw, &va_exp, &va_exp_n);
		(void)raw_owned;
		va_owned = 1;
	}

	n = 0;
	cap = 0;
	dst = NULL;
	for (i = 0; i < m->body_len;) {
		/* # param → stringize unexpanded argument */
		if (is_hash(&m->body[i])) {
			if (!m->func) {
				pp_append(&dst, &n, &cap, m->body[i]);
				i++;
				continue;
			}
			if (i + 1 >= m->body_len || (pi = param_idx(m, &m->body[i + 1])) == MpNone) {
				error_tok(c, &m->body[i], "'#' is not followed by a macro parameter");
				i++;
				continue;
			}
			raw_arg(m, args, argn, args_len, pi, &raw, &nraw, &raw_owned);
			pp_append(&dst, &n, &cap, stringize_arg(raw, nraw, m->body[i].span));
			(void)raw_owned;
			i += 2;
			continue;
		}

		if (is_hashhash(&m->body[i])) {
			error_tok(c, &m->body[i], "'##' cannot appear at either end of a macro body");
			i++;
			continue;
		}

		/* left ## right — operands are not macro-expanded first */
		if (i + 1 < m->body_len && is_hashhash(&m->body[i + 1])) {
			left = NULL;
			nleft = 0;
			capleft = 0;
			pi = param_idx(m, &m->body[i]);
			if (pi != MpNone) {
				raw_arg(m, args, argn, args_len, pi, &raw, &nraw, &raw_owned);
				pp_append_n(&left, &nleft, &capleft, raw, nraw);
				(void)raw_owned;
			} else
				pp_append(&left, &nleft, &capleft, m->body[i]);
			i += 2; /* skip left and ## */

			for (;;) {
				if (i >= m->body_len) {
					error_tok(c, &m->body[i - 1], "'##' cannot appear at either end of a macro body");
					break;
				}
				pi = param_idx(m, &m->body[i]);
				/* GNU ', ## __VA_ARGS__': drop comma when varargs empty */
				if (pi == MpVa) {
					raw_arg(m, args, argn, args_len, MpVa, &raw, &nraw, &raw_owned);
					if (nraw == 0) {
						if (nleft > 0 && left[nleft - 1].kind == TkPunct && left[nleft - 1].punct == PnComma)
							nleft--;
						(void)raw_owned;
						i++;
						break;
					}
					/* Non-empty: ## vanishes; keep left then VA_ARGS tokens */
					pp_append_n(&dst, &n, &cap, left, nleft);
					left = NULL;
					nleft = 0;
					pp_append_n(&dst, &n, &cap, raw, nraw);
					(void)raw_owned;
					i++;
					goto after_paste;
				}
				if (pi != MpNone) {
					raw_arg(m, args, argn, args_len, pi, &raw, &nraw, &raw_owned);
					glue_seq(c, &left, &nleft, &capleft, raw, nraw, m->body[i].span);
					(void)raw_owned;
				} else {
					glue_seq(c, &left, &nleft, &capleft, &m->body[i], 1, m->body[i].span);
				}
				i++;
				if (i < m->body_len && is_hashhash(&m->body[i])) {
					i++;
					continue;
				}
				break;
			}
			pp_append_n(&dst, &n, &cap, left, nleft);
		after_paste:
			continue;
		}

		/* Ordinary parameter: insert expanded argument */
		pi = param_idx(m, &m->body[i]);
		if (pi == MpVa) {
			pp_append_n(&dst, &n, &cap, va_exp, va_exp_n);
			i++;
			continue;
		}
		if (pi >= 0 && pi < m->params_len) {
			pp_append_n(&dst, &n, &cap, earg[pi], eargn[pi]);
			i++;
			continue;
		}
		/* Named param with no corresponding actual → empty */
		if (pi >= 0) {
			i++;
			continue;
		}

		pp_append(&dst, &n, &cap, m->body[i]);
		i++;
	}

	(void)va_owned;
	*out = dst;
	*out_len = n;
}

// Fully expand a token list into a buffer (does not emit to c->tokens).
static void
expand_list_into(Compiler* c, Tok* src, int src_files_len, Tok** out, int* out_len) {
	int i, n, cap, ni, nfully;
	Tok *dst, *fully;

	n = 0;
	cap = 0;
	dst = NULL;
	i = 0;
	while (i < src_files_len) {
		if (src[i].kind == TkIdent) {
			Macro* m = find_macro(c, src[i].s);
			if (m && !m->hide) {
				fully = NULL;
				nfully = 0;
				expand_into_buf(c, src, src_files_len, i, &ni, &fully, &nfully);
				pp_append_n(&dst, &n, &cap, fully, nfully);
				i = ni;
			} else {
				if (src[i].kind != TkNewline && src[i].kind != TkEof)
					pp_append(&dst, &n, &cap, src[i]);
				i++;
			}
		} else {
			if (src[i].kind != TkNewline && src[i].kind != TkEof)
				pp_append(&dst, &n, &cap, src[i]);
			i++;
		}
	}
	*out = dst;
	*out_len = n;
}

// Expand one macro invocation at src[i] into a fully expanded token list.
static void
expand_into_buf(Compiler* c, Tok* src, int src_files_len, int i, int* ni, Tok** out, int* out_len) {
	Macro* m;
	Tok **args, *repl, *fully;
	int *argn, args_len, nrepl, start, nfully;

	m = find_macro(c, src[i].s);
	start = i;
	*out = NULL;
	*out_len = 0;
	if (pp_expand_depth == 0)
		pp_dyn_span = src[start].span;
	pp_expand_depth++;
	if (is_file_or_line(src[start].s)) {
		Tok one;
		int cap = 0;

		if (strcmp(src[start].s, "__FILE__") == 0)
			one = make_file_tok(pp_dyn_span);
		else
			one = make_line_tok(pp_dyn_span);
		pp_append(out, out_len, &cap, one);
		*ni = start + 1;
		pp_expand_depth--;
		return;
	}
	i = start + 1;
	args = NULL;
	argn = NULL;
	args_len = 0;
	if (m->func) {
		while (i < src_files_len && src[i].kind == TkNewline)
			i++;
		if (i >= src_files_len || src[i].kind != TkPunct || src[i].punct != PnLparen) {
			int cap = 0;
			pp_append(out, out_len, &cap, src[start]);
			*ni = start + 1;
			pp_expand_depth--;
			return;
		}
		collect_args(src, src_files_len, &i, &args, &argn, &args_len);
	}
	*ni = i;
	m->hide = 1;
	subst_body(c, m, args, argn, args_len, &repl, &nrepl);
	fully = NULL;
	nfully = 0;
	expand_list_into(c, repl, nrepl, &fully, &nfully);
	m->hide = 0;
	*out = fully;
	*out_len = nfully;
	pp_expand_depth--;
}

// Expand one macro at src[i] and emit the replacement tokens.
static void
expand_into(Compiler* c, Tok* src, int src_files_len, int i, int* ni, int rec) {
	Tok* fully;
	int nfully;

	(void)rec;
	fully = NULL;
	nfully = 0;
	expand_into_buf(c, src, src_files_len, i, ni, &fully, &nfully);
	if (nfully > 0) {
		if (c->tokens_len + nfully > c->tokens_cap) {
			int ncap = c->tokens_cap ? c->tokens_cap : 256;
			while (ncap < c->tokens_len + nfully)
				ncap *= 2;
			c->tokens = xrealloc(c->tokens, (size_t)ncap * sizeof(Tok));
			c->tokens_cap = ncap;
		}
		memcpy(c->tokens + c->tokens_len, fully, (size_t)nfully * sizeof(Tok));
		c->tokens_len += nfully;
	}
	pp_arena_reset();
}

// True when any active #if branch is skipping tokens.
static int
skipping_now(int* st, int nsp) {
	int i;

	for (i = 0; i < nsp; i++)
		if (st[i] != IfOn)
			return 1;
	return 0;
}

// Parse one c_sources path operand from a #pragma modc directive.
static char*
pragma_collect_csource(Tok* src, int src_files_len, int* i) {
	char buf[1024];
	int off;

	if (*i >= src_files_len)
		return NULL;
	if (src[*i].kind == TkString) {
		char* path = xstrdup(src[*i].s);

		(*i)++;
		return path;
	}
	off = 0;
	while (*i < src_files_len) {
		Tok* t = &src[*i];

		if (t->kind == TkPunct && (t->punct == PnRparen || t->punct == PnComma))
			break;
		if (t->kind == TkIdent)
			off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s", t->s);
		else if (t->kind == TkPunct && t->punct == PnSlash) {
			if (off < (int)sizeof(buf))
				buf[off++] = '/';
		} else if (t->kind == TkPunct && t->punct == PnDot) {
			if (off < (int)sizeof(buf))
				buf[off++] = '.';
		} else
			break;
		if (off >= (int)sizeof(buf))
			break;
		(*i)++;
	}
	return off > 0 ? xstrdup(buf) : NULL;
}

// Main preprocessor pass: directives, conditionals, includes, and macro expansion.
static void
process(Compiler* c, Tok* src, int src_files_len, int* st, int* nsp) {
	int i, skip, angled, val;
	Tok* t;
	char *name, *dir;

	i = 0;
	while (i < src_files_len) {
		if (c->fatal)
			break;
		t = &src[i];
		if (t->kind == TkEof)
			break;
		if (t->kind == TkNewline) {
			i++;
			continue;
		}
		skip = skipping_now(st, *nsp);
		if (t->bol && tok_is_hash(t)) {
			i++;
			while (i < src_files_len && src[i].kind == TkNewline)
				i++;
			if (i >= src_files_len)
				break;
			dir = NULL;
			if (src[i].kind == TkIdent || src[i].kind == TkKw)
				dir = src[i].s;
			else if (src[i].kind == TkKw)
				dir = src[i].s;
			if (dir == NULL) {
				skip_nl(src, src_files_len, &i);
				continue;
			}
			i++;
			pp_n_dir++; /* count only; time is nested in include/if_eval */
			if (strcmp(dir, "ifdef") == 0 || strcmp(dir, "ifndef") == 0) {
				int want = dir[2] == 'd'; /* ifdef vs ifndef: ifdef has 'd' at [2] */
				want = strcmp(dir, "ifdef") == 0;
				name = (i < src_files_len && src[i].kind == TkIdent) ? src[i].s : NULL;
				if (i < src_files_len && src[i].kind == TkIdent)
					i++;
				val = name != NULL && find_macro(c, name) != NULL;
				if (!want)
					val = !val;
				if (*nsp >= 64) {
					error_tok(c, t, "too many nested #if");
					continue;
				}
				if (skip)
					st[(*nsp)++] = IfDone;
				else
					st[(*nsp)++] = val ? IfOn : IfWait;
				continue;
			}
			if (strcmp(dir, "if") == 0) {
				if (skip) {
					skip_nl(src, src_files_len, &i);
					if (*nsp < 64)
						st[(*nsp)++] = IfDone;
					continue;
				}
				val = if_eval(c, src, src_files_len, &i);
				if (*nsp >= 64) {
					error_tok(c, t, "too many nested #if");
					continue;
				}
				st[(*nsp)++] = val ? IfOn : IfWait;
				continue;
			}
			if (strcmp(dir, "elif") == 0) {
				if (*nsp <= 0) {
					error_tok(c, t, "#elif without #if");
					continue;
				}
				if (st[*nsp - 1] == IfWait && !skipping_now(st, *nsp - 1)) {
					val = if_eval(c, src, src_files_len, &i);
					st[*nsp - 1] = val ? IfOn : IfWait;
				} else {
					if (st[*nsp - 1] == IfOn)
						st[*nsp - 1] = IfDone;
					skip_nl(src, src_files_len, &i);
					continue;
				}
				continue;
			}
			if (strcmp(dir, "else") == 0) {
				if (*nsp <= 0)
					error_tok(c, t, "#else without #if");
				else if (st[*nsp - 1] == IfOn)
					st[*nsp - 1] = IfDone;
				else if (st[*nsp - 1] == IfWait)
					st[*nsp - 1] = IfOn;
				continue;
			}
			if (strcmp(dir, "endif") == 0) {
				if (*nsp <= 0)
					error_tok(c, t, "#endif without #if");
				else
					(*nsp)--;
				continue;
			}
			if (skip) {
				skip_nl(src, src_files_len, &i);
				continue;
			}
			if (strcmp(dir, "define") == 0) {
				do_define(c, src, src_files_len, &i, 0);
				skip_nl(src, src_files_len, &i);
				continue;
			}
			if (strcmp(dir, "undef") == 0) {
				if (i < src_files_len && (src[i].kind == TkIdent || src[i].kind == TkKw))
					undef_macro(c, src[i].s);
				skip_nl(src, src_files_len, &i);
				continue;
			}
			if (strcmp(dir, "include") == 0) {
				name = header_name(src, src_files_len, &i, &angled);
				skip_nl(src, src_files_len, &i);
				if (name) {
					do_include(c, t, name, angled, 0);
					free(name);
				} else
					error_tok(c, t, "bad #include");
				continue;
			}
			if (strcmp(dir, "error") == 0) {
				char msg[256];
				int nmsg = 0;
				msg[0] = 0;
				nmsg = snprintf(msg, sizeof(msg), "#error");
				while (i < src_files_len && src[i].kind != TkNewline && src[i].kind != TkEof) {
					if (src[i].kind == TkString)
						nmsg += snprintf(msg + nmsg, sizeof(msg) - nmsg, " \"%s\"", src[i].s);
					else if (src[i].kind == TkIdent || src[i].kind == TkNumber)
						nmsg += snprintf(msg + nmsg, sizeof(msg) - nmsg, " %s", src[i].s);
					else if (src[i].kind == TkPunct)
						nmsg += snprintf(msg + nmsg, sizeof(msg) - nmsg, " %c", '?');
					i++;
					if (nmsg >= (int)sizeof(msg) - 1)
						break;
				}
				error_tok(c, t, "%s", msg);
				c->fatal = 1;
				skip_nl(src, src_files_len, &i);
				continue;
			}
			if (strcmp(dir, "pragma") == 0) {
				if (i < src_files_len && ident_is(&src[i], "once")) {
					char* resolved = resolve_once(t->span.file ? t->span.file : "");
					mark_once(c, resolved);
					free(resolved);
				} else if (i < src_files_len && ident_is(&src[i], "modc")) {
					i++;
					if (i < src_files_len && ident_is(&src[i], "c_libs")) {
						i++;
						if (i < src_files_len && src[i].kind == TkPunct && src[i].punct == PnLparen) {
							i++;
							while (i < src_files_len && !(src[i].kind == TkPunct && src[i].punct == PnRparen)) {
								if (src[i].kind == TkIdent || src[i].kind == TkString)
									pkg_add_clib(c, src[i].s);
								i++;
							}
							if (i < src_files_len)
								i++;
						}
					} else if (i < src_files_len && ident_is(&src[i], "frameworks")) {
						i++;
						if (i < src_files_len && src[i].kind == TkPunct && src[i].punct == PnLparen) {
							i++;
							while (i < src_files_len && !(src[i].kind == TkPunct && src[i].punct == PnRparen)) {
								if (src[i].kind == TkIdent || src[i].kind == TkString)
									pkg_add_framework(c, src[i].s);
								i++;
							}
							if (i < src_files_len)
								i++;
						}
					} else if (i < src_files_len && ident_is(&src[i], "c_sources")) {
						i++;
						if (i < src_files_len && src[i].kind == TkPunct && src[i].punct == PnLparen) {
							i++;
							while (i < src_files_len && !(src[i].kind == TkPunct && src[i].punct == PnRparen)) {
								char* path;

								if (i < src_files_len && src[i].kind == TkPunct && src[i].punct == PnComma) {
									i++;
									continue;
								}
								path = pragma_collect_csource(src, src_files_len, &i);
								if (path) {
									pkg_add_csource(c, c->infile, path);
									free(path);
								}
							}
							if (i < src_files_len)
								i++;
						}
					}
				}
				skip_nl(src, src_files_len, &i);
				continue;
			}
			/* unknown directive: ignore */
			skip_nl(src, src_files_len, &i);
			continue;
		}
		if (skip) {
			i++;
			continue;
		}
		if (t->kind == TkIdent) {
			Macro* m = find_macro(c, t->s);
			if (m && !m->hide) {
				double t0 = 0;

				if (pp_prof)
					t0 = pp_now();
				expand_into(c, src, src_files_len, i, &i, 1);
				if (pp_prof) {
					pp_t_expand += pp_now() - t0;
					pp_n_expand++;
				}
			} else {
				double t0 = 0;

				if (pp_prof)
					t0 = pp_now();
				emit_tok(c, *t);
				i++;
				if (pp_prof) {
					pp_t_emit += pp_now() - t0;
					pp_n_emit++;
				} else
					pp_n_emit++;
			}
		} else {
			emit_tok(c, *t);
			i++;
			pp_n_emit++;
		}
	}
}

// Record a -D definition from the driver and apply it immediately.
void pp_define_cli(Compiler* c, const char* def) {
	if (def == NULL || def[0] == 0)
		return;
	if (c->cli_defs_len % 8 == 0)
		c->cli_defs = xrealloc(c->cli_defs, (c->cli_defs_len + 8) * sizeof(char*));
	c->cli_defs[c->cli_defs_len++] = xstrdup(def);
	pp_define(c, def);
}

// Define a macro from a "NAME" or "NAME=value" string (builtin or -D).
void pp_define(Compiler* c, const char* def) {
	char *copy, *eq;
	Tok src[8];
	int ii;

	copy = xstrdup(def);
	eq = strchr(copy, '=');
	memset(src, 0, sizeof(src));
	src[0].kind = TkIdent;
	src[0].s = copy;
	ii = 1;
	if (eq) {
		*eq = 0;
		src[1].kind = TkNumber;
		src[1].s = eq + 1;
		src[1].int_val = strtoll(eq + 1, NULL, 0);
		ii = 2;
	} else {
		src[1].kind = TkNumber;
		src[1].s = "1";
		src[1].int_val = 1;
		ii = 2;
	}
	src[ii].kind = TkNewline;
	ii = 0;
	do_define(c, src, 3, &ii, 0);
}

// Clear #pragma once, include-path, and include-guard caches between TUs.
void pp_clear_once(Compiler* c) {
	int i;
	struct PpOnce *o, *on;
	struct PpInc *e, *en;
	struct PpGuard *g, *gn;

	for (i = 0; i < c->once_files_len; i++)
		free(c->once_files[i]);
	free(c->once_files);
	c->once_files = NULL;
	c->once_files_len = 0;
	if (c->once_tab) {
		for (i = 0; i < c->once_tab_cap; i++) {
			for (o = c->once_tab[i]; o; o = on) {
				on = o->hash_next;
				free(o); /* path aliases once_files */
			}
		}
		free(c->once_tab);
		c->once_tab = NULL;
		c->once_tab_cap = 0;
	}
	if (c->include_tab) {
		for (i = 0; i < c->include_tab_cap; i++) {
			for (e = c->include_tab[i]; e; e = en) {
				en = e->hash_next;
				free(e->key);
				free(e->path);
				free(e);
			}
		}
		free(c->include_tab);
		c->include_tab = NULL;
		c->include_tab_cap = 0;
	}
	if (c->guard_tab) {
		for (i = 0; i < c->guard_tab_cap; i++) {
			for (g = c->guard_tab[i]; g; g = gn) {
				gn = g->hash_next;
				free(g->path);
				free(g->macro);
				free(g);
			}
		}
		free(c->guard_tab);
		c->guard_tab = NULL;
		c->guard_tab_cap = 0;
	}
}

// Install predefined macros (__FILE__, host OS/arch, etc.) before preprocessing.
void pp_init(Compiler* c) {
#ifdef __APPLE__
	pp_define(c, "__APPLE__");
#endif
#ifdef __linux__
	pp_define(c, "__linux__");
#endif
#ifdef _WIN32
	pp_define(c, "_WIN32");
	/* Clang-for-MSVC persona: enough for Windows SDK #if graphs. */
	pp_define(c, "_WIN64");
	pp_define(c, "_MSC_VER=1930");
	pp_define(c, "_MSC_FULL_VER=193000000");
	pp_define(c, "_MSC_EXTENSIONS");
	pp_define(c, "_M_X64=100");
	pp_define(c, "_M_AMD64=100");
	pp_define(c, "__clang__");
	pp_define(c, "__clang_major__=17");
	pp_define(c, "__clang_minor__=0");
	pp_define(c, "__clang_patchlevel__=0");
#endif
	/* Architecture: mirror the host compiler that built modc (not __GNUC__). */
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
	pp_define(c, "__x86_64__");
	pp_define(c, "__amd64__");
#elif defined(__i386__) || defined(_M_IX86)
	pp_define(c, "__i386__");
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64__)
	pp_define(c, "__aarch64__");
	pp_define(c, "__arm64__");
#elif defined(__arm__) || defined(_M_ARM)
	pp_define(c, "__arm__");
#elif defined(__riscv)
	pp_define(c, "__riscv");
#if defined(__riscv_xlen) && (__riscv_xlen == 64)
	pp_define(c, "__riscv_xlen=64");
#elif defined(__riscv_xlen) && (__riscv_xlen == 32)
	pp_define(c, "__riscv_xlen=32");
#endif
#endif
	/* Dynamic at expansion (invocation site); bodies unused. */
	pp_define(c, "__FILE__");
	pp_define(c, "__LINE__");
	(void)c;
}

// Run the preprocessor: rewrite c->tokens to a flat, directive-free token stream.
void pp_run(Compiler* c) {
	Tok* src;
	int src_files_len, skipstack[64], nsp;
	Tok eof;

	pp_prof = getenv("MODC_PROFILE") != NULL;
	pp_ninc_open = pp_ninc_once = pp_ninc_guard = pp_ninc_miss = 0;
	pp_t_find = pp_t_read = pp_t_lex = pp_t_proc = 0;
	pp_t_emit = pp_t_expand = pp_t_dir = 0;
	pp_n_emit = pp_n_expand = pp_n_dir = 0;
	pp_arena_clear();
	src = c->tokens;
	src_files_len = c->tokens_len;
	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = src_files_len > 256 ? src_files_len + src_files_len / 2 : 256;
	c->tokens = xmalloc_raw((size_t)c->tokens_cap * sizeof(Tok));
	nsp = 0;
	process(c, src, src_files_len, skipstack, &nsp);
	memset(&eof, 0, sizeof(eof));
	eof.kind = TkEof;
	eof.span.file = c->infile;
	emit_tok(c, eof);
	pp_arena_clear();
	if (pp_prof)
		fprintf(stderr,
			"modc pp: open=%d once_skip=%d guard_skip=%d miss=%d tokens_len=%d macros_len=%d\n"
			"         emit_toks=%llu expands=%llu dirs=%llu  expand=%.3fs emit=%.3fs find=%.3fs read=%.3fs lex=%.3fs\n",
			pp_ninc_open, pp_ninc_once, pp_ninc_guard, pp_ninc_miss, c->tokens_len, c->macros_len,
			pp_n_emit, pp_n_expand, pp_n_dir, pp_t_expand, pp_t_emit, pp_t_find, pp_t_read, pp_t_lex);
}
