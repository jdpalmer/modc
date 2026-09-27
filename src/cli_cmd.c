/*
 * CLI subcommands: check, emit, build, run, test, doc, format, clean.
 */
#include "cli.h"
#ifndef _WIN32
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
	c->check_only = 1;
	if (o->files_len == 0) {
		fprintf(stderr, "modc check: no input files\n");
		return 1;
	}
	err = 0;
	for (r = 0; r < o->files_len; r++) {
		if (o->verbose)
			fprintf(stderr, "checking %s\n", o->files[r]);
		if (compile_file(c, o->files[r], NULL))
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

	c->c_libs_len = 0;
	c->frameworks_len = 0;
	c->csources_len = 0;
	if (host_mkdtemp(dir, sizeof(dir), "modc-run") != 0) {
		fprintf(stderr, "modc: cannot create temp dir: %s\n", strerror(errno));
		return 1;
	}
	use_wine = 0;
#ifndef _WIN32
	if (c->target == TargetWindows)
		use_wine = 1;
#endif
#ifdef _WIN32
	snprintf(prog, sizeof(prog), "%s/prog.exe", dir);
#else
	if (c->target == TargetWindows)
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

// modc test: discover *_test.mc or --corpus → make check.
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
		const char* make_argv[] = {"make", "check", NULL};

		if (o->files_len != 0) {
			fprintf(stderr, "modc test: --corpus does not take a path\n");
			return 1;
		}
		if (o->verbose)
			fprintf(stderr, "modc test --corpus: make check\n");
		r = host_spawn_wait(make_argv);
		if (r == -1) {
			fprintf(stderr, "modc test: failed to run make check: %s\n",
				strerror(errno));
			return 1;
		}
		return r;
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
		if (i > 0)
			reset_comp_state(c);
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

// Format a type name into buf for modc doc output.
static void
fmt_type(Type* t, char* buf, size_t n) {
	char inner[256];

	if (t == NULL) {
		snprintf(buf, n, "?");
		return;
	}
	if (t->is_ranged) {
		fmt_type(t->base, inner, sizeof(inner));
		snprintf(buf, n, "%s[..]", inner);
		return;
	}
	if (t->kind == TyPtr) {
		fmt_type(t->base, inner, sizeof(inner));
		snprintf(buf, n, "%s*", inner);
		return;
	}
	if (t->kind == TyArray) {
		fmt_type(t->base, inner, sizeof(inner));
		if (t->len >= 0)
			snprintf(buf, n, "%s[%" PRId64 "]", inner, (int64_t)t->len);
		else
			snprintf(buf, n, "%s[]", inner);
		return;
	}
	snprintf(buf, n, "%s", type_name(t));
}

// Print one exported symbol and its doc comment.
static void
print_sym_doc(Symbol* s) {
	Type* t;
	char ret[256], arg[256];
	int i;

	if (s == NULL || s->name == NULL)
		return;
	t = s->type;
	if (s->kind == SkFunc && t && t->kind == TyFunc) {
		fmt_type(t->base, ret, sizeof(ret));
		printf("%s(", s->name);
		for (i = 0; i < t->params_len; i++) {
			if (i)
				printf(", ");
			fmt_type(t->params[i], arg, sizeof(arg));
			if (t->param_names && t->param_names[i])
				printf("%s %s", arg, t->param_names[i]);
			else
				printf("%s", arg);
		}
		if (t->is_varargs)
			printf("%s...", t->params_len ? ", " : "");
		printf(") -> %s\n", ret);
	} else if (s->kind == SkTypedef) {
		fmt_type(t, ret, sizeof(ret));
		printf("typedef %s %s\n", ret, s->name);
	} else {
		fmt_type(t, ret, sizeof(ret));
		printf("%s %s\n", ret, s->name);
	}
	if (s->doc && s->doc[0])
		printf("%s\n", s->doc);
}

// True if s is a non-static API symbol for documentation.
static int
doc_sym_exported(Symbol* s) {
	if (s == NULL || s->name == NULL || s->name[0] == 0)
		return 0;
	if (s->hidden || s->dead || s->block != 0)
		return 0;
	if (s->storage == StStatic || s->storage == StLocal || s->storage == StParam)
		return 0;
	if (s->kind != SkFunc && s->kind != SkVar && s->kind != SkTypedef)
		return 0;
	return 1;
}

/* Returns number of symbols printed. If want!=NULL, only matching names. */
static int
doc_print_package(Compiler* c, const char* want) {
	Symbol* s;
	int n;

	n = 0;
	for (s = c->symbols; s; s = s->next) {
		if (!doc_sym_exported(s))
			continue;
		if (want && strcmp(s->name, want) != 0)
			continue;
		if (n)
			printf("\n");
		print_sym_doc(s);
		n++;
	}
	return n;
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
		if (n > 3 && strcmp(name + n - 3, ".mc") == 0 && !pkg_is_test_src(name)) {
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
	o->check_only = 1;
	c->check_only = 1;
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
	if (target == NULL) {
		load = ".";
	} else if (path_exists(target)) {
		load = target;
	} else if (!host_path_has_sep(target) && (dot = strrchr(target, '.')) != NULL && strcmp(dot, ".mc") != 0) {
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
				n = doc_print_package(c, symbuf);
				if (n > 0)
					return 0;
				reset_comp_state(c);
				c->check_only = 1;
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
	if (sym) {
		n = doc_print_package(c, sym);
		if (n == 0) {
			fprintf(stderr, "modc doc: unknown symbol \"%s\"\n", sym);
			return 1;
		}
		return 0;
	}
	doc_print_package(c, NULL);
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
	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = 0;
	c->pos = 0;
	c->error_count = 0;
	c->fatal = 0;
	c->pending_doc = NULL;
	c->keep_comments = 1;
	c->infile = xstrdup(path);
	lex_file(c, path, text, 1);
	free(text);
	if (c->error_count) {
		c->keep_comments = 0;
		return 1;
	}
	out = fmt_source(c);
	c->keep_comments = 0;
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

// modc clean: remove the project .modc-cache directory.
int
cmd_clean(CliOpts* o, int argc, char** argv) {
	char crooot[HOST_PATH_MAX];
	const char* entry;
	int i;

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
	if (cache_root_for(entry, crooot, sizeof(crooot)) != 0) {
		fprintf(stderr, "modc clean: cannot resolve cache root\n");
		return 1;
	}
	if (!host_is_dir(crooot) && !host_exists(crooot)) {
		if (o->verbose)
			fprintf(stderr, "modc clean: nothing to remove (%s)\n", crooot);
		return 0;
	}
	if (host_rmtree(crooot) != 0) {
		fprintf(stderr, "modc clean: cannot remove %s\n", crooot);
		return 1;
	}
	if (o->verbose)
		fprintf(stderr, "modc clean: removed %s\n", crooot);
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

