/*
 * modc selftest — portable compiler corpus (no shell / Make).
 *
 * Lanes: *_fail.mc, *_ok.mc, *_run.mc.  *_test.mc stays on `modc test`.
 */
#include "cli.h"
#include <ctype.h>
#include <stdlib.h>

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
	if (c->diag.diag_log)
		c->diag.diag_log[0] = 0;
	c->diag.diag_log_len = 0;
}

static void
selftest_reset(Compiler* c) {
	reset_comp_state(c);
	pp_clear_macros(c);
	pp_clear_once(c);
	pp_init(c);
	diag_clear(c);
	c->diag.error_count = 0;
	c->diag.fatal = 0;
	c->paths.cli_defs_len = 0;
	/* Fresh process semantics for mangled statics (__fN_*) and .qbe.expect. */
	c->unit.static_seq = 0;
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
	if (strcmp(tok, "--bounds-check") == 0) {
		o->bounds_check = 1;
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
endswith_str(const char* s, const char* suf) {
	size_t n, m;

	if (s == NULL || suf == NULL)
		return 0;
	n = strlen(s);
	m = strlen(suf);
	return n >= m && strcmp(s + n - m, suf) == 0;
}

static int
cmp_cstr(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

/* Collect test/<name> paths whose basename ends with suffix; sorted. */
static int
collect_suffix_mcs(const char* suffix, char*** out, int* out_n) {
	HostDir* d;
	const char* name;
	char** list = NULL;
	int n = 0, cap = 0;

	*out = NULL;
	*out_n = 0;
	d = host_opendir("test");
	if (d == NULL) {
		fprintf(stderr, "modc selftest: cannot open test/\n");
		return 1;
	}
	while ((name = host_readdir(d)) != NULL) {
		char path[HOST_PATH_MAX];

		if (!endswith_str(name, suffix))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 32;
			list = xrealloc(list, (size_t)cap * sizeof(char*));
		}
		snprintf(path, sizeof(path), "test/%s", name);
		list[n++] = xstrdup(path);
	}
	host_closedir(d);
	if (n > 1)
		qsort(list, (size_t)n, sizeof(char*), cmp_cstr);
	*out = list;
	*out_n = n;
	return 0;
}

/* Body of a leading "// fail: ..." or "// ok: ..." directive, or NULL. */
static char*
tagged_directive(char* line, const char* tag) {
	size_t n;

	while (*line && isspace((unsigned char)*line))
		line++;
	if (strncmp(line, "//", 2) != 0)
		return NULL;
	line += 2;
	while (*line && isspace((unsigned char)*line))
		line++;
	n = strlen(tag);
	if (strncmp(line, tag, n) != 0 || line[n] != ':')
		return NULL;
	line += n + 1;
	while (*line && isspace((unsigned char)*line))
		line++;
	return line;
}

static char*
fail_directive(char* line) {
	return tagged_directive(line, "fail");
}

static char*
ok_directive(char* line) {
	return tagged_directive(line, "ok");
}

static int
run_expect_fail(Compiler* c, CliOpts* base, const char* mc) {
	char* text = NULL;
	size_t len = 0;
	char* line;
	char* save;
	char flags[HOST_PATH_MAX * 2];
	CliOpts o;
	int saw_error;
	int missing;
	int saw_needle = 0;

	flags[0] = 0;
	if (read_file_str(mc, &text, &len)) {
		fprintf(stderr, "modc selftest: cannot read %s\n", mc);
		return 1;
	}
	memset(&o, 0, sizeof(o));
	o.no_system_includes = base->no_system_includes;
	o.verbose = base->verbose;
	o.target = base->target;
	selftest_reset(c);
	c->diag.quiet_diag = 1;
	diag_clear(c);
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char* dir = fail_directive(line);

		if (dir == NULL)
			continue;
		if (strncmp(dir, "flags:", 6) == 0) {
			snprintf(flags, sizeof(flags), "%s", dir + 6);
			while (flags[0] && isspace((unsigned char)flags[0]))
				memmove(flags, flags + 1, strlen(flags));
		}
	}
	free(text);
	text = NULL;
	if (flags[0] && apply_flags_line(c, &o, flags))
		return 1;
	apply_cli(c, &o);
	c->opt.check_only = 1;
	c->diag.quiet_diag = 1;
	diag_clear(c);
	saw_error = compile_file(c, mc, NULL) != 0 || c->diag.error_count != 0;
	if (!saw_error) {
		fprintf(stderr, "FAIL: %s: expected check to fail\n", mc);
		c->diag.quiet_diag = 0;
		return 1;
	}
	if (read_file_str(mc, &text, &len))
		return 1;
	missing = 0;
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char needle[HOST_PATH_MAX * 2];
		const char* raw;
		char* dir = fail_directive(line);

		if (dir == NULL)
			continue;
		if (strncmp(dir, "flags:", 6) == 0)
			continue;
		if (dir[0] == '#')
			continue;
		if (strncmp(dir, "F:", 2) == 0)
			raw = dir + 2;
		else
			raw = dir;
		while (*raw && isspace((unsigned char)*raw))
			raw++;
		if (*raw == 0)
			continue;
		saw_needle = 1;
		expand_root(raw, needle, sizeof(needle));
		if (!file_contains(c->diag.diag_log, needle)) {
			fprintf(stderr, "FAIL: %s: missing needle: %s\n", mc, needle);
			missing = 1;
		}
	}
	free(text);
	c->diag.quiet_diag = 0;
	if (!saw_needle) {
		fprintf(stderr, "FAIL: %s: no // fail: F: needle\n", mc);
		return 1;
	}
	if (missing)
		return 1;
	printf("  fail  %s\n", mc);
	return 0;
}

static int
selftest_expect_fail_all(Compiler* c, CliOpts* o) {
	char** list = NULL;
	int nlist = 0, i, n = 0, failed = 0;

	printf("== selftest: fail ==\n");
	if (collect_suffix_mcs("_fail.mc", &list, &nlist))
		return 1;
	for (i = 0; i < nlist; i++) {
		if (run_expect_fail(c, o, list[i]))
			failed = 1;
		else
			n++;
		free(list[i]);
	}
	free(list);
	printf("== selftest: %d fail ==\n", n);
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
run_check_ok(Compiler* c, CliOpts* base, const char* mc) {
	char* text = NULL;
	size_t len = 0;
	char* line;
	char* save;
	char flags[HOST_PATH_MAX * 2];
	char envpath[HOST_PATH_MAX];
	char stem[256];
	const char* bn;
	CliOpts o;

	flags[0] = 0;
	bn = host_path_basename(mc);
	snprintf(stem, sizeof(stem), "%s", bn);
	if (endswith_str(stem, ".mc"))
		stem[strlen(stem) - 3] = 0;
	snprintf(envpath, sizeof(envpath), "test/%s.env", stem);
	(void)load_env_file(envpath);

	if (read_file_str(mc, &text, &len)) {
		fprintf(stderr, "modc selftest: cannot read %s\n", mc);
		return 1;
	}
	save = text;
	for (line = next_line(&save); line; line = next_line(&save)) {
		char* dir = ok_directive(line);

		if (dir == NULL)
			continue;
		if (strncmp(dir, "flags:", 6) == 0) {
			snprintf(flags, sizeof(flags), "%s", dir + 6);
			while (flags[0] && isspace((unsigned char)flags[0]))
				memmove(flags, flags + 1, strlen(flags));
		}
	}
	free(text);

	memset(&o, 0, sizeof(o));
	o.no_system_includes = base->no_system_includes;
	o.verbose = base->verbose;
	o.target = base->target;
	selftest_reset(c);
	if (flags[0] && apply_flags_line(c, &o, flags))
		return 1;
	apply_cli(c, &o);
	c->opt.check_only = 1;
	if (compile_file(c, mc, NULL)) {
		fprintf(stderr, "FAIL: check %s: expected success\n", mc);
		return 1;
	}
	printf("  ok    %s\n", mc);
	return 0;
}

static int
selftest_check_ok_all(Compiler* c, CliOpts* o) {
	char** list = NULL;
	int nlist = 0, i, n = 0, failed = 0;

	printf("== selftest: ok ==\n");
	if (collect_suffix_mcs("_ok.mc", &list, &nlist))
		return 1;
	for (i = 0; i < nlist; i++) {
		if (run_check_ok(c, o, list[i]))
			failed = 1;
		else
			n++;
		free(list[i]);
	}
	free(list);
	printf("== selftest: %d ok ==\n", n);
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
	/* Sidecar C drivers use stem without _run / _modc suffixes. */
	if (endswith_str(base, "_run"))
		base[strlen(base) - 4] = 0;
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

	/* Pure %C entry (rare). */
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
	char** list = NULL;
	int nlist = 0, i, n = 0, failed = 0;

	printf("== selftest: run ==\n");
	if (collect_suffix_mcs("_run.mc", &list, &nlist))
		return 1;
	if (nlist == 0) {
		printf("== selftest: run (none) ==\n");
		return 0;
	}
	for (i = 0; i < nlist; i++) {
		if (selftest_run_one(c, o, list[i]))
			failed = 1;
		else
			n++;
		free(list[i]);
	}
	free(list);
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
