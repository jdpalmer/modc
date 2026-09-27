/*
 * modc selftest — portable compiler corpus (no shell / Make).
 *
 * Lanes: expect-fail (.expect), check-ok (check-ok.list), run (run.list).
 */
#include "cli.h"
#include <ctype.h>

#ifndef _WIN32
#include <unistd.h>
#endif

/* Portable line iterator: replaces strtok_r. */
static char*
next_line(char** pp) {
	char* s;
	char* e;

	if (pp == NULL || *pp == NULL || **pp == 0)
		return NULL;
	s = *pp;
	e = s;
	while (*e && *e != '\n' && *e != '\r')
		e++;
	if (*e == '\r')
		*e++ = 0;
	if (*e == '\n')
		*e++ = 0;
	else if (*e)
		*e++ = 0;
	*pp = e;
	return s;
}
static void
diag_clear(Compiler* c) {
	if (c->diag_log)
		c->diag_log[0] = 0;
	c->diag_log_len = 0;
}

static void
selftest_reset(Compiler* c) {
	reset_comp_state(c);
	pp_clear_macros(c);
	pp_clear_once(c);
	pp_init(c);
	diag_clear(c);
	c->error_count = 0;
	c->fatal = 0;
	c->cli_defs_len = 0;
	/* Fresh process semantics for mangled statics (__fN_*) and .qbe.expect. */
	c->static_seq = 0;
}

static int
file_contains(const char* hay, const char* needle) {
	return hay && needle && needle[0] && strstr(hay, needle) != NULL;
}

static int
read_file_str(const char* path, char** out, size_t* out_len) {
	size_t n;
	char* t;

	t = read_file(path, &n);
	if (t == NULL)
		return 1;
	*out = t;
	if (out_len)
		*out_len = n;
	return 0;
}

/* Expand $ROOT in-place into buf (ROOT = cwd). */
static void
expand_root(const char* in, char* buf, size_t n) {
	const char* root;
	char cwd[HOST_PATH_MAX];
	const char* p;
	size_t used = 0;

	root = getenv("MODC_SELFTEST_ROOT");
	if (root == NULL || root[0] == 0) {
		if (host_getcwd(cwd, sizeof(cwd)) != 0)
			cwd[0] = 0;
		root = cwd;
	}
	for (p = in; *p && used + 1 < n;) {
		if (p[0] == '$' && strncmp(p, "$ROOT", 5) == 0) {
			size_t rl = strlen(root);
			if (used + rl >= n)
				break;
			memcpy(buf + used, root, rl);
			used += rl;
			p += 5;
		} else
			buf[used++] = *p++;
	}
	buf[used] = 0;
}

static int
apply_flag_token(Compiler* c, CliOpts* o, const char* tok) {
	if (strncmp(tok, "-D", 2) == 0 && tok[2]) {
		pp_define_cli(c, tok + 2);
		return 0;
	}
	if (strncmp(tok, "-I", 2) == 0 && tok[2]) {
		if (o->incpaths_len % 8 == 0)
			o->incpaths = xrealloc(o->incpaths, (o->incpaths_len + 8) * sizeof(char*));
		o->incpaths[o->incpaths_len++] = xstrdup(tok + 2);
		return 0;
	}
	if (strncmp(tok, "-F", 2) == 0 && tok[2]) {
		comp_add_framework_path(c, tok + 2);
		return 0;
	}
	if (strncmp(tok, "-M", 2) == 0 && tok[2]) {
		pkg_add_search_path(c, tok + 2);
		return 0;
	}
	if (strcmp(tok, "--no-system-includes") == 0) {
		o->no_system_includes = 1;
		return 0;
	}
	fprintf(stderr, "modc selftest: unknown flag '%s'\n", tok);
	return 1;
}

static int
apply_flags_line(Compiler* c, CliOpts* o, const char* flags) {
	char buf[HOST_PATH_MAX * 2];
	char* p;
	char* tok;

	expand_root(flags, buf, sizeof(buf));
	p = buf;
	while (*p) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (*p == 0)
			break;
		tok = p;
		while (*p && !isspace((unsigned char)*p))
			p++;
		if (*p)
			*p++ = 0;
		/* Separate-arg forms: -I DIR, -M DIR, -F DIR, -D NAME */
		if ((strcmp(tok, "-I") == 0 || strcmp(tok, "-M") == 0 ||
			strcmp(tok, "-F") == 0 || strcmp(tok, "-D") == 0 ||
			strcmp(tok, "--define") == 0 ||
			strcmp(tok, "--include-dir") == 0)) {
			char* arg;
			char glued[HOST_PATH_MAX + 8];

			while (*p && isspace((unsigned char)*p))
				p++;
			if (*p == 0) {
				fprintf(stderr, "modc selftest: %s requires an argument\n", tok);
				return 1;
			}
			arg = p;
			while (*p && !isspace((unsigned char)*p))
				p++;
			if (*p)
				*p++ = 0;
			if (strcmp(tok, "-D") == 0 || strcmp(tok, "--define") == 0)
				snprintf(glued, sizeof(glued), "-D%s", arg);
			else if (strcmp(tok, "-I") == 0 || strcmp(tok, "--include-dir") == 0)
				snprintf(glued, sizeof(glued), "-I%s", arg);
			else if (strcmp(tok, "-M") == 0)
				snprintf(glued, sizeof(glued), "-M%s", arg);
			else
				snprintf(glued, sizeof(glued), "-F%s", arg);
			if (apply_flag_token(c, o, glued))
				return 1;
			continue;
		}
		if (apply_flag_token(c, o, tok))
			return 1;
	}
	return 0;
}

static int
run_expect_fail(Compiler* c, CliOpts* base, const char* mc, const char* exp_path) {
	char* text = NULL;
	size_t len = 0;
	char* line;
	char* save;
	char flags[HOST_PATH_MAX * 2];
	CliOpts o;
	int saw_error;
	int missing;

	flags[0] = 0;
	if (read_file_str(exp_path, &text, &len)) {
		fprintf(stderr, "modc selftest: cannot read %s\n", exp_path);
		return 1;
	}
	memset(&o, 0, sizeof(o));
	o.no_system_includes = base->no_system_includes;
	o.verbose = base->verbose;
	o.target = base->target;
	selftest_reset(c);
	c->quiet_diag = 1;
	diag_clear(c);
	/* First pass: collect # flags: */
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		if (strncmp(line, "# flags:", 8) == 0) {
			snprintf(flags, sizeof(flags), "%s", line + 8);
			while (flags[0] && isspace((unsigned char)flags[0]))
				memmove(flags, flags + 1, strlen(flags));
		}
	}
	free(text);
	text = NULL;
	if (flags[0] && apply_flags_line(c, &o, flags))
		return 1;
	apply_cli(c, &o);
	c->check_only = 1;
	c->quiet_diag = 1;
	diag_clear(c);
	saw_error = compile_file(c, mc, NULL) != 0 || c->error_count != 0;
	if (!saw_error) {
		fprintf(stderr, "FAIL: %s: expected check to fail\n", mc);
		c->quiet_diag = 0;
		return 1;
	}
	/* Second pass: needles */
	if (read_file_str(exp_path, &text, &len))
		return 1;
	missing = 0;
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char needle[HOST_PATH_MAX * 2];
		const char* raw;

		if (line[0] == 0 || line[0] == '#')
			continue;
		if (strncmp(line, "F:", 2) == 0)
			raw = line + 2;
		else
			raw = line;
		expand_root(raw, needle, sizeof(needle));
		if (!file_contains(c->diag_log, needle)) {
			fprintf(stderr, "FAIL: %s: missing needle: %s\n", mc, needle);
			missing = 1;
		}
	}
	free(text);
	c->quiet_diag = 0;
	if (missing)
		return 1;
	printf("  fail  %s\n", mc);
	return 0;
}

static int
endswith_str(const char* s, const char* suf) {
	size_t n, m;

	if (s == NULL || suf == NULL)
		return 0;
	n = strlen(s);
	m = strlen(suf);
	return n >= m && strcmp(s + n - m, suf) == 0;
}

static int
selftest_expect_fail_all(Compiler* c, CliOpts* o) {
	HostDir* d;
	const char* name;
	char path[HOST_PATH_MAX];
	char mc[HOST_PATH_MAX];
	char stem[256];
	int n = 0, failed = 0;
	size_t i;

	d = host_opendir("test");
	if (d == NULL) {
		fprintf(stderr, "modc selftest: cannot open test/\n");
		return 1;
	}
	printf("== selftest: expect-fail ==\n");
	while ((name = host_readdir(d)) != NULL) {
		if (!endswith_str(name, ".expect"))
			continue;
		if (endswith_str(name, ".qbe.expect"))
			continue;
		if (strstr(name, ".nosys."))
			continue;
		/* stem.qbe.expect already skipped; stem.nosys.expect skipped */
		if (endswith_str(name, "nosys.expect"))
			continue;
		snprintf(path, sizeof(path), "test/%s", name);
		/* basename without .expect */
		snprintf(stem, sizeof(stem), "%s", name);
		i = strlen(stem);
		if (i > 7 && strcmp(stem + i - 7, ".expect") == 0)
			stem[i - 7] = 0;
		if (strstr(stem, ".qbe"))
			continue;
		snprintf(mc, sizeof(mc), "test/%s.mc", stem);
		if (!host_is_file(mc)) {
			fprintf(stderr, "FAIL: missing %s for %s\n", mc, path);
			failed = 1;
			continue;
		}
		if (run_expect_fail(c, o, mc, path))
			failed = 1;
		else
			n++;
	}
	host_closedir(d);
	printf("== selftest: %d expect-fail ==\n", n);
	return failed;
}

static int
load_env_file(const char* path) {
	char* text;
	size_t len;
	char* line;
	char* save;

	if (!host_is_file(path))
		return 0;
	if (read_file_str(path, &text, &len))
		return 1;
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char* eq;
		char key[256];
		char valbuf[HOST_PATH_MAX * 2];
		char expanded[HOST_PATH_MAX * 2];

		if (line[0] == 0 || line[0] == '#')
			continue;
		eq = strchr(line, '=');
		if (eq == NULL)
			continue;
		*eq = 0;
		snprintf(key, sizeof(key), "%s", line);
		expand_root(eq + 1, expanded, sizeof(expanded));
#ifdef _WIN32
		snprintf(valbuf, sizeof(valbuf), "%s=%s", key, expanded);
		_putenv(valbuf);
#else
		if (expanded[0] == 0)
			unsetenv(key);
		else
			setenv(key, expanded, 1);
#endif
		(void)valbuf;
	}
	free(text);
	return 0;
}

static int
check_qbe_expect(const char* stem, const char* qbe_path) {
	char exp_path[HOST_PATH_MAX];
	char* text;
	size_t len;
	char* line;
	char* save;
	char* qbe;
	size_t qlen;
	int bad = 0;

	snprintf(exp_path, sizeof(exp_path), "test/%s.qbe.expect", stem);
	if (!host_is_file(exp_path))
		return 0;
	if (read_file_str(exp_path, &text, &len))
		return 1;
	if (read_file_str(qbe_path, &qbe, &qlen)) {
		free(text);
		return 1;
	}
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		if (line[0] == 0 || line[0] == '#')
			continue;
		if (line[0] == '+') {
			if (!file_contains(qbe, line + 1)) {
				fprintf(stderr, "FAIL: %s: QBE missing: %s\n", stem, line + 1);
				bad = 1;
			}
		} else if (line[0] == '-') {
			if (file_contains(qbe, line + 1)) {
				fprintf(stderr, "FAIL: %s: QBE should not contain: %s\n", stem, line + 1);
				bad = 1;
			}
		} else {
			fprintf(stderr, "FAIL: %s: bad qbe.expect line: %s\n", stem, line);
			bad = 1;
		}
	}
	free(text);
	free(qbe);
	return bad;
}

static int
run_check_ok_line(Compiler* c, CliOpts* base, char* line) {
	CliOpts o;
	char* argv_buf[64];
	int argc = 0;
	char* p;
	char* tok;
	int i, r;

	memset(&o, 0, sizeof(o));
	o.no_system_includes = base->no_system_includes;
	o.verbose = base->verbose;
	o.target = base->target;
	selftest_reset(c);
	p = line;
	while (*p) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (*p == 0)
			break;
		tok = p;
		while (*p && !isspace((unsigned char)*p))
			p++;
		if (*p)
			*p++ = 0;
		if (argc >= 63) {
			fprintf(stderr, "modc selftest: too many args on check-ok line\n");
			return 1;
		}
		argv_buf[argc++] = tok;
	}
	for (i = 0; i < argc; i++) {
		tok = argv_buf[i];
		if (tok[0] == '-') {
			if (apply_flag_token(c, &o, tok))
				return 1;
		} else
			add_file(&o, tok);
	}
	apply_cli(c, &o);
	c->check_only = 1;
	r = 0;
	for (i = 0; i < o.files_len; i++) {
		char* files_save[64];
		int files_n;
		int j;

		files_n = o.files_len;
		for (j = 0; j < files_n && j < 64; j++)
			files_save[j] = o.files[j];
		selftest_reset(c);
		memset(&o, 0, sizeof(o));
		o.no_system_includes = base->no_system_includes;
		o.verbose = base->verbose;
		o.target = base->target;
		for (j = 0; j < argc; j++) {
			if (argv_buf[j][0] == '-') {
				if (apply_flag_token(c, &o, argv_buf[j]))
					return 1;
			}
		}
		apply_cli(c, &o);
		c->check_only = 1;
		if (compile_file(c, files_save[i], NULL))
			r = 1;
	}
	if (r) {
		fprintf(stderr, "FAIL: check: expected success\n");
		return 1;
	}
	return 0;
}

static int
selftest_check_ok_all(Compiler* c, CliOpts* o) {
	char* text;
	size_t len;
	char* line;
	char* save;
	char* copy;
	int n = 0, failed = 0;

	if (!host_is_file("test/check-ok.list")) {
		fprintf(stderr, "modc selftest: missing test/check-ok.list\n");
		return 1;
	}
	printf("== selftest: check-ok ==\n");
	if (read_file_str("test/check-ok.list", &text, &len))
		return 1;
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char envpath[HOST_PATH_MAX];
		char* first_mc;
		char* q;
		char* disp;

		if (line[0] == 0 || line[0] == '#')
			continue;
		copy = xstrdup(line);
		disp = xstrdup(line);
		disp = xstrdup(line);
		first_mc = NULL;
		for (q = copy; *q;) {
			while (*q && isspace((unsigned char)*q))
				q++;
			if (*q == 0)
				break;
			if (first_mc == NULL && endswith_str(q, ".mc")) {
				first_mc = q;
				break;
			}
			while (*q && !isspace((unsigned char)*q))
				q++;
		}
		if (first_mc) {
			char stem[256];
			const char* bname;

			bname = host_path_basename(first_mc);
			snprintf(stem, sizeof(stem), "%s", bname);
			if (endswith_str(stem, ".mc"))
				stem[strlen(stem) - 3] = 0;
			snprintf(envpath, sizeof(envpath), "test/%s.env", stem);
			(void)load_env_file(envpath);
		}
		if (run_check_ok_line(c, o, copy))
			failed = 1;
		else {
			n++;
			printf("  check %s\n", disp);
		}
		free(copy);
		free(disp);
	}
	free(text);
	printf("== selftest: %d check-ok ==\n", n);
	return failed;
}

static int
selftest_run_one(Compiler* c, CliOpts* o, const char* mc) {
	char stem[256];
	char base[256];
	char envpath[HOST_PATH_MAX];
	char emitflags[HOST_PATH_MAX];
	char qbe_out[HOST_PATH_MAX];
	char asm_out[HOST_PATH_MAX];
	char main_c[HOST_PATH_MAX];
	char host_c[HOST_PATH_MAX];
	char bin[HOST_PATH_MAX];
	const char* bn;
	CliOpts local;
	const char* qbe;
	const char* argv[16];
	int r, narg;

	bn = host_path_basename(mc);
	snprintf(stem, sizeof(stem), "%s", bn);
	if (endswith_str(stem, ".mc"))
		stem[strlen(stem) - 3] = 0;
	snprintf(base, sizeof(base), "%s", stem);
	if (endswith_str(base, "_modc"))
		base[strlen(base) - 5] = 0;

	snprintf(envpath, sizeof(envpath), "test/%s.env", stem);
	if (!host_is_file(envpath))
		snprintf(envpath, sizeof(envpath), "test/%s.env", base);
	(void)load_env_file(envpath);

	memset(&local, 0, sizeof(local));
	local.no_system_includes = o->no_system_includes;
	local.verbose = o->verbose;
	local.target = o->target;

	(void)host_mkdir("build");
	snprintf(qbe_out, sizeof(qbe_out), "build/%s.qbe", stem);
	snprintf(asm_out, sizeof(asm_out), "build/%s.s", stem);
	snprintf(bin, sizeof(bin), "build/%s-test", base);
	snprintf(main_c, sizeof(main_c), "test/%s_main.c", base);
	snprintf(host_c, sizeof(host_c), "test/%s_host.c", base);

	selftest_reset(c);
	snprintf(emitflags, sizeof(emitflags), "test/%s.emitflags", stem);
	if (host_is_file(emitflags)) {
		char* t;
		size_t n;
		if (read_file_str(emitflags, &t, &n) == 0) {
			while (n > 0 && (t[n - 1] == '\n' || t[n - 1] == '\r'))
				t[--n] = 0;
			if (apply_flags_line(c, &local, t)) {
				free(t);
				return 1;
			}
			free(t);
		}
	}
	apply_cli(c, &local);
	local.output = qbe_out;
	if (emit_one(c, &local, mc)) {
		fprintf(stderr, "FAIL: emit %s\n", mc);
		return 1;
	}
	if (check_qbe_expect(stem, qbe_out))
		return 1;

	/* Interop leftovers: link host C driver with QBE asm (like old corpus). */
	if (host_is_file(main_c)) {
		qbe = getenv("MODC_QBE");
		if (qbe == NULL || qbe[0] == 0)
			qbe = "qbe";
		argv[0] = qbe;
		argv[1] = "-o";
		argv[2] = asm_out;
		argv[3] = qbe_out;
		argv[4] = NULL;
		if (o->verbose) {
			fprintf(stderr, "+ %s -o %s %s\n", qbe, asm_out, qbe_out);
		}
		if (host_spawn_wait(argv) != 0) {
			fprintf(stderr, "FAIL: qbe %s\n", mc);
			return 1;
		}
		narg = 0;
		argv[narg++] = "cc";
		argv[narg++] = "-o";
		argv[narg++] = bin;
		argv[narg++] = main_c;
		argv[narg++] = asm_out;
		if (host_is_file(host_c))
			argv[narg++] = host_c;
		argv[narg] = NULL;
		if (o->verbose) {
			int i;
			fputc('+', stderr);
			for (i = 0; i < narg; i++)
				fprintf(stderr, " %s", argv[i]);
			fputc('\n', stderr);
		}
		if (host_spawn_wait(argv) != 0) {
			fprintf(stderr, "FAIL: link %s\n", mc);
			return 1;
		}
		argv[0] = bin;
		argv[1] = NULL;
		r = host_spawn_wait(argv);
		if (r != 0) {
			fprintf(stderr, "FAIL: run %s (exit %d)\n", mc, r);
			return 1;
		}
		printf("  run   %s\n", mc);
		return 0;
	}

	/* Pure %C entry (rare on run.list now). */
	selftest_reset(c);
	apply_cli(c, &local);
	r = build_and_run_root(c, &local, mc);
	if (r) {
		fprintf(stderr, "FAIL: run %s\n", mc);
		return 1;
	}
	printf("  run   %s\n", mc);
	return 0;
}

static int
selftest_run_all(Compiler* c, CliOpts* o) {
	char* text;
	size_t len;
	char* line;
	char* save;
	int n = 0, failed = 0;

	if (!host_is_file("test/run.list")) {
		printf("== selftest: run (none) ==\n");
		return 0;
	}
	printf("== selftest: run ==\n");
	if (read_file_str("test/run.list", &text, &len))
		return 1;
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		if (line[0] == 0 || line[0] == '#')
			continue;
		if (selftest_run_one(c, o, line))
			failed = 1;
		else
			n++;
	}
	free(text);
	printf("== selftest: %d run ==\n", n);
	return failed;
}

// modc selftest: portable compiler corpus in-process.
int
cmd_selftest(Compiler* c, CliOpts* o, int argc, char** argv) {
	int i, r, failed;

	i = 2;
	r = parse_common(c, o, &i, argc, argv, 0);
	if (r < 0) {
		usage("selftest");
		return 0;
	}
	if (r != 0)
		return 1;
	if (o->files_len != 0) {
		fprintf(stderr, "modc selftest: unexpected path arguments\n");
		return 1;
	}
	/* Default hermetic stubs for the compiler tree. */
	if (getenv("MODC_NO_SYSTEM_INCLUDES") == NULL)
		o->no_system_includes = 1;
	apply_cli(c, o);
	failed = 0;
	if (selftest_expect_fail_all(c, o))
		failed = 1;
	if (selftest_check_ok_all(c, o))
		failed = 1;
	if (selftest_run_all(c, o))
		failed = 1;
	return failed ? 1 : 0;
}
