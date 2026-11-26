/*
 * Canonical language topic pages for `modc doc <topic>` (dialect rules for
 * humans/agents). Baked into the binary for offline use; longer narrative
 * guides under docs/ may expand these rules but should not contradict them.
 * Package API docs stay in cmd_doc; shipped guides are markdown under lib/modc/docs/.
 */
#include "ast.h"
#include "host_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char* name;
	const char* summary;
	const char* body;
} LangTopic;

static const LangTopic topics[] = {
	{
		"auto",
		"local type inference and multi-return",
		"auto — local type inference and multi-return\n"
		"\n"
		"Rules:\n"
		"  • auto x = expr; infers the type of x from expr\n"
		"  • auto (a, b) = call(); unpacks a multi-return / tuple result\n"
		"  • auto is for locals; it is not a substitute for declared APIs\n"
		"  • for (auto x : range) iterates by value; for (auto *p : range) binds a pointer\n"
		"\n"
		"OK:\n"
		"  auto n = 1;\n"
		"  auto (ok, s) = str_dup(\"hi\");\n"
		"\n"
		"See also: methods, slice\n"
	},
	{
		"const",
		"const / const? and string literals",
		"const — read-only and passthrough APIs\n"
		"\n"
		"Rules:\n"
		"  • const T rejects mutation through that view\n"
		"  • const? T is polymorphic: callers may pass const or mutable; the body sees const\n"
		"  • string literals need const / const? sinks (char[..] or char *)\n"
		"  • do not use const? to launder a const value into a mutable API\n"
		"\n"
		"OK:\n"
		"  int len(const char[..] s);\n"
		"  void take(const? char[..] s);  /* caller binds constness */\n"
		"\n"
		"See also: slice, methods\n"
	},
	{
		"defer",
		"block-scoped LIFO cleanup",
		"defer — block-scoped LIFO cleanup\n"
		"\n"
		"Rules:\n"
		"  • defer runs when the enclosing block exits (including return)\n"
		"  • return values are evaluated before deferred statements run\n"
		"  • never return a pointer freed (or [[releases]]'d) by a defer in the same function\n"
		"  • goto may not jump over a defer\n"
		"  • inside defer: no return, goto, break, continue, labels, [[fallthrough]], or nested defer\n"
		"\n"
		"OK:\n"
		"  void f() {\n"
		"      char *p = malloc(64);\n"
		"      defer free(p);\n"
		"      use(p);\n"
		"  }\n"
		"\n"
		"Not OK:\n"
		"  char *bad() {\n"
		"      char *p = malloc(64);\n"
		"      defer free(p);\n"
		"      return p;   /* dangling */\n"
		"  }\n"
		"\n"
		"See also: provides, escape, goto, pool\n"
	},
	{
		"escape",
		"stack addresses and lifetime escapes",
		"escape — stack addresses must not outlive their frame\n"
		"\n"
		"Rules:\n"
		"  • do not return &local or &param\n"
		"  • do not store a stack address into a longer-lived location\n"
		"  • [[retains]] on a parameter means the callee may keep the pointer\n"
		"  • C / function-pointer boundaries are unchecked by design\n"
		"\n"
		"OK:\n"
		"  int *ok(int *p) { return p; }          /* param may be returned */\n"
		"  void keep([[retains]] char *s);\n"
		"\n"
		"Not OK:\n"
		"  int *bad() { int x = 0; return &x; }\n"
		"\n"
		"See also: defer, provides, pool\n"
	},
	{
		"goto",
		"goto scope and defer rules",
		"goto — restricted jumps in .mc\n"
		"\n"
		"Rules:\n"
		"  • goto must target a label in a compatible lexical scope\n"
		"  • goto may not jump over a declaration\n"
		"  • goto may not jump over a defer\n"
		"  • prefer structured control flow; goto is for rare cases\n"
		"\n"
		"See also: defer\n"
	},
	{
		"import",
		"packages and import resolution",
		"import — source packages without headers\n"
		"\n"
		"Rules:\n"
		"  • import \"path\"; brings a package's non-static API into scope\n"
		"  • static at file scope is package-private, not C file-local\n"
		"  • search order: relative → project root → vendor/ → -M/MODC_PATH → stdlib\n"
		"  • write import \"math\"; for vendor/math (no vendor/ prefix)\n"
		"  • declare deps in modc.ini; fetch with modc vendor\n"
		"\n"
		"See also: methods, auto\n"
	},
	{
		"methods",
		"receiver methods and overload",
		"methods — (T *r).name(…) / (T[..] s).name(…) receivers\n"
		"\n"
		"Rules:\n"
		"  • receivers: named struct/union T* only (no by-value T); or T[..] / T[..]*\n"
		"  • call with dot syntax: r.name() (auto-addr for T / T[..]* methods; no ->)\n"
		"  • method names must not collide with fields (incl. T[..] ptr/len/cap)\n"
		"  • in the method body: no null tests on pointer r, and no rebinding r\n"
		"  • live T* / T[..]*: entry inserts a null trap (--release omits); T[..] by value has none\n"
		"  • any package may extend a visible type; import the defining package to call\n"
		"  • duplicate (recv, name, params) is an error; same name may overload on params\n"
		"  • free functions: at most one unmarked (C ABI) + overload extras; &f needs a typed fnptr\n"
		"  • methods resolve like free overloads + embeds\n"
		"\n"
		"See also: auto, import, const\n"
	},
	{
		"pool",
		"bump regions and view lifetimes",
		"pool — bump allocation with block lifetime\n"
		"\n"
		"Rules:\n"
		"  • create with pool p { … } only (auto-freed at block end)\n"
		"  • p.alloc / p.copy / p.append views are valid until block end, p.free(), or p.reset()\n"
		"  • do not return or stash pool views past the pool's lifetime\n"
		"  • heap T[..] growth stays on bare reserve/push/append/release (not Pool methods)\n"
		"\n"
		"OK:\n"
		"  pool p {\n"
		"      char[..] s = {0};\n"
		"      p.copy(&s, \"hi\");\n"
		"      use(s);\n"
		"  }\n"
		"\n"
		"See also: slice, defer, escape\n"
	},
	{
		"provides",
		"ownership attrs at call boundaries",
		"provides — [[provides]] / [[releases]] / [[retains]]\n"
		"\n"
		"Rules:\n"
		"  • [[provides]] results must be released, retained, or returned as provides\n"
		"  • [[releases]] consumes an owned pointer / T[..] (caller must not use it after)\n"
		"  • [[retains]] lets a callee keep a pointer (escape-checked on .mc callers)\n"
		"  • free/fclose and similar hermetic prototypes carry these attrs\n"
		"  • use of a released value is an error; so is escaping a deferred release\n"
		"\n"
		"See also: defer, escape, pool\n"
	},
	{
		"slice",
		"T[..] ranged views",
		"slice — T[..] length-aware views\n"
		"\n"
		"Rules:\n"
		"  • T[..] is a typed pointer+length view (not a hidden growable object)\n"
		"  • indexed access is bounds-checked by default (--release omits traps)\n"
		"  • do not implicitly decay T[..] to T*; use ptr() / cstr at C edges\n"
		"  • heap growth: reserve / push / append / release on T[..]*\n"
		"  • ownership is convention; only release what you own\n"
		"\n"
		"See also: pool, const, provides\n"
	},
};

enum { LangTopicCount = (int)(sizeof(topics) / sizeof(topics[0])) };

static const LangTopic*
langdoc_find(const char* name) {
	int i;

	if (name == NULL || name[0] == 0)
		return NULL;
	for (i = 0; i < LangTopicCount; i++) {
		if (strcmp(topics[i].name, name) == 0)
			return &topics[i];
	}
	return NULL;
}

int
langdoc_is_topic(const char* name) {
	return langdoc_find(name) != NULL;
}

/*
 * Shipped markdown guides: in-tree <modc_pkg>/docs, or installed
 * dirname(modc_pkg)/docs (lib/modc/docs next to pkg/). MODC_DOCS overrides.
 */
static int
langdoc_guides_dir(Compiler* c, char* out, size_t n) {
	const char* env;
	char parent[HOST_PATH_MAX];
	char* slash;
	char probe[HOST_PATH_MAX];

	env = getenv("MODC_DOCS");
	if (env && env[0]) {
		snprintf(out, n, "%s", env);
		host_path_slashify(out);
		return host_is_dir(out) ? 0 : -1;
	}
	if (c == NULL || c->paths.modc_pkg == NULL || c->paths.modc_pkg[0] == 0)
		return -1;
	snprintf(out, n, "%s/docs", c->paths.modc_pkg);
	host_path_slashify(out);
	snprintf(probe, sizeof(probe), "%s/README.md", out);
	if (host_is_dir(out) && host_is_file(probe))
		return 0;
	snprintf(parent, sizeof(parent), "%s", c->paths.modc_pkg);
	slash = (char*)host_path_last_sep(parent);
	if (slash == NULL || slash == parent)
		return -1;
	*slash = 0;
	snprintf(out, n, "%s/docs", parent);
	host_path_slashify(out);
	snprintf(probe, sizeof(probe), "%s/README.md", out);
	if (host_is_dir(out) && host_is_file(probe))
		return 0;
	return -1;
}

static int
langdoc_guide_name_ok(const char* name) {
	size_t i;

	if (name == NULL || name[0] == 0)
		return 0;
	for (i = 0; name[i]; i++) {
		char ch = name[i];
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
			return 0;
	}
	return 1;
}

static int
langdoc_guide_exists(Compiler* c, const char* name) {
	char dir[HOST_PATH_MAX];
	char path[HOST_PATH_MAX];

	if (!langdoc_guide_name_ok(name))
		return 0;
	if (langdoc_guides_dir(c, dir, sizeof(dir)) != 0)
		return 0;
	snprintf(path, sizeof(path), "%s/%s.md", dir, name);
	host_path_slashify(path);
	return host_is_file(path);
}

static int
langdoc_guide_strcmp(const void* a, const void* b) {
	return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/* Caller frees each names[i] and names. Returns count, or -1 on missing docs/. */
static int
langdoc_collect_guides(Compiler* c, char*** out_names) {
	char dir[HOST_PATH_MAX];
	char** names = NULL;
	int names_len = 0, names_cap = 0;
	HostDir* d;
	const char* ent;

	*out_names = NULL;
	if (langdoc_guides_dir(c, dir, sizeof(dir)) != 0)
		return -1;
	d = host_opendir(dir);
	if (d == NULL)
		return -1;
	while ((ent = host_readdir(d)) != NULL) {
		size_t len = strlen(ent);
		char* base;

		if (len < 4 || strcmp(ent + len - 3, ".md") != 0)
			continue;
		base = xmalloc(len - 2);
		memcpy(base, ent, len - 3);
		base[len - 3] = 0;
		if (!langdoc_guide_name_ok(base)) {
			free(base);
			continue;
		}
		if (names_len >= names_cap) {
			names_cap = names_cap ? names_cap * 2 : 16;
			names = xrealloc(names, (size_t)names_cap * sizeof(char*));
		}
		names[names_len++] = base;
	}
	host_closedir(d);
	if (names_len > 1)
		qsort(names, (size_t)names_len, sizeof(char*), langdoc_guide_strcmp);
	*out_names = names;
	return names_len;
}

static void
langdoc_free_guides(char** names, int n) {
	int i;

	for (i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

static void
langdoc_print_guide_names(Compiler* c) {
	char** names = NULL;
	int n, i;

	n = langdoc_collect_guides(c, &names);
	if (n < 0) {
		printf("  (guides not installed — re-run make install)\n");
		return;
	}
	if (n == 0) {
		printf("  (none)\n");
		langdoc_free_guides(names, n);
		return;
	}
	for (i = 0; i < n; i++) {
		printf("  ");
		color_span(stdout, COL_BOLD COL_CYAN, names[i]);
		printf("\n");
	}
	langdoc_free_guides(names, n);
}

int
langdoc_print_guide_index(Compiler* c) {
	char dir[HOST_PATH_MAX];
	char** names = NULL;
	int n;

	if (langdoc_guides_dir(c, dir, sizeof(dir)) != 0) {
		fprintf(stderr, "modc doc: guides not installed (no docs/ next to stdlib)\n");
		return 1;
	}
	n = langdoc_collect_guides(c, &names);
	if (n < 0) {
		fprintf(stderr, "modc doc: cannot open %s\n", dir);
		return 1;
	}
	color_span(stdout, COL_BOLD COL_YELLOW, "Guides");
	printf(" (modc doc guide.<name>):\n");
	if (n == 0)
		printf("  (none found in %s)\n", dir);
	else {
		int i;

		for (i = 0; i < n; i++) {
			printf("  ");
			color_span(stdout, COL_BOLD COL_CYAN, names[i]);
			printf("\n");
		}
		printf("\n");
		printf("Short dialect rules: modc doc <topic>  (see modc doc topics)\n");
	}
	langdoc_free_guides(names, n);
	return 0;
}

int
langdoc_print_guide(Compiler* c, const char* name) {
	char dir[HOST_PATH_MAX];
	char path[HOST_PATH_MAX];
	char* text;
	size_t n;

	if (!langdoc_guide_name_ok(name)) {
		fprintf(stderr, "modc doc: invalid guide name \"%s\"\n", name);
		return 1;
	}
	if (langdoc_guides_dir(c, dir, sizeof(dir)) != 0) {
		fprintf(stderr, "modc doc: guides not installed (no docs/ next to stdlib)\n");
		return 1;
	}
	snprintf(path, sizeof(path), "%s/%s.md", dir, name);
	host_path_slashify(path);
	text = read_file(path, &n);
	if (text == NULL) {
		fprintf(stderr, "modc doc: unknown guide \"%s\" (try modc doc guide)\n", name);
		return 1;
	}
	fwrite(text, 1, n, stdout);
	if (n == 0 || text[n - 1] != '\n')
		fputc('\n', stdout);
	free(text);
	return 0;
}

void
langdoc_print_index(Compiler* c) {
	int i;

	color_span(stdout, COL_BOLD COL_YELLOW, "modc doc");
	printf(" — %%C documentation hub\n");
	printf("\n");
	color_span(stdout, COL_BOLD COL_YELLOW, "Start here:");
	printf("\n");
	printf("  modc doc               this hub\n");
	printf("  modc doc <topic>       short dialect rules (defer, const, escape, …)\n");
	printf("  modc doc guide         shipped markdown guides\n");
	printf("  modc doc guide.<name>  one guide (quickstart, memory, …)\n");
	printf("  modc doc .             current package API\n");
	printf("  modc doc <pkg>         package exports (cwd / -M / stdlib)\n");
	printf("  modc doc <pkg.Name>    one symbol\n");
	printf("  modc doc --check <pkg> undoc'd export hygiene\n");
	printf("\n");
	color_span(stdout, COL_BOLD COL_YELLOW, "Language topics");
	printf(" (modc doc <topic>):\n");
	for (i = 0; i < LangTopicCount; i++) {
		int pad = 10 - (int)strlen(topics[i].name);

		printf("  ");
		color_span(stdout, COL_BOLD COL_CYAN, topics[i].name);
		if (pad < 1)
			pad = 1;
		printf("%*s%s\n", pad, "", topics[i].summary);
	}
	printf("\n");
	color_span(stdout, COL_BOLD COL_YELLOW, "Stdlib packages:");
	printf("\n");
	printf("  modc doc str | path | fs | os | tty | crash\n");
	printf("\n");
	color_span(stdout, COL_BOLD COL_YELLOW, "Guides");
	printf(" (modc doc guide.<name>):\n");
	langdoc_print_guide_names(c);
	printf("\n");
	printf("Dialect errors may print: note: modc doc <topic>\n");
}

/* Bold section headers in baked topic pages (Rules / OK / Not OK / See also). */
static int
langdoc_section_hdr(const char* line, size_t n) {
	return (n >= 6 && strncmp(line, "Rules:", 6) == 0) ||
	       (n >= 3 && strncmp(line, "OK:", 3) == 0) ||
	       (n >= 7 && strncmp(line, "Not OK:", 7) == 0) ||
	       (n >= 9 && strncmp(line, "See also:", 9) == 0);
}

/* Same-named guide first; else a narrative alias (pool → memory, …). */
static const char*
langdoc_topic_longer_name(Compiler* c, const char* topic) {
	static const struct {
		const char* topic;
		const char* guide;
	} aliases[] = {
		{ "escape", "memory" },
		{ "pool", "memory" },
		{ "provides", "memory" },
		{ "slice", "arrays" },
		{ "import", "packages" },
		{ "goto", "defer" },
	};
	int i;

	if (langdoc_guide_exists(c, topic))
		return topic;
	for (i = 0; i < (int)(sizeof(aliases) / sizeof(aliases[0])); i++) {
		if (strcmp(aliases[i].topic, topic) == 0 &&
		    langdoc_guide_exists(c, aliases[i].guide))
			return aliases[i].guide;
	}
	return NULL;
}

static void
langdoc_print_topic_longer(Compiler* c, const char* name) {
	const char* guide;

	guide = langdoc_topic_longer_name(c, name);
	if (guide == NULL)
		return;
	printf("\n");
	if (color_enabled(stdout))
		color_span(stdout, COL_BOLD COL_YELLOW, "Longer:");
	else
		fputs("Longer:", stdout);
	printf(" modc doc guide.%s\n", guide);
}

int
langdoc_print_topic(Compiler* c, const char* name) {
	const LangTopic* t;
	const char* body;
	const char* p;
	const char* nl;
	size_t n;
	int first;

	t = langdoc_find(name);
	if (t == NULL)
		return 0;
	body = t->body;
	if (body == NULL)
		return 0;
	if (!color_enabled(stdout)) {
		fputs(body, stdout);
		langdoc_print_topic_longer(c, name);
		return 1;
	}
	first = 1;
	p = body;
	while (*p) {
		nl = strchr(p, '\n');
		n = nl ? (size_t)(nl - p) : strlen(p);
		if (first && n > 0) {
			fputs(COL_BOLD, stdout);
			fwrite(p, 1, n, stdout);
			fputs(COL_RESET, stdout);
			first = 0;
		} else if (langdoc_section_hdr(p, n)) {
			fputs(COL_BOLD COL_YELLOW, stdout);
			fwrite(p, 1, n, stdout);
			fputs(COL_RESET, stdout);
		} else
			fwrite(p, 1, n, stdout);
		if (nl) {
			fputc('\n', stdout);
			p = nl + 1;
		} else
			break;
	}
	langdoc_print_topic_longer(c, name);
	return 1;
}
