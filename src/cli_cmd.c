/*
 * CLI subcommands: check, emit, build, run, test, doc, format, install, clean.
 */
#include "cli.h"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

// modc check: typecheck one or more roots without codegen.
int
cmd_check(Compiler* c, CliOpts* o, int argc, char** argv) {
	int i, r, err;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 0);
	if (r < 0) {
		usage("check");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	o->check_only = 1;
	c->opt.check_only = 1;
	if (o->files_len == 0) {
		fprintf(stderr, "modc check: no input files\n");
		return 1;
	}
	err = 0;
	for (r = 0; r < o->files_len; r++) {
		if (o->verbose)
			fprintf(stderr, "checking %s\n", o->files[r]);
		if (compile_check(c, o, o->files[r]))
			err = 1;
		if (r + 1 < o->files_len)
			reset_comp_state(c);
	}
	return err;
}

// modc emit: QBE IL for a single input path.
int
cmd_emit(Compiler* c, CliOpts* o, int argc, char** argv) {
	int i, r;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 1);
	if (r < 0) {
		usage("emit");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	if (o->files_len != 1) {
		fprintf(stderr, "modc emit: exactly one input file required\n");
		return 1;
	}
	if (o->verbose)
		fprintf(stderr, "emitting QBE from %s\n", o->files[0]);
	return emit_one(c, o, o->files[0]);
}

// modc build: compile and link to an executable.
int
cmd_build(Compiler* c, CliOpts* o, int argc, char** argv) {
	char dir[HOST_PATH_MAX];
	int i, r;
	char* outname;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 1);
	if (r < 0) {
		usage("build");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	ensure_build_root(o);
	if (o->files_len != 1) {
		fprintf(stderr, "modc build: at most one input path (file or directory)\n");
		return 1;
	}
	outname = NULL;
	if (o->output == NULL) {
		outname = default_out_name(o->files[0], o->target);
		o->output = outname;
	}
	/* Default (and common -o build/…) lands under build/; ensure it exists. */
	if (o->output &&
	    (strncmp(o->output, "build/", 6) == 0 || strncmp(o->output, "build\\", 6) == 0))
		(void)host_mkdir("build");
	if (o->verbose)
		fprintf(stderr, "building %s -> %s\n", o->files[0], o->output);
	if (host_mkdtemp(dir, sizeof(dir), "modc-build") != 0) {
		fprintf(stderr, "modc: cannot create temp dir: %s\n", strerror(errno));
		free(outname);
		return 1;
	}
	r = compile_link_exe(c, o, o->files[0], dir, o->output);
	cleanup_tmpdir(dir);
	free(outname);
	return r;
}

// True if path is an existing executable file (follows symlinks).
static int
wine_is_bin(const char* path) {
	if (path == NULL || path[0] == 0 || !host_is_file(path))
		return 0;
#ifdef _WIN32
	return 1;
#else
	return access(path, X_OK) == 0;
#endif
}

// Search PATH for an executable name; result in out (HOST_PATH_MAX).
static int
wine_find_on_path(const char* name, char* out, size_t outn) {
	const char* path;
	char piece[HOST_PATH_MAX];
	size_t i;

	if (name == NULL || out == NULL || outn == 0)
		return 0;
	path = getenv("PATH");
	if (path == NULL)
		return 0;
	for (;;) {
		i = 0;
		while (path[i] && path[i] != ':')
			i++;
		if (i > 0 && i < sizeof(piece)) {
			memcpy(piece, path, i);
			piece[i] = 0;
			snprintf(out, outn, "%s/%s", piece, name);
			if (wine_is_bin(out))
				return 1;
		}
		if (path[i] == 0)
			break;
		path += i + 1;
	}
	return 0;
}

// Resolve CrossOver/Wine for running PE on a non-Windows host.
// Prefer CrossOver, then Homebrew/prefix wine, then PATH — never a bare name
// that might hang spawn when missing.
static const char*
tool_wine(void) {
	const char* w;
	static char pathbuf[HOST_PATH_MAX];
	static const char* abs_cands[] = {
		/* CrossOver first on macOS */
		"/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine",
		/* Homebrew / local Wine */
		"/opt/homebrew/bin/wine64",
		"/opt/homebrew/bin/wine",
		"/usr/local/bin/wine64",
		"/usr/local/bin/wine",
		NULL
	};
	static const char* path_names[] = { "wine64", "wine", NULL };
	int i;

	w = getenv("MODC_WINE");
	if (w && w[0]) {
		if (wine_is_bin(w))
			return w;
		fprintf(stderr, "modc: MODC_WINE=%s is not an executable file\n", w);
		return NULL;
	}
	w = getenv("CX_ROOT");
	if (w && w[0]) {
		snprintf(pathbuf, sizeof(pathbuf), "%s/bin/wine", w);
		if (wine_is_bin(pathbuf))
			return pathbuf;
	}
	for (i = 0; abs_cands[i]; i++) {
		if (wine_is_bin(abs_cands[i]))
			return abs_cands[i];
	}
	for (i = 0; path_names[i]; i++) {
		if (wine_find_on_path(path_names[i], pathbuf, sizeof(pathbuf)))
			return pathbuf;
	}
	return NULL;
}

// Build a temp exe for path, spawn it, then clean up.
int
build_and_run_root(Compiler* c, CliOpts* o, const char* path) {
	char dir[HOST_PATH_MAX];
	char prog[512];
	int i, st, runargs_len;
	const char** runargv;
	int nrun;
	const char* wine;
	int use_wine;

	c->paths.c_libs_len = 0;
	c->paths.frameworks_len = 0;
	c->paths.csources_len = 0;
	if (host_mkdtemp(dir, sizeof(dir), "modc-run") != 0) {
		fprintf(stderr, "modc: cannot create temp dir: %s\n", strerror(errno));
		return 1;
	}
	use_wine = 0;
#ifndef _WIN32
	if (c->opt.target == TargetWindows)
		use_wine = 1;
#endif
#ifdef _WIN32
	snprintf(prog, sizeof(prog), "%s/prog.exe", dir);
#else
	if (c->opt.target == TargetWindows)
		snprintf(prog, sizeof(prog), "%s/prog.exe", dir);
	else
		snprintf(prog, sizeof(prog), "%s/prog", dir);
#endif
	runargs_len = o->linkargv_len;
	o->linkargv_len = 0;
	st = compile_link_exe(c, o, path, dir, prog);
	o->linkargv_len = runargs_len;
	if (st != 0) {
		cleanup_tmpdir(dir);
		return 1;
	}
	wine = NULL;
	if (use_wine) {
		wine = tool_wine();
		if (wine == NULL) {
			fprintf(stderr,
				"modc: --target=windows: no Wine runner found\n"
				"  install CrossOver or Homebrew wine, or set MODC_WINE to the wine binary\n");
			cleanup_tmpdir(dir);
			return 1;
		}
		if (getenv("CX_BOTTLE") == NULL && getenv("WINEPREFIX") == NULL) {
			/* Prefer the smoke bottle if present. */
			static char bottle[HOST_PATH_MAX];
			const char* home;

			home = getenv("HOME");
			if (home) {
				snprintf(bottle, sizeof(bottle),
					 "%s/Library/Application Support/CrossOver/Bottles/modc-win64",
					 home);
				if (host_is_dir(bottle))
					setenv("CX_BOTTLE", "modc-win64", 0);
			}
		}
		if (getenv("WINEDEBUG") == NULL)
			setenv("WINEDEBUG", "-all", 0);
	}
	nrun = o->linkargv_len + 2 + (wine ? 1 : 0);
	runargv = xmalloc((size_t)nrun * sizeof(char*));
	i = 0;
	if (wine)
		runargv[i++] = wine;
	runargv[i++] = prog;
	{
		int j;

		for (j = 0; j < o->linkargv_len; j++)
			runargv[i++] = o->linkargv[j];
	}
	runargv[i] = NULL;
	if (o->verbose) {
		fprintf(stderr, "+");
		for (i = 0; runargv[i]; i++)
			fprintf(stderr, " %s", runargv[i]);
		fprintf(stderr, "\n");
	}
	st = host_spawn_wait(runargv);
	free(runargv);
	if (st < 0) {
		fprintf(stderr, "modc: failed to run %s: %s\n", prog, strerror(errno));
		cleanup_tmpdir(dir);
		return 1;
	}
	cleanup_tmpdir(dir);
	return st;
}

// modc run: build, execute, delete (optional -- args).
int
cmd_run(Compiler* c, CliOpts* o, int argc, char** argv) {
	int i, r;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 0);
	if (r < 0) {
		usage("run");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	ensure_build_root(o);
	if (o->files_len != 1) {
		fprintf(stderr, "modc run: at most one input path (file or directory)\n");
		return 1;
	}
	if (o->verbose)
		fprintf(stderr, "running %s\n", o->files[0]);
	return build_and_run_root(c, o, o->files[0]);
}

// modc test: discover and run *_test.mc.
int
cmd_test(Compiler* c, CliOpts* o, int argc, char** argv) {
	char** tests;
	int i, r, ntests, failed, st;
	const char* root;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 0);
	if (r < 0) {
		usage("test");
		return 0;
	}
	if (r != 0)
		return 1;
	if (o->corpus) {
		fprintf(stderr,
			"modc test: --corpus is removed; use 'modc selftest' (portable corpus)\n");
		return 1;
	}
	apply_cli(c, o);
	if (o->files_len > 1) {
		fprintf(stderr, "modc test: at most one path (directory or *_test.mc)\n");
		return 1;
	}
	root = o->files_len == 1 ? o->files[0] : ".";
	tests = NULL;
	ntests = 0;
	if (pkg_list_tests(root, &tests, &ntests)) {
		fprintf(stderr, "modc test: no tests at \"%s\" (need *_test.mc)\n", root);
		return 1;
	}
	if (ntests == 0) {
		printf("modc test: no tests in %s\n", root);
		return 0;
	}
	failed = 0;
	for (i = 0; i < ntests; i++) {
		if (i > 0) {
			/* Match selftest_reset: abandoned types/symbols are not enough —
			 * stale #pragma once / macros / strpool / static_seq from prior
			 * roots can change later codegen under the same cache keys. */
			reset_comp_state(c);
			pp_clear_macros(c);
			pp_clear_once(c);
			pp_init(c);
			c->unit.static_seq = 0;
			c->unit.strpool_len = 0;
			c->unit.src_files_len = 0;
			c->paths.cli_defs_len = 0;
		}
		if (o->verbose)
			fprintf(stderr, "testing %s\n", tests[i]);
		st = build_and_run_root(c, o, tests[i]);
		if (st == 0)
			printf("ok\t%s\n", tests[i]);
		else {
			printf("FAIL\t%s\t(exit %d)\n", tests[i], st);
			failed = 1;
		}
		free(tests[i]);
	}
	free(tests);
	return failed ? 1 : 0;
}

static int
doc_is_synth_tag(const char* tag) {
	if (tag == NULL || tag[0] == 0)
		return 0;
	return strncmp(tag, "__Ranged", 8) == 0 || strncmp(tag, "__Tuple", 7) == 0;
}

// Format a type name into buf for modc doc output (includes const / const?).
static void
fmt_type(Type* t, char* buf, size_t n) {
	char inner[256];
	Field* f;
	size_t pos;
	int first;
	const char* qual;

	if (t == NULL) {
		snprintf(buf, n, "?");
		return;
	}
	qual = "";
	if (t->is_poly)
		qual = "const? ";
	else if (t->is_readonly)
		qual = "const ";
	if (t->is_ranged) {
		fmt_type(t->base, inner, sizeof(inner));
		snprintf(buf, n, "%s%s[..]", qual, inner);
		return;
	}
	/* Multi-return: (T0, T1, …) instead of opaque __TupleN. */
	if (t->is_tuple) {
		if (n == 0)
			return;
		buf[0] = '(';
		pos = 1;
		first = 1;
		for (f = t->fields; f; f = f->next) {
			size_t plen;

			fmt_type(f->type, inner, sizeof(inner));
			plen = strlen(inner);
			if (!first) {
				if (pos + 2 >= n)
					break;
				buf[pos++] = ',';
				buf[pos++] = ' ';
			}
			first = 0;
			if (pos + plen >= n)
				break;
			memcpy(buf + pos, inner, plen);
			pos += plen;
		}
		if (pos + 1 < n) {
			buf[pos++] = ')';
			buf[pos] = 0;
		} else
			buf[n - 1] = 0;
		return;
	}
	if (t->kind == TyPtr) {
		fmt_type(t->base, inner, sizeof(inner));
		snprintf(buf, n, "%s%s*", qual, inner);
		return;
	}
	if (t->kind == TyArray) {
		fmt_type(t->base, inner, sizeof(inner));
		if (t->len >= 0)
			snprintf(buf, n, "%s%s[%" PRId64 "]", qual, inner, (int64_t)t->len);
		else
			snprintf(buf, n, "%s%s[]", qual, inner);
		return;
	}
	if ((t->kind == TyStruct || t->kind == TyUnion || t->kind == TyEnum) && t->tag &&
	    t->tag[0] && !doc_is_synth_tag(t->tag)) {
		snprintf(buf, n, "%s%s", qual, t->tag);
		return;
	}
	snprintf(buf, n, "%s%s", qual, type_name(t));
}

/* Print one [[attr]] with optional dim cyan when stdout is a TTY. */
static void
doc_print_attr(const char* attr) {
	color_span(stdout, COL_DIM COL_CYAN, attr);
}

/* Print [[retains]] / [[releases]] / [[region]] / [[borrows]] for param i. */
static void
doc_print_param_attrs(Type* ft, int i) {
	int any = 0;

	if (ft == NULL || i < 0 || i >= ft->params_len)
		return;
	if (ft->param_retains && ft->param_retains[i]) {
		doc_print_attr("[[retains]]");
		any = 1;
	}
	if (ft->param_releases && ft->param_releases[i]) {
		if (any)
			printf(" ");
		doc_print_attr("[[releases]]");
		any = 1;
	}
	if (ft->param_region && ft->param_region[i]) {
		if (any)
			printf(" ");
		doc_print_attr("[[region]]");
		any = 1;
	}
	if (ft->param_borrows && ft->param_borrows[i]) {
		if (any)
			printf(" ");
		doc_print_attr("[[borrows]]");
		any = 1;
	}
	if (any)
		printf(" ");
}

static void
doc_print_param(Type* ft, int i) {
	char arg[256];

	if (ft == NULL || i < 0 || i >= ft->params_len)
		return;
	doc_print_param_attrs(ft, i);
	fmt_type(ft->params[i], arg, sizeof(arg));
	if (ft->param_names && ft->param_names[i] && ft->param_names[i][0])
		printf("%s %s", arg, ft->param_names[i]);
	else
		printf("%s", arg);
}

static int
doc_is_builtin_pool(Compiler* c, Type* t) {
	return c && c->types.type_pool && t == c->types.type_pool;
}

static const char*
doc_aggr_kw(Type* t) {
	if (t == NULL)
		return "struct";
	if (t->kind == TyUnion)
		return "union";
	if (t->kind == TyEnum)
		return "enum";
	return "struct";
}

/* True if this typedef only restates a named tag (already printed as SkTag). */
static int
doc_typedef_redundant(Symbol* s) {
	Type* t;

	if (s == NULL || s->name == NULL)
		return 1;
	t = s->type;
	if (t == NULL)
		return 0;
	if ((t->kind == TyStruct || t->kind == TyUnion || t->kind == TyEnum) && t->tag &&
	    strcmp(t->tag, s->name) == 0)
		return 1;
	return 0;
}

/* Named enum/struct/union body members for doc (enum constants by value). */
static void
doc_print_enum_body(Compiler* c, Type* et) {
	Symbol* s;
	Symbol** cons;
	int i, n, cap;

	cons = NULL;
	n = 0;
	cap = 0;
	for (s = c->syms.symbols; s; s = s->next) {
		if (s->kind != SkEnumCon || s->type != et)
			continue;
		if (s->header || s->dead || s->storage == StStatic)
			continue;
		if (n >= cap) {
			cap = cap ? cap * 2 : 8;
			cons = xrealloc(cons, (size_t)cap * sizeof(Symbol*));
		}
		cons[n++] = s;
	}
	for (i = 0; i < n; i++) {
		int j;
		for (j = i + 1; j < n; j++) {
			if (cons[j]->int_val < cons[i]->int_val ||
			    (cons[j]->int_val == cons[i]->int_val &&
			     strcmp(cons[j]->name ? cons[j]->name : "",
				    cons[i]->name ? cons[i]->name : "") < 0)) {
				Symbol* tmp = cons[i];
				cons[i] = cons[j];
				cons[j] = tmp;
			}
		}
	}
	for (i = 0; i < n; i++) {
		printf("\t%s = %" PRId64 "%s\n", cons[i]->name ? cons[i]->name : "?",
		       (int64_t)cons[i]->int_val, i + 1 < n ? "," : "");
	}
	free(cons);
}

static void
doc_print_field(Field* f) {
	char ty[256];

	if (f == NULL)
		return;
	printf("\t");
	if (f->pkg_private)
		printf("static ");
	/* Anonymous embed: "Widget;" */
	if ((f->name == NULL || f->name[0] == 0) && f->type &&
	    (f->type->kind == TyStruct || f->type->kind == TyUnion || f->type->kind == TyEnum) &&
	    f->type->tag && f->type->tag[0] && !doc_is_synth_tag(f->type->tag)) {
		printf("%s;\n", f->type->tag);
		return;
	}
	fmt_type(f->type, ty, sizeof(ty));
	if (f->name && f->name[0])
		printf("%s %s;\n", ty, f->name);
	else
		printf("%s;\n", ty);
}

static void
doc_print_tag(Compiler* c, Symbol* s) {
	Type* t;
	Field* f;

	t = s->type;
	if (t == NULL || !t->complete || t->pkg_private)
		return;
	if (t->is_ranged || t->is_tuple || doc_is_synth_tag(t->tag) || doc_is_builtin_pool(c, t))
		return;
	if (t->kind != TyStruct && t->kind != TyUnion && t->kind != TyEnum)
		return;
	if (t->tag == NULL || t->tag[0] == 0)
		return;
	printf("%s %s {\n", doc_aggr_kw(t), t->tag);
	if (t->kind == TyEnum)
		doc_print_enum_body(c, t);
	else {
		for (f = t->fields; f; f = f->next)
			doc_print_field(f);
	}
	printf("}\n");
	if (s->doc && s->doc[0])
		printf("%s\n", s->doc);
}

static void
doc_print_typedef(Compiler* c, Symbol* s) {
	Type* t;
	char ret[256];

	(void)c;
	if (doc_typedef_redundant(s))
		return;
	t = s->type;
	if (doc_is_builtin_pool(c, t) ||
	    (s->name && strcmp(s->name, "Pool") == 0 && c->types.type_pool))
		return;
	fmt_type(t, ret, sizeof(ret));
	printf("typedef %s %s\n", ret, s->name);
	if (s->doc && s->doc[0])
		printf("%s\n", s->doc);
}

static void
doc_print_anon_enumcon(Symbol* s) {
	Type* t;

	t = s->type;
	/* Named enums are printed via their SkTag body. */
	if (t && t->kind == TyEnum && t->tag && t->tag[0] && !doc_is_synth_tag(t->tag))
		return;
	printf("enum { %s = %" PRId64 " }\n", s->name, (int64_t)s->int_val);
	if (s->doc && s->doc[0])
		printf("%s\n", s->doc);
}

// Print one exported symbol and its doc comment.
static void
print_sym_doc(Compiler* c, Symbol* s) {
	Type* t;
	char ret[256], arg[256];
	int i;

	if (s == NULL || s->name == NULL)
		return;
	if (s->kind == SkTag) {
		doc_print_tag(c, s);
		return;
	}
	if (s->kind == SkTypedef) {
		doc_print_typedef(c, s);
		return;
	}
	if (s->kind == SkEnumCon) {
		doc_print_anon_enumcon(s);
		return;
	}
	t = s->type;
	if (s->kind == SkFunc && t && t->kind == TyFunc) {
		fmt_type(t->base, ret, sizeof(ret));
		if (s->is_overload)
			printf("overload ");
		if (s->nodiscard) {
			doc_print_attr("[[nodiscard]]");
			printf(" ");
		}
		if (s->provides || t->provides) {
			doc_print_attr("[[provides]]");
			printf(" ");
		}
		if (s->is_method) {
			int start = 1;

			printf("(");
			if (t->params_len > 0)
				doc_print_param(t, 0);
			else {
				Type* recv = s->recv_type;
				if (recv)
					fmt_type(recv, arg, sizeof(arg));
				else
					snprintf(arg, sizeof(arg), "void*");
				printf("%s r", arg);
			}
			printf(").%s(", s->name);
			for (i = start; i < t->params_len; i++) {
				if (i > start)
					printf(", ");
				doc_print_param(t, i);
			}
			if (t->is_varargs)
				printf("%s...", t->params_len > start ? ", " : "");
			printf(") -> %s\n", ret);
		} else {
			printf("%s(", s->name);
			for (i = 0; i < t->params_len; i++) {
				if (i)
					printf(", ");
				doc_print_param(t, i);
			}
			if (t->is_varargs)
				printf("%s...", t->params_len ? ", " : "");
			printf(") -> %s\n", ret);
		}
	} else {
		fmt_type(t, ret, sizeof(ret));
		printf("%s %s\n", ret, s->name);
	}
	if (s->doc && s->doc[0])
		printf("%s\n", s->doc);
}

/* Like doc_sym_exported, but needs Compiler for builtin Pool filtering. */
static int
doc_sym_exported_c(Compiler* c, Symbol* s) {
	if (s == NULL || s->name == NULL || s->name[0] == 0)
		return 0;
	if (s->header || s->hidden || s->dead || s->block != 0)
		return 0;
	if (s->storage == StStatic || s->storage == StLocal || s->storage == StParam)
		return 0;
	if (s->kind != SkFunc && s->kind != SkVar && s->kind != SkTypedef &&
	    s->kind != SkTag && s->kind != SkEnumCon)
		return 0;
	/* Compiler builtins lower to these; not package API. */
	if (strncmp(s->name, "__modc_", 7) == 0)
		return 0;
	if (doc_is_synth_tag(s->name))
		return 0;
	if (s->kind == SkTag) {
		Type* t = s->type;
		if (t == NULL || !t->complete || t->pkg_private)
			return 0;
		if (t->is_ranged || t->is_tuple || doc_is_synth_tag(t->tag))
			return 0;
		if (doc_is_builtin_pool(c, t))
			return 0;
	}
	if (s->kind == SkTypedef) {
		if (doc_typedef_redundant(s))
			return 0;
		if (doc_is_builtin_pool(c, s->type) ||
		    (strcmp(s->name, "Pool") == 0 && c && c->types.type_pool))
			return 0;
	}
	return 1;
}

/* Absolute package root for a doc load path (file or directory). */
static int
doc_resolve_pkg_root(const char* load, char* out, size_t n) {
	char path[HOST_PATH_MAX], root[HOST_PATH_MAX];
	HostDir* d;
	const char* name;
	size_t nl;

	if (load == NULL || load[0] == 0)
		load = ".";
	root[0] = 0;
	if (host_is_file(load)) {
		pkg_file_root(load, root, sizeof(root));
	} else {
		snprintf(path, sizeof(path), "%s/mod.mc", load);
		if (host_is_file(path)) {
			pkg_file_root(path, root, sizeof(root));
		} else {
			d = host_opendir(load);
			if (d) {
				while ((name = host_readdir(d)) != NULL) {
					nl = strlen(name);
					if (nl < 3 || strcmp(name + nl - 3, ".mc") != 0)
						continue;
					if (pkg_is_test_src(name) || pkg_is_iface_src(name))
						continue;
					snprintf(path, sizeof(path), "%s/%s", load, name);
					if (!host_is_file(path))
						continue;
					pkg_file_root(path, root, sizeof(root));
					break;
				}
				host_closedir(d);
			}
			if (root[0] == 0)
				snprintf(root, sizeof(root), "%s", load);
		}
	}
	if (host_abspath(root, out, n) != 0) {
		if (snprintf(out, n, "%s", root) >= (int)n)
			return -1;
	}
	host_path_slashify(out);
	return 0;
}

/* True if s was defined in pkg_root_abs (package identity directory or file). */
static int
doc_sym_in_root(Symbol* s, const char* pkg_root_abs) {
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX];

	if (s == NULL || s->home == NULL || s->home[0] == 0 || pkg_root_abs == NULL)
		return 0;
	pkg_file_root(s->home, root, sizeof(root));
	if (host_abspath(root, abs, sizeof(abs)) != 0)
		snprintf(abs, sizeof(abs), "%s", root);
	host_path_slashify(abs);
	return strcmp(abs, pkg_root_abs) == 0;
}

/* True when dir is the process cwd (hint can omit -M). */
static int
doc_dir_is_cwd(const char* dir) {
	char abs[HOST_PATH_MAX], cwd[HOST_PATH_MAX];

	if (dir == NULL)
		return 0;
	if (strcmp(dir, ".") == 0)
		return 1;
	if (host_abspath(dir, abs, sizeof(abs)) != 0)
		return 0;
	if (host_abspath(".", cwd, sizeof(cwd)) != 0)
		return 0;
	host_path_slashify(abs);
	host_path_slashify(cwd);
	return strcmp(abs, cwd) == 0;
}

/* List immediate subdirectories of dir that look like packages. */
static int
doc_print_local_pkgs(const char* dir) {
	HostDir* d;
	HostDir* sub;
	const char* name;
	const char* sn;
	char path[HOST_PATH_MAX], child[HOST_PATH_MAX];
	char** names;
	int i, n, cap;
	size_t nl;

	if (dir == NULL || !host_is_dir(dir))
		return 0;
	d = host_opendir(dir);
	if (d == NULL)
		return 0;
	names = NULL;
	n = 0;
	cap = 0;
	while ((name = host_readdir(d)) != NULL) {
		if (name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, name);
		if (!host_is_dir(path))
			continue;
		sub = host_opendir(path);
		if (sub == NULL)
			continue;
		while ((sn = host_readdir(sub)) != NULL) {
			if (sn[0] == '.')
				continue;
			nl = strlen(sn);
			if (nl < 3 || strcmp(sn + nl - 3, ".mc") != 0)
				continue;
			if (pkg_is_iface_src(sn))
				continue;
			snprintf(child, sizeof(child), "%s/%s", path, sn);
			if (!host_is_file(child))
				continue;
			if (n >= cap) {
				cap = cap ? cap * 2 : 8;
				names = xrealloc(names, (size_t)cap * sizeof(char*));
			}
			names[n++] = xstrdup(name);
			break;
		}
		host_closedir(sub);
	}
	host_closedir(d);
	if (n == 0) {
		free(names);
		return 0;
	}
	/* Stable-ish order for agents/humans. */
	for (i = 0; i < n; i++) {
		int j;
		for (j = i + 1; j < n; j++) {
			if (strcmp(names[j], names[i]) < 0) {
				char* t = names[i];
				names[i] = names[j];
				names[j] = t;
			}
		}
	}
	printf("\nLocal packages:\n");
	for (i = 0; i < n; i++)
		printf("  %s\n", names[i]);
	if (doc_dir_is_cwd(dir))
		printf("Document one with: modc doc <pkg>\n");
	else
		printf("Document one with: modc doc -M %s <pkg>\n", dir);
	for (i = 0; i < n; i++)
		free(names[i]);
	free(names);
	return n;
}

/* App trees (jem-style): sibling packages resolve without an explicit -M . */
static void
doc_ensure_cwd_pkgpath(Compiler* c) {
	char abs[HOST_PATH_MAX], have[HOST_PATH_MAX];
	int i;

	if (c == NULL)
		return;
	if (host_abspath(".", abs, sizeof(abs)) != 0)
		snprintf(abs, sizeof(abs), ".");
	host_path_slashify(abs);
	for (i = 0; i < c->paths.pkgpaths_len; i++) {
		if (host_abspath(c->paths.pkgpaths[i], have, sizeof(have)) != 0)
			snprintf(have, sizeof(have), "%s", c->paths.pkgpaths[i]);
		host_path_slashify(have);
		if (strcmp(have, abs) == 0)
			return;
	}
	pkg_add_search_path(c, ".");
}

static void
doc_print_topics_hint(void) {
	printf("\nLanguage topics: modc doc topics\n");
}

/* Group order: types → vars → funcs/methods. */
static int
doc_sym_group(Symbol* s) {
	if (s == NULL)
		return 3;
	if (s->kind == SkTag || s->kind == SkTypedef || s->kind == SkEnumCon)
		return 0;
	if (s->kind == SkVar)
		return 1;
	return 2;
}

/* Sort key: methods as Recv.name so same-receiver methods cluster. */
static void
doc_sym_sort_key(Symbol* s, char* buf, size_t n) {
	if (s == NULL || s->name == NULL) {
		snprintf(buf, n, "");
		return;
	}
	if (s->is_method && s->recv_tag && s->recv_tag[0]) {
		snprintf(buf, n, "%s.%s", s->recv_tag, s->name);
		return;
	}
	snprintf(buf, n, "%s", s->name);
}

static int
doc_sym_cmp(const void* a, const void* b) {
	Symbol* sa = *(Symbol* const*)a;
	Symbol* sb = *(Symbol* const*)b;
	char ka[256], kb[256];
	int ga, gb, c;

	ga = doc_sym_group(sa);
	gb = doc_sym_group(sb);
	if (ga != gb)
		return ga - gb;
	doc_sym_sort_key(sa, ka, sizeof(ka));
	doc_sym_sort_key(sb, kb, sizeof(kb));
	c = strcmp(ka, kb);
	if (c != 0)
		return c;
	/* Stable-ish tie-break for overloads: parameter count then address. */
	if (sa->type && sb->type && sa->type->kind == TyFunc && sb->type->kind == TyFunc) {
		if (sa->type->params_len != sb->type->params_len)
			return sa->type->params_len - sb->type->params_len;
	}
	if (sa < sb)
		return -1;
	if (sa > sb)
		return 1;
	return 0;
}

/* Collect exported package symbols (same filter as printed docs). */
static int
doc_collect_package(Compiler* c, const char* pkg_root_abs, const char* want,
		    Symbol*** out) {
	Symbol* s;
	Symbol** list;
	int n, cap;

	list = NULL;
	n = 0;
	cap = 0;
	for (s = c->syms.symbols; s; s = s->next) {
		if (!doc_sym_exported_c(c, s))
			continue;
		if (!doc_sym_in_root(s, pkg_root_abs))
			continue;
		if (want && strcmp(s->name, want) != 0)
			continue;
		/* Skip named-enum constants; they appear in the SkTag body. */
		if (s->kind == SkEnumCon) {
			Type* t = s->type;
			if (t && t->kind == TyEnum && t->tag && t->tag[0] &&
			    !doc_is_synth_tag(t->tag))
				continue;
		}
		if (n >= cap) {
			cap = cap ? cap * 2 : 16;
			list = xrealloc(list, (size_t)cap * sizeof(Symbol*));
		}
		list[n++] = s;
	}
	if (n > 1)
		qsort(list, (size_t)n, sizeof(Symbol*), doc_sym_cmp);
	*out = list;
	return n;
}

/* Returns number of symbols printed. If want!=NULL, only matching names.
 * pkg_root_abs limits output to that package (required).
 * Order: types → vars → funcs/methods; each group sorted by name. */
static int
doc_print_package(Compiler* c, const char* pkg_root_abs, const char* want) {
	Symbol** list;
	int i, n;

	n = doc_collect_package(c, pkg_root_abs, want, &list);
	for (i = 0; i < n; i++) {
		if (i)
			printf("\n");
		print_sym_doc(c, list[i]);
	}
	free(list);
	return n;
}

/* Warn on exports with no doc comment. Returns undoc'd count (0 = clean). */
static int
doc_check_package(Compiler* c, const char* pkg_root_abs, const char* want) {
	Symbol** list;
	Symbol* s;
	int i, n, missing;
	Span sp;

	n = doc_collect_package(c, pkg_root_abs, want, &list);
	missing = 0;
	for (i = 0; i < n; i++) {
		s = list[i];
		if (s->doc && s->doc[0])
			continue;
		missing++;
		sp = s->span;
		if (sp.file && sp.file[0])
			fprintf(stderr, "%s:%d:%d: ", sp.file, sp.line > 0 ? sp.line : 1,
				sp.col > 0 ? sp.col : 1);
		color_sev(stderr, "warning");
		fprintf(stderr, "undoc'd export '%s'\n", s->name);
	}
	free(list);
	return missing;
}

// True if dir contains at least one .mc file.
static int
dir_has_mc(const char* dir) {
	HostDir* d;
	const char* name;

	d = host_opendir(dir ? dir : ".");
	if (d == NULL)
		return 0;
	while ((name = host_readdir(d)) != NULL) {
		size_t n = strlen(name);
		if (n > 3 && strcmp(name + n - 3, ".mc") == 0 && !pkg_is_test_src(name) &&
		    !pkg_is_iface_src(name)) {
			host_closedir(d);
			return 1;
		}
	}
	host_closedir(d);
	return 0;
}

// True if path exists as file or directory.
static int
path_exists(const char* path) {
	return host_exists(path);
}

// modc doc: print package API docs for a target.
int
cmd_doc(Compiler* c, CliOpts* o, int argc, char** argv) {
	char resolved[1024];
	char pkg_root[HOST_PATH_MAX];
	char *target, *dot;
	char pkgbuf[256], symbuf[256];
	int i, r, n;
	const char* load;
	const char* sym;
	int bare_name;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 0);
	if (r < 0) {
		usage("doc");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	doc_ensure_cwd_pkgpath(c);
	o->check_only = 1;
	c->opt.check_only = 1;
	if (o->files_len > 1) {
		fprintf(stderr, "modc doc: at most one target\n");
		return 1;
	}
	target = o->files_len == 1 ? o->files[0] : NULL;
	sym = NULL;
	load = ".";
	bare_name = 0;
	pkgbuf[0] = 0;
	symbuf[0] = 0;
	/* Language topics win over package names (modc doc defer). */
	if (target && (strcmp(target, "topics") == 0 || strcmp(target, "lang") == 0)) {
		langdoc_print_index();
		return 0;
	}
	if (target && langdoc_is_topic(target)) {
		langdoc_print_topic(target);
		return 0;
	}
	if (target == NULL) {
		load = ".";
	} else if (path_exists(target)) {
		load = target;
	} else if (!host_path_has_sep(target) && (dot = strrchr(target, '.')) != NULL &&
		   strcmp(dot, ".mc") != 0) {
		snprintf(pkgbuf, sizeof(pkgbuf), "%.*s", (int)(dot - target), target);
		snprintf(symbuf, sizeof(symbuf), "%s", dot + 1);
		if (pkg_resolve_spec(c, pkgbuf, resolved, sizeof(resolved))) {
			fprintf(stderr, "modc doc: cannot find package \"%s\"\n", pkgbuf);
			return 1;
		}
		load = resolved;
		sym = symbuf;
	} else if (!host_path_has_sep(target)) {
		bare_name = 1;
		snprintf(symbuf, sizeof(symbuf), "%s", target);
		/* Prefer local symbol in "."; else package list. */
		if (dir_has_mc(".")) {
			if (compile_file(c, ".", NULL) == 0) {
				if (doc_resolve_pkg_root(".", pkg_root, sizeof(pkg_root)) != 0) {
					fprintf(stderr, "modc doc: cannot resolve package root\n");
					return 1;
				}
				{
					Symbol** tmp;
					int m = doc_collect_package(c, pkg_root, symbuf, &tmp);
					free(tmp);
					if (m > 0) {
						if (o->doc_check)
							return doc_check_package(c, pkg_root, symbuf) > 0 ? 1 : 0;
						doc_print_package(c, pkg_root, symbuf);
						return 0;
					}
				}
				reset_comp_state(c);
				c->opt.check_only = 1;
			} else
				return 1;
		}
		if (pkg_resolve_spec(c, symbuf, resolved, sizeof(resolved)) == 0) {
			load = resolved;
			sym = NULL;
		} else {
			fprintf(stderr, "modc doc: unknown symbol or package \"%s\"\n", symbuf);
			return 1;
		}
	} else {
		fprintf(stderr, "modc doc: cannot find \"%s\"\n", target);
		return 1;
	}
	(void)bare_name;
	if (o->verbose)
		fprintf(stderr, "doc %s\n", load);
	if (compile_file(c, load, NULL))
		return 1;
	if (doc_resolve_pkg_root(load, pkg_root, sizeof(pkg_root)) != 0) {
		fprintf(stderr, "modc doc: cannot resolve package root for %s\n", load);
		return 1;
	}
	if (o->doc_check) {
		if (sym) {
			Symbol** tmp;
			if (doc_collect_package(c, pkg_root, sym, &tmp) == 0) {
				free(tmp);
				fprintf(stderr, "modc doc: unknown symbol \"%s\"\n", sym);
				return 1;
			}
			free(tmp);
		}
		n = doc_check_package(c, pkg_root, sym);
		return n > 0 ? 1 : 0;
	}
	if (sym) {
		n = doc_print_package(c, pkg_root, sym);
		if (n == 0) {
			fprintf(stderr, "modc doc: unknown symbol \"%s\"\n", sym);
			return 1;
		}
		return 0;
	}
	doc_print_package(c, pkg_root, NULL);
	/* Multi-package app roots: point at sibling packages instead of drowning. */
	if (host_is_dir(load))
		doc_print_local_pkgs(load);
	doc_print_topics_hint();
	return 0;
}

// Rewrite one .mc file in place with modc format.
static int
format_one(Compiler* c, const char* path) {
	char *text, *out;
	char resolved[HOST_PATH_MAX];
	const char* writepath;
	size_t n;

	text = read_file(path, &n);
	if (text == NULL) {
		fprintf(stderr, "modc format: cannot read %s: %s\n", path, strerror(errno));
		return 1;
	}
	c->lex.tokens = NULL;
	c->lex.tokens_len = 0;
	c->lex.tokens_cap = 0;
	c->lex.pos = 0;
	c->diag.error_count = 0;
	c->diag.fatal = 0;
	c->lex.pending_doc = NULL;
	c->lex.keep_comments = 1;
	c->paths.infile = xstrdup(path);
	lex_file(c, path, text, 1);
	free(text);
	if (c->diag.error_count) {
		c->lex.keep_comments = 0;
		return 1;
	}
	out = fmt_source(c);
	c->lex.keep_comments = 0;
	if (out == NULL)
		return 1;
	writepath = host_realpath(path, resolved, sizeof(resolved)) == 0 ? resolved : path;
	if (host_write_atomic(writepath, out, strlen(out)) != 0) {
		fprintf(stderr, "modc format: cannot write %s: %s\n", path, strerror(errno));
		free(out);
		return 1;
	}
	free(out);
	return 0;
}

/* True if basename is a package build product (.pkg.mc / .pkg.o / .pkg.meta). */
static int
is_pkg_artifact_name(const char* name) {
	size_t n;

	if (name == NULL)
		return 0;
	if (strcmp(name, ".pkg.mc") == 0 || strcmp(name, ".pkg.o") == 0 ||
	    strcmp(name, ".pkg.meta") == 0)
		return 1;
	n = strlen(name);
	if (n >= 7 && strcmp(name + n - 7, ".pkg.mc") == 0)
		return 1;
	if (n >= 6 && strcmp(name + n - 6, ".pkg.o") == 0)
		return 1;
	if (n >= 9 && strcmp(name + n - 9, ".pkg.meta") == 0)
		return 1;
	return 0;
}

/* Recursively delete .pkg.* artifacts under dir. Returns count removed. */
static int
clean_pkg_artifacts(const char* dir, int verbose) {
	HostDir* d;
	const char* name;
	char path[HOST_PATH_MAX];
	int n;

	n = 0;
	d = host_opendir(dir);
	if (d == NULL)
		return 0;
	while ((name = host_readdir(d)) != NULL) {
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, name);
		if (host_is_symlink(path))
			continue;
		if (host_is_dir(path)) {
			if (strcmp(name, ".git") == 0 || strcmp(name, "build") == 0)
				continue;
			n += clean_pkg_artifacts(path, verbose);
			continue;
		}
		if (!is_pkg_artifact_name(name))
			continue;
		if (host_unlink(path) == 0) {
			n++;
			if (verbose)
				fprintf(stderr, "modc clean: removed %s\n", path);
		}
	}
	host_closedir(d);
	return n;
}

/* Default developer bin dir: $MODC_BIN, else ~/bin (POSIX) or
 * %LOCALAPPDATA%/modc/bin (Windows). */
static int
install_default_bindir(char* out, size_t n) {
	const char* e;

	e = getenv("MODC_BIN");
	if (e && e[0]) {
		if (snprintf(out, n, "%s", e) >= (int)n)
			return -1;
		return 0;
	}
#ifdef _WIN32
	e = getenv("LOCALAPPDATA");
	if (e && e[0]) {
		if (snprintf(out, n, "%s/modc/bin", e) >= (int)n)
			return -1;
		return 0;
	}
	e = getenv("USERPROFILE");
	if (e && e[0]) {
		if (snprintf(out, n, "%s/modc/bin", e) >= (int)n)
			return -1;
		return 0;
	}
#else
	e = getenv("HOME");
	if (e && e[0]) {
		if (snprintf(out, n, "%s/bin", e) >= (int)n)
			return -1;
		return 0;
	}
#endif
	return -1;
}

/* mkdir -p for every parent of path (file or dir). */
static int
install_mkdir_parents(const char* path) {
	char buf[HOST_PATH_MAX];
	size_t i, n, start;

	if (path == NULL || path[0] == 0)
		return -1;
	if (snprintf(buf, sizeof(buf), "%s", path) >= (int)sizeof(buf))
		return -1;
	host_path_slashify(buf);
	n = strlen(buf);
	start = 1;
#ifdef _WIN32
	if (n >= 3 && buf[1] == ':' && buf[2] == '/')
		start = 3;
#endif
	for (i = start; i < n; i++) {
		if (buf[i] != '/')
			continue;
		buf[i] = 0;
		if (buf[0] && host_mkdir(buf) != 0 && !host_is_dir(buf))
			return -1;
		buf[i] = '/';
	}
	return 0;
}

/* Basename for the installed binary (with .exe on Windows). */
static char*
install_bin_name(const char* path, int target) {
	char* def;
	const char* base;
	char* name;

	def = default_out_name(path, target);
	base = host_path_basename(def);
	name = xstrdup(base);
	free(def);
	return name;
}

// modc install: build and publish an executable into a developer bin dir.
int
cmd_install(Compiler* c, CliOpts* o, int argc, char** argv) {
	char dir[HOST_PATH_MAX], bindir[HOST_PATH_MAX];
	char dest[HOST_PATH_MAX], tmp[HOST_PATH_MAX];
	char* name;
	const char* outspec;
	int i, r;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 1);
	if (r < 0) {
		usage("install");
		return 0;
	}
	if (r != 0)
		return 1;
	apply_cli(c, o);
	ensure_build_root(o);
	if (o->files_len != 1) {
		fprintf(stderr, "modc install: at most one input path (file or directory)\n");
		return 1;
	}
	if (o->target != TargetHost) {
		fprintf(stderr,
			"modc install: only --target=host is supported "
			"(use 'modc build --target=… -o …' for cross builds)\n");
		return 1;
	}

	outspec = o->output;
	name = NULL;
	if (outspec && (host_path_is_abs(outspec) || host_path_has_sep(outspec))) {
		if (snprintf(dest, sizeof(dest), "%s", outspec) >= (int)sizeof(dest)) {
			fprintf(stderr, "modc install: -o path too long\n");
			return 1;
		}
	} else {
		if (install_default_bindir(bindir, sizeof(bindir)) != 0) {
			fprintf(stderr,
				"modc install: set MODC_BIN to a writable directory"
#ifdef _WIN32
				" (or LOCALAPPDATA / USERPROFILE)\n");
#else
				" (or HOME)\n");
#endif
			return 1;
		}
		if (outspec)
			name = xstrdup(outspec);
		else
			name = install_bin_name(o->files[0], o->target);
		if (snprintf(dest, sizeof(dest), "%s/%s", bindir, name) >= (int)sizeof(dest)) {
			free(name);
			fprintf(stderr, "modc install: install path too long\n");
			return 1;
		}
		free(name);
		name = NULL;
	}
	host_path_slashify(dest);
	if (install_mkdir_parents(dest) != 0) {
		fprintf(stderr, "modc install: cannot create directory for %s\n", dest);
		return 1;
	}
	if (snprintf(tmp, sizeof(tmp), "%s.modc-tmp", dest) >= (int)sizeof(tmp)) {
		fprintf(stderr, "modc install: temp path too long\n");
		return 1;
	}
	if (o->verbose)
		fprintf(stderr, "installing %s -> %s\n", o->files[0], dest);
	if (host_mkdtemp(dir, sizeof(dir), "modc-build") != 0) {
		fprintf(stderr, "modc: cannot create temp dir: %s\n", strerror(errno));
		return 1;
	}
	r = compile_link_exe(c, o, o->files[0], dir, tmp);
	cleanup_tmpdir(dir);
	if (r != 0) {
		(void)host_unlink(tmp);
		return r;
	}
	if (host_publish_file(tmp, dest) != 0) {
		fprintf(stderr, "modc install: cannot write %s: %s\n", dest, strerror(errno));
		(void)host_unlink(tmp);
		return 1;
	}
#ifndef _WIN32
	if (chmod(dest, 0755) != 0) {
		fprintf(stderr, "modc install: chmod %s: %s\n", dest, strerror(errno));
		return 1;
	}
#endif
	fprintf(stderr, "installed %s\n", dest);
	return 0;
}

// modc clean: remove .pkg.* artifacts and any leftover .modc-cache.
int
cmd_clean(CliOpts* o, int argc, char** argv) {
	char crooot[HOST_PATH_MAX], projroot[HOST_PATH_MAX];
	const char* entry;
	int i, n;

	entry = ".";
	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage("clean");
			return 0;
		}
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			o->verbose = 1;
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "modc clean: unknown option %s\n", argv[i]);
			usage("clean");
			return 1;
		}
		if (entry != NULL && strcmp(entry, ".") != 0) {
			fprintf(stderr, "modc clean: at most one path\n");
			return 1;
		}
		entry = argv[i];
	}
	if (cache_project_root(entry, projroot, sizeof(projroot)) != 0) {
		fprintf(stderr, "modc clean: cannot resolve project root\n");
		return 1;
	}
	n = clean_pkg_artifacts(projroot, o->verbose);
	if (cache_root_for(entry, crooot, sizeof(crooot)) == 0 && host_is_dir(crooot)) {
		if (host_rmtree(crooot) != 0) {
			fprintf(stderr, "modc clean: cannot remove %s\n", crooot);
			return 1;
		}
		if (o->verbose)
			fprintf(stderr, "modc clean: removed %s\n", crooot);
		n++;
	}
	if (o->verbose && n == 0)
		fprintf(stderr, "modc clean: nothing to remove\n");
	return 0;
}

// modc format: format each listed .mc path.
int
cmd_format(Compiler* c, CliOpts* o, int argc, char** argv) {
	int i, err;
	const char* path;

	(void)o;
	if (argc < 3) {
		usage("format");
		return 1;
	}
	err = 0;
	for (i = 2; i < argc; i++) {
		path = argv[i];
		if (strcmp(path, "-h") == 0 || strcmp(path, "--help") == 0) {
			usage("format");
			return 0;
		}
		if (path[0] == '-') {
			fprintf(stderr, "modc format: unknown option %s\n", path);
			usage("format");
			return 1;
		}
		if (format_one(c, path))
			err = 1;
	}
	return err;
}

