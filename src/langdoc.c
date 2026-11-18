/*
 * Canonical language topic pages for `modc doc <topic>` (dialect rules for
 * humans/agents). Baked into the binary for offline use; longer narrative
 * guides under docs/ may expand these rules but should not contradict them.
 * Package API docs stay in cmd_doc.
 */
#include "ast.h"
#include <stdio.h>
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
		"  • string literals are const char[..]; pass them to const / const? parameters\n"
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
		"  • a deferred statement cannot contain return/goto/defer\n"
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
		"methods — (T *r).name(…) receivers\n"
		"\n"
		"Rules:\n"
		"  • receivers are explicit pointers: void (T *r).name(…)\n"
		"  • call with dot syntax: r.name() (auto-deref; no ->)\n"
		"  • method names must not collide with fields on T\n"
		"  • methods may be defined outside T's origin package (import the defining package)\n"
		"  • overload marks free-function overloading; methods use receiver dispatch\n"
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

void
langdoc_print_index(void) {
	int i;

	printf("Language topics (modc doc <topic>):\n");
	for (i = 0; i < LangTopicCount; i++)
		printf("  %-10s %s\n", topics[i].name, topics[i].summary);
	printf("\n");
	printf("Package docs:\n");
	printf("  modc doc .           current package API\n");
	printf("  modc doc pkg         package pkg\n");
	printf("  modc doc pkg.Name    one symbol\n");
	printf("  modc doc topics      this index\n");
}

int
langdoc_print_topic(const char* name) {
	const LangTopic* t;
	const char* body;
	const char* nl;
	size_t n;

	t = langdoc_find(name);
	if (t == NULL)
		return 0;
	body = t->body;
	if (body == NULL)
		return 0;
	/* Bold the title line only when stdout is a TTY. */
	nl = strchr(body, '\n');
	if (nl != NULL) {
		n = (size_t)(nl - body);
		if (color_enabled(stdout) && n > 0) {
			fputs(COL_BOLD, stdout);
			fwrite(body, 1, n, stdout);
			fputs(COL_RESET, stdout);
			fputs(nl, stdout);
			return 1;
		}
	}
	fputs(body, stdout);
	return 1;
}
