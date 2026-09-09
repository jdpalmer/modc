/*
 * CLI subcommands: check, emit, build, run, test, doc, format, clean.
 */
#include "cli.h"

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
	char qbe[512], asmpath[512];
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
		outname = default_out_name(o->files[0]);
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
	snprintf(qbe, sizeof(qbe), "%s/out.qbe", dir);
	snprintf(asmpath, sizeof(asmpath), "%s/out.s", dir);
	cleanup_tmpdir(dir, qbe, asmpath, NULL);
	free(outname);
	return r;
}

// Build a temp exe for path, spawn it, then clean up.
int
build_and_run_root(Compiler* c, CliOpts* o, const char* path) {
	char dir[HOST_PATH_MAX];
	char prog[512], qbe[512], asmpath[512];
	int i, st;
	char** runargv;
	int nrun;

	c->c_libs_len = 0;
	c->frameworks_len = 0;
	c->csources_len = 0;
	if (host_mkdtemp(dir, sizeof(dir), "modc-run") != 0) {
		fprintf(stderr, "modc: cannot create temp dir: %s\n", strerror(errno));
		return 1;
	}
#ifdef _WIN32
	snprintf(prog, sizeof(prog), "%s/prog.exe", dir);
#else
	snprintf(prog, sizeof(prog), "%s/prog", dir);
#endif
	if (compile_link_exe(c, o, path, dir, prog) != 0) {
		snprintf(qbe, sizeof(qbe), "%s/out.qbe", dir);
		snprintf(asmpath, sizeof(asmpath), "%s/out.s", dir);
		cleanup_tmpdir(dir, qbe, asmpath, NULL);
		return 1;
	}
	nrun = o->linkargv_len + 2;
	runargv = xmalloc((size_t)nrun * sizeof(char*));
	runargv[0] = prog;
	for (i = 0; i < o->linkargv_len; i++)
		runargv[i + 1] = o->linkargv[i];
	runargv[nrun - 1] = NULL;
	st = host_spawn_wait(runargv);
	free(runargv);
	snprintf(qbe, sizeof(qbe), "%s/out.qbe", dir);
	snprintf(asmpath, sizeof(asmpath), "%s/out.s", dir);
	if (st < 0) {
		fprintf(stderr, "modc: failed to run %s: %s\n", prog, strerror(errno));
		cleanup_tmpdir(dir, qbe, asmpath, prog);
		return 1;
	}
	cleanup_tmpdir(dir, qbe, asmpath, prog);
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
		if (o->files_len != 0) {
			fprintf(stderr, "modc test: --corpus does not take a path\n");
			return 1;
		}
		if (o->verbose)
			fprintf(stderr, "modc test --corpus: make check\n");
		r = host_run("make check");
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
	if (t->kind == TY_PTR) {
		fmt_type(t->base, inner, sizeof(inner));
		snprintf(buf, n, "%s*", inner);
		return;
	}
	if (t->kind == TY_ARRAY) {
		fmt_type(t->base, inner, sizeof(inner));
		if (t->len >= 0)
			snprintf(buf, n, "%s[%lld]", inner, (long long)t->len);
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
	if (s->kind == SK_FUNC && t && t->kind == TY_FUNC) {
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
	} else if (s->kind == SK_TYPEDEF) {
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
	if (s->storage == ST_STATIC || s->storage == ST_LOCAL || s->storage == ST_PARAM)
		return 0;
	if (s->kind != SK_FUNC && s->kind != SK_VAR && s->kind != SK_TYPEDEF)
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
	} else if (strchr(target, '/') == NULL && (dot = strrchr(target, '.')) != NULL && strcmp(dot, ".mc") != 0) {
		snprintf(pkgbuf, sizeof(pkgbuf), "%.*s", (int)(dot - target), target);
		snprintf(symbuf, sizeof(symbuf), "%s", dot + 1);
		if (pkg_resolve_spec(c, pkgbuf, resolved, sizeof(resolved))) {
			fprintf(stderr, "modc doc: cannot find package \"%s\"\n", pkgbuf);
			return 1;
		}
		load = resolved;
		sym = symbuf;
	} else if (strchr(target, '/') == NULL) {
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
	size_t n;
	FILE* f;

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
	f = fopen(path, "wb");
	if (f == NULL) {
		fprintf(stderr, "modc format: cannot write %s: %s\n", path, strerror(errno));
		free(out);
		return 1;
	}
	fwrite(out, 1, strlen(out), f);
	fclose(f);
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

