/*
 * CLI build/link: compile graph, package cache, foreign objs, link.
 *
 * Compilation pipeline: [lex → pp → parse → type check → emit → QBE]
 */
#include "cli.h"
#include "ini.h"
#include <stdarg.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

enum { MaxForeignObj = 64,
       MaxCachePkgs = 64 };

/* Cached post-pp token stream for one TU (prescan + parse share one lex/pp). */
typedef struct {
	Tok* tokens;
	int tokens_len;
	int tokens_cap;
	char* infile;
} TuCache;

typedef struct {
	char dir[HOST_PATH_MAX];
	char id[128];
	uint64_t impl;
	uint64_t iface; /* public API digest (.pkg.mc hash); 0 = unknown */
	uint64_t key;
	char hex[24];
	char ifacepath[HOST_PATH_MAX];
	char objpath[HOST_PATH_MAX];
	char metapath[HOST_PATH_MAX];
	int hit;
	/* Must parse/emit this package (not reuse cached .o). */
	int need_emit;
	/* Under projroot/vendor/ — may freeze hashing to modc.lock (FPC -Ur). */
	int vendored;
	/* Direct import edges into pkgs[] indices. */
	int imports[MaxCachePkgs];
	int nimports;
	/* Transitive import closure bitset (excluding self). */
	unsigned char reach[MaxCachePkgs];
} BuildPkg;

/*
 * TTY progress: hold a line while work runs, commit with a right-aligned
 * wall-time delta. Silent for pipes/CI.
 */
enum { ProgressLabelW = 36, ProgressDeltaW = 7 };

static int progress_held;
static char progress_label[512];
static long long progress_t0_ms;
static int progress_build_on;
static long long progress_build_t0_ms;

static void
progress_clear_held(void) {
	if (!progress_held)
		return;
	fputs("\r\033[K", stderr);
	fflush(stderr);
	progress_held = 0;
}

/* Called from diag before printing so errors do not append to a held line. */
void
progress_interrupt(void) {
	progress_clear_held();
}

static void
progress_fmt_delta(char* out, size_t n, long long ms) {
	if (ms < 0)
		ms = 0;
	if (ms < 1)
		snprintf(out, n, "<1ms");
	else if (ms < 10000)
		snprintf(out, n, "%lldms", ms);
	else
		snprintf(out, n, "%.1fs", (double)ms / 1000.0);
}

/* Commit "modc: <label…> <delta>" with aligned columns. */
static void
progress_commit(const char* label, long long ms) {
	char delta[32];

	if (!host_stderr_tty())
		return;
	progress_fmt_delta(delta, sizeof(delta), ms);
	fprintf(stderr, "\r\033[Kmodc: %-*s %*s\n", ProgressLabelW, label ? label : "",
		ProgressDeltaW, delta);
}

/* Instant line (no timing), e.g. "up to date". */
static void
progress_msg(const char* fmt, ...) {
	va_list ap;

	if (!host_stderr_tty())
		return;
	progress_clear_held();
	fputs("modc: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* Start a held progress line; call progress_end when the action finishes. */
static void
progress_begin(const char* fmt, ...) {
	va_list ap;

	if (!host_stderr_tty())
		return;
	progress_clear_held();
	va_start(ap, fmt);
	vsnprintf(progress_label, sizeof(progress_label), fmt, ap);
	va_end(ap);
	fprintf(stderr, "\r\033[Kmodc: %s", progress_label);
	fflush(stderr);
	progress_t0_ms = host_mono_ms();
	progress_held = 1;
}

/* Commit the held line with elapsed wall time. */
static void
progress_end(void) {
	long long ms;

	if (!progress_held)
		return;
	ms = host_mono_ms() - progress_t0_ms;
	progress_held = 0;
	progress_commit(progress_label, ms);
}

/* Drop an in-flight build total (error paths); does not print. */
static void
progress_build_cancel(void) {
	progress_build_on = 0;
}

static void
progress_build_start(void) {
	if (!host_stderr_tty())
		return;
	progress_build_t0_ms = host_mono_ms();
	progress_build_on = 1;
}

static void
progress_build_done(void) {
	long long ms;

	if (!progress_build_on)
		return;
	ms = host_mono_ms() - progress_build_t0_ms;
	progress_build_cancel();
	progress_commit("done", ms);
}

/* Prefer outpath/projroot basename over bare "." for the root package. */
static void
progress_root_name(const char* projroot, const char* outpath, char* out, size_t n) {
	const char* b;

	b = NULL;
	if (outpath && outpath[0])
		b = host_path_basename(outpath);
	if ((b == NULL || b[0] == 0) && projroot && projroot[0])
		b = host_path_basename(projroot);
	snprintf(out, n, "%s", b && b[0] ? b : ".");
}

/* Project-relative path when under projroot; else parent/base (e.g. crash/mod.mc). */
static void
progress_path_name(const char* path, const char* projroot, char* out, size_t n) {
	const char *base, *sep;
	char parent[HOST_PATH_MAX];
	const char* pbase;

	if (out == NULL || n == 0)
		return;
	out[0] = 0;
	if (path == NULL || path[0] == 0)
		return;
	if (projroot && projroot[0]) {
		cache_path_key(path, projroot, out, n);
		if (out[0] && !host_path_is_abs(out)) {
			if (strcmp(out, ".") == 0)
				progress_root_name(projroot, NULL, out, n);
			return;
		}
	}
	base = host_path_basename(path);
	if (base == NULL || base[0] == 0) {
		snprintf(out, n, "%s", path);
		return;
	}
	host_dirname(path, parent, sizeof(parent));
	sep = host_path_last_sep(parent);
	pbase = sep ? sep + 1 : parent;
	if (pbase[0] && strcmp(pbase, ".") != 0 && strcmp(pbase, "/") != 0)
		snprintf(out, n, "%s/%s", pbase, base);
	else
		snprintf(out, n, "%s", base);
}

/* outpath: optional link output; used when the package is the project root ("."). */
static void
pkg_progress_name(const BuildPkg* pkg, const char* projroot, const char* outpath, char* out,
		  size_t n) {
	const char* base;

	if (out == NULL || n == 0)
		return;
	out[0] = 0;
	if (pkg == NULL)
		return;
	if (projroot && projroot[0]) {
		cache_path_key(pkg->dir, projroot, out, n);
		if (out[0] && !host_path_is_abs(out)) {
			if (strcmp(out, ".") == 0)
				progress_root_name(projroot, outpath, out, n);
			return;
		}
	}
	/* Outside the project (stdlib crash, etc.): directory basename only. */
	base = host_path_basename(pkg->dir);
	snprintf(out, n, "%s", base && base[0] ? base : pkg->id);
}

// Lex and preprocess one path into c->lex.tokens; apply -D from j onward.
static int
lex_pp_file(Compiler* c, const char* path, int j) {
	char* text;

	c->lex.tokens = NULL;
	c->lex.tokens_len = 0;
	c->lex.tokens_cap = 0;
	c->lex.pos = 0;
	pp_clear_macros(c);
	pp_clear_once(c);
	pp_init(c);
	for (; j < c->paths.cli_defs_len; j++)
		pp_define(c, c->paths.cli_defs[j]);
	free(c->paths.infile);
	c->paths.infile = xstrdup(path);
	text = read_file(path, NULL);
	if (text == NULL) {
		fprintf(stderr, "modc: cannot read %s: %s\n", path, strerror(errno));
		return 1;
	}
	lex_file(c, c->paths.infile, text, 1);
	free(text);
	if (c->diag.error_count)
		return 1;
	pp_run(c);
	return c->diag.error_count ? 1 : 0;
}

// Wall-ish seconds from clock() for MODC_PROFILE timings.
static double
now_sec(void) {
	return (double)clock() / (double)CLOCKS_PER_SEC;
}

/* Hold "compiling|checking <file>" through lex/pp of one source. */
static void
progress_file_begin(const char* verb, const char* path, const char* projroot) {
	char name[HOST_PATH_MAX];

	if (verb == NULL)
		return;
	progress_path_name(path, projroot, name, sizeof(name));
	progress_begin("%s %s", verb, name);
}

/* Lex/parse/typecheck an already-discovered file list. Does not emit or free files.
 * After type_init, loads each exports[i] with package dir export_dirs[i].
 * progress_verb/projroot: held+delta per file lex, then typechecking (NULL = silent). */
static int
compile_graph(Compiler* c, char** files, int nfiles, char** exports, char** export_dirs,
	      int nexports, const char* progress_verb, const char* projroot) {
	int i, r;
	TuCache* cache;
	int profile;
	double t0, t1, t_lexpp, t_prescan, t_parse, t_type;

	type_init(c);
	for (i = 0; i < nexports; i++) {
		if (exports[i] &&
		    iface_load(c, exports[i], export_dirs ? export_dirs[i] : NULL) != 0) {
			fprintf(stderr, "modc: failed to load iface %s\n", exports[i]);
			return 1;
		}
	}
	c->unit.unit_files = NULL;
	c->unit.unit_files_len = 0;
	c->unit.unit_src_start = c->unit.src_files_len;
	r = 0;
	profile = getenv("MODC_PROFILE") != NULL;
	t_lexpp = t_prescan = t_parse = t_type = 0;
	cache = xmalloc((size_t)nfiles * sizeof(*cache));
	memset(cache, 0, (size_t)nfiles * sizeof(*cache));
	for (i = 0; i < nfiles; i++) {
		if (c->unit.unit_files_len % 8 == 0)
			c->unit.unit_files = xrealloc(c->unit.unit_files,
						(c->unit.unit_files_len + 8) * sizeof(char*));
		c->unit.unit_files[c->unit.unit_files_len++] = xstrdup(files[i]);
	}
	for (i = 0; i < nfiles; i++) {
		progress_file_begin(progress_verb, files[i], projroot);
		if (profile)
			t0 = now_sec();
		if (lex_pp_file(c, files[i], 0)) {
			progress_end();
			r = 1;
			break;
		}
		if (profile) {
			t1 = now_sec();
			t_lexpp += t1 - t0;
		}
		cache[i].tokens = c->lex.tokens;
		cache[i].tokens_len = c->lex.tokens_len;
		cache[i].tokens_cap = c->lex.tokens_cap;
		cache[i].infile = c->paths.infile; /* span.file aliases this */
		/* Detach so the next lex_pp_file does not drop or free these. */
		c->lex.tokens = NULL;
		c->lex.tokens_len = 0;
		c->lex.tokens_cap = 0;
		c->paths.infile = NULL;
		progress_end();
		if (c->diag.error_count) {
			r = 1;
			break;
		}
	}
	/*
	 * Remaining frontend passes are package-wide (not per-file wall time).
	 * Hold typechecking across prescan/parse/type_check.
	 */
	if (r == 0 && progress_verb)
		progress_begin("typechecking");
	/* Package-wide: type-name stubs, type bodies, layouts, then func/method
	 * signatures — Go-style package scope so file basename order does not matter. */
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->paths.infile = cache[i].infile;
		c->lex.tokens = cache[i].tokens;
		c->lex.tokens_len = cache[i].tokens_len;
		c->lex.tokens_cap = cache[i].tokens_cap;
		c->lex.pos = 0;
		prescan_unit_type_names(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->lex.tokens = NULL;
		c->lex.tokens_len = 0;
		c->lex.tokens_cap = 0;
		c->paths.infile = NULL;
		if (c->diag.error_count) {
			r = 1;
			break;
		}
	}
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->paths.infile = cache[i].infile;
		c->lex.tokens = cache[i].tokens;
		c->lex.tokens_len = cache[i].tokens_len;
		c->lex.tokens_cap = cache[i].tokens_cap;
		c->lex.pos = 0;
		prescan_unit_type_bodies(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->lex.tokens = NULL;
		c->lex.tokens_len = 0;
		c->lex.tokens_cap = 0;
		c->paths.infile = NULL;
		if (c->diag.error_count) {
			r = 1;
			break;
		}
	}
	if (r == 0)
		type_layout_pending(c);
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->paths.infile = cache[i].infile;
		c->lex.tokens = cache[i].tokens;
		c->lex.tokens_len = cache[i].tokens_len;
		c->lex.tokens_cap = cache[i].tokens_cap;
		c->lex.pos = 0;
		prescan_unit_funcs(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->lex.tokens = NULL;
		c->lex.tokens_len = 0;
		c->lex.tokens_cap = 0;
		c->paths.infile = NULL;
		if (c->diag.error_count) {
			r = 1;
			break;
		}
	}
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->paths.infile = cache[i].infile;
		c->lex.tokens = cache[i].tokens;
		c->lex.tokens_len = cache[i].tokens_len;
		c->lex.tokens_cap = cache[i].tokens_cap;
		c->lex.pos = 0;
		parse_unit(c);
		if (profile) {
			t1 = now_sec();
			t_parse += t1 - t0;
		}
		c->lex.tokens = NULL;
		c->lex.tokens_len = 0;
		c->lex.tokens_cap = 0;
		if (c->diag.error_count) {
			r = 1;
			break;
		}
	}
	/* Tokens are unused after parse; free on every exit. infile stays live:
	 * AST/Symbol spans alias those strings (arena-style for the process). */
	if (r) {
		progress_end();
		if (profile)
			fprintf(stderr,
				"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs (failed)\n",
				t_lexpp, t_prescan, t_parse);
		for (i = 0; i < nfiles; i++)
			free(cache[i].tokens);
		free(cache);
		return 1;
	}
	if (profile)
		t0 = now_sec();
	type_check_unit(c);
	if (profile) {
		t1 = now_sec();
		t_type = t1 - t0;
	}
	progress_end();
	if (c->diag.error_count) {
		if (profile)
			fprintf(stderr,
				"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs type=%.3fs\n",
				t_lexpp, t_prescan, t_parse, t_type);
		for (i = 0; i < nfiles; i++)
			free(cache[i].tokens);
		free(cache);
		return 1;
	}
	if (profile)
		fprintf(stderr,
			"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs type=%.3fs\n",
			t_lexpp, t_prescan, t_parse, t_type);
	for (i = 0; i < nfiles; i++)
		free(cache[i].tokens);
	free(cache);
	return 0;
}

// Discover import graph, typecheck, optionally emit_qbe to outf.
int
compile_file(Compiler* c, const char* path, FILE* outf) {
	char** files;
	int nfiles, i, r;
	int profile;
	double t0, t1, t_emit;

	files = NULL;
	nfiles = 0;
	if (pkg_discover(c, path, &files, &nfiles))
		return 1;
	if (pkg_ensure(c, "crash", &files, &nfiles)) {
		for (i = 0; i < nfiles; i++)
			free(files[i]);
		free(files);
		return 1;
	}
	r = compile_graph(c, files, nfiles, NULL, NULL, 0, NULL, NULL);
	for (i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);
	if (r)
		return 1;
	if (!c->opt.check_only) {
		if (outf == NULL)
			outf = stdout;
		profile = getenv("MODC_PROFILE") != NULL;
		if (profile)
			t0 = now_sec();
		emit_qbe(c, outf);
		if (fflush(outf) != 0 || ferror(outf)) {
			fprintf(stderr, "modc: output write failed: %s\n", strerror(errno));
			return 1;
		}
		if (profile) {
			t1 = now_sec();
			t_emit = t1 - t0;
			fprintf(stderr, "modc profile: emit=%.3fs\n", t_emit);
		}
		if (c->diag.error_count)
			return 1;
	}
	return 0;
}

/*
 * Clear per-TU state so the next root can reuse the same Compiler.
 *
 * Cleared: lex tokens/pos; diag error_count/fatal; symbol hash (freed) and
 * symbols_len/block/current_fn; unit funcs/globals/unit_files; type_list
 * (abandoned; compile_graph → type_init rebuilds builtins once); pragma link
 * lists (c_libs/frameworks/csources lens — avoids cross-root contamination).
 * Token/symbol/AST list pointers are dropped without free — arena-style.
 *
 * Preserved: CompOpt; CompPaths (incpaths/sysinc/project_root/…); CompPp
 * (macros, #pragma once, intern); diag quiet flags / diag_log;
 * unit.src_files/src_text/strpool/static_seq (export-load / multi-root).
 * Callers that need a fresh preprocessor (e.g. selftest) must
 * pp_clear_macros / pp_clear_once themselves.
 */
void
reset_comp_state(Compiler* c) {
	c->lex.tokens = NULL;
	c->lex.tokens_len = 0;
	c->lex.tokens_cap = 0;
	c->lex.pos = 0;
	c->diag.error_count = 0;
	c->diag.fatal = 0;
	c->syms.symbols = NULL;
	free(c->syms.symbol_tab);
	c->syms.symbol_tab = NULL;
	c->syms.symbol_tab_cap = 0;
	c->syms.symbols_len = 0;
	c->syms.block = 0;
	c->unit.funcs = NULL;
	c->unit.funcs_len = 0;
	c->unit.funcs_cap = 0;
	c->unit.globals = NULL;
	c->unit.globals_len = 0;
	c->unit.globals_cap = 0;
	c->syms.current_fn = NULL;
	c->types.type_list = NULL;
	free(c->types.ranged_tab);
	c->types.ranged_tab = NULL;
	c->types.ranged_tab_cap = 0;
	c->types.ranged_len = 0;
	free(c->types.tuple_tab);
	c->types.tuple_tab = NULL;
	c->types.tuple_tab_cap = 0;
	c->types.tuple_len = 0;
	c->unit.unit_files = NULL;
	c->unit.unit_files_len = 0;
	c->paths.c_libs_len = 0;
	c->paths.frameworks_len = 0;
	c->paths.csources_len = 0;
}

// Compile one root and write QBE to outpath or stdout.
int
emit_one(Compiler* c, CliOpts* o, const char* path) {
	FILE* f;
	int r;

	if (o->output) {
		f = fopen(o->output, "w");
		if (f == NULL) {
			fprintf(stderr, "modc: cannot write %s: %s\n", o->output, strerror(errno));
			return 1;
		}
		r = compile_file(c, path, f);
		if (fclose(f) != 0) {
			fprintf(stderr, "modc: cannot finish %s: %s\n",
				o->output, strerror(errno));
			r = 1;
		}
		if (r)
			(void)host_unlink(o->output);
		return r;
	}
	return compile_file(c, path, stdout);
}

enum { MaxCmdArgs = 1024 };

// Print one argv element unambiguously for -v diagnostics.
static void
print_arg(const char* s) {
	const char* p;
	int quote;

	quote = s[0] == 0;
	for (p = s; *p; p++)
		if (!isalnum((unsigned char)*p) && strchr("_./:=+,-", *p) == NULL)
			quote = 1;
	if (!quote) {
		fputs(s, stderr);
		return;
	}
	fputc('\'', stderr);
	for (p = s; *p; p++) {
		if (*p == '\'')
			fputs("'\\''", stderr);
		else
			fputc(*p, stderr);
	}
	fputc('\'', stderr);
}

// Run a command directly, without shell parsing.
static int
run_argv(int verbose, const char* const argv[]) {
	int st;
	int i;

	if (verbose) {
		fputc('+', stderr);
		for (i = 0; argv[i]; i++) {
			fputc(' ', stderr);
			print_arg(argv[i]);
		}
		fputc('\n', stderr);
	}
	st = host_spawn_wait(argv);
	if (st != 0) {
		if (st == -1)
			fprintf(stderr, "modc: failed to run command: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

// True if path is a regular file we can execute.
static int
tool_is_exe(const char* path) {
	if (!host_is_file(path))
		return 0;
#ifdef _WIN32
	return 1;
#else
	return access(path, X_OK) == 0;
#endif
}

/* MODC_CC / MODC_CXX: require a real executable (path or bare name on PATH). */
static const char*
tool_env_override(const char* envname, const char* val, char* buf, size_t nbuf) {
	static char last_env[32];
	static char last_val[HOST_PATH_MAX];
	static int last_bad;

	if (val == NULL || val[0] == 0)
		return NULL;
	if (tool_is_exe(val))
		return val;
	if (!host_path_has_sep(val) &&
	    host_resolve_on_path(val, buf, nbuf) == 0 && tool_is_exe(buf))
		return buf;
	/* One diagnostic per distinct bad override (knobs may probe twice). */
	if (!last_bad || strcmp(last_env, envname) != 0 || strcmp(last_val, val) != 0) {
		fprintf(stderr, "modc: %s=%s is not an executable file\n", envname, val);
		snprintf(last_env, sizeof(last_env), "%s", envname);
		snprintf(last_val, sizeof(last_val), "%s", val);
		last_bad = 1;
	}
	return NULL;
}

// Host C compiler for assemble/link (MODC_CC, CC, or platform / --target default).
static const char*
tool_cc(Compiler* c) {
	const char* cc;
	static char mingw_buf[HOST_PATH_MAX];
	static char env_buf[HOST_PATH_MAX];

	cc = getenv("MODC_CC");
	if (cc && cc[0])
		return tool_env_override("MODC_CC", cc, env_buf, sizeof(env_buf));
	if (c && c->opt.target == TargetWindows) {
#ifndef _WIN32
		static const char* cands[] = {
			"/opt/homebrew/bin/x86_64-w64-mingw32-gcc",
			"/usr/local/bin/x86_64-w64-mingw32-gcc",
			"x86_64-w64-mingw32-gcc",
			NULL
		};
		int i;

		for (i = 0; cands[i]; i++) {
			if (strchr(cands[i], '/')) {
				if (host_is_file(cands[i]))
					return cands[i];
			} else {
				/* Bare name: let the linker search PATH. */
				snprintf(mingw_buf, sizeof(mingw_buf), "%s", cands[i]);
				return mingw_buf;
			}
		}
#endif
	}
	cc = getenv("CC");
	if (cc && cc[0])
		return cc;
#ifdef _WIN32
	return "clang";
#else
	return "cc";
#endif
}

// Path to the qbe binary (MODC_QBE or a real file on disk / PATH).
static const char*
tool_qbe(void) {
	const char* q;
	static char pathbuf[HOST_PATH_MAX];
	static const char* abs_cands[] = {
		"/opt/homebrew/bin/qbe",
		"/usr/local/bin/qbe",
		NULL
	};
	int i;
	const char* path;
	char piece[HOST_PATH_MAX];
	size_t n;

	q = getenv("MODC_QBE");
	if (q && q[0]) {
		if (tool_is_exe(q))
			return q;
		fprintf(stderr, "modc: MODC_QBE=%s is not an executable file\n", q);
		return NULL;
	}
#ifdef _WIN32
	if (host_is_file("qbe.exe"))
		return "qbe.exe";
	if (host_is_file("./qbe.exe"))
		return "./qbe.exe";
#endif
	for (i = 0; abs_cands[i]; i++) {
		if (tool_is_exe(abs_cands[i]))
			return abs_cands[i];
	}
	path = getenv("PATH");
	if (path) {
		for (;;) {
			n = 0;
			while (path[n] && path[n] != ':')
				n++;
			if (n > 0 && n < sizeof(piece)) {
				memcpy(piece, path, n);
				piece[n] = 0;
				snprintf(pathbuf, sizeof(pathbuf), "%s/qbe", piece);
				if (tool_is_exe(pathbuf))
					return pathbuf;
			}
			if (path[n] == 0)
				break;
			path += n + 1;
		}
	}
	return "qbe";
}

// QBE -t triple for --target / host (or MODC_QBE_TARGET).
static const char*
tool_qbe_target(Compiler* c) {
	const char* t;

	t = getenv("MODC_QBE_TARGET");
	if (t && t[0])
		return t;
	if (c && c->opt.target == TargetWindows)
		return "amd64_win";
	if (c && c->opt.target == TargetMacos) {
#if defined(__aarch64__) || defined(__arm64__)
		return "arm64_apple";
#else
		return "amd64_apple";
#endif
	}
	if (c && c->opt.target == TargetLinux) {
#if defined(__aarch64__) || defined(__arm64__)
		return "arm64";
#else
		return "amd64_sysv";
#endif
	}
#ifdef _WIN32
	return "amd64_win";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
	return "arm64_apple";
#elif defined(__APPLE__)
	return "amd64_apple";
#elif defined(__aarch64__) || defined(__arm64__)
	return "arm64";
#else
	return "amd64_sysv";
#endif
}

// Host C++ compiler for foreign .cpp/.cxx/.mm (MODC_CXX / CXX).
static const char*
tool_cxx(void) {
	const char* cxx;
	static char env_buf[HOST_PATH_MAX];

	cxx = getenv("MODC_CXX");
	if (cxx && cxx[0])
		return tool_env_override("MODC_CXX", cxx, env_buf, sizeof(env_buf));
	cxx = getenv("CXX");
	if (cxx && cxx[0])
		return cxx;
#ifdef _WIN32
	return "clang++";
#else
	return "c++";
#endif
}

// True if path looks like a C++/ObjC++ translation unit.
static int
src_is_cxx(const char* path) {
	size_t n;

	if (path == NULL)
		return 0;
	n = strlen(path);
	if (n >= 4 && strcmp(path + n - 4, ".cpp") == 0)
		return 1;
	if (n >= 4 && strcmp(path + n - 4, ".cxx") == 0)
		return 1;
	if (n >= 3 && strcmp(path + n - 3, ".mm") == 0)
		return 1;
	return 0;
}

// True if path is Objective-C or Objective-C++ (.m/.mm).
static int
src_is_objc(const char* path) {
	size_t n;

	if (path == NULL)
		return 0;
	n = strlen(path);
	return n >= 2 && strcmp(path + n - 2, ".m") == 0;
}

// True if any c_sources entry needs the C++ linker driver.
static int
needs_cxx_link(Compiler* c) {
	int i;

	for (i = 0; i < c->paths.csources_len; i++)
		if (src_is_cxx(c->paths.csources[i]))
			return 1;
	return 0;
}

// Copy the parent directory of path into out.
static void
src_dirname(const char* path, char* out, size_t n) {
	host_dirname(path, out, n);
}

// Append one argument to a fixed command vector.
static int
add_arg(const char** argv, int* argc, const char* arg) {
	if (arg == NULL || *argc >= MaxCmdArgs - 1)
		return 1;
	argv[(*argc)++] = arg;
	argv[*argc] = NULL;
	return 0;
}

// Append -I/-D/-F (and system -I) flags used for foreign compiles.
static int
add_compile_flags(Compiler* c, const char** argv, int* argc) {
	int i;

	for (i = 0; i < c->paths.incpaths_len; i++)
		if (add_arg(argv, argc, "-I") || add_arg(argv, argc, c->paths.incpaths[i]))
			return 1;
	if (!c->paths.no_system_includes) {
		for (i = 0; i < c->paths.sysincpaths_len; i++)
			if (add_arg(argv, argc, "-I") ||
			    add_arg(argv, argc, c->paths.sysincpaths[i]))
				return 1;
	}
	for (i = 0; i < c->paths.cli_defs_len; i++)
		if (add_arg(argv, argc, "-D") || add_arg(argv, argc, c->paths.cli_defs[i]))
			return 1;
	for (i = 0; i < c->paths.framework_paths_len; i++)
		if (add_arg(argv, argc, "-F") ||
		    add_arg(argv, argc, c->paths.framework_paths[i]))
			return 1;
	return 0;
}

// Short OS tag mixed into cache keys (windows/darwin/linux/unix).
static const char*
cache_host_os(void) {
#ifdef _WIN32
	return "windows";
#elif defined(__APPLE__)
	return "darwin";
#elif defined(__linux__)
	return "linux";
#else
	return "unix";
#endif
}

/*
 * Shared compile environment for package knobs and foreign .o keys: version,
 * schema, this modc binary, target/OS, cc/cxx/qbe stamps, -D/-I/-F,
 * modc/sys includes, flags. #include <x> can resolve differently while .deps
 * still name the old path. Driver stamp ensures a rebuilt modc (new checks)
 * cannot treat a prior export graph as a check/build hit.
 */
static uint64_t
hash_compile_env(Compiler* c, const char* projroot) {
	uint64_t h;
	char key[HOST_PATH_MAX];
	int i;

	h = cache_hash_str(MODC_VERSION);
	h = cache_hash_mix(h, (uint64_t)ModcCacheSchema);
	/* In-place ./modc rebuilds: path + mtime/size, not only the version string. */
	h = cache_hash_mix(h, cache_hash_tool(c->paths.modc_driver));
	h = cache_hash_mix(h, cache_hash_str(tool_qbe_target(c)));
	h = cache_hash_mix(h, cache_hash_str(cache_host_os()));
	h = cache_hash_mix(h, (uint64_t)c->opt.target + 1);
	/* Path + mtime/size so in-place cc/qbe upgrades invalidate. */
	h = cache_hash_mix(h, cache_hash_tool(tool_cc(c)));
	h = cache_hash_mix(h, cache_hash_tool(tool_cxx()));
	{
		const char* qbe = tool_qbe();
		if (qbe)
			h = cache_hash_mix(h, cache_hash_tool(qbe));
	}
	for (i = 0; i < c->paths.cli_defs_len; i++)
		h = cache_hash_mix(h, cache_hash_str(c->paths.cli_defs[i]));
	for (i = 0; i < c->paths.incpaths_len; i++) {
		cache_path_key(c->paths.incpaths[i], projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	for (i = 0; i < c->paths.framework_paths_len; i++) {
		cache_path_key(c->paths.framework_paths[i], projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	if (c->paths.modc_include && c->paths.modc_include[0]) {
		cache_path_key(c->paths.modc_include, projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	for (i = 0; i < c->paths.sysincpaths_len; i++) {
		cache_path_key(c->paths.sysincpaths[i], projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	h = cache_hash_mix(h, c->paths.no_system_includes ? 1 : 0);
	h = cache_hash_mix(h, c->opt.runtime_check ? 1 : 0);
	return h;
}

/* Foreign .o key: compile env + source identity + which driver builds this file. */
static uint64_t
foreign_obj_key(Compiler* c, const char* src, const char* comp, const char* projroot) {
	uint64_t h;
	char key[HOST_PATH_MAX], srcdir[HOST_PATH_MAX];

	h = hash_compile_env(c, projroot);
	h = cache_mix_file(h, src);
	cache_path_key(src, projroot, key, sizeof(key));
	h = cache_hash_mix(h, cache_hash_str(key));
	src_dirname(src, srcdir, sizeof(srcdir));
	cache_path_key(srcdir, projroot, key, sizeof(key));
	h = cache_hash_mix(h, cache_hash_str(key));
	h = cache_hash_mix(h, cache_hash_tool(comp));
	if (src_is_objc(src))
		h = cache_hash_mix(h, cache_hash_str("-fobjc-arc"));
#ifdef _WIN32
	h = cache_hash_mix(h, cache_hash_str("_CRT_SECURE_NO_WARNINGS"));
#endif
	return h;
}

/* True if csource i was declared by package pkg. */
static int
csource_owned_by(Compiler* c, int i, BuildPkg* pkg) {
	if (c == NULL || pkg == NULL || i < 0 || i >= c->paths.csources_len)
		return 0;
	if (c->paths.csource_pkgs == NULL || c->paths.csource_pkgs[i] == NULL)
		return 0;
	return strcmp(c->paths.csource_pkgs[i], pkg->dir) == 0;
}

/* Content hash of c_sources owned by pkg (0 if none). */
static uint64_t
pkg_csources_hash(Compiler* c, BuildPkg* pkg) {
	uint64_t h;
	int i;

	h = 0;
	if (c == NULL || pkg == NULL)
		return 0;
	for (i = 0; i < c->paths.csources_len; i++) {
		if (!csource_owned_by(c, i, pkg))
			continue;
		h = cache_hash_mix(h, cache_hash_str(c->paths.csources[i]));
		h = cache_mix_file(h, c->paths.csources[i]);
	}
	return h;
}

/* Hash csource paths listed in a space-separated meta string. */
static uint64_t
hash_csource_list(const char* list) {
	char buf[HOST_PATH_MAX];
	const char* p;
	size_t n;
	uint64_t h;

	h = 0;
	if (list == NULL || list[0] == 0)
		return 0;
	for (p = list; *p;) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0)
			break;
		n = 0;
		while (p[n] && p[n] != ' ' && p[n] != '\t')
			n++;
		if (n >= sizeof(buf))
			return 0;
		memcpy(buf, p, n);
		buf[n] = 0;
		h = cache_hash_mix(h, cache_hash_str(buf));
		h = cache_mix_file(h, buf);
		p += n;
	}
	return h;
}

/* Compile one C/C++/ObjC source to out_o. 0 ok. */
static int
compile_one_foreign(Compiler* c, CliOpts* o, const char* src, const char* out_o) {
	const char* argv[MaxCmdArgs];
	const char* comp;
	char srcdir[1024], what[HOST_PATH_MAX];
	int argc, show;

	if (!host_is_file(src)) {
		fprintf(stderr, "modc: c_sources file not found: %s\n", src);
		return 1;
	}
	comp = src_is_cxx(src) ? tool_cxx() : tool_cc(c);
	if (comp == NULL)
		return 1;
	snprintf(what, sizeof(what), "foreign %s", src);
	cache_log(o->verbose, "miss", what);
	/* Avoid nesting under an outer progress_begin ("assembling …"). */
	show = !progress_held;
	if (show)
		progress_begin("compiling %s", host_path_basename(src));
	argc = 0;
	if (add_arg(argv, &argc, comp) || add_arg(argv, &argc, "-c"))
		goto toolong;
#ifdef _WIN32
	if (add_arg(argv, &argc, "-D_CRT_SECURE_NO_WARNINGS"))
		goto toolong;
#endif
	if (add_compile_flags(c, argv, &argc))
		goto toolong;
	src_dirname(src, srcdir, sizeof(srcdir));
	if (add_arg(argv, &argc, "-I") || add_arg(argv, &argc, srcdir))
		goto toolong;
	if (src_is_objc(src) && add_arg(argv, &argc, "-fobjc-arc"))
		goto toolong;
	if (add_arg(argv, &argc, src) || add_arg(argv, &argc, "-o") ||
	    add_arg(argv, &argc, out_o))
		goto toolong;
	if (run_argv(o->verbose, argv) != 0) {
		if (show)
			progress_end();
		return 1;
	}
	if (show)
		progress_end();
	return 0;
toolong:
	if (show)
		progress_end();
	fprintf(stderr, "modc: too many compile arguments\n");
	return 1;
}

/* Relocatable link (ld -r) of nobjs into out. 0 ok. */
static int
relocatable_link(CliOpts* o, const char* out, char objs[][512], int nobjs) {
	const char* argv[MaxCmdArgs];
	int argc, i;

	if (nobjs <= 0)
		return 1;
	if (nobjs == 1)
		return cache_copy_file(objs[0], out);
	argc = 0;
	if (add_arg(argv, &argc, "ld") || add_arg(argv, &argc, "-r") ||
	    add_arg(argv, &argc, "-o") || add_arg(argv, &argc, out)) {
		fprintf(stderr, "modc: too many ld -r arguments\n");
		return 1;
	}
	for (i = 0; i < nobjs; i++)
		if (add_arg(argv, &argc, objs[i])) {
			fprintf(stderr, "modc: too many ld -r arguments\n");
			return 1;
		}
	return run_argv(o->verbose, argv) != 0;
}

/*
 * Compile this package's c_sources and ld -r them with qbe_obj into
 * pkg->objpath (single .pkg.o).
 */
static int
fold_pkg_object(Compiler* c, CliOpts* o, BuildPkg* pkg, const char* qbe_obj,
		const char* dir, int pkg_index) {
	char foreign[MaxForeignObj][512];
	char parts[MaxForeignObj + 1][512];
	int nforeign, nparts, i, fi;

	nforeign = 0;
	for (i = 0; i < c->paths.csources_len; i++) {
		if (!csource_owned_by(c, i, pkg))
			continue;
		if (nforeign >= MaxForeignObj) {
			fprintf(stderr, "modc: too many c_sources files\n");
			return 1;
		}
		snprintf(foreign[nforeign], sizeof(foreign[nforeign]),
			 "%s/pkg%d_f%d.o", dir, pkg_index, nforeign);
		if (compile_one_foreign(c, o, c->paths.csources[i], foreign[nforeign]) !=
		    0)
			return 1;
		nforeign++;
	}
	if (nforeign == 0)
		return cache_copy_file(qbe_obj, pkg->objpath);
	snprintf(parts[0], sizeof(parts[0]), "%s", qbe_obj);
	for (fi = 0; fi < nforeign; fi++)
		snprintf(parts[1 + fi], sizeof(parts[1 + fi]), "%s", foreign[fi]);
	nparts = 1 + nforeign;
	return relocatable_link(o, pkg->objpath, parts, nparts);
}

// Link package objs + foreign objs + libs into outpath.
static int
link_modc_objs(Compiler* c, CliOpts* o, const char* outpath, char objs[][512], int nobjs,
	       char foreign[][512], int nforeign) {
	const char* argv[MaxCmdArgs];
	const char* linker;
	int i, argc, n;

	linker = needs_cxx_link(c) ? tool_cxx() : tool_cc(c);
	if (linker == NULL)
		return 1;
	progress_begin("linking %s", host_path_basename(outpath));
	argc = 0;
	if (add_arg(argv, &argc, linker))
		goto toolong;
	for (n = 0; n < nobjs; n++)
		if (add_arg(argv, &argc, objs[n]))
			goto toolong;
	for (n = 0; n < nforeign; n++)
		if (add_arg(argv, &argc, foreign[n]))
			goto toolong;
	if (add_arg(argv, &argc, "-o") || add_arg(argv, &argc, outpath))
		goto toolong;
	if (!c->paths.no_system_includes) {
		for (i = 0; i < c->paths.syslibpaths_len; i++)
			if (add_arg(argv, &argc, "-L") ||
			    add_arg(argv, &argc, c->paths.syslibpaths[i]))
				goto toolong;
	}
	for (i = 0; i < c->paths.c_libs_len; i++)
		if (add_arg(argv, &argc, "-l") || add_arg(argv, &argc, c->paths.c_libs[i]))
			goto toolong;
	for (i = 0; i < c->paths.frameworks_len; i++)
		if (add_arg(argv, &argc, "-framework") ||
		    add_arg(argv, &argc, c->paths.frameworks[i]))
			goto toolong;
	for (i = 0; i < o->linkargv_len; i++)
		if (add_arg(argv, &argc, o->linkargv[i]))
			goto toolong;
	n = run_argv(o->verbose, argv);
	progress_end();
	return n;

toolong:
	progress_end();
	fprintf(stderr, "modc: too many linker arguments\n");
	return 1;
}

// Unique absolute package roots from the discovered .mc file list.
static int
collect_build_pkgs(char** files, int nfiles, BuildPkg* pkgs, int* npkgs,
		   const char* projroot) {
	int i, j, n;
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX];

	n = 0;
	for (i = 0; i < nfiles; i++) {
		pkg_file_root(files[i], root, sizeof(root));
		if (host_abspath(root, abs, sizeof(abs)) == 0)
			snprintf(root, sizeof(root), "%s", abs);
		for (j = 0; j < n; j++)
			if (strcmp(pkgs[j].dir, root) == 0)
				break;
		if (j < n)
			continue;
		if (n >= MaxCachePkgs)
			return 1;
		memset(&pkgs[n], 0, sizeof(pkgs[n]));
		snprintf(pkgs[n].dir, sizeof(pkgs[n].dir), "%s", root);
		cache_pkg_id(root, projroot, pkgs[n].id, sizeof(pkgs[n].id));
		n++;
	}
	*npkgs = n;
	return 0;
}

/* Mix path identity + mtime(ns) + size (no file bytes). Missing files get a salt. */
static uint64_t
mix_path_stamp(uint64_t h, const char* path, const char* path_key) {
	long long mtime_ns;
	unsigned long long size;

	h = cache_hash_mix(h, cache_hash_str(path_key ? path_key : ""));
	if (path == NULL || host_file_stamp(path, &mtime_ns, &size) != 0)
		return cache_hash_mix(h, cache_hash_str("!missing"));
	h = cache_hash_mix(h, (uint64_t)mtime_ns);
	h = cache_hash_mix(h, (uint64_t)size);
	return h;
}

// Mix *.h under dir (recursive) into an impl hash. Skips symlinks.
// Relpath from pkgroot is mixed so renames invalidate even with identical bytes.
static uint64_t
hash_pkg_headers_rec(const char* dir, const char* pkgroot, uint64_t h, int depth,
		     int stamp_only) {
	HostDir* d;
	const char* name;
	char path[HOST_PATH_MAX];
	size_t n, rootlen;
	const char* key;

	if (depth > 32)
		return h;
	d = host_opendir(dir);
	if (d == NULL)
		return h;
	rootlen = strlen(pkgroot);
	while ((name = host_readdir(d)) != NULL) {
		if (name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, name);
		if (host_is_symlink(path))
			continue;
		if (host_is_dir(path)) {
			h = hash_pkg_headers_rec(path, pkgroot, h, depth + 1, stamp_only);
			continue;
		}
		n = strlen(name);
		if (n < 3 || strcmp(name + n - 2, ".h") != 0)
			continue;
		if (!host_is_file(path))
			continue;
		if (strncmp(path, pkgroot, rootlen) == 0 &&
		    (path[rootlen] == '/' || path[rootlen] == '\\'))
			key = path + rootlen;
		else
			key = path;
		if (stamp_only)
			h = mix_path_stamp(h, path, key);
		else {
			h = cache_hash_mix(h, cache_hash_str(key));
			h = cache_mix_file(h, path);
		}
	}
	host_closedir(d);
	return h;
}

/* True if package lives under projroot/vendor/ (importable third-party tree). */
static int
pkg_is_vendored(const char* pkg_dir, const char* projroot) {
	char key[HOST_PATH_MAX];

	if (pkg_dir == NULL || projroot == NULL || projroot[0] == 0)
		return 0;
	cache_path_key(pkg_dir, projroot, key, sizeof(key));
	return strncmp(key, "vendor/", 7) == 0 || strcmp(key, "vendor") == 0;
}

/* mtime+size stamp over the same inputs as pkg_impl_hash (no content reads). */
static uint64_t
pkg_src_stamp(BuildPkg* pkg, char** files, int nfiles, const char* projroot) {
	uint64_t h;
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX], key[HOST_PATH_MAX];
	int i;

	cache_path_key(pkg->dir, projroot, key, sizeof(key));
	h = cache_hash_str(key);
	for (i = 0; i < nfiles; i++) {
		pkg_file_root(files[i], root, sizeof(root));
		if (host_abspath(root, abs, sizeof(abs)) == 0)
			snprintf(root, sizeof(root), "%s", abs);
		if (strcmp(root, pkg->dir) != 0)
			continue;
		cache_path_key(files[i], projroot, key, sizeof(key));
		h = mix_path_stamp(h, files[i], key);
	}
	h = hash_pkg_headers_rec(pkg->dir, pkg->dir, h, 0, 1);
	return h;
}

/*
 * Lock-only stamp for vendored packages (skip walking vendor sources).
 * Caller must ensure vendor_pkg_rev_matches first. No lock → 0 (no freeze).
 */
static uint64_t
pkg_vendor_stamp(BuildPkg* pkg, const char* projroot) {
	uint64_t h;
	char key[HOST_PATH_MAX], lockpath[HOST_PATH_MAX];

	cache_path_key(pkg->dir, projroot, key, sizeof(key));
	h = cache_hash_mix(cache_hash_str("vendor-freeze"), cache_hash_str(key));
	snprintf(lockpath, sizeof(lockpath), "%s/modc.lock", projroot);
	if (!host_is_file(lockpath))
		return 0;
	h = cache_mix_file(h, lockpath);
	return h;
}

// Content hash of a package implementation (.mc + local .h, nested).
static uint64_t
pkg_impl_hash(BuildPkg* pkg, char** files, int nfiles, const char* projroot) {
	uint64_t h;
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX], key[HOST_PATH_MAX];
	int i;

	cache_path_key(pkg->dir, projroot, key, sizeof(key));
	h = cache_hash_str(key);
	for (i = 0; i < nfiles; i++) {
		pkg_file_root(files[i], root, sizeof(root));
		if (host_abspath(root, abs, sizeof(abs)) == 0)
			snprintf(root, sizeof(root), "%s", abs);
		if (strcmp(root, pkg->dir) != 0)
			continue;
		h = cache_mix_file(h, files[i]);
	}
	h = hash_pkg_headers_rec(pkg->dir, pkg->dir, h, 0, 0);
	return h;
}

// Save headers opened while preprocessing the current ModC graph.
static int
save_modc_deps(Compiler* c, const char* path) {
	char** deps;
	int i, j, n;

	deps = xmalloc((size_t)(c->unit.src_files_len - c->unit.unit_src_start) * sizeof(char*));
	n = 0;
	for (i = c->unit.unit_src_start; i < c->unit.src_files_len; i++) {
		for (j = 0; j < c->unit.unit_files_len; j++)
			if (strcmp(c->unit.src_files[i], c->unit.unit_files[j]) == 0)
				break;
		if (j == c->unit.unit_files_len)
			deps[n++] = c->unit.src_files[i];
	}
	i = cache_write_deps(path, deps, n);
	free(deps);
	return i;
}

/* Parse a 16-digit hex digest written by cache_hash_hex. 0 ok. */
static int
pkg_parse_hex(const char* s, uint64_t* out) {
	char* end;
	unsigned long long v;

	if (s == NULL || s[0] == 0)
		return 1;
	v = strtoull(s, &end, 16);
	if (end == s || *end != 0)
		return 1;
	*out = (uint64_t)v;
	return 0;
}

/* Per-package durable slot: stamp + impl + last-known API digest. */
typedef struct {
	uint64_t stamp;
	uint64_t impl;
	uint64_t iface;
	int have_stamp;
	int have_impl;
	int have_iface;
} PkgMeta;

static void
pkg_meta_path(char* out, size_t out_len, const char* crooot, const char* id) {
	snprintf(out, out_len, "%s/pkg/%s/meta", crooot, id);
}

static void
pkg_key_deps_path(char* out, size_t out_len, const char* crooot, const char* id,
		  const char* hex) {
	snprintf(out, out_len, "%s/pkg/%s-%s/deps", crooot, id, hex);
}

static int
pkg_meta_load(const char* path, PkgMeta* m) {
	char* text;
	char* line;
	char* next;
	size_t n;

	memset(m, 0, sizeof(*m));
	text = read_file(path, &n);
	if (text == NULL)
		return 1;
	for (line = text; line && *line; line = next) {
		char* sp;
		uint64_t v;

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		if (line[0] == 0)
			continue;
		sp = strchr(line, ' ');
		if (sp == NULL)
			continue;
		*sp++ = 0;
		while (*sp == ' ')
			sp++;
		if (pkg_parse_hex(sp, &v) != 0)
			continue;
		if (strcmp(line, "stamp") == 0) {
			m->stamp = v;
			m->have_stamp = 1;
		} else if (strcmp(line, "impl") == 0) {
			m->impl = v;
			m->have_impl = 1;
		} else if (strcmp(line, "iface") == 0) {
			m->iface = v;
			m->have_iface = 1;
		}
	}
	free(text);
	return 0;
}

static int
pkg_meta_save(const char* path, const PkgMeta* m) {
	char buf[128], shex[24], ihex[24], ahex[24];

	cache_hash_hex(m->have_stamp ? m->stamp : 0, shex, sizeof(shex));
	cache_hash_hex(m->have_impl ? m->impl : 0, ihex, sizeof(ihex));
	cache_hash_hex(m->have_iface ? m->iface : 0, ahex, sizeof(ahex));
	snprintf(buf, sizeof(buf), "stamp %s\nimpl %s\niface %s\n", shex, ihex, ahex);
	return cache_write_str(path, buf);
}

/*
 * Prefer a cached impl hash when pkg/<id>/meta stamp still matches.
 * Vendored packages whose .modc-vendor-rev matches modc.lock freeze hashing to
 * the lock (skip walking vendor sources); export/needs/deps still gate hits.
 */
static uint64_t
pkg_impl_hash_cached(BuildPkg* pkg, char** files, int nfiles, const char* projroot,
		     const char* crooot) {
	uint64_t stamp, impl;
	char metapath[HOST_PATH_MAX];
	PkgMeta meta;
	int frozen;

	frozen = 0;
	stamp = 0;
	if (pkg->vendored && vendor_pkg_rev_matches(projroot, pkg->dir) == 0) {
		stamp = pkg_vendor_stamp(pkg, projroot);
		if (stamp != 0)
			frozen = 1;
	}
	if (!frozen)
		stamp = pkg_src_stamp(pkg, files, nfiles, projroot);
	pkg_meta_path(metapath, sizeof(metapath), crooot, pkg->id);
	(void)pkg_meta_load(metapath, &meta);
	if (meta.have_stamp && meta.have_impl && meta.stamp == stamp)
		return meta.impl;
	impl = pkg_impl_hash(pkg, files, nfiles, projroot);
	meta.stamp = stamp;
	meta.impl = impl;
	meta.have_stamp = 1;
	meta.have_impl = 1;
	/* Preserve last-known iface across stamp/impl refresh. */
	(void)pkg_meta_save(metapath, &meta);
	return impl;
}

/* True if any transitive import still has an unknown API digest. */
static int
pkg_needs_incomplete(BuildPkg* pkgs, int npkgs, int i) {
	int j;

	for (j = 0; j < npkgs; j++) {
		if (i == j || !pkgs[i].reach[j])
			continue;
		if (pkgs[j].iface == 0)
			return 1;
	}
	return 0;
}

// needs = mix of transitive import API (export) digests.
static uint64_t
pkg_needs_hash(BuildPkg* pkgs, int npkgs, int i) {
	uint64_t needsh;
	int j;

	needsh = 0;
	for (j = 0; j < npkgs; j++) {
		if (i == j || !pkgs[i].reach[j])
			continue;
		needsh = cache_hash_mix(needsh, pkgs[j].iface);
	}
	return needsh;
}

/* Resolve iface: hash per-package .pkg.mc when present. */
static void
pkg_resolve_ifaces(BuildPkg* pkgs, int npkgs) {
	int i;

	for (i = 0; i < npkgs; i++) {
		pkgs[i].iface = 0;
		if (host_is_file(pkgs[i].ifacepath))
			pkgs[i].iface = cache_hash_file(pkgs[i].ifacepath);
	}
}

/* Frontend hit: .pkg.mc + .pkg.meta [build]/[needs]/csrc. require_obj also needs .pkg.o. */
static void
pkg_refresh_hits(BuildPkg* pkgs, int npkgs, uint64_t knobs, int require_obj) {
	int i, j, di, ok;
	IniFile ini;
	IniSect* build;
	IniSect* needs;
	IniSect* link;
	const char* v;
	uint64_t stored, csrc;

	for (i = 0; i < npkgs; i++) {
		pkgs[i].hit = 0;
		if (!host_is_file(pkgs[i].ifacepath))
			continue;
		if (require_obj && !host_is_file(pkgs[i].objpath))
			continue;
		memset(&ini, 0, sizeof(ini));
		if (ini_load(&ini, pkgs[i].metapath) != 0)
			continue;
		build = ini_find_sect(&ini, "build");
		if (build == NULL) {
			ini_free(&ini);
			continue;
		}
		v = ini_get(build, "knobs");
		if (v == NULL || pkg_parse_hex(v, &stored) != 0 || stored != knobs) {
			ini_free(&ini);
			continue;
		}
		v = ini_get(build, "src");
		if (v == NULL || pkg_parse_hex(v, &stored) != 0 || stored != pkgs[i].impl) {
			ini_free(&ini);
			continue;
		}
		v = ini_get(build, "iface");
		if (v == NULL || pkg_parse_hex(v, &stored) != 0 ||
		    stored != pkgs[i].iface) {
			ini_free(&ini);
			continue;
		}
		/* Folded c_sources: re-hash listed paths vs build.csrc. */
		link = ini_find_sect(&ini, "link");
		csrc = 0;
		v = ini_get(build, "csrc");
		if (v != NULL && pkg_parse_hex(v, &csrc) != 0) {
			ini_free(&ini);
			continue;
		}
		if (hash_csource_list(link ? ini_get(link, "csources") : NULL) != csrc) {
			ini_free(&ini);
			continue;
		}
		needs = ini_find_sect(&ini, "needs");
		ok = 1;
		for (j = 0; j < pkgs[i].nimports; j++) {
			di = pkgs[i].imports[j];
			if (pkgs[di].iface == 0) {
				ok = 0;
				break;
			}
			if (needs == NULL) {
				ok = 0;
				break;
			}
			v = ini_get(needs, pkgs[di].id);
			if (v == NULL || pkg_parse_hex(v, &stored) != 0 ||
			    stored != pkgs[di].iface) {
				ok = 0;
				break;
			}
		}
		ini_free(&ini);
		if (!ok)
			continue;
		pkgs[i].hit = 1;
	}
}

/*
 * True when pkg_dir is under modc_pkg (installed or in-tree stdlib root).
 * Uses realpath/abspath so symlink installs still match.
 */
static int
pkg_under_modc_pkg(Compiler* c, const char* pkg_dir) {
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX];
	size_t n;

	if (c == NULL || c->paths.modc_pkg == NULL || c->paths.modc_pkg[0] == 0 ||
	    pkg_dir == NULL || pkg_dir[0] == 0)
		return 0;
	if (host_realpath(c->paths.modc_pkg, root, sizeof(root)) != 0) {
		if (host_abspath(c->paths.modc_pkg, root, sizeof(root)) != 0)
			snprintf(root, sizeof(root), "%s", c->paths.modc_pkg);
	}
	if (host_realpath(pkg_dir, abs, sizeof(abs)) != 0) {
		if (host_abspath(pkg_dir, abs, sizeof(abs)) != 0)
			snprintf(abs, sizeof(abs), "%s", pkg_dir);
	}
	n = strlen(root);
	while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\'))
		root[--n] = 0;
	if (n == 0 || strncmp(abs, root, n) != 0)
		return 0;
	return abs[n] == 0 || host_path_is_sep((unsigned char)abs[n]);
}

/*
 * Shipped stdlib hit: under modc_pkg, artifacts present, package dir not
 * writable. Skips knob/meta rewrite so root-owned installs stay usable when
 * the driver's knobs differ from the stamped .pkg.meta (cc/qbe path, etc.).
 */
static int
pkg_stdlib_readonly_hit(Compiler* c, BuildPkg* pkg, int require_obj) {
	if (pkg == NULL || !pkg_under_modc_pkg(c, pkg->dir))
		return 0;
	if (!host_is_file(pkg->ifacepath))
		return 0;
	if (require_obj && !host_is_file(pkg->objpath))
		return 0;
	if (host_access_write(pkg->dir) == 0)
		return 0;
	return 1;
}

static void
pkg_fill_keys(Compiler* c, BuildPkg* pkgs, int npkgs, uint64_t knobs, int require_obj) {
	int i;
	uint64_t h;

	for (i = 0; i < npkgs; i++) {
		h = cache_hash_mix(pkgs[i].impl, knobs);
		pkgs[i].key = h;
		cache_hash_hex(h, pkgs[i].hex, sizeof(pkgs[i].hex));
		(void)pkg_artifact_paths(pkgs[i].dir, pkgs[i].ifacepath,
					 sizeof(pkgs[i].ifacepath), pkgs[i].objpath,
					 sizeof(pkgs[i].objpath), pkgs[i].metapath,
					 sizeof(pkgs[i].metapath));
	}
	pkg_resolve_ifaces(pkgs, npkgs);
	pkg_refresh_hits(pkgs, npkgs, knobs, require_obj);
	for (i = 0; i < npkgs; i++) {
		if (!pkgs[i].hit && pkg_stdlib_readonly_hit(c, &pkgs[i], require_obj))
			pkgs[i].hit = 1;
		pkgs[i].need_emit = !pkgs[i].hit;
	}
}

// Record direct imports and transitive reachability among build packages.
static int
pkg_fill_imports(Compiler* c, BuildPkg* pkgs, int npkgs, char** files, int nfiles) {
	int i, j, k, fi, nroots, idx;
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX];
	char** roots;
	int changed;

	for (i = 0; i < npkgs; i++) {
		pkgs[i].nimports = 0;
		memset(pkgs[i].reach, 0, sizeof(pkgs[i].reach));
	}
	for (fi = 0; fi < nfiles; fi++) {
		pkg_file_root(files[fi], root, sizeof(root));
		if (host_abspath(root, abs, sizeof(abs)) == 0)
			snprintf(root, sizeof(root), "%s", abs);
		idx = -1;
		for (i = 0; i < npkgs; i++)
			if (strcmp(pkgs[i].dir, root) == 0) {
				idx = i;
				break;
			}
		if (idx < 0)
			continue;
		roots = NULL;
		nroots = 0;
		if (pkg_import_roots(c, files[fi], &roots, &nroots))
			return 1;
		for (j = 0; j < nroots; j++) {
			for (k = 0; k < npkgs; k++) {
				if (strcmp(pkgs[k].dir, roots[j]) != 0)
					continue;
				if (k == idx)
					break;
				for (i = 0; i < pkgs[idx].nimports; i++)
					if (pkgs[idx].imports[i] == k)
						break;
				if (i == pkgs[idx].nimports) {
					if (pkgs[idx].nimports >= MaxCachePkgs) {
						fprintf(stderr,
							"modc: too many imports for package %s\n",
							pkgs[idx].id);
						for (; j < nroots; j++)
							free(roots[j]);
						free(roots);
						return 1;
					}
					pkgs[idx].imports[pkgs[idx].nimports++] = k;
				}
				break;
			}
			free(roots[j]);
		}
		free(roots);
	}
	/* Transitive closure of imports. */
	for (i = 0; i < npkgs; i++)
		for (j = 0; j < pkgs[i].nimports; j++)
			pkgs[i].reach[pkgs[i].imports[j]] = 1;
	do {
		changed = 0;
		for (i = 0; i < npkgs; i++)
			for (j = 0; j < npkgs; j++) {
				if (!pkgs[i].reach[j])
					continue;
				for (k = 0; k < npkgs; k++) {
					if (!pkgs[j].reach[k] || pkgs[i].reach[k] || k == i)
						continue;
					pkgs[i].reach[k] = 1;
					changed = 1;
				}
			}
	} while (changed);
	return 0;
}

// Whole-graph stamp over package impl hashes and knobs.
static uint64_t
graph_digest(BuildPkg* pkgs, int npkgs, uint64_t knobs) {
	uint64_t h;
	int i;

	h = knobs;
	for (i = 0; i < npkgs; i++)
		h = cache_hash_mix(h, pkgs[i].impl);
	return h;
}

// Append snprintf into a growing heap buffer. Returns 0 ok.
static int
link_meta_printf(char** buf, size_t* off, size_t* cap, const char* fmt, ...) {
	va_list ap;
	int n;
	size_t need;

	for (;;) {
		va_start(ap, fmt);
		n = vsnprintf(*buf ? *buf + *off : NULL, *buf ? *cap - *off : 0, fmt, ap);
		va_end(ap);
		if (n < 0)
			return 1;
		need = *off + (size_t)n + 1;
		if (*buf && need <= *cap) {
			*off += (size_t)n;
			return 0;
		}
		if (*cap == 0)
			*cap = 256;
		while (*cap < need)
			*cap *= 2;
		*buf = xrealloc(*buf, *cap);
	}
}

// Persist c_libs/frameworks/csources for a later all-hit link (heap, no 64KiB cap).
static int
save_link_meta(Compiler* c, CliOpts* o, const char* path) {
	char* buf;
	size_t off, cap;
	int i;

	buf = NULL;
	off = 0;
	cap = 0;
	for (i = 0; i < c->paths.c_libs_len; i++)
		if (link_meta_printf(&buf, &off, &cap, "L %s\n", c->paths.c_libs[i]))
			goto fail;
	for (i = 0; i < c->paths.frameworks_len; i++)
		if (link_meta_printf(&buf, &off, &cap, "F %s\n", c->paths.frameworks[i]))
			goto fail;
	for (i = 0; i < c->paths.csources_len; i++)
		if (link_meta_printf(&buf, &off, &cap, "S %s\n", c->paths.csources[i]))
			goto fail;
	for (i = 0; i < o->linkargv_len; i++)
		if (link_meta_printf(&buf, &off, &cap, "X %s\n", o->linkargv[i]))
			goto fail;
	if (cache_write_bytes(path, buf ? buf : "", off) != 0) {
		fprintf(stderr, "modc: cannot write linkmeta %s\n", path);
		free(buf);
		return 1;
	}
	free(buf);
	return 0;
fail:
	fprintf(stderr, "modc: linkmeta too large to format\n");
	free(buf);
	return 1;
}

/* Record an absolute c_sources path (meta reload). owner may be NULL. */
static void
add_csource_abs(Compiler* c, const char* path, const char* owner) {
	char abs[HOST_PATH_MAX];
	size_t n;
	int i;

	if (path == NULL || path[0] == 0 || path[0] == '-')
		return;
	n = strlen(path);
	if (n < 2)
		return;
	/* Same extensions as pkg_add_csource. */
	if (!(n >= 2 && (strcmp(path + n - 2, ".c") == 0 || strcmp(path + n - 2, ".m") == 0)) &&
	    !(n >= 3 && strcmp(path + n - 3, ".mm") == 0) &&
	    !(n >= 4 && (strcmp(path + n - 4, ".cpp") == 0 || strcmp(path + n - 4, ".cxx") == 0)))
		return;
	snprintf(abs, sizeof(abs), "%s", path);
	if (c->paths.project_root[0] &&
	    host_realpath(path, abs, sizeof(abs)) == 0) {
		char root[HOST_PATH_MAX];
		size_t rn;

		if (host_realpath(c->paths.project_root, root, sizeof(root)) != 0)
			return;
		rn = strlen(root);
		if (strncmp(root, abs, rn) != 0 ||
		    (abs[rn] != 0 && !host_path_is_sep((unsigned char)abs[rn])))
			return;
	}
	for (i = 0; i < c->paths.csources_len; i++)
		if (strcmp(c->paths.csources[i], abs) == 0)
			return;
	if (c->paths.csources_len % 8 == 0) {
		c->paths.csources =
			xrealloc(c->paths.csources, (c->paths.csources_len + 8) * sizeof(char*));
		c->paths.csource_pkgs = xrealloc(c->paths.csource_pkgs,
						 (c->paths.csources_len + 8) * sizeof(char*));
	}
	c->paths.csource_pkgs[c->paths.csources_len] =
		owner && owner[0] ? xstrdup(owner) : NULL;
	c->paths.csources[c->paths.csources_len++] = xstrdup(abs);
}

// Append L/F/S lines from a prior linkmeta (does not clear existing lists).
static int
merge_link_meta(Compiler* c, const char* path) {
	char* text;
	size_t n, i, start;
	char line[HOST_PATH_MAX + 4];

	text = read_file(path, &n);
	if (text == NULL)
		return 1;
	i = 0;
	while (i < n) {
		start = i;
		while (i < n && text[i] != '\n' && text[i] != '\r')
			i++;
		if (i - start >= sizeof(line)) {
			free(text);
			return 1;
		}
		memcpy(line, text + start, i - start);
		line[i - start] = 0;
		while (i < n && (text[i] == '\n' || text[i] == '\r'))
			i++;
		if (line[0] == 'L' && line[1] == ' ')
			pkg_add_clib(c, line + 2);
		else if (line[0] == 'F' && line[1] == ' ')
			pkg_add_framework(c, line + 2);
		else if (line[0] == 'S' && line[1] == ' ')
			add_csource_abs(c, line + 2, NULL);
	}
	free(text);
	return 0;
}

// Replace link pragmas from a cached linkmeta file.
static int
load_link_meta(Compiler* c, const char* path) {
	c->paths.c_libs_len = 0;
	c->paths.frameworks_len = 0;
	c->paths.csources_len = 0;
	return merge_link_meta(c, path);
}

// Fingerprint of link inputs for the prog stamp (skip-link short-circuit).
static uint64_t
link_stamp_hash(Compiler* c, CliOpts* o, BuildPkg* pkgs, int npkgs,
		const char* outpath, const char* projroot) {
	uint64_t linkh;
	int i;

	linkh = cache_hash_str(outpath);
	for (i = 0; i < npkgs; i++)
		linkh = cache_hash_mix(linkh, pkgs[i].key);
	for (i = 0; i < c->paths.csources_len; i++)
		linkh = cache_hash_mix(
			linkh, foreign_obj_key(c, c->paths.csources[i],
					       src_is_cxx(c->paths.csources[i])
						       ? tool_cxx()
						       : tool_cc(c),
					       projroot));
	for (i = 0; i < c->paths.c_libs_len; i++)
		linkh = cache_hash_mix(linkh, cache_hash_str(c->paths.c_libs[i]));
	for (i = 0; i < c->paths.frameworks_len; i++)
		linkh = cache_hash_mix(linkh, cache_hash_str(c->paths.frameworks[i]));
	for (i = 0; i < o->linkargv_len; i++)
		linkh = cache_hash_mix(linkh, cache_hash_str(o->linkargv[i]));
	return linkh;
}

// Build QBE string-pool symbol for a package cache key.
static void
pkg_strsym(BuildPkg* pkg, char* out, size_t out_len) {
	int i, j;
	char id[96];

	j = 0;
	for (i = 0; pkg->id[i] && j < (int)sizeof(id) - 1; i++) {
		unsigned char ch = (unsigned char)pkg->id[i];
		if (isalnum(ch))
			id[j++] = (char)ch;
		else
			id[j++] = '_';
	}
	id[j] = 0;
	snprintf(out, out_len, "__string_%s_%.8s", id, pkg->hex);
}

// Write one package's QBE IL (no assemble/compile).
static int
emit_pkg_qbe(Compiler* c, BuildPkg* pkg, int pkg_index, const char* dir, char* qbe,
	     size_t qbe_len) {
	char strsym[128];
	FILE* f;

	snprintf(qbe, qbe_len, "%s/pkg%d.qbe", dir, pkg_index);
	pkg_strsym(pkg, strsym, sizeof(strsym));
	f = fopen(qbe, "w");
	if (f == NULL) {
		fprintf(stderr, "modc: cannot write %s: %s\n", qbe, strerror(errno));
		return 1;
	}
	emit_qbe_pkg(c, f, pkg->dir, strsym);
	if (fclose(f) != 0) {
		fprintf(stderr, "modc: cannot finish %s: %s\n", qbe, strerror(errno));
		(void)host_unlink(qbe);
		return 1;
	}
	return c->diag.error_count ? 1 : 0;
}

typedef struct {
	BuildPkg* pkg;
	int pkg_index;
	char qbe[512];
	char asmpath[512];
	char obj[512];
} PkgAsmJob;

// Log argv like run_argv when verbose.
static void
log_argv(int verbose, const char* const argv[]) {
	int i;

	if (!verbose)
		return;
	fputc('+', stderr);
	for (i = 0; argv[i]; i++) {
		fputc(' ', stderr);
		print_arg(argv[i]);
	}
	fputc('\n', stderr);
}

// Spawn argv asynchronously; logs when verbose. Returns HostPid or -1.
static HostPid
spawn_argv(int verbose, const char* const argv[]) {
	HostPid pid;

	log_argv(verbose, argv);
	pid = host_spawn_async(argv);
	if (pid < 0)
		fprintf(stderr, "modc: failed to run command: %s\n", strerror(errno));
	return pid;
}

// Wait for async spawn; non-zero exit → 1.
static int
wait_argv(HostPid pid) {
	int st;

	st = host_wait_pid(pid);
	if (st != 0) {
		if (st == -1)
			fprintf(stderr, "modc: failed to run command: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

/* Reap already-started async children after a mid-batch spawn failure. */
static void
reap_pids(HostPid* pids, int n) {
	int i;

	for (i = 0; i < n; i++) {
		if (pids[i] < 0)
			continue;
		(void)host_wait_pid(pids[i]);
		pids[i] = -1;
	}
}

// Job parallelism: MODC_JOBS, else host_ncpu(), capped.
static int
job_parallelism(int nwork) {
	const char* env;
	int n, jobs;

	if (nwork <= 1)
		return 1;
	env = getenv("MODC_JOBS");
	if (env && env[0]) {
		jobs = atoi(env);
		if (jobs < 1)
			jobs = 1;
	} else {
		jobs = host_ncpu();
		if (jobs < 1)
			jobs = 1;
	}
	if (jobs > nwork)
		jobs = nwork;
	if (jobs > 32)
		jobs = 32;
	return jobs;
}

/*
 * Assemble+compile jobs in parallel: barrier per stage (all qbe, then all cc),
 * then fold each package's c_sources into .pkg.o via ld -r.
 * Caller prints per-package "assembling" progress before emit.
 */
static int
assemble_pkg_jobs(Compiler* c, CliOpts* o, PkgAsmJob* jobs, int njobs, const char* dir) {
	const char* qbe_tool;
	const char* target;
	const char* cc;
	const char* argv[8];
	HostPid pids[MaxCachePkgs];
	int i, j, batch, jobs_n, st;

	if (njobs <= 0)
		return 0;
	qbe_tool = tool_qbe();
	if (qbe_tool == NULL)
		return 1;
	target = tool_qbe_target(c);
	cc = tool_cc(c);
	if (cc == NULL)
		return 1;
	jobs_n = job_parallelism(njobs);
	for (i = 0; i < njobs; i += jobs_n) {
		batch = njobs - i;
		if (batch > jobs_n)
			batch = jobs_n;
		for (j = 0; j < batch; j++)
			pids[j] = -1;
		for (j = 0; j < batch; j++) {
			argv[0] = qbe_tool;
			argv[1] = "-t";
			argv[2] = target;
			argv[3] = "-o";
			argv[4] = jobs[i + j].asmpath;
			argv[5] = jobs[i + j].qbe;
			argv[6] = NULL;
			pids[j] = spawn_argv(o->verbose, argv);
			if (pids[j] < 0) {
				reap_pids(pids, j);
				return 1;
			}
		}
		st = 0;
		for (j = 0; j < batch; j++)
			if (wait_argv(pids[j]) != 0)
				st = 1;
		if (st)
			return 1;
		for (j = 0; j < batch; j++)
			pids[j] = -1;
		for (j = 0; j < batch; j++) {
			argv[0] = cc;
			argv[1] = "-c";
			argv[2] = jobs[i + j].asmpath;
			argv[3] = "-o";
			argv[4] = jobs[i + j].obj;
			argv[5] = NULL;
			pids[j] = spawn_argv(o->verbose, argv);
			if (pids[j] < 0) {
				reap_pids(pids, j);
				return 1;
			}
		}
		st = 0;
		for (j = 0; j < batch; j++)
			if (wait_argv(pids[j]) != 0)
				st = 1;
		if (st)
			return 1;
		for (j = 0; j < batch; j++) {
			if (fold_pkg_object(c, o, jobs[i + j].pkg, jobs[i + j].obj, dir,
					    jobs[i + j].pkg_index) != 0)
				return 1;
			jobs[i + j].pkg->hit = 1;
		}
	}
	return 0;
}

static void add_csource_abs(Compiler* c, const char* path, const char* owner);

/* Packages without need_emit + .pkg.mc → load; need_emit sources → typecheck.
 * tc_files aliases entries in files[]; exps/exp_dirs are xstrdup'd. Caller frees. */
static int
collect_export_tc(BuildPkg* pkgs, int npkgs, char** files, int nfiles, char*** out_exps,
		  char*** out_exp_dirs, int* out_nexp, char*** out_tc, int* out_ntc) {
	char** exps;
	char** exp_dirs;
	char** tc_files;
	int nexp, ntc, pi, fi;
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX];

	exps = xmalloc((size_t)npkgs * sizeof(char*));
	exp_dirs = xmalloc((size_t)npkgs * sizeof(char*));
	nexp = 0;
	for (pi = 0; pi < npkgs; pi++) {
		if (pkgs[pi].need_emit)
			continue;
		if (!host_is_file(pkgs[pi].ifacepath))
			continue;
		exps[nexp] = xstrdup(pkgs[pi].ifacepath);
		exp_dirs[nexp] = xstrdup(pkgs[pi].dir);
		nexp++;
	}
	tc_files = xmalloc((size_t)nfiles * sizeof(char*));
	ntc = 0;
	for (fi = 0; fi < nfiles; fi++) {
		pkg_file_root(files[fi], root, sizeof(root));
		if (host_abspath(root, abs, sizeof(abs)) == 0)
			snprintf(root, sizeof(root), "%s", abs);
		for (pi = 0; pi < npkgs; pi++) {
			if (strcmp(pkgs[pi].dir, root) != 0)
				continue;
			if (pkgs[pi].need_emit)
				tc_files[ntc++] = files[fi];
			break;
		}
		if (pi >= npkgs)
			tc_files[ntc++] = files[fi];
	}
	*out_exps = exps;
	*out_exp_dirs = exp_dirs;
	*out_nexp = nexp;
	*out_tc = tc_files;
	*out_ntc = ntc;
	return 0;
}

/* Join string list with spaces for .pkg.meta [link] values. Caller frees. */
static char*
join_space_list(char** items, int n) {
	size_t len;
	int i;
	char* out;
	char* p;

	len = 1;
	for (i = 0; i < n; i++)
		len += strlen(items[i]) + (i ? 1 : 0);
	out = xmalloc(len);
	p = out;
	for (i = 0; i < n; i++) {
		size_t L;

		if (i)
			*p++ = ' ';
		L = strlen(items[i]);
		memcpy(p, items[i], L);
		p += L;
	}
	*p = 0;
	return out;
}

/* Space-join c_sources owned by pkg. Caller frees. */
static char*
join_pkg_csources(Compiler* c, BuildPkg* pkg) {
	char** items;
	int i, n;
	char* out;

	n = 0;
	for (i = 0; i < c->paths.csources_len; i++)
		if (csource_owned_by(c, i, pkg))
			n++;
	if (n == 0)
		return xstrdup("");
	items = xmalloc((size_t)n * sizeof(char*));
	n = 0;
	for (i = 0; i < c->paths.csources_len; i++)
		if (csource_owned_by(c, i, pkg))
			items[n++] = c->paths.csources[i];
	out = join_space_list(items, n);
	free(items);
	return out;
}

/* Append space-separated names via pkg_add_clib / pkg_add_framework. */
static void
merge_space_clibs(Compiler* c, const char* s) {
	char buf[HOST_PATH_MAX];
	const char* p;
	size_t n;

	if (s == NULL)
		return;
	for (p = s; *p;) {
		while (*p == ' ')
			p++;
		if (*p == 0)
			break;
		n = 0;
		while (p[n] && p[n] != ' ' && n + 1 < sizeof(buf))
			n++;
		if (n == 0)
			break;
		memcpy(buf, p, n);
		buf[n] = 0;
		(void)pkg_add_clib(c, buf);
		p += n;
	}
}

static void
merge_space_frameworks(Compiler* c, const char* s) {
	char buf[HOST_PATH_MAX];
	const char* p;
	size_t n;

	if (s == NULL)
		return;
	for (p = s; *p;) {
		while (*p == ' ')
			p++;
		if (*p == 0)
			break;
		n = 0;
		while (p[n] && p[n] != ' ' && n + 1 < sizeof(buf))
			n++;
		if (n == 0)
			break;
		memcpy(buf, p, n);
		buf[n] = 0;
		(void)pkg_add_framework(c, buf);
		p += n;
	}
}

/* owner is the package dir that declared these paths (for fold attribution). */
static void
merge_space_csources(Compiler* c, const char* s, const char* owner) {
	char buf[HOST_PATH_MAX];
	const char* p;
	size_t n;

	if (s == NULL)
		return;
	for (p = s; *p;) {
		while (*p == ' ')
			p++;
		if (*p == 0)
			break;
		n = 0;
		while (p[n] && p[n] != ' ' && n + 1 < sizeof(buf))
			n++;
		if (n == 0)
			break;
		memcpy(buf, p, n);
		buf[n] = 0;
		add_csource_abs(c, buf, owner);
		p += n;
	}
}

/* Union [link] libs/frameworks/csources from one .pkg.meta into c->paths. */
static void
pkg_merge_link_meta(Compiler* c, const char* metapath, const char* pkg_dir) {
	IniFile ini;
	IniSect* link;

	if (metapath == NULL || !host_is_file(metapath))
		return;
	memset(&ini, 0, sizeof(ini));
	if (ini_load(&ini, metapath) != 0)
		return;
	link = ini_find_sect(&ini, "link");
	if (link) {
		merge_space_clibs(c, ini_get(link, "libs"));
		merge_space_frameworks(c, ini_get(link, "frameworks"));
		/* Keep paths for needs_cxx_link; objects are already in .pkg.o. */
		merge_space_csources(c, ini_get(link, "csources"), pkg_dir);
	}
	ini_free(&ini);
}

static void
pkgs_merge_link_metas(Compiler* c, BuildPkg* pkgs, int npkgs) {
	int i;

	for (i = 0; i < npkgs; i++)
		pkg_merge_link_meta(c, pkgs[i].metapath, pkgs[i].dir);
}

/* Write .pkg.meta for package i after iface rebuild (final iface digests). */
static int
pkg_write_build_meta(Compiler* c, BuildPkg* pkgs, int npkgs, int i, uint64_t knobs) {
	IniFile ini;
	char hex[24];
	char* joined;
	int j, di, r;
	uint64_t csrc;

	memset(&ini, 0, sizeof(ini));
	if (ini_set(&ini, "build", "version", "1") != 0)
		goto fail;
	cache_hash_hex(knobs, hex, sizeof(hex));
	if (ini_set(&ini, "build", "knobs", hex) != 0)
		goto fail;
	cache_hash_hex(pkgs[i].impl, hex, sizeof(hex));
	if (ini_set(&ini, "build", "src", hex) != 0)
		goto fail;
	cache_hash_hex(pkgs[i].iface, hex, sizeof(hex));
	if (ini_set(&ini, "build", "iface", hex) != 0)
		goto fail;
	csrc = pkg_csources_hash(c, &pkgs[i]);
	cache_hash_hex(csrc, hex, sizeof(hex));
	if (ini_set(&ini, "build", "csrc", hex) != 0)
		goto fail;
	for (j = 0; j < pkgs[i].nimports; j++) {
		di = pkgs[i].imports[j];
		cache_hash_hex(pkgs[di].iface, hex, sizeof(hex));
		if (ini_set(&ini, "needs", pkgs[di].id, hex) != 0)
			goto fail;
	}
	joined = join_space_list(c->paths.c_libs, c->paths.c_libs_len);
	if (ini_set(&ini, "link", "libs", joined) != 0) {
		free(joined);
		goto fail;
	}
	free(joined);
	joined = join_space_list(c->paths.frameworks, c->paths.frameworks_len);
	if (ini_set(&ini, "link", "frameworks", joined) != 0) {
		free(joined);
		goto fail;
	}
	free(joined);
	joined = join_pkg_csources(c, &pkgs[i]);
	if (ini_set(&ini, "link", "csources", joined) != 0) {
		free(joined);
		goto fail;
	}
	free(joined);
	r = ini_save(&ini, pkgs[i].metapath);
	ini_free(&ini);
	return r;
fail:
	ini_free(&ini);
	return 1;
}

/* True if outpath exists and is at least as new as every package .pkg.o. */
static int
outfile_newer_than_objs(const char* outpath, BuildPkg* pkgs, int npkgs) {
	long long out_m, obj_m;
	unsigned long long sz;
	int i;

	if (host_file_stamp(outpath, &out_m, &sz) != 0)
		return 0;
	for (i = 0; i < npkgs; i++) {
		if (host_file_stamp(pkgs[i].objpath, &obj_m, &sz) != 0)
			return 0;
		if (obj_m > out_m)
			return 0;
	}
	return 1;
}

/* Drop symbols/types between cascade recompiles; keep link pragma lists. */
static void
reset_frontend_state(Compiler* c) {
	c->lex.tokens = NULL;
	c->lex.tokens_len = 0;
	c->lex.tokens_cap = 0;
	c->lex.pos = 0;
	c->diag.error_count = 0;
	c->diag.fatal = 0;
	c->syms.symbols = NULL;
	free(c->syms.symbol_tab);
	c->syms.symbol_tab = NULL;
	c->syms.symbol_tab_cap = 0;
	c->syms.symbols_len = 0;
	c->syms.block = 0;
	c->unit.funcs = NULL;
	c->unit.funcs_len = 0;
	c->unit.funcs_cap = 0;
	c->unit.globals = NULL;
	c->unit.globals_len = 0;
	c->unit.globals_cap = 0;
	c->syms.current_fn = NULL;
	c->types.type_list = NULL;
	free(c->types.ranged_tab);
	c->types.ranged_tab = NULL;
	c->types.ranged_tab_cap = 0;
	c->types.ranged_len = 0;
	free(c->types.tuple_tab);
	c->types.tuple_tab = NULL;
	c->types.tuple_tab_cap = 0;
	c->types.tuple_len = 0;
	c->unit.unit_files = NULL;
	c->unit.unit_files_len = 0;
}

/*
 * Typecheck need_emit packages (load .pkg.mc for the rest). Cascade when a
 * rebuilt package's iface/API digest changes. Write .pkg.mc + .pkg.meta.
 * Does not emit objects. Logs hit/miss per package. 0 ok.
 */
static int
pkgs_frontend_compile(Compiler* c, CliOpts* o, BuildPkg* pkgs, int npkgs,
		      char** files, int nfiles, uint64_t knobs, const char* projroot) {
	char** tc_files;
	char** exps;
	char** exp_dirs;
	int ntc, nexp, pi, demoted, pass, i, r;
	uint64_t iface_at_start[MaxCachePkgs];
	char what[HOST_PATH_MAX];
	const char* verb;

	verb = o->check_only ? "checking" : "compiling";
	for (pi = 0; pi < npkgs; pi++)
		iface_at_start[pi] = pkgs[pi].iface;

	r = 0;
	for (pass = 0; pass < MaxCachePkgs; pass++) {
		if (pass > 0)
			reset_frontend_state(c);
		collect_export_tc(pkgs, npkgs, files, nfiles, &exps, &exp_dirs, &nexp,
				  &tc_files, &ntc);
		if (ntc == 0) {
			for (pi = 0; pi < nexp; pi++) {
				free(exps[pi]);
				free(exp_dirs[pi]);
			}
			free(exps);
			free(exp_dirs);
			free(tc_files);
			break;
		}
		r = compile_graph(c, tc_files, ntc, exps, exp_dirs, nexp, verb, projroot);
		for (pi = 0; pi < nexp; pi++) {
			free(exps[pi]);
			free(exp_dirs[pi]);
		}
		free(exps);
		free(exp_dirs);
		free(tc_files);
		if (r != 0)
			return 1;
		demoted = 0;
		for (pi = 0; pi < npkgs; pi++) {
			if (!pkgs[pi].need_emit)
				continue;
			if (iface_write_pkg(c, pkgs[pi].dir, pkgs[pi].ifacepath) != 0) {
				fprintf(stderr, "modc: failed to write iface for %s\n",
					pkgs[pi].id);
				return 1;
			}
			pkgs[pi].iface = cache_hash_file(pkgs[pi].ifacepath);
		}
		for (pi = 0; pi < npkgs; pi++) {
			int k;

			if (!pkgs[pi].need_emit)
				continue;
			if (pkgs[pi].iface == iface_at_start[pi])
				continue;
			for (k = 0; k < npkgs; k++) {
				if (k == pi || !pkgs[k].reach[pi])
					continue;
				if (pkgs[k].need_emit)
					continue;
				pkgs[k].need_emit = 1;
				pkgs[k].hit = 0;
				demoted = 1;
			}
		}
		if (!demoted)
			break;
	}
	if (pass >= MaxCachePkgs) {
		fprintf(stderr, "modc: package API cascade did not converge\n");
		return 1;
	}

	for (i = 0; i < npkgs; i++) {
		if (!pkgs[i].need_emit)
			continue;
		if (pkg_write_build_meta(c, pkgs, npkgs, i, knobs) != 0) {
			fprintf(stderr, "modc: failed to write meta for %s\n", pkgs[i].id);
			return 1;
		}
	}
	for (i = 0; i < npkgs; i++) {
		snprintf(what, sizeof(what), "pkg %s", pkgs[i].id);
		cache_log(o->verbose, pkgs[i].need_emit ? "miss" : "hit", what);
	}
	return 0;
}

/* Discovered package graph + cache fingerprints shared by check and build. */
typedef struct {
	char** files;
	int nfiles;
	BuildPkg pkgs[MaxCachePkgs];
	int npkgs;
	uint64_t knobs;
	char crooot[HOST_PATH_MAX];
	char projroot[HOST_PATH_MAX];
} PkgCacheState;

static void
pkgs_free_files(PkgCacheState* st) {
	int i;

	if (st == NULL || st->files == NULL)
		return;
	for (i = 0; i < st->nfiles; i++)
		free(st->files[i]);
	free(st->files);
	st->files = NULL;
	st->nfiles = 0;
}

static int
pkgs_all_cache_hit(BuildPkg* pkgs, int npkgs) {
	int i;

	for (i = 0; i < npkgs; i++)
		if (!pkgs[i].hit)
			return 0;
	return 1;
}

static void
pkgs_log_each(CliOpts* o, BuildPkg* pkgs, int npkgs, const char* hitmiss) {
	int i;
	char what[HOST_PATH_MAX];

	for (i = 0; i < npkgs; i++) {
		snprintf(what, sizeof(what), "pkg %s", pkgs[i].id);
		cache_log(o->verbose, hitmiss, what);
	}
}

/*
 * Discover import graph, fingerprint packages, fill import reachability and
 * cache keys/hits. require_obj=1 for build (pkg.o required); 0 for check.
 * On failure frees files and returns 1; on success caller owns st->files.
 */
static int
pkgs_cache_prepare(Compiler* c, const char* path, int require_obj, PkgCacheState* st) {
	int i;

	memset(st, 0, sizeof(*st));
	if (pkg_discover(c, path, &st->files, &st->nfiles))
		return 1;
	if (pkg_ensure(c, "crash", &st->files, &st->nfiles)) {
		pkgs_free_files(st);
		return 1;
	}
	cache_root_for(path, st->crooot, sizeof(st->crooot));
	cache_project_root(path, st->projroot, sizeof(st->projroot));
	if (collect_build_pkgs(st->files, st->nfiles, st->pkgs, &st->npkgs, st->projroot) !=
	    0) {
		fprintf(stderr, "modc: too many packages in build graph\n");
		pkgs_free_files(st);
		return 1;
	}
	st->knobs = hash_compile_env(c, st->projroot);
	for (i = 0; i < st->npkgs; i++) {
		st->pkgs[i].vendored = pkg_is_vendored(st->pkgs[i].dir, st->projroot);
		st->pkgs[i].impl = pkg_impl_hash_cached(&st->pkgs[i], st->files, st->nfiles,
							st->projroot, st->crooot);
	}
	if (pkg_fill_imports(c, st->pkgs, st->npkgs, st->files, st->nfiles) != 0) {
		pkgs_free_files(st);
		return 1;
	}
	pkg_fill_keys(c, st->pkgs, st->npkgs, st->knobs, require_obj);
	return 0;
}

/*
 * Typecheck path using per-package .pkg.mc when valid (no codegen).
 * Hits require .pkg.mc + .pkg.meta [build]/[needs] under knobs that include
 * this modc binary stamp — a rebuilt driver cannot inherit a hit from an
 * older binary. Writes iface artifacts for miss packages so later
 * checks/builds can reuse them.
 *
 * Uses parse_only: decls + signatures (skip function bodies). That matches
 * FPC's interface parse — enough for API/iface/cascade; body diagnostics
 * (dead-init, releases, bounds of locals, …) run on build. Selftest still
 * uses compile_file (full parse). Do not treat check as a full safety gate.
 */
int
compile_check(Compiler* c, CliOpts* o, const char* path) {
	PkgCacheState st;
	int r, prev_parse_only;

	prev_parse_only = c->opt.parse_only;
	c->opt.parse_only = 1;
	if (pkgs_cache_prepare(c, path, 0, &st) != 0) {
		c->opt.parse_only = prev_parse_only;
		return 1;
	}
	if (pkgs_all_cache_hit(st.pkgs, st.npkgs)) {
		pkgs_log_each(o, st.pkgs, st.npkgs, "hit");
		cache_log(o->verbose, "hit", "graph");
		progress_msg("up to date (API check)");
		pkgs_free_files(&st);
		c->opt.parse_only = prev_parse_only;
		return 0;
	}
	cache_log(o->verbose, "miss", "graph");
	r = pkgs_frontend_compile(c, o, st.pkgs, st.npkgs, st.files, st.nfiles, st.knobs,
				  st.projroot);
	pkgs_free_files(&st);
	c->opt.parse_only = prev_parse_only;
	return r;
}

/* Compile path → per-package .pkg.o (c_sources folded in) → link into outpath.
 * dir is an existing temp directory; caller cleans artifacts. */
int
compile_link_exe(Compiler* c, CliOpts* o, const char* path, const char* dir, const char* outpath) {
	PkgCacheState st;
	int i, r;
	char objs[MaxCachePkgs][512];

	/* Fail fast on bad MODC_CC / MODC_QBE even on a full graph cache hit. */
	if (tool_cc(c) == NULL || tool_qbe() == NULL)
		return 1;
#ifndef _WIN32
	if (c->opt.target == TargetWindows) {
		const char* cc;

		cc = tool_cc(c);
		if (cc && strchr(cc, '/') && !host_is_file(cc)) {
			fprintf(stderr,
				"modc: --target=windows needs x86_64-w64-mingw32-gcc (or set MODC_CC)\n");
			return 1;
		}
	}
#endif
	if (pkgs_cache_prepare(c, path, 1, &st) != 0)
		return 1;

	if (pkgs_all_cache_hit(st.pkgs, st.npkgs)) {
		pkgs_log_each(o, st.pkgs, st.npkgs, "hit");
		for (i = 0; i < st.npkgs; i++)
			snprintf(objs[i], sizeof(objs[i]), "%s", st.pkgs[i].objpath);
		cache_log(o->verbose, "hit", "graph");
		c->paths.c_libs_len = 0;
		c->paths.frameworks_len = 0;
		c->paths.csources_len = 0;
		pkgs_merge_link_metas(c, st.pkgs, st.npkgs);
		if (host_is_file(outpath) &&
		    outfile_newer_than_objs(outpath, st.pkgs, st.npkgs)) {
			progress_msg("up to date");
			pkgs_free_files(&st);
			return 0;
		}
		progress_build_start();
		r = link_modc_objs(c, o, outpath, objs, st.npkgs, NULL, 0);
		if (r == 0)
			progress_build_done();
		else
			progress_build_cancel();
		pkgs_free_files(&st);
		return r;
	}

	cache_log(o->verbose, "miss", "graph");
	progress_build_start();
	{
		int pi;
		PkgAsmJob miss[MaxCachePkgs];
		int nmiss;

		/* Hit packages are not re-preprocessed; seed link pragmas from
		 * their .pkg.meta [link], then let need_emit PP add via pkg_add_*. */
		c->paths.c_libs_len = 0;
		c->paths.frameworks_len = 0;
		c->paths.csources_len = 0;
		for (pi = 0; pi < st.npkgs; pi++)
			if (!st.pkgs[pi].need_emit)
				pkg_merge_link_meta(c, st.pkgs[pi].metapath,
						    st.pkgs[pi].dir);

		r = pkgs_frontend_compile(c, o, st.pkgs, st.npkgs, st.files, st.nfiles,
					  st.knobs, st.projroot);
		if (r != 0) {
			progress_build_cancel();
			pkgs_free_files(&st);
			return 1;
		}

		for (i = 0; i < st.npkgs; i++)
			snprintf(objs[i], sizeof(objs[i]), "%s", st.pkgs[i].objpath);
		nmiss = 0;
		for (i = 0; i < st.npkgs; i++) {
			if (!st.pkgs[i].need_emit)
				continue;
			miss[nmiss].pkg = &st.pkgs[i];
			miss[nmiss].pkg_index = i;
			snprintf(miss[nmiss].asmpath, sizeof(miss[nmiss].asmpath),
				 "%s/pkg%d.s", dir, i);
			snprintf(miss[nmiss].obj, sizeof(miss[nmiss].obj), "%s/pkg%d.o", dir,
				 i);
			nmiss++;
		}
		/*
		 * TTY: assemble one package at a time so held lines + deltas are
		 * attributable. Non-TTY: emit all, then parallel qbe/cc.
		 */
		if (host_stderr_tty()) {
			for (i = 0; i < nmiss; i++) {
				char pname[HOST_PATH_MAX];

				pkg_progress_name(miss[i].pkg, st.projroot, outpath, pname,
						  sizeof(pname));
				progress_begin("assembling %s", pname);
				if (emit_pkg_qbe(c, miss[i].pkg, miss[i].pkg_index, dir,
						 miss[i].qbe, sizeof(miss[i].qbe)) != 0) {
					progress_end();
					progress_build_cancel();
					pkgs_free_files(&st);
					return 1;
				}
				if (assemble_pkg_jobs(c, o, &miss[i], 1, dir) != 0) {
					progress_end();
					progress_build_cancel();
					pkgs_free_files(&st);
					return 1;
				}
				progress_end();
			}
		} else {
			for (i = 0; i < nmiss; i++) {
				if (emit_pkg_qbe(c, miss[i].pkg, miss[i].pkg_index, dir,
						 miss[i].qbe, sizeof(miss[i].qbe)) != 0) {
					progress_build_cancel();
					pkgs_free_files(&st);
					return 1;
				}
			}
			if (assemble_pkg_jobs(c, o, miss, nmiss, dir) != 0) {
				progress_build_cancel();
				pkgs_free_files(&st);
				return 1;
			}
		}
	}
	pkgs_free_files(&st);

	r = link_modc_objs(c, o, outpath, objs, st.npkgs, NULL, 0);
	if (r == 0)
		progress_build_done();
	else
		progress_build_cancel();
	return r;
}

/* Heap string: build/<stem> from a file or directory path (build/ is gitignored). */
char*
default_out_name(const char* path, int target) {
	char buf[PATH_MAX], cwd[PATH_MAX];
	const char *base, *dot;
	size_t n;
	char* name;
	char* out;
	int want_exe;
	const char* fallback;

	want_exe = (target == TargetWindows);
#ifdef _WIN32
	want_exe = 1;
#endif
	fallback = want_exe ? "a.exe" : "a.out";
	if (path == NULL || path[0] == 0)
		path = ".";
	if (strcmp(path, ".") == 0 || strcmp(path, "./") == 0 || strcmp(path, ".\\") == 0) {
		if (host_getcwd(cwd, sizeof(cwd)) != 0) {
			out = xmalloc(6 + strlen(fallback) + 1);
			memcpy(out, "build/", 6);
			memcpy(out + 6, fallback, strlen(fallback) + 1);
			return out;
		}
		path = cwd;
	}
	snprintf(buf, sizeof(buf), "%s", path);
	n = strlen(buf);
	while (n > 1 && host_path_is_sep((unsigned char)buf[n - 1])) {
		buf[n - 1] = 0;
		n--;
	}
	base = host_path_last_sep(buf);
	base = base ? base + 1 : buf;
	if (base[0] == 0)
		base = fallback;
	dot = strrchr(base, '.');
	if (dot && strcmp(dot, ".mc") == 0 && dot > base)
		name = xstrndup(base, (size_t)(dot - base));
	else
		name = xstrdup(base);
	if (want_exe) {
		n = strlen(name);
		if (n < 4 || strcmp(name + n - 4, ".exe") != 0) {
			char* with;

			with = xmalloc(n + 5);
			memcpy(with, name, n);
			memcpy(with + n, ".exe", 5);
			free(name);
			name = with;
		}
	}
	n = strlen(name);
	out = xmalloc(6 + n + 1);
	memcpy(out, "build/", 6);
	memcpy(out + 6, name, n + 1);
	free(name);
	return out;
}

// Default build input to "." when none was given.
void
ensure_build_root(CliOpts* o) {
	if (o->files_len == 0)
		add_file(o, ".");
}

// Remove the entire temp build directory (pkg*.o, foreign*.o, etc.).
void
cleanup_tmpdir(const char* dir) {
	if (dir && dir[0])
		(void)host_rmtree(dir);
}

